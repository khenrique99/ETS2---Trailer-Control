#pragma once
#include "trailer_registry.h"

namespace tc {

enum class CouplingResult {
    Requested,       // comando enviado ao jogo
    NotSupported,    // esse backend nao consegue atuar nessa articulacao
    Unavailable,     // backend desativado / nao inicializado / dispositivo inativo
    Busy,            // ainda processando o comando anterior
};

const char *toString(CouplingResult r);

// Interface comum para quem efetivamente engata/desengata no jogo.
class ICouplingBackend {
public:
    virtual ~ICouplingBackend() = default;
    virtual const char *name() const = 0;
    virtual bool available() const = 0;
    virtual bool supports(const Joint &joint) const = 0;
    virtual CouplingResult detach(const Joint &joint) = 0;
    virtual CouplingResult attach(const Joint &joint) = 0;
};

} // namespace tc
