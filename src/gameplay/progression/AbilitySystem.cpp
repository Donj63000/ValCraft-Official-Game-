#include "gameplay/progression/AbilitySystem.h"
#include "world/Block.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace valcraft {

namespace {

[[nodiscard]] auto target_is_required(
    AbilityTargeting targeting) noexcept -> bool {
    return targeting != AbilityTargeting::Self;
}

[[nodiscard]] auto target_is_ground_based(
    AbilityTargeting targeting) noexcept -> bool {
    return targeting == AbilityTargeting::GroundPoint ||
           targeting == AbilityTargeting::WorldLineOrGrid;
}

[[nodiscard]] auto active_construction_plan(
    const PlayerBuildState& state) noexcept
    -> const ConstructionPlan& {
    const auto selected =
        std::min<std::size_t>(
            state.selected_construction_plan,
            state.construction_plans.size() - 1U);
    return state.construction_plans[selected];
}

[[nodiscard]] auto construction_cell_limit(
    std::uint8_t rank,
    const ConstructionPlan& plan,
    bool mastery_active) noexcept -> std::uint8_t {
    if (mastery_active) {
        return static_cast<std::uint8_t>(
            kConstructionPlanMaximumCellCount);
    }
    if (rank == 1U) {
        return 2U;
    }
    if (rank == 2U) {
        return 3U;
    }
    if (rank >= 3U) {
        // Je reserve le plan 3 x 3 au rang trois sans relever la longueur
        // maximale d'une ligne. La maitrise reste seule a autoriser dix blocs.
        return plan.shape == ConstructionPlanShape::Grid
                   ? 9U
                   : 5U;
    }
    return 0U;
}

void add_pilot_effects(
    const PlayerBuildState& state,
    const AbilityCastRequest& request,
    AbilityCastResolution& resolution) noexcept {
    switch (resolution.id) {
    case AbilityId::KnightVanguardStrike:
        if (std::isfinite(
                request.effective_range_meters) &&
            request.effective_range_meters > 0.0F) {
            resolution.range_meters =
                request.effective_range_meters;
        }
        if (resolution.rank >= 3U) {
            resolution.effects |=
                AbilityEffectFlag::VanguardSecondaryImpact;
        }
        if (resolution.mastery_active &&
            std::isfinite(
                request.seconds_since_successful_shield_block) &&
            request.seconds_since_successful_shield_block >= 0.0F &&
            request.seconds_since_successful_shield_block <= 3.0F) {
            resolution.energy_cost =
                std::max(
                    0.0F,
                    resolution.energy_cost - 6.0F);
            resolution.values[0] *= 1.25F;
            resolution.effects |=
                AbilityEffectFlag::VanguardBlockSynergy;
        }
        break;
    case AbilityId::NinjaWindAcceleration:
        if (resolution.rank >= 3U) {
            resolution.effects |=
                AbilityEffectFlag::WindBlade;
        }
        if (resolution.mastery_active) {
            resolution.effects |=
                AbilityEffectFlag::WindMasteryCleanseSlow;
            resolution.effects |=
                AbilityEffectFlag::WindMasteryDodge;
        }
        break;
    case AbilityId::CommanderFootman:
        if (resolution.rank >= 2U) {
            resolution.effects |=
                AbilityEffectFlag::FootmanLightTaunt;
        }
        if (resolution.rank >= 3U) {
            resolution.effects |=
                AbilityEffectFlag::FootmanProjectileBlock;
        }
        if (resolution.mastery_active) {
            resolution.effects |=
                AbilityEffectFlag::FootmanMasterySurvival;
            resolution.effects |=
                AbilityEffectFlag::FootmanMasteryDamageReduction;
        }
        break;
    case AbilityId::BuilderConstructionPlan: {
        resolution.construction_plan =
            active_construction_plan(state);
        const auto stored_cell_count =
            resolution.construction_plan.cell_count;
        if (stored_cell_count == 0U &&
            request.construction_cell_count == 0U) {
            resolution.construction_plan.shape =
                ConstructionPlanShape::Line;
            resolution.construction_plan.cell_count =
                resolution.rank == 1U
                    ? 2U
                    : resolution.rank == 2U
                          ? 3U
                          : 5U;
            for (std::size_t index = 0U;
                 index <
                 resolution.construction_plan
                     .cell_count;
                 ++index) {
                resolution.construction_plan
                    .cells[index] = {
                    static_cast<std::int8_t>(
                        index),
                    0,
                    0,
                    to_block_id(
                        BlockType::Planks),
                };
            }
        }
        const auto request_expands_plan =
            request.construction_cell_count != 0U &&
            (stored_cell_count == 0U ||
             request.construction_cell_count >
                 stored_cell_count);
        if (request.construction_cell_count != 0U) {
            resolution.construction_plan.cell_count =
                request.construction_cell_count;
        }
        resolution.maximum_construction_cells =
            request_expands_plan
                ? 0U
                : construction_cell_limit(
                      resolution.rank,
                      resolution.construction_plan,
                      resolution.mastery_active);
        if (resolution.mastery_active &&
            resolution.construction_plan.mirrored) {
            resolution.effects |=
                AbilityEffectFlag::ConstructionMirror;
        }
        break;
    }
    default:
        break;
    }
}

[[nodiscard]] auto construction_plan_is_valid(
    const AbilityCastResolution& resolution) noexcept -> bool {
    if (resolution.id !=
        AbilityId::BuilderConstructionPlan) {
        return true;
    }
    const auto count =
        resolution.construction_plan.cell_count;
    return construction_plan_is_canonical(
               resolution.construction_plan) &&
           count > 0U &&
           resolution.maximum_construction_cells > 0U &&
           count <= resolution.maximum_construction_cells &&
           count <= kConstructionPlanMaximumCellCount &&
           (!resolution.construction_plan.mirrored ||
            resolution.mastery_active);
}

void restore_ability_charge_if_ready(
    PlayerBuildState& state,
    std::size_t index,
    float step_seconds) noexcept {
    constexpr auto kTimerEpsilon = 0.00001F;
    const auto id =
        ability_id_from_index(index);
    const auto* definition =
        ability_definition(id);
    const auto rank =
        player_ability_rank(
            state,
            id);
    if (definition == nullptr ||
        rank == 0U ||
        definition->maximum_charges == 0U) {
        state.cooldowns_remaining[index] = 0.0F;
        state.charges[index] = 0U;
        return;
    }

    auto& charges =
        state.charges[index];
    charges =
        std::min(
            charges,
            definition->maximum_charges);
    auto& cooldown =
        state.cooldowns_remaining[index];
    if (!std::isfinite(cooldown) ||
        cooldown < 0.0F) {
        cooldown = 0.0F;
    }
    if (charges >= definition->maximum_charges) {
        cooldown = 0.0F;
        return;
    }
    const auto* rank_definition =
        ability_rank_definition(
            id,
            rank);
    const auto maximum_cooldown =
        rank_definition == nullptr
            ? 0.0F
            : rank_definition->cooldown_seconds;
    cooldown =
        std::min(
            cooldown,
            maximum_cooldown);
    if (cooldown <=
        step_seconds + kTimerEpsilon) {
        ++charges;
        if (charges < definition->maximum_charges) {
            cooldown =
                rank_definition == nullptr
                    ? 0.0F
                    : rank_definition->cooldown_seconds;
        } else {
            cooldown = 0.0F;
        }
    } else {
        cooldown -= step_seconds;
    }
}

} // namespace

