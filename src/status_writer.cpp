#include "status_writer.h"

#include <windows.h>
#include <shlobj.h>

#include <fstream>
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

std::wstring statusPath() {
    PWSTR docs = nullptr;
    std::wstring dir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &docs))) {
        dir = docs;
        CoTaskMemFree(docs);
    } else {
        return {};
    }
    dir += L"\\ETS2TrailerControl";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir + L"\\status.json";
}

} // namespace

void writeStatus(const CombinationSnapshot &snap, int selectedJoint, const std::string &lastMessage,
                 bool memoryBackendReady) {
    const std::wstring path = statusPath();
    if (path.empty()) return;

    std::ostringstream j;
    j << "{\n";
    j << "  \"combination\": \"" << esc(snap.combinationName) << "\",\n";
    j << "  \"chain_type\": \"" << esc(snap.chainType) << "\",\n";
    j << "  \"selected_joint\": " << selectedJoint << ",\n";
    j << "  \"memory_backend\": " << (memoryBackendReady ? "true" : "false") << ",\n";
    j << "  \"last_message\": \"" << esc(lastMessage) << "\",\n";
    j << "  \"units\": [\n";
    for (size_t i = 0; i < snap.units.size(); ++i) {
        const Unit &u = snap.units[i];
        j << "    {\"index\": " << u.index << ", \"kind\": \"" << toString(u.kind)
          << "\", \"name\": \"" << esc(u.displayName()) << "\", \"id\": \"" << esc(u.attr("id"))
          << "\", \"body\": \"" << esc(u.attr("body.type")) << "\", \"connected\": "
          << (u.connected ? "true" : "false") << "}" << (i + 1 < snap.units.size() ? "," : "") << "\n";
    }
    j << "  ],\n  \"joints\": [\n";
    for (size_t i = 0; i < snap.joints.size(); ++i) {
        const Joint &jt = snap.joints[i];
        j << "    {\"index\": " << jt.index << ", \"front\": " << jt.front << ", \"rear\": " << jt.rear
          << ", \"kind\": \"" << toString(jt.kind) << "\", \"coupled\": "
          << (jt.coupled ? "true" : "false") << "}" << (i + 1 < snap.joints.size() ? "," : "") << "\n";
    }
    j << "  ]\n}\n";

    // Grava num temporario e troca, para leitores externos nunca verem arquivo pela metade.
    const std::wstring tmp = path + L".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return;
        out << j.str();
    }
    MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
}

} // namespace tc
