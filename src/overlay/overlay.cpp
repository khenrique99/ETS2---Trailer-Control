#include "overlay.h"

#include "controller.h"
#include "log.h"
#include "overlay_ui.h"

#include <windows.h>

#include <d3d11.h>
#include <dxgi1_2.h>

#include <backends/imgui_impl_dx11.h>
#include <backends/imgui_impl_win32.h>
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <mutex>

// Declarada em imgui_impl_win32.cpp dentro de "#if 0".
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace tc {

namespace {

// Posicoes na vtable de IDXGISwapChain / IDXGISwapChain1 (IUnknown 0-2, IDXGIObject 3-6,
// IDXGIDeviceSubObject 7, IDXGISwapChain 8..17, IDXGISwapChain1 18..).
constexpr int kSlotPresent = 8;
constexpr int kSlotPresent1 = 22;

using PresentFn = HRESULT(STDMETHODCALLTYPE *)(IDXGISwapChain *, UINT, UINT);
using Present1Fn = HRESULT(STDMETHODCALLTYPE *)(IDXGISwapChain1 *, UINT, UINT, const DXGI_PRESENT_PARAMETERS *);

// Tudo que os hooks (funcoes soltas) precisam ver. Vive o tempo da DLL.
struct Hooks {
    void **vtable = nullptr;
    PresentFn origPresent = nullptr;
    Present1Fn origPresent1 = nullptr;
    bool patchedPresent = false;
    bool patchedPresent1 = false;
    std::atomic<int> inflight{0};
    std::atomic<bool> failed{false};
    std::atomic<bool> shuttingDown{false};

    // ImGui + janela. Protegidos por mtx (WndProc e render rodam em threads diferentes).
    std::recursive_mutex mtx;
    bool imguiReady = false;
    HWND hwnd = nullptr;
    WNDPROC origWndProc = nullptr;
    bool unicodeWindow = true;
    ID3D11Device *device = nullptr;
    ID3D11DeviceContext *context = nullptr;
    IDXGISwapChain *swap = nullptr; // a swap chain da janela do jogo (as outras sao ignoradas)
    ui::Fonts fonts;
    ui::Context uiCtx;
    float fontScale = 0.0f;
    bool wasOpen = false;

    std::mutex stateMtx;
    OverlayState latest;
};

Hooks g;

thread_local bool tlInPresent = false;

struct InflightGuard {
    InflightGuard() { ++g.inflight; }
    ~InflightGuard() { --g.inflight; }
};

// ------------------------------------------------------------------ vtable

bool patchSlot(int slot, void *hook, void **orig) {
    void **entry = g.vtable + slot;
    DWORD old = 0;
    if (!VirtualProtect(entry, sizeof(void *), PAGE_READWRITE, &old)) return false;
    *orig = *entry;
    *entry = hook;
    DWORD ignored = 0;
    VirtualProtect(entry, sizeof(void *), old, &ignored);
    return true;
}

void restoreSlot(int slot, void *orig) {
    void **entry = g.vtable + slot;
    DWORD old = 0;
    if (VirtualProtect(entry, sizeof(void *), PAGE_READWRITE, &old)) {
        *entry = orig;
        DWORD ignored = 0;
        VirtualProtect(entry, sizeof(void *), old, &ignored);
    }
}

// ------------------------------------------------------------------ janela do jogo (mouse/teclado)

bool isRawMouseButtonUp(HRAWINPUT h) {
    RAWINPUT ri{};
    UINT size = sizeof(ri);
    if (GetRawInputData(h, RID_INPUT, &ri, &size, sizeof(RAWINPUTHEADER)) == static_cast<UINT>(-1)) return true;
    if (ri.header.dwType != RIM_TYPEMOUSE) return true; // teclado/HID: segue para o jogo
    constexpr USHORT kUps = RI_MOUSE_LEFT_BUTTON_UP | RI_MOUSE_RIGHT_BUTTON_UP | RI_MOUSE_MIDDLE_BUTTON_UP |
                            RI_MOUSE_BUTTON_4_UP | RI_MOUSE_BUTTON_5_UP;
    return (ri.data.mouse.usButtonFlags & kUps) != 0;
}

LRESULT callOriginal(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (g.unicodeWindow) return CallWindowProcW(g.origWndProc, h, m, w, l);
    return CallWindowProcA(g.origWndProc, h, m, w, l);
}

LRESULT defProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    return g.unicodeWindow ? DefWindowProcW(h, m, w, l) : DefWindowProcA(h, m, w, l);
}

LRESULT CALLBACK gameWndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    Overlay &ov = Overlay::instance();
    if (!ov.isOpen() || !g.imguiReady || g.shuttingDown.load()) return callOriginal(h, m, w, l);

    {
        std::lock_guard<std::recursive_mutex> lk(g.mtx);
        ImGui_ImplWin32_WndProcHandler(h, m, w, l);
    }

    switch (m) {
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        // Esc fecha; setas e ENTER sao a alternativa ao mouse. O resto nao chega ao jogo.
        if (w == VK_ESCAPE) ov.close();
        else if (w == VK_UP) g.uiCtx.keyNav.store(-1);
        else if (w == VK_DOWN) g.uiCtx.keyNav.store(1);
        else if (w == VK_RETURN) g.uiCtx.keyActivate.store(true);
        return 0;
    case WM_CHAR:
    case WM_SYSCHAR:
    case WM_DEADCHAR:
    case WM_UNICHAR:
        return 0;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONDBLCLK:
    case WM_MBUTTONDOWN:
    case WM_MBUTTONDBLCLK:
    case WM_XBUTTONDOWN:
    case WM_XBUTTONDBLCLK:
    case WM_MOUSEMOVE:
    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL:
        return 0;
    case WM_INPUT:
        // Movimento/clique do mouse nao chegam ao jogo; soltar de botao e teclado passam, para
        // nao deixar tecla ou botao "preso" no jogo.
        if (!isRawMouseButtonUp(reinterpret_cast<HRAWINPUT>(l))) return defProc(h, m, w, l);
        break;
    case WM_SETCURSOR:
        SetCursor(nullptr); // o ImGui desenha o proprio cursor
        return TRUE;
    default:
        break;
    }
    return callOriginal(h, m, w, l);
}

