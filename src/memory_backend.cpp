#include "memory_backend.h"

#include "log.h"

#include <windows.h>
#include <psapi.h>

#include <cstdlib>
#include <sstream>
#include <vector>

namespace tc {

MemoryBackend &MemoryBackend::instance() {
    static MemoryBackend inst;
    return inst;
}

std::uintptr_t MemoryBackend::findPattern(const std::string &pattern) {
    // Converte "48 8B ?? 05" em bytes + mascara
    std::vector<int> bytes;
    std::istringstream ss(pattern);
    std::string tok;
    while (ss >> tok) {
        if (tok == "?" || tok == "??") bytes.push_back(-1);
        else bytes.push_back(static_cast<int>(std::strtoul(tok.c_str(), nullptr, 16)));
    }
    if (bytes.empty()) return 0;

    HMODULE game = GetModuleHandleW(nullptr);
    MODULEINFO info{};
    if (!GetModuleInformation(GetCurrentProcess(), game, &info, sizeof(info))) return 0;

    const auto *base = static_cast<const std::uint8_t *>(info.lpBaseOfDll);
    const size_t size = info.SizeOfImage;
    const size_t n = bytes.size();

    std::uintptr_t found = 0;
    int matches = 0;
    for (size_t i = 0; i + n <= size; ++i) {
        size_t k = 0;
        for (; k < n; ++k) {
            if (bytes[k] != -1 && base[i + k] != static_cast<std::uint8_t>(bytes[k])) break;
        }
        if (k == n) {
            if (++matches == 1) found = reinterpret_cast<std::uintptr_t>(base + i);
            else break; // ambiguo
        }
    }
    if (matches > 1) {
        log::warn("Assinatura ambigua (mais de 1 ocorrencia): %s", pattern.c_str());
        return 0;
    }
    return found;
}

bool MemoryBackend::initialize(bool enabled, const std::string &gameVersion,
                               const std::string &expectedVersion,
                               const std::string &detachSignature,
                               const std::string &attachSignature) {
    ready_ = false;
    if (!enabled) {
        log::info("Backend de memoria desativado (articulacoes internas indisponiveis).");
        return false;
    }
    // Trava de seguranca: offsets mudam a cada atualizacao do jogo.
    if (expectedVersion.empty() || gameVersion != expectedVersion) {
        log::warn("Backend de memoria: versao do jogo %s difere da esperada '%s'. Desativado.",
                  gameVersion.c_str(), expectedVersion.c_str());
        return false;
    }
    if (detachSignature.empty() || attachSignature.empty()) {
        log::warn("Backend de memoria: assinaturas nao configuradas no .ini. Desativado.");
        return false;
    }
    detachFn_ = findPattern(detachSignature);
    attachFn_ = findPattern(attachSignature);
    if (!detachFn_ || !attachFn_) {
        log::warn("Backend de memoria: assinaturas nao encontradas nesta versao. Desativado.");
        return false;
    }
    log::info("Backend de memoria: funcoes localizadas (detach=%p attach=%p).",
              reinterpret_cast<void *>(detachFn_), reinterpret_cast<void *>(attachFn_));
    ready_ = true;
    return true;
}

CouplingResult MemoryBackend::detach(const Joint &joint) {
    if (!ready_) return CouplingResult::Unavailable;
    // TODO(v2): chamar a rotina interna de desacoplamento da articulacao `joint.index`.
    // A convencao de chamada e a estrutura do objeto de reboque precisam ser
    // confirmadas por engenharia reversa antes de qualquer chamada real.
    log::warn("Backend de memoria: desengate da articulacao %d ainda nao implementado.", joint.index);
    return CouplingResult::NotSupported;
}

CouplingResult MemoryBackend::attach(const Joint &joint) {
    if (!ready_) return CouplingResult::Unavailable;
    // TODO(v2): idem detach.
    log::warn("Backend de memoria: engate da articulacao %d ainda nao implementado.", joint.index);
    return CouplingResult::NotSupported;
}

} // namespace tc
