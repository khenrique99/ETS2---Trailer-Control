#pragma once
#include "config.h"
#include "coupling_backend.h"
#include "hotkeys.h"
#include "status_writer.h"
#include "trailer_registry.h"

#include <atomic>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace tc {

// Liga tudo: recebe atalhos (thread de teclado) e comandos do app externo (command.txt),
// enfileira e executa na thread do jogo (frame_start), escolhendo o backend capaz.
class Controller {
public:
    static Controller &instance();

    void configure(const Config &cfg);
    void startHotkeys();
    void stopHotkeys();

    TrailerRegistry &registry() { return registry_; }

    void setSpeed(float metersPerSecond) { speedMs_ = metersPerSecond; }
    void setPaused(bool p) { paused_ = p; }
    // Canal multiplayer.time.offset: diferente de 0 = Convoy.
    void setMultiplayerOffset(int minutes) { multiplayerOffset_ = minutes; }

    // Thread do jogo, uma vez por frame.
    void tick();

    // Pode ser chamado de qualquer thread. seq > 0 = comando do app externo.
    void enqueue(const std::string &action, long long seq = 0);

    // trailer_control.ini ao lado da DLL.
    static std::wstring configPath();

private:
    struct Pending {
        std::string action;
        long long seq = 0;
    };
    struct Deferred {
        Pending cmd;
        unsigned long long since;
    };

    void execute(const std::string &action);
    void detachJoint(const Joint &j);
    void attachJoint(const Joint &j);
    bool checkSpeed();
    ICouplingBackend *pickBackend(const Joint &j);
    void report(const std::string &msg, bool ok = true);
    CombinationSnapshot currentSnapshot() const;
    void writeStatusIfNeeded(const CombinationSnapshot &snap);
    StatusInfo buildStatusInfo(const CombinationSnapshot &snap) const;
    void checkMultiplayer(unsigned long long now);
    // Relê o .ini e aplica. false se o arquivo nao pode ser lido (tenta de novo depois).
    bool reloadConfig();
    // Detecta edicao do .ini (a mao ou pela tela) e recarrega sozinho.
    void watchConfig(unsigned long long now);
    static unsigned long long configStamp();

    TrailerRegistry registry_;
    HotkeyListener hotkeys_;
    std::vector<ICouplingBackend *> backends_;

    std::mutex queueMutex_;
    std::deque<Pending> queue_;
    std::deque<Deferred> deferred_; // comandos do app esperando o jogo despausar

    std::atomic<float> speedMs_{0.f};
    std::atomic<bool> paused_{true};
    std::atomic<int> multiplayerOffset_{0};
    unsigned long long lastMultiplayerCheckMs_ = 0;
    bool allowConvoy_ = true;
    unsigned long long configStamp_ = 0;       // ultima gravacao do .ini ja aplicada (0 = ainda nao vista)
    unsigned long long lastConfigCheckMs_ = 0;

    bool requireStopped_ = true;
    double maxSpeedKmh_ = 3.0;

    int selectedJoint_ = 0;
    std::string lastMessage_;
    bool lastOk_ = true;
    long long lastCommandSeq_ = 0;
    std::deque<long long> recentSeqs_; // seqs ja recebidos do app (descarta reenvios)
    std::string lastCommandResult_;
    bool lastCommandOk_ = true;

    std::string lastStatusKey_;
    unsigned long long lastStatusWriteMs_ = 0;
    unsigned long long lastInboxPollMs_ = 0;
    bool hotkeysStarted_ = false;
    std::string appHotkey_;
};

} // namespace tc
