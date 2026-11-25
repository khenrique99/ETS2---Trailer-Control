// ETS2 Trailer Control - ponto de entrada do plugin.
//
// A mesma DLL implementa as duas APIs da SCS:
//   * Telemetry SDK -> detecta cavalo, reboques, dollies, tipo de composicao e estado de engate
//   * Input SDK     -> dispositivo virtual que aciona o controle nativo de engate/desengate
//
// Instalar em: <ETS2>\bin\win_x64\plugins\trailer_control.dll (+ trailer_control.ini)

#include <windows.h>

#include <scssdk_telemetry.h>
#include <scssdk_input.h>
#include <eurotrucks2/scssdk_eut2.h>
#include <eurotrucks2/scssdk_telemetry_eut2.h>

#include "config.h"
#include "controller.h"
#include "input_backend.h"
#include "log.h"
#include "memory_backend.h"

#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <string>

using namespace tc;

namespace {

std::once_flag g_configOnce;
Config g_config;
std::string g_gameVersion;

std::string versionString(scs_u32_t v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%u.%u", SCS_GET_MAJOR_VERSION(v), SCS_GET_MINOR_VERSION(v));
    return buf;
}

// Carregado uma unica vez, por quem o jogo inicializar primeiro (input ou telemetria).
void loadConfigOnce(scs_u32_t gameVersion) {
    std::call_once(g_configOnce, [gameVersion] {
        g_gameVersion = versionString(gameVersion);
        const std::wstring path = Controller::configPath();
        if (!g_config.load(path)) {
            log::warn("trailer_control.ini nao encontrado ao lado da DLL; usando padroes.");
        }
        Controller::instance().configure(g_config); // inclui o backend de memoria e o atalho
    });
}

std::string valueToString(const scs_value_t &v) {
    char buf[64];
    switch (v.type) {
    case SCS_VALUE_TYPE_string: return v.value_string.value ? v.value_string.value : "";
    case SCS_VALUE_TYPE_bool:   return v.value_bool.value ? "true" : "false";
    case SCS_VALUE_TYPE_u32:    std::snprintf(buf, sizeof(buf), "%u", v.value_u32.value); return buf;
    case SCS_VALUE_TYPE_s32:    std::snprintf(buf, sizeof(buf), "%d", v.value_s32.value); return buf;
    case SCS_VALUE_TYPE_u64:    std::snprintf(buf, sizeof(buf), "%llu", static_cast<unsigned long long>(v.value_u64.value)); return buf;
    case SCS_VALUE_TYPE_s64:    std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(v.value_s64.value)); return buf;
    case SCS_VALUE_TYPE_float:  std::snprintf(buf, sizeof(buf), "%g", v.value_float.value); return buf;
    case SCS_VALUE_TYPE_double: std::snprintf(buf, sizeof(buf), "%g", v.value_double.value); return buf;
    default: return {};
    }
}

std::map<std::string, std::string> collectAttributes(const scs_named_value_t *attrs) {
    std::map<std::string, std::string> out;
    for (const scs_named_value_t *a = attrs; a && a->name; ++a) {
        if (a->index != SCS_U32_NIL) continue; // atributos indexados (rodas etc.) nao interessam aqui
        const std::string s = valueToString(a->value);
        if (!s.empty()) out[a->name] = s;
    }
    return out;
}

// "trailer" (legado) -> 0, "trailer.3" -> 3, outro -> -1
int parseTrailerConfigId(const char *id) {
    if (std::strcmp(id, "trailer") == 0) return 0;
    if (std::strncmp(id, "trailer.", 8) == 0) {
        const char *n = id + 8;
        if (*n >= '0' && *n <= '9') return std::atoi(n);
    }
    return -1;
}

// ---- callbacks de telemetria (thread do jogo) ----

SCSAPI_VOID onFrameStart(const scs_event_t, const void *const, const scs_context_t) {
    Controller::instance().tick();
}

SCSAPI_VOID onPause(const scs_event_t event, const void *const, const scs_context_t) {
    Controller::instance().setPaused(event == SCS_TELEMETRY_EVENT_paused);
}

