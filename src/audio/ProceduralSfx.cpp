#include "audio/ProceduralSfx.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace valcraft {

namespace {

constexpr float kPi = 3.14159265358979323846F;
constexpr float kTwoPi = 2.0F * kPi;

[[nodiscard]] auto finite_unit(float value, float fallback = 0.0F) noexcept -> float {
    return std::isfinite(value) ? std::clamp(value, 0.0F, 1.0F) : fallback;
}

[[nodiscard]] auto finite_pan(float value) noexcept -> float {
    return std::isfinite(value) ? std::clamp(value, -1.0F, 1.0F) : 0.0F;
}

} // namespace

ProceduralSfxMixer::ProceduralSfxMixer(int sample_rate) noexcept {
    set_sample_rate(sample_rate);
}

void ProceduralSfxMixer::set_sample_rate(int sample_rate) noexcept {
    sample_rate_ = std::clamp(sample_rate, 8'000, 192'000);
}

void ProceduralSfxMixer::reset() noexcept {
    for (auto& voice : voices_) {
        voice = {};
    }
    next_seed_ = 0x9E3779B9U;
}

void ProceduralSfxMixer::play(const ProceduralSfxRequest& request) noexcept {
    const auto volume = finite_unit(request.volume) * finite_unit(request.attenuation, 1.0F);
    if (volume <= 0.0F) {
        return;
    }

    auto* selected = static_cast<Voice*>(nullptr);
    auto oldest_ratio = -1.0F;
    for (auto& voice : voices_) {
        if (!voice.active) {
            selected = &voice;
            break;
        }
        const auto age_ratio =
            voice.duration > 1.0e-5F
                ? voice.age / voice.duration
                : std::numeric_limits<float>::infinity();
        if (age_ratio > oldest_ratio) {
            oldest_ratio = age_ratio;
            selected = &voice;
        }
    }
    if (selected == nullptr) {
        return;
    }

    next_seed_ = next_seed_ * 1664525U + 1013904223U;
    auto seed = request.seed != 0U ? request.seed : next_seed_;
    if (seed == 0U) {
        seed = 1U;
    }

    *selected = {};
    selected->kind = request.kind;
    selected->duration = effect_duration(request.kind);
    selected->volume = volume;
    selected->pan = finite_pan(request.pan);
    selected->seed = seed;
    // Je decale legerement la phase grave par graine sans changer la duree.
    selected->phase =
        static_cast<float>((seed >> 9U) & 0x3FFU) /
        1024.0F * kTwoPi;
    selected->secondary_phase =
        static_cast<float>((seed >> 19U) & 0x3FFU) /
        1024.0F * kTwoPi;
    selected->active = true;
}

void ProceduralSfxMixer::mix_interleaved(std::span<float> output,
                                         std::size_t channel_count) noexcept {
    if (output.empty() || channel_count == 0U) {
        return;
    }
    const auto frame_count = output.size() / channel_count;
    if (frame_count == 0U) {
        return;
    }

    for (std::size_t frame = 0U; frame < frame_count; ++frame) {
        auto left = 0.0F;
        auto right = 0.0F;
        auto rendered_voice = false;
        for (auto& voice : voices_) {
            if (!voice.active) {
                continue;
            }

            const auto sample = render_voice_sample(voice);
            const auto angle = (voice.pan + 1.0F) * (kPi * 0.25F);
            left += sample * std::cos(angle);
            right += sample * std::sin(angle);
            rendered_voice = true;
        }
        if (!rendered_voice) {
            continue;
        }

        const auto base = frame * channel_count;
        if (channel_count == 1U) {
            output[base] = soft_limit(output[base] + (left + right) * 0.70710678F);
            continue;
        }

        output[base] = soft_limit(output[base] + left);
        output[base + 1U] = soft_limit(output[base + 1U] + right);
        const auto center = (left + right) * 0.70710678F;
        for (std::size_t channel = 2U; channel < channel_count; ++channel) {
            output[base + channel] = soft_limit(output[base + channel] + center);
        }
    }
}

auto ProceduralSfxMixer::active_voice_count() const noexcept -> std::size_t {
    return static_cast<std::size_t>(
        std::count_if(
            std::begin(voices_),
            std::end(voices_),
            [](const Voice& voice) noexcept {
                return voice.active;
            }));
}

auto ProceduralSfxMixer::effect_duration(GameSfxKind kind) noexcept -> float {
    switch (kind) {
    case GameSfxKind::SwordSwing:
        return 0.18F;
    case GameSfxKind::CreatureHit:
        return 0.22F;
    case GameSfxKind::CreatureDeath:
        return 0.72F;
    case GameSfxKind::CreatureAttack:
        return 0.30F;
    case GameSfxKind::MusketShot:
        return 1.08F;
    }
    return 0.10F;
}

auto ProceduralSfxMixer::next_noise_unit(std::uint32_t& seed) noexcept -> float {
    seed = seed * 1664525U + 1013904223U;
    const auto value =
        static_cast<float>((seed >> 8U) & 0x00FFFFFFU) /
        static_cast<float>(0x01000000U);
    return value * 2.0F - 1.0F;
}

