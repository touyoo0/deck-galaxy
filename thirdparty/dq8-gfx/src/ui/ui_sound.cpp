#include "ui/ui_sound.h"

#include <SDL3/SDL.h>

#include <cmath>
#include <cstdlib>
#include <vector>

namespace dq8::ui::sound {
namespace {

constexpr int kRate = 48000;
constexpr float kPi = 3.14159265358979f;

SDL_AudioStream *gStream = nullptr;
bool gTried = false;
float gVolume = 1.0f;

bool muted() {
    static const bool mute = [] {
        const char *v = std::getenv("RG_MUTE");
        return v && *v && *v != '0';
    }();
    return mute;
}

// One soft note: a sine and a quiet octave above, a short rise (no click) and
// an exponential fall, added into `out` from `start` seconds.
void note(std::vector<float> &out, float start, float freq, float length, float level, float rise = 0.02f) {
    const size_t from = static_cast<size_t>(start * kRate);
    const size_t count = static_cast<size_t>(length * kRate);
    if (out.size() < from + count)
        out.resize(from + count, 0.0f);
    for (size_t i = 0; i < count; ++i) {
        const float t = static_cast<float>(i) / kRate;
        const float attack = std::min(1.0f, t / rise);
        const float fall = std::exp(-t * (5.0f / length));
        const float tone = std::sin(2.0f * kPi * freq * t) + 0.05f * std::sin(2.0f * kPi * freq * 2.0f * t);
        out[from + i] += tone * attack * fall * level;
    }
}

std::vector<float> render(Cue cue) {
    std::vector<float> out;
    switch (cue) {
    case Cue::Move:
        note(out, 0.0f, 523.25f, 0.14f, 0.05f);
        break;
    case Cue::Select:
        note(out, 0.0f, 440.0f, 0.16f, 0.055f);
        note(out, 0.06f, 587.33f, 0.18f, 0.04f);
        break;
    case Cue::Launch: {
        // One warm chord (G, D, G) that swells in slowly and fades away, no
        // melody.
        note(out, 0.0f, 196.0f, 1.5f, 0.05f, 0.25f);
        note(out, 0.03f, 293.66f, 1.5f, 0.035f, 0.25f);
        note(out, 0.06f, 392.0f, 1.5f, 0.03f, 0.25f);
        break;
    }
    }
    return out;
}

bool open() {
    if (gStream)
        return true;
    if (gTried)
        return false;
    gTried = true;
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
        return false;
    SDL_AudioSpec spec{};
    spec.format = SDL_AUDIO_F32;
    spec.channels = 1;
    spec.freq = kRate;
    gStream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (!gStream)
        return false;
    SDL_ResumeAudioStreamDevice(gStream);
    return true;
}

} // namespace

void setVolume(float volume) { gVolume = volume < 0.0f ? 0.0f : volume > 1.0f ? 1.0f : volume; }

void play(Cue cue) {
    if (muted() || gVolume <= 0.0f || !open())
        return;
    std::vector<float> samples = render(cue);
    for (float &s : samples)
        s *= gVolume;
    SDL_PutAudioStreamData(gStream, samples.data(), static_cast<int>(samples.size() * sizeof(float)));
}

void shutdown() {
    if (gStream) {
        SDL_DestroyAudioStream(gStream);
        gStream = nullptr;
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }
}

} // namespace dq8::ui::sound
