#include "gameplay/progression/NinjaAbilityEffects.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace valcraft {

namespace {

inline constexpr float kMinimumMovementDistance = 0.01F;
inline constexpr float kDashSweepRadius = 0.85F;
inline constexpr float kBehindTargetOffset = 1.0F;
inline constexpr float kDistanceTolerance = 0.02F;

[[nodiscard]] auto finite_vector(const glm::vec3& value) noexcept -> bool {
    return std::isfinite(value.x) &&
           std::isfinite(value.y) &&
           std::isfinite(value.z);
}

[[nodiscard]] auto squared_length(const glm::vec3& value) noexcept -> float {
    return value.x * value.x +
           value.y * value.y +
           value.z * value.z;
}

[[nodiscard]] auto normalized_direction(
    const glm::vec3& value) noexcept -> std::optional<glm::vec3> {
    if (!finite_vector(value)) {
        return std::nullopt;
    }
    const auto length_squared = squared_length(value);
    if (!std::isfinite(length_squared) ||
        length_squared <=
            kMinimumMovementDistance *
                kMinimumMovementDistance) {
        return std::nullopt;
    }
    return value / std::sqrt(length_squared);
}

[[nodiscard]] auto distance_squared(
    const glm::vec3& lhs,
    const glm::vec3& rhs) noexcept -> float {
    return squared_length(lhs - rhs);
}

[[nodiscard]] auto distance_to_segment_squared(
    const glm::vec3& point,
    const glm::vec3& start,
    const glm::vec3& end) noexcept -> float {
    const auto segment = end - start;
    const auto length_squared = squared_length(segment);
    if (length_squared <=
        kMinimumMovementDistance *
            kMinimumMovementDistance) {
        return distance_squared(point, start);
    }
    const auto relative = point - start;
    const auto projection =
        relative.x * segment.x +
        relative.y * segment.y +
        relative.z * segment.z;
    const auto amount =
        std::clamp(
            projection / length_squared,
            0.0F,
            1.0F);
    return distance_squared(
        point,
        start + segment * amount);
}

[[nodiscard]] constexpr auto ticks_from_seconds(
    float seconds) noexcept -> std::uint32_t {
    return static_cast<std::uint32_t>(
        seconds * 60.0F + 0.5F);
}

[[nodiscard]] auto context_is_valid(
    const NinjaCastContext& context) noexcept -> NinjaEffectFailure {
    if (context.owner_id == 0U) {
        return NinjaEffectFailure::InvalidOwner;
    }
    if (context.activation_id == 0U) {
        return NinjaEffectFailure::InvalidActivation;
    }
    if (!ninja_rank_is_valid(context.rank)) {
        return NinjaEffectFailure::InvalidRank;
    }
    if (!std::isfinite(context.agility)) {
        return NinjaEffectFailure::NonFiniteInput;
    }
    return NinjaEffectFailure::None;
}

[[nodiscard]] constexpr auto mastery_is_active(
    const NinjaCastContext& context) noexcept -> bool {
    return context.mastered &&
           context.rank == NinjaAbilityRank::RankThree;
}

[[nodiscard]] auto valid_hostile(
    const NinjaEntitySnapshot& entity,
    NinjaEntityId owner_id) noexcept -> bool {
    return entity.entity_id != 0U &&
           entity.entity_id != owner_id &&
           entity.alive &&
           entity.hostile_to_owner &&
           finite_vector(entity.position);
}

template <std::size_t Capacity>
auto sort_entities(
    std::array<NinjaEntitySnapshot, Capacity>& entities,
    std::size_t count) noexcept -> void {
    std::sort(
        entities.begin(),
        entities.begin() +
            static_cast<std::ptrdiff_t>(count),
        [](const NinjaEntitySnapshot& lhs,
           const NinjaEntitySnapshot& rhs) {
            return lhs.entity_id < rhs.entity_id;
        });
}

[[nodiscard]] auto collect_entities(
    const NinjaWorldCallbacks& callbacks,
    const NinjaEntityQuery& query,
    std::array<
        NinjaEntitySnapshot,
        kNinjaMaximumQueryEntities>& entities,
    bool& saturated) -> std::size_t {
    if (!callbacks.query_entities) {
        return 0U;
    }
    const auto reported_count =
        callbacks.query_entities(
            query,
            std::span<NinjaEntitySnapshot> {
                entities.data(),
                entities.size(),
            });
    saturated =
        saturated ||
        reported_count > entities.size();
    const auto count =
        std::min(reported_count, entities.size());
    sort_entities(entities, count);
    return count;
}

[[nodiscard]] auto movement_result_is_valid(
    const NinjaSafeMovementRequest& request,
    const NinjaSafeMovementResult& result) noexcept -> bool {
    if (!result.valid ||
        !finite_vector(result.destination)) {
        return false;
    }
    const auto maximum =
        request.maximum_distance +
        kDistanceTolerance;
    return distance_squared(
               request.start,
               result.destination) <=
           maximum * maximum;
}

[[nodiscard]] auto unique_entity(
    std::span<const NinjaEntityId> ids,
    NinjaEntityId candidate) noexcept -> bool {
    return std::find(
               ids.begin(),
               ids.end(),
               candidate) == ids.end();
}

} // namespace

