#include "world/OceanSimulation.h"

#include <glm/geometric.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace valcraft {

namespace {

constexpr float kGravity = 9.80665F;
constexpr float kTwoPi = 6.28318530717958647692F;
constexpr float kCalmOceanAmplitude = 0.09F;
constexpr float kMaximumOceanAmplitude = 1.405F;
constexpr float kWeatherOceanAmplitudeRange =
    kMaximumOceanAmplitude -
    kCalmOceanAmplitude;

// Une petite seconde harmonique rend les cretes plus aiguës sans provoquer
// les boucles ou auto-intersections possibles avec des Gerstner trop fortes.
constexpr float kSecondHarmonicScale = 0.14F;

struct WavePreset {
    glm::vec2 direction;
    float wavelength;
    float amplitude_share;
    float phase_offset;
    float steepness;
};

// Les trois premières vagues constituent la houle qui déplace le navire.
// Les trois suivantes apportent le clapot visible de moyenne et petite échelle.
constexpr std::array<WavePreset, kOceanMaxWaveCount> kWavePresets {{
    {{ 0.180F, 0.984F}, 42.0F, 0.360F, 0.15F, 0.48F},
    {{-0.320F, 0.947F}, 27.0F, 0.240F, 1.72F, 0.54F},
    {{ 0.620F, 0.785F}, 16.0F, 0.170F, 3.36F, 0.61F},
    {{-0.780F, 0.626F},  9.0F, 0.110F, 5.02F, 0.68F},
    {{ 0.930F, 0.368F},  4.8F, 0.075F, 2.58F, 0.74F},
    {{-0.480F, 0.877F},  2.4F, 0.045F, 4.41F, 0.80F},
}};

[[nodiscard]] auto finite_or(
    float value,
    float fallback) noexcept -> float {

    return std::isfinite(value) ? value : fallback;
}

[[nodiscard]] auto saturate(float value) noexcept -> float {
    return std::clamp(finite_or(value, 0.0F), 0.0F, 1.0F);
}

[[nodiscard]] auto smootherstep(float value) noexcept -> float {
    const auto x = saturate(value);

    return x * x * x *
           (x * (x * 6.0F - 15.0F) + 10.0F);
}

[[nodiscard]] auto wrap_phase(double phase) noexcept -> float {
    constexpr double kTwoPiDouble =
        6.283185307179586476925286766559;

    auto wrapped = std::fmod(phase, kTwoPiDouble);

    if (wrapped < 0.0) {
        wrapped += kTwoPiDouble;
    }

    return static_cast<float>(wrapped);
}

[[nodiscard]] auto classify_sea(
    float severity) noexcept -> OceanSeaState {

    if (severity < 0.24F) {
        return OceanSeaState::Calm;
    }

    if (severity < 0.46F) {
        return OceanSeaState::Moderate;
    }

    if (severity < 0.68F) {
        return OceanSeaState::Rough;
    }

    if (severity < 0.86F) {
        return OceanSeaState::Storm;
    }

    return OceanSeaState::Tempest;
}

} // namespace

