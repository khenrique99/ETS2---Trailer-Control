#include "model.h"

#include "shared/ipc.h"

#include <cstdlib>

namespace tc::app {

std::wstring widen(const std::string &utf8) {
    if (utf8.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), &w[0], n);
    return w;
}

bool heartbeatFresh(unsigned long long ts, unsigned long long nowMs) {
    if (ts == 0) return false;
    if (ts > nowMs) return ts - nowMs <= 2000; // relogio do plugin um pouco adiantado
    return nowMs - ts <= ipc::kStaleMs;
}

bool stateFromJson(const json::Value &v, unsigned long long nowMs, GameState &s) {
    if (v.type != json::Value::Type::Object) return false;
    s = GameState{};
    s.fileFound = true;
    s.connected = heartbeatFresh(static_cast<unsigned long long>(v["timestamp_ms"].asNumber(0)), nowMs);
    s.paused = v["paused"].asBool(true);
    s.speedKmh = v["speed_kmh"].asNumber(0);
    s.maxSpeedKmh = v["max_speed_kmh"].asNumber(3);
    s.requireStopped = v["require_stopped"].asBool(true);
    s.inputReady = v["input_backend"].asBool(false);
    s.inputActive = v["input_active"].asBool(false);
    s.memoryReady = v["memory_backend"].asBool(false);
    s.memoryStatus = widen(v["memory_status"].asString());
    s.configPath = widen(v["config_path"].asString());
    s.appHotkey = widen(v["app_hotkey"].asString());
    s.queuedCommands = static_cast<int>(v["queued_commands"].asInt(0));
    s.exeVersion = widen(v["exe_version"].asString());
    s.pluginVersion = widen(v["plugin_version"].asString());
    s.combination = widen(v["combination"].asString());
    s.lastMessage = widen(v["last_message"].asString());
    s.lastOk = v["last_ok"].asBool(true);
    s.lastCommandSeq = v["last_command"]["seq"].asInt(0);
    s.lastCommandOk = v["last_command"]["ok"].asBool(true);
    s.lastCommandResult = widen(v["last_command"]["result"].asString());
    s.selectedJoint = static_cast<int>(v["selected_joint"].asInt(0));

    const json::Value &units = v["units"];
    for (size_t i = 0; i < units.size(); ++i) {
        UnitView u;
        u.index = static_cast<int>(units[i]["index"].asInt(static_cast<long long>(i)));
        u.kind = widen(units[i]["kind"].asString());
        u.name = widen(units[i]["name"].asString());
        s.units.push_back(u);
    }
    const json::Value &joints = v["joints"];
    for (size_t i = 0; i < joints.size(); ++i) {
        JointView j;
        j.index = static_cast<int>(joints[i]["index"].asInt(static_cast<long long>(i)));
        j.front = static_cast<int>(joints[i]["front"].asInt(0));
        j.rear = static_cast<int>(joints[i]["rear"].asInt(1));
        j.kind = widen(joints[i]["kind"].asString());
        j.coupled = joints[i]["coupled"].asBool(false);
        j.fromMemory = joints[i]["source"].asString() == "memoria";
        j.backend = widen(joints[i]["backend"].asString());
        s.joints.push_back(j);
    }
    return true;
}

// ------------------------------------------------------------------ plugin

bool PluginSource::poll(GameState &state) {
    static const std::wstring path = ipc::statusPath();
    std::string text;
    if (path.empty() || !ipc::readAll(path, text)) {
        const bool changed = state.fileFound || state.connected;
        state = GameState{};
        lastText_.clear();
        return changed;
    }
    const unsigned long long now = ipc::nowUnixMs();
    if (text == lastText_) {
        // Mesmo conteudo: so o batimento pode ter vencido.
        json::Value v;
        if (!json::parse(text, v)) return false;
        const auto ts = static_cast<unsigned long long>(v["timestamp_ms"].asNumber(0));
        const bool connected = heartbeatFresh(ts, now);
        if (connected == lastConnected_) return false;
        lastConnected_ = connected;
        state.connected = connected;
        return true;
    }
    json::Value v;
    if (!json::parse(text, v)) return false; // arquivo pela metade: tenta de novo
    lastText_ = text;
    GameState s;
    if (!stateFromJson(v, now, s)) return false;
    lastConnected_ = s.connected;
    state = std::move(s);
    return true;
}

