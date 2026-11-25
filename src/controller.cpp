#include "controller.h"

#include "app_launcher.h"
#include "blinker_sound.h"
#include "command_inbox.h"
#include "input_backend.h"
#include "log.h"
#include "memory_backend.h"
#include "shared/ipc.h"
#include "status_writer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace tc {

namespace {
// Intervalo entre consultas ao command.txt.
constexpr unsigned long long kInboxPollMs = 100;
// Intervalo entre checagens de mudanca no trailer_control.ini.
constexpr unsigned long long kConfigWatchMs = 1000;
} // namespace

Controller &Controller::instance() {
    static Controller inst;
    return inst;
}

std::wstring Controller::configPath() { return pluginDirectory() + L"\\trailer_control.ini"; }

// Aplica o .ini. Chamado na inicializacao e no comando "recarregar" (vindo da tela de
// configuracoes do app). Sempre na thread do jogo.
void Controller::configure(const Config &cfg) {
    requireStopped_ = cfg.getBool("geral", "exigir_parado", true);
    maxSpeedKmh_ = cfg.getDouble("geral", "velocidade_max_kmh", 3.0);

    registry_.setDollyKeywords(cfg.getList("deteccao", "palavras_dolly"));

    // Ordem = preferencia. O backend oficial vem primeiro sempre que suportar.
    backends_ = {&InputBackend::instance(), &MemoryBackend::instance()};

    MemoryBackend &mem = MemoryBackend::instance();
    mem.setRegistry(&registry_);
    mem.initialize(cfg.getBool("memoria", "habilitado", false), cfg.get("memoria", "versao_jogo"),
                   cfg.getDouble("memoria", "distancia_max_engate_m", 1.5));
    allowConvoy_ = cfg.getBool("memoria", "permitir_convoy", true);
    lastMultiplayerCheckMs_ = 0; // reavalia ja no proximo tick

    BlinkerSound::instance().configure(cfg.getBool("som", "alarme_seta", true),
                                       static_cast<int>(cfg.getDouble("som", "volume", 60)));

    // Unico atalho: abrir a tela. Todo o resto e feito pela tela.
    appHotkey_ = cfg.get("atalhos", "abrir_tela", "CTRL+MINUS");
    const bool restart = hotkeysStarted_;
    hotkeys_.clear();
    if (!hotkeys_.add("abrir_tela", appHotkey_)) {
        log::warn("Atalho para abrir a tela invalido (%s); usando CTRL+MINUS.", appHotkey_.c_str());
        appHotkey_ = "CTRL+MINUS";
        hotkeys_.add("abrir_tela", appHotkey_);
    }
    if (restart) startHotkeys();
}

void Controller::startHotkeys() {
    hotkeysStarted_ = true;
    // Thread do teclado so enfileira; a tela e aberta no tick (thread do jogo).
    hotkeys_.start([this](const std::string &a) { enqueue(a); });
}

void Controller::stopHotkeys() {
    hotkeysStarted_ = false;
    hotkeys_.stop();
}

namespace {
// Acoes que mexem no caminhao: com o jogo pausado (ex.: a tela do app na frente), ficam na
// fila e rodam quando o jogo voltar.
bool isCouplingAction(const std::string &line) {
    const std::string verb = line.substr(0, line.find(' '));
    return verb == "desengatar" || verb == "engatar" || verb == "desengatar_tudo";
}
constexpr unsigned long long kDeferredMaxMs = 30000;
} // namespace

void Controller::enqueue(const std::string &action, long long seq) {
    std::lock_guard<std::mutex> lock(queueMutex_);
    if (queue_.size() < 32) queue_.push_back({action, seq});
}

CombinationSnapshot Controller::currentSnapshot() const {
    CombinationSnapshot snap = registry_.snapshot();
    // Articulacoes internas: a telemetria nao sabe que soltamos; vale o que esta na memoria.
    const MemoryBackend &mem = MemoryBackend::instance();
    if (mem.available()) {
        for (Joint &j : snap.joints) {
            bool coupled = false;
            if (j.index >= 1 && mem.probeCoupled(j.index, coupled)) {
                j.coupled = coupled;
                j.fromMemory = true;
            }
        }
    }
    return snap;
}

void Controller::checkMultiplayer(unsigned long long now) {
    if (now - lastMultiplayerCheckMs_ < 2000) return;
    lastMultiplayerCheckMs_ = now;
    std::string why;
    if (GetModuleHandleW(L"core_ets2mp.dll") || GetModuleHandleW(L"core_atsmp.dll")) why = "TruckersMP";
    // Convoy (multiplayer oficial da SCS) e liberado por padrao; o TruckersMP nunca (anticheat/ban).
    else if (multiplayerOffset_.load() != 0 && !allowConvoy_) why = "Convoy";
    MemoryBackend::instance().setMultiplayer(why);
}

