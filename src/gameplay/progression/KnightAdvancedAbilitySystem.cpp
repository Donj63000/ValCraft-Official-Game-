#include "gameplay/progression/KnightAdvancedAbilitySystem.h"

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace valcraft {

namespace {

[[nodiscard]] auto finite_vector(const glm::vec3& value) noexcept -> bool {
    return std::isfinite(value.x) &&
           std::isfinite(value.y) &&
           std::isfinite(value.z);
}

[[nodiscard]] auto valid_weight(KnightTargetWeight weight) noexcept -> bool {
    switch (weight) {
    case KnightTargetWeight::Light:
    case KnightTargetWeight::Normal:
    case KnightTargetWeight::Heavy:
    case KnightTargetWeight::Boss:
        return true;
    }
    return false;
}

[[nodiscard]] auto valid_traversal(
    KnightChargeTraversal traversal) noexcept -> bool {
    switch (traversal) {
    case KnightChargeTraversal::Safe:
    case KnightChargeTraversal::WorldBlocked:
    case KnightChargeTraversal::ChunkNotReady:
        return true;
    }
    return false;
}

[[nodiscard]] auto valid_relation(
    KnightTargetRelation relation) noexcept -> bool {
    switch (relation) {
    case KnightTargetRelation::Ally:
    case KnightTargetRelation::Enemy:
    case KnightTargetRelation::Neutral:
        return true;
    }
    return false;
}

template <std::size_t Capacity>
[[nodiscard]] auto contains_id(
    const std::array<KnightEntityId, Capacity>& ids,
    std::size_t count,
    KnightEntityId expected) noexcept -> bool {
    const auto bounded_count = std::min(count, ids.size());
    return std::find(
               ids.begin(),
               ids.begin() +
                   static_cast<std::ptrdiff_t>(bounded_count),
               expected) !=
           ids.begin() +
               static_cast<std::ptrdiff_t>(bounded_count);
}

[[nodiscard]] auto remaining_seconds(std::uint64_t ticks) noexcept -> float {
    return static_cast<float>(ticks) *
           kKnightAbilityFixedStepSeconds;
}

} // namespace

auto KnightAdvancedAbilitySystem::execute_bulwark_charge(
    const KnightBulwarkChargeRequest& request,
    const KnightBulwarkChargeCallbacks& callbacks) noexcept
    -> KnightBulwarkChargeResult {
    KnightBulwarkChargeResult result {};
    const auto* definition =
        knight_bulwark_charge_definition(request.rank);
    if (definition == nullptr) {
        result.error = KnightAdvancedAbilityError::InvalidRank;
        return result;
    }
    if (request.activation_id == 0U ||
        request.activation_id <= last_charge_activation_id_) {
        result.error = KnightAdvancedAbilityError::InvalidActivation;
        return result;
    }
    if (request.caster_id == 0U) {
        result.error = KnightAdvancedAbilityError::InvalidEntity;
        return result;
    }
    if (callbacks.probe_step == nullptr ||
        callbacks.commit == nullptr) {
        result.error = KnightAdvancedAbilityError::MissingCallback;
        return result;
    }
    if (!finite_vector(request.start_position) ||
        !finite_vector(request.direction) ||
        !std::isfinite(request.weapon_damage) ||
        request.weapon_damage < 0.0F) {
        result.error = KnightAdvancedAbilityError::InvalidInput;
        return result;
    }

    const auto horizontal_direction = glm::vec3 {
        request.direction.x,
        0.0F,
        request.direction.z,
    };
    const auto direction_length = glm::length(horizontal_direction);
    if (!std::isfinite(direction_length) ||
        direction_length <= 0.0001F) {
        result.error = KnightAdvancedAbilityError::InvalidInput;
        return result;
    }
    const auto direction = horizontal_direction / direction_length;

    KnightBulwarkChargeCommitRequest committed {};
    committed.activation_id = request.activation_id;
    committed.caster_id = request.caster_id;
    committed.rank = request.rank;
    committed.start_position = request.start_position;
    committed.final_position = request.start_position;
    committed.direction = direction;

    auto current_position = request.start_position;
    auto remaining_distance = definition->distance_meters;
    std::array<KnightEntityId, kKnightMaximumChargeTargets> processed_targets {};
    std::size_t processed_target_count = 0U;
    bool stop = false;

    while (remaining_distance > 0.0001F && !stop) {
        if (committed.safe_path_count >= committed.safe_path.size()) {
            result.error = KnightAdvancedAbilityError::CapacityExceeded;
            return result;
        }
        const auto step_distance =
            std::min(kKnightMaximumChargeStepMeters, remaining_distance);
        const auto candidate =
            current_position + direction * step_distance;
        if (!finite_vector(candidate) ||
            !std::isfinite(step_distance) ||
            step_distance <= 0.0F ||
            step_distance >
                kKnightMaximumChargeStepMeters + 0.00001F) {
            result.error = KnightAdvancedAbilityError::InvalidInput;
            return result;
        }

        const auto probe = callbacks.probe_step(
            callbacks.user_data,
            {
                request.activation_id,
                current_position,
                candidate,
                step_distance,
                static_cast<std::uint8_t>(committed.safe_path_count),
            });
        if (!valid_traversal(probe.traversal) ||
            probe.contact_count > probe.contacts.size() ||
            (probe.enemy_blocks_path &&
             (probe.traversal != KnightChargeTraversal::Safe ||
              probe.contact_count == 0U))) {
            result.error =
                KnightAdvancedAbilityError::InvalidCallbackResult;
            return result;
        }
        if (probe.traversal == KnightChargeTraversal::WorldBlocked) {
            committed.stopped_by_world = true;
            break;
        }
        if (probe.traversal == KnightChargeTraversal::ChunkNotReady) {
            committed.stopped_by_unready_chunk = true;
            break;
        }

        bool contacted_enemy = false;
        for (std::size_t contact_index = 0U;
             contact_index < probe.contact_count;
             ++contact_index) {
            const auto& contact = probe.contacts[contact_index];
            if (contact.target_id == 0U ||
                !valid_weight(contact.weight)) {
                result.error =
                    KnightAdvancedAbilityError::InvalidCallbackResult;
                return result;
            }
            contacted_enemy = true;
            if (contains_id(
                    processed_targets,
                    processed_target_count,
                    contact.target_id)) {
                continue;
            }
            if (processed_target_count < processed_targets.size()) {
                processed_targets[processed_target_count++] =
                    contact.target_id;
            }
            if (committed.hit_count >= definition->maximum_targets) {
                continue;
            }

            auto& hit = committed.hits[committed.hit_count++];
            hit.target_id = contact.target_id;
            hit.weight = contact.weight;
            hit.damage =
                request.weapon_damage *
                definition->weapon_damage_multiplier;
            hit.knockback_multiplier =
                knight_knockback_multiplier(contact.weight);
            if (!std::isfinite(hit.damage)) {
                result.error = KnightAdvancedAbilityError::InvalidInput;
                return result;
            }
        }

        const auto single_target_charge =
            definition->maximum_targets == 1U;
        const auto reached_target_limit =
            committed.hit_count >= definition->maximum_targets;
        const auto ends_at_contact =
            contacted_enemy &&
            (single_target_charge ||
             probe.enemy_blocks_path ||
             reached_target_limit);
        if (ends_at_contact) {
            committed.ended_in_enemy_contact = true;
            stop = true;
            continue;
        }

        committed.safe_path[committed.safe_path_count++] = candidate;
        current_position = candidate;
        committed.final_position = candidate;
        committed.travelled_distance_meters += step_distance;
        remaining_distance -= step_distance;

        if (probe.enemy_blocks_path) {
            committed.ended_in_enemy_contact = contacted_enemy;
            stop = true;
        }
    }

    if (!callbacks.commit(callbacks.user_data, committed)) {
        result.error = KnightAdvancedAbilityError::ExternalCommitRejected;
        return result;
    }

    // Je n'arme les suites temporelles qu'après le commit atomique du monde.
    for (auto& pending : pending_wall_impacts_) {
        pending = {};
    }
    if (definition->wall_impact_window_seconds > 0.0F) {
        for (std::size_t index = 0U;
             index < committed.hit_count &&
             index < pending_wall_impacts_.size();
             ++index) {
            const auto& hit = committed.hits[index];
            if (hit.knockback_multiplier <= 0.0F) {
                continue;
            }
            pending_wall_impacts_[index] = {
                request.activation_id,
                hit.target_id,
                seconds_to_ticks(
                    definition->wall_impact_window_seconds),
                true,
            };
        }
    }
    if (request.mastery_active &&
        committed.ended_in_enemy_contact) {
        breach_activation_id_ = request.activation_id;
        breach_remaining_ticks_ = seconds_to_ticks(2.0F);
    }
    last_charge_activation_id_ = request.activation_id;
    result.committed = committed;
    return result;
}

auto KnightAdvancedAbilitySystem::notify_charge_wall_impact(
    KnightEntityId target_id,
    void* user_data,
    KnightWallImpactCommit commit) noexcept
    -> KnightWallImpactResult {
    KnightWallImpactResult result {};
    if (target_id == 0U) {
        result.error = KnightAdvancedAbilityError::InvalidEntity;
        return result;
    }
    if (commit == nullptr) {
        result.error = KnightAdvancedAbilityError::MissingCallback;
        return result;
    }

    const auto pending = std::find_if(
        pending_wall_impacts_.begin(),
        pending_wall_impacts_.end(),
        [target_id](const PendingWallImpact& candidate) {
            return candidate.active &&
                   candidate.remaining_ticks > 0U &&
                   candidate.target_id == target_id;
        });
    if (pending == pending_wall_impacts_.end()) {
        result.error = KnightAdvancedAbilityError::NoMatchingWindow;
        return result;
    }

    result.committed = {
        pending->activation_id,
        pending->target_id,
        1.0F,
        true,
    };
    if (!commit(user_data, result.committed)) {
        result.error = KnightAdvancedAbilityError::ExternalCommitRejected;
        return result;
    }
    *pending = {};
    return result;
}

auto KnightAdvancedAbilitySystem::resolve_breach_melee_hit(
    KnightEntityId target_id,
    float base_damage,
    void* user_data,
    KnightBreachMeleeCommit commit) noexcept
    -> KnightBreachMeleeResult {
    KnightBreachMeleeResult result {};
    if (breach_remaining_ticks_ == 0U ||
        breach_activation_id_ == 0U) {
        result.error = KnightAdvancedAbilityError::NotActive;
        return result;
    }
    if (target_id == 0U) {
        result.error = KnightAdvancedAbilityError::InvalidEntity;
        return result;
    }
    if (commit == nullptr) {
        result.error = KnightAdvancedAbilityError::MissingCallback;
        return result;
    }
    if (!std::isfinite(base_damage) || base_damage <= 0.0F) {
        result.error = KnightAdvancedAbilityError::InvalidInput;
        return result;
    }

    const auto total_damage = base_damage * 1.40F;
    if (!std::isfinite(total_damage)) {
        result.error = KnightAdvancedAbilityError::InvalidInput;
        return result;
    }
    result.committed = {
        breach_activation_id_,
        target_id,
        base_damage,
        total_damage,
        0.40F,
        0.10F,
        5.0F,
        kKnightBreachVulnerabilityStackTag,
    };
    if (!commit(user_data, result.committed)) {
        result.error = KnightAdvancedAbilityError::ExternalCommitRejected;
        return result;
    }

    breach_activation_id_ = 0U;
    breach_remaining_ticks_ = 0U;
    return result;
}

auto KnightAdvancedAbilitySystem::activate_champion_cry(
    const KnightChampionCryRequest& request,
    const KnightChampionCryCallbacks& callbacks) noexcept
    -> KnightChampionCryResult {
    KnightChampionCryResult result {};
    const auto* definition =
        knight_champion_cry_definition(request.rank);
    if (definition == nullptr) {
        result.error = KnightAdvancedAbilityError::InvalidRank;
        return result;
    }
    if (request.activation_id == 0U ||
        request.activation_id <= last_champion_activation_id_) {
        result.error = KnightAdvancedAbilityError::InvalidActivation;
        return result;
    }
    if (request.caster_id == 0U) {
        result.error = KnightAdvancedAbilityError::InvalidEntity;
        return result;
    }
    if (champion_remaining_ticks_ > 0U) {
        result.error = KnightAdvancedAbilityError::Busy;
        return result;
    }
    if (callbacks.query_nearby == nullptr ||
        callbacks.commit == nullptr) {
        result.error = KnightAdvancedAbilityError::MissingCallback;
        return result;
    }
    if (!finite_vector(request.center)) {
        result.error = KnightAdvancedAbilityError::InvalidInput;
        return result;
    }

    const auto nearby = callbacks.query_nearby(
        callbacks.user_data,
        {
            request.activation_id,
            request.caster_id,
            request.center,
            definition->radius_meters,
        });
    if (nearby.target_count > nearby.targets.size()) {
        result.error = KnightAdvancedAbilityError::CapacityExceeded;
        return result;
    }

    KnightChampionCryCommitRequest committed {};
    committed.activation_id = request.activation_id;
    committed.caster_id = request.caster_id;
    committed.rank = request.rank;
    committed.center = request.center;
    committed.radius_meters = definition->radius_meters;
    committed.duration_seconds = definition->duration_seconds;
    committed.self_melee_damage_bonus =
        definition->self_melee_damage_bonus;
    committed.immediate_self_heal =
        definition->immediate_self_heal;

    std::array<KnightEntityId, kKnightMaximumNearbyTargets> seen_ids {};
    std::size_t seen_count = 0U;
    const auto radius_squared =
        definition->radius_meters *
        definition->radius_meters;
    for (std::size_t index = 0U;
         index < nearby.target_count;
         ++index) {
        const auto& target = nearby.targets[index];
        if (target.target_id == 0U ||
            !finite_vector(target.position) ||
            !valid_relation(target.relation)) {
            result.error =
                KnightAdvancedAbilityError::InvalidCallbackResult;
            return result;
        }
        if (target.target_id == request.caster_id ||
            contains_id(seen_ids, seen_count, target.target_id)) {
            continue;
        }
        const auto offset = target.position - request.center;
        const auto distance_squared = glm::dot(offset, offset);
        if (!std::isfinite(distance_squared) ||
            distance_squared > radius_squared + 0.0001F) {
            continue;
        }
        seen_ids[seen_count++] = target.target_id;

        if (target.relation == KnightTargetRelation::Ally) {
            if (committed.ally_count >= committed.allies.size()) {
                result.error = KnightAdvancedAbilityError::CapacityExceeded;
                return result;
            }
            committed.allies[committed.ally_count++] = {
                target.target_id,
                definition->ally_melee_damage_bonus,
                request.mastery_active ? 0.10F : 0.0F,
                definition->duration_seconds,
                kKnightMeleeDamageBuffStackTag,
                KnightEffectStackPolicy::Strongest,
                request.mastery_active,
            };
        } else if (
            target.relation == KnightTargetRelation::Enemy &&
            target.can_be_taunted) {
            if (committed.taunt_count >= committed.taunts.size()) {
                result.error = KnightAdvancedAbilityError::CapacityExceeded;
                return result;
            }
            committed.taunts[committed.taunt_count++] = {
                target.target_id,
                request.caster_id,
                definition->duration_seconds,
            };
        }
    }

    if (!callbacks.commit(callbacks.user_data, committed)) {
        result.error = KnightAdvancedAbilityError::ExternalCommitRejected;
        return result;
    }

    champion_activation_id_ = request.activation_id;
    last_champion_activation_id_ = request.activation_id;
    champion_caster_id_ = request.caster_id;
    champion_remaining_ticks_ =
        seconds_to_ticks(definition->duration_seconds);
    champion_self_melee_bonus_ =
        definition->self_melee_damage_bonus;
    champion_ally_melee_bonus_ =
        definition->ally_melee_damage_bonus;
    champion_mastery_active_ = request.mastery_active;
    champion_ally_count_ = committed.ally_count;
    for (auto& ally : champion_allies_) {
        ally = {};
    }
    for (std::size_t index = 0U;
         index < committed.ally_count;
         ++index) {
        champion_allies_[index] = {
            committed.allies[index].target_id,
            false,
            true,
        };
    }

    result.committed = committed;
    return result;
}

auto KnightAdvancedAbilitySystem::arm_perfect_riposte(
    const KnightPerfectRiposteActivationRequest& request) noexcept
    -> KnightPerfectRiposteActivationResult {
    KnightPerfectRiposteActivationResult result {};
    const auto* definition =
        knight_perfect_riposte_definition(request.rank);
    if (definition == nullptr) {
        result.error = KnightAdvancedAbilityError::InvalidRank;
        return result;
    }
    if (request.activation_id == 0U ||
        request.activation_id <= last_riposte_activation_id_) {
        result.error = KnightAdvancedAbilityError::InvalidActivation;
        return result;
    }
    if (request.caster_id == 0U) {
        result.error = KnightAdvancedAbilityError::InvalidEntity;
        return result;
    }
    if (riposte_remaining_ticks_ > 0U) {
        result.error = KnightAdvancedAbilityError::Busy;
        return result;
    }
    if (!std::isfinite(request.weapon_damage) ||
        request.weapon_damage < 0.0F ||
        !std::isfinite(
            request.weapon_damage *
            definition->counter_weapon_damage_multiplier)) {
        result.error = KnightAdvancedAbilityError::InvalidInput;
        return result;
    }

    riposte_activation_id_ = request.activation_id;
    last_riposte_activation_id_ = request.activation_id;
    riposte_caster_id_ = request.caster_id;
    riposte_rank_ = request.rank;
    riposte_remaining_ticks_ =
        seconds_to_ticks(definition->parry_window_seconds);
    riposte_weapon_damage_ = request.weapon_damage;
    riposte_mastery_active_ = request.mastery_active;
    result.parry_window_seconds =
        definition->parry_window_seconds;
    return result;
}

auto KnightAdvancedAbilitySystem::resolve_incoming_attack(
    const KnightIncomingAttack& attack,
    const KnightPerfectRiposteCallbacks& callbacks) noexcept
    -> KnightPerfectRiposteResult {
    KnightPerfectRiposteResult result {};
    if (riposte_remaining_ticks_ == 0U ||
        riposte_activation_id_ == 0U) {
        result.error = KnightAdvancedAbilityError::NotActive;
        return result;
    }
    if (attack.attack_id == 0U ||
        attack.attacker_id == 0U) {
        result.error = KnightAdvancedAbilityError::InvalidEntity;
        return result;
    }
    if (!std::isfinite(attack.incoming_damage) ||
        attack.incoming_damage <= 0.0F) {
        result.error = KnightAdvancedAbilityError::InvalidInput;
        return result;
    }
    if (!attack.parryable) {
        result.error = KnightAdvancedAbilityError::NotParryable;
        return result;
    }
    if (callbacks.commit == nullptr) {
        result.error = KnightAdvancedAbilityError::MissingCallback;
        return result;
    }

    const auto* definition =
        knight_perfect_riposte_definition(riposte_rank_);
    if (definition == nullptr) {
        result.error = KnightAdvancedAbilityError::InvalidRank;
        return result;
    }
    result.committed.activation_id = riposte_activation_id_;
    result.committed.incoming_attack_id = attack.attack_id;
    result.committed.caster_id = riposte_caster_id_;
    result.committed.attacker_id = attack.attacker_id;
    result.committed.rank = riposte_rank_;
    result.committed.cancelled_incoming_damage =
        attack.incoming_damage;
    result.committed.counter_weapon_damage_multiplier =
        definition->counter_weapon_damage_multiplier;
    result.committed.counter_damage =
        riposte_weapon_damage_ *
        definition->counter_weapon_damage_multiplier;
    result.committed.energy_refund = definition->energy_refund;
    result.committed.emit_secondary_cone =
        definition->secondary_cone_damage > 0.0F;
    result.committed.secondary_cone_damage =
        definition->secondary_cone_damage;
    result.committed.light_target_stun_seconds =
        definition->light_target_stun_seconds;
    result.committed.reset_vanguard_strike_cooldown =
        riposte_mastery_active_;
    result.committed.mastery_damage_reduction =
        riposte_mastery_active_ ? 0.20F : 0.0F;
    result.committed.mastery_damage_reduction_seconds =
        riposte_mastery_active_ ? 1.5F : 0.0F;

    if (!callbacks.commit(callbacks.user_data, result.committed)) {
        result.error = KnightAdvancedAbilityError::ExternalCommitRejected;
        return result;
    }

    riposte_activation_id_ = 0U;
    riposte_caster_id_ = 0U;
    riposte_remaining_ticks_ = 0U;
    riposte_weapon_damage_ = 0.0F;
    if (riposte_mastery_active_) {
        mastery_damage_reduction_ticks_ = seconds_to_ticks(1.5F);
    }
    riposte_mastery_active_ = false;
    result.incoming_attack_cancelled = true;
    return result;
}

auto KnightAdvancedAbilitySystem::update(float elapsed_seconds) noexcept
    -> KnightAdvancedAbilityUpdateResult {
    KnightAdvancedAbilityUpdateResult result {};
    if (!std::isfinite(elapsed_seconds) ||
        elapsed_seconds < 0.0F ||
        elapsed_seconds > kKnightMaximumUpdateSeconds) {
        return result;
    }
    result.accepted = true;

    fixed_step_accumulator_seconds_ +=
        static_cast<double>(elapsed_seconds);
    // Je garde l'horloge logique en double exact à 60 Hz : une seconde
    // entière doit toujours produire soixante ticks, même si l'API reçoit
    // par ailleurs la représentation float de 1 / 60.
    constexpr auto fixed_step = 1.0 / 60.0;
    const auto tick_value = std::floor(
        fixed_step_accumulator_seconds_ /
            fixed_step +
        1.0e-9);
    if (tick_value <= 0.0) {
        return result;
    }
    const auto maximum_ticks =
        static_cast<double>(
            std::numeric_limits<std::uint64_t>::max());
    if (tick_value > maximum_ticks) {
        result.accepted = false;
        return result;
    }

    result.advanced_ticks =
        static_cast<std::uint64_t>(tick_value);
    fixed_step_accumulator_seconds_ -=
        static_cast<double>(result.advanced_ticks) *
        fixed_step;
    if (fixed_step_accumulator_seconds_ < 0.0) {
        fixed_step_accumulator_seconds_ = 0.0;
    }
    advance_ticks(result.advanced_ticks, result);
    return result;
}

auto KnightAdvancedAbilitySystem::consume_champion_ally_interruption(
    KnightEntityId ally_id) noexcept -> bool {
    if (ally_id == 0U ||
        champion_remaining_ticks_ == 0U ||
        !champion_mastery_active_) {
        return false;
    }
    const auto ally = std::find_if(
        champion_allies_.begin(),
        champion_allies_.begin() +
            static_cast<std::ptrdiff_t>(champion_ally_count_),
        [ally_id](const ChampionAllyState& candidate) {
            return candidate.active &&
                   candidate.target_id == ally_id;
        });
    if (ally ==
            champion_allies_.begin() +
                static_cast<std::ptrdiff_t>(champion_ally_count_) ||
        ally->interruption_consumed) {
        return false;
    }
    ally->interruption_consumed = true;
    return true;
}

auto KnightAdvancedAbilitySystem::melee_damage_bonus(
    KnightEntityId target_id) const noexcept -> float {
    if (target_id == 0U ||
        champion_remaining_ticks_ == 0U) {
        return 0.0F;
    }
    if (target_id == champion_caster_id_) {
        return champion_self_melee_bonus_;
    }
    const auto ally = std::find_if(
        champion_allies_.begin(),
        champion_allies_.begin() +
            static_cast<std::ptrdiff_t>(champion_ally_count_),
        [target_id](const ChampionAllyState& candidate) {
            return candidate.active &&
                   candidate.target_id == target_id;
        });
    return ally !=
                   champion_allies_.begin() +
                       static_cast<std::ptrdiff_t>(
                           champion_ally_count_)
               ? champion_ally_melee_bonus_
               : 0.0F;
}

auto KnightAdvancedAbilitySystem::movement_speed_bonus(
    KnightEntityId target_id) const noexcept -> float {
    if (!champion_mastery_active_ ||
        champion_remaining_ticks_ == 0U ||
        target_id == 0U ||
        target_id == champion_caster_id_) {
        return 0.0F;
    }
    const auto ally = std::find_if(
        champion_allies_.begin(),
        champion_allies_.begin() +
            static_cast<std::ptrdiff_t>(champion_ally_count_),
        [target_id](const ChampionAllyState& candidate) {
            return candidate.active &&
                   candidate.target_id == target_id;
        });
    return ally !=
                   champion_allies_.begin() +
                       static_cast<std::ptrdiff_t>(
                           champion_ally_count_)
               ? 0.10F
               : 0.0F;
}

auto KnightAdvancedAbilitySystem::snapshot() const noexcept
    -> KnightAdvancedAbilitySnapshot {
    KnightAdvancedAbilitySnapshot view {};
    view.breach_armed = breach_remaining_ticks_ > 0U;
    view.breach_remaining_seconds =
        remaining_seconds(breach_remaining_ticks_);
    view.champion_cry_active = champion_remaining_ticks_ > 0U;
    view.champion_cry_remaining_seconds =
        remaining_seconds(champion_remaining_ticks_);
    view.perfect_riposte_armed = riposte_remaining_ticks_ > 0U;
    view.perfect_riposte_remaining_seconds =
        remaining_seconds(riposte_remaining_ticks_);
    view.mastery_damage_reduction =
        mastery_damage_reduction_ticks_ > 0U ? 0.20F : 0.0F;
    view.mastery_damage_reduction_remaining_seconds =
        remaining_seconds(mastery_damage_reduction_ticks_);
    view.pending_wall_impact_count =
        static_cast<std::size_t>(std::count_if(
            pending_wall_impacts_.begin(),
            pending_wall_impacts_.end(),
            [](const PendingWallImpact& pending) {
                return pending.active &&
                       pending.remaining_ticks > 0U;
            }));
    return view;
}

void KnightAdvancedAbilitySystem::clear() noexcept {
    for (auto& pending : pending_wall_impacts_) {
        pending = {};
    }
    for (auto& ally : champion_allies_) {
        ally = {};
    }
    champion_ally_count_ = 0U;
    breach_activation_id_ = 0U;
    breach_remaining_ticks_ = 0U;
    last_charge_activation_id_ = 0U;
    champion_activation_id_ = 0U;
    last_champion_activation_id_ = 0U;
    champion_caster_id_ = 0U;
    champion_remaining_ticks_ = 0U;
    champion_self_melee_bonus_ = 0.0F;
    champion_ally_melee_bonus_ = 0.0F;
    champion_mastery_active_ = false;
    riposte_activation_id_ = 0U;
    last_riposte_activation_id_ = 0U;
    riposte_caster_id_ = 0U;
    riposte_rank_ = KnightAbilityRank::RankOne;
    riposte_remaining_ticks_ = 0U;
    riposte_weapon_damage_ = 0.0F;
    riposte_mastery_active_ = false;
    mastery_damage_reduction_ticks_ = 0U;
    fixed_step_accumulator_seconds_ = 0.0;
}

auto KnightAdvancedAbilitySystem::seconds_to_ticks(
    float seconds) noexcept -> std::uint64_t {
    if (!std::isfinite(seconds) || seconds <= 0.0F) {
        return 0U;
    }
    const auto ticks = std::ceil(
        static_cast<double>(seconds) *
            60.0 -
        1.0e-6);
    return ticks > 0.0
               ? static_cast<std::uint64_t>(ticks)
               : 0U;
}

void KnightAdvancedAbilitySystem::advance_ticks(
    std::uint64_t ticks,
    KnightAdvancedAbilityUpdateResult& result) noexcept {
    const auto expire_counter =
        [ticks](std::uint64_t& remaining) noexcept -> bool {
        if (remaining == 0U) {
            return false;
        }
        if (ticks >= remaining) {
            remaining = 0U;
            return true;
        }
        remaining -= ticks;
        return false;
    };

    for (auto& pending : pending_wall_impacts_) {
        if (!pending.active) {
            continue;
        }
        if (expire_counter(pending.remaining_ticks)) {
            pending = {};
            ++result.expired_wall_impact_count;
        }
    }
    if (expire_counter(breach_remaining_ticks_)) {
        breach_activation_id_ = 0U;
        result.breach_expired = true;
    }
    if (expire_counter(champion_remaining_ticks_)) {
        champion_activation_id_ = 0U;
        champion_caster_id_ = 0U;
        champion_self_melee_bonus_ = 0.0F;
        champion_ally_melee_bonus_ = 0.0F;
        champion_mastery_active_ = false;
        champion_ally_count_ = 0U;
        for (auto& ally : champion_allies_) {
            ally = {};
        }
        result.champion_cry_expired = true;
    }
    if (expire_counter(riposte_remaining_ticks_)) {
        riposte_activation_id_ = 0U;
        riposte_caster_id_ = 0U;
        riposte_weapon_damage_ = 0.0F;
        riposte_mastery_active_ = false;
        result.perfect_riposte_expired = true;
    }
    if (expire_counter(mastery_damage_reduction_ticks_)) {
        result.mastery_damage_reduction_expired = true;
    }
}

} // namespace valcraft
