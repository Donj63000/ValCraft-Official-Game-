#include "gameplay/progression/PlayerBuildState.h"

#include "world/Block.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace valcraft {

namespace {

[[nodiscard]] auto finite_clamp(
    float value,
    float minimum,
    float maximum,
    float fallback) noexcept -> float {
    if (!std::isfinite(value)) {
        return fallback;
    }
    return std::clamp(
        value,
        minimum,
        maximum);
}

void increment_build_revision(
    PlayerBuildState& state) noexcept {
    if (state.revision !=
        std::numeric_limits<std::uint64_t>::max()) {
        ++state.revision;
    }
}

[[nodiscard]] auto normalized_rank(
    const PlayerBuildState& state,
    std::size_t index) noexcept -> std::uint8_t {
    if (index >= state.ability_ranks.size()) {
        return 0U;
    }
    const auto rank =
        state.ability_ranks[index];
    const auto maximum_rank =
        static_cast<std::uint8_t>(
            kAbilityRankCount);
    return rank <= maximum_rank
               ? rank
               : 0U;
}

[[nodiscard]] auto cell_position_matches(
    const ConstructionPlanCell& lhs,
    const ConstructionPlanCell& rhs) noexcept -> bool {
    return lhs.x == rhs.x &&
           lhs.y == rhs.y &&
           lhs.z == rhs.z;
}

void sanitize_construction_plan(
    ConstructionPlan& plan) noexcept {
    if (plan.shape != ConstructionPlanShape::Line &&
        plan.shape != ConstructionPlanShape::Grid) {
        plan.shape = ConstructionPlanShape::Line;
    }

    const auto requested_count =
        std::min<std::size_t>(
            plan.cell_count,
            plan.cells.size());
    std::array<
        ConstructionPlanCell,
        kConstructionPlanMaximumCellCount> sanitized_cells {};
    auto sanitized_count = std::size_t {0U};
    for (std::size_t source_index = 0U;
         source_index < requested_count;
         ++source_index) {
        auto cell = plan.cells[source_index];
        cell.x = std::clamp(
            cell.x,
            static_cast<std::int8_t>(
                -kConstructionPlanCoordinateLimit),
            kConstructionPlanCoordinateLimit);
        cell.y = std::clamp(
            cell.y,
            static_cast<std::int8_t>(
                -kConstructionPlanCoordinateLimit),
            kConstructionPlanCoordinateLimit);
        cell.z = std::clamp(
            cell.z,
            static_cast<std::int8_t>(
                -kConstructionPlanCoordinateLimit),
            kConstructionPlanCoordinateLimit);
        if (!construction_plan_material_is_valid(
                cell.material_id)) {
            cell.material_id =
                to_block_id(
                    BlockType::Air);
        }

        auto duplicate = false;
        for (std::size_t accepted_index = 0U;
             accepted_index < sanitized_count;
             ++accepted_index) {
            if (cell_position_matches(
                    cell,
                    sanitized_cells[accepted_index])) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) {
            sanitized_cells[sanitized_count] = cell;
            ++sanitized_count;
        }
    }

    plan.cells = sanitized_cells;
    plan.cell_count =
        static_cast<std::uint8_t>(
            sanitized_count);
}

void sanitize_runtime_state(
    PlayerBuildState& state) noexcept {
    constexpr auto kGlobalCooldown = 0.25F;
    constexpr auto kRegenerationDelay = 1.5F;
    const auto maximum_energy =
        player_max_val_energy(
            player_attribute_value(
                state.attributes,
                PlayerAttribute::Wisdom));

    state.val_energy =
        finite_clamp(
            state.val_energy,
            0.0F,
            maximum_energy,
            maximum_energy);
    state.global_cooldown_remaining =
        finite_clamp(
            state.global_cooldown_remaining,
            0.0F,
            kGlobalCooldown,
            0.0F);
    state.energy_regeneration_delay_remaining =
        finite_clamp(
            state.energy_regeneration_delay_remaining,
            0.0F,
            kRegenerationDelay,
            0.0F);

    for (std::size_t index = 0U;
         index < kAbilityCount;
         ++index) {
        const auto id =
            ability_id_from_index(index);
        const auto* definition =
            ability_definition(id);
        const auto rank =
            normalized_rank(state, index);
        if (definition == nullptr ||
            rank == 0U ||
            definition->category == AbilityCategory::Passive) {
            state.cooldowns_remaining[index] = 0.0F;
            state.charges[index] = 0U;
            continue;
        }

        const auto* rank_definition =
            ability_rank_definition(
                id,
                rank);
        const auto maximum_cooldown =
            rank_definition == nullptr
                ? 0.0F
                : rank_definition->cooldown_seconds;
        auto& cooldown =
            state.cooldowns_remaining[index];
        cooldown =
            finite_clamp(
                cooldown,
                0.0F,
                maximum_cooldown,
                0.0F);

        auto& charges =
            state.charges[index];
        charges =
            std::min(
                charges,
                definition->maximum_charges);
        if (charges >= definition->maximum_charges) {
            cooldown = 0.0F;
        } else if (cooldown <= 0.0F) {
            charges = definition->maximum_charges;
        }
    }
}

} // namespace