unsigned long long Controller::configStamp() {
    WIN32_FILE_ATTRIBUTE_DATA d;
    if (!GetFileAttributesExW(configPath().c_str(), GetFileExInfoStandard, &d)) return 0;
    return (static_cast<unsigned long long>(d.ftLastWriteTime.dwHighDateTime) << 32) | d.ftLastWriteTime.dwLowDateTime;
}

bool Controller::reloadConfig() {
    const unsigned long long stamp = configStamp(); // antes de ler: mudanca no meio gera nova recarga
    Config cfg;
    if (!cfg.load(configPath())) return false;
    configStamp_ = stamp;
    configure(cfg);
    return true;
}

void Controller::watchConfig(unsigned long long now) {
    if (now - lastConfigCheckMs_ < kConfigWatchMs) return;
    lastConfigCheckMs_ = now;
    const unsigned long long stamp = configStamp();
    if (stamp == 0) return;
    if (configStamp_ == 0) {
        configStamp_ = stamp; // primeira vista: o que ja foi carregado na inicializacao
        return;
    }
    if (stamp != configStamp_ && reloadConfig()) log::info("trailer_control.ini mudou: configuracao recarregada.");
}

void Controller::tick() {
    const unsigned long long now = GetTickCount64();
    checkMultiplayer(now);
    watchConfig(now);
    if (now - lastInboxPollMs_ >= kInboxPollMs) {
        lastInboxPollMs_ = now;
        // Mesmo seq de novo = reenvio do app (acrescentou num command.txt que ainda nao tinhamos
        // pego); ignora. So compara com os ultimos vistos: nao depende do relogio do app.
        for (const ExternalCommand &c : takeExternalCommands()) {
            if (std::find(recentSeqs_.begin(), recentSeqs_.end(), c.seq) != recentSeqs_.end()) continue;
            recentSeqs_.push_back(c.seq);
            if (recentSeqs_.size() > 64) recentSeqs_.pop_front();
            enqueue(c.action, c.seq);
        }
    }

    std::deque<Pending> pending;
    // Comandos que esperavam o jogo despausar vao na frente.
    if (!paused_ && !deferred_.empty()) {
        for (const Deferred &d : deferred_) {
            if (now - d.since > kDeferredMaxMs) {
                report("Comando '" + d.cmd.action + "' expirou: o jogo ficou pausado tempo demais.", false);
                continue;
            }
            Pending p = d.cmd;
            p.seq = -p.seq; // ja respondido como "na fila"; nao reenvia a confirmacao
            pending.push_back(p);
        }
        deferred_.clear();
    }
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        for (auto &p : queue_) pending.push_back(std::move(p));
        queue_.clear();
    }
    for (const auto &p : pending) {
        if (paused_ && p.seq > 0 && isCouplingAction(p.action)) {
            deferred_.push_back({p, now});
            report("Na fila: '" + p.action + "' sera feito quando voce voltar ao jogo.");
            lastCommandSeq_ = p.seq;
            lastCommandResult_ = lastMessage_;
            lastCommandOk_ = true;
            lastStatusKey_.clear();
            continue;
        }
        if (p.seq > 0) {
            lastMessage_.clear();
            lastOk_ = true;
        }
        execute(p.action);
        if (p.seq > 0) {
            lastCommandSeq_ = p.seq;
            lastCommandResult_ = lastMessage_.empty() ? "ok" : lastMessage_;
            lastCommandOk_ = lastOk_;
            lastStatusKey_.clear(); // forca regravar
        }
    }

    const CombinationSnapshot snap = currentSnapshot();
    // "Qualquer reboque": basta ter algo engatado no cavalo.
    BlinkerSound::instance().update(paused_, !snap.joints.empty() && snap.joints[0].coupled);
    writeStatusIfNeeded(snap);
}

