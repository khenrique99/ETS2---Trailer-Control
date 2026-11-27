#include "overlay_ui.h"

#include "blinker_sound.h"
#include "controller.h"
#include "shared/ini_edit.h"
#include "shared/ipc.h"

#include <windows.h>
#include <mmsystem.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <vector>

namespace tc::ui {

namespace {

// Paleta do app externo (app/main.cpp), para o painel manter a mesma cara.
constexpr ImU32 rgb(int r, int g, int b, int a = 255) { return IM_COL32(r, g, b, a); }
const ImU32 kBg = rgb(243, 244, 246);
const ImU32 kHeader = rgb(24, 31, 42);
const ImU32 kWhite = rgb(255, 255, 255);
const ImU32 kBorder = rgb(222, 226, 231);
const ImU32 kText = rgb(31, 35, 40);
const ImU32 kMuted = rgb(101, 109, 118);
const ImU32 kAccent = rgb(37, 99, 235);
const ImU32 kAccentHover = rgb(29, 78, 216);
const ImU32 kGreen = rgb(26, 127, 55);
const ImU32 kGreenSoft = rgb(220, 245, 228);
const ImU32 kRed = rgb(207, 34, 46);
const ImU32 kRedDark = rgb(170, 25, 35);
const ImU32 kRedSoft = rgb(255, 230, 232);
const ImU32 kAmber = rgb(154, 103, 0);
const ImU32 kAmberSoft = rgb(255, 244, 214);
const ImU32 kGray = rgb(140, 149, 159);
const ImU32 kGraySoft = rgb(234, 238, 242);
const ImU32 kSelected = rgb(232, 240, 254);
const ImU32 kTruck = rgb(37, 99, 235);
const ImU32 kTrailer = rgb(71, 85, 105);
const ImU32 kDolly = rgb(180, 120, 0);

constexpr unsigned long long kPendingTimeoutMs = 4000;
constexpr unsigned long long kLocalMessageMs = 6000;

struct Painter {
    ImDrawList *dl;
    const Fonts &f;
    float s; // escala
    float S(float v) const { return v * s; }
};

struct Rect {
    float x, y, w, h;
    ImVec2 min() const { return {x, y}; }
    ImVec2 max() const { return {x + w, y + h}; }
};

enum class Align { Left, Center, Right };

float textW(ImFont *font, const std::string &t) {
    return font->CalcTextSizeA(font->FontSize, FLT_MAX, 0.0f, t.c_str()).x;
}

// Corta no limite da caixa com reticencias (a fonte pode nao ter "…": usa "...").
std::string fitText(ImFont *font, const std::string &t, float maxW, const char *ellipsis) {
    if (textW(font, t) <= maxW) return t;
    std::string out = t;
    while (!out.empty()) {
        // volta ate o inicio de um caractere UTF-8
        do {
            out.pop_back();
        } while (!out.empty() && (static_cast<unsigned char>(out.back()) & 0xC0) == 0x80);
        if (textW(font, out + ellipsis) <= maxW) return out + ellipsis;
    }
    return std::string();
}

void fillRound(const Painter &p, const Rect &r, float radius, ImU32 c, ImDrawFlags flags = 0) {
    p.dl->AddRectFilled(r.min(), r.max(), c, radius, flags);
}

void strokeRound(const Painter &p, const Rect &r, float radius, ImU32 c, float width = 1.0f) {
    p.dl->AddRect(r.min(), r.max(), c, radius, 0, width);
}

void text(const Painter &p, ImFont *font, const std::string &t, const Rect &r, ImU32 col, Align a = Align::Left,
          bool vcenter = true) {
    const std::string s = fitText(font, t, r.w, p.f.unicode ? "\xE2\x80\xA6" : "...");
    if (s.empty()) return;
    const ImVec2 sz = font->CalcTextSizeA(font->FontSize, FLT_MAX, 0.0f, s.c_str());
    float x = r.x;
    if (a == Align::Center) x = r.x + (r.w - sz.x) * 0.5f;
    else if (a == Align::Right) x = r.x + r.w - sz.x;
    const float y = vcenter ? r.y + (r.h - sz.y) * 0.5f : r.y;
    p.dl->AddText(font, font->FontSize, ImVec2(std::floor(x), std::floor(y)), col, s.c_str());
}

// Area clicavel invisivel. Devolve true no clique; hover/held pelo ImGui.
struct HitState {
    bool clicked = false, hovered = false, held = false;
};

HitState hit(int id, const Rect &r, bool enabled = true, bool allowOverlap = false) {
    HitState h;
    if (!enabled) return h;
    ImGui::PushID(id);
    ImGui::SetCursorScreenPos(r.min());
    if (allowOverlap) ImGui::SetNextItemAllowOverlap(); // botao por cima da linha
    h.clicked = ImGui::InvisibleButton("##hit", ImVec2(r.w, r.h));
    h.hovered = ImGui::IsItemHovered();
    h.held = ImGui::IsItemActive();
    ImGui::PopID();
    return h;
}

enum class BtnStyle { Primary, Danger, Secondary };

bool button(const Painter &p, int id, const Rect &r, const std::string &label, bool enabled, BtnStyle style) {
    const HitState h = hit(id, r, enabled);
    ImU32 fill, fg, border = 0;
    if (!enabled) {
        fill = kGraySoft;
        fg = kGray;
        border = kGraySoft;
    } else if (style == BtnStyle::Primary) {
        fill = h.hovered ? kAccentHover : kAccent;
        fg = kWhite;
    } else if (style == BtnStyle::Danger) {
        fill = h.hovered ? kRedDark : kRed;
        fg = kWhite;
    } else {
        fill = h.hovered ? kGraySoft : kWhite;
        fg = kText;
        border = kBorder;
    }
    Rect rr = r;
    if (h.held && h.hovered) rr.y += p.S(1);
    fillRound(p, rr, p.S(6), fill);
    if (style == BtnStyle::Secondary || !enabled) strokeRound(p, rr, p.S(6), border);
    text(p, p.f.bold, label, rr, fg, Align::Center);
    return h.clicked;
}

void chip(const Painter &p, const Rect &r, const std::string &label, ImU32 fg, ImU32 bg) {
    fillRound(p, r, r.h * 0.5f, bg);
    const float d = p.S(8);
    p.dl->AddCircleFilled(ImVec2(r.x + p.S(10) + d * 0.5f, r.y + r.h * 0.5f), d * 0.5f, fg, 16);
    text(p, p.f.bold, label, Rect{r.x + p.S(22), r.y, r.w - p.S(26), r.h}, fg);
}

// ------------------------------------------------------------------ regras de UI

bool commandsBlocked(const StatusInfo &s, std::string *why) {
    if (s.requireStopped && s.speedKmh > s.maxSpeedKmh) {
        if (why) *why = "Pare o caminhão";
        return true;
    }
    return false;
}

std::string unitLabel(const CombinationSnapshot &snap, int unitIndex) {
    for (const Unit &u : snap.units) {
        if (u.index != unitIndex) continue;
        if (u.kind == UnitKind::Truck) return "Cavalo";
        if (u.kind == UnitKind::Dolly) return "Dolly";
        int n = 0;
        for (const Unit &v : snap.units) {
            if (v.kind == UnitKind::Trailer) ++n;
            if (v.index == unitIndex) break;
        }
        return "Carreta " + std::to_string(n);
    }
    return "?";
}

std::string niceKind(JointKind k) {
    switch (k) {
    case JointKind::TruckFifthWheel:   return "Quinta roda do cavalo";
    case JointKind::DollyDrawbar:      return "Lança do dolly";
    case JointKind::DollyFifthWheel:   return "Quinta roda do dolly";
    case JointKind::TrailerFifthWheel: return "Quinta roda bitrem/rodotrem";
    case JointKind::TrailerHitch:      return "Engate de lança";
    }
    return "?";
}

void setLocalMessage(Context &ctx, const std::string &m, bool ok) {
    ctx.localMessage = m;
    ctx.localOk = ok;
    ctx.localMessageAt = GetTickCount64();
}

void sendJointCommand(Context &ctx, const Joint &j) {
    ctx.selected = j.index;
    Controller::instance().enqueue(std::string(j.coupled ? "desengatar " : "engatar ") + std::to_string(j.index));
    if (j.index >= 0 && j.index <= kMaxTrailers) {
        Context::Pending &p = ctx.pending[j.index];
        p.active = true;
        p.wantCoupled = !j.coupled;
        p.since = GetTickCount64();
    }
    ctx.localMessage.clear();
}

void selectJoint(Context &ctx, int idx) {
    ctx.selected = idx;
    Controller::instance().enqueue("selecionar " + std::to_string(idx));
}

// Grava uma chave de [som] no .ini (o plugin recarrega sozinho; o comando so acelera).
bool writeSoundSetting(Context &ctx, const char *key, const std::string &value) {
    const std::wstring path = Controller::configPath();
    std::string txt;
    if (!ipc::readAll(path, txt)) {
        setLocalMessage(ctx, "Não achei o trailer_control.ini ao lado da DLL.", false);
        return false;
    }
    if (!ipc::writeAtomic(path, ini::apply(txt, {{"som", key, value}}))) {
        setLocalMessage(ctx, "Não consegui gravar o trailer_control.ini (pasta protegida?).", false);
        return false;
    }
    Controller::instance().enqueue("recarregar");
    return true;
}

void previewBeep(int volume) {
    static std::vector<std::uint8_t> wav; // precisa viver enquanto toca (SND_MEMORY assincrono)
    if (volume <= 0) {
        PlaySoundW(nullptr, nullptr, 0);
        return;
    }
    wav = makeBlinkerBeepWav(volume);
    PlaySoundW(reinterpret_cast<LPCWSTR>(wav.data()), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
}

// "CTRL+MINUS" -> "Ctrl + -"
std::string prettyHotkey(const std::string &combo) {
    std::string out, tok;
    auto flush = [&] {
        if (tok.empty()) return;
        std::string t = tok;
        if (t == "MINUS") t = "-";
        else if (t == "PLUS") t = "+";
        else if (t.size() > 1) {
            for (size_t i = 1; i < t.size(); ++i) t[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(t[i])));
        }
        out += (out.empty() ? "" : " + ") + t;
        tok.clear();
    };
    for (char c : combo.empty() ? std::string("CTRL+MINUS") : combo) {
        if (c == '+') flush();
        else tok += c;
    }
    flush();
    return out;
}

// ------------------------------------------------------------------ secoes

void paintHeader(const Painter &p, const Rect &r, const OverlayState &st) {
    fillRound(p, Rect{r.x, r.y, r.w, p.S(56)}, p.S(10), kHeader, ImDrawFlags_RoundCornersTop);
    text(p, p.f.title, "Trailer Control", Rect{r.x + p.S(20), r.y, p.S(200), p.S(56)}, kWhite);
    text(p, p.f.tiny, "Voltar ao jogo:  Esc  ou  " + prettyHotkey(st.info.appHotkey),
         Rect{r.x + p.S(200), r.y, r.w - p.S(420), p.S(56)}, kGray);
    const bool paused = st.info.paused;
    const float cw = p.S(176);
    chip(p, Rect{r.x + r.w - cw - p.S(20), r.y + p.S(14), cw, p.S(28)}, paused ? "Jogo pausado" : "Jogo conectado",
         paused ? kAmber : kGreen, paused ? kAmberSoft : kGreenSoft);
}

bool jointPending(const Context &ctx, int idx) {
    return idx >= 0 && idx <= kMaxTrailers && ctx.pending[idx].active;
}

void paintCombination(const Painter &p, Context &ctx, const Rect &card, const OverlayState &st) {
    const CombinationSnapshot &snap = st.snap;
    fillRound(p, card, p.S(10), kWhite);
    strokeRound(p, card, p.S(10), kBorder);

    text(p, p.f.mid, snap.combinationName.empty() ? "Sem composição" : snap.combinationName,
         Rect{card.x + p.S(20), card.y + p.S(12), card.w - p.S(280), p.S(28)}, kText);
    const std::string ver = "jogo " + (st.info.exeVersion.empty() ? std::string("?") : st.info.exeVersion) +
                            "  ·  plugin " + kPluginVersion;
    text(p, p.f.tiny, ver, Rect{card.x + card.w - p.S(260), card.y + p.S(12), p.S(240), p.S(28)}, kMuted,
         Align::Right);

    const size_t n = snap.units.size();
    if (n == 0) return;
    const float top = card.y + p.S(52);
    const float boxH = p.S(66);
    const float gap = p.S(46);
    const float avail = card.w - p.S(40);
    float boxW = (avail - gap * static_cast<float>(n - 1)) / static_cast<float>(n);
    boxW = std::min(boxW, p.S(190));
    const float totalW = boxW * static_cast<float>(n) + gap * static_cast<float>(n - 1);
    float x = card.x + (card.w - totalW) / 2;

    std::vector<Rect> boxes;
    for (size_t i = 0; i < n; ++i) {
        const Unit &u = snap.units[i];
        const Rect r{x, top, boxW, boxH};
        boxes.push_back(r);
        const ImU32 strip = u.kind == UnitKind::Truck ? kTruck : u.kind == UnitKind::Dolly ? kDolly : kTrailer;
        fillRound(p, r, p.S(8), kBg);
        strokeRound(p, r, p.S(8), kBorder);
        fillRound(p, Rect{r.x, r.y, r.w, p.S(6)}, p.S(3), strip, ImDrawFlags_RoundCornersTop);
        text(p, p.f.bold, unitLabel(snap, u.index), Rect{r.x + p.S(10), r.y + p.S(12), r.w - p.S(20), p.S(22)}, kText,
             Align::Center);
        text(p, p.f.tiny, u.displayName(), Rect{r.x + p.S(8), r.y + p.S(34), r.w - p.S(16), p.S(20)}, kMuted,
             Align::Center);
        const float wd = p.S(6);
        p.dl->AddCircleFilled(ImVec2(r.x + r.w * 0.22f, r.y + boxH), wd, kText, 16);
        p.dl->AddCircleFilled(ImVec2(r.x + r.w * 0.78f, r.y + boxH), wd, kText, 16);
        x += boxW + gap;
    }

    for (size_t i = 0; i + 1 < boxes.size() && i < snap.joints.size(); ++i) {
        const Joint &j = snap.joints[i];
        const float x1 = boxes[i].x + boxes[i].w, x2 = boxes[i + 1].x;
        const float cy = top + boxH / 2;
        const float cx = (x1 + x2) / 2;
        const ImU32 c = jointPending(ctx, j.index) ? kAmber : (j.coupled ? kGreen : kRed);
        if (j.coupled) {
            p.dl->AddLine(ImVec2(x1, cy), ImVec2(x2, cy), c, p.S(3));
        } else {
            p.dl->AddLine(ImVec2(x1, cy), ImVec2(cx - p.S(14), cy), c, p.S(3));
            p.dl->AddLine(ImVec2(cx + p.S(14), cy), ImVec2(x2, cy), c, p.S(3));
        }
        const float rad = p.S(13);
        const Rect nodeRect{cx - rad - p.S(6), cy - rad - p.S(6), (rad + p.S(6)) * 2, (rad + p.S(6)) * 2};
        if (hit(3000 + j.index, nodeRect).clicked) selectJoint(ctx, j.index);
        if (j.index == ctx.selected) {
            p.dl->AddCircle(ImVec2(cx, cy), rad + p.S(5), kAccent, 32, p.S(2.5f));
        }
        p.dl->AddCircleFilled(ImVec2(cx, cy), rad, c, 24);
        text(p, p.f.bold, std::to_string(j.index), Rect{cx - rad, cy - rad, rad * 2, rad * 2}, kWhite, Align::Center);
    }
}

void paintJoints(const Painter &p, Context &ctx, const Rect &card, const OverlayState &st) {
    const CombinationSnapshot &snap = st.snap;
    const StatusInfo &info = st.info;
    fillRound(p, card, p.S(10), kWhite);
    strokeRound(p, card, p.S(10), kBorder);
    text(p, p.f.mid, "Articulações", Rect{card.x + p.S(20), card.y + p.S(10), p.S(200), p.S(26)}, kText);

    if (snap.joints.empty()) {
        text(p, p.f.regular, "Nenhum reboque detectado. Pegue uma carga ou engate um reboque.",
             Rect{card.x + p.S(20), card.y + p.S(44), card.w - p.S(40), p.S(24)}, kMuted);
        return;
    }

    std::string blockedWhy;
    const bool blocked = commandsBlocked(info, &blockedWhy);
    const float maxRowH = p.S(54);
    const float rowH = std::min(maxRowH, (card.h - p.S(54)) / static_cast<float>(snap.joints.size()));
    float y = card.y + p.S(42);
    const char *arrow = p.f.unicode ? "  \xE2\x86\x94  " : "  <->  ";
    for (const Joint &j : snap.joints) {
        if (y + rowH > card.y + card.h - p.S(6)) break;
        const Rect row{card.x + p.S(10), y, card.w - p.S(20), rowH - p.S(4)};
        const bool sel = j.index == ctx.selected;
        const HitState rowHit = hit(2000 + j.index, row, true, true);
        if (sel) fillRound(p, row, p.S(8), kSelected);
        else if (rowHit.hovered) fillRound(p, row, p.S(8), kBg);
        if (rowHit.clicked) selectJoint(ctx, j.index);

        const bool pend = jointPending(ctx, j.index);
        const ImU32 c = pend ? kAmber : (j.coupled ? kGreen : kRed);
        const float rad = p.S(13);
        p.dl->AddCircleFilled(ImVec2(row.x + p.S(12) + rad, row.y + row.h / 2), rad, c, 24);
        text(p, p.f.bold, std::to_string(j.index), Rect{row.x + p.S(12), row.y + row.h / 2 - rad, rad * 2, rad * 2},
             kWhite, Align::Center);

        const float tx = row.x + p.S(50);
        const float btnW = p.S(124), chipW = p.S(112);
        const float textWd = row.w - (tx - row.x) - btnW - chipW - p.S(36);
        const bool compact = rowH < p.S(52);
        const float titleY = compact ? p.S(3) : p.S(5);
        const float titleH = compact ? p.S(19) : p.S(22);
        const float detailY = compact ? p.S(22) : p.S(26);
        const float detailH = compact ? p.S(17) : p.S(20);
        text(p, p.f.bold, niceKind(j.kind), Rect{tx, row.y + titleY, textWd, titleH}, kText);

        std::string sub = unitLabel(snap, j.front) + arrow + unitLabel(snap, j.rear);
        const bool noBackend = j.index == 0 ? !info.inputReady : !info.memoryReady;
        const bool inputUnavailable = j.index == 0 && info.inputReady && !info.inputActive;
        ImU32 subColor = kMuted;
        if (noBackend) {
            sub += j.index == 0 ? "   ·   controle nativo indisponível"
                                : "   ·   indisponível: " + (info.memoryStatus.empty() ? std::string("backend de memória desligado")
                                                                                         : info.memoryStatus);
            subColor = kAmber;
        } else if (inputUnavailable) {
            sub += "   ·   associe \"Trailer Control\" em Opções › Controles";
            subColor = kAmber;
        } else if (j.fromMemory) {
            sub += "   ·   estado lido da memória";
        }
        text(p, p.f.tiny, sub, Rect{tx, row.y + detailY, textWd, detailH}, subColor);

        const float chipH = compact ? p.S(24) : p.S(26);
        const Rect chipR{row.x + row.w - btnW - chipW - p.S(22), row.y + (row.h - chipH) / 2, chipW, chipH};
        if (pend) chip(p, chipR, "Enviando…", kAmber, kAmberSoft);
        else if (j.coupled) chip(p, chipR, "Engatada", kGreen, kGreenSoft);
        else chip(p, chipR, "Solta", kRed, kRedSoft);

        const bool enabled = !blocked && !noBackend && !inputUnavailable && !pend;
        const float btnH = compact ? p.S(30) : p.S(34);
        const Rect btn{row.x + row.w - btnW - p.S(10), row.y + (row.h - btnH) / 2, btnW, btnH};
        if (button(p, 1000 + j.index, btn, j.coupled ? "Desengatar" : "Engatar", enabled,
                   j.coupled ? BtnStyle::Danger : BtnStyle::Primary)) {
            sendJointCommand(ctx, j);
        }
        y += rowH;
    }

    if (blocked) {
        text(p, p.f.tiny, blockedWhy, Rect{card.x + card.w - p.S(300), card.y + p.S(10), p.S(280), p.S(26)}, kAmber,
             Align::Right);
    }
}

void paintMessage(const Painter &p, const Context &ctx, const Rect &r, const OverlayState &st) {
    std::string msg = ctx.localMessage;
    bool ok = ctx.localOk;
    if (msg.empty()) {
        msg = st.info.lastMessage;
        ok = st.info.lastOk;
    }
    if (msg.empty()) return;
    fillRound(p, r, p.S(8), ok ? kGreenSoft : kRedSoft);
    text(p, p.f.bold, msg, Rect{r.x + p.S(14), r.y, r.w - p.S(28), r.h}, ok ? kGreen : kRed);
}

// Faixa do alarme da seta: liga/desliga + volume de 0 a 100 (clique ou arraste na barra).
void paintSoundStrip(const Painter &p, Context &ctx, const Rect &r, const OverlayState &st) {
    fillRound(p, r, p.S(10), kWhite);
    strokeRound(p, r, p.S(10), kBorder);
    const bool on = ctx.soundOnLocal >= 0 ? ctx.soundOnLocal != 0 : st.info.blinkerSound;
    const float bh = p.S(32), by = r.y + (r.h - bh) / 2;
    if (button(p, 6, Rect{r.x + p.S(10), by, p.S(150), bh}, on ? "Som da seta: ligado" : "Som da seta: desligado",
               true, BtnStyle::Secondary)) {
        if (writeSoundSetting(ctx, "alarme_seta", on ? "0" : "1")) {
            ctx.soundOnLocal = on ? 0 : 1;
            ctx.soundLocalAt = GetTickCount64();
            setLocalMessage(ctx, on ? "Som da seta desligado." : "Som da seta ligado.", true);
        }
    }

    const int committed = ctx.volumeLocal >= 0 ? ctx.volumeLocal : st.info.blinkerVolume;
    const int vol = ctx.volumeDraft >= 0 ? ctx.volumeDraft : committed;
    text(p, p.f.regular, "Volume", Rect{r.x + p.S(176), r.y, p.S(60), r.h}, kMuted);
    const float tx = r.x + p.S(240), tw = std::max(p.S(60), r.w - p.S(240) - p.S(70));
    const float cy = r.y + r.h / 2;
    const Rect track{tx, cy - p.S(3), tw, p.S(6)};
    fillRound(p, track, p.S(3), kGraySoft);
    const ImU32 fillColor = on ? kAccent : kGray;
    if (vol > 0) fillRound(p, Rect{tx, cy - p.S(3), tw * static_cast<float>(vol) / 100.0f, p.S(6)}, p.S(3), fillColor);
    const float kx = tx + tw * static_cast<float>(vol) / 100.0f, kr = p.S(9);
    p.dl->AddCircleFilled(ImVec2(kx, cy), kr, kWhite, 24);
    p.dl->AddCircle(ImVec2(kx, cy), kr, fillColor, 24, p.S(2));

    const float k = p.S(2);
    const HitState h = hit(7, Rect{tx - kr, r.y + k, tw + 2 * kr, r.h - 2 * k});
    if (h.held) {
        const float t = (ImGui::GetIO().MousePos.x - tx) / std::max(1.0f, tw);
        ctx.volumeDraft = std::clamp(static_cast<int>(t * 100.0f + 0.5f), 0, 100);
    } else if (ctx.volumeDraft >= 0) {
        const int v = ctx.volumeDraft;
        ctx.volumeDraft = -1;
        if (writeSoundSetting(ctx, "volume", std::to_string(v))) {
            ctx.volumeLocal = v;
            ctx.soundLocalAt = GetTickCount64();
            previewBeep(v);
        }
    }
    text(p, p.f.bold, std::to_string(vol), Rect{tx + tw + p.S(16), r.y, p.S(48), r.h}, on ? kText : kMuted);
}

void paintFooter(const Painter &p, Context &ctx, const Rect &r, const OverlayState &st) {
    const StatusInfo &info = st.info;
    float x = r.x + r.w;
    const float h = p.S(34), y = r.y + (r.h - h) / 2;
    x -= p.S(130);
    if (button(p, 5, Rect{x, y, p.S(130), h}, "Fechar", true, BtnStyle::Primary)) ctx.closeRequested = true;
    x -= p.S(140);
    if (button(p, 4, Rect{x, y, p.S(130), h}, "Diagnóstico", true, BtnStyle::Secondary)) {
        Controller::instance().enqueue("diagnostico");
        setLocalMessage(ctx, "Diagnóstico escrito no game.log.txt.", true);
    }
    char buf[128];
    if (info.requireStopped)
        std::snprintf(buf, sizeof(buf), "%.1f km/h  ·  limite %d km/h", info.speedKmh, static_cast<int>(info.maxSpeedKmh));
    else
        std::snprintf(buf, sizeof(buf), "%.1f km/h", info.speedKmh);
    const bool fast = info.requireStopped && info.speedKmh > info.maxSpeedKmh;
    text(p, fast ? p.f.bold : p.f.regular, buf, Rect{r.x, r.y, x - r.x - p.S(12), r.h}, fast ? kAmber : kMuted);
}

// Teclado: setas escolhem a articulacao, ENTER engata/desengata (alternativa ao mouse).
void handleKeys(Context &ctx, const OverlayState &st) {
    const int nav = ctx.keyNav.exchange(0);
    const int count = static_cast<int>(st.snap.joints.size());
    if (nav != 0 && count > 0) {
        selectJoint(ctx, std::clamp(ctx.selected + nav, 0, count - 1));
    }
    if (ctx.keyActivate.exchange(false) && count > 0 && ctx.selected < count) {
        const Joint &j = st.snap.joints[static_cast<size_t>(ctx.selected)];
        const StatusInfo &info = st.info;
        const bool noBackend = j.index == 0 ? !info.inputReady : !info.memoryReady;
        const bool inputUnavailable = j.index == 0 && info.inputReady && !info.inputActive;
        if (!commandsBlocked(info, nullptr) && !noBackend && !inputUnavailable && !jointPending(ctx, j.index))
            sendJointCommand(ctx, j);
    }
}

// Confirmacoes: some o "Enviando…" quando a articulacao muda (ou apos o limite de tempo).
void updatePending(Context &ctx, const OverlayState &st) {
    const unsigned long long now = GetTickCount64();
    for (int i = 0; i <= kMaxTrailers; ++i) {
        Context::Pending &pd = ctx.pending[i];
        if (!pd.active) continue;
        const bool known = i < static_cast<int>(st.snap.joints.size());
        if (known && st.snap.joints[static_cast<size_t>(i)].coupled == pd.wantCoupled) {
            pd.active = false;
        } else if (now - pd.since > kPendingTimeoutMs) {
            pd.active = false;
            const std::string &last = st.info.lastMessage;
            if (last.empty()) setLocalMessage(ctx, "Sem resposta do plugin (o jogo está fora do menu?).", false);
        }
    }
    if (!ctx.localMessage.empty() && now - ctx.localMessageAt > kLocalMessageMs) ctx.localMessage.clear();
    if (ctx.soundOnLocal >= 0 || ctx.volumeLocal >= 0) {
        if (now - ctx.soundLocalAt > 2500) {
            ctx.soundOnLocal = -1;
            ctx.volumeLocal = -1;
        }
    }
    const int count = static_cast<int>(st.snap.joints.size());
    if (count > 0) {
        if (ctx.selected >= count) ctx.selected = count - 1;
        // Segue a selecao feita por outros meios (atalhos antigos, app externo).
        if (st.info.selectedJoint >= 0 && st.info.selectedJoint < count && ctx.keyNav.load() == 0 &&
            ctx.selected != st.info.selectedJoint && now - ctx.soundLocalAt > 600)
            ctx.selected = st.info.selectedJoint;
    }
}

} // namespace

Fonts loadFonts(float scale) {
    Fonts f;
    ImGuiIO &io = ImGui::GetIO();
    io.Fonts->Clear();
    static const ImWchar ranges[] = {0x0020, 0x00FF, 0x2010, 0x2027, 0x2190, 0x2195, 0};
    const char *regularPath = "C:\\Windows\\Fonts\\segoeui.ttf";
    const char *boldPath = "C:\\Windows\\Fonts\\segoeuib.ttf";
    const bool have = GetFileAttributesA(regularPath) != INVALID_FILE_ATTRIBUTES &&
                      GetFileAttributesA(boldPath) != INVALID_FILE_ATTRIBUTES;
    auto add = [&](const char *path, float px) -> ImFont * {
        ImFontConfig cfg;
        cfg.OversampleH = 2;
        cfg.OversampleV = 1;
        return io.Fonts->AddFontFromFileTTF(path, px * scale, &cfg, ranges);
    };
    if (have) {
        f.regular = add(regularPath, 13.5f);
        f.bold = add(boldPath, 13.5f);
        f.title = add(boldPath, 18.0f);
        f.mid = add(boldPath, 16.0f);
        f.tiny = add(regularPath, 12.0f);
        f.unicode = f.regular && f.bold && f.title && f.mid && f.tiny;
    }
    if (!f.unicode) {
        // Sem Segoe UI (ou falhou): fonte embutida do ImGui, sem setas nem reticencias.
        io.Fonts->Clear();
        ImFontConfig cfg;
        cfg.SizePixels = 13.0f * scale;
        ImFont *d = io.Fonts->AddFontDefault(&cfg);
        f.regular = f.bold = f.title = f.mid = f.tiny = d;
        f.unicode = false;
    }
    io.FontDefault = f.regular;
    return f;
}

void draw(const OverlayState &st, Context &ctx, const Fonts &fonts, float scale) {
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    ImDrawList *bgl = ImGui::GetBackgroundDrawList();
    bgl->AddRectFilled(ImVec2(0, 0), display, IM_COL32(0, 0, 0, 120)); // escurece o jogo atras

    // Janela do tamanho do app externo (900 x 620 em 1080p), limitada pela tela.
    const float w = std::min(900.0f * scale, display.x - 24.0f);
    const float h = std::min(620.0f * scale, display.y - 24.0f);
    ImGui::SetNextWindowPos(ImVec2(std::floor((display.x - w) * 0.5f), std::floor((display.y - h) * 0.5f)));
    ImGui::SetNextWindowSize(ImVec2(w, h));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                                   ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoNav;
    ImGui::SetNextWindowFocus();
    ImGui::Begin("##trailer_control", nullptr, flags);
    ImGui::PushFont(fonts.regular);

    Painter p{ImGui::GetWindowDrawList(), fonts, scale};
    const ImVec2 o = ImGui::GetWindowPos();
    const Rect win{o.x, o.y, w, h};

    updatePending(ctx, st);
    handleKeys(ctx, st);

    fillRound(p, win, p.S(10), kBg);
    strokeRound(p, win, p.S(10), rgb(0, 0, 0, 90), 1.0f);
    paintHeader(p, win, st);

    const float m = p.S(20);
    const float footerH = p.S(52);
    const float stripH = p.S(52);
    const Rect footer{win.x + m, win.y + h - footerH, w - 2 * m, footerH};
    const Rect strip{win.x + m, footer.y - stripH, w - 2 * m, stripH - p.S(8)};
    const Rect content{win.x + m, win.y + p.S(56) + m, w - 2 * m, h - p.S(56) - m - footerH - p.S(6) - stripH};

    const Rect combo{content.x, content.y, content.w, p.S(150)};
    paintCombination(p, ctx, combo, st);
    const float msgH = p.S(40);
    const Rect joints{content.x, combo.y + combo.h + p.S(14), content.w,
                      content.h - combo.h - p.S(14) - msgH - p.S(12)};
    paintJoints(p, ctx, joints, st);
    paintMessage(p, ctx, Rect{content.x, joints.y + joints.h + p.S(12), content.w, msgH}, st);
    paintSoundStrip(p, ctx, strip, st);
    paintFooter(p, ctx, footer, st);

    ImGui::PopFont();
    ImGui::End();
    ImGui::PopStyleVar(3);
}

} // namespace tc::ui