auto ninja_spell_damage(
    float base_damage,
    std::uint32_t player_level,
    float agility) noexcept -> float {
    if (!std::isfinite(base_damage) ||
        base_damage < 0.0F ||
        !std::isfinite(agility)) {
        return 0.0F;
    }
    const auto level =
        std::clamp(
            player_level,
            1U,
            kNinjaMaximumLevel);
    const auto bounded_agility =
        std::clamp(
            agility,
            0.0F,
            kNinjaMaximumAgility);
    const auto multiplier =
        1.0F +
        0.0025F *
            static_cast<float>(level - 1U) +
        0.02F * bounded_agility;
    const auto result = base_damage * multiplier;
    return std::isfinite(result)
               ? result
               : 0.0F;
}

auto NinjaAbilityEffects::cast_smoke_bomb(
    const NinjaCastContext& context,
    const glm::vec3& target,
    const NinjaWorldCallbacks& callbacks)
    -> NinjaSmokeBombCastResult {
    NinjaSmokeBombCastResult result {};
    result.failure = context_is_valid(context);
    if (result.failure != NinjaEffectFailure::None) {
        return result;
    }
    if (!finite_vector(target)) {
        result.failure =
            NinjaEffectFailure::NonFiniteInput;
        return result;
    }
    if (!callbacks.validate_smoke_placement) {
        result.failure =
            NinjaEffectFailure::MissingCallback;
        return result;
    }

    result.tuning =
        ninja_smoke_bomb_tuning(context.rank);
    const NinjaSmokePlacementRequest placement {
        context.owner_id,
        target,
        result.tuning.radius,
    };
    if (!callbacks.validate_smoke_placement(
            placement)) {
        result.failure =
            NinjaEffectFailure::InvalidTarget;
        return result;
    }

    // Je remplace l'ancien nuage seulement après validation complète du
    // nouveau placement, afin qu'un refus ne consomme aucun état logique.
    smoke_.owner_id = context.owner_id;
    smoke_.activation_id =
        context.activation_id;
    smoke_.rank = context.rank;
    smoke_.center = target;
    smoke_.remaining_ticks =
        ticks_from_seconds(
            result.tuning.duration_seconds);
    smoke_.active = true;
    smoke_.mastered =
        mastery_is_active(context);
    smoke_.attack_bonus_available =
        context.rank != NinjaAbilityRank::RankOne;
    clear_smoke_observers();

    result.cast = true;
    result.center = target;
    return result;
}

auto NinjaAbilityEffects::resolve_smoke_attack(
    const NinjaSmokeAttackRequest& request) noexcept
    -> NinjaSmokeAttackResult {
    if (!smoke_.active ||
        !smoke_.attack_bonus_available ||
        request.owner_id == 0U ||
        request.owner_id != smoke_.owner_id ||
        request.target_id == 0U ||
        !request.attack_landed ||
        !finite_vector(request.origin) ||
        !finite_vector(request.impact)) {
        return {};
    }

    const auto tuning =
        ninja_smoke_bomb_tuning(smoke_.rank);
    const auto radius_squared =
        tuning.radius * tuning.radius;
    if (distance_squared(
            request.origin,
            smoke_.center) >
            radius_squared ||
        distance_squared(
            request.impact,
            smoke_.center) <=
            radius_squared) {
        return {};
    }

    smoke_.attack_bonus_available = false;
    return {
        true,
        tuning.attack_bonus,
        tuning.slow_fraction,
        tuning.slow_duration_seconds,
    };
}

auto NinjaAbilityEffects::cast_shinobi_leap(
    const NinjaCastContext& context,
    const NinjaShinobiLeapCastRequest& request,
    const NinjaWorldCallbacks& callbacks)
    -> NinjaShinobiLeapCastResult {
    NinjaShinobiLeapCastResult result {};
    result.failure = context_is_valid(context);
    if (result.failure != NinjaEffectFailure::None) {
        return result;
    }
    if (!finite_vector(request.start) ||
        !finite_vector(request.direction)) {
        result.failure =
            NinjaEffectFailure::NonFiniteInput;
        return result;
    }
    const auto direction =
        normalized_direction(request.direction);
    if (!direction.has_value()) {
        result.failure =
            NinjaEffectFailure::InvalidDirection;
        return result;
    }
    if (!callbacks.find_safe_movement ||
        !callbacks.commit_movement) {
        result.failure =
            NinjaEffectFailure::MissingCallback;
        return result;
    }

    const auto tuning =
        ninja_shinobi_leap_tuning(context.rank);
    const auto free_impulse =
        request.use_free_second_impulse;
    if (free_impulse) {
        if (!request.airborne ||
            context.rank !=
                NinjaAbilityRank::RankThree ||
            aerial_bond_owner_ !=
                context.owner_id ||
            !free_second_impulse_available_) {
            result.failure =
                NinjaEffectFailure::
                    FreeImpulseUnavailable;
            return result;
        }
    } else if (request.airborne) {
        if (!tuning.aerial_use_allowed ||
            (aerial_bond_used_ &&
             aerial_bond_owner_ ==
                 context.owner_id)) {
            result.failure =
                NinjaEffectFailure::
                    AirUseUnavailable;
            return result;
        }
    }

    const auto requested_distance =
        free_impulse
            ? tuning.free_second_impulse_distance
            : tuning.distance;
    const NinjaSafeMovementRequest movement_request {
        free_impulse
            ? NinjaSafeMovementKind::
                  ShinobiFreeImpulse
            : NinjaSafeMovementKind::ShinobiLeap,
        context.owner_id,
        request.start,
        request.start +
            *direction * requested_distance,
        requested_distance,
    };
    const auto safe =
        callbacks.find_safe_movement(
            movement_request);
    if (!movement_result_is_valid(
            movement_request,
            safe) ||
        distance_squared(
            request.start,
            safe.destination) <=
            kMinimumMovementDistance *
                kMinimumMovementDistance) {
        result.failure =
            NinjaEffectFailure::UnsafeDestination;
        return result;
    }

    const NinjaMovementCommit movement {
        context.owner_id,
        context.activation_id,
        AbilityId::NinjaShinobiLeap,
        request.start,
        safe.destination,
    };
    if (!callbacks.commit_movement(movement)) {
        result.failure =
            NinjaEffectFailure::MovementRejected;
        return result;
    }

    if (free_impulse) {
        free_second_impulse_available_ = false;
        result.consumes_energy_and_cooldown = false;
    } else if (request.airborne) {
        aerial_bond_owner_ = context.owner_id;
        aerial_bond_used_ = true;
        free_second_impulse_available_ =
            tuning.free_second_impulse_allowed;
        result.armed_free_second_impulse =
            free_second_impulse_available_;
    }

    if (mastery_is_active(context)) {
        landing_.owner_id = context.owner_id;
        landing_.activation_id =
            context.activation_id;
        landing_.remaining_ticks =
            ticks_from_seconds(
                tuning.mastery_window_seconds);
        landing_.player_level =
            context.player_level;
        landing_.agility = context.agility;
        landing_.active = true;
    }

    result.moved = true;
    result.destination = safe.destination;
    result.requested_distance =
        requested_distance;
    return result;
}