auto player_ability_energy_parameters(
    const PlayerBuildState& state,
    std::uint8_t wisdom_equipment_bonus) noexcept
    -> AbilityEnergyParameters {
    const auto wisdom =
        player_attribute_value(
            state.attributes,
            PlayerAttribute::Wisdom,
            wisdom_equipment_bonus);
    return {
        player_max_val_energy(wisdom),
        player_val_energy_regeneration(wisdom),
    };
}

auto prepare_player_ability_cast(
    const PlayerBuildState& state,
    const AbilityCastRequest& request) noexcept -> AbilityCastResult {
    const auto* definition =
        ability_definition(
            request.id);
    if (definition == nullptr) {
        return {
            AbilityCastFailure::InvalidAbility,
            {},
        };
    }
    if (!definition->implemented) {
        return {
            AbilityCastFailure::UnimplementedAbility,
            {},
        };
    }
    if (definition->category ==
        AbilityCategory::Passive) {
        return {
            AbilityCastFailure::PassiveAbility,
            {},
        };
    }

    const auto rank =
        player_ability_rank(
            state,
            request.id);
    if (rank == 0U) {
        return {
            AbilityCastFailure::AbilityNotLearned,
            {},
        };
    }
    if (!player_ability_is_equipped(
            state,
            request.id)) {
        return {
            AbilityCastFailure::AbilityNotEquipped,
            {},
        };
    }
    if (!std::isfinite(
            state.global_cooldown_remaining) ||
        state.global_cooldown_remaining > 0.0F) {
        return {
            AbilityCastFailure::GlobalCooldown,
            {},
        };
    }

    const auto index =
        ability_index(
            request.id);
    const auto cooldown =
        state.cooldowns_remaining[index];
    const auto charges =
        state.charges[index];
    if (!std::isfinite(cooldown) ||
        cooldown < 0.0F ||
        (charges == 0U && cooldown > 0.0F)) {
        return {
            AbilityCastFailure::Cooldown,
            {},
        };
    }
    if (charges == 0U) {
        return {
            AbilityCastFailure::NoCharges,
            {},
        };
    }

    const auto* rank_definition =
        ability_rank_definition(
            request.id,
            rank);
    if (rank_definition == nullptr) {
        return {
            AbilityCastFailure::AbilityNotLearned,
            {},
        };
    }

    AbilityCastResolution resolution {};
    resolution.id = request.id;
    resolution.rank = rank;
    resolution.energy_cost =
        rank_definition->energy_cost;
    resolution.cooldown_seconds =
        rank_definition->cooldown_seconds;
    resolution.range_meters =
        rank_definition->range_meters;
    resolution.duration_seconds =
        rank_definition->duration_seconds;
    resolution.values =
        rank_definition->values;
    resolution.mastery_active =
        player_ability_has_mastery(
            state,
            request.id);
    add_pilot_effects(
        state,
        request,
        resolution);

    if (!std::isfinite(state.val_energy) ||
        state.val_energy <
            resolution.energy_cost) {
        return {
            AbilityCastFailure::InsufficientEnergy,
            resolution,
        };
    }

    if (target_is_required(
            definition->targeting)) {
        if (!request.target_valid ||
            !std::isfinite(
                request.target_distance_meters) ||
            request.target_distance_meters < 0.0F ||
            (target_is_ground_based(
                 definition->targeting) &&
             !request.ground_target_valid)) {
            return {
                AbilityCastFailure::InvalidTarget,
                resolution,
            };
        }
        if (request.target_distance_meters >
            resolution.range_meters) {
            return {
                AbilityCastFailure::TargetOutOfRange,
                resolution,
            };
        }
    }

    if (request.id ==
            AbilityId::BuilderConstructionPlan &&
        request.on_moving_ship) {
        return {
            AbilityCastFailure::MovingShipConstruction,
            resolution,
        };
    }
    if (!construction_plan_is_valid(
            resolution)) {
        return {
            AbilityCastFailure::InvalidConstructionPlan,
            resolution,
        };
    }
    return {
        AbilityCastFailure::None,
        resolution,
    };
}

