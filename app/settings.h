#pragma once
// Preferencias do app e a janela de Configuracoes (edita o trailer_control.ini do plugin).
#include <windows.h>

#include <string>

namespace tc::app {

// Preferencias do proprio app: Documentos\ETS2TrailerControl\app.ini
struct AppPrefs {
    bool returnToGame = true;   // voltar ao jogo depois de cada comando
    std::wstring lastConfigPath; // ultimo trailer_control.ini informado pelo plugin

    void load();
    bool save() const;
};

std::string narrow(const std::wstring &w); // UTF-16 -> UTF-8

// Abre a janela de configuracoes (modal). configPath vazio = plugin nunca conectou.
// Devolve true se salvou o .ini do plugin (o chamador manda "recarregar").
bool showSettingsDialog(HWND owner, const std::wstring &configPath, AppPrefs &prefs);

} // namespace tc::app
