#include "world/OceanSimulation.h"
#include "world/WorldGenerator.h"

#include <glm/geometric.hpp>

#include <algorithm>
#include <array>
#include <cmath>

namespace valcraft {

namespace {

constexpr float kGravity = 9.80665F;
constexpr float kTwoPi = 6.28318530717958647692F;
constexpr float kInlandCalmAmplitude = 0.09F;
constexpr float kOpenSeaCalmAmplitude = 0.22F;
constexpr float kMaximumInlandAmplitude = 0.34F;
constexpr float kOpenSeaStormAmplitude = 1.405F;
constexpr float kMaximumOpenSeaTempestAmplitude = 3.33F;

// Une petite seconde harmonique rend les cretes plus aiguës sans provoquer
// les boucles ou auto-intersections possibles avec des Gerstner trop fortes.
constexpr float kSecondHarmonicScale = 0.14F;

struct WavePreset {
    glm::vec2 direction;
    float inland_wavelength;
    float open_sea_wavelength;
    float amplitude_share;
    float tempest_amplitude_share;
    float phase_offset;
    float steepness;
};

// Les trois premières vagues constituent la houle qui déplace le navire.
// En Tempest, je concentre 90 % de l'énergie sur cette houle longue : la
// flottabilité et les profils graphiques Low restent ainsi cohérents.
constexpr std::array<WavePreset, kOceanMaxWaveCount> kWavePresets {{
    {{ 0.180F, 0.984F}, 42.0F, 96.0F, 0.360F, 0.480F, 0.15F, 0.48F},
    {{-0.320F, 0.947F}, 27.0F, 64.0F, 0.240F, 0.270F, 1.72F, 0.54F},
    {{ 0.620F, 0.785F}, 16.0F, 36.0F, 0.170F, 0.150F, 3.36F, 0.61F},
    {{-0.780F, 0.626F},  9.0F, 14.0F, 0.110F, 0.055F, 5.02F, 0.68F},
    {{ 0.930F, 0.368F},  4.8F,  7.0F, 0.075F, 0.030F, 2.58F, 0.74F},
    {{-0.480F, 0.877F},  2.4F,  3.5F, 0.045F, 0.015F, 4.41F, 0.80F},
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
    float severity,
    float tempest_factor) noexcept -> OceanSeaState {

    if (tempest_factor >= 0.50F) {
        return OceanSeaState::Tempest;
    }

    if (severity < 0.24F) {
        return OceanSeaState::Calm;
    }

    if (severity < 0.46F) {
        return OceanSeaState::Moderate;
    }

    if (severity < 0.68F) {
        return OceanSeaState::Rough;
    }

    return OceanSeaState::Storm;
}

} // namespace

auto OceanSimulation::surface_profile_for_world(
    WorldGenerationProfile profile) noexcept
    -> OceanSurfaceProfile {

    return profile ==
                   WorldGenerationProfile::OceanAdventure
               ? OceanSurfaceProfile::OpenSea
               : OceanSurfaceProfile::InlandWater;
}

auto OceanSimulation::evaluate(
    const EnvironmentState& environment,
    OceanSurfaceProfile profile) noexcept -> OceanState {

    const auto wind = saturate(environment.wind_strength);
    const auto storm = saturate(environment.storm_intensity);
    const auto precipitation =
        saturate(environment.precipitation_intensity);
    const auto authored_violent_storm =
        saturate(
            environment.violent_storm_intensity);

    // Le vent forme la houle de fond. La tempête augmente surtout son énergie
    // et sa cambrure. La pluie ne fournit qu'une faible contribution.
    const auto raw_energy = std::clamp(
        wind * 0.58F +
        storm * 0.72F +
        precipitation * 0.08F,
        0.0F,
        1.0F);

    const auto severity = smootherstep(raw_energy);

    // Je conserve une voie de secours pour les tests, mods et futurs appels
    // publics qui renseignent uniquement vent=1 et tempête=1. Le profil météo
    // normal fournit cependant violent_storm_intensity et reste la référence.
    const auto inferred_tempest =
        smootherstep(
            (storm - 0.82F) /
            0.18F) *
        smootherstep(
            (wind - 0.62F) /
            0.30F);
    const auto tempest_factor =
        profile == OceanSurfaceProfile::OpenSea
            ? smootherstep(
                  std::max(
                      authored_violent_storm,
                      inferred_tempest))
            : 0.0F;

    // Je conserve une houle lisible dès le départ. Les lacs restent bornés à
    // quelques dizaines de centimètres, tandis que seule la Tempest de pleine
    // mer ajoute la houle extrême de plusieurs mètres.
    const auto calm_amplitude =
        profile == OceanSurfaceProfile::OpenSea
            ? kOpenSeaCalmAmplitude
            : kInlandCalmAmplitude;

    const auto ordinary_amplitude_ceiling =
        profile == OceanSurfaceProfile::OpenSea
            ? kOpenSeaStormAmplitude
            : kMaximumInlandAmplitude;
    const auto ordinary_amplitude =
        calm_amplitude +
        (ordinary_amplitude_ceiling - calm_amplitude) *
            std::pow(raw_energy, 1.65F);
    const auto total_amplitude =
        profile == OceanSurfaceProfile::OpenSea
            ? ordinary_amplitude +
                  (kMaximumOpenSeaTempestAmplitude -
                   ordinary_amplitude) *
                      tempest_factor
            : ordinary_amplitude;

    const auto safe_time = static_cast<double>(
        std::clamp(
            finite_or(
                environment.weather_time_seconds,
                0.0F),
            -1.0e9F,
            1.0e9F));

    OceanState result {};
    result.sea_state =
        classify_sea(
            severity,
            tempest_factor);
    result.severity = severity;
    result.tempest_factor =
        tempest_factor;
    result.total_amplitude = total_amplitude;
    if (profile == OceanSurfaceProfile::OpenSea) {
        // Je garde des cretes lisibles par beau temps sans les transformer en
        // ecume de tempete. La meteo abaisse ensuite progressivement le seuil.
        result.foam_threshold = std::max(
            0.54F,
            0.84F -
                severity * 0.17F -
                tempest_factor * 0.13F);
        result.detail_strength =
            0.0080F +
            severity * 0.0073F +
            tempest_factor * 0.0040F;
    } else {
        result.foam_threshold = std::max(
            0.67F,
            0.93F - severity * 0.26F);
        result.detail_strength =
            0.0028F + severity * 0.0125F;
    }

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

        // Je garde les nombres d'onde fixes pendant les transitions météo.
        // Faire varier la longueur avec l'énergie ferait glisser la phase de
        // omega(E)*temps et déplacerait brutalement les crêtes en longue partie.
        const auto wavelength = std::max(
            1.0F,
            profile == OceanSurfaceProfile::OpenSea
                ? preset.open_sea_wavelength
                : preset.inland_wavelength);

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
                (0.34F +
                 severity * 0.58F +
                 tempest_factor * 0.08F),
            0.12F,
            0.86F);

        const auto amplitude_share =
            preset.amplitude_share +
            (preset.tempest_amplitude_share -
             preset.amplitude_share) *
                tempest_factor;
        const auto amplitude =
            total_amplitude *
            amplitude_share;

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

        // Je conserve la crete la plus nette au lieu de moyenner toutes les
        // ondes. Une moyenne effacait presque totalement les lignes de houle
        // des que plusieurs directions se croisaient par beau temps.
        const auto normalized_wave_height =
            std::clamp(
                0.5F +
                    0.5F *
                        (sine +
                         harmonic * double_sine) /
                        (1.0F + harmonic),
                0.0F,
                1.0F);

        result.crest = std::max(
            result.crest,
            normalized_wave_height *
                normalized_wave_height *
                normalized_wave_height);
    }

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
