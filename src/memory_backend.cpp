#include "memory_backend.h"

#include "log.h"
#include "trailer_registry.h"

#include <windows.h>
#include <psapi.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <vector>

namespace tc {

// ---------------------------------------------------------------------------------------------
// Tabela por versao do jogo. Enderecos sao RVAs (relativos a base do eurotrucks2.exe).
// Fonte: docs/re/structures-1.62.3.3.md
// ---------------------------------------------------------------------------------------------
struct MemoryBackend::Layout {
    const char *exeVersion;

    std::uint32_t rvaGameGlobal;      // ponteiro global para o objeto "game"
    std::uint32_t offGameActor;       // game -> ator do jogador
    std::uint32_t offActorTruck;      // ator -> caminhao
    std::uint32_t offTruckFirstTrailer; // caminhao -> 1o reboque fisico
    std::uint32_t offTrailerNext;     // reboque -> reboque de tras (owner_ptr, nunca escrever)
    std::uint32_t offTrailerMaster;   // reboque -> mestre fisico (0 = solto)
    std::uint32_t offHoseCount;       // veiculo -> quantidade de mangueiras (cabos) que ele segura

    std::uint32_t vtSlotConnect;      // vtable do reboque: engatar(this, master, pos, ...)
    std::uint32_t vtSlotDisconnect;   // vtable do reboque: desengatar(this)
    std::uint32_t rvaConnect;         // alvo esperado de vtSlotConnect
    std::uint32_t rvaDisconnect;      // alvo esperado de vtSlotDisconnect
    std::uint32_t rvaReconnectSlave;  // void f(reboque_mestre): engata o reboque de tras nele
    std::uint32_t rvaSlaveHookPos;    // float* f(reboque, float out[3]): engate traseiro (local)
    std::uint32_t rvaClearHoses;      // void f(frente, tras): remove as mangueiras da frente e apaga as visuais
    std::uint32_t rvaBuildHoses;      // void f(frente, tras): cria as mangueiras frente -> tras (tras = 0: repouso)

