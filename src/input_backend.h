#pragma once
#include "coupling_backend.h"

#include <scssdk_input.h>

#include <atomic>
#include <string>

namespace tc {

// Backend oficial: registra um dispositivo de entrada virtual pelo SCS Input SDK
// e "aperta" o controle de Engatar/Desengatar reboque do proprio jogo.
//
// Limite do jogo: o controle nativo so age na articulacao cavalo <-> 1a carreta
// (desengata/engata o conjunto inteiro). Articulacoes internas (dolly, bitrem)
// nao sao expostas pelo SDK -> veja MemoryBackend.
class InputBackend final : public ICouplingBackend {
public:
    static InputBackend &instance();

    // Chamado de scs_input_init. mode: "generic" ou "semantical".
    bool registerDevice(const scs_input_init_params_v100_t &params, const std::string &mode,
                        const std::string &semanticalControl);

    const char *name() const override { return "Entrada (SDK oficial)"; }
    bool available() const override { return registered_; }
    bool supports(const Joint &joint) const override { return joint.index == 0; }
    CouplingResult detach(const Joint &joint) override;
    CouplingResult attach(const Joint &joint) override;

    bool deviceActive() const { return active_; }

private:
    CouplingResult pulse(const Joint &joint, const char *what);

    static SCSAPI_VOID onActive(const scs_u8_t active, const scs_context_t ctx);
    static SCSAPI_RESULT onEvent(scs_input_event_t *const event, const scs_u32_t flags,
                                 const scs_context_t ctx);

    enum class Phase { Idle, Pressed };

    std::atomic<bool> registered_{false};
    std::atomic<bool> active_{false};
    std::atomic<int> pendingPulses_{0};

    // Estado usado apenas na thread do jogo
    Phase phase_ = Phase::Idle;
    int framesHeld_ = 0;
    bool emittedThisFrame_ = false;

    std::string semanticalName_;
};

} // namespace tc