SCSAPI_VOID onConfiguration(const scs_event_t, const void *const info, const scs_context_t) {
    const auto *cfg = static_cast<const scs_telemetry_configuration_t *>(info);
    if (!cfg || !cfg->id) return;

    if (std::strcmp(cfg->id, SCS_TELEMETRY_CONFIG_truck) == 0) {
        Controller::instance().registry().onTruckConfig(collectAttributes(cfg->attributes));
        return;
    }
    const int idx = parseTrailerConfigId(cfg->id);
    if (idx >= 0) {
        TrailerRegistry &reg = Controller::instance().registry();
        reg.onTrailerConfig(idx, collectAttributes(cfg->attributes));
        bool hasHook = false;
        scs_value_fvector_t hook{};
        for (const scs_named_value_t *a = cfg->attributes; a && a->name; ++a) {
            if (std::strcmp(a->name, SCS_TELEMETRY_CONFIG_ATTRIBUTE_hook_position) == 0 &&
                a->value.type == SCS_VALUE_TYPE_fvector) {
                hook = a->value.value_fvector;
                hasHook = true;
            }
        }
        reg.onTrailerHook(idx, hasHook, hook.x, hook.y, hook.z);
    }
}

SCSAPI_VOID onTrailerConnected(const scs_string_t, const scs_u32_t, const scs_value_t *const value,
                               const scs_context_t ctx) {
    const int idx = static_cast<int>(reinterpret_cast<intptr_t>(ctx));
    Controller::instance().registry().onTrailerConnected(idx, value && value->value_bool.value);
}

SCSAPI_VOID onTrailerPlacement(const scs_string_t, const scs_u32_t, const scs_value_t *const value,
                               const scs_context_t ctx) {
    if (!value || value->type != SCS_VALUE_TYPE_dplacement) return;
    const scs_value_dplacement_t &p = value->value_dplacement;
    TrailerPlacement tp;
    tp.x = p.position.x;
    tp.y = p.position.y;
    tp.z = p.position.z;
    tp.heading = p.orientation.heading;
    tp.pitch = p.orientation.pitch;
    tp.roll = p.orientation.roll;
    Controller::instance().registry().onTrailerPlacement(static_cast<int>(reinterpret_cast<intptr_t>(ctx)), tp);
}

SCSAPI_VOID onMultiplayerOffset(const scs_string_t, const scs_u32_t, const scs_value_t *const value,
                                const scs_context_t) {
    Controller::instance().setMultiplayerOffset(value && value->type == SCS_VALUE_TYPE_s32 ? value->value_s32.value : 0);
}

SCSAPI_VOID onSpeed(const scs_string_t, const scs_u32_t, const scs_value_t *const value,
                    const scs_context_t) {
    Controller::instance().setSpeed(value ? value->value_float.value : 0.f);
}

} // namespace

// =====================================================================
// Telemetry SDK
// =====================================================================

