#include "audio/BackroomsAmbience.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace valcraft {

namespace {

constexpr float kTwoPi = 6.28318530717958647692F;
constexpr float kHalfPi = 1.57079632679489661923F;

[[nodiscard]] auto finite_unit(float value) noexcept -> float {
    return std::isfinite(value)
               ? std::clamp(value, 0.0F, 1.0F)
               : 0.0F;
}

[[nodiscard]] auto wrap_phase(float phase) noexcept -> float {
    if (phase >= 1.0F) {
        phase -= std::floor(phase);
    } else if (phase < 0.0F) {
        phase -= std::floor(phase);
    }
    return phase;
}

void advance_phase(
    float& phase,
    float frequency_hz,
    float sample_rate) noexcept {

    phase = wrap_phase(
        phase + frequency_hz / sample_rate);
}

[[nodiscard]] auto sine(float phase) noexcept -> float {
    return std::sin(phase * kTwoPi);
}

[[nodiscard]] auto smoothing_factor(
    float response_seconds,
    float sample_rate) noexcept -> float {

    const auto seconds =
        std::max(response_seconds, 0.001F);
    const auto rate =
        std::max(sample_rate, 8'000.0F);
    return 1.0F -
           static_cast<float>(
               std::exp(
                   -1.0 /
                   (static_cast<double>(seconds) *
                    static_cast<double>(rate))));
}

[[nodiscard]] auto mix_value(
    float first,
    float second,
    float amount) noexcept -> float {

    return first + (second - first) * amount;
}

} // namespace

BackroomsAmbience::BackroomsAmbience(
    int sample_rate) noexcept {

    set_sample_rate(sample_rate);
    reset();
}

void BackroomsAmbience::set_sample_rate(
    int sample_rate) noexcept {

    sample_rate_ = std::clamp(
        sample_rate,
        8'000,
        192'000);
}

void BackroomsAmbience::reset() noexcept {
    target_active_ = false;
    seed_ = 0U;
    noise_state_ = 0x7F4A7C15U;
    target_darkness_ = 0.0F;
    target_anomaly_ = 0.0F;
    smoothed_darkness_ = 0.0F;
    smoothed_anomaly_ = 0.0F;
    mix_ = 0.0F;
    mains_phase_ = 0.0F;
    ventilation_phase_ = 0.0F;
    ballast_phase_ = 0.0F;
    drift_phase_ = 0.0F;
    stereo_phase_ = 0.0F;
    noise_lowpass_ = 0.0F;
    noise_bandpass_ = 0.0F;
    previous_noise_lowpass_ = 0.0F;
    apply_seed(0xB4C3'0001U);
}

void BackroomsAmbience::apply_seed(
    std::uint32_t seed) noexcept {

    seed_ = seed == 0U ? 0xB4C3'0001U : seed;
    auto state = seed_ ^ 0x9E37'79B9U;
    state ^= state << 13U;
    state ^= state >> 17U;
    state ^= state << 5U;
    noise_state_ =
        state == 0U ? 0x7F4A7C15U : state;

    const auto unit = [](std::uint32_t value) noexcept {
        return static_cast<float>(value & 0xFFFFU) /
               65'535.0F;
    };
    mains_frequency_hz_ =
        49.72F + unit(state) * 0.56F;
    ventilation_frequency_hz_ =
        26.8F + unit(state >> 5U) * 3.1F;
    ballast_frequency_hz_ =
        116.0F + unit(state >> 11U) * 17.0F;
    stereo_detune_hz_ =
        0.11F + unit(state >> 17U) * 0.17F;
}

void BackroomsAmbience::set_context(
    const BackroomsAmbienceContext& context) noexcept {

    target_active_ = context.active;
    target_darkness_ =
        finite_unit(context.darkness);
    target_anomaly_ =
        finite_unit(context.anomaly);

    const auto sanitized_seed =
        context.seed == 0U
            ? 0xB4C3'0001U
            : context.seed;
    if (sanitized_seed != seed_) {
        // Les phases restent continues : changer de partie modifie la couleur
        // du réseau électrique sans créer de clic dans le callback audio.
        apply_seed(sanitized_seed);
    }
}

auto BackroomsAmbience::next_noise() noexcept -> float {
    auto state = noise_state_;
    state ^= state << 13U;
    state ^= state >> 17U;
    state ^= state << 5U;
    noise_state_ =
        state == 0U ? 0x7F4A7C15U : state;
    const auto normalized =
        static_cast<float>(
            noise_state_ &
            std::numeric_limits<std::uint16_t>::max()) /
        static_cast<float>(
            std::numeric_limits<std::uint16_t>::max());
    return normalized * 2.0F - 1.0F;
}

void BackroomsAmbience::advance_phases() noexcept {
    const auto sample_rate =
        static_cast<float>(sample_rate_);
    const auto drift =
        sine(drift_phase_);
    advance_phase(
        mains_phase_,
        mains_frequency_hz_ + drift * 0.055F,
        sample_rate);
    advance_phase(
        ventilation_phase_,
        ventilation_frequency_hz_ +
            drift * 0.018F,
        sample_rate);
    advance_phase(
        ballast_phase_,
        ballast_frequency_hz_ +
            drift * 0.21F,
        sample_rate);
    advance_phase(
        drift_phase_,
        0.037F,
        sample_rate);
    advance_phase(
        stereo_phase_,
        stereo_detune_hz_,
        sample_rate);
}

auto BackroomsAmbience::render_channel(
    bool right_channel,
    float darkness,
    float anomaly) noexcept -> float {

    const auto side =
        right_channel ? 1.0F : -1.0F;
    const auto stereo_offset =
        side *
        (0.0018F +
         sine(stereo_phase_) *
             (0.0008F + anomaly * 0.0012F));
    const auto mains =
        sine(mains_phase_ + stereo_offset);
    const auto second_harmonic =
        sine(mains_phase_ * 2.0F +
             0.13F +
             stereo_offset * 0.45F);
    const auto third_harmonic =
        sine(mains_phase_ * 3.0F +
             0.31F -
             stereo_offset * 0.30F);
    const auto ballast =
        sine(
            ballast_phase_ +
            side * 0.017F);
    const auto ventilation =
        sine(
            ventilation_phase_ +
            side * 0.006F);

    const auto voltage_sag =
        0.91F +
        sine(drift_phase_) * 0.055F +
        sine(
            drift_phase_ * 0.271F +
            0.41F) *
            0.025F;
    const auto electrical =
        (mains * 0.58F +
         second_harmonic * 0.27F +
         third_harmonic * 0.10F +
         ballast * 0.05F) *
        voltage_sag;

    const auto noise =
        next_noise();
    previous_noise_lowpass_ = noise_lowpass_;
    noise_lowpass_ +=
        (noise - noise_lowpass_) *
        mix_value(0.0014F, 0.0042F, anomaly);
    const auto differentiated =
        noise_lowpass_ - previous_noise_lowpass_;
    noise_bandpass_ +=
        (differentiated - noise_bandpass_) *
        0.031F;

    const auto electrical_gain =
        mix_value(0.034F, 0.025F, darkness);
    const auto ventilation_gain =
        mix_value(0.009F, 0.021F, darkness);
    const auto noise_gain =
        mix_value(0.004F, 0.011F, anomaly);
    return electrical * electrical_gain +
           ventilation * ventilation_gain +
           noise_bandpass_ * noise_gain;
}

void BackroomsAmbience::mix_interleaved(
    std::span<float> samples,
    std::size_t channel_count) noexcept {

    if (channel_count == 0U ||
        samples.size() < channel_count) {
        return;
    }

    const auto frame_count =
        samples.size() / channel_count;
    const auto sample_rate =
        static_cast<float>(sample_rate_);
    const auto fade_step =
        1.0F /
        (1.65F * sample_rate);
    const auto parameter_smoothing =
        smoothing_factor(
            0.85F,
            sample_rate);

    for (std::size_t frame = 0U;
         frame < frame_count;
         ++frame) {
        const auto target_mix =
            target_active_ ? 1.0F : 0.0F;
        if (mix_ < target_mix) {
            mix_ =
                std::min(
                    target_mix,
                    mix_ + fade_step);
        } else if (mix_ > target_mix) {
            mix_ =
                std::max(
                    target_mix,
                    mix_ - fade_step);
        }

        smoothed_darkness_ +=
            (target_darkness_ -
             smoothed_darkness_) *
            parameter_smoothing;
        smoothed_anomaly_ +=
            (target_anomaly_ -
             smoothed_anomaly_) *
            parameter_smoothing;

        const auto ambience_gain =
            std::sin(mix_ * kHalfPi);
        const auto music_gain =
            std::cos(mix_ * kHalfPi);
        const auto left =
            render_channel(
                false,
                smoothed_darkness_,
                smoothed_anomaly_);
        const auto right =
            render_channel(
                true,
                smoothed_darkness_,
                smoothed_anomaly_);

        const auto base_index =
            frame * channel_count;
        for (std::size_t channel = 0U;
             channel < channel_count;
             ++channel) {
            const auto ambience =
                channel == 0U ? left : right;
            const auto mixed =
                samples[base_index + channel] *
                    music_gain +
                ambience * ambience_gain;
            samples[base_index + channel] =
                std::isfinite(mixed)
                    ? std::clamp(
                          mixed,
                          -1.0F,
                          1.0F)
                    : 0.0F;
        }

        advance_phases();
    }
}

auto BackroomsAmbience::current_mix() const noexcept
    -> float {

    return mix_;
}

} // namespace valcraft