void AbilitySystem::update(
    PlayerBuildState& state,
    float elapsed_seconds) noexcept {
    update(
        state,
        elapsed_seconds,
        player_ability_energy_parameters(
            state));
}

void AbilitySystem::update(
    PlayerBuildState& state,
    float elapsed_seconds,
    const AbilityEnergyParameters& energy_parameters) noexcept {
    if (!std::isfinite(elapsed_seconds) ||
        elapsed_seconds <= 0.0F) {
        return;
    }

    // Je garde un accumulateur en double pour rendre la simulation identique
    // quelle que soit la découpe des images reçues par le moteur.
    constexpr auto kMaximumCatchUpSeconds =
        120.0;
    fixed_step_accumulator_seconds_ =
        std::min(
            kMaximumCatchUpSeconds,
            fixed_step_accumulator_seconds_ +
                static_cast<double>(
                    elapsed_seconds));
    constexpr auto kFixedStep =
        1.0 / 60.0;
    constexpr auto kStepComparisonTolerance =
        0.000000001;
    while (fixed_step_accumulator_seconds_ +
               kStepComparisonTolerance >=
           kFixedStep) {
        simulate_fixed_step(
            state,
            energy_parameters);
        fixed_step_accumulator_seconds_ -=
            kFixedStep;
    }
    fixed_step_accumulator_seconds_ =
        std::max(
            0.0,
            fixed_step_accumulator_seconds_);
}

