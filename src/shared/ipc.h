#pragma once
// Protocolo entre o plugin (dentro do jogo) e o app externo "Trailer Control".
// Compartilhado pelos dois executaveis; nao depende do SCS SDK.
//
// Pasta: Documentos\ETS2TrailerControl\
//
//   status.json  plugin -> app. Regravado a cada mudanca e pelo menos 1x por segundo
//                (campo "timestamp_ms" = batimento). Gravado em .tmp + troca atomica.
//
//   command.txt  app -> plugin. Uma linha por comando:  <seq> <acao> [articulacao]
//                O app grava em command.txt.tmp e troca atomicamente. O plugin "pega" o
//                arquivo renomeando para command.txt.taken, le e apaga. Comandos novos que
//                chegarem enquanto isso vao para um command.txt novo.
//
//   Acoes:  desengatar <n>     solta a articulacao n (0 = cavalo <-> 1o reboque)
//           engatar <n>        engata a articulacao n
//           selecionar <n>     muda a articulacao selecionada (a mesma dos atalhos)
//           desengatar_tudo    solta o conjunto inteiro do cavalo
//           listar             escreve a composicao no game.log.txt
//           diagnostico        escreve no game.log.txt a cadeia lida da memoria (so leitura)
//
//   O resultado de cada comando volta em status.json -> "last_command": {"seq", "result"}.

#include <windows.h>
#include <shlobj.h>

#include <string>

namespace tc::ipc {

constexpr int kProtocolVersion = 1;

// Janela do app: o plugin a encontra pela classe para mostra-la (atalho Ctrl + -).
constexpr const wchar_t *kAppWindowClass = L"ETS2TrailerControlApp";
// plugin -> app: "mostre-se"; wParam = HWND da janela do jogo (para "voltar ao jogo").
constexpr UINT kMsgShowFromGame = WM_APP + 0x54;
constexpr const wchar_t *kFolderName = L"ETS2TrailerControl";
constexpr const wchar_t *kStatusFile = L"status.json";
constexpr const wchar_t *kCommandFile = L"command.txt";

// Batimento: o plugin regrava status.json pelo menos a cada kHeartbeatMs.
constexpr unsigned kHeartbeatMs = 1000;
// O app considera o jogo desconectado se o batimento ficar mais velho que isso.
constexpr unsigned kStaleMs = 3500;

// Documentos\ETS2TrailerControl (cria se nao existir). Vazio se falhar.
inline std::wstring folder() {
    PWSTR docs = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &docs))) return {};
    std::wstring dir = docs;
    CoTaskMemFree(docs);
    dir += L"\\";
    dir += kFolderName;
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

inline std::wstring statusPath() {
    const std::wstring d = folder();
    return d.empty() ? d : d + L"\\" + kStatusFile;
}

inline std::wstring commandPath() {
    const std::wstring d = folder();
    return d.empty() ? d : d + L"\\" + kCommandFile;
}

// Tempo Unix em milissegundos (para o batimento).
inline unsigned long long nowUnixMs() {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER u;
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    return (u.QuadPart - 116444736000000000ULL) / 10000ULL;
}

// Grava texto num arquivo de forma atomica (tmp + troca).
inline bool writeAtomic(const std::wstring &path, const std::string &text) {
    const std::wstring tmp = path + L".tmp";
    HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const BOOL ok = WriteFile(h, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
    CloseHandle(h);
    if (!ok || written != text.size()) return false;
    return MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
}

// Le o arquivo inteiro (compartilhado, aceita escrita/troca concorrente).
inline bool readAll(const std::wstring &path, std::string &out) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(h, &size) || size.QuadPart > 4 * 1024 * 1024) {
        CloseHandle(h);
        return false;
    }
    out.resize(static_cast<size_t>(size.QuadPart));
    DWORD read = 0;
    const BOOL ok = out.empty() || ReadFile(h, &out[0], static_cast<DWORD>(out.size()), &read, nullptr);
    CloseHandle(h);
    if (!ok) return false;
    out.resize(read);
    return true;
}

} // namespace tc::ipc
