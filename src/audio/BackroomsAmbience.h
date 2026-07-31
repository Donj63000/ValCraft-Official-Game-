#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace valcraft {

struct BackroomsAmbienceContext {
    bool active = false;
    std::uint32_t seed = 0U;
    float darkness = 0.0F;
    float anomaly = 0.0F;
};

class BackroomsAmbience {
public:
    explicit BackroomsAmbience(int sample_rate = 48'000) noexcept;

    void set_sample_rate(int sample_rate) noexcept;
    void reset() noexcept;
    void set_context(const BackroomsAmbienceContext& context) noexcept;

    // Le signal remplace progressivement la musique existante, mais laisse les
    // effets de pas et d'interface être mixés ensuite par GameMusic.
    void mix_interleaved(
        std::span<float> samples,
        std::size_t channel_count) noexcept;

    [[nodiscard]] auto current_mix() const noexcept -> float;

private:
    [[nodiscard]] auto next_noise() noexcept -> float;
    [[nodiscard]] auto render_channel(
        bool right_channel,
        float darkness,
        float anomaly) noexcept -> float;
    void advance_phases() noexcept;
    void apply_seed(std::uint32_t seed) noexcept;

    int sample_rate_ = 48'000;
    bool target_active_ = false;
    std::uint32_t seed_ = 0U;
    std::uint32_t noise_state_ = 0x7F4A7C15U;

    float target_darkness_ = 0.0F;
    float target_anomaly_ = 0.0F;
    float smoothed_darkness_ = 0.0F;
    float smoothed_anomaly_ = 0.0F;
    float mix_ = 0.0F;

    float mains_frequency_hz_ = 50.0F;
    float ventilation_frequency_hz_ = 28.0F;
    float ballast_frequency_hz_ = 121.0F;
    float stereo_detune_hz_ = 0.17F;

    float mains_phase_ = 0.0F;
    float ventilation_phase_ = 0.0F;
    float ballast_phase_ = 0.0F;
    float drift_phase_ = 0.0F;
    float stereo_phase_ = 0.0F;
    float noise_lowpass_ = 0.0F;
    float noise_bandpass_ = 0.0F;
    float previous_noise_lowpass_ = 0.0F;
};

} // namespace valcraft