void Controller::writeStatusIfNeeded(const CombinationSnapshot &snap) {
    // Chave do que o app ve; muda -> regrava na hora. Sem mudanca -> batimento a cada 1 s.
    std::string key = std::to_string(registry_.revision()) + "|" + std::to_string(selectedJoint_) + "|" +
                      std::to_string(paused_.load()) + "|" + lastMessage_ + "|";
    for (const Joint &j : snap.joints) key += j.coupled ? '1' : '0';
    key += InputBackend::instance().deviceActive() ? 'a' : 'i';
    key += std::to_string(deferred_.size());
    key += BlinkerSound::instance().enabled() ? 's' : 'n';
    key += std::to_string(BlinkerSound::instance().volume());

    const unsigned long long now = GetTickCount64();
    if (key == lastStatusKey_ && now - lastStatusWriteMs_ < ipc::kHeartbeatMs) return;
    lastStatusKey_ = key;
    lastStatusWriteMs_ = now;

    StatusInfo info;
    info.snap = &snap;
    info.selectedJoint = selectedJoint_;
    info.lastMessage = lastMessage_;
    info.lastOk = lastOk_;
    info.lastCommandSeq = lastCommandSeq_;
    info.lastCommandResult = lastCommandResult_;
    info.lastCommandOk = lastCommandOk_;
    info.paused = paused_;
    info.speedKmh = std::fabs(speedMs_.load()) * 3.6;
    info.requireStopped = requireStopped_;
    info.maxSpeedKmh = maxSpeedKmh_;
    info.inputReady = InputBackend::instance().available();
    info.inputActive = InputBackend::instance().deviceActive();
    const MemoryBackend &mem = MemoryBackend::instance();
    info.memoryReady = mem.available();
    info.memoryStatus = mem.statusText();
    info.exeVersion = mem.exeVersion();
    {
        const std::wstring p = configPath();
        const int n = WideCharToMultiByte(CP_UTF8, 0, p.c_str(), -1, nullptr, 0, nullptr, nullptr);
        std::string utf8(n > 0 ? static_cast<size_t>(n - 1) : 0, '\0');
        if (n > 1) WideCharToMultiByte(CP_UTF8, 0, p.c_str(), -1, &utf8[0], n, nullptr, nullptr);
        info.configPath = utf8;
    }
    info.appHotkey = appHotkey_;
    info.queuedCommands = deferred_.size();
    info.blinkerSound = BlinkerSound::instance().enabled();
    info.blinkerVolume = BlinkerSound::instance().volume();
    writeStatus(info);
}

void Controller::report(const std::string &msg, bool ok) {
    lastMessage_ = msg;
    lastOk_ = ok;
    if (ok) log::info("%s", msg.c_str());
    else log::warn("%s", msg.c_str());
}

bool Controller::checkSpeed() {
    if (!requireStopped_) return true;
    const double kmh = std::fabs(speedMs_.load()) * 3.6;
    if (kmh > maxSpeedKmh_) {
        char buf[128];
        std::snprintf(buf, sizeof(buf), "Pare o caminhao (%.1f km/h > %.1f km/h).", kmh, maxSpeedKmh_);
        report(buf, false);
        return false;
    }
    return true;
}

ICouplingBackend *Controller::pickBackend(const Joint &j) {
    for (auto *b : backends_) {
        if (b->available() && b->supports(j)) return b;
    }
    return nullptr;
}

namespace {
std::string withDetail(const std::string &base, ICouplingBackend *b) {
    if (b != &MemoryBackend::instance()) return base;
    const std::string &err = MemoryBackend::instance().lastError();
    return err.empty() ? base : base + " (" + err + ")";
}
} // namespace

void Controller::detachJoint(const Joint &j) {
    if (!j.coupled) {
        report("Articulacao " + std::to_string(j.index) + " ja esta desengatada.", false);
        return;
    }
    if (!checkSpeed()) return;
    ICouplingBackend *b = pickBackend(j);
    if (!b) {
        report(std::string("Sem backend para desengatar '") + toString(j.kind) + "' (articulacao " +
                   std::to_string(j.index) + "): " + MemoryBackend::instance().statusText() + ".",
               false);
        return;
    }
    const CouplingResult r = b->detach(j);
    report(withDetail(std::string("Desengatar '") + toString(j.kind) + "' via " + b->name() + ": " +
                          toString(r),
                      b),
           r == CouplingResult::Requested);
}

void Controller::attachJoint(const Joint &j) {
    if (j.coupled) {
        report("Articulacao " + std::to_string(j.index) + " ja esta engatada.", false);
        return;
    }
    if (!checkSpeed()) return;
    ICouplingBackend *b = pickBackend(j);
    if (!b) {
        report(std::string("Sem backend para engatar '") + toString(j.kind) + "' (articulacao " +
                   std::to_string(j.index) + "): " + MemoryBackend::instance().statusText() + ".",
               false);
        return;
    }
    const CouplingResult r = b->attach(j);
    report(withDetail(std::string("Engatar '") + toString(j.kind) + "' via " + b->name() + ": " +
                          toString(r),
                      b),
           r == CouplingResult::Requested);
}