void subclassWindow(HWND hwnd) {
    if (g.origWndProc) return;
    g.unicodeWindow = IsWindowUnicode(hwnd) != FALSE;
    const LONG_PTR proc = reinterpret_cast<LONG_PTR>(&gameWndProc);
    LONG_PTR prev = g.unicodeWindow ? SetWindowLongPtrW(hwnd, GWLP_WNDPROC, proc)
                                    : SetWindowLongPtrA(hwnd, GWLP_WNDPROC, proc);
    g.origWndProc = reinterpret_cast<WNDPROC>(prev);
}

void unsubclassWindow() {
    if (!g.origWndProc || !g.hwnd) return;
    const LONG_PTR now = g.unicodeWindow ? GetWindowLongPtrW(g.hwnd, GWLP_WNDPROC) : GetWindowLongPtrA(g.hwnd, GWLP_WNDPROC);
    if (now == reinterpret_cast<LONG_PTR>(&gameWndProc)) {
        const LONG_PTR orig = reinterpret_cast<LONG_PTR>(g.origWndProc);
        if (g.unicodeWindow) SetWindowLongPtrW(g.hwnd, GWLP_WNDPROC, orig);
        else SetWindowLongPtrA(g.hwnd, GWLP_WNDPROC, orig);
        g.origWndProc = nullptr;
    }
    // Se outro programa se pendurou depois de nos, nao da para sair da cadeia: o proc continua
    // existindo e so repassa (shuttingDown).
}

// ------------------------------------------------------------------ ImGui

void releaseImGui() {
    if (!g.imguiReady) return;
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    if (g.context) g.context->Release();
    if (g.device) g.device->Release();
    g.context = nullptr;
    g.device = nullptr;
    g.swap = nullptr;
    g.imguiReady = false;
    g.fontScale = 0.0f;
}

float scaleFor(HWND hwnd) {
    RECT rc{};
    GetClientRect(hwnd, &rc);
    const float h = static_cast<float>(rc.bottom - rc.top);
    // Um pouco maior que o app externo: o painel e lido de longe, sentado no banco do caminhao.
    return std::clamp(h / 1080.0f * 1.15f, 0.8f, 3.5f);
}

// Janela principal do jogo: a swap chain que aponta para uma janela visivel deste processo.
bool isGameSwapChain(IDXGISwapChain *swap, HWND &hwnd) {
    DXGI_SWAP_CHAIN_DESC desc{};
    if (FAILED(swap->GetDesc(&desc)) || !desc.OutputWindow) return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(desc.OutputWindow, &pid);
    if (pid != GetCurrentProcessId() || !IsWindowVisible(desc.OutputWindow)) return false;
    if (desc.BufferDesc.Width < 640 || desc.BufferDesc.Height < 360) return false;
    hwnd = desc.OutputWindow;
    return true;
}

