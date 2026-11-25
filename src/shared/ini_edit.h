#pragma once
// Edicao de .ini preservando comentarios e a ordem das linhas. Usado pela tela de
// configuracoes do app para mexer no trailer_control.ini do plugin.
#include <string>
#include <vector>

namespace tc::ini {

struct Value {
    std::string section; // sem colchetes
    std::string key;
    std::string value;   // sem ';' nem '#' (sao comentario no .ini)
};

// Aplica os valores ao texto do .ini: troca o valor das chaves existentes (mantendo o
// comentario na mesma linha), acrescenta chaves que faltam no fim da secao e secoes que
// faltam no fim do arquivo.
std::string apply(const std::string &text, const std::vector<Value> &values);

// Le um valor do texto (vazio se nao existir).
std::string get(const std::string &text, const std::string &section, const std::string &key);

// Remove caracteres que o .ini trataria como comentario ou quebra de linha.
std::string sanitize(const std::string &v);

} // namespace tc::ini
