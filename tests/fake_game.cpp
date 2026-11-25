// "Jogo falso": carrega trailer_control.dll e faz o papel do ETS2 pelo SCS SDK, para testar
// o plugin + app sem o jogo. Simula um rodotrem (cavalo + carreta + dolly + carreta) parado,
// com o dispositivo de entrada ativo; quando o plugin "aperta" o botao de engate, o falso
// jogo solta/engata o conjunto do cavalo como o jogo de verdade faria.
//
// Uso: fake_game.exe <caminho\trailer_control.dll> [segundos=30]
//
// O backend de memoria fica desligado aqui (este exe nao e o eurotrucks2.exe), entao as
// articulacoes internas aparecem como "indisponiveis" - isso so da para testar no jogo.

#include <windows.h>

#include <scssdk_input.h>
#include <scssdk_telemetry.h>
#include <eurotrucks2/scssdk_eut2.h>
#include <eurotrucks2/scssdk_telemetry_eut2.h>

#include "json.h"
#include "shared/ipc.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace {

struct ChannelSub {
    scs_telemetry_channel_callback_t cb;
    scs_context_t ctx;
};

std::map<scs_event_t, std::pair<scs_telemetry_event_callback_t, scs_context_t>> g_events;
std::map<std::string, ChannelSub> g_channels;
const scs_input_device_t *g_device = nullptr;
bool g_trailerCoupled = true;

SCSAPI_VOID logCb(const scs_log_type_t type, const scs_string_t msg) {
    const char *t = type == SCS_LOG_TYPE_error ? "ERRO " : type == SCS_LOG_TYPE_warning ? "AVISO" : "info ";
    std::printf("[jogo] %s %s\n", t, msg);
}

SCSAPI_RESULT regEvent(const scs_event_t e, const scs_telemetry_event_callback_t cb, const scs_context_t ctx) {
    g_events[e] = {cb, ctx};
    return SCS_RESULT_ok;
}
SCSAPI_RESULT unregEvent(const scs_event_t e) {
    g_events.erase(e);
    return SCS_RESULT_ok;
}
SCSAPI_RESULT regChannel(const scs_string_t name, const scs_u32_t, const scs_value_type_t, const scs_u32_t,
                         const scs_telemetry_channel_callback_t cb, const scs_context_t ctx) {
    g_channels[name] = {cb, ctx};
    return SCS_RESULT_ok;
}
SCSAPI_RESULT unregChannel(const scs_string_t name, const scs_u32_t, const scs_value_type_t) {
    g_channels.erase(name);
    return SCS_RESULT_ok;
}
SCSAPI_RESULT regDevice(const scs_input_device_t *const dev) {
    g_device = dev;
    std::printf("[jogo] dispositivo de entrada registrado: %s (%u entrada[s])\n", dev->display_name,
                dev->input_count);
    return SCS_RESULT_ok;
}

void fireEvent(scs_event_t e, const void *info) {
    const auto it = g_events.find(e);
    if (it != g_events.end()) it->second.first(e, info, it->second.second);
}

void fireChannel(const std::string &name, const scs_value_t &v) {
    const auto it = g_channels.find(name);
    if (it != g_channels.end()) it->second.cb(name.c_str(), SCS_U32_NIL, &v, it->second.ctx);
}

scs_named_value_t strAttr(const char *name, const char *value) {
    scs_named_value_t a{};
    a.name = name;
    a.index = SCS_U32_NIL;
    a.value.type = SCS_VALUE_TYPE_string;
    a.value.value_string.value = value;
    return a;
}

void sendTrailerConfig(int idx, const char *id, const char *name, float hookZ) {
    static std::vector<std::string> ids;
    char cfgId[32];
    std::snprintf(cfgId, sizeof(cfgId), "trailer.%d", idx);
    ids.emplace_back(cfgId);
    std::vector<scs_named_value_t> attrs = {strAttr("id", id), strAttr("name", name),
                                            strAttr("chain.type", "double"), strAttr("brand", "SCS")};
    scs_named_value_t hook{};
    hook.name = SCS_TELEMETRY_CONFIG_ATTRIBUTE_hook_position;
    hook.index = SCS_U32_NIL;
    hook.value.type = SCS_VALUE_TYPE_fvector;
    hook.value.value_fvector = {0.f, 1.2f, hookZ};
    attrs.push_back(hook);
    attrs.push_back(scs_named_value_t{}); // terminador
    scs_telemetry_configuration_t cfg{};
    cfg.id = ids.back().c_str();
    cfg.attributes = attrs.data();
    fireEvent(SCS_TELEMETRY_EVENT_configuration, &cfg);
}