auto NinjaAbilityEffects::handle_contact(
    const NinjaContactRequest& request,
    const NinjaWorldCallbacks& callbacks)
    -> NinjaLandingResult {
    NinjaLandingResult result {};
    if (request.owner_id == 0U) {
        result.failure =
            NinjaEffectFailure::InvalidOwner;
        return result;
    }
    if (!finite_vector(request.position)) {
        result.failure =
            NinjaEffectFailure::NonFiniteInput;
        return result;
    }

    if (aerial_bond_owner_ == request.owner_id) {
        aerial_bond_owner_ = 0U;
        aerial_bond_used_ = false;
        free_second_impulse_available_ = false;
    }
    if (!landing_.active ||
        landing_.owner_id != request.owner_id) {
        return result;
    }

    const auto landing = landing_;
    landing_ = {};
    if (request.kind == NinjaContactKind::Water) {
        return result;
    }

    result.mastery_triggered = true;
    result.cancel_fall_damage = true;
    if (!callbacks.query_entities ||
        !callbacks.apply_damage) {
        result.failure =
            NinjaEffectFailure::MissingCallback;
        return result;
    }

    std::array<
        NinjaEntitySnapshot,
        kNinjaMaximumQueryEntities>
        entities {};
    bool saturated = false;
    const auto tuning =
        ninja_shinobi_leap_tuning(
            NinjaAbilityRank::RankThree);
    const NinjaEntityQuery query {
        NinjaEntityQueryKind::LandingArea,
        request.owner_id,
        0U,
        request.position,
        request.position,
        request.position,
        tuning.mastery_wave_radius,
    };
    const auto count =
        collect_entities(
            callbacks,
            query,
            entities,
            saturated);
    NinjaEntityId previous_id = 0U;
    const auto damage =
        ninja_spell_damage(
            tuning.mastery_wave_damage,
            landing.player_level,
            landing.agility);
    for (std::size_t index = 0U;
         index < count;
         ++index) {
        const auto& entity = entities[index];
        if (!valid_hostile(
                entity,
                request.owner_id) ||
            entity.entity_id == previous_id ||
            distance_squared(
                entity.position,
                request.position) >
                tuning.mastery_wave_radius *
                    tuning.mastery_wave_radius) {
            continue;
        }
        previous_id = entity.entity_id;
        const NinjaDamageRequest damage_request {
            request.owner_id,
            entity.entity_id,
            landing.activation_id,
            AbilityId::NinjaShinobiLeap,
            NinjaDamagePass::ShinobiLanding,
            static_cast<std::uint8_t>(
                result.hit_count),
            damage,
        };
        const auto applied =
            callbacks.apply_damage(
                damage_request);
        auto& outcome =
            result.hits[result.hit_count++];
        outcome.target_id = entity.entity_id;
        outcome.pass =
            NinjaDamagePass::ShinobiLanding;
        outcome.requested_damage = damage;
        outcome.applied_damage =
            std::isfinite(
                applied.applied_damage)
                ? std::max(
                      applied.applied_damage,
                      0.0F)
                : 0.0F;
        outcome.hit = applied.hit;
        outcome.killed =
            applied.hit && applied.killed;
    }
    return result;
}

