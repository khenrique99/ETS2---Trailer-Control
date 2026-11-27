#pragma once
// Desenho do painel (Dear ImGui). So aparencia e comandos; nada de DirectX ou janela aqui.
#include "overlay.h"

#include <imgui.h>

#include <atomic>
#include <string>

namespace tc::ui {

struct Fonts {
    ImFont *regular = nullptr; // 13.5 px
    ImFont *bold = nullptr;
    ImFont *title = nullptr;   // 18 px negrito
    ImFont *mid = nullptr;     // 16 px negrito
    ImFont *tiny = nullptr;    // 12 px
    bool unicode = false;      // Segoe UI carregada (setas, reticencias); senao so ASCII/Latin-1
};

// Estado do painel entre quadros.
struct Context {
    struct Pending {
        bool active = false;
        bool wantCoupled = false;
        unsigned long long since = 0;
    };

    int selected = 0;
    Pending pending[kMaxTrailers + 1];
    std::string localMessage;
    bool localOk = true;
    unsigned long long localMessageAt = 0;

    // Barra de volume enquanto arrasta (-1 = nenhum) e valores locais ate o plugin confirmar.
    int volumeDraft = -1;
    int volumeLocal = -1;
    int soundOnLocal = -1;
    unsigned long long soundLocalAt = 0;

    bool closeRequested = false;

    // Teclado (thread da janela -> thread de render): +1 baixo, -1 cima, ENTER ativa.
    std::atomic<int> keyNav{0};
    std::atomic<bool> keyActivate{false};
};

// Desenha o painel. scale = pixels por "unidade de layout" (1.0 em 1080p).
void draw(const OverlayState &state, Context &ctx, const Fonts &fonts, float scale);

// Cria as fontes (Segoe UI do Windows, se existir) no atlas do ImGui. scale = fator de tela.
Fonts loadFonts(float scale);

} // namespace tc::ui
