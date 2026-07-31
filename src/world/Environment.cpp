#include "world/Environment.h"

#include "world/BackroomsGenerator.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace valcraft {

namespace {

constexpr float kFullDayDurationSeconds = 12.0F * 60.0F;
constexpr float kDefaultTimeOfDay = 8.0F;
constexpr float kWeatherSlotDurationSeconds = 240.0F;
constexpr float kWeatherTransitionSeconds = 42.0F;
constexpr float kWeatherEventStartOffsetSeconds =
    kWeatherSlotDurationSeconds * 0.5F;
constexpr float kPi = 3.14159265358979323846F;
constexpr float kTwoPi = 2.0F * kPi;

struct WeatherProfile {
    WeatherKind kind = WeatherKind::Clear;
    float cloud_intensity = 0.08F;
    float overcast_intensity = 0.0F;
    float precipitation_intensity = 0.0F;
    float storm_intensity = 0.0F;
    float cloud_shadow_strength = 0.04F;
    float wind_strength = 0.18F;
    float exposure_adjust = 0.03F;
    float saturation_adjust = 0.03F;
    float contrast_adjust = 0.02F;
    float fog_density_bonus = 0.0F;
    float violent_storm_intensity = 0.0F;
};

struct WeatherSample {
    WeatherKind kind = WeatherKind::Clear;
    float weather_time_seconds = 0.0F;
    float transition_factor = 1.0F;
    float cloud_intensity = 0.08F;
    float overcast_intensity = 0.0F;
    float precipitation_intensity = 0.0F;
    float storm_intensity = 0.0F;
    float violent_storm_intensity = 0.0F;
    float lightning_intensity = 0.0F;
    float lightning_bolt_intensity = 0.0F;
    glm::vec3 lightning_direction {0.0F, 0.35F, 0.93675F};
    float lightning_shape_seed = 0.0F;
    float cloud_shadow_strength = 0.04F;
    glm::vec2 wind_direction_xz {0.0F, 1.0F};
    float wind_strength = 0.18F;
    float exposure_adjust = 0.03F;
    float saturation_adjust = 0.03F;
    float contrast_adjust = 0.02F;
    float fog_density_bonus = 0.0F;
};

struct LightningSample {
    float flash_intensity = 0.0F;
    float bolt_intensity = 0.0F;
    glm::vec3 direction {0.0F, 0.35F, 0.93675F};
    float shape_seed = 0.0F;
};

auto saturate(float value) noexcept -> float {
    return glm::clamp(value, 0.0F, 1.0F);
}

auto smooth_curve(float edge0, float edge1, float value) noexcept -> float {
    const auto t = glm::clamp((value - edge0) / (edge1 - edge0), 0.0F, 1.0F);
    return t * t * (3.0F - 2.0F * t);
}

auto peaked_curve(float rise_edge0, float rise_edge1, float fall_edge0, float fall_edge1, float value) noexcept -> float {
    return smooth_curve(rise_edge0, rise_edge1, value) * (1.0F - smooth_curve(fall_edge0, fall_edge1, value));
}

auto blend_cycle_color(const glm::vec3& night_color,
                       const glm::vec3& twilight_color,
                       const glm::vec3& day_color,
                       float twilight_amount,
                       float daylight_amount) noexcept -> glm::vec3 {
    return glm::mix(glm::mix(night_color, twilight_color, saturate(twilight_amount)), day_color, saturate(daylight_amount));
}

auto hash_u32(std::uint32_t value) noexcept -> std::uint32_t {
    value ^= value >> 16U;
    value *= 0x7feb352dU;
    value ^= value >> 15U;
    value *= 0x846ca68bU;
    value ^= value >> 16U;
    return value;
}

auto slot_key(std::int64_t slot_index) noexcept -> std::uint32_t {
    return static_cast<std::uint32_t>(slot_index) ^ static_cast<std::uint32_t>(slot_index >> 32);
}

auto weather_random(std::uint32_t seed, std::int64_t slot_index, std::uint32_t salt) noexcept -> float {
    const auto mixed = hash_u32(seed ^ hash_u32(slot_key(slot_index) + 0x9e3779b9U) ^ hash_u32(salt + 0x85ebca6bU));
    return static_cast<float>(mixed >> 8U) * (1.0F / 16777216.0F);
}

auto safe_wind_direction(const glm::vec2& direction) noexcept -> glm::vec2 {
    if (!std::isfinite(direction.x) || !std::isfinite(direction.y)) {
        return {0.0F, 1.0F};
    }
    const auto length_squared = glm::dot(direction, direction);
    if (!std::isfinite(length_squared) || length_squared <= 1.0e-6F) {
        return {0.0F, 1.0F};
    }
    return direction / std::sqrt(length_squared);
}

auto wind_direction_for_slot(std::uint32_t seed, std::int64_t slot_index) noexcept -> glm::vec2 {
    const auto angle =
        weather_random(
            seed,
            slot_index,
            113U) *
        kTwoPi;
    return {
        std::cos(angle),
        std::sin(angle),
    };
}

auto blend_wind_direction(
    const glm::vec2& previous,
    const glm::vec2& current,
    float blend) noexcept -> glm::vec2 {
    const auto safe_previous =
        safe_wind_direction(
            previous);
    const auto safe_current =
        safe_wind_direction(
            current);
    const auto previous_angle =
        std::atan2(
            safe_previous.y,
            safe_previous.x);
    const auto current_angle =
        std::atan2(
            safe_current.y,
            safe_current.x);
    const auto angle_delta =
        std::atan2(
            std::sin(current_angle - previous_angle),
            std::cos(current_angle - previous_angle));
    const auto angle =
        previous_angle +
        angle_delta *
            saturate(
                blend);
    return safe_wind_direction({
        std::cos(angle),
        std::sin(angle),
    });
}

auto weather_kind_for_slot(std::uint32_t seed, std::int64_t slot_index) noexcept -> WeatherKind {
    if (slot_index <= 0) {
        return WeatherKind::Clear;
    }

    const auto roll = weather_random(seed, slot_index, 11U);
    if (roll < 0.38F) {
        return WeatherKind::Clear;
    }
    if (roll < 0.68F) {
        return WeatherKind::PartlyCloudy;
    }
    if (roll < 0.78F) {
        return WeatherKind::Overcast;
    }
    if (roll < 0.86F) {
        return WeatherKind::LightRain;
    }
    if (roll < 0.92F) {
        return WeatherKind::LightStorm;
    }
    if (roll < 0.96F) {
        return WeatherKind::HeavyStorm;
    }
    return WeatherKind::Tempest;
}

auto weather_profile(WeatherKind kind) noexcept -> WeatherProfile {
    switch (kind) {
    case WeatherKind::Clear:
        return {kind, 0.08F, 0.00F, 0.00F, 0.00F, 0.04F, 0.22F, 0.04F, 0.03F, 0.02F, 0.0000F, 0.00F};
    case WeatherKind::PartlyCloudy:
        return {kind, 0.38F, 0.08F, 0.00F, 0.00F, 0.15F, 0.26F, 0.01F, 0.01F, 0.01F, 0.0002F, 0.00F};
    case WeatherKind::Overcast:
        return {kind, 0.74F, 0.66F, 0.00F, 0.00F, 0.30F, 0.34F, -0.08F, -0.06F, -0.02F, 0.0009F, 0.00F};
    case WeatherKind::LightRain:
        return {kind, 0.84F, 0.76F, 0.44F, 0.00F, 0.36F, 0.42F, -0.12F, -0.10F, -0.03F, 0.0016F, 0.00F};
    case WeatherKind::LightStorm:
        return {kind, 0.92F, 0.88F, 0.58F, 0.34F, 0.46F, 0.50F, -0.18F, -0.16F, -0.05F, 0.0022F, 0.00F};
    case WeatherKind::HeavyStorm:
        return {kind, 0.98F, 0.94F, 0.82F, 0.68F, 0.58F, 0.68F, -0.25F, -0.22F, -0.07F, 0.0030F, 0.12F};
    case WeatherKind::Tempest:
        return {kind, 1.00F, 1.00F, 1.00F, 1.00F, 0.72F, 0.92F, -0.32F, -0.28F, -0.08F, 0.0040F, 1.00F};
    }
    return {};
}

auto blend_weather_profile(const WeatherProfile& previous, const WeatherProfile& current, float blend) noexcept
    -> WeatherSample {
    const auto t = saturate(blend);
    WeatherSample sample {};
    sample.kind = t < 0.5F ? previous.kind : current.kind;
    sample.transition_factor = t;
    sample.cloud_intensity = glm::mix(previous.cloud_intensity, current.cloud_intensity, t);
    sample.overcast_intensity = glm::mix(previous.overcast_intensity, current.overcast_intensity, t);
    sample.precipitation_intensity = glm::mix(previous.precipitation_intensity, current.precipitation_intensity, t);
    sample.storm_intensity = glm::mix(previous.storm_intensity, current.storm_intensity, t);
    sample.cloud_shadow_strength = glm::mix(previous.cloud_shadow_strength, current.cloud_shadow_strength, t);
    sample.wind_strength = glm::mix(previous.wind_strength, current.wind_strength, t);
    sample.exposure_adjust = glm::mix(previous.exposure_adjust, current.exposure_adjust, t);
    sample.saturation_adjust = glm::mix(previous.saturation_adjust, current.saturation_adjust, t);
    sample.contrast_adjust = glm::mix(previous.contrast_adjust, current.contrast_adjust, t);
    sample.fog_density_bonus = glm::mix(previous.fog_density_bonus, current.fog_density_bonus, t);
    sample.violent_storm_intensity =
        glm::mix(
            previous.violent_storm_intensity,
            current.violent_storm_intensity,
            t);
    return sample;
}

auto lightning_for_weather(std::uint32_t seed,
                           std::int64_t slot_index,
                           float weather_time_seconds,
                           float storm_intensity,
                           float violent_storm_intensity) noexcept
    -> LightningSample {
    constexpr float kLightningPeriodSeconds = 4.8F;
    const auto event_index = static_cast<std::int64_t>(std::floor(weather_time_seconds / kLightningPeriodSeconds));
    const auto period_position = weather_time_seconds / kLightningPeriodSeconds;
    const auto phase = period_position - std::floor(period_position);
    const auto azimuth =
        weather_random(seed, event_index, 71U) *
        kTwoPi;
    const auto elevation =
        0.12F +
        weather_random(seed, event_index, 83U) *
            0.38F;
    const auto horizontal_scale = std::cos(elevation);

    LightningSample sample {};
    sample.direction = glm::normalize(
        glm::vec3 {
            std::cos(azimuth) * horizontal_scale,
            std::sin(elevation),
            std::sin(azimuth) * horizontal_scale,
        });
    sample.shape_seed =
        weather_random(seed, event_index, 97U);

    // Je fais tendre l'eclair vers zero avec la tempete au lieu de couper son
    // pulse a un seuil binaire pendant les premieres frames d'une transition.
    const auto storm_presence =
        smooth_curve(
            0.0F,
            0.10F,
            storm_intensity);
    const auto violent = saturate(violent_storm_intensity);
    const auto chance =
        0.06F +
        storm_intensity * 0.28F +
        violent * 0.34F;
    const auto event_roll =
        weather_random(
            seed,
            event_index,
            31U);
    // Je fais entrer l'événement progressivement lorsque la météo transitoire
    // franchit son seuil. Un test booléen ferait sinon apparaître un éclair
    // déjà au maximum d'une frame à l'autre pendant un pulse en cours.
    const auto activation =
        smooth_curve(
            event_roll,
            event_roll + 0.04F,
            chance);
    if (activation <= 0.0F) {
        return sample;
    }

    const auto center = 0.18F + weather_random(seed, event_index, 47U) * 0.62F;
    const auto width =
        0.018F +
        storm_intensity * 0.025F +
        violent * 0.012F;
    const auto primary = saturate(1.0F - std::abs(phase - center) / width);
    const auto secondary = saturate(1.0F - std::abs(phase - (center + 0.075F)) / (width * 0.72F));
    const auto pulse = std::max(primary * primary, secondary * secondary * 0.45F);
    const auto strength =
        (0.35F + storm_intensity * 0.65F) *
        (0.70F +
         0.30F *
             weather_random(
                 seed,
                 slot_index,
                 59U)) *
        storm_presence;

    sample.flash_intensity =
        pulse *
        strength *
        activation;
    sample.bolt_intensity =
        std::max(
            primary * primary * primary,
            secondary * secondary * 0.32F) *
        strength *
        activation;
    return sample;
}

auto sanitize_weather_time_seconds(float weather_time_seconds) noexcept
    -> float {
    if (!std::isfinite(weather_time_seconds)) {
        return 0.0F;
    }
    return std::clamp(
        weather_time_seconds,
        0.0F,
        kMaximumWeatherTimeSeconds);
}

auto weather_sample_at(std::uint32_t seed, float weather_time_seconds) noexcept -> WeatherSample {
    const auto safe_time =
        sanitize_weather_time_seconds(
            weather_time_seconds);
    const auto slot_index = static_cast<std::int64_t>(std::floor(safe_time / kWeatherSlotDurationSeconds));
    const auto slot_start = static_cast<float>(slot_index) * kWeatherSlotDurationSeconds;
    const auto slot_progress = safe_time - slot_start;
    const auto blend =
        slot_progress < kWeatherTransitionSeconds ? smooth_curve(0.0F, kWeatherTransitionSeconds, slot_progress) : 1.0F;

    const auto previous = weather_profile(weather_kind_for_slot(seed, slot_index - 1));
    const auto current = weather_profile(weather_kind_for_slot(seed, slot_index));
    auto sample = blend_weather_profile(previous, current, blend);
    sample.weather_time_seconds = safe_time;
    sample.wind_direction_xz =
        blend_wind_direction(
            wind_direction_for_slot(
                seed,
                slot_index - 1),
            wind_direction_for_slot(
                seed,
                slot_index),
            blend);
    const auto lightning =
        lightning_for_weather(
            seed,
            slot_index,
            safe_time,
            sample.storm_intensity,
            sample.violent_storm_intensity);
    sample.lightning_intensity =
        lightning.flash_intensity;
    sample.lightning_bolt_intensity =
        lightning.bolt_intensity;
    sample.lightning_direction =
        lightning.direction;
    sample.lightning_shape_seed =
        lightning.shape_seed;
    return sample;
}

void apply_weather(EnvironmentState& state,
                   const WeatherSample& weather,
                   float daylight,
                   float twilight_presence,
                   float glow_presence,
                   float night_factor,
                   float low_sun) noexcept {
    state.weather = weather.kind;
    state.weather_time_seconds = weather.weather_time_seconds;
    state.weather_transition_factor = weather.transition_factor;
    state.cloud_intensity = saturate(weather.cloud_intensity * glm::mix(0.72F, 1.00F, daylight) + twilight_presence * 0.035F);
    state.overcast_intensity = saturate(weather.overcast_intensity);
    state.precipitation_intensity = saturate(weather.precipitation_intensity);
    state.storm_intensity = saturate(weather.storm_intensity);
    state.violent_storm_intensity =
        saturate(
            weather.violent_storm_intensity);
    state.lightning_intensity = saturate(weather.lightning_intensity);
    state.lightning_bolt_intensity =
        saturate(
            weather.lightning_bolt_intensity);
    state.lightning_direction =
        weather.lightning_direction;
    state.lightning_shape_seed =
        saturate(
            weather.lightning_shape_seed);
    state.cloud_shadow_strength =
        saturate(glm::mix(0.02F, 0.10F, daylight) + weather.cloud_shadow_strength * glm::mix(0.45F, 1.0F, daylight));
    state.wind_direction_xz =
        safe_wind_direction(
            weather.wind_direction_xz);
    state.wind_strength = saturate(std::max(state.wind_strength * 0.62F, weather.wind_strength + low_sun * 0.03F));

    const auto grey_strength =
        saturate(
            state.overcast_intensity *
                (0.54F + daylight * 0.40F) +
            state.violent_storm_intensity * 0.18F);
    const auto storm_grade =
        saturate(
            state.storm_intensity +
            state.precipitation_intensity * 0.20F +
            state.violent_storm_intensity * 0.28F);
    const auto grey_zenith = glm::mix(
        glm::vec3 {0.48F, 0.56F, 0.66F},
        glm::vec3 {0.13F, 0.15F, 0.19F},
        storm_grade);
    const auto grey_horizon = glm::mix(
        glm::vec3 {0.64F, 0.69F, 0.74F},
        glm::vec3 {0.25F, 0.27F, 0.30F},
        storm_grade);
    const auto grey_fog = glm::mix(
        glm::vec3 {0.56F, 0.62F, 0.68F},
        glm::vec3 {0.19F, 0.22F, 0.25F},
        storm_grade);
    state.sky_zenith_color = glm::mix(state.sky_zenith_color, grey_zenith, grey_strength);
    state.sky_horizon_color = glm::mix(state.sky_horizon_color, grey_horizon, saturate(grey_strength * 0.86F + state.precipitation_intensity * 0.12F));
    state.sky_color = glm::mix(state.sky_horizon_color, state.sky_zenith_color, 0.54F);
    state.fog_color = glm::mix(state.fog_color, grey_fog, saturate(grey_strength * 0.50F + state.precipitation_intensity * 0.24F));
    state.distant_fog_color = glm::mix(
        state.distant_fog_color,
        glm::mix(glm::vec3 {0.52F, 0.60F, 0.68F}, glm::vec3 {0.24F, 0.29F, 0.36F}, storm_grade),
        saturate(grey_strength * 0.58F + state.precipitation_intensity * 0.18F));

    const auto sun_loss =
        saturate(
            state.overcast_intensity * 0.34F +
            state.precipitation_intensity * 0.18F +
            state.storm_intensity * 0.24F +
            state.violent_storm_intensity * 0.12F);
    state.sun_color *= 1.0F - sun_loss * saturate(daylight + twilight_presence * 0.25F);
    state.sun_disk_color = glm::mix(state.sun_disk_color, glm::vec3 {0.78F, 0.82F, 0.88F}, saturate(grey_strength * 0.65F));
    state.horizon_glow_color =
        glm::mix(state.horizon_glow_color, grey_horizon, saturate(state.overcast_intensity * 0.55F + state.storm_intensity * 0.22F));
    state.ambient_color = glm::mix(
        state.ambient_color,
        glm::mix(glm::vec3 {0.36F, 0.41F, 0.48F}, glm::vec3 {0.13F, 0.15F, 0.19F}, storm_grade),
        saturate(grey_strength * 0.46F));
    state.night_tint_color =
        glm::mix(state.night_tint_color, glm::vec3 {0.06F, 0.09F, 0.16F}, saturate(state.storm_intensity * 0.34F));

    state.atmospheric_scatter_strength +=
        state.overcast_intensity * 0.010F + state.precipitation_intensity * 0.008F + state.storm_intensity * 0.010F;
    state.height_fog_density += weather.fog_density_bonus;
    state.exposure = glm::clamp(state.exposure + weather.exposure_adjust, 0.62F, 1.12F);
    state.saturation_boost = glm::clamp(state.saturation_boost + weather.saturation_adjust, 0.72F, 1.10F);
    state.contrast = glm::clamp(state.contrast + weather.contrast_adjust, 0.96F, 1.16F);
    state.vignette_strength = glm::clamp(state.vignette_strength + state.storm_intensity * 0.06F + state.precipitation_intensity * 0.02F, 0.08F, 0.30F);
    state.glow_threshold = glm::clamp(state.glow_threshold - state.storm_intensity * 0.06F - state.lightning_intensity * 0.08F, 0.46F, 0.80F);
    state.glow_strength = glm::clamp(state.glow_strength + state.storm_intensity * 0.08F + state.lightning_intensity * 0.24F, 0.16F, 0.58F);
    state.post_sharpen_strength = glm::clamp(state.post_sharpen_strength - state.precipitation_intensity * 0.04F, 0.08F, 0.20F);
    state.post_edge_strength = glm::clamp(state.post_edge_strength + state.precipitation_intensity * 0.03F + state.storm_intensity * 0.03F, 0.09F, 0.22F);

    const auto lightning_color = glm::vec3 {0.62F, 0.72F, 1.0F} * state.lightning_intensity;
    state.sun_color += lightning_color * 0.38F;
    state.ambient_color += lightning_color * (0.08F + night_factor * 0.12F);
    state.sky_zenith_color += lightning_color * (0.10F + glow_presence * 0.05F);
    state.sky_horizon_color += lightning_color * 0.08F;
}

} // namespace


