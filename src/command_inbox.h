#pragma once
#include <string>
#include <vector>

namespace tc {

// Comando vindo do app externo (Documentos\ETS2TrailerControl\command.txt). Ver shared/ipc.h.
struct ExternalCommand {
    long long seq = 0;
    std::string action; // ex.: "desengatar 2"
};

// Le e consome command.txt. Barato quando nao ha arquivo (uma consulta de atributos).
std::vector<ExternalCommand> takeExternalCommands();

// Interpreta uma linha "<seq> <acao> [arg]". Exposto para teste.
bool parseCommandLine(const std::string &line, ExternalCommand &out);

} // namespace tc
