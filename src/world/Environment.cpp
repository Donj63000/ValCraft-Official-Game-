#include "world/Environment.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>

#include <cmath>

namespace valcraft {

namespace {

constexpr float kFullDayDurationSeconds = 12.0F * 60.0F;
constexpr float kPi = 3.14159265358979323846F;
constexpr float kTwoPi = 2.0F * kPi;

auto smooth_curve(float edge0, float edge1, float value) noexcept -> float {
    const auto t = glm::clamp((value - edge0) / (edge1 - edge0), 0.0F, 1.0F);
    return t * t * (3.0F - 2.0F * t);
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

    const auto daylight = smooth_curve(-0.18F, 0.24F, sun_height);
    const auto horizon_glow =
        smooth_curve(-0.20F, 0.10F, sun_height) *
        (1.0F - smooth_curve(0.18F, 0.82F, sun_height));
    const auto twilight = smooth_curve(-0.12F, 0.10F, sun_height) * (1.0F - daylight);
    const auto night_factor = 1.0F - smooth_curve(-0.20F, 0.02F, sun_height);
    state.daylight_factor = glm::mix(0.16F, 1.0F, daylight);

    const auto day_sun = glm::vec3 {1.00F, 0.95F, 0.84F};
    const auto dusk_sun = glm::vec3 {1.00F, 0.62F, 0.34F};
    const auto night_sun = glm::vec3 {0.08F, 0.09F, 0.16F};
    state.sun_color = glm::mix(glm::mix(night_sun, dusk_sun, horizon_glow), day_sun, daylight);

    const auto day_ambient = glm::vec3 {0.42F, 0.48F, 0.56F};
    const auto dusk_ambient = glm::vec3 {0.22F, 0.20F, 0.28F};
    const auto night_ambient = glm::vec3 {0.09F, 0.10F, 0.16F};
    state.ambient_color = glm::mix(glm::mix(night_ambient, dusk_ambient, horizon_glow), day_ambient, daylight);

    const auto day_fog = glm::vec3 {0.62F, 0.78F, 0.96F};
    const auto dusk_fog = glm::vec3 {0.62F, 0.48F, 0.56F};
    const auto night_fog = glm::vec3 {0.06F, 0.08F, 0.14F};
    state.fog_color = glm::mix(glm::mix(night_fog, dusk_fog, horizon_glow), day_fog, daylight);

    const auto day_sky = glm::vec3 {0.50F, 0.74F, 0.98F};
    const auto dusk_sky = glm::vec3 {0.84F, 0.46F, 0.42F};
    const auto night_sky = glm::vec3 {0.02F, 0.03F, 0.08F};
    state.sky_color = glm::mix(glm::mix(night_sky, dusk_sky, horizon_glow), day_sky, daylight);

    const auto day_zenith = glm::vec3 {0.26F, 0.62F, 0.98F};
    const auto dusk_zenith = glm::vec3 {0.38F, 0.30F, 0.60F};
    const auto night_zenith = glm::vec3 {0.02F, 0.05F, 0.14F};
    state.sky_zenith_color = glm::mix(glm::mix(night_zenith, dusk_zenith, horizon_glow), day_zenith, daylight);

    const auto day_horizon = glm::vec3 {0.96F, 0.84F, 0.62F};
    const auto dusk_horizon = glm::vec3 {1.00F, 0.52F, 0.36F};
    const auto night_horizon = glm::vec3 {0.07F, 0.09F, 0.18F};
    state.sky_horizon_color = glm::mix(glm::mix(night_horizon, dusk_horizon, horizon_glow), day_horizon, daylight);

    state.horizon_glow_color = glm::mix(glm::vec3 {0.30F, 0.20F, 0.42F}, glm::vec3 {1.00F, 0.68F, 0.36F}, horizon_glow);
    state.distant_fog_color = glm::mix(glm::vec3 {0.05F, 0.08F, 0.14F}, glm::vec3 {0.86F, 0.92F, 0.98F}, daylight);
    state.night_tint_color = glm::mix(glm::vec3 {0.08F, 0.12F, 0.24F}, glm::vec3 {0.04F, 0.07F, 0.16F}, twilight);
    state.sun_disk_color = glm::mix(glm::vec3 {0.90F, 0.48F, 0.30F}, glm::vec3 {1.00F, 0.90F, 0.62F}, daylight);
    state.moon_disk_color = glm::mix(glm::vec3 {0.74F, 0.86F, 1.00F}, glm::vec3 {0.90F, 0.94F, 1.00F}, twilight);
    state.star_intensity = glm::clamp(night_factor * (1.0F - horizon_glow * 0.75F), 0.0F, 1.0F);
    state.cloud_intensity = glm::mix(0.16F, 0.52F, daylight) * (0.82F + 0.18F * twilight);
    state.exposure = glm::mix(0.86F, 1.08F, daylight) + horizon_glow * 0.08F;
    state.saturation_boost = glm::mix(0.94F, 1.10F, daylight) + twilight * 0.06F;
    state.contrast = glm::mix(1.04F, 1.12F, daylight) + twilight * 0.04F;
    state.vignette_strength = glm::mix(0.20F, 0.12F, daylight);
    state.glow_threshold = glm::mix(0.58F, 0.78F, daylight);
    state.glow_strength = glm::mix(0.34F, 0.22F, daylight) + twilight * 0.06F;

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