void setCoupled(bool coupled) {
    g_trailerCoupled = coupled;
    scs_value_t v{};
    v.type = SCS_VALUE_TYPE_bool;
    v.value_bool.value = coupled ? 1 : 0;
    for (int i = 0; i < 3; ++i) fireChannel("trailer." + std::to_string(i) + ".connected", v);
    std::printf("[jogo] conjunto %s\n", coupled ? "ENGATADO no cavalo" : "SOLTO do cavalo");
}

// Le os eventos do dispositivo como o jogo faz a cada frame.
void pumpInput(bool &held) {
    if (!g_device) return;
    scs_u32_t flags = SCS_INPUT_EVENT_CALLBACK_FLAG_first_in_frame;
    for (int guard = 0; guard < 8; ++guard) {
        scs_input_event_t ev{};
        if (g_device->input_event_callback(&ev, flags, g_device->callback_context) != SCS_RESULT_ok) break;
        flags = 0;
        const bool down = ev.value_bool.value != 0;
        if (down && !held) std::printf("[jogo] botao 'engatar/desengatar' pressionado\n");
        if (!down && held) setCoupled(!g_trailerCoupled); // o jogo age ao soltar o botao
        held = down;
    }
}

// ---- autoteste: manda comandos como o app faria e confere o status.json ----
struct Step {
    const char *cmd;
    bool expectOk;
    int joint;          // articulacao a conferir (-1 = nenhuma)
    bool expectCoupled;
    int joint2 = -1;    // segunda articulacao a conferir depois do efeito
    bool expectCoupled2 = false;
};

bool readStatus(tc::json::Value &v) {
    std::string text;
    return tc::ipc::readAll(tc::ipc::statusPath(), text) && tc::json::parse(text, v);
}

void frame(bool &held) {
    scs_telemetry_frame_start_t fs{};
    fireEvent(SCS_TELEMETRY_EVENT_frame_start, &fs);
    pumpInput(held);
    fireEvent(SCS_TELEMETRY_EVENT_frame_end, nullptr);
    Sleep(16);
}

