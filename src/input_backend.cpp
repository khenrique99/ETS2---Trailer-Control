#include "input_backend.h"

#include "log.h"

namespace tc {

namespace {
// Quantos frames o "botao" fica pressionado. O jogo precisa ver pelo menos
// um frame com o botao em 1 e depois em 0 para disparar a acao.
constexpr int kHoldFrames = 4;

scs_input_device_input_t g_inputs[1];
scs_input_device_t g_device{};
} // namespace

const char *toString(CouplingResult r) {
    switch (r) {
    case CouplingResult::Requested:    return "enviado";
    case CouplingResult::NotSupported: return "nao suportado";
    case CouplingResult::Unavailable:  return "indisponivel";
    case CouplingResult::Busy:         return "ocupado";
    }
    return "?";
}

InputBackend &InputBackend::instance() {
    static InputBackend inst;
    return inst;
}

bool InputBackend::registerDevice(const scs_input_init_params_v100_t &params,
                                  const std::string &mode, const std::string &semanticalControl) {
    const bool semantical = (mode == "semantical");
    semanticalName_ = semanticalControl.empty() ? "trailer" : semanticalControl;

    if (semantical) {
        // Dispositivo semantico: o nome da entrada E o nome do controle no controls.sii,
        // entao funciona sem o jogador precisar mapear nada.
        g_inputs[0].name = semanticalName_.c_str();
        g_inputs[0].display_name = "Couple Uncouple Trailer";
    } else {
        // Dispositivo generico: aparece em Opcoes > Controles como
        // "Trailer Control" e o jogador associa o botao a "Engatar/Desengatar reboque".
        g_inputs[0].name = "trailer_toggle";
        g_inputs[0].display_name = "Couple Uncouple Trailer";
    }
    g_inputs[0].value_type = SCS_VALUE_TYPE_bool;

    g_device.name = "trailer_control";
    g_device.display_name = "Trailer Control";
    g_device.type = semantical ? SCS_INPUT_DEVICE_TYPE_semantical : SCS_INPUT_DEVICE_TYPE_generic;
    g_device.input_count = 1;
    g_device.inputs = g_inputs;
    g_device.callback_context = this;
    g_device.input_active_callback = &InputBackend::onActive;
    g_device.input_event_callback = &InputBackend::onEvent;

    if (params.register_device(&g_device) != SCS_RESULT_ok) {
        log::error("Falha ao registrar o dispositivo de entrada virtual.");
        return false;
    }
    registered_ = true;
    log::info("Dispositivo de entrada registrado (modo %s%s%s).", semantical ? "semantical" : "generic",
              semantical ? ", controle=" : "", semantical ? semanticalName_.c_str() : "");
    return true;
}

CouplingResult InputBackend::pulse(const Joint &joint, const char *what) {
    if (!registered_) return CouplingResult::Unavailable;
    if (!supports(joint)) return CouplingResult::NotSupported;
    if (pendingPulses_ > 0 || phase_ != Phase::Idle) return CouplingResult::Busy;
    if (!active_) {
        log::warn("Dispositivo virtual inativo: no modo 'generic' associe "
                  "'Trailer Control > Engatar/Desengatar reboque' nos controles do jogo.");
    }
    pendingPulses_ = 1;
    log::info("%s articulacao %d via controle nativo.", what, joint.index);
    return CouplingResult::Requested;
}

CouplingResult InputBackend::detach(const Joint &joint) { return pulse(joint, "Desengatando"); }
CouplingResult InputBackend::attach(const Joint &joint) { return pulse(joint, "Engatando"); }

SCSAPI_VOID InputBackend::onActive(const scs_u8_t active, const scs_context_t ctx) {
    auto *self = static_cast<InputBackend *>(ctx);
    self->active_ = active != 0;
    if (!active) {
        // Ao desativar, o jogo considera todas as entradas soltas.
        self->phase_ = Phase::Idle;
        self->framesHeld_ = 0;
    }
}

// O jogo chama este callback repetidamente a cada frame ate recebermos SCS_RESULT_not_found.
// Emitimos no maximo um evento por frame para garantir a sequencia 1 -> 0.
SCSAPI_RESULT InputBackend::onEvent(scs_input_event_t *const event, const scs_u32_t flags,
                                    const scs_context_t ctx) {
    auto *self = static_cast<InputBackend *>(ctx);

    if (flags & SCS_INPUT_EVENT_CALLBACK_FLAG_first_in_frame) {
        self->emittedThisFrame_ = false;
        if (self->phase_ == Phase::Pressed) ++self->framesHeld_;
    }
    if (self->emittedThisFrame_) return SCS_RESULT_not_found;

    if (self->phase_ == Phase::Idle && self->pendingPulses_ > 0) {
        --self->pendingPulses_;
        self->phase_ = Phase::Pressed;
        self->framesHeld_ = 0;
        event->input_index = 0;
        event->value_bool.value = 1;
        self->emittedThisFrame_ = true;
        return SCS_RESULT_ok;
    }

    if (self->phase_ == Phase::Pressed && self->framesHeld_ >= kHoldFrames) {
        self->phase_ = Phase::Idle;
        event->input_index = 0;
        event->value_bool.value = 0;
        self->emittedThisFrame_ = true;
        return SCS_RESULT_ok;
    }

    return SCS_RESULT_not_found;
}

} // namespace tc
