#include "shared/ini_edit.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace tc::ini {

namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string trimmed(const std::string &s) {
    const auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return {};
    const auto e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

std::vector<std::string> splitLines(const std::string &text, std::string &eol) {
    eol = text.find("\r\n") != std::string::npos ? "\r\n" : "\n";
    std::vector<std::string> lines;
    std::string line;
    std::istringstream in(text);
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(line);
    }
    return lines;
}

// Secao de uma linha "[nome]" (minusculas) ou vazio.
bool sectionOf(const std::string &line, std::string &out) {
    const std::string t = trimmed(line);
    if (t.size() < 2 || t.front() != '[') return false;
    const auto close = t.find(']');
    if (close == std::string::npos) return false;
    out = lower(trimmed(t.substr(1, close - 1)));
    return true;
}

// Chave de uma linha "chave = valor ; comentario" (minusculas) ou vazio.
std::string keyOf(const std::string &line) {
    const std::string t = trimmed(line);
    if (t.empty() || t[0] == ';' || t[0] == '#') return {};
    const auto eq = t.find('=');
    if (eq == std::string::npos) return {};
    return lower(trimmed(t.substr(0, eq)));
}

} // namespace

std::string sanitize(const std::string &v) {
    std::string o;
    for (char c : v) {
        if (c == ';' || c == '#' || c == '\r' || c == '\n') continue;
        o += c;
    }
    return trimmed(o);
}

std::string get(const std::string &text, const std::string &section, const std::string &key) {
    std::string eol, current;
    const std::string sec = lower(section), k = lower(key);
    for (const std::string &line : splitLines(text, eol)) {
        std::string s;
        if (sectionOf(line, s)) {
            current = s;
            continue;
        }
        if (current != sec || keyOf(line) != k) continue;
        std::string v = line.substr(line.find('=') + 1);
        const auto c = v.find_first_of(";#");
        if (c != std::string::npos) v = v.substr(0, c);
        return trimmed(v);
    }
    return {};
}

std::string apply(const std::string &text, const std::vector<Value> &values) {
    std::string eol;
    std::vector<std::string> lines = splitLines(text, eol);
    std::vector<bool> done(values.size(), false);

    // 1) troca no lugar
    std::string current;
    for (std::string &line : lines) {
        std::string s;
        if (sectionOf(line, s)) {
            current = s;
            continue;
        }
        const std::string k = keyOf(line);
        if (k.empty()) continue;
        for (size_t i = 0; i < values.size(); ++i) {
            if (done[i] || lower(values[i].section) != current || lower(values[i].key) != k) continue;
            const auto eq = line.find('=');
            const auto comment = line.find_first_of(";#", eq);
            std::string tail;
            if (comment != std::string::npos) {
                // mantem o alinhamento do comentario quando o valor cabe
                const size_t valueStart = eq + 2;
                const std::string v = sanitize(values[i].value);
                const size_t pad = comment > valueStart + v.size() ? comment - valueStart - v.size() : 1;
                tail = std::string(pad, ' ') + line.substr(comment);
            }
            line = line.substr(0, eq) + "= " + sanitize(values[i].value) + tail;
            done[i] = true;
        }
    }

    // 2) chaves que faltam em secoes existentes / secoes que faltam
    for (size_t i = 0; i < values.size(); ++i) {
        if (done[i]) continue;
        const std::string sec = lower(values[i].section);
        const std::string newLine = values[i].key + " = " + sanitize(values[i].value);
        int lastInSection = -1;
        bool inSec = false;
        for (size_t n = 0; n < lines.size(); ++n) {
            std::string s;
            if (sectionOf(lines[n], s)) {
                inSec = s == sec;
                if (inSec) lastInSection = static_cast<int>(n);
                continue;
            }
            if (inSec && !trimmed(lines[n]).empty()) lastInSection = static_cast<int>(n);
        }
        if (lastInSection >= 0) {
            lines.insert(lines.begin() + lastInSection + 1, newLine);
        } else {
            if (!lines.empty() && !trimmed(lines.back()).empty()) lines.push_back("");
            lines.push_back("[" + values[i].section + "]");
            lines.push_back(newLine);
        }
        done[i] = true;
    }

    std::string out;
    for (const std::string &l : lines) out += l + eol;
    return out;
}

} // namespace tc::ini
