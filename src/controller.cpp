#include "controller.h"

#include "input_backend.h"
#include "log.h"
#include "memory_backend.h"
#include "status_writer.h"

#include <cmath>
#include <cstdio>

namespace tc {

Controller &Controller::instance() {
    static Controller inst;
    return inst;
}

void Controller::configure(const Config &cfg) {
    requireStopped_ = cfg.getBool("geral", "exigir_parado", true);
    maxSpeedKmh_ = cfg.getDouble("geral", "velocidade_max_kmh", 3.0);

    registry_.setDollyKeywords(cfg.getList("deteccao", "palavras_dolly"));

    // Ordem = preferencia. O backend oficial vem primeiro sempre que suportar.
    backends_ = {&InputBackend::instance(), &MemoryBackend::instance()};

    hotkeys_.add("desengatar_tudo", cfg.get("atalhos", "desengatar_tudo", "CTRL+SHIFT+D"));
    hotkeys_.add("desengatar_ultimo", cfg.get("atalhos", "desengatar_ultimo", "CTRL+SHIFT+U"));
    hotkeys_.add("desengatar_selecionado", cfg.get("atalhos", "desengatar_selecionado", "CTRL+SHIFT+X"));
    hotkeys_.add("engatar_selecionado", cfg.get("atalhos", "engatar_selecionado", "CTRL+SHIFT+E"));
    hotkeys_.add("selecionar_proximo", cfg.get("atalhos", "selecionar_proximo", "CTRL+SHIFT+RIGHT"));
    hotkeys_.add("selecionar_anterior", cfg.get("atalhos", "selecionar_anterior", "CTRL+SHIFT+LEFT"));
    hotkeys_.add("listar", cfg.get("atalhos", "listar", "CTRL+SHIFT+L"));
}

void Controller::startHotkeys() {
    hotkeys_.start([this](const std::string &a) { enqueue(a); });
}

void Controller::stopHotkeys() { hotkeys_.stop(); }

void Controller::enqueue(const std::string &action) {
    std::lock_guard<std::mutex> lock(queueMutex_);
    if (queue_.size() < 16) queue_.push_back(action);
}

void Controller::tick() {
    std::deque<std::string> pending;
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        pending.swap(queue_);
    }
    for (const auto &a : pending) execute(a);

    const unsigned rev = registry_.revision();
    if (rev != lastRevision_ || statusDirty_) {
        lastRevision_ = rev;
        statusDirty_ = false;
        writeStatus(registry_.snapshot(), selectedJoint_, lastMessage_,
                    MemoryBackend::instance().available());
    }
}

void Controller::report(const std::string &msg) {
    lastMessage_ = msg;
    statusDirty_ = true;
    log::info("%s", msg.c_str());
}

bool Controller::checkSpeed() {
    if (!requireStopped_) return true;
    const double kmh = std::fabs(speedMs_.load()) * 3.6;
    if (kmh > maxSpeedKmh_) {
        char buf[128];
        std::snprintf(buf, sizeof(buf), "Pare o caminhao (%.1f km/h > %.1f km/h).", kmh, maxSpeedKmh_);
        report(buf);
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

void Controller::detachJoint(const Joint &j) {
    if (!j.coupled) {
        report("Articulacao " + std::to_string(j.index) + " ja esta desengatada.");
        return;
    }
    if (!checkSpeed()) return;
    ICouplingBackend *b = pickBackend(j);
    if (!b) {
        report(std::string("Sem backend para desengatar '") + toString(j.kind) +
               "' (articulacao " + std::to_string(j.index) + "). Requer backend de memoria.");
        return;
    }
    const CouplingResult r = b->detach(j);
    report(std::string("Desengatar '") + toString(j.kind) + "' via " + b->name() + ": " + toString(r));
}

void Controller::attachJoint(const Joint &j) {
    if (j.coupled) {
        report("Articulacao " + std::to_string(j.index) + " ja esta engatada.");
        return;
    }
    if (!checkSpeed()) return;
    ICouplingBackend *b = pickBackend(j);
    if (!b) {
        report(std::string("Sem backend para engatar '") + toString(j.kind) +
               "' (articulacao " + std::to_string(j.index) + "). Requer backend de memoria.");
        return;
    }
    const CouplingResult r = b->attach(j);
    report(std::string("Engatar '") + toString(j.kind) + "' via " + b->name() + ": " + toString(r));
}

void Controller::execute(const std::string &action) {
    if (paused_) return;
    const CombinationSnapshot snap = registry_.snapshot();
    const int count = static_cast<int>(snap.joints.size());

    if (action == "listar") {
        log::info("Composicao: %s (chain=%s), %d articulacao(oes):", snap.combinationName.c_str(),
                  snap.chainType.c_str(), count);
        for (const auto &u : snap.units)
            log::info("  unidade %d [%s] %s", u.index, toString(u.kind), u.displayName().c_str());
        for (const auto &j : snap.joints)
            log::info("  %s articulacao %d: %d<->%d %s - %s", j.index == selectedJoint_ ? ">" : " ",
                      j.index, j.front, j.rear, toString(j.kind), j.coupled ? "engatada" : "solta");
        return;
    }

    if (action == "selecionar_proximo" || action == "selecionar_anterior") {
        if (count == 0) { selectedJoint_ = 0; report("Nenhuma articulacao detectada."); return; }
        selectedJoint_ += (action == "selecionar_proximo") ? 1 : -1;
        if (selectedJoint_ >= count) selectedJoint_ = 0;
        if (selectedJoint_ < 0) selectedJoint_ = count - 1;
        const Joint &j = snap.joints[selectedJoint_];
        report("Selecionada articulacao " + std::to_string(j.index) + ": " + toString(j.kind) + " (" +
               snap.units[j.front].displayName() + " <-> " + snap.units[j.rear].displayName() + ")");
        return;
    }

    if (action == "desengatar_tudo") {
        if (count == 0 || !snap.joints[0].coupled) { report("Nenhum reboque engatado no cavalo."); return; }
        detachJoint(snap.joints[0]);
        return;
    }

    if (action == "desengatar_ultimo") {
        for (int i = count - 1; i >= 0; --i) {
            if (snap.joints[i].coupled) { detachJoint(snap.joints[i]); return; }
        }
        report("Nenhum reboque engatado.");
        return;
    }

    if (action == "desengatar_selecionado") {
        if (selectedJoint_ >= count) { report("Articulacao selecionada nao existe mais."); return; }
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

    log::warn("Acao desconhecida: %s", action.c_str());
}

} // namespace tc
