#include "audio/LeviathanProceduralAudio.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace valcraft {

namespace {

constexpr float kPi = 3.14159265358979323846F;
constexpr float kTwoPi = kPi * 2.0F;

[[nodiscard]] auto finite_clamped(float value,
                                  float minimum,
                                  float maximum,
                                  float fallback = 0.0F) noexcept -> float {
    return std::isfinite(value)
        ? std::clamp(value, minimum, maximum)
        : fallback;
}

[[nodiscard]] auto next_noise(std::uint32_t& state) noexcept -> float {
    state = state * 1664525U + 1013904223U;
    const auto unit =
        static_cast<float>((state >> 8U) & 0x00FFFFFFU) /
        static_cast<float>(0x01000000U);
    return unit * 2.0F - 1.0F;
}

void add_layer(LeviathanAudioRecipe& recipe,
               ProceduralAudioWaveform waveform,
               float start,
               float duration,
               float frequency_start,
               float frequency_end,
               float gain,
               float attack,
               float release,
               float filter = 0.25F) noexcept {
    if (recipe.layer_count >= recipe.layers.size()) {
        return;
    }
    recipe.layers[recipe.layer_count++] = {
        waveform,
        start,
        duration,
        frequency_start,
        frequency_end,
        gain,
        attack,
        release,
        filter,
    };
    recipe.duration_seconds =
        std::max(recipe.duration_seconds, start + duration);
}

[[nodiscard]] auto safe_awakening(
    LegendaryWeaponAwakening awakening) noexcept
    -> LegendaryWeaponAwakening {
    switch (awakening) {
    case LegendaryWeaponAwakening::Dormant:
    case LegendaryWeaponAwakening::Corrupted:
    case LegendaryWeaponAwakening::Astral:
    case LegendaryWeaponAwakening::Awakened:
        return awakening;
    }
    return LegendaryWeaponAwakening::Dormant;
}

[[nodiscard]] auto safe_impact_material(
    LeviathanImpactAudioMaterial material) noexcept
    -> LeviathanImpactAudioMaterial {
    switch (material) {
    case LeviathanImpactAudioMaterial::Flesh:
    case LeviathanImpactAudioMaterial::Wood:
    case LeviathanImpactAudioMaterial::Stone:
    case LeviathanImpactAudioMaterial::Metal:
    case LeviathanImpactAudioMaterial::Sand:
    case LeviathanImpactAudioMaterial::Water:
        return material;
    }
    return LeviathanImpactAudioMaterial::Flesh;
}

void add_sweep_layers(LeviathanAudioRecipe& recipe,
                      float direction,
                      float mass,
                      float variant_pitch) noexcept {
    add_layer(
        recipe, ProceduralAudioWaveform::FilteredNoise,
        0.0F, 0.34F,
        420.0F * variant_pitch,
        92.0F * variant_pitch,
        0.38F * mass, 0.015F, 0.12F, 0.11F);
    add_layer(
        recipe, ProceduralAudioWaveform::Triangle,
        0.035F, 0.29F,
        (direction > 0.0F ? 104.0F : 92.0F) * variant_pitch,
        52.0F * variant_pitch,
        0.34F * mass, 0.02F, 0.15F);
    add_layer(
        recipe, ProceduralAudioWaveform::Metallic,
        0.075F, 0.22F,
        288.0F * variant_pitch,
        206.0F * variant_pitch,
        0.12F * mass, 0.006F, 0.18F);
}

void add_impact_layers(LeviathanAudioRecipe& recipe,
                       LeviathanImpactAudioMaterial material,
                       float pitch) noexcept {
    switch (material) {
    case LeviathanImpactAudioMaterial::Flesh:
        add_layer(
            recipe, ProceduralAudioWaveform::FilteredNoise,
            0.0F, 0.24F, 88.0F * pitch, 48.0F * pitch,
            0.50F, 0.001F, 0.18F, 0.06F);
        add_layer(
            recipe, ProceduralAudioWaveform::Sine,
            0.0F, 0.20F, 72.0F * pitch, 42.0F * pitch,
            0.42F, 0.002F, 0.16F);
        break;
    case LeviathanImpactAudioMaterial::Wood:
        add_layer(
            recipe, ProceduralAudioWaveform::Impulse,
            0.0F, 0.18F, 176.0F * pitch, 98.0F * pitch,
            0.58F, 0.001F, 0.12F);
        add_layer(
            recipe, ProceduralAudioWaveform::Triangle,
            0.008F, 0.24F, 118.0F * pitch, 74.0F * pitch,
            0.32F, 0.002F, 0.18F);
        break;
    case LeviathanImpactAudioMaterial::Stone:
        add_layer(
            recipe, ProceduralAudioWaveform::Noise,
            0.0F, 0.16F, 260.0F * pitch, 120.0F * pitch,
            0.38F, 0.001F, 0.11F);
        add_layer(
            recipe, ProceduralAudioWaveform::Metallic,
            0.0F, 0.34F, 214.0F * pitch, 158.0F * pitch,
            0.46F, 0.001F, 0.26F);
        break;
    case LeviathanImpactAudioMaterial::Metal:
        add_layer(
            recipe, ProceduralAudioWaveform::Metallic,
            0.0F, 0.68F, 482.0F * pitch, 326.0F * pitch,
            0.56F, 0.001F, 0.55F);
        add_layer(
            recipe, ProceduralAudioWaveform::Impulse,
            0.0F, 0.12F, 230.0F * pitch, 160.0F * pitch,
            0.42F, 0.001F, 0.09F);
        break;
    case LeviathanImpactAudioMaterial::Sand:
        add_layer(
            recipe, ProceduralAudioWaveform::FilteredNoise,
            0.0F, 0.30F, 180.0F * pitch, 72.0F * pitch,
            0.43F, 0.008F, 0.24F, 0.035F);
        add_layer(
            recipe, ProceduralAudioWaveform::Sine,
            0.0F, 0.16F, 62.0F * pitch, 38.0F * pitch,
            0.26F, 0.002F, 0.13F);
        break;
    case LeviathanImpactAudioMaterial::Water:
        add_layer(
            recipe, ProceduralAudioWaveform::FilteredNoise,
            0.0F, 0.48F, 320.0F * pitch, 82.0F * pitch,
            0.46F, 0.006F, 0.34F, 0.045F);
        add_layer(
            recipe, ProceduralAudioWaveform::Sine,
            0.025F, 0.40F, 94.0F * pitch, 46.0F * pitch,
            0.25F, 0.02F, 0.30F);
        break;
    }
}

[[nodiscard]] auto render_waveform(
    ProceduralAudioWaveform waveform,
    float phase,
    float noise,
    float filtered_noise) noexcept -> float {
    switch (waveform) {
    case ProceduralAudioWaveform::Sine:
        return std::sin(phase);
    case ProceduralAudioWaveform::Triangle:
        return std::asin(std::sin(phase)) * (2.0F / kPi);
    case ProceduralAudioWaveform::Noise:
        return noise;
    case ProceduralAudioWaveform::FilteredNoise:
        return filtered_noise;
    case ProceduralAudioWaveform::Metallic:
        return std::sin(phase) * 0.60F +
               std::sin(phase * 1.4142F) * 0.26F +
               std::sin(phase * 2.731F) * 0.14F;
    case ProceduralAudioWaveform::Impulse:
        return noise * 0.45F + std::sin(phase) * 0.55F;
    }
    return 0.0F;
}

} // namespace

