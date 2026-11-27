#pragma once
// Painel do Trailer Control desenhado DENTRO do jogo (DirectX 11 + Dear ImGui).
//
// Como funciona:
//  - Uma thread curta cria uma swap chain descartavel so para descobrir a vtable do IDXGISwapChain
//    e troca os ponteiros de Present / Present1 / ResizeBuffers por funcoes nossas (o jogo usa a
//    mesma vtable do dxgi.dll). Nenhum codigo do exe do jogo e alterado.
//  - Com o painel aberto, cada Present desenha o ImGui por cima do quadro e a janela do jogo e
//    "subclassada" para o mouse/teclado irem so para o painel (o caminhao nao recebe nada).
//  - Comandos viram Controller::enqueue(); o estado chega por publish() a cada frame_start.
//
// Threads: publish() roda na thread do jogo; os hooks e o ImGui na thread de render; toggle() em
// qualquer uma. Tudo que e compartilhado fica atras de mutex ou atomic.

#include "status_writer.h"
#include "trailer_registry.h"

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

namespace tc {

// Copia por valor do que o painel mostra (o StatusInfo aponta para o snapshot, que nao pode ser
// lido de outra thread).
struct OverlayState {
    CombinationSnapshot snap;
    StatusInfo info; // info.snap nao e usado
    unsigned long long stamp = 0;
};

class Overlay {
public:
    static Overlay &instance();

    // Thread do jogo. enabled = [tela] modo = jogo. Instala os hooks na primeira vez.
    void configure(bool enabled);
    // Desfaz hooks e subclasse; antes de a DLL ser descarregada.
    void shutdown();

    // Hooks instalados e painel habilitado (senao o atalho abre o app externo).
    bool active() const { return hooked_.load() && enabled_.load() && !blockedByMultiplayer(); }
    bool isOpen() const { return open_.load(); }
    void toggle();
    void close();

    // Thread do jogo: estado mais recente para o painel.
    void publish(OverlayState state);

private:
    Overlay() = default;
    static bool blockedByMultiplayer();

    std::atomic<bool> enabled_{false};
    std::atomic<bool> hooked_{false};
    std::atomic<bool> open_{false};
    std::atomic<bool> started_{false};
    std::thread installThread_;
};

} // namespace tc