auto player_ability_rank(
    const PlayerBuildState& state,
    AbilityId id) noexcept -> std::uint8_t {
    const auto index =
        ability_index(id);
    return normalized_rank(
        state,
        index);
}

auto player_ability_has_mastery(
    const PlayerBuildState& state,
    AbilityId id) noexcept -> bool {
    const auto index =
        ability_index(id);
    return index < state.ability_masteries.size() &&
           state.ability_masteries[index] == 1U;
}

auto player_ability_is_equipped(
    const PlayerBuildState& state,
    AbilityId id) noexcept -> bool {
    if (!ability_id_is_valid(id)) {
        return false;
    }
    return std::find(
               state.equipped_abilities.begin(),
               state.equipped_abilities.end(),
               id) !=
           state.equipped_abilities.end();
}

auto ability_category_fits_equipped_slot(
    AbilityCategory category,
    std::size_t slot) noexcept -> bool {
    if (slot < 3U) {
        return category == AbilityCategory::Active;
    }
    if (slot == 3U) {
        return category == AbilityCategory::Utility;
    }
    if (slot == 4U) {
        return category == AbilityCategory::Ultimate;
    }
    return false;
}

auto player_dominant_path(
    const PlayerBuildState& state,
    std::uint32_t level) noexcept -> AbilityPath {
    const auto budget =
        player_build_point_budget(
            state,
            level);
    const auto maximum =
        *std::max_element(
            budget.spent_path_points.begin(),
            budget.spent_path_points.end());
    const auto preferred_index =
        ability_path_index(
            state.last_dominant_path);
    if (preferred_index <
            budget.spent_path_points.size() &&
        budget.spent_path_points[preferred_index] ==
            maximum) {
        return state.last_dominant_path;
    }
    const auto first =
        std::find(
            budget.spent_path_points.begin(),
            budget.spent_path_points.end(),
            maximum);
    return static_cast<AbilityPath>(
        static_cast<std::size_t>(
            std::distance(
                budget.spent_path_points.begin(),
                first)));
}

void select_player_dominant_path(
    PlayerBuildState& state,
    AbilityPath path) noexcept {
    if (ability_path_index(path) >=
            kAbilityPathCount ||
        state.last_dominant_path == path) {
        return;
    }
    state.last_dominant_path = path;
    increment_build_revision(state);
}

auto construction_plan_material_is_valid(
    std::uint16_t material_id) noexcept -> bool {
    if (material_id >
        std::numeric_limits<BlockId>::max()) {
        return false;
    }
    return is_known_block_id(
        static_cast<BlockId>(
            material_id));
}

auto construction_plan_is_canonical(
    const ConstructionPlan& plan) noexcept -> bool {
    if (plan.shape != ConstructionPlanShape::Line &&
        plan.shape != ConstructionPlanShape::Grid) {
        return false;
    }
    if (plan.cell_count >
        plan.cells.size()) {
        return false;
    }
    for (std::size_t index = 0U;
         index < plan.cell_count;
         ++index) {
        const auto& cell =
            plan.cells[index];
        if (cell.x < -kConstructionPlanCoordinateLimit ||
            cell.x > kConstructionPlanCoordinateLimit ||
            cell.y < -kConstructionPlanCoordinateLimit ||
            cell.y > kConstructionPlanCoordinateLimit ||
            cell.z < -kConstructionPlanCoordinateLimit ||
            cell.z > kConstructionPlanCoordinateLimit ||
            !construction_plan_material_is_valid(
                cell.material_id)) {
            return false;
        }
        for (std::size_t previous = 0U;
             previous < index;
             ++previous) {
            if (cell_position_matches(
                    cell,
                    plan.cells[previous])) {
                return false;
            }
        }
    }
    return true;
}