int selfTest(bool &held) {
    const Step steps[] = {
        {"selecionar 2", true, -1, false},
        {"desengatar 0", true, 0, false},  // cavalo solta o conjunto (via entrada)
        {"desengatar 0", false, 0, false}, // ja solto
        {"engatar 0", true, 0, true},
        {"desengatar 1", false, 1, true},  // interna: memoria indisponivel aqui
        {"desengatar 9", false, -1, false}, // nao existe
        {"voar 1", false, -1, false},       // acao desconhecida
        {"diagnostico", true, -1, false},   // so leitura; aqui nao ha tabela para o exe
        {"desengatar 0", true, 0, false, 1, true}, // conjunto solto: dolly continua preso na carreta
        {"engatar 0", true, 0, true},
    };
    int failures = 0;
    long long seq = static_cast<long long>(tc::ipc::nowUnixMs());
    for (const Step &s : steps) {
        ++seq;
        tc::ipc::writeAtomic(tc::ipc::commandPath(), std::to_string(seq) + " " + s.cmd + "\n");
        bool done = false;
        tc::json::Value v;
        for (int f = 0; f < 120 && !done; ++f) { // ate 2 s
            frame(held);
            if (!readStatus(v) || v["last_command"]["seq"].asInt() != seq) continue;
            // espera o efeito no jogo (o botao virtual leva alguns frames)
            if (s.joint >= 0 && s.expectOk && v["joints"][static_cast<size_t>(s.joint)]["coupled"].asBool() != s.expectCoupled)
                continue;
            done = true;
        }
        const bool ok = v["last_command"]["ok"].asBool();
        bool pass = done && ok == s.expectOk;
        if (pass && s.joint >= 0) pass = v["joints"][static_cast<size_t>(s.joint)]["coupled"].asBool() == s.expectCoupled;
        if (pass && s.joint2 >= 0) pass = v["joints"][static_cast<size_t>(s.joint2)]["coupled"].asBool() == s.expectCoupled2;
        std::printf("[teste] %-14s -> %s  (%s)\n", s.cmd, pass ? "OK" : "FALHOU",
                    v["last_command"]["result"].asString().c_str());
        if (!pass) ++failures;
    }
    // Jogo pausado (ex.: a tela do app na frente): o comando fica na fila e roda ao despausar.
    {
        fireEvent(SCS_TELEMETRY_EVENT_paused, nullptr);
        ++seq;
        tc::ipc::writeAtomic(tc::ipc::commandPath(), std::to_string(seq) + " desengatar 0\n");
        tc::json::Value v;
        bool queued = false, executed = false;
        for (int f = 0; f < 60 && !queued; ++f) {
            frame(held);
            queued = readStatus(v) && v["last_command"]["seq"].asInt() == seq &&
                     v["queued_commands"].asInt() == 1 && v["joints"][0]["coupled"].asBool();
        }
        fireEvent(SCS_TELEMETRY_EVENT_started, nullptr);
        for (int f = 0; f < 120 && !executed; ++f) {
            frame(held);
            executed = readStatus(v) && v["queued_commands"].asInt() == 0 && !v["joints"][0]["coupled"].asBool();
        }
        const bool pass = queued && executed;
        std::printf("[teste] %-14s -> %s  (%s)\n", "fila pausado", pass ? "OK" : "FALHOU",
                    v["last_message"].asString().c_str());
        if (!pass) ++failures;
    }
    // Recarregar configuracao (a tela de configuracoes salva o .ini e manda isto).
    {
        ++seq;
        tc::ipc::writeAtomic(tc::ipc::commandPath(), std::to_string(seq) + " recarregar\n");
        tc::json::Value v;
        bool ok = false;
        for (int f = 0; f < 60 && !ok; ++f) {
            frame(held);
            ok = readStatus(v) && v["last_command"]["seq"].asInt() == seq && v["last_command"]["ok"].asBool() &&
                 v["app_hotkey"].asString() == "CTRL+MINUS" && !v["config_path"].asString().empty();
        }
        std::printf("[teste] %-14s -> %s  (%s)\n", "recarregar", ok ? "OK" : "FALHOU",
                    v["last_command"]["result"].asString().c_str());
        if (!ok) ++failures;
    }
    // seq menor que o anterior (relogio do PC voltou / outro app) ainda e executado;
    // o mesmo seq repetido (reenvio) nao.
    {
        const long long older = seq - 100000;
        tc::ipc::writeAtomic(tc::ipc::commandPath(), std::to_string(older) + " selecionar 1\n");
        tc::json::Value v;
        bool ok = false;
        for (int f = 0; f < 60 && !ok; ++f) {
            frame(held);
            ok = readStatus(v) && v["last_command"]["seq"].asInt() == older && v["selected_joint"].asInt() == 1;
        }
        tc::ipc::writeAtomic(tc::ipc::commandPath(), std::to_string(older) + " selecionar 2\n");
        for (int f = 0; f < 20; ++f) frame(held);
        const bool dupIgnored = readStatus(v) && v["selected_joint"].asInt() == 1;
        std::printf("[teste] %-14s -> %s\n", "seq fora ordem", ok && dupIgnored ? "OK" : "FALHOU");
        if (!(ok && dupIgnored)) ++failures;
    }
    // Convoy (multiplayer): backend de memoria desliga; volta ao sair.
    {
        auto setOffset = [](int minutes) {
            scs_value_t v{};
            v.type = SCS_VALUE_TYPE_s32;
            v.value_s32.value = minutes;
            fireChannel(SCS_TELEMETRY_CHANNEL_multiplayer_time_offset, v);
        };
        auto waitStatus = [&](const char *needle, bool present) {
            tc::json::Value v;
            for (int f = 0; f < 240; ++f) { // ate 4 s (checagem a cada 2 s)
                frame(held);
                if (!readStatus(v)) continue;
                const bool has = v["memory_status"].asString().find(needle) != std::string::npos;
                if (has == present) return true;
            }
            return false;
        };
        setOffset(37);
        const bool off = waitStatus("Convoy", true);
        setOffset(0);
        const bool back = waitStatus("multiplayer", false);
        std::printf("[teste] %-14s -> %s\n", "multiplayer", off && back ? "OK" : "FALHOU");
        if (!(off && back)) ++failures;
    }
    std::printf("[teste] %d falha(s)\n", failures);
    return failures;
}

} // namespace

