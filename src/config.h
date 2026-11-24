#pragma once
#include <map>
#include <string>
#include <vector>

namespace tc {

// INI simples: [secao] chave=valor ; comentarios com ';' ou '#'
class Config {
public:
    bool load(const std::wstring &path);

    std::string get(const std::string &section, const std::string &key,
                    const std::string &fallback = "") const;
    int getInt(const std::string &section, const std::string &key, int fallback) const;
    double getDouble(const std::string &section, const std::string &key, double fallback) const;
    bool getBool(const std::string &section, const std::string &key, bool fallback) const;
    std::vector<std::string> getList(const std::string &section, const std::string &key) const;

private:
    std::map<std::string, std::map<std::string, std::string>> data_;
};

// Pasta onde a DLL esta (bin/win_x64/plugins).
std::wstring pluginDirectory();

std::string toLower(std::string s);
std::string trim(const std::string &s);

} // namespace tc
