#pragma once
#include "trailer_registry.h"

#include <string>

namespace tc {

// Grava Documentos\ETS2TrailerControl\status.json com a composicao atual,
// a articulacao selecionada e o ultimo comando. Serve para depuracao e para
// um overlay/app externo ler (planejado para a v2).
void writeStatus(const CombinationSnapshot &snap, int selectedJoint, const std::string &lastMessage,
                 bool memoryBackendReady);

} // namespace tc