void AbilitySystem::simulate_fixed_step(
    PlayerBuildState& state) noexcept {
    simulate_fixed_step(
        state,
        player_ability_energy_parameters(
            state));
}

void AbilitySystem::simulate_fixed_step(
    PlayerBuildState& state,
    const AbilityEnergyParameters& energy_parameters) noexcept {
    constexpr auto step =
        kAbilityFixedStepSeconds;
    const auto maximum_energy =
        std::isfinite(
            energy_parameters.maximum_energy)
            ? std::max(
                  0.0F,
                  energy_parameters.maximum_energy)
            : kPlayerBaseMaximumValEnergy;
    const auto regeneration_per_second =
        std::isfinite(
            energy_parameters.regeneration_per_second)
            ? std::max(
                  0.0F,
                  energy_parameters.regeneration_per_second)
            : kPlayerBaseValEnergyRegenerationPerSecond;

    if (!std::isfinite(
            state.global_cooldown_remaining) ||
        state.global_cooldown_remaining < 0.0F) {
        state.global_cooldown_remaining = 0.0F;
    } else {
        state.global_cooldown_remaining =
            std::max(
                0.0F,
                state.global_cooldown_remaining - step);
    }

    const auto previous_regeneration_delay =
        std::isfinite(
            state.energy_regeneration_delay_remaining)
            ? std::max(
                  0.0F,
                  state.energy_regeneration_delay_remaining)
            : 0.0F;
    state.energy_regeneration_delay_remaining =
        std::max(
            0.0F,
            previous_regeneration_delay - step);

    if (!std::isfinite(state.val_energy)) {
        state.val_energy =
            maximum_energy;
    }
    state.val_energy =
        std::clamp(
            state.val_energy,
            0.0F,
            maximum_energy);
    const auto regeneration_time =
        std::max(
            0.0F,
            step - previous_regeneration_delay);
    state.val_energy =
        std::min(
            maximum_energy,
            state.val_energy +
                regeneration_per_second *
                    regeneration_time);

    for (std::size_t index = 0U;
         index < kAbilityCount;
         ++index) {
        restore_ability_charge_if_ready(
            state,
            index,
            step);
    }
}