bool ensureImGui(IDXGISwapChain *swap) {
    ID3D11Device *dev = nullptr;
    if (FAILED(swap->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void **>(&dev))) || !dev) return false;

    if (g.imguiReady && (g.device != dev || g.swap != swap)) {
        // Outra swap chain: so a primeira valida e nossa. Dispositivo novo (troca de video): recomeca.
        if (g.device == dev) {
            dev->Release();
            return false;
        }
        releaseImGui();
    }
    if (!g.imguiReady) {
        HWND hwnd = nullptr;
        if (!isGameSwapChain(swap, hwnd)) {
            dev->Release();
            return false;
        }
        ID3D11DeviceContext *ctx = nullptr;
        dev->GetImmediateContext(&ctx);
        if (!ctx) {
            dev->Release();
            return false;
        }
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO &io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.LogFilename = nullptr;
        io.MouseDrawCursor = true;
        io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
        ImGui::StyleColorsLight();
        g.hwnd = hwnd;
        const float scale = scaleFor(hwnd);
        g.fonts = ui::loadFonts(scale);
        g.fontScale = scale;
        if (!ImGui_ImplWin32_Init(hwnd) || !ImGui_ImplDX11_Init(dev, ctx)) {
            ImGui::DestroyContext();
            ctx->Release();
            dev->Release();
            return false;
        }
        subclassWindow(hwnd);
        g.device = dev; // fica com a referencia do GetDevice
        g.context = ctx;
        g.swap = swap;
        g.imguiReady = true;
        log::info("Painel no jogo: pronto (janela %p).", static_cast<void *>(hwnd));
        return true;
    }
    dev->Release();
    return true;
}

// Desenha o painel por cima do quadro, na thread de render. Sem objetos com destrutor (SEH fora).
void renderFrame(IDXGISwapChain *swap) {
    std::lock_guard<std::recursive_mutex> lk(g.mtx);
    if (!ensureImGui(swap)) return;
    if (swap != g.swap) return;

    const float scale = scaleFor(g.hwnd);
    if (std::fabs(scale - g.fontScale) > 0.04f) {
        ImGui_ImplDX11_InvalidateDeviceObjects();
        g.fonts = ui::loadFonts(scale);
        g.fontScale = scale;
    }

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    {
        OverlayState st;
        {
            std::lock_guard<std::mutex> sl(g.stateMtx);
            st = g.latest;
        }
        ui::draw(st, g.uiCtx, g.fonts, g.fontScale);
    }
    ImGui::Render();

    ID3D11Texture2D *back = nullptr;
    if (SUCCEEDED(swap->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void **>(&back))) && back) {
        ID3D11RenderTargetView *rtv = nullptr;
        if (SUCCEEDED(g.device->CreateRenderTargetView(back, nullptr, &rtv)) && rtv) {
            g.context->OMSetRenderTargets(1, &rtv, nullptr);
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
            rtv->Release();
        }
        back->Release();
    }

    if (g.uiCtx.closeRequested) {
        g.uiCtx.closeRequested = false;
        Overlay::instance().close();
    }
}

bool renderSafe(IDXGISwapChain *swap) {
    __try {
        renderFrame(swap);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void maybeRender(IDXGISwapChain *swap, UINT flags) {
    if (g.failed.load() || g.shuttingDown.load() || (flags & DXGI_PRESENT_TEST)) return;
    if (!Overlay::instance().isOpen()) return;
    if (!renderSafe(swap)) {
        g.failed = true;
        Overlay::instance().close();
        log::error("Painel no jogo: erro ao desenhar; desligado nesta sessao.");
    }
}

HRESULT STDMETHODCALLTYPE presentHook(IDXGISwapChain *swap, UINT sync, UINT flags) {
    InflightGuard guard;
    const bool outer = !tlInPresent;
    if (outer) {
        tlInPresent = true;
        maybeRender(swap, flags);
    }
    const HRESULT hr = g.origPresent(swap, sync, flags);
    if (outer) tlInPresent = false;
    return hr;
}

HRESULT STDMETHODCALLTYPE present1Hook(IDXGISwapChain1 *swap, UINT sync, UINT flags,
                                       const DXGI_PRESENT_PARAMETERS *params) {
    InflightGuard guard;
    const bool outer = !tlInPresent;
    if (outer) {
        tlInPresent = true;
        maybeRender(swap, flags);
    }
    const HRESULT hr = g.origPresent1(swap, sync, flags, params);
    if (outer) tlInPresent = false;
    return hr;
}

// ------------------------------------------------------------------ instalacao

// Cria uma swap chain descartavel (janela escondida) so para ler a vtable do dxgi.dll.
void **findSwapChainVtable() {
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"TrailerControlDummyDx";
    RegisterClassExW(&wc);
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"", WS_OVERLAPPED, 0, 0, 16, 16, nullptr, nullptr,
                                wc.hInstance, nullptr);
    void **vtable = nullptr;
    if (hwnd) {
        DXGI_SWAP_CHAIN_DESC sd{};
        sd.BufferCount = 1;
        sd.BufferDesc.Width = 16;
        sd.BufferDesc.Height = 16;
        sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.OutputWindow = hwnd;
        sd.SampleDesc.Count = 1;
        sd.Windowed = TRUE;
        sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

        IDXGISwapChain *sc = nullptr;
        ID3D11Device *dev = nullptr;
        ID3D11DeviceContext *ctx = nullptr;
        D3D_FEATURE_LEVEL level{};
        const D3D_DRIVER_TYPE types[] = {D3D_DRIVER_TYPE_HARDWARE, D3D_DRIVER_TYPE_WARP};
        for (D3D_DRIVER_TYPE t : types) {
            if (SUCCEEDED(D3D11CreateDeviceAndSwapChain(nullptr, t, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &sd,
                                                        &sc, &dev, &level, &ctx))) {
                break;
            }
        }
        if (sc) {
            vtable = *reinterpret_cast<void ***>(sc);
            sc->Release();
        }
        if (ctx) ctx->Release();
        if (dev) dev->Release();
        DestroyWindow(hwnd);
    }
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    return vtable;
}

} // namespace

