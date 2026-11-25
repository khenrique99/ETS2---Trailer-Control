#pragma once
// Conversao entre teclas do Windows e o formato do .ini ("CTRL+MINUS"), e o texto bonito
// mostrado na tela ("Ctrl + -"). Os nomes sao os mesmos que o plugin entende (hotkeys.cpp).
#include <string>

namespace tc::app {

// Nome da tecla no formato do .ini ("A", "F9", "MINUS", "NUMPAD3"...); vazio = nao suportada
// ou e so um modificador.
std::string vkToIniName(unsigned vk);

// Monta "CTRL+SHIFT+F9". Vazio se a tecla nao for suportada.
std::string makeCombo(bool ctrl, bool shift, bool alt, unsigned vk);

// "CTRL+MINUS" -> "Ctrl + -"
std::wstring prettyCombo(const std::string &combo);

} // namespace tc::app
