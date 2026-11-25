#pragma once
#include <string>

namespace tc {

// Mostra a tela do Trailer Control (TrailerControl.exe, na mesma pasta da DLL).
// Se ja estiver aberta, traz para a frente; senao, abre. Passa a janela do jogo para o app
// conseguir devolver o foco ("voltar ao jogo"). Pode ser chamado de qualquer thread.
// Devolve uma mensagem para o log/status.
std::string showControlApp();

} // namespace tc
