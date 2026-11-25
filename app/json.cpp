#include "json.h"

#include <cstdlib>

namespace tc::json {

namespace {

const Value &nullValue() {
    static const Value v;
    return v;
}

class Parser {
public:
    explicit Parser(const std::string &t) : t_(t) {}

    bool parseDocument(Value &out) {
        skipWs();
        if (!parseValue(out, 0)) return false;
        skipWs();
        if (p_ != t_.size()) return fail("conteudo apos o fim");
        return true;
    }

    std::string error;

private:
    bool fail(const char *msg) {
        if (error.empty()) error = std::string(msg) + " (pos " + std::to_string(p_) + ")";
        return false;
    }

    void skipWs() {
        while (p_ < t_.size() && (t_[p_] == ' ' || t_[p_] == '\n' || t_[p_] == '\r' || t_[p_] == '\t')) ++p_;
    }

    bool literal(const char *word) {
        size_t i = 0;
        for (; word[i]; ++i) {
            if (p_ + i >= t_.size() || t_[p_ + i] != word[i]) return false;
        }
        p_ += i;
        return true;
    }

    bool parseValue(Value &v, int depth) {
        if (depth > 32) return fail("aninhamento demais");
        if (p_ >= t_.size()) return fail("fim inesperado");
        const char c = t_[p_];
        if (c == '{') return parseObject(v, depth);
        if (c == '[') return parseArray(v, depth);
        if (c == '"') {
            v.type = Value::Type::String;
            return parseString(v.s);
        }
        if (literal("true")) { v.type = Value::Type::Bool; v.b = true; return true; }
        if (literal("false")) { v.type = Value::Type::Bool; v.b = false; return true; }
        if (literal("null")) { v.type = Value::Type::Null; return true; }
        return parseNumber(v);
    }

    bool parseNumber(Value &v) {
        const char *start = t_.c_str() + p_;
        char *end = nullptr;
        const double d = std::strtod(start, &end);
        if (end == start) return fail("valor invalido");
        p_ += static_cast<size_t>(end - start);
        v.type = Value::Type::Number;
        v.n = d;
        return true;
    }

    static void appendUtf8(std::string &o, unsigned cp) {
        if (cp < 0x80) o += static_cast<char>(cp);
        else if (cp < 0x800) {
            o += static_cast<char>(0xC0 | (cp >> 6));
            o += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            o += static_cast<char>(0xE0 | (cp >> 12));
            o += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            o += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }

    bool parseString(std::string &out) {
        ++p_; // "
        while (p_ < t_.size()) {
            const char c = t_[p_++];
            if (c == '"') return true;
            if (c != '\\') {
                out += c;
                continue;
            }
            if (p_ >= t_.size()) break;
            const char e = t_[p_++];
            switch (e) {
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u': {
                if (p_ + 4 > t_.size()) return fail("\\u incompleto");
                const unsigned cp = static_cast<unsigned>(std::strtoul(t_.substr(p_, 4).c_str(), nullptr, 16));
                p_ += 4;
                appendUtf8(out, cp);
                break;
            }
            default: return fail("escape invalido");
            }
        }
        return fail("texto sem fim");
    }

    bool parseArray(Value &v, int depth) {
        v.type = Value::Type::Array;
        ++p_;
        skipWs();
        if (p_ < t_.size() && t_[p_] == ']') { ++p_; return true; }
        while (true) {
            Value item;
            skipWs();
            if (!parseValue(item, depth + 1)) return false;
            v.arr.push_back(std::move(item));
            skipWs();
            if (p_ >= t_.size()) return fail("lista sem fim");
            if (t_[p_] == ',') { ++p_; continue; }
            if (t_[p_] == ']') { ++p_; return true; }
            return fail("esperava , ou ]");
        }
    }

    bool parseObject(Value &v, int depth) {
        v.type = Value::Type::Object;
        ++p_;
        skipWs();
        if (p_ < t_.size() && t_[p_] == '}') { ++p_; return true; }
        while (true) {
            skipWs();
            if (p_ >= t_.size() || t_[p_] != '"') return fail("esperava chave");
            std::string key;
            if (!parseString(key)) return false;
            skipWs();
            if (p_ >= t_.size() || t_[p_] != ':') return fail("esperava :");
            ++p_;
            skipWs();
            Value item;
            if (!parseValue(item, depth + 1)) return false;
            v.obj[key] = std::move(item);
            skipWs();
            if (p_ >= t_.size()) return fail("objeto sem fim");
            if (t_[p_] == ',') { ++p_; continue; }
            if (t_[p_] == '}') { ++p_; return true; }
            return fail("esperava , ou }");
        }
    }

    const std::string &t_;
    size_t p_ = 0;
};

} // namespace

const Value &Value::operator[](const std::string &key) const {
    if (type != Type::Object) return nullValue();
    const auto it = obj.find(key);
    return it == obj.end() ? nullValue() : it->second;
}

const Value &Value::operator[](size_t i) const {
    if (type != Type::Array || i >= arr.size()) return nullValue();
    return arr[i];
}

const std::string &Value::asString() const {
    static const std::string empty;
    return type == Type::String ? s : empty;
}

bool parse(const std::string &text, Value &out, std::string *error) {
    Parser p(text);
    out = Value{};
    const bool ok = p.parseDocument(out);
    if (!ok && error) *error = p.error;
    return ok;
}

} // namespace tc::json