void Controller::execute(const std::string &actionLine) {
    // "desengatar 2" -> verbo "desengatar", argumento 2
    std::string action = actionLine;
    int arg = -1;
    const auto space = actionLine.find(' ');
    if (space != std::string::npos) {
        action = actionLine.substr(0, space);
        arg = std::atoi(actionLine.c_str() + space + 1);
    }

    if (action == "recarregar") {
        if (!reloadConfig()) {
            report("Nao consegui ler o trailer_control.ini.", false);
            return;
        }
        report("Configuracoes aplicadas.");
        return;
    }
    if (action == "abrir_tela") {
        report(showControlApp());
        return;
    }
    if (action == "diagnostico") {
        const std::string summary = MemoryBackend::instance().diagnose();
        report("Diagnostico no game.log.txt: " + summary + ".");
        return;
    }
    if (paused_ && action != "listar" && action != "selecionar") {
        report("Jogo pausado ou no menu: comando ignorado.", false);
        return;
    }
    const CombinationSnapshot snap = currentSnapshot();
    const int count = static_cast<int>(snap.joints.size());

    if (action == "listar") {
        log::info("Composicao: %s (chain=%s), %d articulacao(oes):", snap.combinationName.c_str(),
                  snap.chainType.c_str(), count);
        for (const auto &u : snap.units)
            log::info("  unidade %d [%s] %s", u.index, toString(u.kind), u.displayName().c_str());
        for (const auto &j : snap.joints)
            log::info("  %s articulacao %d: %d<->%d %s - %s%s", j.index == selectedJoint_ ? ">" : " ",
                      j.index, j.front, j.rear, toString(j.kind), j.coupled ? "engatada" : "solta",
                      j.fromMemory ? " (memoria)" : "");
        report("Composicao escrita no game.log.txt.");
        return;
    }

    if (action == "selecionar_proximo" || action == "selecionar_anterior" || action == "selecionar") {
        if (count == 0) {
            selectedJoint_ = 0;
            report("Nenhuma articulacao detectada.", false);
            return;
        }
        if (action == "selecionar") {
            if (arg < 0 || arg >= count) {
                report("Articulacao " + std::to_string(arg) + " nao existe.", false);
                return;
            }
            selectedJoint_ = arg;
        } else {
            selectedJoint_ += (action == "selecionar_proximo") ? 1 : -1;
            if (selectedJoint_ >= count) selectedJoint_ = 0;
            if (selectedJoint_ < 0) selectedJoint_ = count - 1;
        }
        const Joint &j = snap.joints[selectedJoint_];
        report("Selecionada articulacao " + std::to_string(j.index) + ": " + toString(j.kind) + " (" +
               snap.units[j.front].displayName() + " <-> " + snap.units[j.rear].displayName() + ")");
        return;
    }

    if (action == "engatar" && arg == 0 && count == 0) {
        execute("engatar_selecionado"); // sem reboque: engate nativo pega o que estiver atras
        return;
    }
    if (action == "desengatar" || action == "engatar") {
        if (arg < 0 || arg >= count) {
            report("Articulacao " + std::to_string(arg) + " nao existe na composicao atual.", false);
            return;
        }
        selectedJoint_ = arg;
        if (action == "desengatar") detachJoint(snap.joints[arg]);
        else attachJoint(snap.joints[arg]);
        return;
    }

    if (action == "desengatar_tudo") {
        if (count == 0 || !snap.joints[0].coupled) {
            report("Nenhum reboque engatado no cavalo.", false);
            return;
        }
        detachJoint(snap.joints[0]);
        return;
    }

    if (action == "desengatar_ultimo") {
        for (int i = count - 1; i >= 0; --i) {
            if (snap.joints[i].coupled && pickBackend(snap.joints[i])) {
                detachJoint(snap.joints[i]);
                return;
            }
        }
        report("Nenhuma articulacao engatada que possa ser solta.", false);
        return;
    }

    if (action == "desengatar_selecionado") {
        if (selectedJoint_ >= count) {
            report("Articulacao selecionada nao existe mais.", false);
            return;
        }
        detachJoint(snap.joints[selectedJoint_]);
        return;
    }

    if (action == "engatar_selecionado") {
        // Sem reboque no cavalo: o engate nativo pega o reboque que estiver atras.
        if (count == 0 || selectedJoint_ >= count) {
            Joint truckJoint;
            truckJoint.index = 0;
            truckJoint.front = 0;
            truckJoint.rear = 1;
            truckJoint.kind = JointKind::TruckFifthWheel;
            truckJoint.coupled = false;
            attachJoint(truckJoint);
            return;
        }
        attachJoint(snap.joints[selectedJoint_]);
        return;
    }

    report("Acao desconhecida: " + action, false);
}

} // namespace tc
