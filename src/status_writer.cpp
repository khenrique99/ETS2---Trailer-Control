#include "status_writer.h"

#include "shared/ipc.h"

#include <cstdio>
#include <sstream>

namespace tc {

namespace {

std::string esc(const std::string &s) {
    std::string o;
    o.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
        case '"':  o += "\\\""; break;
        case '\\': o += "\\\\"; break;
        case '\n': o += "\\n"; break;
        case '\r': break;
        case '\t': o += "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) continue;
            o += c;
        }
    }
    return o;
}

const char *b(bool v) { return v ? "true" : "false"; }

std::string num(double v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.2f", v);
    return buf;
}

// Qual backend atenderia essa articulacao (o app usa para habilitar os botoes).
const char *backendFor(const Joint &jt, const StatusInfo &info) {
    if (jt.index == 0) return info.inputReady ? "entrada" : "nenhum";
    return info.memoryReady ? "memoria" : "nenhum";
}

} // namespace

void writeStatus(const StatusInfo &info) {
    static const std::wstring path = ipc::statusPath();
    if (path.empty() || !info.snap) return;
    const CombinationSnapshot &snap = *info.snap;

    std::ostringstream j;
    j << "{\n";
    j << "  \"protocol\": " << ipc::kProtocolVersion << ",\n";
    j << "  \"plugin_version\": \"" << kPluginVersion << "\",\n";
    j << "  \"exe_version\": \"" << esc(info.exeVersion) << "\",\n";
    j << "  \"timestamp_ms\": " << ipc::nowUnixMs() << ",\n";
    j << "  \"paused\": " << b(info.paused) << ",\n";
    j << "  \"speed_kmh\": " << num(info.speedKmh) << ",\n";
    j << "  \"require_stopped\": " << b(info.requireStopped) << ",\n";
    j << "  \"max_speed_kmh\": " << num(info.maxSpeedKmh) << ",\n";
    j << "  \"input_backend\": " << b(info.inputReady) << ",\n";
    j << "  \"input_active\": " << b(info.inputActive) << ",\n";
    j << "  \"memory_backend\": " << b(info.memoryReady) << ",\n";
    j << "  \"memory_status\": \"" << esc(info.memoryStatus) << "\",\n";
    j << "  \"config_path\": \"" << esc(info.configPath) << "\",\n";
    j << "  \"app_hotkey\": \"" << esc(info.appHotkey) << "\",\n";
    j << "  \"queued_commands\": " << info.queuedCommands << ",\n";
    j << "  \"combination\": \"" << esc(snap.combinationName) << "\",\n";
    j << "  \"chain_type\": \"" << esc(snap.chainType) << "\",\n";
    j << "  \"selected_joint\": " << info.selectedJoint << ",\n";
    j << "  \"last_message\": \"" << esc(info.lastMessage) << "\",\n";
    j << "  \"last_ok\": " << b(info.lastOk) << ",\n";
    j << "  \"last_command\": {\"seq\": " << info.lastCommandSeq << ", \"ok\": " << b(info.lastCommandOk)
      << ", \"result\": \"" << esc(info.lastCommandResult) << "\"},\n";
    j << "  \"units\": [\n";
    for (size_t i = 0; i < snap.units.size(); ++i) {
        const Unit &u = snap.units[i];
        j << "    {\"index\": " << u.index << ", \"kind\": \"" << toString(u.kind) << "\", \"name\": \""
          << esc(u.displayName()) << "\", \"id\": \"" << esc(u.attr("id")) << "\", \"body\": \""
          << esc(u.attr("body.type")) << "\", \"connected\": " << b(u.connected) << "}"
          << (i + 1 < snap.units.size() ? "," : "") << "\n";
    }
    j << "  ],\n  \"joints\": [\n";
    for (size_t i = 0; i < snap.joints.size(); ++i) {
        const Joint &jt = snap.joints[i];
        j << "    {\"index\": " << jt.index << ", \"front\": " << jt.front << ", \"rear\": " << jt.rear
          << ", \"kind\": \"" << toString(jt.kind) << "\", \"coupled\": " << b(jt.coupled)
          << ", \"source\": \"" << (jt.fromMemory ? "memoria" : "telemetria") << "\", \"backend\": \""
          << backendFor(jt, info) << "\"}" << (i + 1 < snap.joints.size() ? "," : "") << "\n";
    }
    j << "  ]\n}\n";

    ipc::writeAtomic(path, j.str());
}

} // namespace tc