auto make_backrooms_environment_state(
    float elapsed_seconds,
    int seed,
    float player_x,
    float player_z,
    bool poolrooms) noexcept -> EnvironmentState {

    const auto safe_coordinate = [](float value) noexcept -> double {
        if (!std::isfinite(value)) {
            return 0.0;
        }
        return static_cast<double>(
            std::clamp(
                value,
                -1'000'000.0F,
                1'000'000.0F));
    };

    const auto safe_elapsed =
        std::isfinite(elapsed_seconds)
            ? std::clamp(
                  elapsed_seconds,
                  0.0F,
                  kMaximumWeatherTimeSeconds)
            : 0.0F;
    const BackroomsGenerator generator {seed};

    // La variation reste lente et de faible amplitude : elle entretient une
    // instabilité perceptible sans stroboscope ni rupture brutale de lisibilité.
    const auto seed_phase =
        static_cast<float>(
            static_cast<std::uint32_t>(seed) % 997U) /
        997.0F;
    const auto electrical_drift =
        0.965F +
        std::sin(
            safe_elapsed * 0.17F +
            seed_phase * kTwoPi) *
            0.018F +
        std::sin(
            safe_elapsed * 0.043F +
            seed_phase * 3.7F) *
            0.012F;

    struct ModuleLighting {
        glm::vec3 ambient_tint {0.0F};
        glm::vec3 fog_tint {0.0F};
        float exposure = 0.0F;
        float fog_density = 0.0F;
        float vignette = 0.0F;
    };

    const auto module_lighting =
        [&](int module_x, int module_z) noexcept {
            const auto descriptor =
                generator.module_descriptor(module_x, module_z);
            ModuleLighting lighting {
                .ambient_tint = {0.105F, 0.101F, 0.068F},
                .fog_tint = {0.145F, 0.137F, 0.082F},
                .exposure = 0.94F * electrical_drift,
                .fog_density = 0.0065F,
                .vignette = 0.12F,
            };

            // Je représente ici le rebond diffus des rampes sur la moquette et
            // les murs sans modifier la palette propre aux modules éloignés.
            switch (descriptor.palette) {
            case BackroomsPalette::SickGreen:
                lighting.ambient_tint = {0.078F, 0.101F, 0.073F};
                lighting.fog_tint = {0.105F, 0.142F, 0.094F};
                break;
            case BackroomsPalette::WashedBlue:
                lighting.ambient_tint = {0.073F, 0.087F, 0.101F};
                lighting.fog_tint = {0.091F, 0.116F, 0.132F};
                break;
            case BackroomsPalette::FadedRose:
                lighting.ambient_tint = {0.101F, 0.073F, 0.071F};
                lighting.fog_tint = {0.137F, 0.094F, 0.092F};
                break;
            case BackroomsPalette::Oxide:
                lighting.ambient_tint = {0.098F, 0.068F, 0.050F};
                lighting.fog_tint = {0.131F, 0.086F, 0.058F};
                break;
            case BackroomsPalette::RawConcrete:
                lighting.ambient_tint = {0.083F, 0.083F, 0.078F};
                lighting.fog_tint = {0.105F, 0.105F, 0.096F};
                break;
            case BackroomsPalette::NicotineYellow:
            default:
                break;
            }

            // Je laisse la tension assombrir progressivement la scène sans
            // sacrifier les basses lumières au contraste du post-traitement.
            switch (descriptor.tension) {
            case BackroomsTension::Familiarity:
                lighting.exposure *= 1.04F;
                lighting.fog_density = 0.0045F;
                lighting.vignette = 0.08F;
                break;
            case BackroomsTension::Compression:
                lighting.exposure *= 0.93F;
                lighting.fog_density = 0.0080F;
                lighting.vignette = 0.15F;
                break;
            case BackroomsTension::Expansion:
                lighting.exposure *= 0.98F;
                lighting.fog_density = 0.0105F;
                lighting.vignette = 0.11F;
                break;
            case BackroomsTension::Repetition:
                lighting.exposure *= 0.95F;
                lighting.fog_density = 0.0070F;
                lighting.vignette = 0.13F;
                break;
            case BackroomsTension::Anomaly:
                lighting.exposure *= 0.89F;
                lighting.fog_density = 0.0090F;
                lighting.vignette = 0.17F;
                break;
            case BackroomsTension::Blackout:
                lighting.exposure *= 0.75F;
                lighting.fog_density = 0.0120F;
                lighting.vignette = 0.23F;
                break;
            }
            lighting.exposure =
                std::clamp(lighting.exposure, 0.66F, 1.02F);
            return lighting;
        };

    struct AxisModuleBlend {
        int first_module = 0;
        int second_module = 0;
        float first_weight = 1.0F;
        float second_weight = 0.0F;
    };

    const auto axis_module_blend =
        [](double world_coordinate) noexcept {
            constexpr auto kBlendRadius = 4.0;
            constexpr auto kModuleSize =
                static_cast<double>(kBackroomsModuleSize);
            const auto module_floor =
                std::floor(world_coordinate / kModuleSize);
            const auto module = static_cast<int>(module_floor);
            const auto local_coordinate =
                world_coordinate - module_floor * kModuleSize;
            const auto neighbour_weight =
                [](double boundary_distance) noexcept {
                    constexpr auto kBlendRadius = 4.0;
                    const auto normalized =
                        std::clamp(
                            boundary_distance / kBlendRadius,
                            0.0,
                            1.0);
                    const auto eased =
                        normalized * normalized *
                        (3.0 - 2.0 * normalized);
                    return static_cast<float>(
                        0.5 * (1.0 - eased));
                };

            AxisModuleBlend blend {
                .first_module = module,
                .second_module = module,
                .first_weight = 1.0F,
                .second_weight = 0.0F,
            };
            if (local_coordinate < kBlendRadius) {
                const auto weight =
                    neighbour_weight(local_coordinate);
                blend.first_module = module - 1;
                blend.second_module = module;
                blend.first_weight = weight;
                blend.second_weight = 1.0F - weight;
            } else if (
                kModuleSize - local_coordinate <
                kBlendRadius) {
                const auto weight =
                    neighbour_weight(
                        kModuleSize - local_coordinate);
                blend.first_module = module;
                blend.second_module = module + 1;
                blend.first_weight = 1.0F - weight;
                blend.second_weight = weight;
            }
            return blend;
        };

    const auto x_blend =
        axis_module_blend(safe_coordinate(player_x));
    const auto z_blend =
        axis_module_blend(safe_coordinate(player_z));
    ModuleLighting blended_lighting {};
    for (int z_index = 0; z_index < 2; ++z_index) {
        const auto module_z =
            z_index == 0
                ? z_blend.first_module
                : z_blend.second_module;
        const auto z_weight =
            z_index == 0
                ? z_blend.first_weight
                : z_blend.second_weight;
        for (int x_index = 0; x_index < 2; ++x_index) {
            const auto module_x =
                x_index == 0
                    ? x_blend.first_module
                    : x_blend.second_module;
            const auto x_weight =
                x_index == 0
                    ? x_blend.first_weight
                    : x_blend.second_weight;
            const auto weight = x_weight * z_weight;
            if (!(weight > 0.0F)) {
                continue;
            }
            const auto lighting =
                module_lighting(module_x, module_z);
            blended_lighting.ambient_tint +=
                lighting.ambient_tint * weight;
            blended_lighting.fog_tint +=
                lighting.fog_tint * weight;
            blended_lighting.exposure +=
                lighting.exposure * weight;
            blended_lighting.fog_density +=
                lighting.fog_density * weight;
            blended_lighting.vignette +=
                lighting.vignette * weight;
        }
    }

    // Je combine deux modules près d'une arête et quatre près d'un angle.
    // Je place exactement la moitié de chaque poids sur la frontière pour
    // obtenir le même résultat des deux côtés, puis je restitue le module
    // courant après quatre mètres.
    const auto ambient_tint = blended_lighting.ambient_tint;
    const auto fog_tint = blended_lighting.fog_tint;
    const auto exposure = blended_lighting.exposure;
    const auto fog_density = blended_lighting.fog_density;
    const auto vignette = blended_lighting.vignette;

    EnvironmentState state {};
    state.time_of_day = 0.0F;
    state.weather_time_seconds = safe_elapsed;
    state.daylight_factor = 0.0F;
    state.weather = WeatherKind::Clear;
    state.sun_direction = {0.0F, -1.0F, 0.0F};
    state.sun_color = {0.0F, 0.0F, 0.0F};
    state.ambient_color = ambient_tint;
    state.block_light_color = {0.94F, 1.00F, 0.82F};
    state.fog_color = fog_tint;
    state.sky_color = fog_tint * 0.16F;
    state.sky_zenith_color = fog_tint * 0.10F;
    state.sky_horizon_color = fog_tint * 0.20F;
    state.horizon_glow_color = {0.0F, 0.0F, 0.0F};
    state.distant_fog_color = fog_tint * 0.82F;
    // Je conserve l'ambiance comme teinte de rebond pour les surfaces
    // réellement éclairées, mais je n'ajoute aucune lumière nocturne globale :
    // un noir absolu doit rester noir jusque dans le post-traitement.
    state.night_tint_color = {0.0F, 0.0F, 0.0F};
    state.sun_disk_color = {0.0F, 0.0F, 0.0F};
    state.moon_disk_color = {0.0F, 0.0F, 0.0F};
    state.star_intensity = 0.0F;
    state.cloud_intensity = 0.0F;
    state.overcast_intensity = 0.0F;
    state.precipitation_intensity = 0.0F;
    state.storm_intensity = 0.0F;
    state.violent_storm_intensity = 0.0F;
    state.lightning_intensity = 0.0F;
    state.lightning_bolt_intensity = 0.0F;
    state.weather_transition_factor = 1.0F;
    state.cloud_shadow_strength = 0.0F;
    state.wind_direction_xz = {0.0F, 0.0F};
    state.wind_strength = 0.0F;
    state.atmospheric_scatter_strength = 0.010F;
    state.height_fog_density = fog_density;
    state.exposure = exposure;
    state.saturation_boost = 0.88F;
    state.contrast = 1.01F;
    state.vignette_strength = vignette;
    state.glow_threshold = 0.78F;
    state.glow_strength = 0.13F;
    state.post_sharpen_strength = 0.07F;
    state.post_edge_strength = 0.04F;
    state.suppress_gameplay_hud = true;
    state.enclosed_interior = true;
    state.poolrooms = poolrooms;

    if (poolrooms) {
        // Je remplace le rebond jaune des bureaux par une lumière froide et
        // humide. La visibilité finale dépend toujours des vraies lampes et de
        // la Maglite : cette palette ne crée aucune lumière dans le noir total.
        state.ambient_color =
            glm::mix(
                glm::vec3 {0.024F, 0.050F, 0.055F},
                glm::vec3 {0.044F, 0.078F, 0.080F},
                std::clamp(electrical_drift, 0.0F, 1.0F));
        state.block_light_color = {0.67F, 0.91F, 0.96F};
        state.fog_color = {0.016F, 0.043F, 0.048F};
        state.distant_fog_color = {0.009F, 0.026F, 0.031F};
        state.sky_color = state.fog_color * 0.10F;
        state.sky_zenith_color = state.fog_color * 0.07F;
        state.sky_horizon_color = state.fog_color * 0.13F;
        state.height_fog_density =
            std::clamp(fog_density * 0.82F, 0.0040F, 0.0105F);
        state.exposure =
            std::clamp(exposure * 0.86F, 0.58F, 0.86F);
        // Je garde le carrelage froid sans délaver le cyan de l'eau : cette
        // saturation ne relève aucune zone noire, elle ne colore que les
        // surfaces réellement atteintes par un néon ou la Maglite.
        state.saturation_boost = 0.96F;
        state.contrast = 1.035F;
        state.vignette_strength =
            std::clamp(vignette + 0.045F, 0.12F, 0.27F);
        state.glow_threshold = 0.82F;
        state.glow_strength = 0.16F;
        state.post_sharpen_strength = 0.08F;
        state.post_edge_strength = 0.035F;
    }
    return state;
}

