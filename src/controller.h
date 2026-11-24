#pragma once
#include "config.h"
#include "coupling_backend.h"
#include "hotkeys.h"
#include "trailer_registry.h"

#include <atomic>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace tc {

// Liga tudo: recebe atalhos (thread de teclado), enfileira, e executa os
// comandos na thread do jogo (frame_start), escolhendo o backend capaz.
class Controller {
public:
    static Controller &instance();

    void configure(const Config &cfg);
    void startHotkeys();
    void stopHotkeys();

    TrailerRegistry &registry() { return registry_; }

    void setSpeed(float metersPerSecond) { speedMs_ = metersPerSecond; }
    void setPaused(bool p) { paused_ = p; }

    // Thread do jogo, uma vez por frame.
    void tick();

    // Pode ser chamado de qualquer thread.
    void enqueue(const std::string &action);

private:
    void execute(const std::string &action);
    void detachJoint(const Joint &j);
    void attachJoint(const Joint &j);
    bool checkSpeed();
    ICouplingBackend *pickBackend(const Joint &j);
    void report(const std::string &msg);

    TrailerRegistry registry_;
    HotkeyListener hotkeys_;
    std::vector<ICouplingBackend *> backends_;

    std::mutex queueMutex_;
    std::deque<std::string> queue_;

    std::atomic<float> speedMs_{0.f};
    std::atomic<bool> paused_{true};

    bool requireStopped_ = true;
    double maxSpeedKmh_ = 3.0;

    int selectedJoint_ = 0;
    unsigned lastRevision_ = ~0u;
    std::string lastMessage_;
    bool statusDirty_ = true;
};

} // namespace tc