auto player_build_point_budget(
    const PlayerBuildState& state,
    std::uint32_t level) noexcept -> PlayerBuildPointBudget {
    PlayerBuildPointBudget budget {};
    const auto normalized_level =
        normalize_player_progression_level(
            level);
    budget.earned_skill_points =
        player_skill_points_earned(
            normalized_level);
    budget.earned_attribute_points =
        player_attribute_points_earned(
            normalized_level);
    budget.earned_mastery_points =
        player_mastery_points_earned(
            normalized_level);

    for (std::size_t index = 0U;
         index < kAbilityCount;
         ++index) {
        const auto id =
            ability_id_from_index(index);
        const auto* definition =
            ability_definition(id);
        const auto rank =
            normalized_rank(
                state,
                index);
        if (definition != nullptr) {
            auto ability_cost = std::uint32_t {0U};
            for (std::uint8_t purchased_rank = 1U;
                 purchased_rank <= rank;
                 ++purchased_rank) {
                const auto* rank_definition =
                    ability_rank_definition(
                        id,
                        purchased_rank);
                if (rank_definition != nullptr) {
                    ability_cost +=
                        rank_definition->skill_point_cost;
                }
            }
            budget.spent_skill_points +=
                ability_cost;
            if (definition->implemented) {
                budget.spent_implemented_skill_points +=
                    ability_cost;
                for (const auto& rank_definition :
                     definition->ranks) {
                    if (rank_definition.required_level <=
                        normalized_level) {
                        budget
                            .implemented_skill_point_capacity +=
                            rank_definition
                                .skill_point_cost;
                    }
                }
            }
            const auto path_index =
                ability_path_index(
                    definition->path);
            if (path_index <
                budget.spent_path_points.size()) {
                budget.spent_path_points[path_index] +=
                    ability_cost;
            }
        }
        budget.spent_mastery_points +=
            state.ability_masteries[index] != 0U
                ? 1U
                : 0U;
    }

    for (const auto value : state.attributes.values) {
        budget.spent_attribute_points +=
            value;
    }

    budget.available_skill_points =
        budget.earned_skill_points >
                budget.spent_skill_points
            ? budget.earned_skill_points -
                  budget.spent_skill_points
            : 0U;
    const auto remaining_implemented_capacity =
        budget.implemented_skill_point_capacity >
                budget.spent_implemented_skill_points
            ? budget.implemented_skill_point_capacity -
                  budget.spent_implemented_skill_points
            : 0U;
    budget.spendable_skill_points =
        std::min(
            budget.available_skill_points,
            remaining_implemented_capacity);
    budget.reserved_skill_points =
        budget.available_skill_points -
        budget.spendable_skill_points;
    budget.available_attribute_points =
        budget.earned_attribute_points >
                budget.spent_attribute_points
            ? budget.earned_attribute_points -
                  budget.spent_attribute_points
            : 0U;
    budget.available_mastery_points =
        budget.earned_mastery_points >
                budget.spent_mastery_points
            ? budget.earned_mastery_points -
                  budget.spent_mastery_points
            : 0U;
    return budget;
}

