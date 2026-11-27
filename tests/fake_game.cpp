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

#include <d3d11.h>
#include <dxgi.h>

#include <scssdk_input.h>
#include <scssdk_telemetry.h>
#include <eurotrucks2/scssdk_eut2.h>
#include <eurotrucks2/scssdk_telemetry_eut2.h>

#include "json.h"
#include "shared/ini_edit.h"
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

std::wstring widen(const std::string &utf8) {
    const int n = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    std::wstring w(n > 0 ? static_cast<size_t>(n - 1) : 0, L'\0');
    if (n > 1) MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &w[0], n);
    return w;
}

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
                 v["app_hotkey"].asString() == "CTRL+MINUS" && !v["config_path"].asString().empty() &&
                 v["blinker_sound"].asBool(false) && v["blinker_volume"].asInt() == 60; // padrao do .ini
        }
        std::printf("[teste] %-14s -> %s  (%s)\n", "recarregar", ok ? "OK" : "FALHOU",
                    v["last_command"]["result"].asString().c_str());
        if (!ok) ++failures;
    }
    // Editar o .ini (a mao ou pela tela) vale sozinho, sem comando: o plugin vigia o arquivo.
    {
        tc::json::Value v;
        std::string original;
        const bool read = readStatus(v) && tc::ipc::readAll(widen(v["config_path"].asString()), original);
        const std::wstring path = widen(v["config_path"].asString());
        auto waitState = [&](int volume, bool sound) {
            for (int f = 0; f < 400; ++f) { // ~6 s: o plugin checa o arquivo a cada 1 s
                frame(held);
                if (readStatus(v) && v["blinker_volume"].asInt() == volume && v["blinker_sound"].asBool(!sound) == sound)
                    return true;
            }
            return false;
        };
        bool ok = read && tc::ipc::writeAtomic(path, tc::ini::apply(original, {{"som", "volume", "30"}})) &&
                  waitState(30, true);
        ok = ok && tc::ipc::writeAtomic(path, tc::ini::apply(original, {{"som", "alarme_seta", "0"}})) &&
             waitState(60, false);
        // Devolve o .ini como estava (e confere que o plugin acompanha)
        ok = tc::ipc::writeAtomic(path, original) && waitState(60, true) && ok;
        std::printf("[teste] %-14s -> %s\n", "ini vigiado", ok ? "OK" : "FALHOU");
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
    // Multiplayer: Convoy liberado, TruckersMP bloqueado.
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
        // Convoy liberado (permitir_convoy = 1, o padrao): o backend NAO pode desligar.
        setOffset(37);
        bool convoyStayedOn = true;
        tc::json::Value v;
        for (int f = 0; f < 180; ++f) { // 3 s: passa por pelo menos uma checagem
            frame(held);
            if (readStatus(v) && v["memory_status"].asString().find("multiplayer") != std::string::npos)
                convoyStayedOn = false;
        }
        setOffset(0);
        std::printf("[teste] %-14s -> %s\n", "convoy", convoyStayedOn ? "OK" : "FALHOU");
        if (!convoyStayedOn) ++failures;

        // TruckersMP: a DLL dele no processo desliga o backend; ao sair, volta.
        wchar_t dir[MAX_PATH];
        GetModuleFileNameW(nullptr, dir, MAX_PATH);
        std::wstring tmp(dir);
        tmp = tmp.substr(0, tmp.find_last_of(L"\\/")) + L"\\core_ets2mp.dll";
        HMODULE fakeTmp = LoadLibraryW(tmp.c_str());
        const bool off = fakeTmp && waitStatus("TruckersMP", true);
        if (fakeTmp) FreeLibrary(fakeTmp);
        const bool back = waitStatus("multiplayer", false);
        std::printf("[teste] %-14s -> %s\n", "truckersmp", off && back ? "OK" : "FALHOU");
        if (!(off && back)) ++failures;
    }
    std::printf("[teste] %d falha(s)\n", failures);
    return failures;
}

