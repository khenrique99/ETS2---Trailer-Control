// Trailer Control - app externo.
//
// Janela que mostra a composicao do caminhao (lida de status.json, escrito pelo plugin dentro
// do jogo) e manda comandos de engatar/desengatar (command.txt). Funciona em outro monitor;
// nao toca no processo do jogo. Sem o jogo aberto, o "modo demonstracao" simula um rodotrem.
//
// Uso: TrailerControl.exe [--demo]

#include "blinker_sound.h"
#include "model.h"
#include "hotkey_text.h"
#include "settings.h"
#include "shared/ini_edit.h"
#include "shared/ipc.h"

#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <mmsystem.h>

#include <objidl.h>
#include <gdiplus.h>

#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <vector>

#pragma comment(lib, "gdiplus.lib")

using namespace Gdiplus;
using namespace tc::app;

namespace {

// ------------------------------------------------------------------ cores / medidas

struct Palette {
    Color bg{255, 243, 244, 246};
    Color header{255, 24, 31, 42};
    Color headerText{255, 255, 255, 255};
    Color card{255, 255, 255, 255};
    Color border{255, 222, 226, 231};
    Color text{255, 31, 35, 40};
    Color muted{255, 101, 109, 118};
    Color accent{255, 37, 99, 235};
    Color accentHover{255, 29, 78, 216};
    Color green{255, 26, 127, 55};
    Color greenSoft{255, 220, 245, 228};
    Color red{255, 207, 34, 46};
    Color redSoft{255, 255, 230, 232};
    Color amber{255, 154, 103, 0};
    Color amberSoft{255, 255, 244, 214};
    Color gray{255, 140, 149, 159};
    Color graySoft{255, 234, 238, 242};
    Color selected{255, 232, 240, 254};
    Color truck{255, 37, 99, 235};
    Color trailer{255, 71, 85, 105};
    Color dolly{255, 180, 120, 0};
};
const Palette P;

enum HitId {
    kHitNone = 0,
    kHitTopmost = 1,
    kHitFolder = 2,
    kHitDemo = 3,
    kHitDiagnose = 4,
    kHitSettings = 5,
    kHitBlinkerSound = 6,
    kHitVolume = 7,
    kHitRowAction = 1000, // + indice da articulacao
    kHitRow = 2000,       // + indice
    kHitNode = 3000,      // + indice
};

struct Hit {
    RectF r;
    int id;
    bool enabled;
};

// ------------------------------------------------------------------ estado do app

struct App {
    HWND hwnd = nullptr;
    float scale = 1.0f;
    std::unique_ptr<Source> source;
    GameState state;
    int selected = 0;
    bool topmost = false;
    HWND gameHwnd = nullptr; // janela do jogo (vem do plugin) para "voltar ao jogo"
    AppPrefs prefs;
    std::vector<Hit> hits;
    int hover = kHitNone;
    int pressed = kHitNone;
    bool tracking = false;
    bool draggingVolume = false;
    int volumeDraft = -1;       // valor enquanto arrasta o controle (-1 = nenhum)
    RectF volumeTrack;          // area util da barra de volume (ultima pintura)

