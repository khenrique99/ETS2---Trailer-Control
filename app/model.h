#pragma once
// Estado mostrado pelo app e as duas fontes possiveis: o plugin (status.json/command.txt)
// ou o modo demonstracao (simulado, sem jogo).
#include "json.h"

#include <string>
#include <vector>

namespace tc::app {

struct UnitView {
    int index = 0;
    std::wstring kind; // "cavalo", "carreta", "dolly"
    std::wstring name;
};

struct JointView {
    int index = 0, front = 0, rear = 1;
    std::wstring kind;
    bool coupled = false;
    bool fromMemory = false;
    std::wstring backend; // "entrada", "memoria", "nenhum"
};

struct GameState {
    bool fileFound = false;   // existe status.json
    bool connected = false;   // batimento recente
    bool paused = true;
    double speedKmh = 0, maxSpeedKmh = 3;
    bool requireStopped = true;
    bool inputReady = false, inputActive = false, memoryReady = false;
    std::wstring memoryStatus, exeVersion, pluginVersion, combination, lastMessage;
    std::wstring configPath, appHotkey; // .ini do plugin (para a tela de configuracoes)
    int queuedCommands = 0;             // comandos esperando o jogo despausar
    bool lastOk = true;
    long long lastCommandSeq = 0;
    bool lastCommandOk = true;
    std::wstring lastCommandResult;
    int selectedJoint = 0;
    std::vector<UnitView> units;
    std::vector<JointView> joints;
};

std::wstring widen(const std::string &utf8);

// Batimento do plugin ainda valido? (tolera relogio adiantado ate 2 s)
bool heartbeatFresh(unsigned long long ts, unsigned long long nowMs);

// Converte status.json em GameState. nowMs = relogio Unix em ms (para o batimento).
bool stateFromJson(const json::Value &v, unsigned long long nowMs, GameState &out);

class Source {
public:
    virtual ~Source() = default;
    // Atualiza o estado; true se algo mudou.
    virtual bool poll(GameState &state) = 0;
    // Envia um comando; devolve o seq usado.
    virtual long long send(const std::string &action) = 0;
    virtual bool isDemo() const = 0;
};

class PluginSource final : public Source {
public:
    bool poll(GameState &state) override;
    long long send(const std::string &action) override;
    bool isDemo() const override { return false; }

private:
    std::string lastText_;
    bool lastConnected_ = false;
    long long lastSeq_ = 0;
};

class DemoSource final : public Source {
public:
    DemoSource();
    bool poll(GameState &state) override;
    long long send(const std::string &action) override;
    bool isDemo() const override { return true; }

private:
    GameState sim_;
    long long seq_ = 0;
    std::vector<std::pair<unsigned long long, std::string>> pending_; // (quando, acao)
    std::vector<long long> pendingSeq_;
    bool changed_ = true;
};

} // namespace tc::app