auto build_leviathan_audio_recipe(
    const LeviathanAudioRequest& request) noexcept
    -> LeviathanAudioRecipe {
    auto recipe = LeviathanAudioRecipe {};
    recipe.seed = request.seed == 0U ? 1U : request.seed;
    recipe.resolved_variant =
        static_cast<std::uint8_t>(request.variant % 4U);
    const auto intensity =
        finite_clamped(request.intensity, 0.0F, 1.0F, 1.0F);
    recipe.master_gain = 0.34F + intensity * 0.66F;
    const auto variant_offset =
        static_cast<float>(recipe.resolved_variant) - 1.5F;
    const auto pitch = 1.0F + variant_offset * 0.025F;
    const auto awakening =
        static_cast<float>(static_cast<std::uint8_t>(
            safe_awakening(request.awakening)));

    switch (request.cue) {
    case LeviathanAudioCue::Draw:
    case LeviathanAudioCue::Sheath:
        add_layer(
            recipe, ProceduralAudioWaveform::FilteredNoise,
            0.0F, 0.52F, 210.0F * pitch, 82.0F * pitch,
            0.34F, 0.012F, 0.26F, 0.08F);
        add_layer(
            recipe, ProceduralAudioWaveform::Metallic,
            0.06F, 0.34F, 340.0F * pitch, 220.0F * pitch,
            0.20F, 0.004F, 0.27F);
        break;
    case LeviathanAudioCue::FirstSweep:
        add_sweep_layers(recipe, 1.0F, 1.0F, pitch);
        break;
    case LeviathanAudioCue::SecondSweep:
        add_sweep_layers(recipe, -1.0F, 1.08F, pitch * 0.96F);
        break;
    case LeviathanAudioCue::Earthbreaker:
        add_sweep_layers(recipe, 1.0F, 1.22F, pitch * 0.90F);
        add_layer(
            recipe, ProceduralAudioWaveform::Impulse,
            0.22F, 0.56F, 74.0F, 32.0F,
            0.62F, 0.001F, 0.42F);
        break;
    case LeviathanAudioCue::ChargedExecution:
        add_layer(
            recipe, ProceduralAudioWaveform::Sine,
            0.0F, 1.20F, 46.0F, 108.0F + awakening * 12.0F,
            0.34F, 0.08F, 0.18F);
        add_sweep_layers(recipe, 1.0F, 1.34F, pitch * 0.84F);
        add_layer(
            recipe, ProceduralAudioWaveform::Impulse,
            0.92F, 0.82F, 66.0F, 28.0F,
            0.72F, 0.001F, 0.62F);
        break;
    case LeviathanAudioCue::GuardRaised:
        add_layer(
            recipe, ProceduralAudioWaveform::Metallic,
            0.0F, 0.24F, 260.0F * pitch, 182.0F * pitch,
            0.38F, 0.002F, 0.18F);
        break;
    case LeviathanAudioCue::PerfectGuard:
        add_layer(
            recipe, ProceduralAudioWaveform::Impulse,
            0.0F, 0.18F, 520.0F * pitch, 240.0F * pitch,
            0.66F, 0.001F, 0.12F);
        add_layer(
            recipe, ProceduralAudioWaveform::Metallic,
            0.012F, 0.52F, 680.0F * pitch, 460.0F * pitch,
            0.38F, 0.001F, 0.42F);
        break;
    case LeviathanAudioCue::GuardBreak:
        add_layer(
            recipe, ProceduralAudioWaveform::Metallic,
            0.0F, 0.62F, 320.0F * pitch, 88.0F * pitch,
            0.58F, 0.001F, 0.50F);
        add_layer(
            recipe, ProceduralAudioWaveform::Noise,
            0.0F, 0.28F, 180.0F, 62.0F,
            0.32F, 0.001F, 0.22F);
        break;
    case LeviathanAudioCue::Impact:
        add_impact_layers(
            recipe, safe_impact_material(request.impact_material), pitch);
        break;
    case LeviathanAudioCue::CrowdMurmur:
        add_layer(
            recipe, ProceduralAudioWaveform::FilteredNoise,
            0.0F, 1.80F, 184.0F, 132.0F,
            0.30F, 0.18F, 0.35F, 0.018F);
        add_layer(
            recipe, ProceduralAudioWaveform::Triangle,
            0.08F, 1.52F, 96.0F * pitch, 84.0F * pitch,
            0.16F, 0.22F, 0.28F);
        break;
    case LeviathanAudioCue::CrowdCheer:
        add_layer(
            recipe, ProceduralAudioWaveform::FilteredNoise,
            0.0F, 1.45F, 220.0F, 148.0F,
            0.44F, 0.12F, 0.48F, 0.028F);
        add_layer(
            recipe, ProceduralAudioWaveform::Triangle,
            0.02F, 1.18F, 126.0F * pitch, 102.0F * pitch,
            0.22F, 0.12F, 0.42F);
        break;
    case LeviathanAudioCue::ArenaHorn:
        add_layer(
            recipe, ProceduralAudioWaveform::Triangle,
            0.0F, 1.65F, 92.0F * pitch, 88.0F * pitch,
            0.62F, 0.12F, 0.42F);
        add_layer(
            recipe, ProceduralAudioWaveform::Sine,
            0.0F, 1.65F, 184.0F * pitch, 176.0F * pitch,
            0.30F, 0.10F, 0.46F);
        add_layer(
            recipe, ProceduralAudioWaveform::Sine,
            0.02F, 1.60F, 276.0F * pitch, 264.0F * pitch,
            0.16F, 0.12F, 0.44F);
        break;
    case LeviathanAudioCue::ArenaDrum:
        add_layer(
            recipe, ProceduralAudioWaveform::Sine,
            0.0F, 0.52F, 82.0F * pitch, 38.0F * pitch,
            0.74F, 0.001F, 0.42F);
        add_layer(
            recipe, ProceduralAudioWaveform::Impulse,
            0.0F, 0.18F, 132.0F, 72.0F,
            0.32F, 0.001F, 0.12F);
        break;
    case LeviathanAudioCue::ChainStrain:
        add_layer(
            recipe, ProceduralAudioWaveform::Metallic,
            0.0F, 0.72F, 248.0F * pitch, 172.0F * pitch,
            0.42F, 0.02F, 0.42F);
        add_layer(
            recipe, ProceduralAudioWaveform::FilteredNoise,
            0.06F, 0.58F, 160.0F, 82.0F,
            0.18F, 0.02F, 0.36F, 0.08F);
        break;
    case LeviathanAudioCue::ChainBreak:
        add_layer(
            recipe, ProceduralAudioWaveform::Impulse,
            0.0F, 0.26F, 320.0F * pitch, 110.0F * pitch,
            0.70F, 0.001F, 0.18F);
        add_layer(
            recipe, ProceduralAudioWaveform::Metallic,
            0.03F, 0.88F, 420.0F * pitch, 148.0F * pitch,
            0.52F, 0.001F, 0.70F);
        break;
    default:
        // Je rabats un identifiant futur ou corrompu sur un balayage audible.
        add_sweep_layers(recipe, 1.0F, 1.0F, pitch);
        break;
    }

    recipe.low_pass_hz =
        request.cue == LeviathanAudioCue::CrowdMurmur
            ? 4'800.0F
            : 15'500.0F;
    return recipe;
}

