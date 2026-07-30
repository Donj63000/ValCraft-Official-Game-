#pragma once

#include "gameplay/progression/AbilityCatalog.h"
#include "gameplay/progression/PlayerAttributes.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace valcraft {

inline constexpr std::size_t kEquippedAbilitySlotCount = 5U;
inline constexpr std::size_t kConstructionPlanCount = 3U;
inline constexpr std::size_t kConstructionPlanMaximumCellCount = 10U;
inline constexpr std::int8_t kConstructionPlanCoordinateLimit = 32;

enum class ConstructionPlanShape : std::uint8_t {
    Line = 0,
    Grid = 1,
};

struct ConstructionPlanCell {
    std::int8_t x = 0;
    std::int8_t y = 0;
    std::int8_t z = 0;
    // Je garde une valeur large pendant le décodage pour détecter une
    // sauvegarde corrompue avant de la réduire au BlockId du monde.
    std::uint16_t material_id = 0U;

    auto operator==(const ConstructionPlanCell&) const -> bool = default;
};

struct ConstructionPlan {
    std::array<
        ConstructionPlanCell,
        kConstructionPlanMaximumCellCount> cells {};
    std::uint8_t cell_count = 0U;
    ConstructionPlanShape shape = ConstructionPlanShape::Line;
    bool mirrored = false;

    auto operator==(const ConstructionPlan&) const -> bool = default;
};

struct PlayerBuildState {
    PlayerAttributeAllocation attributes {};
    std::array<std::uint8_t, kAbilityCount> ability_ranks {};
    std::array<std::uint8_t, kAbilityCount> ability_masteries {};
    std::array<AbilityId, kEquippedAbilitySlotCount> equipped_abilities {
        AbilityId::None,
        AbilityId::None,
        AbilityId::None,
        AbilityId::None,
        AbilityId::None,
    };
    float val_energy = 100.0F;
    float global_cooldown_remaining = 0.0F;
    float energy_regeneration_delay_remaining = 0.0F;
    std::array<float, kAbilityCount> cooldowns_remaining {};
    std::array<std::uint8_t, kAbilityCount> charges {};
    std::array<ConstructionPlan, kConstructionPlanCount>
        construction_plans {};
    std::uint8_t selected_construction_plan = 0U;
    std::uint64_t successful_cast_sequence = 0ULL;
    AbilityPath last_dominant_path = AbilityPath::Knight;
    std::uint64_t revision = 0ULL;

    auto operator==(const PlayerBuildState&) const -> bool = default;
};

// Je sérialise ces champs un par un ; je ne sauvegarde jamais le padding ou
// la représentation mémoire brute de cette structure.

struct PlayerBuildPointBudget {
    std::uint32_t earned_skill_points = 0U;
    std::uint32_t spent_skill_points = 0U;
    std::uint32_t available_skill_points = 0U;
    // Je distingue les points encore utilisables par le contenu jouable de la
    // reserve conservee pour les capacites qui ne sont pas encore activees.
    std::uint32_t implemented_skill_point_capacity = 0U;
    std::uint32_t spent_implemented_skill_points = 0U;
    std::uint32_t spendable_skill_points = 0U;
    std::uint32_t reserved_skill_points = 0U;
    std::uint32_t earned_attribute_points = 0U;
    std::uint32_t spent_attribute_points = 0U;
    std::uint32_t available_attribute_points = 0U;
    std::uint32_t earned_mastery_points = 0U;
    std::uint32_t spent_mastery_points = 0U;
    std::uint32_t available_mastery_points = 0U;
    std::array<std::uint32_t, kAbilityPathCount>
        spent_path_points {};

    auto operator==(const PlayerBuildPointBudget&) const -> bool = default;
};

enum class AbilityBuildFailure : std::uint8_t {
    None = 0,
    InvalidAbility,
    InvalidRank,
    RankOutOfSequence,
    RequiredLevel,
    RequiredPathPoints,
    MissingPrerequisite,
    InsufficientSkillPoints,
    AlreadyMastered,
    RankThreeRequired,
    InsufficientMasteryPoints,
    InvalidSlot,
    AbilityNotLearned,
    PassiveNotEquippable,
    DuplicateAbility,
    UnimplementedAbility,
    IncompatibleSlot,
};

struct AbilityBuildResult {
    AbilityBuildFailure failure = AbilityBuildFailure::None;

    [[nodiscard]] constexpr auto succeeded() const noexcept -> bool {
        return failure == AbilityBuildFailure::None;
    }
};

[[nodiscard]] auto player_ability_rank(
    const PlayerBuildState& state,
    AbilityId id) noexcept -> std::uint8_t;

[[nodiscard]] auto player_ability_has_mastery(
    const PlayerBuildState& state,
    AbilityId id) noexcept -> bool;

[[nodiscard]] auto player_ability_is_equipped(
    const PlayerBuildState& state,
    AbilityId id) noexcept -> bool;

[[nodiscard]] auto ability_category_fits_equipped_slot(
    AbilityCategory category,
    std::size_t slot) noexcept -> bool;

[[nodiscard]] auto player_dominant_path(
    const PlayerBuildState& state,
    std::uint32_t level) noexcept -> AbilityPath;

void select_player_dominant_path(
    PlayerBuildState& state,
    AbilityPath path) noexcept;

[[nodiscard]] auto construction_plan_material_is_valid(
    std::uint16_t material_id) noexcept -> bool;

[[nodiscard]] auto construction_plan_is_canonical(
    const ConstructionPlan& plan) noexcept -> bool;

[[nodiscard]] auto player_build_point_budget(
    const PlayerBuildState& state,
    std::uint32_t level) noexcept -> PlayerBuildPointBudget;

[[nodiscard]] auto player_ability_rank_purchase_failure(
    const PlayerBuildState& state,
    std::uint32_t level,
    AbilityId id) noexcept -> AbilityBuildFailure;

[[nodiscard]] auto purchase_player_ability_rank(
    PlayerBuildState& state,
    std::uint32_t level,
    AbilityId id) noexcept -> AbilityBuildResult;

[[nodiscard]] auto player_ability_mastery_purchase_failure(
    const PlayerBuildState& state,
    std::uint32_t level,
    AbilityId id) noexcept -> AbilityBuildFailure;

[[nodiscard]] auto purchase_player_ability_mastery(
    PlayerBuildState& state,
    std::uint32_t level,
    AbilityId id) noexcept -> AbilityBuildResult;

[[nodiscard]] auto equip_player_ability(
    PlayerBuildState& state,
    std::size_t slot,
    AbilityId id) noexcept -> AbilityBuildResult;

void sanitize_player_build_state(
    PlayerBuildState& state,
    std::uint32_t level) noexcept;

} // namespace valcraft
