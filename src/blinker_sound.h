#pragma once
#include <atomic>
#include <cstdint>
#include <vector>

namespace tc {

enum class BlinkerSoundAction { None, Play, Stop };

// Decide o que fazer com o som a cada frame. Puro (sem Windows) para poder ser testado.
// `lampOn` e o estado da LAMPADA da seta (pisca), nao o da alavanca: cada acendida toca um apito.
BlinkerSoundAction decideBlinkerSound(bool wasLampOn, bool lampOn, bool enabled, bool paused, bool trailerCoupled);

// WAV (PCM 16 bits, mono) de um apito de alarme externo de seta; volume em 0..100.
std::vector<std::uint8_t> makeBlinkerBeepWav(int volumePercent);

// Alarme sonoro da seta, como o apito externo de algumas carretas. Toca pelo Windows
// (nao passa pelo audio do ETS2: nao segue o volume do jogo e nao e 3D).
class BlinkerSound {
public:
    static BlinkerSound &instance();

    // Thread do jogo (callbacks dos canais truck.light.lblinker / rblinker).
    void setLeftLamp(bool on) { left_ = on; }
    void setRightLamp(bool on) { right_ = on; }

    void configure(bool enabled, int volumePercent);
    // Thread do jogo, uma vez por frame.
    void update(bool paused, bool trailerCoupled);

    bool enabled() const { return enabled_; }
    int volume() const { return volume_; }

private:
    void stop();

    std::atomic<bool> left_{false};
    std::atomic<bool> right_{false};
    bool enabled_ = false;
    int volume_ = 60;
    bool wasLampOn_ = false;
    bool playing_ = false;
    std::vector<std::uint8_t> wav_; // precisa viver enquanto toca (SND_MEMORY assincrono)
};

} // namespace tc