long long PluginSource::send(const std::string &action) {
    static const std::wstring path = ipc::commandPath();
    if (path.empty()) return 0;
    long long seq = static_cast<long long>(ipc::nowUnixMs());
    if (seq <= lastSeq_) seq = lastSeq_ + 1;
    lastSeq_ = seq;

    // Se o plugin ainda nao pegou o anterior, acrescenta (o plugin ignora seq repetido).
    std::string text;
    ipc::readAll(path, text);
    if (!text.empty() && text.back() != '\n') text += '\n';
    text += std::to_string(seq) + " " + action + "\n";
    return ipc::writeAtomic(path, text) ? seq : 0;
}

// ------------------------------------------------------------------ demonstracao

DemoSource::DemoSource() {
    GameState &s = sim_;
    s.fileFound = true;
    s.connected = true;
    s.paused = false;
    s.speedKmh = 0;
    s.maxSpeedKmh = 3;
    s.inputReady = true;
    s.inputActive = true;
    s.memoryReady = true;
    s.memoryStatus = L"ativo (demonstração)";
    s.exeVersion = L"1.62.3.3";
    s.pluginVersion = L"demo";
    s.combination = L"Rodotrem / Double com dolly";
    s.lastMessage = L"Modo demonstração: nada é enviado ao jogo.";
    s.units = {{0, L"cavalo", L"Scania S 730"},
               {1, L"carreta", L"Graneleira 3 eixos"},
               {2, L"dolly", L"Dolly 2 eixos"},
               {3, L"carreta", L"Graneleira 2 eixos"}};
    s.joints = {{0, 0, 1, L"Quinta roda do cavalo", true, false, L"entrada"},
                {1, 1, 2, L"Lanca do dolly", true, true, L"memoria"},
                {2, 2, 3, L"Quinta roda do dolly", true, true, L"memoria"}};
}

long long DemoSource::send(const std::string &action) {
    ++seq_;
    pending_.push_back({GetTickCount64() + 350, action});
    pendingSeq_.push_back(seq_);
    return seq_;
}

bool DemoSource::poll(GameState &state) {
    const unsigned long long now = GetTickCount64();
    for (size_t i = 0; i < pending_.size();) {
        if (pending_[i].first > now) {
            ++i;
            continue;
        }
        const std::string action = pending_[i].second;
        const long long seq = pendingSeq_[i];
        pending_.erase(pending_.begin() + static_cast<long>(i));
        pendingSeq_.erase(pendingSeq_.begin() + static_cast<long>(i));

        std::string verb = action;
        int arg = -1;
        const auto sp = action.find(' ');
        if (sp != std::string::npos) {
            verb = action.substr(0, sp);
            arg = std::atoi(action.c_str() + sp + 1);
        }
        bool ok = true;
        std::wstring msg;
        if ((verb == "desengatar" || verb == "engatar") && arg >= 0 &&
            arg < static_cast<int>(sim_.joints.size())) {
            JointView &j = sim_.joints[static_cast<size_t>(arg)];
            const bool want = verb == "engatar";
            if (j.coupled == want) {
                ok = false;
                msg = L"Articulacao " + std::to_wstring(arg) + (want ? L" ja esta engatada." : L" ja esta desengatada.");
            } else {
                j.coupled = want;
                sim_.selectedJoint = arg;
                msg = (want ? L"Engatar '" : L"Desengatar '") + j.kind + L"' via " +
                      (arg == 0 ? L"Entrada (SDK oficial)" : L"Memoria (experimental)") + L": enviado";
            }
        } else if (verb == "selecionar" && arg >= 0 && arg < static_cast<int>(sim_.joints.size())) {
            sim_.selectedJoint = arg;
            msg = L"Selecionada articulacao " + std::to_wstring(arg) + L": " + sim_.joints[static_cast<size_t>(arg)].kind;
        } else if (verb == "listar") {
            msg = L"Composicao escrita no game.log.txt.";
        } else {
            ok = false;
            msg = L"Acao desconhecida: " + widen(verb);
        }
        sim_.lastMessage = msg;
        sim_.lastOk = ok;
        sim_.lastCommandSeq = seq;
        sim_.lastCommandOk = ok;
        sim_.lastCommandResult = msg;
        changed_ = true;
    }
    if (!changed_) return false;
    changed_ = false;
    state = sim_;
    return true;
}

} // namespace tc::app