    // Primeiros bytes de cada funcao, conferidos antes de ativar.
    unsigned char sigConnect[16];
    unsigned char sigDisconnect[16];
    unsigned char sigReconnectSlave[16];
    unsigned char sigSlaveHookPos[16];
    unsigned char sigClearHoses[16];
    unsigned char sigBuildHoses[16];
};

namespace {

const MemoryBackend::Layout *knownLayouts();

// ---- acesso protegido a memoria (SEH; sem objetos C++ nestas funcoes) ----

bool safeReadPtr(std::uintptr_t addr, std::uintptr_t &out) {
    __try {
        out = *reinterpret_cast<const std::uintptr_t *>(addr);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool safeCompare(std::uintptr_t addr, const unsigned char *expected, size_t n) {
    __try {
        return std::memcmp(reinterpret_cast<const void *>(addr), expected, n) == 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

using ThisCall = void(__fastcall *)(std::uintptr_t self);
using HookPosCall = float *(__fastcall *)(std::uintptr_t self, float *out);

bool safeCall(std::uintptr_t fn, std::uintptr_t self) {
    __try {
        reinterpret_cast<ThisCall>(fn)(self);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

using PairCall = void(__fastcall *)(std::uintptr_t front, std::uintptr_t rear);

bool safeCallPair(std::uintptr_t fn, std::uintptr_t front, std::uintptr_t rear) {
    __try {
        reinterpret_cast<PairCall>(fn)(front, rear);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool safeHookPos(std::uintptr_t fn, std::uintptr_t self, float *out) {
    __try {
        reinterpret_cast<HookPosCall>(fn)(self, out);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// So aceita ponteiros alinhados dentro do espaco de usuario x64.
bool plausible(std::uintptr_t p) {
    return p >= 0x10000 && p < 0x00007FFFFFFFFFFFull && (p & 7) == 0;
}

std::string readExeVersion() {
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    DWORD dummy = 0;
    const DWORD size = GetFileVersionInfoSizeW(path, &dummy);
    if (size == 0) return {};
    std::vector<unsigned char> data(size);
    if (!GetFileVersionInfoW(path, 0, size, data.data())) return {};
    VS_FIXEDFILEINFO *fi = nullptr;
    UINT len = 0;
    if (!VerQueryValueW(data.data(), L"\\", reinterpret_cast<void **>(&fi), &len) || !fi) return {};
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%u.%u.%u.%u", HIWORD(fi->dwFileVersionMS), LOWORD(fi->dwFileVersionMS),
                  HIWORD(fi->dwFileVersionLS), LOWORD(fi->dwFileVersionLS));
    return buf;
}

struct Vec3 {
    double x, y, z;
};

// Local -> mundo usando a orientacao do SDK (heading/pitch/roll em fracoes de volta).
// Convencao SCS: local X direita, Y cima, Z para tras; heading anti-horario visto de cima.
Vec3 toWorld(const TrailerPlacement &p, double lx, double ly, double lz) {
    const double tau = 6.283185307179586;
    const double h = p.heading * tau, pi = p.pitch * tau, r = p.roll * tau;
    // roll (eixo Z)
    double x = lx * std::cos(r) - ly * std::sin(r);
    double y = lx * std::sin(r) + ly * std::cos(r);
    double z = lz;
    // pitch (eixo X)
    const double y2 = y * std::cos(pi) - z * std::sin(pi);
    const double z2 = y * std::sin(pi) + z * std::cos(pi);
    y = y2;
    z = z2;
    // heading (eixo Y)
    const double x3 = x * std::cos(h) + z * std::sin(h);
    const double z3 = -x * std::sin(h) + z * std::cos(h);
    return {p.x + x3, p.y + y, p.z + z3};
}

} // namespace

MemoryBackend &MemoryBackend::instance() {
    static MemoryBackend inst;
    return inst;
}

std::uintptr_t MemoryBackend::findPattern(const std::string &pattern) {
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
            else break;
        }
    }
    if (matches > 1) {
        log::warn("Assinatura ambigua (mais de 1 ocorrencia): %s", pattern.c_str());
        return 0;
    }
    return found;
}

bool MemoryBackend::initialize(bool enabled, const std::string &expectedVersion, double maxAttachDistanceM) {
    ready_ = false;
    layout_ = nullptr;
    maxAttachDistanceM_ = maxAttachDistanceM;
    base_ = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    exeVersion_ = readExeVersion();

    if (!enabled) {
        statusText_ = "desativado no .ini ([memoria] habilitado = 0)";
        log::info("Backend de memoria desativado (articulacoes internas indisponiveis).");
        return false;
    }
    if (!expectedVersion.empty() && expectedVersion != exeVersion_) {
        statusText_ = "versao do jogo " + exeVersion_ + " diferente da exigida no .ini (" + expectedVersion + ")";
        log::warn("Backend de memoria: %s. Desativado.", statusText_.c_str());
        return false;
    }
    for (const Layout *l = knownLayouts(); l->exeVersion; ++l) {
        if (exeVersion_ == l->exeVersion) layout_ = l;
    }
    if (!layout_) {
        statusText_ = exeVersion_.empty() ? "versao do jogo desconhecida (este processo nao e o eurotrucks2.exe?)"
                                          : "versao do jogo " + exeVersion_ + " ainda nao suportada";
        log::warn("Backend de memoria: %s. Desativado.", statusText_.c_str());
        return false;
    }

    const struct {
        std::uint32_t rva;
        const unsigned char *sig;
        const char *what;
    } checks[] = {
        {layout_->rvaConnect, layout_->sigConnect, "engatar"},
        {layout_->rvaDisconnect, layout_->sigDisconnect, "desengatar"},
        {layout_->rvaReconnectSlave, layout_->sigReconnectSlave, "reconectar"},
        {layout_->rvaSlaveHookPos, layout_->sigSlaveHookPos, "posicao do engate"},
        {layout_->rvaClearHoses, layout_->sigClearHoses, "remover mangueiras"},
        {layout_->rvaBuildHoses, layout_->sigBuildHoses, "criar mangueiras"},
    };
    for (const auto &c : checks) {
        if (!safeCompare(base_ + c.rva, c.sig, 16)) {
            statusText_ = std::string("bytes da funcao '") + c.what + "' nao conferem (exe modificado?)";
            log::warn("Backend de memoria: %s. Desativado.", statusText_.c_str());
            return false;
        }
    }

    statusText_ = "ativo (" + exeVersion_ + ")";
    log::info("Backend de memoria ativo para %s (base %p).", exeVersion_.c_str(),
              reinterpret_cast<void *>(base_));
    ready_ = true;
    return true;
}

std::uintptr_t MemoryBackend::trailerObject(int chainIndex) const {
    if (!ready_ || chainIndex < 0 || chainIndex >= kMaxTrailers) return 0;
    std::uintptr_t game = 0, actor = 0, truck = 0, t = 0;
    if (!safeReadPtr(base_ + layout_->rvaGameGlobal, game) || !plausible(game)) return 0;
    if (!safeReadPtr(game + layout_->offGameActor, actor) || !plausible(actor)) return 0;
    if (!safeReadPtr(actor + layout_->offActorTruck, truck) || !plausible(truck)) return 0;
    if (!safeReadPtr(truck + layout_->offTruckFirstTrailer, t) || !plausible(t)) return 0;
    for (int i = 0; i < chainIndex; ++i) {
        if (!safeReadPtr(t + layout_->offTrailerNext, t) || !plausible(t)) return 0;
    }
    // Confere a vtable: so aceita objetos cujas rotinas de engate sao as esperadas.
    std::uintptr_t vt = 0, fnDisconnect = 0;
    if (!safeReadPtr(t, vt) || !plausible(vt)) return 0;
    if (!safeReadPtr(vt + layout_->vtSlotDisconnect, fnDisconnect)) return 0;
    if (fnDisconnect != base_ + layout_->rvaDisconnect) return 0;
    return t;
}

void MemoryBackend::setMultiplayer(const std::string &why) {
    if (why == multiplayer_) return;
    multiplayer_ = why;
    if (why.empty()) log::info("Backend de memoria: single-player, liberado de novo.");
    else log::warn("Backend de memoria desligado: %s.", why.c_str());
}

std::string MemoryBackend::statusText() const {
    if (!multiplayer_.empty()) return "desligado no multiplayer (" + multiplayer_ + ")";
    return statusText_;
}

bool MemoryBackend::probeCoupled(int jointIndex, bool &coupled) const {
    if (!available() || jointIndex < 1) return false;
    const std::uintptr_t slave = trailerObject(jointIndex);
    if (!slave) return false;
    std::uintptr_t master = 0;
    if (!safeReadPtr(slave + layout_->offTrailerMaster, master)) return false;
    coupled = master != 0;
    return true;
}

std::string MemoryBackend::diagnose() const {
    if (!multiplayer_.empty()) return "desligado no multiplayer (" + multiplayer_ + ")";
    log::info("=== Diagnostico de memoria ===");
    log::info("exe %s, base %p, backend: %s", exeVersion_.c_str(), reinterpret_cast<void *>(base_),
              statusText().c_str());
    const Layout *l = layout_;
    if (!l) {
        for (const Layout *k = knownLayouts(); k->exeVersion; ++k) {
            if (exeVersion_ == k->exeVersion) l = k;
        }
    }
    if (!l) {
        log::info("Sem tabela de enderecos para esta versao; nada a ler.");
        return "sem tabela para a versao " + exeVersion_;
    }
    auto hex = [](std::uintptr_t v) {
        char b[32];
        std::snprintf(b, sizeof(b), "%p", reinterpret_cast<void *>(v));
        return std::string(b);
    };
    std::uintptr_t game = 0, actor = 0, truck = 0, t = 0;
    const bool okGame = safeReadPtr(base_ + l->rvaGameGlobal, game) && plausible(game);
    log::info("game   = %s %s", hex(game).c_str(), okGame ? "" : "(INVALIDO)");
    if (!okGame) return "ponteiro global do jogo invalido";
    const bool okActor = safeReadPtr(game + l->offGameActor, actor) && plausible(actor);
    log::info("ator   = %s %s", hex(actor).c_str(), okActor ? "" : "(INVALIDO - fora da cabine?)");
    if (!okActor) return "ator do jogador nao encontrado";
    const bool okTruck = safeReadPtr(actor + l->offActorTruck, truck) && plausible(truck);
    log::info("truck  = %s %s", hex(truck).c_str(), okTruck ? "" : "(INVALIDO)");
    if (!okTruck) return "caminhao nao encontrado";
    std::uintptr_t state = 0;
    safeReadPtr(truck + 0x14b8, state);
    log::info("estado do engate (truck+0x14b8) = %u", static_cast<unsigned>(state & 0xffffffffu));

    int count = 0, okVt = 0;
    bool okFirst = safeReadPtr(truck + l->offTruckFirstTrailer, t) && plausible(t);
    for (int i = 0; okFirst && i < kMaxTrailers; ++i) {
        std::uintptr_t vt = 0, c = 0, d = 0, master = 0, next = 0;
        safeReadPtr(t, vt);
        safeReadPtr(vt + l->vtSlotConnect, c);
        const bool vtOk = plausible(vt) && safeReadPtr(vt + l->vtSlotDisconnect, d) &&
                          d == base_ + l->rvaDisconnect;
        safeReadPtr(t + l->offTrailerMaster, master);
        log::info("reboque %d = %s vtable %s (rva %llx) conectar %s (rva %llx), desconectar %s (rva %llx) %s, mestre %s",
                  i, hex(t).c_str(), hex(vt).c_str(),
                  static_cast<unsigned long long>(vt - base_),
                  hex(c).c_str(), static_cast<unsigned long long>(c ? c - base_ : 0),
                  hex(d).c_str(), static_cast<unsigned long long>(d ? d - base_ : 0),
                  vtOk ? "OK" : "NAO CONFERE", hex(master).c_str());
        ++count;
        if (vtOk) ++okVt;
        if (!safeReadPtr(t + l->offTrailerNext, next) || !plausible(next)) break;
        t = next;
    }
    if (!okFirst) log::info("nenhum reboque ligado ao caminhao (truck+0x1580 vazio)");
    char buf[128];
    std::snprintf(buf, sizeof(buf), "%d reboque(s) na cadeia, %d com vtable conferida", count, okVt);
    log::info("=== %s ===", buf);
    return buf;
}

bool MemoryBackend::hookDistance(int jointIndex, std::uintptr_t master, double &meters) const {
    if (!registry_) return false;
    TrailerPlacement pm, ps;
    float slaveHook[3];
    if (!registry_->trailerGeometry(jointIndex - 1, pm, nullptr)) return false;
    if (!registry_->trailerGeometry(jointIndex, ps, slaveHook)) return false;
    float masterHook[3] = {};
    if (!safeHookPos(base_ + layout_->rvaSlaveHookPos, master, masterHook)) return false;
    const Vec3 a = toWorld(pm, masterHook[0], masterHook[1], masterHook[2]);
    const Vec3 b = toWorld(ps, slaveHook[0], slaveHook[1], slaveHook[2]);
    meters = std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z));
    return std::isfinite(meters);
}

bool MemoryBackend::hasHoses(std::uintptr_t vehicle) const {
    std::uintptr_t count = 0;
    return safeReadPtr(vehicle + layout_->offHoseCount, count) && count != 0 && count < 64;
}

// Trecho da cadeia a partir de `first` (inclusive) ate o ultimo reboque; devolve quantos.
int MemoryBackend::chainFrom(std::uintptr_t first, std::uintptr_t *out, int max) const {
    int n = 0;
    std::uintptr_t t = first;
    while (n < max && plausible(t)) {
        out[n++] = t;
        if (!safeReadPtr(t + layout_->offTrailerNext, t)) break;
    }
    return n;
}

// Mesma sequencia que o jogo faz ao soltar/engatar o reboque do cavalo (FUN_1405dbd20 /
// FUN_1405db530): apaga as mangueiras da frente e recria as dos pares que continuam
// ligados. So refaz onde ja havia mangueira, assim respeita a opcao de cabos do jogo.
void MemoryBackend::rebuildHoses(std::uintptr_t master, std::uintptr_t slave, bool masterHadHoses,
                                 bool pairConnected) {
    std::uintptr_t chain[kMaxTrailers];
    const int n = chainFrom(slave, chain, kMaxTrailers);
    if (n == 0) return;
    bool downstream[kMaxTrailers] = {};
    bool any = masterHadHoses;
    for (int i = 0; i + 1 < n; ++i) {
        downstream[i] = hasHoses(chain[i]);
        any = any || downstream[i];
    }
    if (!any) return;

    // Apaga: cabos fisicos da frente + visual "cables_off" do reboque e dos de tras.
    if (!safeCallPair(base_ + layout_->rvaClearHoses, master, slave)) {
        log::error("Backend de memoria: excecao ao remover as mangueiras.");
        return;
    }
    bool ok = true;
    for (int i = 0; i + 1 < n; ++i) {
        if (downstream[i]) ok = safeCallPair(base_ + layout_->rvaBuildHoses, chain[i], chain[i + 1]) && ok;
    }
    if (masterHadHoses) {
        // Engatado: mangueira do mestre vai para o reboque. Solto: pendura na posicao de repouso.
        ok = safeCallPair(base_ + layout_->rvaBuildHoses, master, pairConnected ? slave : 0) && ok;
    }
    if (!ok) log::error("Backend de memoria: excecao ao recriar as mangueiras.");
}

CouplingResult MemoryBackend::detach(const Joint &joint) {
    lastError_.clear();
    if (!available()) return CouplingResult::Unavailable;
    if (joint.index < 1) return CouplingResult::NotSupported;

    const std::uintptr_t slave = trailerObject(joint.index);
    if (!slave) {
        lastError_ = "reboque da articulacao nao encontrado na memoria";
        return CouplingResult::Unavailable;
    }
    std::uintptr_t master = 0, vt = 0, fn = 0;
    if (!safeReadPtr(slave + layout_->offTrailerMaster, master) || master == 0) {
        lastError_ = "articulacao ja esta solta";
        return CouplingResult::NotSupported;
    }
    safeReadPtr(slave, vt);
    safeReadPtr(vt + layout_->vtSlotDisconnect, fn);
    if (!safeCall(fn, slave)) {
        lastError_ = "excecao ao chamar a rotina de desengate";
        log::error("Backend de memoria: excecao ao desengatar articulacao %d.", joint.index);
        return CouplingResult::Unavailable;
    }
    // Sem isto as mangueiras continuam presas ao reboque solto e esticam quando o caminhao se afasta.
    const std::uintptr_t front = trailerObject(joint.index - 1);
    if (front) {
        const bool had = hasHoses(front);
        hosesWereOn_[joint.index] = had;
        rebuildHoses(front, slave, had, false);
    }
    log::info("Backend de memoria: articulacao %d desengatada (reboque %p).", joint.index,
              reinterpret_cast<void *>(slave));
    return CouplingResult::Requested;
}

CouplingResult MemoryBackend::attach(const Joint &joint) {
    lastError_.clear();
    if (!available()) return CouplingResult::Unavailable;
    if (joint.index < 1) return CouplingResult::NotSupported;

    const std::uintptr_t master = trailerObject(joint.index - 1);
    const std::uintptr_t slave = trailerObject(joint.index);
    if (!master || !slave) {
        lastError_ = "reboques da articulacao nao encontrados na memoria";
        return CouplingResult::Unavailable;
    }
    std::uintptr_t current = 0;
    if (safeReadPtr(slave + layout_->offTrailerMaster, current) && current != 0) {
        lastError_ = "articulacao ja esta engatada";
        return CouplingResult::NotSupported;
    }
    if (maxAttachDistanceM_ > 0.0) {
        double d = 0.0;
        if (!hookDistance(joint.index, master, d)) {
            lastError_ = "nao deu para medir a distancia entre os engates (use distancia_max_engate_m = 0 "
                         "para ignorar)";
            return CouplingResult::NotSupported;
        }
        if (d > maxAttachDistanceM_) {
            char buf[160];
            std::snprintf(buf, sizeof(buf), "engates a %.2f m (maximo %.2f m) - aproxime os reboques", d,
                          maxAttachDistanceM_);
            lastError_ = buf;
            return CouplingResult::NotSupported;
        }
    }
    if (!safeCall(base_ + layout_->rvaReconnectSlave, master)) {
        lastError_ = "excecao ao chamar a rotina de engate";
        log::error("Backend de memoria: excecao ao engatar articulacao %d.", joint.index);
        return CouplingResult::Unavailable;
    }
    rebuildHoses(master, slave, hosesWereOn_[joint.index] || hasHoses(master), true);
    log::info("Backend de memoria: articulacao %d engatada.", joint.index);
    return CouplingResult::Requested;
}

namespace {

const MemoryBackend::Layout kLayouts[] = {
    {
        "1.62.3.3",
        0x3714fb0, // DAT_143714fb0
        0x31b0,    // game -> ator (FUN_1404db5a0 / FUN_1416ec770)
        0x18,      // ator -> caminhao
        0x1580,    // caminhao -> 1o reboque
        0x1058,    // reboque -> reboque de tras
        0x1050,    // reboque -> mestre
        0xea0,     // veiculo -> contagem do array de mangueiras (+0xe90)
        0x310, 0x318,
        0x774710, // FUN_140774710 engatar
        0x775290, // FUN_140775290 desengatar
        0x775050, // FUN_140775050 reconectar reboque de tras
        0x774ef0, // FUN_140774ef0 posicao do engate traseiro
        0x866440, // FUN_140866440 remover mangueiras
        0x865600, // FUN_140865600 criar mangueiras
        {0x48, 0x8b, 0xc4, 0x48, 0x89, 0x58, 0x18, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56},
        {0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83, 0xec, 0x30, 0x48, 0x8b, 0x05, 0x0f, 0xfd, 0xf9},
        {0x40, 0x53, 0x48, 0x83, 0xec, 0x60, 0x48, 0x83, 0xb9, 0x58, 0x10, 0x00, 0x00, 0x00, 0x48, 0x8b},
        {0x4c, 0x8b, 0xdc, 0x49, 0x89, 0x5b, 0x10, 0x49, 0x89, 0x73, 0x18, 0x57, 0x48, 0x81, 0xec, 0xb0},
        {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xec, 0x40, 0x48},
        {0x48, 0x89, 0x4c, 0x24, 0x08, 0x55, 0x53, 0x41, 0x55, 0x41, 0x56, 0x48, 0x8d, 0xac, 0x24, 0x78},
    },
    {nullptr, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, {}, {}, {}, {}, {}, {}},
};

const MemoryBackend::Layout *knownLayouts() { return kLayouts; }

} // namespace

} // namespace tc
