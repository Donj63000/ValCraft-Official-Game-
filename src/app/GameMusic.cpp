#include "app/GameMusic.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <span>

namespace valcraft {

namespace {

constexpr int kPreferredSampleRate = 48000;
constexpr std::uint16_t kPreferredChannels = 2;
constexpr std::uint16_t kPreferredBufferFrames = 2048;

} // namespace

auto GameMusic::initialize() -> bool {
    if (device_id_ != 0) {
        return true;
    }

    if (SDL_WasInit(SDL_INIT_AUDIO) == 0U) {
        if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
            return false;
        }
        owns_audio_subsystem_ = true;
    }

    SDL_AudioSpec desired_spec {};
    desired_spec.freq = kPreferredSampleRate;
    desired_spec.format = AUDIO_F32SYS;
    desired_spec.channels = kPreferredChannels;
    desired_spec.samples = kPreferredBufferFrames;
    desired_spec.callback = &GameMusic::audio_callback;
    desired_spec.userdata = this;

    std::memset(&obtained_spec_, 0, sizeof(obtained_spec_));
    device_id_ = SDL_OpenAudioDevice(
        nullptr,
        0,
        &desired_spec,
        &obtained_spec_,
        SDL_AUDIO_ALLOW_FREQUENCY_CHANGE | SDL_AUDIO_ALLOW_SAMPLES_CHANGE);
    if (device_id_ == 0) {
        shutdown();
        return false;
    }

    composer_ = ProceduralMusicComposer(obtained_spec_.freq);
    SDL_PauseAudioDevice(device_id_, 0);
    return true;
}

void GameMusic::shutdown() noexcept {
    if (device_id_ != 0) {
        SDL_PauseAudioDevice(device_id_, 1);
        SDL_CloseAudioDevice(device_id_);
        device_id_ = 0;
    }

    if (owns_audio_subsystem_ && SDL_WasInit(SDL_INIT_AUDIO) != 0U) {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }
    owns_audio_subsystem_ = false;
}

void GameMusic::sync_environment(const EnvironmentState& environment,
                                 const CreatureCycleState& cycle,
                                 bool has_active_session,
                                 bool front_end_visible) noexcept {
    if (device_id_ == 0) {
        return;
    }

    SDL_LockAudioDevice(device_id_);
    composer_.set_environment(environment, cycle, has_active_session, front_end_visible);
    SDL_UnlockAudioDevice(device_id_);
}

void GameMusic::pump() {
    // Je garde cette methode pour ne pas toucher a la boucle de jeu, mais le
    // rendu audio vit maintenant dans le callback SDL et reste fluide en cas de frame lente.
}

auto GameMusic::available() const noexcept -> bool {
    return device_id_ != 0;
}

void GameMusic::audio_callback(void* userdata, Uint8* stream, int len) noexcept {
    if (stream == nullptr || len <= 0) {
        return;
    }

    std::memset(stream, 0, static_cast<std::size_t>(len));
    auto* music = static_cast<GameMusic*>(userdata);
    if (music == nullptr) {
        return;
    }

    music->render_callback_stream(stream, len);
}

void GameMusic::render_callback_stream(Uint8* stream, int len) noexcept {
    if (stream == nullptr || len <= 0 || obtained_spec_.channels == 0) {
        return;
    }

    auto sample_count = static_cast<std::size_t>(len) / sizeof(float);
    const auto channel_count = static_cast<std::size_t>(obtained_spec_.channels);
    sample_count -= sample_count % channel_count;
    if (sample_count == 0U) {
        return;
    }

    auto* output = reinterpret_cast<float*>(stream);
    composer_.render_interleaved(std::span<float> {output, sample_count});
}

} // namespace valcraft
