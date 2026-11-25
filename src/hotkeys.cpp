#include "hotkeys.h"

#include "config.h"
#include "log.h"

#include <windows.h>

#include <chrono>
#include <map>
#include <sstream>

namespace tc {

namespace {

int keyNameToVk(const std::string &upper) {
    static const std::map<std::string, int> named = {
        {"LEFT", VK_LEFT},   {"RIGHT", VK_RIGHT}, {"UP", VK_UP},         {"DOWN", VK_DOWN},
        {"HOME", VK_HOME},   {"END", VK_END},     {"INSERT", VK_INSERT}, {"DELETE", VK_DELETE},
        {"PGUP", VK_PRIOR},  {"PGDN", VK_NEXT},   {"SPACE", VK_SPACE},   {"ENTER", VK_RETURN},
        {"TAB", VK_TAB},     {"BACKSPACE", VK_BACK}, {"MINUS", VK_OEM_MINUS}, {"-", VK_OEM_MINUS},
        {"NUMMINUS", VK_SUBTRACT}, {"PLUS", VK_OEM_PLUS}, {"NUMPLUS", VK_ADD},
    };
    if (auto it = named.find(upper); it != named.end()) return it->second;
    if (upper.size() == 1) {
        const char c = upper[0];
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) return c;
    }
    if (upper.size() >= 2 && upper[0] == 'F') {
        const int n = std::atoi(upper.c_str() + 1);
        if (n >= 1 && n <= 24) return VK_F1 + n - 1;
    }
    if (upper.rfind("NUMPAD", 0) == 0 && upper.size() == 7) {
        const int n = upper[6] - '0';
        if (n >= 0 && n <= 9) return VK_NUMPAD0 + n;
    }
    return 0;
}

bool down(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }

bool gameHasFocus() {
    HWND fg = GetForegroundWindow();
    if (!fg) return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    return pid == GetCurrentProcessId();
}

} // namespace

bool HotkeyListener::add(const std::string &action, const std::string &combo) {
    if (combo.empty()) return false;
    Binding b;
    b.action = action;
    std::stringstream ss(combo);
    std::string part;
    while (std::getline(ss, part, '+')) {
        std::string p = trim(part);
        for (auto &c : p) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        if (p == "CTRL") b.ctrl = true;
        else if (p == "SHIFT") b.shift = true;
        else if (p == "ALT") b.alt = true;
        else b.vk = keyNameToVk(p);
    }
    if (!b.vk) {
        log::warn("Atalho invalido para '%s': %s", action.c_str(), combo.c_str());
        return false;
    }
    bindings_.push_back(b);
    // "-" vale tanto no teclado principal quanto no numerico.
    if (b.vk == VK_OEM_MINUS) {
        b.vk = VK_SUBTRACT;
        bindings_.push_back(b);
    }
    return true;
}

void HotkeyListener::clear() {
    stop();
    bindings_.clear();
}

void HotkeyListener::start(Callback cb) {
    if (running_ || bindings_.empty()) return;
    cb_ = std::move(cb);
    running_ = true;
    thread_ = std::thread(&HotkeyListener::run, this);
}

void HotkeyListener::stop() {
    running_ = false;
    if (thread_.joinable()) thread_.join();
}

void HotkeyListener::run() {
    using namespace std::chrono_literals;
    while (running_) {
        const bool focus = gameHasFocus();
        const bool ctrl = down(VK_CONTROL), shift = down(VK_SHIFT), alt = down(VK_MENU);
        for (auto &b : bindings_) {
            const bool isDown = focus && down(b.vk) && ctrl == b.ctrl && shift == b.shift && alt == b.alt;
            if (isDown && !b.wasDown && cb_) cb_(b.action);
            b.wasDown = isDown;
        }
        std::this_thread::sleep_for(15ms);
    }
}

} // namespace tc
