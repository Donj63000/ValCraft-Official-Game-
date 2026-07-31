#pragma once

#include "gameplay/weapons/LegendaryWeaponProgression.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace valcraft {

inline constexpr std::size_t kMaximumLeviathanAudioLayers = 6U;

enum class LeviathanAudioCue : std::uint8_t {
    Draw = 0,
    Sheath,
    FirstSweep,
    SecondSweep,
    Earthbreaker,
    ChargedExecution,
    GuardRaised,
    PerfectGuard,
    GuardBreak,
    Impact,
    CrowdMurmur,
    CrowdCheer,
    ArenaHorn,
    ArenaDrum,
    ChainStrain,
    ChainBreak,
};

enum class LeviathanImpactAudioMaterial : std::uint8_t {
    Flesh = 0,
    Wood,
    Stone,
    Metal,
    Sand,
    Water,
};

enum class ProceduralAudioWaveform : std::uint8_t {
    Sine = 0,
    Triangle,
    Noise,
    FilteredNoise,
    Metallic,
    Impulse,
};

struct ProceduralAudioLayer {
    ProceduralAudioWaveform waveform =
        ProceduralAudioWaveform::Sine;
    float start_seconds = 0.0F;
    float duration_seconds = 0.10F;
    float frequency_start_hz = 120.0F;
    float frequency_end_hz = 120.0F;
    float gain = 0.5F;
    float attack_seconds = 0.005F;
    float release_seconds = 0.05F;
    float noise_filter = 0.25F;
};

struct LeviathanAudioRequest {
    LeviathanAudioCue cue = LeviathanAudioCue::FirstSweep;
    LeviathanImpactAudioMaterial impact_material =
        LeviathanImpactAudioMaterial::Flesh;
    LegendaryWeaponAwakening awakening =
        LegendaryWeaponAwakening::Dormant;
    float intensity = 1.0F;
    std::uint32_t seed = 1U;
    std::uint8_t variant = 0U;
};

struct LeviathanAudioRecipe {
    std::array<ProceduralAudioLayer, kMaximumLeviathanAudioLayers>
        layers {};
    std::size_t layer_count = 0U;
    float duration_seconds = 0.0F;
    float master_gain = 1.0F;
    float low_pass_hz = 18'000.0F;
    std::uint32_t seed = 1U;
    std::uint8_t resolved_variant = 0U;
};

[[nodiscard]] auto build_leviathan_audio_recipe(
    const LeviathanAudioRequest& request) noexcept
    -> LeviathanAudioRecipe;
[[nodiscard]] auto synthesize_leviathan_audio(
    const LeviathanAudioRecipe& recipe,
    int sample_rate = 48'000) -> std::vector<float>;
[[nodiscard]] auto leviathan_audio_recipe_is_valid(
    const LeviathanAudioRecipe& recipe) noexcept -> bool;

} // namespace valcraft
