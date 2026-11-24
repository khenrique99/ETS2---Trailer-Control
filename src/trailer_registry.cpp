#include "trailer_registry.h"

#include "config.h"
#include "log.h"

namespace tc {

const char *toString(JointKind k) {
    switch (k) {
    case JointKind::TruckFifthWheel:   return "Quinta roda do cavalo";
    case JointKind::DollyDrawbar:      return "Lanca do dolly";
    case JointKind::DollyFifthWheel:   return "Quinta roda do dolly";
    case JointKind::TrailerFifthWheel: return "Quinta roda bitrem/rodotrem";
    case JointKind::TrailerHitch:      return "Engate de lanca";
    }
    return "?";
}

const char *toString(UnitKind k) {
    switch (k) {
    case UnitKind::Truck:   return "cavalo";
    case UnitKind::Trailer: return "carreta";
    case UnitKind::Dolly:   return "dolly";
    }
    return "?";
}

std::string Unit::displayName() const {
    std::string n = attr("name");
    if (n.empty()) n = attr("id");
    if (n.empty()) n = toString(kind);
    const std::string brand = attr("brand");
    return brand.empty() ? n : brand + " " + n;
}

void TrailerRegistry::setDollyKeywords(std::vector<std::string> keywords) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!keywords.empty()) dollyKeywords_ = std::move(keywords);
}

UnitKind TrailerRegistry::classify(const std::map<std::string, std::string> &attrs) const {
    // Funciona para jogo original e mods: procura palavras-chave no id, nome e
    // tipo de carroceria. Ex.: "scs.dolly.single", "dolly_hct", body "dolly".
    static const char *fields[] = {"id", "name", "body.type", "cargo.accessory.id"};
    for (const char *f : fields) {
        const auto it = attrs.find(f);
        if (it == attrs.end()) continue;
        const std::string v = toLower(it->second);
        for (const auto &kw : dollyKeywords_) {
            if (v.find(kw) != std::string::npos) return UnitKind::Dolly;
        }
    }
    return UnitKind::Trailer;
}

void TrailerRegistry::onTrailerConfig(int idx, std::map<std::string, std::string> attrs) {
    if (idx < 0 || idx >= kMaxTrailers) return;
    std::lock_guard<std::mutex> lock(mutex_);
    Unit &u = trailers_[idx];
    u.index = idx + 1;
    u.present = !attrs.empty() && !attrs["id"].empty();
    u.kind = u.present ? classify(attrs) : UnitKind::Trailer;
    u.attributes = std::move(attrs);
    if (!u.present) u.connected = false;
    ++revision_;

    if (u.present) {
        log::info("Reboque %d detectado: %s [%s] id=%s chain=%s", idx, u.displayName().c_str(),
                  toString(u.kind), u.attr("id").c_str(), u.attr("chain.type").c_str());
    }
}

void TrailerRegistry::onTrailerConnected(int idx, bool connected) {
    if (idx < 0 || idx >= kMaxTrailers) return;
    std::lock_guard<std::mutex> lock(mutex_);
    if (trailers_[idx].connected != connected) {
        trailers_[idx].connected = connected;
        ++revision_;
    }
}

void TrailerRegistry::onTruckConfig(std::map<std::string, std::string> attrs) {
    std::lock_guard<std::mutex> lock(mutex_);
    truck_.index = 0;
    truck_.kind = UnitKind::Truck;
    truck_.present = true;
    truck_.connected = true;
    truck_.attributes = std::move(attrs);
    ++revision_;
}

unsigned TrailerRegistry::revision() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return revision_;
}

std::string TrailerRegistry::describeCombination(const std::string &chain,
                                                 const std::vector<Unit> &units) {
    int trailers = 0, dollies = 0;
    for (const auto &u : units) {
        if (u.kind == UnitKind::Trailer) ++trailers;
        if (u.kind == UnitKind::Dolly) ++dollies;
    }
    const std::string c = toLower(chain);
    if (trailers == 0) return "Sem reboque";
    if (trailers == 1 && dollies == 0) return "Carreta simples";
    if (c == "b_double" || (trailers == 2 && dollies == 0)) return "Bitrem (B-double)";
    if (trailers == 2 && dollies >= 1) return "Rodotrem / Double com dolly";
    if (trailers >= 3) return dollies > 0 ? "Treminhao / Road train com dolly" : "Tritrem / Rodotres";
    if (c == "hct") return "HCT";
    return "Composicao personalizada";
}

CombinationSnapshot TrailerRegistry::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    CombinationSnapshot s;

    Unit truck = truck_;
    truck.present = true;
    truck.connected = true;
    s.units.push_back(truck);

    // Unidades presentes em sequencia (o SDK preenche trailer.0, .1, ... em ordem).
    for (const auto &t : trailers_) {
        if (!t.present) break;
        s.units.push_back(t);
        if (s.chainType.empty()) s.chainType = t.attr("chain.type");
    }

    for (size_t i = 0; i + 1 < s.units.size(); ++i) {
        const Unit &front = s.units[i];
        const Unit &rear = s.units[i + 1];
        Joint j;
        j.index = static_cast<int>(i);
        j.front = front.index;
        j.rear = rear.index;
        j.coupled = rear.connected;

        if (front.kind == UnitKind::Truck)        j.kind = JointKind::TruckFifthWheel;
        else if (rear.kind == UnitKind::Dolly)    j.kind = JointKind::DollyDrawbar;
        else if (front.kind == UnitKind::Dolly)   j.kind = JointKind::DollyFifthWheel;
        else if (toLower(s.chainType) == "hct")   j.kind = JointKind::TrailerHitch;
        else                                      j.kind = JointKind::TrailerFifthWheel;

        s.joints.push_back(j);
    }

    s.combinationName = describeCombination(s.chainType, s.units);
    return s;
}

} // namespace tc
