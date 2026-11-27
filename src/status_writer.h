#pragma once
#include "trailer_registry.h"

#include <string>

namespace tc {

constexpr const char *kPluginVersion = "0.3.2";

struct StatusInfo {
    const CombinationSnapshot *snap = nullptr;
    int selectedJoint = 0;
    std::string lastMessage;
    bool lastOk = true;

    long long lastCommandSeq = 0;   // ultimo comando do app processado (0 = nenhum)
    std::string lastCommandResult;
    bool lastCommandOk = true;

    bool paused = true;
    double speedKmh = 0.0;
    bool requireStopped = true;
    double maxSpeedKmh = 3.0;

    bool inputReady = false;        // dispositivo virtual registrado
    bool inputActive = false;       // jogador associou o controle
    bool memoryReady = false;
    std::string memoryStatus;
    std::string exeVersion;
    std::string configPath;          // trailer_control.ini (UTF-8) - o app edita pela tela
    std::string appHotkey;           // atalho que abre a tela
    size_t queuedCommands = 0;       // comandos esperando o jogo despausar
    bool blinkerSound = false;       // alarme sonoro da seta ligado
    int blinkerVolume = 60;          // 0..100
};

// Grava Documentos\ETS2TrailerControl\status.json (lido pelo app externo; ver shared/ipc.h).
void writeStatus(const StatusInfo &info);

} // namespace tc