auto leviathan_audio_recipe_is_valid(
    const LeviathanAudioRecipe& recipe) noexcept -> bool {
    if (recipe.layer_count == 0U ||
        recipe.layer_count > recipe.layers.size() ||
        !std::isfinite(recipe.duration_seconds) ||
        recipe.duration_seconds <= 0.0F ||
        recipe.duration_seconds > 3.0F ||
        !std::isfinite(recipe.master_gain) ||
        recipe.master_gain < 0.0F ||
        recipe.master_gain > 1.0F ||
        !std::isfinite(recipe.low_pass_hz) ||
        recipe.low_pass_hz < 20.0F ||
        recipe.low_pass_hz > 24'000.0F) {
        return false;
    }
    for (std::size_t index = 0U;
         index < recipe.layer_count;
         ++index) {
        const auto& layer = recipe.layers[index];
        if (!std::isfinite(layer.start_seconds) ||
            !std::isfinite(layer.duration_seconds) ||
            !std::isfinite(layer.frequency_start_hz) ||
            !std::isfinite(layer.frequency_end_hz) ||
            !std::isfinite(layer.gain) ||
            !std::isfinite(layer.attack_seconds) ||
            !std::isfinite(layer.release_seconds) ||
            !std::isfinite(layer.noise_filter) ||
            layer.start_seconds < 0.0F ||
            layer.duration_seconds <= 0.0F ||
            layer.frequency_start_hz <= 0.0F ||
            layer.frequency_end_hz <= 0.0F ||
            layer.gain < 0.0F ||
            layer.gain > 1.0F ||
            layer.attack_seconds < 0.0F ||
            layer.release_seconds < 0.0F ||
            layer.noise_filter < 0.0F ||
            layer.noise_filter > 1.0F) {
            return false;
        }
    }
    return true;
}