EnvironmentClock::EnvironmentClock(float initial_time_of_day, bool frozen, std::uint32_t weather_seed)
    : time_of_day_(normalize_time_of_day(initial_time_of_day)),
      weather_seed_(weather_seed),
      frozen_(frozen) {
}

void EnvironmentClock::update(float dt) {
    if (frozen_) {
        return;
    }

    const auto day_fraction = dt / kFullDayDurationSeconds;
    set_time_of_day(time_of_day_ + day_fraction * 24.0F);
    set_weather_time_seconds(weather_time_seconds_ + dt);
}

void EnvironmentClock::set_frozen(bool frozen) noexcept {
    frozen_ = frozen;
}

void EnvironmentClock::set_time_of_day(float time_of_day) noexcept {
    time_of_day_ = normalize_time_of_day(time_of_day);
}

void EnvironmentClock::set_weather_seed(std::uint32_t weather_seed) noexcept {
    weather_seed_ = weather_seed;
}

void EnvironmentClock::set_weather_time_seconds(float weather_time_seconds) noexcept {
    weather_time_seconds_ =
        sanitize_weather_time_seconds(
            weather_time_seconds);
}

auto EnvironmentClock::start_weather_event(WeatherKind weather) noexcept
    -> bool {
    constexpr std::int64_t kMaximumSearchSlots = 4'096;
    const auto maximum_slot =
        static_cast<std::int64_t>(
            std::floor(
                (kMaximumWeatherTimeSeconds -
                 kWeatherEventStartOffsetSeconds) /
                kWeatherSlotDurationSeconds));
    if (maximum_slot < 1) {
        return false;
    }

    auto first_slot =
        static_cast<std::int64_t>(
            std::floor(
                weather_time_seconds_ /
                kWeatherSlotDurationSeconds));
    first_slot = std::clamp<std::int64_t>(
        first_slot,
        1,
        maximum_slot);

    for (std::int64_t offset = 0;
         offset < kMaximumSearchSlots;
         ++offset) {
        const auto candidate_slot =
            1 +
            ((first_slot - 1 + offset) %
             maximum_slot);
        if (weather_kind_for_slot(
                weather_seed_,
                candidate_slot) != weather) {
            continue;
        }

        // Je vise le milieu du creneau : meme lorsque la precision du float
        // diminue en tres longue session, je reste loin des deux transitions.
        set_weather_time_seconds(
            static_cast<float>(
                static_cast<double>(candidate_slot) *
                        static_cast<double>(
                            kWeatherSlotDurationSeconds) +
                    static_cast<double>(
                        kWeatherEventStartOffsetSeconds)));
        return true;
    }
    return false;
}