auto OceanSimulation::evaluate(
    const EnvironmentState& environment) noexcept -> OceanState {

    const auto wind = saturate(environment.wind_strength);
    const auto storm = saturate(environment.storm_intensity);
    const auto precipitation =
        saturate(environment.precipitation_intensity);

    // Le vent forme la houle de fond. La tempête augmente surtout son énergie
    // et sa cambrure. La pluie ne fournit qu'une faible contribution.
    const auto raw_energy = std::clamp(
        wind * 0.58F +
        storm * 0.72F +
        precipitation * 0.08F,
        0.0F,
        1.0F);

    const auto severity = smootherstep(raw_energy);

    // Je conserve une houle lisible dès le départ, même sans vent. Je réduis
    // d'autant la plage météorologique afin de ne pas renforcer les tempêtes.
    const auto total_amplitude =
        kCalmOceanAmplitude +
        kWeatherOceanAmplitudeRange *
            std::pow(raw_energy, 1.65F);

    const auto wavelength_scale =
        0.82F + severity * 0.38F;

    const auto safe_time = static_cast<double>(
        std::clamp(
            finite_or(
                environment.weather_time_seconds,
                0.0F),
            -1.0e9F,
            1.0e9F));

    OceanState result {};
    result.sea_state = classify_sea(severity);
    result.severity = severity;
    result.total_amplitude = total_amplitude;
    result.foam_threshold = 0.93F - severity * 0.26F;
    result.detail_strength =
        0.0028F + severity * 0.0125F;

    // La phase des petites rides est bornée. On évite ainsi les pertes de
    // précision flottante après plusieurs heures de jeu.
    result.detail_phase =
        wrap_phase(0.42 * safe_time);

    for (std::size_t index = 0;
         index < result.waves.size();
         ++index) {

        const auto& preset = kWavePresets[index];

        const auto direction_length =
            glm::length(preset.direction);

        const auto direction =
            direction_length > 1.0e-5F
                ? preset.direction / direction_length
                : glm::vec2 {0.0F, 1.0F};

        const auto wavelength = std::max(
            1.0F,
            preset.wavelength * wavelength_scale);

        const auto wave_number =
            kTwoPi / wavelength;

        // Relation de dispersion d'une onde de gravité en eau profonde.
        const auto angular_frequency =
            std::sqrt(kGravity * wave_number);

        const auto phase = wrap_phase(
            static_cast<double>(preset.phase_offset) -
            static_cast<double>(angular_frequency) *
                safe_time);

        const auto steepness = std::clamp(
            preset.steepness *
                (0.34F + severity * 0.66F),
            0.12F,
            0.86F);

        const auto amplitude =
            total_amplitude *
            preset.amplitude_share;

        result.waves[index] = {
            direction,
            amplitude,
            wave_number,
            phase,
            steepness,
        };

        result.maximum_displacement +=
            amplitude *
            (1.0F +
             kSecondHarmonicScale * steepness);
    }

    return result;
}

auto OceanSimulation::sample(
    const OceanState& ocean,
    const glm::vec2& world_xz,
    std::size_t wave_count) noexcept -> OceanSample {

    if (!std::isfinite(world_xz.x) ||
        !std::isfinite(world_xz.y)) {
        return {};
    }

    const auto count = std::min(
        wave_count,
        ocean.waves.size());

    auto amplitude_sum = 0.0F;
    OceanSample result {};

    for (std::size_t index = 0;
         index < count;
         ++index) {

        const auto& wave = ocean.waves[index];

        if (!std::isfinite(wave.direction.x) ||
            !std::isfinite(wave.direction.y) ||
            !std::isfinite(wave.amplitude) ||
            !std::isfinite(wave.wave_number) ||
            !std::isfinite(wave.phase) ||
            wave.amplitude <= 0.0F ||
            wave.wave_number <= 0.0F) {
            continue;
        }

        const auto theta =
            glm::dot(wave.direction, world_xz) *
                wave.wave_number +
            wave.phase;

        if (!std::isfinite(theta)) {
            continue;
        }

        const auto harmonic =
            kSecondHarmonicScale *
            std::clamp(
                finite_or(wave.steepness, 0.0F),
                0.0F,
                1.0F);

        const auto sine = std::sin(theta);
        const auto cosine = std::cos(theta);
        const auto double_sine =
            std::sin(theta * 2.0F);
        const auto double_cosine =
            std::cos(theta * 2.0F);

        result.height +=
            wave.amplitude *
            (sine +
             harmonic * double_sine);

        const auto derivative =
            wave.amplitude *
            wave.wave_number *
            (cosine +
             2.0F * harmonic * double_cosine);

        result.gradient +=
            wave.direction * derivative;

        result.crest +=
            wave.amplitude *
            (0.5F + 0.5F * sine);

        amplitude_sum += wave.amplitude;
    }

    result.crest =
        amplitude_sum >
                std::numeric_limits<float>::epsilon()
            ? std::clamp(
                  result.crest / amplitude_sum,
                  0.0F,
                  1.0F)
            : 0.0F;

    return result;
}

auto OceanSimulation::state_label(
    OceanSeaState state) noexcept -> const char* {

    switch (state) {
    case OceanSeaState::Calm:
        return "calme";

    case OceanSeaState::Moderate:
        return "moderee";

    case OceanSeaState::Rough:
        return "agitee";

    case OceanSeaState::Storm:
        return "tempete";

    case OceanSeaState::Tempest:
        return "tempete majeure";

    default:
        return "calme";
    }
}

} // namespace valcraft
