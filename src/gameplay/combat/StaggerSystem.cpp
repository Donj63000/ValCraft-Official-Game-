#include "gameplay/combat/StaggerSystem.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace valcraft {

namespace {

inline constexpr double kTickSnapTolerance = 1.0e-5;

[[nodiscard]] auto duration_ticks(float seconds) noexcept
    -> std::uint64_t {
    const auto scaled =
        static_cast<double>(seconds) * 60.0;
    const auto nearest = std::round(scaled);
    const auto rounded =
        std::abs(scaled - nearest) <= kTickSnapTolerance
            ? nearest
            : std::ceil(scaled);
    return static_cast<std::uint64_t>(
        std::max(rounded, 0.0));
}

[[nodiscard]] auto point_units(float value) noexcept
    -> std::uint64_t {
    const auto scaled =
        static_cast<double>(value) * 1'000.0;
    return static_cast<std::uint64_t>(
        std::max(std::round(scaled), 0.0));
}

[[nodiscard]] auto points(
    std::uint64_t units) noexcept -> float {
    return static_cast<float>(
        static_cast<double>(units) / 1'000.0);
}

} // namespace

StaggerSystem::StaggerSystem() noexcept {
    static_cast<void>(configure(config_));
}

auto StaggerSystem::configure(
    const StaggerConfig& config) noexcept
    -> StaggerConfigureResult {
    StaggerConfigureResult result {};
    if (!std::isfinite(config.maximum) ||
        config.maximum <= 0.0F ||
        config.maximum > kMaximumStaggerPoints) {
        result.error =
            StaggerConfigureError::InvalidMaximum;
        return result;
    }
    if (!std::isfinite(config.recovery_per_second) ||
        config.recovery_per_second < 0.0F ||
        config.recovery_per_second >
            kMaximumStaggerPoints) {
        result.error =
            StaggerConfigureError::InvalidRecovery;
        return result;
    }
    if (!std::isfinite(config.recovery_delay_seconds) ||
        config.recovery_delay_seconds < 0.0F ||
        config.recovery_delay_seconds >
            kMaximumStaggerDurationSeconds) {
        result.error =
            StaggerConfigureError::InvalidRecoveryDelay;
        return result;
    }
    if (!std::isfinite(config.staggered_duration_seconds) ||
        config.staggered_duration_seconds <= 0.0F ||
        config.staggered_duration_seconds >
            kMaximumStaggerDurationSeconds) {
        result.error =
            StaggerConfigureError::InvalidStaggerDuration;
        return result;
    }

    // Je quantifie la jauge au millième de point et les durées au tick fixe :
    // deux exécutions recevant les mêmes événements produisent le même état.
    config_ = config;
    maximum_units_ = point_units(config.maximum);
    recovery_units_per_second_ =
        point_units(config.recovery_per_second);
    recovery_delay_ticks_ =
        duration_ticks(config.recovery_delay_seconds);
    stagger_duration_ticks_ =
        std::max<std::uint64_t>(
            1U,
            duration_ticks(
                config.staggered_duration_seconds));
    reset();
    result.configured = true;
    return result;
}

auto StaggerSystem::apply(
    float power,
    float multiplier) noexcept -> StaggerApplyResult {
    StaggerApplyResult result {};
    result.requested_power = power;
    result.previous = points(current_units_);
    result.current = result.previous;
    if (!std::isfinite(power) ||
        power < 0.0F ||
        power > kMaximumStaggerPoints) {
        result.error = StaggerApplyError::InvalidPower;
        return result;
    }
    if (!std::isfinite(multiplier) ||
        multiplier < 0.0F ||
        multiplier > kMaximumStaggerMultiplier) {
        result.error =
            StaggerApplyError::InvalidMultiplier;
        return result;
    }

    result.accepted = true;
    if (stagger_ticks_remaining_ > 0U) {
        result.ignored_while_staggered = power > 0.0F;
        return result;
    }

    const auto scaled =
        std::clamp(
            static_cast<double>(power) *
                static_cast<double>(multiplier),
            0.0,
            static_cast<double>(
                kMaximumStaggerPoints));
    const auto requested_units =
        point_units(static_cast<float>(scaled));
    const auto available =
        maximum_units_ - current_units_;
    const auto applied_units =
        std::min(requested_units, available);
    current_units_ += applied_units;
    result.applied_power = points(applied_units);

    if (applied_units > 0U) {
        recovery_delay_ticks_remaining_ =
            recovery_delay_ticks_;
        recovery_remainder_ = 0U;
    }
    if (current_units_ >= maximum_units_) {
        current_units_ = maximum_units_;
        stagger_ticks_remaining_ =
            stagger_duration_ticks_;
        recovery_delay_ticks_remaining_ = 0U;
        recovery_remainder_ = 0U;
        result.triggered = true;
    }
    result.current = points(current_units_);
    return result;
}

auto StaggerSystem::update(
    float dt) noexcept -> StaggerUpdateResult {
    StaggerUpdateResult result {};
    if (!std::isfinite(dt) ||
        dt < 0.0F ||
        dt > kMaximumStaggerUpdateSeconds) {
        return result;
    }
    result.accepted = true;

    tick_accumulator_ +=
        static_cast<double>(dt) *
        static_cast<double>(kTicksPerSecond);
    const auto nearest =
        std::round(tick_accumulator_);
    if (std::abs(tick_accumulator_ - nearest) <=
        kTickSnapTolerance) {
        tick_accumulator_ = nearest;
    }
    const auto ticks =
        static_cast<std::uint64_t>(
            std::floor(tick_accumulator_));
    tick_accumulator_ -=
        static_cast<double>(ticks);
    result.advanced_ticks = ticks;

    auto recovered_units = std::uint64_t {0U};
    for (std::uint64_t tick = 0U;
         tick < ticks;
         ++tick) {
        if (stagger_ticks_remaining_ > 0U) {
            --stagger_ticks_remaining_;
            if (stagger_ticks_remaining_ == 0U) {
                current_units_ = 0U;
                recovery_delay_ticks_remaining_ = 0U;
                recovery_remainder_ = 0U;
                result.stagger_ended = true;
            }
            continue;
        }
        if (recovery_delay_ticks_remaining_ > 0U) {
            --recovery_delay_ticks_remaining_;
            continue;
        }
        if (current_units_ == 0U ||
            recovery_units_per_second_ == 0U) {
            continue;
        }

        recovery_remainder_ +=
            recovery_units_per_second_;
        const auto requested_recovery =
            recovery_remainder_ /
            kTicksPerSecond;
        recovery_remainder_ %=
            kTicksPerSecond;
        const auto applied_recovery =
            std::min(
                requested_recovery,
                current_units_);
        current_units_ -= applied_recovery;
        recovered_units += applied_recovery;
        if (current_units_ == 0U) {
            recovery_remainder_ = 0U;
        }
    }

    result.recovered = points(recovered_units);
    return result;
}

auto StaggerSystem::state() const noexcept -> StaggerState {
    return {
        points(current_units_),
        points(maximum_units_),
        static_cast<float>(
            static_cast<double>(
                recovery_delay_ticks_remaining_) /
            static_cast<double>(kTicksPerSecond)),
        static_cast<float>(
            static_cast<double>(
                stagger_ticks_remaining_) /
            static_cast<double>(kTicksPerSecond)),
        stagger_ticks_remaining_ > 0U,
    };
}

auto StaggerSystem::config() const noexcept -> StaggerConfig {
    return {
        points(maximum_units_),
        points(recovery_units_per_second_),
        static_cast<float>(
            static_cast<double>(recovery_delay_ticks_) /
            static_cast<double>(kTicksPerSecond)),
        static_cast<float>(
            static_cast<double>(stagger_duration_ticks_) /
            static_cast<double>(kTicksPerSecond)),
    };
}

void StaggerSystem::reset() noexcept {
    current_units_ = 0U;
    recovery_remainder_ = 0U;
    recovery_delay_ticks_remaining_ = 0U;
    stagger_ticks_remaining_ = 0U;
    tick_accumulator_ = 0.0;
}

} // namespace valcraft
