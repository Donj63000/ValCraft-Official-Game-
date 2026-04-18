#include "world/Environment.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>

#include <cmath>

namespace valcraft {

namespace {

constexpr float kFullDayDurationSeconds = 12.0F * 60.0F;
constexpr float kPi = 3.14159265358979323846F;
constexpr float kTwoPi = 2.0F * kPi;

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

} // namespace

EnvironmentClock::EnvironmentClock(float initial_time_of_day, bool frozen)
    : time_of_day_(normalize_time_of_day(initial_time_of_day)),
      frozen_(frozen) {
}

void EnvironmentClock::update(float dt) {
    if (frozen_) {
        return;
    }

    const auto day_fraction = dt / kFullDayDurationSeconds;
    set_time_of_day(time_of_day_ + day_fraction * 24.0F);
}

void EnvironmentClock::set_frozen(bool frozen) noexcept {
    frozen_ = frozen;
}

void EnvironmentClock::set_time_of_day(float time_of_day) noexcept {
    time_of_day_ = normalize_time_of_day(time_of_day);
}

auto EnvironmentClock::is_frozen() const noexcept -> bool {
    return frozen_;
}

auto EnvironmentClock::time_of_day() const noexcept -> float {
    return time_of_day_;
}

auto EnvironmentClock::current_state() const -> EnvironmentState {
    return compute_state(time_of_day_);
}

auto EnvironmentClock::current_creature_cycle() const noexcept -> CreatureCycleState {
    return classify_creature_cycle(time_of_day_);
}

auto EnvironmentClock::normalize_time_of_day(float time_of_day) noexcept -> float {
    auto wrapped = std::fmod(time_of_day, 24.0F);
    if (wrapped < 0.0F) {
        wrapped += 24.0F;
    }
    return wrapped;
}

auto EnvironmentClock::compute_state(float time_of_day) -> EnvironmentState {
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
    const auto twilight_presence = saturate(twilight + low_sun * 0.18F * (1.0F - daylight));
    const auto glow_presence = saturate(horizon_glow + low_sun * 0.22F * (1.0F - daylight));

    state.daylight_factor = glm::mix(0.18F, 1.0F, daylight);

    const auto day_sun = glm::vec3 {1.00F, 0.96F, 0.88F};
    const auto twilight_sun = glm::vec3 {1.00F, 0.67F, 0.38F};
    const auto night_sun = glm::vec3 {0.18F, 0.22F, 0.34F};
    state.sun_color = blend_cycle_color(night_sun, twilight_sun, day_sun, glow_presence, daylight);

    const auto day_ambient = glm::vec3 {0.44F, 0.51F, 0.60F};
    const auto twilight_ambient = glm::vec3 {0.26F, 0.23F, 0.28F};
    const auto night_ambient = glm::vec3 {0.11F, 0.13F, 0.19F};
    state.ambient_color = blend_cycle_color(night_ambient, twilight_ambient, day_ambient, twilight_presence, daylight);

    const auto day_fog = glm::vec3 {0.68F, 0.80F, 0.94F};
    const auto twilight_fog = glm::vec3 {0.52F, 0.41F, 0.45F};
    const auto night_fog = glm::vec3 {0.05F, 0.07F, 0.13F};
    state.fog_color = blend_cycle_color(night_fog, twilight_fog, day_fog, twilight_presence, daylight);

    const auto day_zenith = glm::vec3 {0.13F, 0.47F, 0.94F};
    const auto twilight_zenith = glm::vec3 {0.21F, 0.30F, 0.56F};
    const auto night_zenith = glm::vec3 {0.01F, 0.03F, 0.09F};
    state.sky_zenith_color = blend_cycle_color(night_zenith, twilight_zenith, day_zenith, twilight_presence * 0.92F, daylight);

    const auto day_horizon = glm::vec3 {0.72F, 0.88F, 1.00F};
    const auto twilight_horizon = glm::vec3 {1.00F, 0.60F, 0.36F};
    const auto night_horizon = glm::vec3 {0.07F, 0.10F, 0.18F};
    state.sky_horizon_color = blend_cycle_color(night_horizon, twilight_horizon, day_horizon, glow_presence, daylight);

    state.sky_color = glm::mix(state.sky_horizon_color, state.sky_zenith_color, 0.54F);
    state.horizon_glow_color = glm::mix(glm::vec3 {0.17F, 0.22F, 0.40F}, glm::vec3 {1.00F, 0.58F, 0.26F}, glow_presence);
    state.distant_fog_color = blend_cycle_color(
        glm::vec3 {0.14F, 0.18F, 0.30F},
        glm::vec3 {0.64F, 0.50F, 0.50F},
        glm::vec3 {0.88F, 0.94F, 0.99F},
        twilight_presence,
        daylight);
    state.night_tint_color = glm::mix(
        glm::vec3 {0.10F, 0.15F, 0.28F},
        glm::vec3 {0.05F, 0.07F, 0.13F},
        saturate(daylight + glow_presence * 0.18F));
    state.sun_disk_color = glm::mix(
        glm::vec3 {1.00F, 0.62F, 0.32F},
        glm::vec3 {1.00F, 0.95F, 0.78F},
        smooth_curve(-0.04F, 0.54F, sun_height));
    state.moon_disk_color = glm::mix(glm::vec3 {0.68F, 0.78F, 0.92F}, glm::vec3 {0.92F, 0.96F, 1.00F}, night_factor);
    state.star_intensity = saturate(night_factor * (1.0F - glow_presence * 0.82F));
    state.cloud_intensity =
        glm::mix(0.18F, 0.58F, daylight) * glm::mix(0.74F, 1.00F, 1.0F - night_factor) + twilight_presence * 0.06F;
    state.cloud_shadow_strength = glm::mix(0.08F, 0.18F, daylight) + twilight_presence * 0.04F;
    state.wind_strength = glm::mix(0.20F, 0.34F, saturate(state.cloud_intensity)) + low_sun * 0.04F;
    state.atmospheric_scatter_strength = glm::mix(0.10F, 0.18F, daylight) + glow_presence * 0.16F;
    state.height_fog_density = glm::mix(0.020F, 0.012F, daylight) + glow_presence * 0.005F;
    state.exposure = glm::mix(0.84F, 1.06F, daylight) + glow_presence * 0.05F;
    state.saturation_boost = glm::mix(0.95F, 1.04F, daylight) + twilight_presence * 0.08F;
    state.contrast = glm::mix(1.06F, 1.12F, daylight) + twilight_presence * 0.03F;
    state.vignette_strength = glm::mix(0.22F, 0.10F, daylight);
    state.glow_threshold = glm::mix(0.55F, 0.78F, daylight) - twilight_presence * 0.04F;
    state.glow_strength = glm::mix(0.30F, 0.20F, daylight) + twilight_presence * 0.10F;
    state.post_sharpen_strength = glm::mix(0.10F, 0.18F, daylight);
    state.post_edge_strength = glm::mix(0.17F, 0.11F, daylight) + twilight_presence * 0.04F;

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
