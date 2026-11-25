#include "blinker_sound.h"

#include <windows.h>
#include <mmsystem.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace tc {

namespace {
constexpr int kSampleRate = 44100;
constexpr double kBeepSeconds = 0.35; // metade do ciclo do pisca: apita enquanto a luz esta acesa
constexpr double kBeepHz = 2800.0;    // tom agudo de buzzer
constexpr double kFadeSeconds = 0.004;
constexpr double kPi = 3.14159265358979323846;

void put32(std::vector<std::uint8_t> &v, std::uint32_t x) {
    for (int i = 0; i < 4; ++i) v.push_back(static_cast<std::uint8_t>((x >> (8 * i)) & 0xFF));
}
void put16(std::vector<std::uint8_t> &v, std::uint16_t x) {
    v.push_back(static_cast<std::uint8_t>(x & 0xFF));
    v.push_back(static_cast<std::uint8_t>((x >> 8) & 0xFF));
}
void putTag(std::vector<std::uint8_t> &v, const char *tag) { v.insert(v.end(), tag, tag + 4); }
} // namespace

BlinkerSoundAction decideBlinkerSound(bool wasLampOn, bool lampOn, bool enabled, bool paused, bool trailerCoupled) {
    const bool active = enabled && !paused && trailerCoupled;
    if (!active) return wasLampOn ? BlinkerSoundAction::Stop : BlinkerSoundAction::None;
    if (lampOn && !wasLampOn) return BlinkerSoundAction::Play;
    if (!lampOn && wasLampOn) return BlinkerSoundAction::Stop;
    return BlinkerSoundAction::None;
}

std::vector<std::uint8_t> makeBlinkerBeepWav(int volumePercent) {
    const double gain = 0.4 * std::clamp(volumePercent, 0, 100) / 100.0;
    const int n = static_cast<int>(kBeepSeconds * kSampleRate);
    const int fade = static_cast<int>(kFadeSeconds * kSampleRate);

    std::vector<std::uint8_t> w;
    w.reserve(44 + static_cast<size_t>(n) * 2);
    putTag(w, "RIFF");
    put32(w, static_cast<std::uint32_t>(36 + n * 2));
    putTag(w, "WAVE");
    putTag(w, "fmt ");
    put32(w, 16);
    put16(w, 1); // PCM
    put16(w, 1); // mono
    put32(w, kSampleRate);
    put32(w, kSampleRate * 2);
    put16(w, 2);
    put16(w, 16);
    putTag(w, "data");
    put32(w, static_cast<std::uint32_t>(n * 2));

    for (int i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / kSampleRate;
        // Onda quase quadrada (fundamental + harmonicos impares): soa como buzzer.
        double x = 0.6 * std::sin(2 * kPi * kBeepHz * t) + 0.25 * std::sin(6 * kPi * kBeepHz * t) +
                   0.15 * std::sin(10 * kPi * kBeepHz * t);
        double env = 1.0;
        if (i < fade) env = static_cast<double>(i) / fade;
        else if (i > n - fade) env = static_cast<double>(n - i) / fade;
        const double s = std::clamp(gain * env * x, -1.0, 1.0);
        put16(w, static_cast<std::uint16_t>(static_cast<std::int16_t>(s * 32000.0)));
    }
    return w;
}

BlinkerSound &BlinkerSound::instance() {
    static BlinkerSound inst;
    return inst;
}

void BlinkerSound::stop() {
    if (playing_) PlaySoundW(nullptr, nullptr, 0);
    playing_ = false;
}

void BlinkerSound::configure(bool enabled, int volumePercent) {
    stop();
    enabled_ = enabled;
    volume_ = std::clamp(volumePercent, 0, 100);
    wav_ = makeBlinkerBeepWav(volume_);
    wasLampOn_ = false;
}

void BlinkerSound::update(bool paused, bool trailerCoupled) {
    const bool lampOn = left_.load() || right_.load();
    switch (decideBlinkerSound(wasLampOn_, lampOn, enabled_, paused, trailerCoupled)) {
    case BlinkerSoundAction::Play:
        if (!wav_.empty() && volume_ > 0) {
            playing_ = PlaySoundW(reinterpret_cast<LPCWSTR>(wav_.data()), nullptr,
                                  SND_MEMORY | SND_ASYNC | SND_NODEFAULT) != FALSE;
        }
        break;
    case BlinkerSoundAction::Stop: stop(); break;
    case BlinkerSoundAction::None: break;
    }
    wasLampOn_ = lampOn && enabled_ && !paused && trailerCoupled;
}

} // namespace tc
