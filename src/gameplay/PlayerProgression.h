#pragma once

#include "creatures/CreatureTypes.h"
#include "world/Block.h"
#include "world/Environment.h"

#include <glm/vec3.hpp>

#include <cstdint>
#include <optional>

namespace valcraft {

inline constexpr std::uint32_t kPlayerProgressionMinLevel = 1U;
inline constexpr std::uint32_t kPlayerProgressionMaxLevel = 100U;
inline constexpr std::uint32_t kPlayerProgressionSuperVisionLevel = 30U;
inline constexpr std::uint32_t kPlayerProgressionFlightLevel = kPlayerProgressionMaxLevel;
inline constexpr std::uint64_t kPlayerProgressionFirstLevelExperience = 100ULL;

struct PlayerProgressionState {
    std::uint32_t level = kPlayerProgressionMinLevel;
    std::uint64_t experience = 0ULL;

    auto operator==(const PlayerProgressionState&) const -> bool = default;
};

struct PlayerExperienceGainResult {
    std::uint64_t awarded_experience = 0ULL;
    std::uint32_t levels_gained = 0U;
    bool reached_max_level = false;
};

[[nodiscard]] auto player_experience_for_next_level(std::uint32_t level) noexcept -> std::uint64_t;
[[nodiscard]] auto player_progression_bonus_percent(std::uint32_t level) noexcept -> float;
[[nodiscard]] auto player_has_super_vision_power(std::uint32_t level) noexcept -> bool;
[[nodiscard]] auto player_has_flight_power(std::uint32_t level) noexcept -> bool;
[[nodiscard]] auto sanitize_player_progression_state(PlayerProgressionState state) noexcept -> PlayerProgressionState;
[[nodiscard]] auto block_break_experience(BlockId block_id) noexcept -> std::uint64_t;
[[nodiscard]] auto creature_kill_experience(CreatureSpecies species,
                                            const glm::vec3& position,
                                            std::uint32_t salt) noexcept -> std::uint64_t;
[[nodiscard]] auto experience_multiplier_for_activity(const CreatureCycleState& cycle,
                                                      std::optional<int> surface_y,
                                                      int activity_y) noexcept -> std::uint32_t;
[[nodiscard]] auto multiply_experience(std::uint64_t base_experience, std::uint32_t multiplier) noexcept -> std::uint64_t;

class PlayerProgression {
public:
    PlayerProgression() = default;

    void reset() noexcept;
    void load_state(PlayerProgressionState state) noexcept;

    [[nodiscard]] auto state() const noexcept -> PlayerProgressionState;
    [[nodiscard]] auto level() const noexcept -> std::uint32_t;
    [[nodiscard]] auto experience() const noexcept -> std::uint64_t;
    [[nodiscard]] auto experience_for_next_level() const noexcept -> std::uint64_t;
    [[nodiscard]] auto level_progress_ratio() const noexcept -> float;
    [[nodiscard]] auto attack_damage_multiplier() const noexcept -> float;
    [[nodiscard]] auto damage_resistance_percent() const noexcept -> float;
    [[nodiscard]] auto apnea_resistance_percent() const noexcept -> float;
    [[nodiscard]] auto fall_safety_multiplier() const noexcept -> float;
    [[nodiscard]] auto movement_speed_multiplier() const noexcept -> float;
    [[nodiscard]] auto has_super_vision_power() const noexcept -> bool;
    [[nodiscard]] auto has_flight_power() const noexcept -> bool;
    [[nodiscard]] auto is_max_level() const noexcept -> bool;

    auto add_experience(std::uint64_t amount) noexcept -> PlayerExperienceGainResult;

private:
    PlayerProgressionState state_ {};
};

} // namespace valcraft