auto NinjaAbilityEffects::cast_lightning_dash(
    const NinjaCastContext& context,
    const NinjaLightningDashCastRequest& request,
    const NinjaWorldCallbacks& callbacks)
    -> NinjaLightningDashCastResult {
    NinjaLightningDashCastResult result {};
    result.failure = context_is_valid(context);
    if (result.failure != NinjaEffectFailure::None) {
        return result;
    }
    if (!finite_vector(request.start) ||
        !finite_vector(request.direction)) {
        result.failure =
            NinjaEffectFailure::NonFiniteInput;
        return result;
    }
    const auto direction =
        normalized_direction(request.direction);
    if (!direction.has_value()) {
        result.failure =
            NinjaEffectFailure::InvalidDirection;
        return result;
    }
    if (!callbacks.find_safe_movement ||
        !callbacks.commit_movement ||
        !callbacks.query_entities ||
        !callbacks.apply_damage) {
        result.failure =
            NinjaEffectFailure::MissingCallback;
        return result;
    }

    const auto tuning =
        ninja_lightning_dash_tuning(
            context.rank);
    const auto mastered =
        mastery_is_active(context);
    std::size_t consumed_charge_index =
        dash_charge_ticks_.size();
    if (mastered) {
        for (std::size_t index = 0U;
             index < dash_charge_ticks_.size();
             ++index) {
            if (dash_charge_ticks_[index] == 0U) {
                consumed_charge_index = index;
                break;
            }
        }
        if (consumed_charge_index ==
            dash_charge_ticks_.size()) {
            result.failure =
                NinjaEffectFailure::NoCharge;
            return result;
        }
    }

    NinjaSafeMovementRequest movement_request {
        NinjaSafeMovementKind::LightningDash,
        context.owner_id,
        request.start,
        request.start +
            *direction * tuning.distance,
        tuning.distance,
    };
    auto safe =
        NinjaSafeMovementResult {};

    if (context.rank ==
            NinjaAbilityRank::RankThree &&
        request.primary_target.has_value()) {
        const auto& target =
            *request.primary_target;
        const auto target_forward =
            normalized_direction(
                target.forward);
        if (valid_hostile(
                target,
                context.owner_id) &&
            target_forward.has_value() &&
            distance_squared(
                request.start,
                target.position) <=
                tuning.distance *
                    tuning.distance) {
            auto desired =
                target.position -
                *target_forward *
                    kBehindTargetOffset;
            const auto delta =
                desired - request.start;
            const auto delta_length_squared =
                squared_length(delta);
            if (delta_length_squared >
                tuning.distance *
                    tuning.distance) {
                const auto delta_direction =
                    normalized_direction(delta);
                if (delta_direction.has_value()) {
                    desired =
                        request.start +
                        *delta_direction *
                            tuning.distance;
                }
            }
            movement_request.kind =
                NinjaSafeMovementKind::
                    LightningDashBehindTarget;
            movement_request.requested_end =
                desired;
            safe =
                callbacks.find_safe_movement(
                    movement_request);
            if (!safe.reached_requested_end ||
                !movement_result_is_valid(
                    movement_request,
                    safe)) {
                safe = {};
            }
        }
    }

    if (!safe.valid) {
        movement_request.kind =
            NinjaSafeMovementKind::LightningDash;
        movement_request.requested_end =
            request.start +
            *direction * tuning.distance;
        safe =
            callbacks.find_safe_movement(
                movement_request);
    }
    if (!movement_result_is_valid(
            movement_request,
            safe) ||
        distance_squared(
            request.start,
            safe.destination) <=
            kMinimumMovementDistance *
                kMinimumMovementDistance) {
        result.failure =
            NinjaEffectFailure::UnsafeDestination;
        return result;
    }

    const NinjaMovementCommit movement {
        context.owner_id,
        context.activation_id,
        AbilityId::NinjaLightningDash,
        request.start,
        safe.destination,
    };
    if (!callbacks.commit_movement(movement)) {
        result.failure =
            NinjaEffectFailure::MovementRejected;
        return result;
    }

    if (mastered) {
        dash_charge_ticks_[
            consumed_charge_index] =
            ticks_from_seconds(
                tuning
                    .mastery_charge_recharge_seconds);
    }

    std::array<
        NinjaEntitySnapshot,
        kNinjaMaximumQueryEntities>
        entities {};
    const NinjaEntityQuery query {
        NinjaEntityQueryKind::DashPath,
        context.owner_id,
        request.primary_target.has_value()
            ? request.primary_target->entity_id
            : 0U,
        (request.start + safe.destination) *
            0.5F,
        request.start,
        safe.destination,
        kDashSweepRadius,
    };
    const auto count =
        collect_entities(
            callbacks,
            query,
            entities,
            result.query_saturated);
    const auto damage =
        ninja_spell_damage(
            tuning.base_damage,
            context.player_level,
            context.agility);
    NinjaEntityId previous_id = 0U;
    for (std::size_t index = 0U;
         index < count;
         ++index) {
        const auto& entity = entities[index];
        if (!valid_hostile(
                entity,
                context.owner_id) ||
            entity.entity_id == previous_id ||
            distance_to_segment_squared(
                entity.position,
                request.start,
                safe.destination) >
                kDashSweepRadius *
                    kDashSweepRadius ||
            (mastered &&
             recent_dash_hit_blocked(
                 entity.entity_id))) {
            continue;
        }
        previous_id = entity.entity_id;
        const NinjaDamageRequest damage_request {
            context.owner_id,
            entity.entity_id,
            context.activation_id,
            AbilityId::NinjaLightningDash,
            NinjaDamagePass::LightningDash,
            static_cast<std::uint8_t>(
                result.hit_count),
            damage,
        };
        const auto applied =
            callbacks.apply_damage(
                damage_request);
        auto& outcome =
            result.hits[result.hit_count++];
        outcome.target_id = entity.entity_id;
        outcome.pass =
            NinjaDamagePass::LightningDash;
        outcome.requested_damage = damage;
        outcome.applied_damage =
            std::isfinite(
                applied.applied_damage)
                ? std::max(
                      applied.applied_damage,
                      0.0F)
                : 0.0F;
        outcome.hit = applied.hit;
        outcome.killed =
            applied.hit && applied.killed;
        if (outcome.hit && mastered) {
            record_recent_dash_hit(
                entity.entity_id);
        }
        if (outcome.killed) {
            ++result.kill_count;
            apply_dash_kill_reduction(
                context,
                consumed_charge_index,
                tuning.kill_cooldown_reduction,
                callbacks);
        }
    }

    result.moved = true;
    result.destination = safe.destination;
    result.remaining_mastery_charges =
        available_lightning_dash_charges();
    return result;
}

