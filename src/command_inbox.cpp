#include "command_inbox.h"

#include "shared/ipc.h"

#include <sstream>

namespace tc {

bool parseCommandLine(const std::string &line, ExternalCommand &out) {
    std::istringstream ss(line);
    long long seq = 0;
    std::string action, arg;
    if (!(ss >> seq >> action)) return false;
    if (action.size() > 32) return false;
    for (char c : action) {
        if (!((c >= 'a' && c <= 'z') || c == '_')) return false;
    }
    out.seq = seq;
    out.action = action;
    if (ss >> arg) {
        for (char c : arg) {
            if (c < '0' || c > '9') return false;
        }
        if (arg.size() > 3) return false;
        out.action += " " + arg;
    }
    return true;
}

std::vector<ExternalCommand> takeExternalCommands() {
    std::vector<ExternalCommand> out;
    static const std::wstring path = ipc::commandPath();
    if (path.empty()) return out;
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) return out;

    // "Pega" o arquivo atomicamente; se o app estiver trocando agora, tenta no proximo ciclo.
    const std::wstring taken = path + L".taken";
    if (!MoveFileExW(path.c_str(), taken.c_str(), MOVEFILE_REPLACE_EXISTING)) return out;

    std::string text;
    ipc::readAll(taken, text);
    DeleteFileW(taken.c_str());

    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line) && out.size() < 16) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        ExternalCommand c;
        if (parseCommandLine(line, c)) out.push_back(c);
    }
    return out;
}

} // namespace tc
