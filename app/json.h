#pragma once
// Leitor de JSON minimo (so o necessario para status.json). Sem dependencias.
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace tc::json {

class Value {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Type type = Type::Null;
    bool b = false;
    double n = 0.0;
    std::string s;
    std::vector<Value> arr;
    std::map<std::string, Value> obj;

    bool isNull() const { return type == Type::Null; }

    // Acesso tolerante: chave/indice ausente devolve um Value nulo.
    const Value &operator[](const std::string &key) const;
    const Value &operator[](size_t i) const;
    size_t size() const { return type == Type::Array ? arr.size() : 0; }

    bool asBool(bool fallback = false) const { return type == Type::Bool ? b : fallback; }
    double asNumber(double fallback = 0.0) const { return type == Type::Number ? n : fallback; }
    long long asInt(long long fallback = 0) const {
        return type == Type::Number ? static_cast<long long>(n) : fallback;
    }
    const std::string &asString() const;
};

// false se o texto nao for JSON valido.
bool parse(const std::string &text, Value &out, std::string *error = nullptr);

} // namespace tc::json
