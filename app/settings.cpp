#include "settings.h"

#include "hotkey_text.h"
#include "model.h"
#include "shared/ini_edit.h"
#include "shared/ipc.h"

#include <windows.h>

#include <cctype>

namespace tc::app {

std::string narrow(const std::wstring &w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), &s[0], n, nullptr, nullptr);
    return s;
}

namespace {

std::wstring prefsPath() {
    const std::wstring d = ipc::folder();
    return d.empty() ? d : d + L"\\app.ini";
}

} // namespace

void AppPrefs::load() {
    std::string text;
    if (!ipc::readAll(prefsPath(), text)) return;
    const std::string r = ini::get(text, "app", "voltar_ao_jogo");
    if (!r.empty()) returnToGame = r == "1";
    lastConfigPath = widen(ini::get(text, "app", "ultimo_config"));
}

bool AppPrefs::save() const {
    const std::wstring path = prefsPath();
    if (path.empty()) return false;
    std::string text;
    ipc::readAll(path, text);
    text = ini::apply(text, {{"app", "voltar_ao_jogo", returnToGame ? "1" : "0"},
                             {"app", "ultimo_config", narrow(lastConfigPath)}});
    return ipc::writeAtomic(path, text);
}

// ------------------------------------------------------------------ janela: so o atalho
//
// Mostra o atalho atual; "Alterar" entra no modo de captura: a proxima combinacao apertada
// vira o atalho (Esc cancela). Salvar grava [atalhos] abrir_tela no trailer_control.ini.

namespace {

constexpr const char *kDefaultCombo = "CTRL+MINUS";

enum Ctl {
    kLblCombo = 101,
    kLblHint,
    kBtnChange,
    kBtnDefault,
    kBtnSave,
    kBtnCancel,
};

struct Dialog {
    HWND hwnd = nullptr;
    HFONT font = nullptr, bold = nullptr, big = nullptr;
    float scale = 1.0f;
    std::wstring configPath;
    std::string iniText;
    std::string combo;     // atalho escolhido (formato do .ini)
    bool capturing = false;
    bool saved = false;
    bool done = false;
};
Dialog *g_dlg = nullptr;

int Sx(int v) { return static_cast<int>(static_cast<float>(v) * g_dlg->scale + 0.5f); }

HWND add(const wchar_t *cls, const wchar_t *text, DWORD style, int x, int y, int w, int h, int id, HFONT font) {
    HWND c = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, Sx(x), Sx(y), Sx(w), Sx(h), g_dlg->hwnd,
                             reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr);
    SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    return c;
}

HWND item(int id) { return GetDlgItem(g_dlg->hwnd, id); }

void refresh() {
    SetWindowTextW(item(kLblCombo), g_dlg->capturing ? L"Aperte a combinação…" : prettyCombo(g_dlg->combo).c_str());
    SetWindowTextW(item(kLblHint), g_dlg->capturing
                                       ? L"Ex.: Ctrl + -, Ctrl + F9, Alt + T.  Esc cancela."
                                       : L"Clique em Alterar e aperte a combinação de teclas que quiser.");
    SetWindowTextW(item(kBtnChange), g_dlg->capturing ? L"Cancelar captura" : L"Alterar…");
    EnableWindow(item(kBtnSave), !g_dlg->capturing && !g_dlg->iniText.empty());
    EnableWindow(item(kBtnDefault), !g_dlg->capturing);
}

