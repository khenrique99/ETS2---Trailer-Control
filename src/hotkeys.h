#pragma once
#include <atomic>
#include <functional>
#include <string>
#include <thread>
#include <vector>

namespace tc {

// Le o teclado numa thread propria (so dispara com a janela do jogo em foco).
// O callback e chamado nessa thread: quem recebe deve apenas enfileirar.
class HotkeyListener {
public:
    using Callback = std::function<void(const std::string &action)>;

    // Formato: "CTRL+SHIFT+D", "ALT+F5", "NUMPAD1"... Retorna false se invalido.
    bool add(const std::string &action, const std::string &combo);
    void start(Callback cb);
    void stop();
    ~HotkeyListener() { stop(); }

private:
    struct Binding {
        std::string action;
        bool ctrl = false, shift = false, alt = false;
        int vk = 0;
        bool wasDown = false;
    };
    void run();

    std::vector<Binding> bindings_;
    Callback cb_;
    std::atomic<bool> running_{false};
    std::thread thread_;
};

} // namespace tc
