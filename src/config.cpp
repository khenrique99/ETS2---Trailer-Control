#include "config.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace tc {

std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string trim(const std::string &s) {
    const auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return {};
    const auto e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

bool Config::load(const std::wstring &path) {
    std::ifstream in(path);
    if (!in) return false;

    std::string line, section;
    while (std::getline(in, line)) {
        // remove BOM UTF-8
        if (line.size() >= 3 && static_cast<unsigned char>(line[0]) == 0xEF) line = line.substr(3);
        const auto cpos = line.find_first_of(";#");
        if (cpos != std::string::npos) line = line.substr(0, cpos);
        line = trim(line);
        if (line.empty()) continue;

        if (line.front() == '[' && line.back() == ']') {
            section = toLower(trim(line.substr(1, line.size() - 2)));
            continue;
        }
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        data_[section][toLower(trim(line.substr(0, eq)))] = trim(line.substr(eq + 1));
    }
    return true;
}

std::string Config::get(const std::string &section, const std::string &key,
                        const std::string &fallback) const {
    const auto s = data_.find(toLower(section));
    if (s == data_.end()) return fallback;
    const auto k = s->second.find(toLower(key));
    return k == s->second.end() ? fallback : k->second;
}

int Config::getInt(const std::string &section, const std::string &key, int fallback) const {
    const auto v = get(section, key);
    return v.empty() ? fallback : std::atoi(v.c_str());
}

double Config::getDouble(const std::string &section, const std::string &key, double fallback) const {
    const auto v = get(section, key);
    return v.empty() ? fallback : std::atof(v.c_str());
}

bool Config::getBool(const std::string &section, const std::string &key, bool fallback) const {
    const auto v = toLower(get(section, key));
    if (v.empty()) return fallback;
    return v == "1" || v == "true" || v == "sim" || v == "yes" || v == "on";
}

std::vector<std::string> Config::getList(const std::string &section, const std::string &key) const {
    std::vector<std::string> out;
    std::stringstream ss(get(section, key));
    std::string item;
    while (std::getline(ss, item, ',')) {
        item = toLower(trim(item));
        if (!item.empty()) out.push_back(item);
    }
    return out;
}

std::wstring pluginDirectory() {
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&pluginDirectory), &self);
    wchar_t buf[MAX_PATH] = {};
    GetModuleFileNameW(self, buf, MAX_PATH);
    std::wstring path(buf);
    const auto slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"." : path.substr(0, slash);
}

} // namespace tc