auto player_ability_rank_purchase_failure(
    const PlayerBuildState& state,
    std::uint32_t level,
    AbilityId id) noexcept -> AbilityBuildFailure {
    const auto* definition =
        ability_definition(id);
    if (definition == nullptr) {
        return AbilityBuildFailure::InvalidAbility;
    }
    if (!definition->implemented) {
        return AbilityBuildFailure::
            UnimplementedAbility;
    }

    const auto current_rank =
        player_ability_rank(
            state,
            id);
    if (current_rank >= kAbilityRankCount) {
        return AbilityBuildFailure::InvalidRank;
    }
    const auto next_rank =
        static_cast<std::uint8_t>(
            current_rank + 1U);
    const auto* next_definition =
        ability_rank_definition(
            id,
            next_rank);
    if (next_definition == nullptr) {
        return AbilityBuildFailure::InvalidRank;
    }
    if (level < next_definition->required_level) {
        return AbilityBuildFailure::RequiredLevel;
    }

    const auto budget =
        player_build_point_budget(
            state,
            level);
    if (next_rank == 1U) {
        if (definition->prerequisite != AbilityId::None &&
            player_ability_rank(
                state,
                definition->prerequisite) == 0U) {
            return AbilityBuildFailure::MissingPrerequisite;
        }
        const auto path_index =
            ability_path_index(
                definition->path);
        if (path_index >=
                budget.spent_path_points.size() ||
            budget.spent_path_points[path_index] <
                definition->required_path_points) {
            return AbilityBuildFailure::RequiredPathPoints;
        }
    }
    if (budget.available_skill_points <
        next_definition->skill_point_cost) {
        return AbilityBuildFailure::InsufficientSkillPoints;
    }
    return AbilityBuildFailure::None;
}

auto purchase_player_ability_rank(
    PlayerBuildState& state,
    std::uint32_t level,
    AbilityId id) noexcept -> AbilityBuildResult {
    const auto failure =
        player_ability_rank_purchase_failure(
            state,
            level,
            id);
    if (failure != AbilityBuildFailure::None) {
        return {failure};
    }

    const auto index =
        ability_index(id);
    ++state.ability_ranks[index];
    if (state.ability_ranks[index] == 1U) {
        const auto* definition =
            ability_definition(id);
        state.charges[index] =
            definition == nullptr
                ? 0U
                : definition->maximum_charges;
        state.cooldowns_remaining[index] = 0.0F;
    }
    increment_build_revision(state);
    return {};
}

auto player_ability_mastery_purchase_failure(
    const PlayerBuildState& state,
    std::uint32_t level,
    AbilityId id) noexcept -> AbilityBuildFailure {
    const auto* definition =
        ability_definition(id);
    if (definition == nullptr) {
        return AbilityBuildFailure::InvalidAbility;
    }
    if (!definition->implemented) {
        return AbilityBuildFailure::
            UnimplementedAbility;
    }
    if (player_ability_has_mastery(
            state,
            id)) {
        return AbilityBuildFailure::AlreadyMastered;
    }
    if (player_ability_rank(
            state,
            id) <
        kAbilityRankCount) {
        return AbilityBuildFailure::RankThreeRequired;
    }
    const auto budget =
        player_build_point_budget(
            state,
            level);
    if (budget.available_mastery_points == 0U) {
        return AbilityBuildFailure::InsufficientMasteryPoints;
    }
    return AbilityBuildFailure::None;
}

auto purchase_player_ability_mastery(
    PlayerBuildState& state,
    std::uint32_t level,
    AbilityId id) noexcept -> AbilityBuildResult {
    const auto failure =
        player_ability_mastery_purchase_failure(
            state,
            level,
            id);
    if (failure != AbilityBuildFailure::None) {
        return {failure};
    }
    state.ability_masteries[
        ability_index(id)] = 1U;
    increment_build_revision(state);
    return {};
}

auto equip_player_ability(
    PlayerBuildState& state,
    std::size_t slot,
    AbilityId id) noexcept -> AbilityBuildResult {
    if (slot >= state.equipped_abilities.size()) {
        return {AbilityBuildFailure::InvalidSlot};
    }
    if (id == AbilityId::None) {
        state.equipped_abilities[slot] =
            AbilityId::None;
        increment_build_revision(state);
        return {};
    }
    const auto* definition =
        ability_definition(id);
    if (definition == nullptr) {
        return {AbilityBuildFailure::InvalidAbility};
    }
    if (!definition->implemented) {
        return {
            AbilityBuildFailure::
                UnimplementedAbility,
        };
    }
    if (player_ability_rank(
            state,
            id) == 0U) {
        return {AbilityBuildFailure::AbilityNotLearned};
    }
    if (definition->category ==
        AbilityCategory::Passive) {
        return {AbilityBuildFailure::PassiveNotEquippable};
    }
    if (!ability_category_fits_equipped_slot(
            definition->category,
            slot)) {
        return {
            AbilityBuildFailure::
                IncompatibleSlot,
        };
    }
    for (std::size_t index = 0U;
         index < state.equipped_abilities.size();
         ++index) {
        if (index != slot &&
            state.equipped_abilities[index] == id) {
            return {AbilityBuildFailure::DuplicateAbility};
        }
    }
    state.equipped_abilities[slot] = id;
    increment_build_revision(state);
    return {};
}

