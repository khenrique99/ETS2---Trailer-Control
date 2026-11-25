#include "hotkey_text.h"

#include <windows.h>

#include <cctype>
#include <sstream>

namespace tc::app {

std::string vkToIniName(unsigned vk) {
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) return std::string(1, static_cast<char>(vk));
    if (vk >= VK_F1 && vk <= VK_F24) return "F" + std::to_string(vk - VK_F1 + 1);
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) return "NUMPAD" + std::to_string(vk - VK_NUMPAD0);
    switch (vk) {
    case VK_OEM_MINUS:
    case VK_SUBTRACT: return "MINUS"; // o plugin aceita os dois "-"
    case VK_OEM_PLUS: return "PLUS";
    case VK_ADD: return "NUMPLUS";
    case VK_LEFT: return "LEFT";
    case VK_RIGHT: return "RIGHT";
    case VK_UP: return "UP";
    case VK_DOWN: return "DOWN";
    case VK_HOME: return "HOME";
    case VK_END: return "END";
    case VK_INSERT: return "INSERT";
    case VK_DELETE: return "DELETE";
    case VK_PRIOR: return "PGUP";
    case VK_NEXT: return "PGDN";
    case VK_SPACE: return "SPACE";
    case VK_RETURN: return "ENTER";
    case VK_TAB: return "TAB";
    case VK_BACK: return "BACKSPACE";
    }
    return {};
}

std::string makeCombo(bool ctrl, bool shift, bool alt, unsigned vk) {
    const std::string key = vkToIniName(vk);
    if (key.empty()) return {};
    std::string c;
    if (ctrl) c += "CTRL+";
    if (shift) c += "SHIFT+";
    if (alt) c += "ALT+";
    return c + key;
}

std::wstring prettyCombo(const std::string &combo) {
    static const struct {
        const char *ini;
        const wchar_t *shown;
    } names[] = {
        {"CTRL", L"Ctrl"},       {"SHIFT", L"Shift"},     {"ALT", L"Alt"},         {"MINUS", L"-"},
        {"PLUS", L"="},          {"NUMPLUS", L"+ (num)"}, {"NUMMINUS", L"- (num)"}, {"LEFT", L"←"},
        {"RIGHT", L"→"},         {"UP", L"↑"},            {"DOWN", L"↓"},          {"PGUP", L"Page Up"},
        {"PGDN", L"Page Down"},  {"HOME", L"Home"},       {"END", L"End"},         {"INSERT", L"Insert"},
        {"DELETE", L"Delete"},   {"SPACE", L"Espaço"},    {"ENTER", L"Enter"},     {"TAB", L"Tab"},
        {"BACKSPACE", L"Backspace"},
    };
    std::wstring out;
    std::stringstream ss(combo);
    std::string part;
    while (std::getline(ss, part, '+')) {
        for (auto &ch : part) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        std::wstring shown;
        for (const auto &n : names)
            if (part == n.ini) shown = n.shown;
        if (shown.empty()) {
            if (part.rfind("NUMPAD", 0) == 0 && part.size() == 7) shown = L"Num " + std::wstring(1, part[6]);
            else shown.assign(part.begin(), part.end());
        }
        if (!out.empty()) out += L" + ";
        out += shown;
    }
    return out;
}

} // namespace tc::app