// ------------------------------------------------------------------ Overlay

Overlay &Overlay::instance() {
    static Overlay inst;
    return inst;
}

bool Overlay::blockedByMultiplayer() {
    // TruckersMP: nada de hook de video (risco de banimento). Convoy (SCS) pode.
    return GetModuleHandleW(L"core_ets2mp.dll") != nullptr || GetModuleHandleW(L"core_atsmp.dll") != nullptr;
}

void Overlay::configure(bool enabled) {
    enabled_ = enabled;
    if (!enabled) {
        close();
        return;
    }
    if (started_.exchange(true)) return;
    if (blockedByMultiplayer()) {
        log::warn("Painel no jogo: desligado no TruckersMP.");
        return;
    }
    installThread_ = std::thread([this] {
        g.vtable = findSwapChainVtable();
        if (!g.vtable) {
            log::warn("Painel no jogo: nao achei a swap chain do DirectX 11 (o jogo esta em DX11?). "
                      "Usando o app externo.");
            return;
        }
        g.patchedPresent = patchSlot(kSlotPresent, reinterpret_cast<void *>(&presentHook),
                                     reinterpret_cast<void **>(&g.origPresent));
        g.patchedPresent1 = patchSlot(kSlotPresent1, reinterpret_cast<void *>(&present1Hook),
                                      reinterpret_cast<void **>(&g.origPresent1));
        if (!g.patchedPresent && !g.patchedPresent1) {
            log::warn("Painel no jogo: nao consegui instalar o desvio do Present. Usando o app externo.");
            return;
        }
        hooked_ = true;
        log::info("Painel no jogo: instalado (Present%s%s).", g.patchedPresent ? "" : " sem Present",
                  g.patchedPresent1 ? " + Present1" : "");
    });
}

void Overlay::shutdown() {
    enabled_ = false;
    open_ = false;
    g.shuttingDown = true;
    if (installThread_.joinable()) installThread_.join();
    if (g.vtable) {
        if (g.patchedPresent) restoreSlot(kSlotPresent, reinterpret_cast<void *>(g.origPresent));
        if (g.patchedPresent1) restoreSlot(kSlotPresent1, reinterpret_cast<void *>(g.origPresent1));
    }
    hooked_ = false;
    // Espera quem ainda estiver dentro dos nossos hooks (antes de a DLL sair da memoria).
    for (int i = 0; i < 200 && g.inflight.load() > 0; ++i) Sleep(5);
    // Se um erro de SEH interrompeu o desenho com o mutex preso, nao trava o fechamento do jogo.
    std::unique_lock<std::recursive_mutex> lk(g.mtx, std::try_to_lock);
    if (lk.owns_lock()) {
        unsubclassWindow();
        releaseImGui();
    }
}

void Overlay::toggle() {
    if (!active() || g.failed.load()) return;
    const bool now = !open_.load();
    if (now) {
        g.uiCtx.keyNav = 0;
        g.uiCtx.keyActivate = false;
    }
    open_ = now;
}

void Overlay::close() { open_ = false; }

void Overlay::publish(OverlayState state) {
    std::lock_guard<std::mutex> lk(g.stateMtx);
    g.latest = std::move(state);
}

} // namespace tc