// ---- teste do painel dentro do jogo: janela DX11 de verdade; abre com Ctrl + -, tira "foto" da tela
// (e, se pedido, clica num ponto) para conferir o desenho e a captura de entrada sem o ETS2.
LRESULT CALLBACK testWndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

bool saveScreenBmp(HWND hwnd, const std::wstring &path) {
    RECT rc{};
    GetClientRect(hwnd, &rc);
    POINT origin{0, 0};
    ClientToScreen(hwnd, &origin);
    const int w = rc.right, h = rc.bottom;
    HDC screen = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(screen);
    HBITMAP bmp = CreateCompatibleBitmap(screen, w, h);
    HGDIOBJ old = SelectObject(mem, bmp);
    BitBlt(mem, 0, 0, w, h, screen, origin.x, origin.y, SRCCOPY | CAPTUREBLT);
    BITMAPINFOHEADER bi{sizeof(bi), w, -h, 1, 32, BI_RGB, 0, 0, 0, 0, 0};
    std::vector<unsigned char> px(static_cast<size_t>(w) * h * 4);
    GetDIBits(mem, bmp, 0, static_cast<UINT>(h), px.data(), reinterpret_cast<BITMAPINFO *>(&bi), DIB_RGB_COLORS);
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);

    BITMAPFILEHEADER fh{0x4D42, static_cast<DWORD>(sizeof(fh) + sizeof(bi) + px.size()), 0, 0,
                        sizeof(fh) + sizeof(bi)};
    bi.biHeight = -h;
    FILE *f = _wfopen(path.c_str(), L"wb");
    if (!f) return false;
    fwrite(&fh, sizeof(fh), 1, f);
    fwrite(&bi, sizeof(bi), 1, f);
    fwrite(px.data(), 1, px.size(), f);
    fclose(f);
    return true;
}

// Componente verde do pixel (10,10) da area do jogo: o cenario e 82; com o painel aberto fica escurecido.
int backdropGreen(HWND hwnd) {
    POINT o{10, 10};
    ClientToScreen(hwnd, &o);
    HDC screen = GetDC(nullptr);
    const COLORREF c = GetPixel(screen, o.x, o.y);
    ReleaseDC(nullptr, screen);
    return GetGValue(c);
}

void sendKey(WORD vk, bool down) {
    INPUT in{};
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = vk;
    in.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
    SendInput(1, &in, sizeof(in));
}

void sendComboCtrlMinus() {
    sendKey(VK_CONTROL, true);
    sendKey(VK_OEM_MINUS, true);
    Sleep(120);
    sendKey(VK_OEM_MINUS, false);
    sendKey(VK_CONTROL, false);
}