auto EnvironmentClock::is_frozen() const noexcept -> bool {
    return frozen_;
}

auto EnvironmentClock::time_of_day() const noexcept -> float {
    return time_of_day_;
}

auto EnvironmentClock::weather_time_seconds() const noexcept -> float {
    return weather_time_seconds_;
}

auto EnvironmentClock::current_state() const -> EnvironmentState {
    return compute_state(time_of_day_, weather_seed_, weather_time_seconds_);
}

auto EnvironmentClock::current_creature_cycle() const noexcept -> CreatureCycleState {
    return classify_creature_cycle(time_of_day_);
}

auto EnvironmentClock::normalize_time_of_day(float time_of_day) noexcept -> float {
    if (!std::isfinite(time_of_day)) {
        return kDefaultTimeOfDay;
    }

    auto wrapped = std::fmod(time_of_day, 24.0F);
    if (wrapped < 0.0F) {
        wrapped += 24.0F;
    }
    return wrapped;
}

auto EnvironmentClock::compute_state(float time_of_day) -> EnvironmentState {
    return compute_state(time_of_day, 1337U, 0.0F);
}

auto EnvironmentClock::compute_state(float time_of_day, std::uint32_t weather_seed, float weather_time_seconds)
    -> EnvironmentState {
    const auto normalized_time = normalize_time_of_day(time_of_day);
    const auto solar_angle = ((normalized_time - 6.0F) / 24.0F) * kTwoPi;
    const auto sun_height = std::sin(solar_angle);
    const auto sun_x = std::cos(solar_angle) * 0.55F;
    const auto sun_z = std::sin(solar_angle * 0.5F + 0.65F) * 0.45F;

    EnvironmentState state {};
    state.time_of_day = normalized_time;
    state.sun_direction = glm::normalize(glm::vec3 {sun_x, sun_height, sun_z});

    const auto daylight = smooth_curve(-0.16F, 0.28F, sun_height);
    const auto twilight = peaked_curve(-0.22F, 0.06F, 0.14F, 0.62F, sun_height);
    const auto horizon_glow = peaked_curve(-0.24F, 0.04F, 0.18F, 0.52F, sun_height);
    const auto night_factor = 1.0F - smooth_curve(-0.24F, 0.05F, sun_height);
    const auto low_sun = 1.0F - smooth_curve(0.18F, 0.74F, std::abs(sun_height));
    const auto twilight_presence = saturate(twilight + low_sun * 0.22F * (1.0F - daylight));
    const auto glow_presence = saturate(horizon_glow + low_sun * 0.30F * (1.0F - daylight));
    const auto cinematic_twilight = saturate(glow_presence * 0.78F + twilight_presence * 0.36F);

    state.daylight_factor = glm::mix(0.18F, 1.0F, daylight);

    const auto day_sun = glm::vec3 {1.00F, 0.96F, 0.88F};
    const auto twilight_sun = glm::vec3 {1.00F, 0.48F, 0.20F};
    const auto night_sun = glm::vec3 {0.18F, 0.22F, 0.34F};
    state.sun_color = blend_cycle_color(night_sun, twilight_sun, day_sun, glow_presence, daylight);

    const auto day_ambient = glm::vec3 {0.44F, 0.51F, 0.60F};
    const auto twilight_ambient = glm::vec3 {0.32F, 0.20F, 0.24F};
    const auto night_ambient = glm::vec3 {0.11F, 0.13F, 0.19F};
    state.ambient_color = blend_cycle_color(night_ambient, twilight_ambient, day_ambient, twilight_presence, daylight);

    const auto day_fog = glm::vec3 {0.52F, 0.68F, 0.86F};
    const auto twilight_fog = glm::vec3 {0.64F, 0.30F, 0.28F};
    const auto night_fog = glm::vec3 {0.05F, 0.07F, 0.13F};
    state.fog_color = blend_cycle_color(night_fog, twilight_fog, day_fog, twilight_presence, daylight);

    const auto day_zenith = glm::vec3 {0.13F, 0.47F, 0.94F};
    const auto twilight_zenith = glm::vec3 {0.42F, 0.22F, 0.50F};
    const auto night_zenith = glm::vec3 {0.01F, 0.03F, 0.09F};
    state.sky_zenith_color = blend_cycle_color(night_zenith, twilight_zenith, day_zenith, twilight_presence * 0.92F, daylight);

    const auto day_horizon = glm::vec3 {0.72F, 0.88F, 1.00F};
    const auto twilight_horizon = glm::vec3 {1.00F, 0.34F, 0.15F};
    const auto night_horizon = glm::vec3 {0.07F, 0.10F, 0.18F};
    state.sky_horizon_color = blend_cycle_color(night_horizon, twilight_horizon, day_horizon, glow_presence, daylight);

    state.sky_color = glm::mix(state.sky_horizon_color, state.sky_zenith_color, 0.54F);
    state.horizon_glow_color = glm::mix(glm::vec3 {0.17F, 0.22F, 0.40F}, glm::vec3 {1.00F, 0.25F, 0.08F}, cinematic_twilight);
    state.distant_fog_color = blend_cycle_color(
        glm::vec3 {0.14F, 0.18F, 0.30F},
        glm::vec3 {0.72F, 0.34F, 0.30F},
        glm::vec3 {0.60F, 0.76F, 0.92F},
        twilight_presence,
        daylight);
    state.night_tint_color = glm::mix(
        glm::vec3 {0.10F, 0.15F, 0.28F},
        glm::vec3 {0.05F, 0.07F, 0.13F},
        saturate(daylight + glow_presence * 0.18F));
    state.sun_disk_color = glm::mix(
        glm::vec3 {1.00F, 0.38F, 0.14F},
        glm::vec3 {1.00F, 0.95F, 0.78F},
        smooth_curve(-0.04F, 0.54F, sun_height));
    state.moon_disk_color = glm::mix(glm::vec3 {0.68F, 0.78F, 0.92F}, glm::vec3 {0.92F, 0.96F, 1.00F}, night_factor);
    state.star_intensity = saturate(night_factor * (1.0F - glow_presence * 0.82F));
    state.cloud_intensity =
        glm::mix(0.18F, 0.58F, daylight) * glm::mix(0.74F, 1.00F, 1.0F - night_factor) + twilight_presence * 0.06F;
    state.cloud_shadow_strength = glm::mix(0.08F, 0.18F, daylight) + twilight_presence * 0.04F;
    state.wind_strength = glm::mix(0.20F, 0.34F, saturate(state.cloud_intensity)) + low_sun * 0.04F;
    state.atmospheric_scatter_strength = glm::mix(0.035F, 0.070F, daylight) + glow_presence * 0.088F;
    state.height_fog_density = glm::mix(0.0060F, 0.0025F, daylight) + glow_presence * 0.0019F;
    state.exposure = glm::mix(0.84F, 1.06F, daylight) + cinematic_twilight * 0.065F;
    state.saturation_boost = glm::mix(0.95F, 1.04F, daylight) + cinematic_twilight * 0.12F;
    state.contrast = glm::mix(1.06F, 1.12F, daylight) + twilight_presence * 0.04F;
    state.vignette_strength = glm::mix(0.22F, 0.10F, daylight);
    state.glow_threshold = glm::mix(0.55F, 0.78F, daylight) - cinematic_twilight * 0.055F;
    state.glow_strength = glm::mix(0.30F, 0.20F, daylight) + cinematic_twilight * 0.14F;
    state.post_sharpen_strength = glm::mix(0.10F, 0.18F, daylight);
    state.post_edge_strength = glm::mix(0.17F, 0.11F, daylight) + twilight_presence * 0.04F;

    apply_weather(
        state,
        weather_sample_at(weather_seed, weather_time_seconds),
        daylight,
        twilight_presence,
        glow_presence,
        night_factor,
        low_sun);

    return state;
}

auto EnvironmentClock::classify_creature_cycle(float time_of_day) noexcept -> CreatureCycleState {
    const auto normalized_time = normalize_time_of_day(time_of_day);

    if (normalized_time >= 18.0F && normalized_time < 19.0F) {
        return {
            CreaturePhase::DuskMorph,
            glm::clamp(normalized_time - 18.0F, 0.0F, 1.0F),
        };
    }
    if (normalized_time >= 19.0F || normalized_time < 5.0F) {
        return {
            CreaturePhase::Night,
            1.0F,
        };
    }
    if (normalized_time >= 5.0F && normalized_time < 6.0F) {
        return {
            CreaturePhase::DawnRecover,
            glm::clamp(6.0F - normalized_time, 0.0F, 1.0F),
        };
    }

    return {
        CreaturePhase::Day,
        0.0F,
    };
}

} // namespace valcraft