void build() {
    const bool haveIni = !g_dlg->iniText.empty();
    add(L"STATIC", L"Atalho no jogo para abrir o Trailer Control", SS_LEFT, 20, 18, 420, 24, 0, g_dlg->bold);
    add(L"STATIC", L"", SS_CENTER | SS_CENTERIMAGE | SS_SUNKEN | SS_NOPREFIX, 20, 52, 400, 56, kLblCombo, g_dlg->big);
    add(L"STATIC", L"", SS_LEFT, 20, 116, 400, 22, kLblHint, g_dlg->font);
    add(L"BUTTON", L"Alterar…", BS_PUSHBUTTON | WS_TABSTOP, 20, 146, 160, 34, kBtnChange, g_dlg->font);
    add(L"BUTTON", L"Usar o padrão (Ctrl + -)", BS_PUSHBUTTON | WS_TABSTOP, 190, 146, 230, 34, kBtnDefault,
        g_dlg->font);
    if (!haveIni) {
        add(L"STATIC", L"Abra o jogo com o plugin uma vez para a tela achar o trailer_control.ini.", SS_LEFT, 20,
            192, 400, 40, 0, g_dlg->font);
        EnableWindow(item(kBtnChange), FALSE);
        EnableWindow(item(kBtnDefault), FALSE);
    }
    add(L"BUTTON", L"Cancelar", BS_PUSHBUTTON | WS_TABSTOP, 210, 240, 100, 34, kBtnCancel, g_dlg->font);
    add(L"BUTTON", L"Salvar", BS_DEFPUSHBUTTON | WS_TABSTOP, 320, 240, 100, 34, kBtnSave, g_dlg->font);
    refresh();
}

void save() {
    const std::string updated = ini::apply(g_dlg->iniText, {{"atalhos", "abrir_tela", g_dlg->combo}});
    if (!ipc::writeAtomic(g_dlg->configPath, updated)) {
        MessageBoxW(g_dlg->hwnd,
                    (L"Não consegui gravar:\n" + g_dlg->configPath +
                     L"\n\nA pasta do jogo pode estar protegida. Abra o TrailerControl.exe como administrador "
                     L"uma vez, ou edite \"abrir_tela\" no arquivo à mão.")
                        .c_str(),
                    L"Configurações", MB_ICONERROR);
        return;
    }
    g_dlg->saved = true;
    g_dlg->done = true;
}

bool isModifier(unsigned vk) {
    return vk == VK_CONTROL || vk == VK_LCONTROL || vk == VK_RCONTROL || vk == VK_SHIFT || vk == VK_LSHIFT ||
           vk == VK_RSHIFT || vk == VK_MENU || vk == VK_LMENU || vk == VK_RMENU || vk == VK_LWIN || vk == VK_RWIN;
}

// Tecla apertada durante a captura. true = consumida.
bool onCaptureKey(unsigned vk) {
    if (isModifier(vk)) return true; // espera a tecla "de verdade"
    if (vk == VK_ESCAPE) {
        g_dlg->capturing = false;
        refresh();
        return true;
    }
    const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    const bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
    const bool fkey = vk >= VK_F1 && vk <= VK_F24;
    const std::string combo = makeCombo(ctrl, shift, alt, vk);
    if (combo.empty()) {
        SetWindowTextW(item(kLblHint), L"Essa tecla não pode ser usada. Tente outra.");
        return true;
    }
    if (!ctrl && !alt && !fkey) {
        // Sem Ctrl/Alt a tecla sozinha ia disparar durante o jogo normal.
        SetWindowTextW(item(kLblHint), L"Use junto com Ctrl ou Alt (ou uma tecla F1–F24).");
        return true;
    }
    g_dlg->combo = combo;
    g_dlg->capturing = false;
    refresh();
    SetFocus(item(kBtnSave));
    return true;
}