SCSAPI_RESULT scs_telemetry_init(const scs_u32_t version, const scs_telemetry_init_params_t *const params) {
    if (version != SCS_TELEMETRY_VERSION_1_01) return SCS_RESULT_unsupported;

    const auto *p = static_cast<const scs_telemetry_init_params_v101_t *>(params);
    log::init(p->common.log);

    if (std::strcmp(p->common.game_id, SCS_GAME_ID_EUT2) != 0) {
        log::warn("Jogo '%s' nao e o ETS2; o plugin foi feito para o ETS2 mas tentara funcionar.",
                  p->common.game_id);
    }
    log::info("Iniciando no %s %s", p->common.game_name, versionString(p->common.game_version).c_str());

    loadConfigOnce(p->common.game_version);

    const bool eventsOk =
        p->register_for_event(SCS_TELEMETRY_EVENT_frame_start, onFrameStart, nullptr) == SCS_RESULT_ok &&
        p->register_for_event(SCS_TELEMETRY_EVENT_paused, onPause, nullptr) == SCS_RESULT_ok &&
        p->register_for_event(SCS_TELEMETRY_EVENT_started, onPause, nullptr) == SCS_RESULT_ok &&
        p->register_for_event(SCS_TELEMETRY_EVENT_configuration, onConfiguration, nullptr) == SCS_RESULT_ok;
    if (!eventsOk) {
        log::error("Falha ao registrar eventos de telemetria.");
        return SCS_RESULT_generic_error;
    }

    // Convoy (multiplayer da SCS): offset diferente de 0 -> backend de memoria desligado.
    p->register_for_channel(SCS_TELEMETRY_CHANNEL_multiplayer_time_offset, SCS_U32_NIL, SCS_VALUE_TYPE_s32,
                            SCS_TELEMETRY_CHANNEL_FLAG_none, onMultiplayerOffset, nullptr);
    p->register_for_channel(SCS_TELEMETRY_TRUCK_CHANNEL_speed, SCS_U32_NIL, SCS_VALUE_TYPE_float,
                            SCS_TELEMETRY_CHANNEL_FLAG_none, onSpeed, nullptr);

    // Canal de engate por reboque: "trailer.N.connected" (SDK 1.14+, ate 10 reboques).
    int registered = 0;
    for (int i = 0; i < kMaxTrailers; ++i) {
        char name[64];
        std::snprintf(name, sizeof(name), "trailer.%d.connected", i);
        if (p->register_for_channel(name, SCS_U32_NIL, SCS_VALUE_TYPE_bool,
                                    SCS_TELEMETRY_CHANNEL_FLAG_no_value, onTrailerConnected,
                                    reinterpret_cast<scs_context_t>(static_cast<intptr_t>(i))) == SCS_RESULT_ok) {
            ++registered;
        }
        // Posicao no mundo: usada para medir a distancia entre engates antes de engatar.
        std::snprintf(name, sizeof(name), "trailer.%d.world.placement", i);
        p->register_for_channel(name, SCS_U32_NIL, SCS_VALUE_TYPE_dplacement, SCS_TELEMETRY_CHANNEL_FLAG_none,
                                onTrailerPlacement, reinterpret_cast<scs_context_t>(static_cast<intptr_t>(i)));
    }
    if (registered == 0) {
        // Versoes antigas so tem um reboque: "trailer.connected".
        p->register_for_channel("trailer.connected", SCS_U32_NIL, SCS_VALUE_TYPE_bool,
                                SCS_TELEMETRY_CHANNEL_FLAG_no_value, onTrailerConnected,
                                reinterpret_cast<scs_context_t>(static_cast<intptr_t>(0)));
        log::warn("Jogo sem suporte a multiplos reboques na telemetria; so o 1o sera detectado.");
    }

    Controller::instance().setPaused(true);
    Controller::instance().startHotkeys();
    log::info("Pronto. %d canais de reboque registrados. Memoria: %s.", registered,
              MemoryBackend::instance().available() ? "ativa" : "inativa");
    return SCS_RESULT_ok;
}

SCSAPI_VOID scs_telemetry_shutdown(void) {
    Controller::instance().stopHotkeys();
    log::info("Telemetria encerrada.");
}

// =====================================================================
// Input SDK
// =====================================================================

SCSAPI_RESULT scs_input_init(const scs_u32_t version, const scs_input_init_params_t *const params) {
    if (version != SCS_INPUT_VERSION_1_00) return SCS_RESULT_unsupported;

    const auto *p = static_cast<const scs_input_init_params_v100_t *>(params);
    log::init(p->common.log);
    loadConfigOnce(p->common.game_version);

    const std::string mode = toLower(g_config.get("entrada", "modo", "generic"));
    const std::string control = g_config.get("entrada", "controle_semantico", "trailer");
    if (!InputBackend::instance().registerDevice(*p, mode, control)) {
        return SCS_RESULT_generic_error;
    }
    return SCS_RESULT_ok;
}

SCSAPI_VOID scs_input_shutdown(void) {
    log::info("Entrada encerrada.");
}

// Os dois shutdowns chegam antes de a DLL ser descarregada; so entao soltamos o logger.
BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_DETACH) log::shutdown();
    return TRUE;
}
