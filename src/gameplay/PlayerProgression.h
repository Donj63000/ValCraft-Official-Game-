#pragma once

#include "gameplay/progression/ExperienceRewardPolicy.h"
#include "gameplay/progression/PlayerDerivedStats.h"
#include "gameplay/progression/ProgressionCurve.h"
#include "world/Block.h"
#include "world/Environment.h"

#include <glm/vec3.hpp>

#include <cstdint>

namespace valcraft {

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

enum class ExperienceReason : std::uint8_t {
    Harvest = 0,
    Combat = 1,
    Fishing = 2,
    Exploration = 3,
    Crafting = 4,
    Construction = 5,
    Quest = 6,
    Event = 7,
};

struct ExperienceAwardContext {
    ExperienceReason reason = ExperienceReason::Event;
    bool hostile_target = false;
    // Je conserve ce champ uniquement pour la compatibilité des appelants
    // actuels ; le bonus maritime ne dépend plus d'une hauteur de surface.
    bool at_surface = false;
    bool surface_water_context = false;
    CreaturePhase phase = CreaturePhase::Day;
};

[[nodiscard]] auto player_progression_bonus_percent(std::uint32_t level) noexcept -> float;
[[nodiscard]] auto player_has_super_vision_power(std::uint32_t level) noexcept -> bool;
[[nodiscard]] auto player_has_flight_power(std::uint32_t level) noexcept -> bool;
[[nodiscard]] auto sanitize_player_progression_state(PlayerProgressionState state) noexcept -> PlayerProgressionState;
[[nodiscard]] auto migrate_legacy_player_progression_state(
    PlayerProgressionState legacy_state) noexcept -> PlayerProgressionState;
[[nodiscard]] auto block_break_experience(
    BlockId block_id,
    bool player_placed = false) noexcept -> std::uint64_t;
[[nodiscard]] auto creature_kill_experience(CreatureSpecies species,
                                            const glm::vec3& position,
                                            std::uint32_t salt) noexcept -> std::uint64_t;
[[nodiscard]] auto apply_experience_modifiers(
    std::uint64_t base_experience,
    const ExperienceAwardContext& context) noexcept -> std::uint64_t;

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
    [[nodiscard]] auto block_break_speed_multiplier() const noexcept -> float;
    [[nodiscard]] auto base_max_health() const noexcept -> float;
    [[nodiscard]] auto derived_stats() const noexcept -> PlayerDerivedStats;
    [[nodiscard]] auto capabilities(
        PlayerProgressionMode mode =
            PlayerProgressionMode::ClassicAdventure) const noexcept
        -> PlayerProgressionCapabilities;
    [[nodiscard]] auto has_super_vision_power() const noexcept -> bool;
    [[nodiscard]] auto has_flight_power() const noexcept -> bool;
    [[nodiscard]] auto is_max_level() const noexcept -> bool;

    auto add_experience(std::uint64_t amount) noexcept -> PlayerExperienceGainResult;

private:
    PlayerProgressionState state_ {};
};

} // namespace valcraft
