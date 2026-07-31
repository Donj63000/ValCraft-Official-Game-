#pragma once

#include <cstdint>

namespace valcraft {

inline constexpr float kStaggerFixedStepSeconds = 1.0F / 60.0F;
inline constexpr float kMaximumStaggerPoints = 1'000'000.0F;
inline constexpr float kMaximumStaggerMultiplier = 16.0F;
inline constexpr float kMaximumStaggerDurationSeconds = 60.0F;
inline constexpr float kMaximumStaggerUpdateSeconds = 60.0F;

struct StaggerConfig {
    float maximum = 120.0F;
    float recovery_per_second = 12.0F;
    float recovery_delay_seconds = 1.5F;
    float staggered_duration_seconds = 2.0F;

    auto operator==(const StaggerConfig&) const -> bool = default;
};

enum class StaggerConfigureError : std::uint8_t {
    None = 0,
    InvalidMaximum,
    InvalidRecovery,
    InvalidRecoveryDelay,
    InvalidStaggerDuration,
};

enum class StaggerApplyError : std::uint8_t {
    None = 0,
    InvalidPower,
    InvalidMultiplier,
};

struct StaggerConfigureResult {
    bool configured = false;
    StaggerConfigureError error = StaggerConfigureError::None;
};

struct StaggerApplyResult {
    bool accepted = false;
    bool triggered = false;
    bool ignored_while_staggered = false;
    StaggerApplyError error = StaggerApplyError::None;
    float requested_power = 0.0F;
    float applied_power = 0.0F;
    float previous = 0.0F;
    float current = 0.0F;
};

struct StaggerUpdateResult {
    bool accepted = false;
    bool stagger_ended = false;
    std::uint64_t advanced_ticks = 0U;
    float recovered = 0.0F;
};

struct StaggerState {
    float current = 0.0F;
    float maximum = 0.0F;
    float recovery_delay_remaining_seconds = 0.0F;
    float staggered_remaining_seconds = 0.0F;
    bool staggered = false;

    auto operator==(const StaggerState&) const -> bool = default;
};

class StaggerSystem {
public:
    StaggerSystem() noexcept;

    [[nodiscard]] auto configure(
        const StaggerConfig& config) noexcept
        -> StaggerConfigureResult;
    [[nodiscard]] auto apply(
        float power,
        float multiplier = 1.0F) noexcept
        -> StaggerApplyResult;
    [[nodiscard]] auto update(
        float dt) noexcept -> StaggerUpdateResult;

    [[nodiscard]] auto state() const noexcept -> StaggerState;
    [[nodiscard]] auto config() const noexcept -> StaggerConfig;

    void reset() noexcept;

private:
    static constexpr std::uint64_t kUnitsPerPoint = 1'000U;
    static constexpr std::uint64_t kTicksPerSecond = 60U;

    StaggerConfig config_ {};
    std::uint64_t current_units_ = 0U;
    std::uint64_t maximum_units_ = 120U * kUnitsPerPoint;
    std::uint64_t recovery_units_per_second_ =
        12U * kUnitsPerPoint;
    std::uint64_t recovery_remainder_ = 0U;
    std::uint64_t recovery_delay_ticks_ = 90U;
    std::uint64_t recovery_delay_ticks_remaining_ = 0U;
    std::uint64_t stagger_duration_ticks_ = 120U;
    std::uint64_t stagger_ticks_remaining_ = 0U;
    double tick_accumulator_ = 0.0;
};

} // namespace valcraft