auto NinjaAbilityEffects::cast_spectral_kunai(
    const NinjaCastContext& context,
    const NinjaSpectralKunaiCastRequest& request,
    const NinjaWorldCallbacks& callbacks)
    -> NinjaSpectralKunaiCastResult {
    NinjaSpectralKunaiCastResult result {};
    result.failure = context_is_valid(context);
    if (result.failure != NinjaEffectFailure::None) {
        return result;
    }
    if (request.first_target_id == 0U) {
        result.failure =
            NinjaEffectFailure::InvalidTarget;
        return result;
    }
    if (!callbacks.entity_snapshot ||
        !callbacks.apply_damage ||
        !callbacks.select_kunai_target) {
        result.failure =
            NinjaEffectFailure::MissingCallback;
        return result;
    }

    const auto first =
        callbacks.entity_snapshot(
            request.first_target_id);
    if (!first.has_value() ||
        !valid_hostile(
            *first,
            context.owner_id)) {
        result.failure =
            NinjaEffectFailure::InvalidTarget;
        return result;
    }

    const auto tuning =
        ninja_spectral_kunai_tuning(
            context.rank);
    const auto base_damage =
        ninja_spell_damage(
            tuning.base_damage,
            context.player_level,
            context.agility);
    std::array<NinjaEntityId, 4U> visited_ids {};
    std::array<NinjaEntitySnapshot, 4U>
        visited_entities {};
    std::array<float, 4U> powers {};
    std::size_t visited_count = 0U;
    auto current = *first;
    auto power = 1.0F;

    const auto maximum_outward_hits =
        static_cast<std::size_t>(
            tuning.bounce_count) +
        1U;
    for (std::size_t outward_index = 0U;
         outward_index <
         maximum_outward_hits;
         ++outward_index) {
        visited_ids[visited_count] =
            current.entity_id;
        visited_entities[visited_count] =
            current;
        powers[visited_count] = power;
        ++visited_count;

        const NinjaDamageRequest damage_request {
            context.owner_id,
            current.entity_id,
            context.activation_id,
            AbilityId::NinjaSpectralKunai,
            NinjaDamagePass::
                SpectralKunaiOutward,
            static_cast<std::uint8_t>(
                result.hit_count),
            base_damage * power,
        };
        const auto applied =
            callbacks.apply_damage(
                damage_request);
        auto& outcome =
            result.hits[result.hit_count++];
        outcome.target_id = current.entity_id;
        outcome.pass =
            NinjaDamagePass::
                SpectralKunaiOutward;
        outcome.requested_damage =
            damage_request.damage;
        outcome.applied_damage =
            std::isfinite(
                applied.applied_damage)
                ? std::max(
                      applied.applied_damage,
                      0.0F)
                : 0.0F;
        outcome.hit = applied.hit;
        outcome.killed =
            applied.hit && applied.killed;
        ++result.outward_hit_count;

        if (outward_index == 0U &&
            outcome.hit) {
            apply_mark(
                context,
                current.entity_id,
                tuning.mark_melee_bonus);
            result.mark_applied = true;
        }
        if (outward_index + 1U >=
            maximum_outward_hits) {
            break;
        }

        const NinjaKunaiTargetQuery query {
            context.owner_id,
            current.entity_id,
            current.position,
            std::span<const NinjaEntityId> {
                visited_ids.data(),
                visited_count,
            },
            static_cast<std::uint8_t>(
                outward_index + 1U),
        };
        const auto next =
            callbacks.select_kunai_target(
                query);
        if (!next.has_value() ||
            !valid_hostile(
                *next,
                context.owner_id) ||
            !unique_entity(
                query.excluded_targets,
                next->entity_id)) {
            break;
        }
        current = *next;
        power *=
            1.0F -
            tuning.bounce_power_loss;
    }

    if (mastery_is_active(context)) {
        for (std::size_t reverse_index =
                 visited_count;
             reverse_index > 0U;
             --reverse_index) {
            const auto index =
                reverse_index - 1U;
            const auto damage =
                base_damage *
                powers[index] *
                tuning.mastery_return_power;
            const NinjaDamageRequest
                damage_request {
                    context.owner_id,
                    visited_ids[index],
                    context.activation_id,
                    AbilityId::
                        NinjaSpectralKunai,
                    NinjaDamagePass::
                        SpectralKunaiReturn,
                    static_cast<std::uint8_t>(
                        result.hit_count),
                    damage,
                };
            const auto applied =
                callbacks.apply_damage(
                    damage_request);
            auto& outcome =
                result.hits[
                    result.hit_count++];
            outcome.target_id =
                visited_ids[index];
            outcome.pass =
                NinjaDamagePass::
                    SpectralKunaiReturn;
            outcome.requested_damage =
                damage;
            outcome.applied_damage =
                std::isfinite(
                    applied.applied_damage)
                    ? std::max(
                          applied.applied_damage,
                          0.0F)
                    : 0.0F;
            outcome.hit = applied.hit;
            outcome.killed =
                applied.hit &&
                applied.killed;
            ++result.return_hit_count;
        }
    }

    result.cast = true;
    return result;
}

auto NinjaAbilityEffects::consume_spectral_mark(
    const NinjaMeleeMarkRequest& request) noexcept
    -> NinjaMeleeMarkResult {
    if (request.owner_id == 0U ||
        request.target_id == 0U ||
        !request.melee_hit ||
        !std::isfinite(
            request.melee_damage) ||
        request.melee_damage < 0.0F) {
        return {};
    }
    for (auto& mark : marks_) {
        if (!mark.active ||
            mark.owner_id != request.owner_id ||
            mark.target_id != request.target_id) {
            continue;
        }
        const auto bonus =
            request.melee_damage *
            mark.melee_bonus;
        const auto fraction =
            mark.melee_bonus;
        mark = {};
        return {
            true,
            fraction,
            bonus,
        };
    }
    return {};
}

