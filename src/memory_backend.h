#pragma once
#include "coupling_backend.h"
#include "trailer_registry.h"

#include <cstdint>
#include <string>

namespace tc {

class TrailerRegistry;

// Backend para as articulacoes que o jogo NAO expoe pelo SDK
// (dolly, quinta roda do bitrem/rodotrem, engates internos).
//
// Atua direto na memoria do eurotrucks2.exe usando enderecos levantados por engenharia
// reversa (ver docs/re/structures-1.62.3.3.md). Cada versao do jogo tem uma tabela propria; se o exe
// nao for exatamente uma versao conhecida, ou se os bytes/vtables nao baterem, o backend
// fica desligado e nada e tocado.
//
// Articulacao k (k >= 1) liga o reboque k-1 ao reboque k da cadeia fisica:
//   caminhao(+0x1580) -> reboque 0 (+0x1058) -> reboque 1 (+0x1058) -> ...
//   desengatar k: reboque[k]->vt[0x318]()        (destroi a junta fisica; o jogo continua dono)
//   engatar k:    reconectar_reboque_de_tras(reboque[k-1])   (mesma rotina usada na criacao)
class MemoryBackend final : public ICouplingBackend {
public:
    static MemoryBackend &instance();

    // expectedVersion: versao do exe exigida pelo .ini ("1.62.3.3"); vazio = qualquer conhecida.
    // maxAttachDistanceM: distancia maxima entre os engates para permitir engatar (0 = sem checar).
    bool initialize(bool enabled, const std::string &expectedVersion, double maxAttachDistanceM);
    void setRegistry(const TrailerRegistry *registry) { registry_ = registry; }

    // Desfaz o desvio do comprimento da cadeia (precisa rodar antes de a DLL ser descarregada).
    void shutdown();

    const char *name() const override { return "Memoria (experimental)"; }
    bool available() const override { return ready_ && multiplayer_.empty(); }
    bool supports(const Joint &joint) const override { return available() && joint.index >= 1; }

    // Multiplayer (TruckersMP / Convoy): risco de banimento e de dessincronizar -> desliga.
    // why vazio = single-player.
    void setMultiplayer(const std::string &why);
    CouplingResult detach(const Joint &joint) override;
    CouplingResult attach(const Joint &joint) override;

    // Estado real da articulacao k >= 1 lido da memoria. false = nao deu para ler.
    bool probeCoupled(int jointIndex, bool &coupled) const;

    // So leitura: escreve no game.log.txt a cadeia de ponteiros (jogo -> ator -> caminhao ->
    // reboques), se as vtables conferem e o mestre de cada reboque. Para depurar no jogo real.
    // Devolve um resumo de uma linha.
    std::string diagnose() const;

    // Versao do exe (ex.: "1.62.3.3") e motivo de estar desligado (para o status/app).
    const std::string &exeVersion() const { return exeVersion_; }
    std::string statusText() const;
    const std::string &lastError() const { return lastError_; }

    // Busca um padrao estilo IDA ("48 8B ?? ?? 89") no modulo principal do jogo.
    static std::uintptr_t findPattern(const std::string &pattern);

    struct Layout; // tabela de enderecos por versao do jogo (memory_backend.cpp)

private:

    std::uintptr_t trailerObject(int chainIndex) const; // 0 = 1o reboque; 0 se invalido
    bool hookDistance(int jointIndex, std::uintptr_t master, double &meters) const;
    bool hasHoses(std::uintptr_t vehicle) const;
    bool isCoupled(std::uintptr_t trailer) const;
    int chainFrom(std::uintptr_t first, std::uintptr_t *out, int max) const;
    void rebuildHoses(std::uintptr_t master, std::uintptr_t slave, bool masterHadHoses, bool pairConnected);

    // Desvio de FUN_140775120 (comprimento da cadeia): a camera e outras rotinas somam o comprimento
    // de todos os reboques ligados por +0x1058, inclusive os que soltamos. O desvio para no 1o solto.
    void syncChainHook();
    bool installChainHook();
    void removeChainHook();
    bool chainHookInstalled_ = false;

    // Articulacao k tinha mangueiras antes de soltar (para recriar ao engatar).
    bool hosesWereOn_[kMaxTrailers + 1] = {};

    const Layout *layout_ = nullptr;
    const TrailerRegistry *registry_ = nullptr;
    std::uintptr_t base_ = 0;
    bool ready_ = false;
    double maxAttachDistanceM_ = 1.5;
    std::string exeVersion_;
    std::string statusText_ = "desativado";
    std::string multiplayer_;
    mutable std::string lastError_;
};

} // namespace tc