int wmain(int argc, wchar_t **argv) {
    if (argc < 2) {
        std::printf("uso: fake_game.exe <trailer_control.dll> [segundos]\n");
        return 2;
    }
    const bool selftest = argc >= 3 && wcscmp(argv[2], L"--selftest") == 0;
    const int seconds = (argc >= 3 && !selftest) ? _wtoi(argv[2]) : 30;
    HMODULE dll = LoadLibraryW(argv[1]);
    if (!dll) {
        std::printf("nao carregou a DLL (erro %lu)\n", GetLastError());
        return 1;
    }
    auto telInit = reinterpret_cast<decltype(&scs_telemetry_init)>(GetProcAddress(dll, "scs_telemetry_init"));
    auto telShutdown = reinterpret_cast<decltype(&scs_telemetry_shutdown)>(GetProcAddress(dll, "scs_telemetry_shutdown"));
    auto inInit = reinterpret_cast<decltype(&scs_input_init)>(GetProcAddress(dll, "scs_input_init"));
    auto inShutdown = reinterpret_cast<decltype(&scs_input_shutdown)>(GetProcAddress(dll, "scs_input_shutdown"));
    if (!telInit || !telShutdown || !inInit || !inShutdown) {
        std::printf("exports do SDK ausentes na DLL\n");
        return 1;
    }

    const scs_u32_t gameVersion = SCS_TELEMETRY_EUT2_GAME_VERSION_CURRENT;

    scs_input_init_params_v100_t ip{};
    ip.common.game_name = "Euro Truck Simulator 2 (falso)";
    ip.common.game_id = SCS_GAME_ID_EUT2;
    ip.common.game_version = gameVersion;
    ip.common.log = logCb;
    ip.register_device = regDevice;
    if (inInit(SCS_INPUT_VERSION_1_00, &ip) != SCS_RESULT_ok) std::printf("scs_input_init falhou\n");

    scs_telemetry_init_params_v101_t tp{};
    tp.common = ip.common;
    tp.register_for_event = regEvent;
    tp.unregister_from_event = unregEvent;
    tp.register_for_channel = regChannel;
    tp.unregister_from_channel = unregChannel;
    if (telInit(SCS_TELEMETRY_VERSION_1_01, &tp) != SCS_RESULT_ok) {
        std::printf("scs_telemetry_init falhou\n");
        return 1;
    }

    // Composicao: rodotrem
    std::vector<scs_named_value_t> truckAttrs = {strAttr("brand", "Scania"), strAttr("name", "S 730"),
                                                 scs_named_value_t{}};
    scs_telemetry_configuration_t truckCfg{SCS_TELEMETRY_CONFIG_truck, truckAttrs.data()};
    fireEvent(SCS_TELEMETRY_EVENT_configuration, &truckCfg);
    sendTrailerConfig(0, "scs.grain.3axle", "Graneleira 3 eixos", -5.5f);
    sendTrailerConfig(1, "scs.dolly.2axle", "Dolly 2 eixos", -1.8f);
    sendTrailerConfig(2, "scs.grain.2axle", "Graneleira 2 eixos", -5.0f);
    setCoupled(true);

    if (g_device && g_device->input_active_callback)
        g_device->input_active_callback(1, g_device->callback_context);
    fireEvent(SCS_TELEMETRY_EVENT_started, nullptr);

    bool held = false;
    if (selftest) {
        for (int f = 0; f < 10; ++f) frame(held);
        const int failures = selfTest(held);
        telShutdown();
        inShutdown();
        FreeLibrary(dll);
        return failures == 0 ? 0 : 1;
    }
    std::printf("[jogo] rodando %d s a 60 fps (Ctrl+C para sair)\n", seconds);
    const ULONGLONG end = GetTickCount64() + static_cast<ULONGLONG>(seconds) * 1000;
    while (GetTickCount64() < end) {
        scs_telemetry_frame_start_t fs{};
        fireEvent(SCS_TELEMETRY_EVENT_frame_start, &fs);
        pumpInput(held);
        scs_value_t speed{};
        speed.type = SCS_VALUE_TYPE_float;
        speed.value_float.value = 0.f;
        fireChannel(SCS_TELEMETRY_TRUCK_CHANNEL_speed, speed);
        fireEvent(SCS_TELEMETRY_EVENT_frame_end, nullptr);
        Sleep(16);
    }

    fireEvent(SCS_TELEMETRY_EVENT_paused, nullptr);
    telShutdown();
    inShutdown();
    FreeLibrary(dll);
    std::printf("[jogo] encerrado\n");
    return 0;
}