auto NinjaAbilityEffects::update(
    float dt,
    const NinjaWorldCallbacks& callbacks)
    -> NinjaFixedUpdateResult {
    NinjaFixedUpdateResult result {};
    if (!std::isfinite(dt) ||
        dt < 0.0F) {
        return result;
    }

    result.accepted = true;
    fixed_accumulator_seconds_ +=
        static_cast<double>(dt);
    const auto maximum_accumulator =
        static_cast<double>(
            kNinjaMaximumTicksPerUpdate) *
        kNinjaFixedStepSeconds;
    if (fixed_accumulator_seconds_ >
        maximum_accumulator) {
        fixed_accumulator_seconds_ =
            maximum_accumulator;
        result.saturated = true;
    }

    const auto available_ticks =
        static_cast<std::uint64_t>(
            std::floor(
                fixed_accumulator_seconds_ /
                    kNinjaFixedStepSeconds +
                1.0e-7));
    result.simulated_ticks =
        static_cast<std::uint32_t>(
            std::min<std::uint64_t>(
                available_ticks,
                kNinjaMaximumTicksPerUpdate));
    fixed_accumulator_seconds_ -=
        static_cast<double>(
            result.simulated_ticks) *
        kNinjaFixedStepSeconds;
    fixed_accumulator_seconds_ =
        std::max(
            fixed_accumulator_seconds_,
            0.0);

    for (std::uint32_t tick = 0U;
         tick < result.simulated_ticks;
         ++tick) {
        update_one_tick(
            callbacks,
            result);
    }
    return result;
}

auto NinjaAbilityEffects::smoke_state() const noexcept
    -> NinjaSmokeStateView {
    if (!smoke_.active) {
        return {};
    }
    const auto tuning =
        ninja_smoke_bomb_tuning(
            smoke_.rank);
    return {
        smoke_.active,
        smoke_.owner_id,
        smoke_.activation_id,
        smoke_.center,
        tuning.radius,
        static_cast<float>(
            smoke_.remaining_ticks) /
            60.0F,
        smoke_.rank,
        smoke_.mastered,
        smoke_.attack_bonus_available,
    };
}

auto NinjaAbilityEffects::
    available_lightning_dash_charges() const noexcept
    -> std::uint8_t {
    return static_cast<std::uint8_t>(
        std::count(
            dash_charge_ticks_.begin(),
            dash_charge_ticks_.end(),
            0U));
}

auto NinjaAbilityEffects::
    lightning_dash_charge_state() const noexcept
    -> NinjaLightningDashChargeState {
    NinjaLightningDashChargeState result {};
    for (std::size_t index = 0U;
         index < dash_charge_ticks_.size();
         ++index) {
        result.remaining_recharge_seconds[index] =
            static_cast<float>(
                dash_charge_ticks_[index]) /
            60.0F;
    }
    return result;
}

auto NinjaAbilityEffects::
    restore_lightning_dash_charge_state(
        const NinjaLightningDashChargeState& state) noexcept -> bool {
    const auto maximum =
        ninja_lightning_dash_tuning(
            NinjaAbilityRank::RankThree)
            .mastery_charge_recharge_seconds;
    for (const auto seconds :
         state.remaining_recharge_seconds) {
        if (!std::isfinite(seconds) ||
            seconds < 0.0F ||
            seconds > maximum) {
            return false;
        }
    }

    std::array<std::uint32_t, 2U> restored {};
    for (std::size_t index = 0U;
         index < restored.size();
         ++index) {
        restored[index] =
            static_cast<std::uint32_t>(
                std::ceil(
                    state
                        .remaining_recharge_seconds[
                            index] *
                        60.0F -
                    1.0e-6F));
    }
    dash_charge_ticks_ = restored;
    return true;
}

auto NinjaAbilityEffects::active_mark_count() const noexcept
    -> std::size_t {
    return static_cast<std::size_t>(
        std::count_if(
            marks_.begin(),
            marks_.end(),
            [](const MarkState& mark) {
                return mark.active;
            }));
}

auto NinjaAbilityEffects::
    free_shinobi_impulse_available() const noexcept
    -> bool {
    return free_second_impulse_available_;
}

void NinjaAbilityEffects::clear() noexcept {
    fixed_accumulator_seconds_ = 0.0;
    simulation_tick_ = 0U;
    smoke_ = {};
    smoke_observers_.fill({});
    landing_ = {};
    marks_.fill({});
    recent_dash_hits_.fill({});
    dash_charge_ticks_.fill(0U);
    aerial_bond_owner_ = 0U;
    aerial_bond_used_ = false;
    free_second_impulse_available_ = false;
}

