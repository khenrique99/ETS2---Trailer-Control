#pragma once
#include <array>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace tc {

// O SDK expoe ate 10 reboques por composicao (trailer.0 .. trailer.9).
constexpr int kMaxTrailers = 10;

enum class UnitKind { Truck, Trailer, Dolly };

// Tipo da articulacao entre duas unidades da composicao.
enum class JointKind {
    TruckFifthWheel,   // cavalo -> 1a carreta (quinta roda do cavalo)
    DollyDrawbar,      // carreta -> dolly (lanca / cambao do dolly)
    DollyFifthWheel,   // dolly -> carreta (quinta roda do dolly)
    TrailerFifthWheel, // carreta -> carreta (bitrem / rodotrem / B-train)
    TrailerHitch,      // carreta -> reboque por lanca (romeu e julieta, HCT etc.)
};

struct Unit {
    int index = 0;            // 0 = cavalo, 1..N = trailer.(N-1)
    UnitKind kind = UnitKind::Truck;
    bool present = false;     // existe configuracao para essa unidade
    bool connected = false;   // canal trailer.N.connected
    std::map<std::string, std::string> attributes; // todos os atributos de config do SDK

    std::string attr(const std::string &k) const {
        const auto it = attributes.find(k);
        return it == attributes.end() ? std::string() : it->second;
    }
    std::string displayName() const;
};

struct Joint {
    int index = 0;        // 0 = cavalo/1a carreta, 1 = 1a/2a carreta, ...
    int front = 0;        // indice da unidade da frente
    int rear = 1;         // indice da unidade de tras
    JointKind kind = JointKind::TruckFifthWheel;
    bool coupled = false;
};

struct CombinationSnapshot {
    std::string chainType;       // "single", "double", "b_double", "hct", ... (do SDK)
    std::string combinationName; // nome amigavel deduzido
    std::vector<Unit> units;     // [0] = cavalo
    std::vector<Joint> joints;   // articulacoes entre unidades presentes
};

const char *toString(JointKind k);
const char *toString(UnitKind k);

// Mantem o estado dos reboques vindo do SDK de telemetria.
// Todas as escritas ocorrem na thread do jogo; leituras podem vir de outras threads.
class TrailerRegistry {
public:
    void setDollyKeywords(std::vector<std::string> keywords);

    // Chamado no evento de configuracao "trailer.N" (ou "trailer" legado = 0).
    void onTrailerConfig(int trailerIdx, std::map<std::string, std::string> attrs);
    void onTrailerConnected(int trailerIdx, bool connected);
    void onTruckConfig(std::map<std::string, std::string> attrs);

    CombinationSnapshot snapshot() const;

    // Incrementa a cada mudanca; usado para saber quando regravar o status.
    unsigned revision() const;

private:
    UnitKind classify(const std::map<std::string, std::string> &attrs) const;
    static std::string describeCombination(const std::string &chain, const std::vector<Unit> &units);

    mutable std::mutex mutex_;
    std::vector<std::string> dollyKeywords_{"dolly"};
    Unit truck_;
    std::array<Unit, kMaxTrailers> trailers_{};
    unsigned revision_ = 0;
};

} // namespace tc