auto ProceduralSfxMixer::render_voice_sample(Voice& voice) const noexcept -> float {
    if (!voice.active || voice.duration <= 1.0e-5F || sample_rate_ <= 1) {
        voice.active = false;
        return 0.0F;
    }

    const auto sample_rate = static_cast<float>(sample_rate_);
    const auto normalized_age = std::clamp(voice.age / voice.duration, 0.0F, 1.0F);
    const auto decay = 1.0F - normalized_age;
    const auto noise = next_noise_unit(voice.seed);
    auto sample = 0.0F;

    switch (voice.kind) {
    case GameSfxKind::SwordSwing: {
        const auto envelope = std::sin(normalized_age * kPi) * decay;
        const auto frequency = 310.0F - 190.0F * normalized_age;
        voice.phase += kTwoPi * frequency / sample_rate;
        sample =
            (std::sin(voice.phase) * 0.28F + noise * 0.16F) *
            envelope * 0.80F;
        break;
    }
    case GameSfxKind::CreatureHit: {
        const auto envelope = decay * decay;
        const auto frequency = 118.0F - 36.0F * normalized_age;
        voice.phase += kTwoPi * frequency / sample_rate;
        sample =
            (std::sin(voice.phase) * 0.42F +
             std::sin(voice.phase * 2.17F) * 0.18F +
             noise * 0.22F) *
            envelope;
        break;
    }
    case GameSfxKind::CreatureDeath: {
        const auto envelope =
            decay * decay *
            (0.75F + 0.25F * std::sin(normalized_age * kPi));
        const auto frequency = 150.0F - 86.0F * normalized_age;
        voice.phase += kTwoPi * frequency / sample_rate;
        sample =
            (std::sin(voice.phase) * 0.46F +
             std::sin(voice.phase * 0.51F) * 0.18F +
             noise * 0.10F) *
            envelope;
        break;
    }
    case GameSfxKind::CreatureAttack: {
        const auto envelope =
            decay *
            (0.65F + 0.35F * std::sin(normalized_age * kPi));
        const auto frequency =
            86.0F + 26.0F * std::sin(normalized_age * kTwoPi);
        voice.phase += kTwoPi * frequency / sample_rate;
        sample =
            (std::sin(voice.phase) * 0.38F + noise * 0.18F) *
            envelope * 0.82F;
        break;
    }
    case GameSfxKind::MusketShot: {
        // Je superpose un claquement tres bref, le corps grave de la charge
        // noire puis une queue filtree qui donne de la masse sans asset WAV.
        const auto t = voice.age;
        const auto high_noise = noise - voice.previous_noise * 0.72F;
        voice.previous_noise = noise;
        voice.filtered_noise += (noise - voice.filtered_noise) * 0.055F;

        const auto crack_envelope = std::exp(-t * 115.0F);
        const auto boom_envelope =
            (1.0F - std::exp(-t * 150.0F)) *
            std::exp(-t * 6.0F);
        const auto tail_envelope =
            (1.0F - std::exp(-t * 24.0F)) *
            std::exp(-t * 3.35F);
        const auto boom_frequency = 79.0F - 28.0F * normalized_age;
        voice.phase += kTwoPi * boom_frequency / sample_rate;
        voice.secondary_phase +=
            kTwoPi * (43.0F - 9.0F * normalized_age) /
            sample_rate;

        const auto crack = high_noise * crack_envelope * 1.18F;
        const auto boom =
            (std::sin(voice.phase) * 0.72F +
             std::sin(voice.secondary_phase) * 0.34F) *
            boom_envelope;
        const auto tail =
            (voice.filtered_noise * 0.62F +
             std::sin(voice.phase * 0.37F) * 0.13F) *
            tail_envelope;
        sample = crack + boom + tail;
        break;
    }
    }

    if (voice.phase > kTwoPi) {
        voice.phase = std::fmod(voice.phase, kTwoPi);
    }
    if (voice.secondary_phase > kTwoPi) {
        voice.secondary_phase = std::fmod(voice.secondary_phase, kTwoPi);
    }

    voice.age += 1.0F / sample_rate;
    if (voice.age >= voice.duration) {
        voice.active = false;
    }

    const auto finite_sample = std::isfinite(sample) ? sample : 0.0F;
    return finite_sample * voice.volume;
}

auto ProceduralSfxMixer::soft_limit(float sample) noexcept -> float {
    if (!std::isfinite(sample)) {
        return 0.0F;
    }
    const auto magnitude = std::abs(sample);
    if (magnitude <= 0.82F) {
        return sample;
    }

    // Je garde les petits signaux intacts et je compresse seulement la reserve
    // haute, ce qui autorise six mousquets simultanes sans ecretage brutal.
    const auto excess = magnitude - 0.82F;
    const auto limited =
        0.82F +
        0.179F *
            (1.0F - std::exp(-excess * 4.5F));
    return std::copysign(std::min(limited, 0.999F), sample);
}

} // namespace valcraft
