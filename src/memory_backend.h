#pragma once
#include "coupling_backend.h"

#include <cstdint>
#include <string>

namespace tc {

// Backend para as articulacoes que o jogo NAO expoe pelo SDK
// (dolly, quinta roda do bitrem/rodotrem, engates internos).
//
// Isso so e possivel atuando direto na memoria do executavel do jogo, com
// assinaturas (padroes de bytes) encontradas por engenharia reversa para cada
// versao do ETS2. Esta v1 traz a infraestrutura (scanner de padroes, trava de
// versao, configuracao no .ini), mas as assinaturas ainda precisam ser
// levantadas -> enquanto isso, available() == false e nada e tocado na memoria.
class MemoryBackend final : public ICouplingBackend {
public:
    static MemoryBackend &instance();

    // gameVersion: "major.minor" vindo do SDK. Retorna true se tudo resolveu.
    bool initialize(bool enabled, const std::string &gameVersion,
                    const std::string &expectedVersion, const std::string &detachSignature,
                    const std::string &attachSignature);

    const char *name() const override { return "Memoria (experimental)"; }
    bool available() const override { return ready_; }
    bool supports(const Joint &joint) const override { return ready_ && joint.index >= 0; }
    CouplingResult detach(const Joint &joint) override;
    CouplingResult attach(const Joint &joint) override;

    // Busca um padrao estilo IDA ("48 8B ?? ?? 89") no modulo principal do jogo.
    static std::uintptr_t findPattern(const std::string &pattern);

private:
    bool ready_ = false;
    std::uintptr_t detachFn_ = 0;
    std::uintptr_t attachFn_ = 0;
};

} // namespace tc