    struct PendingCmd {
        long long seq;
        int joint;
        unsigned long long sentAt;
    };
    std::vector<PendingCmd> pending;
    std::wstring localMessage; // mensagens do proprio app (ex.: sem resposta)
    bool localMessageOk = true;
    long long shownSeq = 0;
};
App g;

void returnToGame();

// Atalho que abre a tela (vem do plugin); com a tela na frente, ele tambem volta ao jogo.
std::string appHotkey() {
    const std::string h = narrow(g.state.appHotkey);
    return h.empty() ? std::string("CTRL+MINUS") : h;
}

bool isAppHotkey(WPARAM vk) {
    const std::string c = makeCombo((GetKeyState(VK_CONTROL) & 0x8000) != 0, (GetKeyState(VK_SHIFT) & 0x8000) != 0,
                                    (GetKeyState(VK_MENU) & 0x8000) != 0, static_cast<unsigned>(vk));
    return !c.empty() && c == appHotkey();
}

float S(float v) { return v * g.scale; }

// ------------------------------------------------------------------ desenho util

void roundRectPath(GraphicsPath &path, const RectF &r, float radius) {
    const float d = radius * 2;
    path.Reset();
    path.AddArc(r.X, r.Y, d, d, 180, 90);
    path.AddArc(r.X + r.Width - d, r.Y, d, d, 270, 90);
    path.AddArc(r.X + r.Width - d, r.Y + r.Height - d, d, d, 0, 90);
    path.AddArc(r.X, r.Y + r.Height - d, d, d, 90, 90);
    path.CloseFigure();
}

void fillRound(Graphics &gr, const RectF &r, float radius, const Color &c) {
    GraphicsPath path;
    roundRectPath(path, r, radius);
    SolidBrush b(c);
    gr.FillPath(&b, &path);
}

void strokeRound(Graphics &gr, const RectF &r, float radius, const Color &c, float width = 1.0f) {
    GraphicsPath path;
    roundRectPath(path, r, radius);
    Pen pen(c, width);
    gr.DrawPath(&pen, &path);
}

enum class Align { Left, Center, Right };

void text(Graphics &gr, const std::wstring &s, const RectF &r, float sizePx, const Color &c, bool bold = false,
          Align align = Align::Left, bool vcenter = true) {
    FontFamily fam(L"Segoe UI");
    Font font(&fam, sizePx, bold ? FontStyleBold : FontStyleRegular, UnitPixel);
    StringFormat fmt;
    fmt.SetAlignment(align == Align::Left ? StringAlignmentNear
                     : align == Align::Center ? StringAlignmentCenter
                                              : StringAlignmentFar);
    fmt.SetLineAlignment(vcenter ? StringAlignmentCenter : StringAlignmentNear);
    fmt.SetTrimming(StringTrimmingEllipsisCharacter);
    fmt.SetFormatFlags(StringFormatFlagsNoWrap);
    SolidBrush b(c);
    gr.DrawString(s.c_str(), -1, &font, r, &fmt, &b);
}

float textWidth(Graphics &gr, const std::wstring &s, float sizePx, bool bold = false) {
    FontFamily fam(L"Segoe UI");
    Font font(&fam, sizePx, bold ? FontStyleBold : FontStyleRegular, UnitPixel);
    RectF out;
    gr.MeasureString(s.c_str(), -1, &font, PointF(0, 0), &out);
    return out.Width;
}

void addHit(const RectF &r, int id, bool enabled = true) { g.hits.push_back({r, id, enabled}); }

// Botao: primario (azul), perigo (vermelho), secundario (contorno).
enum class BtnStyle { Primary, Danger, Secondary };

void button(Graphics &gr, const RectF &r, const std::wstring &label, int id, bool enabled, BtnStyle style) {
    const bool hot = enabled && g.hover == id;
    const bool down = hot && g.pressed == id;
    Color fill, fg, border;
    if (!enabled) {
        fill = P.graySoft;
        fg = P.gray;
        border = P.graySoft;
    } else if (style == BtnStyle::Primary) {
        fill = hot ? P.accentHover : P.accent;
        fg = P.headerText;
        border = fill;
    } else if (style == BtnStyle::Danger) {
        fill = hot ? Color(255, 170, 25, 35) : P.red;
        fg = P.headerText;
        border = fill;
    } else {
        fill = hot ? P.graySoft : P.card;
        fg = P.text;
        border = P.border;
    }
    RectF rr = r;
    if (down) rr.Offset(0, S(1));
    fillRound(gr, rr, S(6), fill);
    if (style == BtnStyle::Secondary || !enabled) strokeRound(gr, rr, S(6), border);
    text(gr, label, rr, S(13), fg, true, Align::Center);
    addHit(r, id, enabled);
}

void chip(Graphics &gr, const RectF &r, const std::wstring &label, const Color &fg, const Color &bg) {
    fillRound(gr, r, r.Height / 2, bg);
    SolidBrush dot(fg);
    const float d = S(8);
    gr.FillEllipse(&dot, r.X + S(10), r.Y + (r.Height - d) / 2, d, d);
    text(gr, label, RectF(r.X + S(22), r.Y, r.Width - S(26), r.Height), S(12.5f), fg, true);
}

// ------------------------------------------------------------------ regras de UI

bool commandsBlocked(std::wstring *why) {
    const GameState &s = g.state;
    if (!s.connected) {
        if (why) *why = L"Jogo não conectado";
        return true;
    }
    if (s.requireStopped && s.speedKmh > s.maxSpeedKmh) {
        if (why) *why = L"Pare o caminhão";
        return true;
    }
    return false;
}

bool jointPending(int joint) {
    for (const auto &p : g.pending)
        if (p.joint == joint) return true;
    return false;
}

std::wstring unitLabel(int unitIndex) {
    for (const auto &u : g.state.units) {
        if (u.index != unitIndex) continue;
        if (u.kind == L"cavalo") return L"Cavalo";
        if (u.kind == L"dolly") return L"Dolly";
        int n = 0;
        for (const auto &v : g.state.units) {
            if (v.kind == L"carreta") ++n;
            if (v.index == unitIndex) break;
        }
        return L"Carreta " + std::to_wstring(n);
    }
    return L"?";
}

// "Lanca do dolly" (sem acento no plugin) -> rotulo com acento para a tela.
std::wstring niceKind(const std::wstring &k) {
    if (k == L"Lanca do dolly") return L"Lança do dolly";
    if (k == L"Engate de lanca") return L"Engate de lança";
    return k;
}

void sendCommand(const std::string &action, int joint) {
    const long long seq = g.source->send(action);
    if (seq == 0) {
        g.localMessage = L"Não foi possível gravar o comando na pasta Documentos\\ETS2TrailerControl.";
        g.localMessageOk = false;
        return;
    }
    if (joint >= 0) g.pending.push_back({seq, joint, GetTickCount64()});
    g.localMessage.clear();
}

void toggleJoint(int idx) {
    if (idx < 0 || idx >= static_cast<int>(g.state.joints.size())) return;
    const JointView &j = g.state.joints[static_cast<size_t>(idx)];
    g.selected = idx;
    sendCommand(std::string(j.coupled ? "desengatar " : "engatar ") + std::to_string(idx), idx);
    // Volta ao jogo: se ele pausou com a tela na frente, o comando roda ao despausar.
    if (g.prefs.returnToGame && g.gameHwnd && !g.source->isDemo()) SetTimer(g.hwnd, 2, 300, nullptr);
}

void selectJoint(int idx, bool tellPlugin) {
    if (idx < 0 || idx >= static_cast<int>(g.state.joints.size())) return;
    g.selected = idx;
    if (tellPlugin && g.state.connected) sendCommand("selecionar " + std::to_string(idx), -1);
}

void setTopmost(bool on) {
    g.topmost = on;
    SetWindowPos(g.hwnd, on ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
}

void setDemo(bool on) {
    if (on) g.source = std::make_unique<DemoSource>();
    else g.source = std::make_unique<PluginSource>();
    g.state = GameState{};
    g.pending.clear();
    g.localMessage.clear();
    g.selected = 0;
    g.source->poll(g.state);
}

// Devolve o foco ao jogo e tira a tela da frente (Esc, Ctrl + - ou depois de um comando).
void returnToGame() {
    if (g.gameHwnd && IsWindow(g.gameHwnd)) {
        SetForegroundWindow(g.gameHwnd);
        ShowWindow(g.hwnd, SW_MINIMIZE);
    } else {
        ShowWindow(g.hwnd, SW_MINIMIZE);
    }
}

// O plugin pediu para aparecer (atalho no jogo).
void showFromGame(HWND game) {
    if (game) g.gameHwnd = game;
    if (IsIconic(g.hwnd)) ShowWindow(g.hwnd, SW_RESTORE);
    else ShowWindow(g.hwnd, SW_SHOW);
    SetForegroundWindow(g.hwnd);
    InvalidateRect(g.hwnd, nullptr, FALSE);
}

void openSettings() {
    const std::wstring path = !g.state.configPath.empty() ? g.state.configPath : g.prefs.lastConfigPath;
    if (showSettingsDialog(g.hwnd, path, g.prefs)) {
        if (g.state.connected && !g.source->isDemo()) {
            sendCommand("recarregar", -1);
        } else {
            g.localMessage = L"Configurações salvas. Valem na próxima vez que o jogo abrir.";
            g.localMessageOk = true;
        }
    }
}

// Grava uma chave de [som] no .ini do plugin (ele recarrega sozinho; o comando so acelera).
bool writeSoundSetting(const char *key, const std::string &value) {
    const std::wstring path = !g.state.configPath.empty() ? g.state.configPath : g.prefs.lastConfigPath;
    std::string text;
    if (path.empty() || !tc::ipc::readAll(path, text)) {
        g.localMessage = L"Não achei o trailer_control.ini (abra o jogo uma vez com o plugin instalado).";
        g.localMessageOk = false;
        return false;
    }
    if (!tc::ipc::writeAtomic(path, tc::ini::apply(text, {{"som", key, value}}))) {
        g.localMessage = L"Não consegui gravar o trailer_control.ini (pasta protegida?).";
        g.localMessageOk = false;
        return false;
    }
    sendCommand("recarregar", -1);
    return true;
}

void toggleBlinkerSound() {
    const bool on = !g.state.blinkerSound;
    if (!writeSoundSetting("alarme_seta", on ? "1" : "0")) return;
    g.state.blinkerSound = on;
    g.localMessage = on ? L"Som da seta ligado." : L"Som da seta desligado.";
    g.localMessageOk = true;
}

// Toca o apito no volume escolhido, para ouvir o ajuste na hora (a tela roda fora do jogo).
void previewBeep(int volume) {
    static std::vector<std::uint8_t> wav; // precisa viver enquanto toca (SND_MEMORY assincrono)
    if (volume <= 0) {
        PlaySoundW(nullptr, nullptr, 0);
        return;
    }
    wav = tc::makeBlinkerBeepWav(volume);
    PlaySoundW(reinterpret_cast<LPCWSTR>(wav.data()), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
}

int volumeFromX(int x) {
    const float t = (static_cast<float>(x) - g.volumeTrack.X) / std::max(1.0f, g.volumeTrack.Width);
    return std::clamp(static_cast<int>(t * 100.0f + 0.5f), 0, 100);
}

void commitVolume() {
    const int v = g.volumeDraft;
    g.volumeDraft = -1;
    if (v < 0) return;
    if (!writeSoundSetting("volume", std::to_string(v))) return;
    g.state.blinkerVolume = v;
    previewBeep(v);
}

// ------------------------------------------------------------------ pintura

void paintHeader(Graphics &gr, float w) {
    SolidBrush hb(P.header);
    gr.FillRectangle(&hb, 0.f, 0.f, w, S(56));
    text(gr, L"Trailer Control", RectF(S(20), 0, S(260), S(56)), S(18), P.headerText, true);
    if (!g.source->isDemo())
        text(gr, L"Voltar ao jogo:  Esc  ou  " + prettyCombo(appHotkey()), RectF(S(196), 0, w - S(420), S(56)), S(12.5f), P.gray);

    const GameState &s = g.state;
    std::wstring label;
    Color fg, bg;
    if (g.source->isDemo()) {
        label = L"Demonstração";
        fg = P.accent;
        bg = P.selected;
    } else if (!s.fileFound || !s.connected) {
        label = L"Aguardando o jogo";
        fg = P.gray;
        bg = P.graySoft;
    } else if (s.paused) {
        label = L"Jogo pausado";
        fg = P.amber;
        bg = P.amberSoft;
    } else {
        label = L"Jogo conectado";
        fg = P.green;
        bg = P.greenSoft;
    }
    const float cw = S(176);
    chip(gr, RectF(w - cw - S(20), S(14), cw, S(28)), label, fg, bg);
}

void paintWaiting(Graphics &gr, const RectF &area) {
    fillRound(gr, area, S(10), P.card);
    strokeRound(gr, area, S(10), P.border);
    const float cx = area.X + S(28);
    float y = area.Y + S(28);
    text(gr, L"Aguardando o jogo…", RectF(cx, y, area.Width - S(56), S(30)), S(20), P.text, true);
    y += S(42);
    const wchar_t *lines[] = {
        L"1. Instale trailer_control.dll e trailer_control.ini em  …\\Euro Truck Simulator 2\\bin\\win_x64\\plugins",
        L"2. Abra o ETS2 e aceite o aviso de \"SDK features\".",
        L"3. Entre no jogo com o caminhão: este app conecta sozinho.",
    };
    for (const wchar_t *l : lines) {
        text(gr, l, RectF(cx, y, area.Width - S(56), S(24)), S(13.5f), P.muted);
        y += S(28);
    }
    if (g.state.fileFound) {
        y += S(4);
        text(gr, L"Existe um status.json antigo, mas o plugin não está atualizando (jogo fechado?).",
             RectF(cx, y, area.Width - S(56), S(24)), S(12.5f), P.amber);
        y += S(28);
    }
    y += S(10);
    button(gr, RectF(cx, y, S(200), S(38)), L"Ver demonstração", kHitDemo, true, BtnStyle::Primary);
    button(gr, RectF(cx + S(212), y, S(150), S(38)), L"Abrir pasta", kHitFolder, true, BtnStyle::Secondary);
}

void paintCombination(Graphics &gr, const RectF &card) {
    const GameState &s = g.state;
    fillRound(gr, card, S(10), P.card);
    strokeRound(gr, card, S(10), P.border);

    text(gr, s.combination.empty() ? L"Sem composição" : s.combination,
         RectF(card.X + S(20), card.Y + S(12), card.Width - S(260), S(28)), S(16), P.text, true);
    std::wstring ver = L"jogo " + (s.exeVersion.empty() ? L"?" : s.exeVersion) + L"  ·  plugin " +
                       (s.pluginVersion.empty() ? L"?" : s.pluginVersion);
    text(gr, ver, RectF(card.X + card.Width - S(260), card.Y + S(12), S(240), S(28)), S(12), P.muted, false,
         Align::Right);

    const size_t n = s.units.size();
    if (n == 0) return;
    const float top = card.Y + S(52);
    const float boxH = S(66);
    const float gap = S(46);
    const float avail = card.Width - S(40);
    float boxW = (avail - gap * static_cast<float>(n - 1)) / static_cast<float>(n);
    boxW = std::min(boxW, S(190));
    const float totalW = boxW * static_cast<float>(n) + gap * static_cast<float>(n - 1);
    float x = card.X + (card.Width - totalW) / 2;

    std::vector<RectF> boxes;
    for (size_t i = 0; i < n; ++i) {
        const UnitView &u = s.units[i];
        const RectF r(x, top, boxW, boxH);
        boxes.push_back(r);
        const Color strip = u.kind == L"cavalo" ? P.truck : u.kind == L"dolly" ? P.dolly : P.trailer;
        fillRound(gr, r, S(8), P.bg);
        strokeRound(gr, r, S(8), P.border);
        fillRound(gr, RectF(r.X, r.Y, r.Width, S(6)), S(3), strip);
        text(gr, unitLabel(u.index), RectF(r.X + S(10), r.Y + S(12), r.Width - S(20), S(22)), S(13.5f), P.text,
             true, Align::Center);
        text(gr, u.name, RectF(r.X + S(8), r.Y + S(34), r.Width - S(16), S(20)), S(11.5f), P.muted, false,
             Align::Center);
        // rodas
        SolidBrush wheel(P.text);
        const float wd = S(12);
        gr.FillEllipse(&wheel, r.X + r.Width * 0.22f - wd / 2, r.Y + boxH - wd / 2, wd, wd);
        gr.FillEllipse(&wheel, r.X + r.Width * 0.78f - wd / 2, r.Y + boxH - wd / 2, wd, wd);
        x += boxW + gap;
    }

    for (size_t i = 0; i + 1 < boxes.size() && i < s.joints.size(); ++i) {
        const JointView &j = s.joints[i];
        const float x1 = boxes[i].X + boxes[i].Width, x2 = boxes[i + 1].X;
        const float cy = top + boxH / 2;
        const float cx = (x1 + x2) / 2;
        const Color c = jointPending(j.index) ? P.amber : (j.coupled ? P.green : P.red);
        Pen line(c, S(3));
        if (j.coupled) {
            gr.DrawLine(&line, x1, cy, x2, cy);
        } else {
            gr.DrawLine(&line, x1, cy, cx - S(14), cy);
            gr.DrawLine(&line, cx + S(14), cy, x2, cy);
        }
        const float rad = S(13);
        if (static_cast<int>(i) == g.selected) {
            Pen ring(P.accent, S(2.5f));
            gr.DrawEllipse(&ring, cx - rad - S(5), cy - rad - S(5), (rad + S(5)) * 2, (rad + S(5)) * 2);
        }
        SolidBrush nb(c);
        gr.FillEllipse(&nb, cx - rad, cy - rad, rad * 2, rad * 2);
        text(gr, std::to_wstring(j.index), RectF(cx - rad, cy - rad, rad * 2, rad * 2), S(12), P.headerText, true,
             Align::Center);
        addHit(RectF(cx - rad - S(6), cy - rad - S(6), (rad + S(6)) * 2, (rad + S(6)) * 2), kHitNode + j.index);
    }
}

void paintJoints(Graphics &gr, const RectF &card) {
    const GameState &s = g.state;
    fillRound(gr, card, S(10), P.card);
    strokeRound(gr, card, S(10), P.border);
    text(gr, L"Articulações", RectF(card.X + S(20), card.Y + S(10), S(200), S(26)), S(14), P.text, true);

    if (s.joints.empty()) {
        text(gr, L"Nenhum reboque detectado. Pegue uma carga ou engate um reboque.",
             RectF(card.X + S(20), card.Y + S(44), card.Width - S(40), S(24)), S(13), P.muted);
        return;
    }

    std::wstring blockedWhy;
    const bool blocked = commandsBlocked(&blockedWhy);
    const float rowH = S(54);
    float y = card.Y + S(42);
    for (const JointView &j : s.joints) {
        if (y + rowH > card.Y + card.Height - S(6)) break;
        const RectF row(card.X + S(10), y, card.Width - S(20), rowH - S(4));
        const bool sel = j.index == g.selected;
        if (sel) fillRound(gr, row, S(8), P.selected);
        else if (g.hover == kHitRow + j.index) fillRound(gr, row, S(8), P.bg);
        addHit(row, kHitRow + j.index);

        const bool pend = jointPending(j.index);
        const Color c = pend ? P.amber : (j.coupled ? P.green : P.red);
        SolidBrush nb(c);
        const float rad = S(13);
        gr.FillEllipse(&nb, row.X + S(12), row.Y + row.Height / 2 - rad, rad * 2, rad * 2);
        text(gr, std::to_wstring(j.index), RectF(row.X + S(12), row.Y + row.Height / 2 - rad, rad * 2, rad * 2),
             S(12), P.headerText, true, Align::Center);

        const float tx = row.X + S(50);
        const float btnW = S(124), chipW = S(112);
        const float textW = row.Width - (tx - row.X) - btnW - chipW - S(36);
        text(gr, niceKind(j.kind), RectF(tx, row.Y + S(5), textW, S(22)), S(14), P.text, true);

        std::wstring sub = unitLabel(j.front) + L"  ↔  " + unitLabel(j.rear);
        const bool noBackend = j.backend == L"nenhum";
        Color subColor = P.muted;
        if (noBackend) {
            sub += j.index == 0 ? L"   ·   controle nativo indisponível"
                                : L"   ·   indisponível: " + (s.memoryStatus.empty() ? L"backend de memória desligado"
                                                                                       : s.memoryStatus);
            subColor = P.amber;
        } else if (j.index == 0 && !s.inputActive && !g.source->isDemo()) {
            sub += L"   ·   associe \"Trailer Control\" em Opções › Controles";
            subColor = P.amber;
        } else if (j.fromMemory) {
            sub += L"   ·   estado lido da memória";
        }
        text(gr, sub, RectF(tx, row.Y + S(26), textW, S(20)), S(12), subColor);

        const RectF chipR(row.X + row.Width - btnW - chipW - S(22), row.Y + (row.Height - S(26)) / 2, chipW, S(26));
        if (pend) chip(gr, chipR, L"Enviando…", P.amber, P.amberSoft);
        else if (j.coupled) chip(gr, chipR, L"Engatada", P.green, P.greenSoft);
        else chip(gr, chipR, L"Solta", P.red, P.redSoft);

        const bool enabled = !blocked && !noBackend && !pend;
        const RectF btn(row.X + row.Width - btnW - S(10), row.Y + (row.Height - S(34)) / 2, btnW, S(34));
        button(gr, btn, j.coupled ? L"Desengatar" : L"Engatar", kHitRowAction + j.index, enabled,
               j.coupled ? BtnStyle::Danger : BtnStyle::Primary);
        y += rowH;
    }

    if (!blocked && s.paused && !g.source->isDemo()) {
        text(gr, L"Jogo pausado: o comando roda quando você voltar ao jogo",
             RectF(card.X + card.Width - S(430), card.Y + S(10), S(410), S(26)), S(12.5f), P.amber, true, Align::Right);
    }
    if (blocked) {
        text(gr, blockedWhy, RectF(card.X + card.Width - S(300), card.Y + S(10), S(280), S(26)), S(12.5f), P.amber,
             true, Align::Right);
    }
}

void paintMessage(Graphics &gr, const RectF &r) {
    const GameState &s = g.state;
    std::wstring msg = g.localMessage;
    bool ok = g.localMessageOk;
    if (msg.empty()) {
        msg = s.lastMessage;
        ok = s.lastOk;
    }
    if (msg.empty()) return;
    fillRound(gr, r, S(8), ok ? P.greenSoft : P.redSoft);
    text(gr, msg, RectF(r.X + S(14), r.Y, r.Width - S(28), r.Height), S(13), ok ? P.green : P.red, true);
}

void paintFooter(Graphics &gr, const RectF &r) {
    const GameState &s = g.state;
    float x = r.X + r.Width;
    const float h = S(34), y = r.Y + (r.Height - h) / 2;
    x -= S(150);
    button(gr, RectF(x, y, S(150), h), g.source->isDemo() ? L"Sair da demo" : L"Demonstração", kHitDemo, true,
           BtnStyle::Secondary);
    x -= S(130);
    button(gr, RectF(x, y, S(120), h), L"Configurações", kHitSettings, true, BtnStyle::Secondary);
    x -= S(170);
    button(gr, RectF(x, y, S(160), h), g.topmost ? L"✓ Sempre no topo" : L"Sempre no topo", kHitTopmost, true,
           BtnStyle::Secondary);
    if (s.connected && !g.source->isDemo()) {
        x -= S(130);
        button(gr, RectF(x, y, S(120), h), L"Diagnóstico", kHitDiagnose, true, BtnStyle::Secondary);
    }
    // Texto no espaco que sobrou a esquerda dos botoes.
    if (s.connected || g.source->isDemo()) {
        wchar_t buf[128];
        if (s.requireStopped)
            swprintf_s(buf, L"%.1f km/h  ·  limite %d km/h", s.speedKmh,
                       static_cast<int>(s.maxSpeedKmh));
        else
            swprintf_s(buf, L"%.1f km/h", s.speedKmh);
        const bool fast = s.requireStopped && s.speedKmh > s.maxSpeedKmh;
        text(gr, buf, RectF(r.X, r.Y, x - r.X - S(12), r.Height), S(12.5f), fast ? P.amber : P.muted, fast);
    }
}

// Faixa do alarme da seta: liga/desliga + volume de 0 a 100 (clique ou arraste na barra).
void paintSoundStrip(Graphics &gr, const RectF &r) {
    fillRound(gr, r, S(10), P.card);
    strokeRound(gr, r, S(10), P.border);
    const bool on = g.state.blinkerSound;
    const float bh = S(32), by = r.Y + (r.Height - bh) / 2;
    button(gr, RectF(r.X + S(10), by, S(150), bh), on ? L"✓ Som da seta" : L"Som da seta", kHitBlinkerSound, true,
           BtnStyle::Secondary);

    const int vol = g.volumeDraft >= 0 ? g.volumeDraft : g.state.blinkerVolume;
    text(gr, L"Volume", RectF(r.X + S(176), r.Y, S(60), r.Height), S(12.5f), P.muted);
    const float tx = r.X + S(240), tw = std::max(S(60), r.Width - S(240) - S(70));
    const float cy = r.Y + r.Height / 2;
    g.volumeTrack = RectF(tx, cy - S(3), tw, S(6));
    fillRound(gr, g.volumeTrack, S(3), P.graySoft);
    const Color &fillColor = on ? P.accent : P.gray;
    if (vol > 0) fillRound(gr, RectF(tx, cy - S(3), tw * vol / 100.0f, S(6)), S(3), fillColor);
    const float kx = tx + tw * vol / 100.0f, kr = S(9);
    SolidBrush kb(Color(255, 255, 255, 255));
    Pen kp(fillColor, S(2));
    gr.FillEllipse(&kb, kx - kr, cy - kr, 2 * kr, 2 * kr);
    gr.DrawEllipse(&kp, kx - kr, cy - kr, 2 * kr, 2 * kr);
    addHit(RectF(tx - kr, r.Y + S(2), tw + 2 * kr, r.Height - S(4)), kHitVolume, true);
    text(gr, std::to_wstring(vol), RectF(tx + tw + S(16), r.Y, S(48), r.Height), S(13), on ? P.text : P.muted, true);
}

void paint(HDC hdc, const RECT &rc) {
    const float w = static_cast<float>(rc.right - rc.left), h = static_cast<float>(rc.bottom - rc.top);
    Graphics gr(hdc);
    gr.SetSmoothingMode(SmoothingModeAntiAlias);
    gr.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
    gr.Clear(P.bg);
    g.hits.clear();

    paintHeader(gr, w);
    const float m = S(20);
    const float footerH = S(52);
    const RectF footer(m, h - footerH, w - 2 * m, footerH);
    const bool live = g.source->isDemo() || (g.state.fileFound && g.state.connected);
    // A faixa do som (so com o plugin conectado) fica logo acima do rodape.
    const bool soundStrip = live && !g.source->isDemo();
    const float stripH = soundStrip ? S(52) : 0.f;
    const RectF content(m, S(56) + m, w - 2 * m, h - S(56) - m - footerH - S(6) - stripH);
    if (soundStrip) paintSoundStrip(gr, RectF(m, footer.Y - stripH, w - 2 * m, stripH - S(8)));
    if (!live) {
        paintWaiting(gr, content);
    } else {
        const RectF combo(content.X, content.Y, content.Width, S(150));
        paintCombination(gr, combo);
        const float msgH = S(40);
        const RectF joints(content.X, combo.Y + combo.Height + S(14), content.Width,
                           content.Height - combo.Height - S(14) - msgH - S(12));
        paintJoints(gr, joints);
        paintMessage(gr, RectF(content.X, joints.Y + joints.Height + S(12), content.Width, msgH));
    }
    paintFooter(gr, footer);
}

int hitTest(int x, int y) {
    // Ultimos desenhados ficam por cima (botao dentro da linha).
    for (auto it = g.hits.rbegin(); it != g.hits.rend(); ++it) {
        if (it->r.Contains(static_cast<REAL>(x), static_cast<REAL>(y))) return it->enabled ? it->id : kHitNone;
    }
    return kHitNone;
}

void onClick(int id) {
    if (id == kHitTopmost) setTopmost(!g.topmost);
    else if (id == kHitFolder) {
        const std::wstring dir = tc::ipc::folder();
        if (!dir.empty()) ShellExecuteW(g.hwnd, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    } else if (id == kHitDemo) setDemo(!g.source->isDemo());
    else if (id == kHitDiagnose) sendCommand("diagnostico", -1);
    else if (id == kHitSettings) openSettings();
    else if (id == kHitBlinkerSound) toggleBlinkerSound();
    else if (id >= kHitRowAction && id < kHitRow) toggleJoint(id - kHitRowAction);
    else if (id >= kHitRow && id < kHitNode) selectJoint(id - kHitRow, true);
    else if (id >= kHitNode) selectJoint(id - kHitNode, true);
    InvalidateRect(g.hwnd, nullptr, FALSE);
}

void onTimer() {
    bool changed = g.source->poll(g.state);
    if (!g.state.configPath.empty() && g.state.configPath != g.prefs.lastConfigPath) {
        g.prefs.lastConfigPath = g.state.configPath; // lembra para configurar com o jogo fechado
        g.prefs.save();
    }

    // Confirmacoes: o plugin devolve o seq do ultimo comando processado.
    const unsigned long long now = GetTickCount64();
    for (auto it = g.pending.begin(); it != g.pending.end();) {
        if (g.state.lastCommandSeq >= it->seq) {
            it = g.pending.erase(it);
            changed = true;
        } else if (now - it->sentAt > 4000) {
            g.localMessage = L"Sem resposta do plugin (o jogo está aberto e fora do menu?).";
            g.localMessageOk = false;
            it = g.pending.erase(it);
            changed = true;
        } else {
            ++it;
        }
    }
    if (g.state.lastCommandSeq != g.shownSeq) {
        g.shownSeq = g.state.lastCommandSeq;
        g.localMessage.clear();
    }
    if (!g.state.joints.empty() && g.selected >= static_cast<int>(g.state.joints.size()))
        g.selected = static_cast<int>(g.state.joints.size()) - 1;
    if (changed) InvalidateRect(g.hwnd, nullptr, FALSE);
}

void updateScale(HWND hwnd) { g.scale = static_cast<float>(GetDpiForWindow(hwnd)) / 96.0f; }

LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    // Com a janela de Configuracoes aberta a principal fica desabilitada: ignora entrada.
    const bool input = msg == WM_LBUTTONDOWN || msg == WM_LBUTTONUP || msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN;
    if (input && !IsWindowEnabled(hwnd)) return 0;
    switch (msg) {
    case WM_CREATE:
        g.hwnd = hwnd;
        updateScale(hwnd);
        SetTimer(hwnd, 1, 200, nullptr);
        return 0;
    case WM_TIMER:
        if (wp == 2) {
            KillTimer(hwnd, 2);
            returnToGame();
            return 0;
        }
        onTimer();
        return 0;
    case WM_DPICHANGED: {
        updateScale(hwnd);
        const RECT *r = reinterpret_cast<const RECT *>(lp);
        SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_GETMINMAXINFO: {
        auto *mmi = reinterpret_cast<MINMAXINFO *>(lp);
        mmi->ptMinTrackSize.x = static_cast<LONG>(S(720));
        mmi->ptMinTrackSize.y = static_cast<LONG>(S(520));
        return 0;
    }
    case WM_SIZE:
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        HDC mem = CreateCompatibleDC(hdc);
        HBITMAP bmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
        HGDIOBJ old = SelectObject(mem, bmp);
        paint(mem, rc);
        BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, old);
        DeleteObject(bmp);
        DeleteDC(mem);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_MOUSEMOVE: {
        if (!g.tracking) {
            TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&tme);
            g.tracking = true;
        }
        if (g.draggingVolume) {
            g.volumeDraft = volumeFromX(GET_X_LPARAM(lp));
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        const int h = hitTest(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        if (h != g.hover) {
            g.hover = h;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        SetCursor(LoadCursor(nullptr, h != kHitNone ? IDC_HAND : IDC_ARROW));
        return 0;
    }
    case WM_MOUSELEAVE:
        g.tracking = false;
        g.hover = kHitNone;
        g.pressed = kHitNone;
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) return TRUE; // cursor definido em WM_MOUSEMOVE
        break;
    case WM_LBUTTONDOWN:
        g.pressed = hitTest(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        SetCapture(hwnd);
        if (g.pressed == kHitVolume) { // clique ou arrasto na barra define o volume
            g.draggingVolume = true;
            g.volumeDraft = volumeFromX(GET_X_LPARAM(lp));
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_LBUTTONUP: {
        ReleaseCapture();
        if (g.draggingVolume) {
            g.draggingVolume = false;
            g.pressed = kHitNone;
            commitVolume();
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        const int h = hitTest(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        const int p = g.pressed;
        g.pressed = kHitNone;
        if (h != kHitNone && h == p) onClick(h);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_SYSKEYDOWN:
        if (isAppHotkey(wp)) {
            returnToGame();
            return 0;
        }
        break;
    case WM_KEYDOWN: {
        const int n = static_cast<int>(g.state.joints.size());
        if (wp == VK_UP || wp == VK_LEFT) {
            if (n) selectJoint((g.selected + n - 1) % n, true);
        } else if (wp == VK_DOWN || wp == VK_RIGHT) {
            if (n) selectJoint((g.selected + 1) % n, true);
        } else if (wp == VK_RETURN || wp == VK_SPACE) {
            std::wstring why;
            if (n && !commandsBlocked(&why) && !jointPending(g.selected) &&
                g.state.joints[static_cast<size_t>(g.selected)].backend != L"nenhum")
                toggleJoint(g.selected);
        } else if (wp == VK_ESCAPE || isAppHotkey(wp)) {
            returnToGame();
        } else if (wp == 'T') {
            setTopmost(!g.topmost);
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case tc::ipc::kMsgShowFromGame:
        showFromGame(reinterpret_cast<HWND>(wp));
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR cmdLine, int show) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    // So uma janela: se ja houver outra, traz para frente.
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\ETS2TrailerControlApp");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (HWND other = FindWindowW(tc::ipc::kAppWindowClass, nullptr)) {
            ShowWindow(other, SW_RESTORE);
            SetForegroundWindow(other);
        }
        return 0;
    }

    GdiplusStartupInput gdiIn;
    ULONG_PTR gdiToken = 0;
    GdiplusStartup(&gdiToken, &gdiIn, nullptr);

    const bool demo = cmdLine && wcsstr(cmdLine, L"--demo");
    // Aberto pelo plugin (atalho no jogo): "--from-game <hwnd do jogo>"
    if (const wchar_t *fg = cmdLine ? wcsstr(cmdLine, L"--from-game") : nullptr) {
        g.gameHwnd = reinterpret_cast<HWND>(static_cast<uintptr_t>(_wcstoui64(fg + 11, nullptr, 10)));
    }
    g.prefs.load();
    g.source = demo ? std::unique_ptr<Source>(std::make_unique<DemoSource>())
                    : std::unique_ptr<Source>(std::make_unique<PluginSource>());
    g.source->poll(g.state);

    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = wndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    wc.hIconSm = wc.hIcon;
    wc.lpszClassName = tc::ipc::kAppWindowClass;
    RegisterClassExW(&wc);

    const UINT dpi = GetDpiForSystem();
    const int w = MulDiv(860, static_cast<int>(dpi), 96), h = MulDiv(640, static_cast<int>(dpi), 96);
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"Trailer Control", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
                                CW_USEDEFAULT, w, h, nullptr, nullptr, inst, nullptr);
    if (!hwnd) return 1;

    // Barra de titulo escura, combinando com o cabecalho.
    BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof(dark));

    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0)) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    g.source.reset();
    GdiplusShutdown(gdiToken);
    if (mutex) CloseHandle(mutex);
    return static_cast<int>(m.wParam);
}