void NinjaAbilityEffects::update_one_tick(
    const NinjaWorldCallbacks& callbacks,
    NinjaFixedUpdateResult& result) {
    ++simulation_tick_;

    if (smoke_.active) {
        update_smoke_tick(
            callbacks,
            result);
        if (smoke_.remaining_ticks > 0U) {
            --smoke_.remaining_ticks;
        }
        if (smoke_.remaining_ticks == 0U) {
            smoke_ = {};
            clear_smoke_observers();
            result.smoke_expired = true;
        }
    }

    if (landing_.active &&
        landing_.remaining_ticks > 0U) {
        --landing_.remaining_ticks;
        if (landing_.remaining_ticks == 0U) {
            landing_ = {};
            result.landing_window_expired =
                true;
        }
    }

    for (auto& mark : marks_) {
        if (!mark.active ||
            mark.remaining_ticks == 0U) {
            continue;
        }
        --mark.remaining_ticks;
        if (mark.remaining_ticks == 0U) {
            mark = {};
            ++result.expired_mark_count;
        }
    }

    for (auto& recent : recent_dash_hits_) {
        if (!recent.active ||
            recent.remaining_ticks == 0U) {
            continue;
        }
        --recent.remaining_ticks;
        if (recent.remaining_ticks == 0U) {
            recent = {};
        }
    }

    for (auto& charge : dash_charge_ticks_) {
        if (charge == 0U) {
            continue;
        }
        --charge;
        if (charge == 0U) {
            ++result
                  .recharged_dash_charge_count;
        }
    }
}

void NinjaAbilityEffects::update_smoke_tick(
    const NinjaWorldCallbacks& callbacks,
    NinjaFixedUpdateResult& result) {
    if (!callbacks.entity_snapshot) {
        clear_smoke_observers();
        return;
    }

    std::array<
        NinjaEntitySnapshot,
        kNinjaMaximumSmokeProtectedActors>
        protected_actors {};
    std::size_t protected_count = 0U;
    const auto tuning =
        ninja_smoke_bomb_tuning(
            smoke_.rank);
    const auto radius_squared =
        tuning.radius * tuning.radius;
    const auto owner =
        callbacks.entity_snapshot(
            smoke_.owner_id);
    if (owner.has_value() &&
        owner->alive &&
        finite_vector(owner->position) &&
        distance_squared(
            owner->position,
            smoke_.center) <=
            radius_squared) {
        protected_actors[protected_count++] =
            *owner;
        if (callbacks.apply_modifier) {
            callbacks.apply_modifier({
                smoke_.owner_id,
                smoke_.owner_id,
                smoke_.activation_id,
                NinjaModifierKind::
                    MovementSpeed,
                tuning.ninja_speed_bonus,
                static_cast<float>(
                    kNinjaFixedStepSeconds *
                    2.0),
            });
            ++result
                  .modifier_application_count;
        }
    }

    if (smoke_.mastered &&
        callbacks.query_entities) {
        std::array<
            NinjaEntitySnapshot,
            kNinjaMaximumQueryEntities>
            entities {};
        bool saturated = false;
        const NinjaEntityQuery query {
            NinjaEntityQueryKind::InsideSmoke,
            smoke_.owner_id,
            0U,
            smoke_.center,
            smoke_.center,
            smoke_.center,
            tuning.radius,
        };
        const auto count =
            collect_entities(
                callbacks,
                query,
                entities,
                saturated);
        result.saturated =
            result.saturated || saturated;
        for (std::size_t index = 0U;
             index < count;
             ++index) {
            const auto& entity =
                entities[index];
            if (entity.entity_id == 0U ||
                entity.entity_id ==
                    smoke_.owner_id ||
                !entity.alive ||
                !entity.allied_with_owner ||
                !finite_vector(
                    entity.position) ||
                distance_squared(
                    entity.position,
                    smoke_.center) >
                    radius_squared) {
                continue;
            }
            if (protected_count <
                protected_actors.size()) {
                protected_actors[
                    protected_count++] =
                    entity;
            } else {
                result.saturated = true;
            }
            if (callbacks.apply_modifier) {
                callbacks.apply_modifier({
                    smoke_.owner_id,
                    entity.entity_id,
                    smoke_.activation_id,
                    NinjaModifierKind::
                        MovementSpeed,
                    tuning.allied_speed_bonus,
                    static_cast<float>(
                        kNinjaFixedStepSeconds *
                        2.0),
                });
                ++result
                      .modifier_application_count;
            }
        }
    }

    if (callbacks.query_entities &&
        callbacks.smoke_occludes &&
        callbacks.break_target_lock) {
        for (std::size_t index = 0U;
             index < protected_count;
             ++index) {
            update_smoke_target(
                protected_actors[index],
                callbacks,
                result);
        }
    } else {
        clear_smoke_observers();
        return;
    }

    for (auto& observer : smoke_observers_) {
        if (observer.active &&
            observer.last_seen_tick !=
                simulation_tick_) {
            observer = {};
        }
    }
}

