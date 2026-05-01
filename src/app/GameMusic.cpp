#include "app/GameMusic.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <span>

namespace valcraft {

namespace {

constexpr int kPreferredSampleRate = 48000;
constexpr std::uint16_t kPreferredChannels = 2;
constexpr std::uint16_t kPreferredBufferFrames = 2048;
constexpr float kSfxPi = 3.14159265358979323846F;
constexpr float kSfxTwoPi = 2.0F * kSfxPi;

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

    for (auto& voice : sfx_voices_) {
        voice.active = false;
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

void GameMusic::play_sfx(GameSfxKind kind, float volume) noexcept {
    if (device_id_ == 0 || volume <= 0.0F) {
        return;
    }

    SDL_LockAudioDevice(device_id_);

    auto* target = static_cast<SfxVoice*>(nullptr);
    auto oldest_age = -1.0F;
    for (auto& voice : sfx_voices_) {
        if (!voice.active) {
            target = &voice;
            break;
        }
        if (voice.age > oldest_age) {
            oldest_age = voice.age;
            target = &voice;
        }
    }

    if (target != nullptr) {
        sfx_seed_ = sfx_seed_ * 1664525U + 1013904223U;
        target->kind = kind;
        target->age = 0.0F;
        target->duration = sfx_duration(kind);
        target->volume = std::clamp(volume, 0.0F, 1.0F);
        target->phase = 0.0F;
        target->seed = sfx_seed_;
        target->active = true;
    }

    SDL_UnlockAudioDevice(device_id_);
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
    mix_sfx(output, sample_count / channel_count, channel_count);
}

void GameMusic::mix_sfx(float* output, std::size_t frame_count, std::size_t channel_count) noexcept {
    if (output == nullptr || frame_count == 0U || channel_count == 0U) {
        return;
    }

    const auto sample_rate = static_cast<float>(std::max(obtained_spec_.freq, 1));
    for (std::size_t frame = 0U; frame < frame_count; ++frame) {
        auto mixed = 0.0F;
        for (auto& voice : sfx_voices_) {
            if (voice.active) {
                mixed += render_sfx_sample(voice, sample_rate);
            }
        }

        if (std::abs(mixed) <= 1.0e-5F) {
            continue;
        }

        for (std::size_t channel = 0U; channel < channel_count; ++channel) {
            const auto sample_index = frame * channel_count + channel;
            output[sample_index] = std::clamp(output[sample_index] + mixed, -1.0F, 1.0F);
        }
    }
}

auto GameMusic::sfx_duration(GameSfxKind kind) noexcept -> float {
    switch (kind) {
    case GameSfxKind::SwordSwing:
        return 0.18F;
    case GameSfxKind::CreatureHit:
        return 0.22F;
    case GameSfxKind::CreatureDeath:
        return 0.72F;
    case GameSfxKind::CreatureAttack:
        return 0.30F;
    }

    return 0.10F;
}

auto GameMusic::next_noise_unit(std::uint32_t& seed) noexcept -> float {
    seed = seed * 1664525U + 1013904223U;
    const auto value = static_cast<float>((seed >> 8U) & 0x00FFFFFFU) / static_cast<float>(0x01000000U);
    return value * 2.0F - 1.0F;
}

auto GameMusic::render_sfx_sample(SfxVoice& voice, float sample_rate) noexcept -> float {
    if (!voice.active || voice.duration <= 1.0e-4F || sample_rate <= 1.0F) {
        voice.active = false;
        return 0.0F;
    }

    const auto t = std::clamp(voice.age / voice.duration, 0.0F, 1.0F);
    const auto decay = 1.0F - t;
    const auto noise = next_noise_unit(voice.seed);
    auto frequency = 120.0F;
    auto sample = 0.0F;

    switch (voice.kind) {
    case GameSfxKind::SwordSwing: {
        const auto envelope = std::sin(t * kSfxPi) * decay;
        frequency = 310.0F - 190.0F * t;
        voice.phase += kSfxTwoPi * frequency / sample_rate;
        sample = (std::sin(voice.phase) * 0.28F + noise * 0.16F) * envelope * 0.80F;
        break;
    }
    case GameSfxKind::CreatureHit: {
        const auto envelope = decay * decay;
        frequency = 118.0F - 36.0F * t;
        voice.phase += kSfxTwoPi * frequency / sample_rate;
        sample = (std::sin(voice.phase) * 0.42F + std::sin(voice.phase * 2.17F) * 0.18F + noise * 0.22F) * envelope;
        break;
    }
    case GameSfxKind::CreatureDeath: {
        const auto envelope = decay * decay * (0.75F + 0.25F * std::sin(t * kSfxPi));
        frequency = 150.0F - 86.0F * t;
        voice.phase += kSfxTwoPi * frequency / sample_rate;
        sample = (std::sin(voice.phase) * 0.46F + std::sin(voice.phase * 0.51F) * 0.18F + noise * 0.10F) * envelope;
        break;
    }
    case GameSfxKind::CreatureAttack: {
        const auto envelope = decay * (0.65F + 0.35F * std::sin(t * kSfxPi));
        frequency = 86.0F + 26.0F * std::sin(t * kSfxTwoPi);
        voice.phase += kSfxTwoPi * frequency / sample_rate;
        sample = (std::sin(voice.phase) * 0.38F + noise * 0.18F) * envelope * 0.82F;
        break;
    }
    }

    if (voice.phase > kSfxTwoPi) {
        voice.phase = std::fmod(voice.phase, kSfxTwoPi);
    }

    voice.age += 1.0F / sample_rate;
    if (voice.age >= voice.duration) {
        voice.active = false;
    }

    return sample * voice.volume;
}

} // namespace valcraft