auto AbilitySystem::try_cast(
    PlayerBuildState& state,
    const AbilityCastRequest& request,
    const AbilityCastCallbacks& callbacks) noexcept
    -> AbilityCastResult {
    auto result =
        prepare_player_ability_cast(
            state,
            request);
    if (!result.succeeded()) {
        return result;
    }

    auto cast_payload =
        request.event_payload;
    cast_payload.ability_id =
        request.id;
    cast_payload.primary_value =
        result.resolution.energy_cost;
    cast_payload.duration_seconds =
        result.resolution.duration_seconds;
    cast_payload.visual_id =
        resolved_ability_visual_id(
            request.id);
    cast_payload.sfx_id =
        resolved_ability_sfx_id(
            request.id);
    const auto cast_start =
        logical_events_.start_cast(
            cast_payload);
    result.resolution.cast_sequence =
        cast_start.cast_sequence;

    const auto publish_blocked =
        [&](AbilityCastFailure failure) noexcept {
            auto blocked_payload =
                cast_payload;
            blocked_payload.detail_code =
                static_cast<std::uint32_t>(
                    failure);
            static_cast<void>(
                logical_events_.publish(
                    AbilityEventType::Blocked,
                    result.resolution
                        .cast_sequence,
                    blocked_payload));
        };

    if (callbacks.commit == nullptr) {
        result.failure =
            AbilityCastFailure::MissingCommitter;
        publish_blocked(
            result.failure);
        return result;
    }
    if (callbacks.validate != nullptr &&
        !callbacks.validate(
            callbacks.user_data,
            request,
            result.resolution)) {
        result.failure =
            AbilityCastFailure::ExternalValidationRejected;
        publish_blocked(
            result.failure);
        return result;
    }

    auto next_state = state;
    const auto index =
        ability_index(
            request.id);
    const auto* definition =
        ability_definition(
            request.id);
    const auto maximum_charges =
        definition == nullptr
            ? std::uint8_t {0U}
            : definition->maximum_charges;
    next_state.charges[index] =
        std::min(
            next_state.charges[index],
            maximum_charges);
    if (next_state.charges[index] >=
        maximum_charges) {
        next_state.cooldowns_remaining[index] = 0.0F;
    }
    next_state.val_energy =
        std::max(
            0.0F,
            next_state.val_energy -
                result.resolution.energy_cost);
    next_state.energy_regeneration_delay_remaining =
        kPlayerValEnergyRegenerationDelaySeconds;
    next_state.global_cooldown_remaining =
        kAbilityGlobalCooldownSeconds;
    if (next_state.charges[index] > 0U) {
        --next_state.charges[index];
    }
    if (next_state.charges[index] <
            maximum_charges &&
        next_state.cooldowns_remaining[index] <= 0.0F) {
        next_state.cooldowns_remaining[index] =
            result.resolution.cooldown_seconds;
    }
    ++next_state.successful_cast_sequence;
    if (next_state.revision !=
        std::numeric_limits<std::uint64_t>::max()) {
        ++next_state.revision;
    }

    // Je ne paie la ressource et ne démarre les délais qu'après la validation
    // et l'application réussie de l'effet externe.
    if (!callbacks.commit(
            callbacks.user_data,
            request,
            result.resolution)) {
        result.failure =
            AbilityCastFailure::ExternalCommitRejected;
        publish_blocked(
            result.failure);
        return result;
    }
    state = next_state;
    static_cast<void>(
        logical_events_.publish(
            AbilityEventType::CastSucceeded,
            result.resolution
                .cast_sequence,
            cast_payload));
    return result;
}

void AbilitySystem::reset_timing() noexcept {
    fixed_step_accumulator_seconds_ = 0.0;
    logical_events_.clear();
}

auto AbilitySystem::pending_time_seconds() const noexcept -> double {
    return fixed_step_accumulator_seconds_;
}

auto AbilitySystem::logical_events() const noexcept
    -> std::span<const AbilityLogicalEvent> {
    return logical_events_.peek();
}

auto AbilitySystem::publish_logical_event(
    AbilityEventType type,
    AbilityCastSequence cast_sequence,
    const AbilityEventPayload& payload) noexcept
    -> AbilityEventPublishResult {
    return logical_events_.publish(
        type,
        cast_sequence,
        payload);
}

auto AbilitySystem::drain_logical_events(
    std::span<AbilityLogicalEvent> destination) noexcept
    -> std::size_t {
    return logical_events_.drain(
        destination);
}

auto AbilitySystem::next_cast_sequence() const noexcept
    -> AbilityCastSequence {
    return logical_events_.next_cast_sequence();
}

void AbilitySystem::reserve_next_cast_sequence(
    AbilityCastSequence minimum_next_sequence) noexcept {
    logical_events_.reserve_next_cast_sequence(
        minimum_next_sequence);
}

} // namespace valcraft