void NinjaAbilityEffects::update_smoke_target(
    const NinjaEntitySnapshot& target,
    const NinjaWorldCallbacks& callbacks,
    NinjaFixedUpdateResult& result) {
    std::array<
        NinjaEntitySnapshot,
        kNinjaMaximumQueryEntities>
        enemies {};
    bool saturated = false;
    const auto tuning =
        ninja_smoke_bomb_tuning(
            smoke_.rank);
    const NinjaEntityQuery query {
        NinjaEntityQueryKind::LockingTarget,
        smoke_.owner_id,
        target.entity_id,
        smoke_.center,
        target.position,
        target.position,
        tuning.radius,
    };
    const auto count =
        collect_entities(
            callbacks,
            query,
            enemies,
            saturated);
    result.saturated =
        result.saturated || saturated;
    NinjaEntityId previous_id = 0U;
    for (std::size_t index = 0U;
         index < count;
         ++index) {
        const auto& enemy = enemies[index];
        if (!valid_hostile(
                enemy,
                smoke_.owner_id) ||
            enemy.entity_id == previous_id ||
            enemy.target_id !=
                target.entity_id) {
            continue;
        }
        previous_id = enemy.entity_id;

        const auto obscured =
            callbacks.smoke_occludes({
                enemy.entity_id,
                target.entity_id,
                enemy.position,
                target.position,
                smoke_.center,
                tuning.radius,
            });

        auto observer =
            std::find_if(
                smoke_observers_.begin(),
                smoke_observers_.end(),
                [&](const SmokeObserverState&
                        candidate) {
                    return candidate.active &&
                           candidate.enemy_id ==
                               enemy.entity_id &&
                           candidate.target_id ==
                               target.entity_id;
                });
        if (observer ==
                smoke_observers_.end() &&
            obscured) {
            observer =
                std::find_if(
                    smoke_observers_.begin(),
                    smoke_observers_.end(),
                    [](const SmokeObserverState&
                           candidate) {
                        return !candidate.active;
                    });
            if (observer ==
                smoke_observers_.end()) {
                result.saturated = true;
                continue;
            }
            observer->enemy_id =
                enemy.entity_id;
            observer->target_id =
                target.entity_id;
            observer->active = true;
        }
        if (observer ==
            smoke_observers_.end()) {
            continue;
        }

        observer->last_seen_tick =
            simulation_tick_;
        if (!obscured) {
            observer->obscured_ticks = 0U;
            continue;
        }
        ++observer->obscured_ticks;
        if (observer->obscured_ticks <
            ticks_from_seconds(
                tuning
                    .lock_break_delay_seconds)) {
            continue;
        }

        callbacks.break_target_lock({
            enemy.entity_id,
            target.entity_id,
            smoke_.activation_id,
        });
        ++result.lock_break_count;
        *observer = {};
    }
}

void NinjaAbilityEffects::
    clear_smoke_observers() noexcept {
    smoke_observers_.fill({});
}

void NinjaAbilityEffects::record_recent_dash_hit(
    NinjaEntityId target_id) noexcept {
    auto existing =
        std::find_if(
            recent_dash_hits_.begin(),
            recent_dash_hits_.end(),
            [&](const RecentDashHitState&
                    hit) {
                return hit.active &&
                       hit.target_id ==
                           target_id;
            });
    if (existing ==
        recent_dash_hits_.end()) {
        existing =
            std::find_if(
                recent_dash_hits_.begin(),
                recent_dash_hits_.end(),
                [](const RecentDashHitState&
                       hit) {
                    return !hit.active;
                });
    }
    if (existing ==
        recent_dash_hits_.end()) {
        return;
    }
    existing->target_id = target_id;
    existing->remaining_ticks =
        ticks_from_seconds(
            ninja_lightning_dash_tuning(
                NinjaAbilityRank::RankThree)
                .mastery_repeat_target_interval_seconds);
    existing->active = true;
}

auto NinjaAbilityEffects::recent_dash_hit_blocked(
    NinjaEntityId target_id) const noexcept -> bool {
    return std::any_of(
        recent_dash_hits_.begin(),
        recent_dash_hits_.end(),
        [&](const RecentDashHitState& hit) {
            return hit.active &&
                   hit.target_id == target_id &&
                   hit.remaining_ticks > 0U;
        });
}

void NinjaAbilityEffects::apply_dash_kill_reduction(
    const NinjaCastContext& context,
    std::size_t consumed_charge_index,
    float fraction,
    const NinjaWorldCallbacks& callbacks) {
    if (!std::isfinite(fraction) ||
        fraction <= 0.0F) {
        return;
    }
    const auto bounded_fraction =
        std::clamp(fraction, 0.0F, 1.0F);
    if (mastery_is_active(context) &&
        consumed_charge_index <
            dash_charge_ticks_.size()) {
        const auto remaining =
            static_cast<float>(
                dash_charge_ticks_[
                    consumed_charge_index]);
        dash_charge_ticks_[
            consumed_charge_index] =
            static_cast<std::uint32_t>(
                std::ceil(
                    remaining *
                    (1.0F -
                     bounded_fraction)));
        return;
    }
    if (callbacks.reduce_cooldown) {
        callbacks.reduce_cooldown({
            context.owner_id,
            AbilityId::NinjaLightningDash,
            bounded_fraction,
        });
    }
}

void NinjaAbilityEffects::apply_mark(
    const NinjaCastContext& context,
    NinjaEntityId target_id,
    float bonus) noexcept {
    auto destination =
        std::find_if(
            marks_.begin(),
            marks_.end(),
            [&](const MarkState& mark) {
                return mark.active &&
                       mark.owner_id ==
                           context.owner_id &&
                       mark.target_id ==
                           target_id;
            });
    if (destination == marks_.end()) {
        destination =
            std::find_if(
                marks_.begin(),
                marks_.end(),
                [](const MarkState& mark) {
                    return !mark.active;
                });
    }
    if (destination == marks_.end()) {
        // Je remplace d'abord la marque la plus proche de l'expiration pour
        // garder une limite mémoire stricte et un choix parfaitement stable.
        destination =
            std::min_element(
                marks_.begin(),
                marks_.end(),
                [](const MarkState& lhs,
                   const MarkState& rhs) {
                    if (lhs.remaining_ticks !=
                        rhs.remaining_ticks) {
                        return lhs.remaining_ticks <
                               rhs.remaining_ticks;
                    }
                    return lhs.target_id <
                           rhs.target_id;
                });
    }
    *destination = {
        context.owner_id,
        target_id,
        context.activation_id,
        ticks_from_seconds(
            ninja_spectral_kunai_tuning(
                context.rank)
                .mark_duration_seconds),
        bonus,
        true,
    };
}

} // namespace valcraft