LRESULT CALLBACK proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    // So trata mensagens enquanto o dialogo dono desta janela esta ativo.
    if (!g_dlg || g_dlg->hwnd != hwnd) return DefWindowProcW(hwnd, msg, wp, lp);
    switch (msg) {
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case kBtnSave: save(); return 0;
        case IDCANCEL:
        case kBtnCancel: g_dlg->done = true; return 0;
        case kBtnChange:
            g_dlg->capturing = !g_dlg->capturing;
            refresh();
            SetFocus(hwnd); // as teclas vao para a janela, nao para um botao
            return 0;
        case kBtnDefault:
            g_dlg->combo = kDefaultCombo;
            refresh();
            return 0;
        }
        break;
    case WM_CTLCOLORSTATIC: {
        HDC dc = reinterpret_cast<HDC>(wp);
        SetBkColor(dc, GetSysColor(COLOR_WINDOW));
        if (reinterpret_cast<HWND>(lp) == item(kLblCombo))
            SetTextColor(dc, g_dlg->capturing ? RGB(154, 103, 0) : RGB(37, 99, 235));
        return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
    }
    case WM_CLOSE:
        g_dlg->done = true;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace

bool showSettingsDialog(HWND owner, const std::wstring &configPath, AppPrefs &prefs) {
    (void)prefs;
    if (g_dlg) { // ja tem uma aberta: so traz para frente
        SetForegroundWindow(g_dlg->hwnd);
        return false;
    }
    Dialog dlg;
    g_dlg = &dlg;
    dlg.configPath = configPath;
    dlg.scale = static_cast<float>(GetDpiForWindow(owner)) / 96.0f;
    if (!configPath.empty()) ipc::readAll(configPath, dlg.iniText);
    dlg.combo = ini::get(dlg.iniText, "atalhos", "abrir_tela");
    for (auto &c : dlg.combo) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    if (dlg.combo.empty()) dlg.combo = kDefaultCombo;

    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{sizeof(wc)};
        wc.lpfnWndProc = proc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
        wc.lpszClassName = L"ETS2TrailerControlSettings";
        RegisterClassExW(&wc);
        registered = true;
    }

    auto mkFont = [](int px, int weight) {
        return CreateFontW(-Sx(px), 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    };
    dlg.font = mkFont(14, FW_NORMAL);
    dlg.bold = mkFont(15, FW_SEMIBOLD);
    dlg.big = mkFont(24, FW_SEMIBOLD);

    const UINT dpi = GetDpiForWindow(owner);
    RECT rc{0, 0, Sx(460), Sx(292)};
    AdjustWindowRectExForDpi(&rc, WS_CAPTION | WS_SYSMENU, FALSE, WS_EX_DLGMODALFRAME, dpi);
    RECT orc;
    GetWindowRect(owner, &orc);
    const int w = rc.right - rc.left, h = rc.bottom - rc.top;
    dlg.hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, L"ETS2TrailerControlSettings", L"Configurações",
                               WS_CAPTION | WS_SYSMENU, orc.left + ((orc.right - orc.left) - w) / 2,
                               orc.top + ((orc.bottom - orc.top) - h) / 2, w, h, owner, nullptr, nullptr, nullptr);
    build();
    EnableWindow(owner, FALSE);
    ShowWindow(dlg.hwnd, SW_SHOW);

    MSG m{};
    bool quit = false;
    while (!dlg.done) {
        if (GetMessageW(&m, nullptr, 0, 0) <= 0) {
            quit = true;
            break;
        }
        const bool key = m.message == WM_KEYDOWN || m.message == WM_SYSKEYDOWN;
        if (key && dlg.capturing && onCaptureKey(static_cast<unsigned>(m.wParam))) continue;
        if (key && m.wParam == VK_ESCAPE) {
            dlg.done = true;
            continue;
        }
        if (!IsDialogMessageW(dlg.hwnd, &m)) {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
    }
    EnableWindow(owner, TRUE);
    SetForegroundWindow(owner);
    g_dlg = nullptr; // antes de destruir: mensagens da destruicao vao para DefWindowProc
    DestroyWindow(dlg.hwnd);
    DeleteObject(dlg.font);
    DeleteObject(dlg.bold);
    DeleteObject(dlg.big);
    g_dlg = nullptr;
    if (quit) PostQuitMessage(static_cast<int>(m.wParam));
    return dlg.saved;
}

} // namespace tc::app