int overlayTest(bool &held, const std::wstring &outPath, int clickX, int clickY) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2); // como o jogo: coordenadas reais
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = testWndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"FakeGameDx11";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassExW(&wc);
    RECT r{0, 0, 1600, 900};
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"Jogo falso DX11", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 60, 40,
                                r.right - r.left, r.bottom - r.top, nullptr, nullptr, wc.hInstance, nullptr);

    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 1600;
    sd.BufferDesc.Height = 900;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    IDXGISwapChain *sc = nullptr;
    ID3D11Device *dev = nullptr;
    ID3D11DeviceContext *ctx = nullptr;
    if (FAILED(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
                                             D3D11_SDK_VERSION, &sd, &sc, &dev, nullptr, &ctx))) {
        std::printf("[teste] nao criou o dispositivo DX11\n");
        return 1;
    }
    ID3D11Texture2D *back = nullptr;
    ID3D11RenderTargetView *rtv = nullptr;
    sc->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void **>(&back));
    dev->CreateRenderTargetView(back, nullptr, &rtv);
    back->Release();

    int failures = 0;
    const bool doClick = clickX >= 0;
    const int kOpen = 60, kShot = 140, kClick = 160, kCheck = 240, kClose = 260, kEnd = 320;
    for (int f = 0; f <= kEnd; ++f) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        scs_telemetry_frame_start_t fs{};
        fireEvent(SCS_TELEMETRY_EVENT_frame_start, &fs);
        pumpInput(held);
        scs_value_t speed{};
        speed.type = SCS_VALUE_TYPE_float;
        speed.value_float.value = 0.f;
        fireChannel(SCS_TELEMETRY_TRUCK_CHANNEL_speed, speed);
        fireEvent(SCS_TELEMETRY_EVENT_frame_end, nullptr);

        const float bg[4] = {0.18f, 0.32f, 0.22f, 1.0f}; // "cenario" verde para ver o escurecimento
        ctx->OMSetRenderTargets(1, &rtv, nullptr);
        ctx->ClearRenderTargetView(rtv, bg);

        if (f == 20) {
            ShowWindow(hwnd, SW_SHOW);
            SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
            SetForegroundWindow(hwnd);
        }
        if (f == kOpen) sendComboCtrlMinus();
        if (f == kShot) {
            Sleep(100);
            std::printf("[teste] foto do painel: %s\n", saveScreenBmp(hwnd, outPath) ? "ok" : "FALHOU");
            const int dim = backdropGreen(hwnd);
            std::printf("[teste] %s painel aberto escurece o jogo (verde %d)\n", dim < 70 ? "OK:" : "FALHA:", dim);
            failures += dim < 70 ? 0 : 1;
        }
        if (doClick && f >= kClick && f <= kClick + 20) {
            // Cursor real no ponto (o ImGui le a posicao por GetCursorPos) e botoes por mensagem direta para
            // a janela do teste: nao arrisca clicar em outro programa da area de trabalho.
            POINT o{clickX, clickY};
            ClientToScreen(hwnd, &o);
            const LPARAM lp = MAKELPARAM(clickX, clickY);
            if (f == kClick) std::printf("[teste] clicando em (%d, %d); conjunto engatado antes: %d\n", clickX, clickY, g_trailerCoupled);
            if (f < kClick + 15) SetCursorPos(o.x, o.y);
            if (f == kClick + 5) PostMessageW(hwnd, WM_MOUSEMOVE, 0, lp);
            if (f == kClick + 8) PostMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, lp);
            if (f == kClick + 12) PostMessageW(hwnd, WM_LBUTTONUP, 0, lp);
        }
        if (doClick && f == kCheck) {
            const bool ok = !g_trailerCoupled;
            std::printf("[teste] %s clique no painel desengatou o conjunto\n", ok ? "OK:" : "FALHA:");
            failures += ok ? 0 : 1;
            saveScreenBmp(hwnd, outPath + L".depois.bmp");
        }
        if (f == kClose) {
            std::printf("[teste] primeiro plano antes de fechar: %d\n", GetForegroundWindow() == hwnd);
            sendComboCtrlMinus();
        }
        if (f == kEnd) {
            const int g2 = backdropGreen(hwnd);
            const bool ok = g2 >= 78;
            std::printf("[teste] %s painel fechado devolve o jogo (verde %d)\n", ok ? "OK:" : "FALHA:", g2);
            failures += ok ? 0 : 1;
        }
        sc->Present(1, 0);
    }
    if (rtv) rtv->Release();
    if (ctx) ctx->Release();
    if (sc) sc->Release();
    if (dev) dev->Release();
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
    if (argc >= 4 && wcscmp(argv[2], L"--overlay") == 0) {
        const int cx = argc >= 6 ? _wtoi(argv[4]) : -1, cy = argc >= 6 ? _wtoi(argv[5]) : -1;
        const int failures = overlayTest(held, argv[3], cx, cy);
        telShutdown();
        inShutdown();
        FreeLibrary(dll);
        return failures == 0 ? 0 : 1;
    }
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