void sanitize_player_build_state(
    PlayerBuildState& state,
    std::uint32_t level) noexcept {
    const auto requested_state = state;
    state.revision =
        requested_state.revision;

    // Je reconstruis l'arbre dans l'ordre stable du catalogue pour ne jamais
    // conserver un rang dont le coût ou le prérequis est corrompu.
    state.ability_ranks.fill(0U);
    state.ability_masteries.fill(0U);
    state.cooldowns_remaining.fill(0.0F);
    state.charges.fill(0U);
    for (std::size_t index = 0U;
         index < kAbilityCount;
         ++index) {
        const auto id =
            ability_id_from_index(index);
        const auto requested_rank =
            std::min<std::uint8_t>(
                requested_state.ability_ranks[index],
                static_cast<std::uint8_t>(
                    kAbilityRankCount));
        for (std::uint8_t rank = 0U;
             rank < requested_rank;
             ++rank) {
            if (!purchase_player_ability_rank(
                     state,
                     level,
                     id)
                     .succeeded()) {
                break;
            }
        }
    }

    for (std::size_t index = 0U;
         index < kAbilityCount;
         ++index) {
        if (requested_state.ability_masteries[index] != 0U) {
            static_cast<void>(
                purchase_player_ability_mastery(
                    state,
                    level,
                    ability_id_from_index(index)));
        }
    }

    auto progression_is_valid =
        state.ability_ranks ==
        requested_state.ability_ranks;
    for (std::size_t index = 0U;
         index < kAbilityCount;
         ++index) {
        const auto requested_mastery =
            requested_state
                .ability_masteries[index];
        progression_is_valid =
            progression_is_valid &&
            requested_mastery <= 1U &&
            state.ability_masteries[index] ==
                requested_mastery;
    }
    // Je conserve les achats valides reconstruits dans l'ordre des paliers.
    // Les rangs corrompus, non implémentés ou hors budget sont remboursés
    // sans effacer les choix sains qui les précèdent.

    state.attributes =
        sanitize_player_attribute_allocation(
            requested_state.attributes,
            level);

    state.equipped_abilities.fill(
        AbilityId::None);
    for (std::size_t slot = 0U;
         slot < state.equipped_abilities.size();
         ++slot) {
        static_cast<void>(
            equip_player_ability(
                state,
                slot,
                requested_state.equipped_abilities[slot]));
    }

    state.val_energy =
        requested_state.val_energy;
    state.global_cooldown_remaining =
        requested_state.global_cooldown_remaining;
    state.energy_regeneration_delay_remaining =
        requested_state.energy_regeneration_delay_remaining;
    state.cooldowns_remaining =
        requested_state.cooldowns_remaining;
    state.charges =
        requested_state.charges;
    state.successful_cast_sequence =
        progression_is_valid
            ? requested_state
                  .successful_cast_sequence
            : 0ULL;
    state.last_dominant_path =
        ability_path_index(
            requested_state.last_dominant_path) <
                kAbilityPathCount
            ? requested_state.last_dominant_path
            : AbilityPath::Knight;

    state.construction_plans =
        requested_state.construction_plans;
    for (auto& plan : state.construction_plans) {
        sanitize_construction_plan(plan);
    }
    state.selected_construction_plan =
        std::min<std::uint8_t>(
            requested_state.selected_construction_plan,
            static_cast<std::uint8_t>(
                kConstructionPlanCount - 1U));
    if (!player_ability_has_mastery(
            state,
            AbilityId::BuilderConstructionPlan)) {
        for (auto& plan : state.construction_plans) {
            plan.mirrored = false;
        }
    }

    sanitize_runtime_state(state);
    state.revision =
        requested_state.revision;
}

} // namespace valcraft