auto synthesize_leviathan_audio(
    const LeviathanAudioRecipe& recipe,
    int sample_rate) -> std::vector<float> {
    if (!leviathan_audio_recipe_is_valid(recipe)) {
        return {};
    }
    const auto safe_sample_rate =
        std::clamp(sample_rate, 8'000, 192'000);
    const auto sample_count = static_cast<std::size_t>(
        std::ceil(
            recipe.duration_seconds *
            static_cast<float>(safe_sample_rate)));
    auto output = std::vector<float>(sample_count, 0.0F);
    auto phases =
        std::array<float, kMaximumLeviathanAudioLayers> {};
    auto filtered_noise =
        std::array<float, kMaximumLeviathanAudioLayers> {};
    auto seeds =
        std::array<std::uint32_t, kMaximumLeviathanAudioLayers> {};
    for (std::size_t index = 0U;
         index < recipe.layer_count;
         ++index) {
        seeds[index] =
            recipe.seed ^ static_cast<std::uint32_t>(
                (index + 1U) * 0x9E3779B9U);
        if (seeds[index] == 0U) {
            seeds[index] = 1U;
        }
    }

    auto master_filter = 0.0F;
    const auto filter_amount = std::clamp(
        kTwoPi * recipe.low_pass_hz /
            static_cast<float>(safe_sample_rate),
        0.001F, 1.0F);
    for (std::size_t sample_index = 0U;
         sample_index < sample_count;
         ++sample_index) {
        const auto global_time =
            static_cast<float>(sample_index) /
            static_cast<float>(safe_sample_rate);
        auto mixed = 0.0F;
        for (std::size_t layer_index = 0U;
             layer_index < recipe.layer_count;
             ++layer_index) {
            const auto& layer = recipe.layers[layer_index];
            const auto local_time =
                global_time - layer.start_seconds;
            if (local_time < 0.0F ||
                local_time >= layer.duration_seconds) {
                continue;
            }
            const auto progress =
                local_time / layer.duration_seconds;
            const auto frequency =
                layer.frequency_start_hz +
                (layer.frequency_end_hz -
                 layer.frequency_start_hz) *
                    progress;
            phases[layer_index] +=
                kTwoPi * frequency /
                static_cast<float>(safe_sample_rate);
            if (phases[layer_index] > kTwoPi) {
                phases[layer_index] =
                    std::fmod(phases[layer_index], kTwoPi);
            }
            const auto noise = next_noise(seeds[layer_index]);
            filtered_noise[layer_index] +=
                (noise - filtered_noise[layer_index]) *
                layer.noise_filter;
            const auto attack =
                layer.attack_seconds > 1.0e-5F
                    ? std::min(
                          1.0F,
                          local_time / layer.attack_seconds)
                    : 1.0F;
            const auto remaining =
                layer.duration_seconds - local_time;
            const auto release =
                layer.release_seconds > 1.0e-5F
                    ? std::min(
                          1.0F,
                          remaining / layer.release_seconds)
                    : 1.0F;
            const auto envelope = attack * release;
            mixed += render_waveform(
                         layer.waveform,
                         phases[layer_index],
                         noise,
                         filtered_noise[layer_index]) *
                     envelope * layer.gain;
        }
        master_filter +=
            (mixed - master_filter) * filter_amount;
        output[sample_index] =
            std::tanh(master_filter * recipe.master_gain);
    }
    return output;
}

} // namespace valcraft
