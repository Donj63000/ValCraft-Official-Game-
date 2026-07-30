#include "gameplay/progression/PlayerAbilityEffects.h"

#include <algorithm>
#include <cmath>

namespace valcraft {

namespace {

[[nodiscard]] auto finite_non_negative(
    float value) noexcept -> float {
    return std::isfinite(value)
               ? std::max(value, 0.0F)
               : 0.0F;
}

struct IronGuardSnapshotInspection {
    bool has_any_effect = false;
    bool coherent = false;
    std::size_t effect_count = 0U;
};

[[nodiscard]] auto inspect_iron_guard_snapshot(
    const StatusEffectSystemSnapshot& snapshot) noexcept
    -> IronGuardSnapshotInspection {
    auto has_damage_reduction = false;
    auto has_knockback_resistance = false;
    auto expected_ticks = std::uint64_t {0U};
    auto coherent = true;
    auto has_any_effect = false;
    auto effect_count =
        std::size_t {0U};

    for (const auto& entry : snapshot.entries) {
        if (!entry.active ||
            entry.target_id !=
                kPlayerStatusEffectTargetId ||
            entry.stack_tag !=
                kIronGuardStatusStackTag) {
            continue;
        }
        has_any_effect = true;
        ++effect_count;
        if (expected_ticks == 0U) {
            expected_ticks =
                entry.remaining_ticks;
        } else if (
            entry.remaining_ticks !=
            expected_ticks) {
            coherent = false;
        }

        switch (entry.kind) {
        case StatusEffectKind::DamageReduction:
            has_damage_reduction = true;
            break;
        case StatusEffectKind::KnockbackResistance:
            has_knockback_resistance = true;
            break;
        case StatusEffectKind::FrontalProjectileReduction:
        case StatusEffectKind::FirstAbsorption:
            break;
        default:
            coherent = false;
            break;
        }
    }

    return {
        has_any_effect,
        has_any_effect &&
            coherent &&
            has_damage_reduction &&
            has_knockback_resistance,
        effect_count,
    };
}

} // namespace

auto PlayerAbilityEffects::activate_iron_guard(
    const AbilityCastResolution& resolution,
    bool shield_equipped) noexcept
    -> IronGuardActivationResult {
    if (resolution.id !=
            AbilityId::KnightIronGuard ||
        resolution.cast_sequence == 0U ||
        resolution.rank == 0U ||
        resolution.rank >
            kAbilityRankCount) {
        return {
            false,
            StatusEffectApplyError::InvalidKind,
        };
    }

    const auto damage_reduction =
        finite_non_negative(
            resolution.values[0U]);
    const auto knockback_resistance =
        finite_non_negative(
            resolution.values[1U]);
    const auto frontal_projectile_reduction =
        resolution.rank >= 3U &&
                shield_equipped
            ? finite_non_negative(
                  resolution.values[2U])
            : 0.0F;
    const auto application =
        status_effects_.apply_iron_guard({
            kPlayerStatusEffectTargetId,
            kIronGuardStatusStackTag,
            resolution.duration_seconds,
            damage_reduction,
            knockback_resistance,
            frontal_projectile_reduction,
            resolution.mastery_active,
        });
    if (!application.applied) {
        return {
            false,
            application.error,
        };
    }

    // Je ne remplace les métadonnées de l'activation qu'après le commit
    // atomique des statuts, afin qu'un échec ne laisse aucun effet réactif.
    iron_guard_cast_sequence_ =
        resolution.cast_sequence;
    iron_guard_wave_damage_ =
        finite_non_negative(
            resolution.values[3U]);
    iron_guard_wave_radius_ =
        finite_non_negative(
            resolution.values[4U]);
    iron_guard_energy_refund_ =
        finite_non_negative(
            resolution.values[5U]);
    return {
        true,
        StatusEffectApplyError::None,
    };
}

auto PlayerAbilityEffects::update(
    float dt) noexcept
    -> PlayerAbilityEffectsUpdateResult {
    const auto was_active =
        iron_guard_active();
    const auto previous_sequence =
        iron_guard_cast_sequence_;
    const auto update =
        status_effects_.update(
            dt);

    PlayerAbilityEffectsUpdateResult result {};
    result.expired_effect_count =
        update.expired_effect_count;
    if (update.accepted &&
        was_active &&
        !iron_guard_active()) {
        result.iron_guard_expired = true;
        result.iron_guard_cast_sequence =
            previous_sequence;
        iron_guard_cast_sequence_ = 0U;
        iron_guard_wave_damage_ = 0.0F;
        iron_guard_wave_radius_ = 0.0F;
        iron_guard_energy_refund_ = 0.0F;
    }
    return result;
}

auto PlayerAbilityEffects::aggregate(
    float maximum_health) const noexcept
    -> StatusEffectAggregate {
    return status_effects_.aggregate(
        kPlayerStatusEffectTargetId,
        maximum_health);
}

auto PlayerAbilityEffects::
    consume_iron_guard_absorption() noexcept
    -> IronGuardReactiveResult {
    const auto consumed =
        status_effects_
            .consume_first_absorption(
                kPlayerStatusEffectTargetId,
                kIronGuardStatusStackTag);
    if (!consumed.consumed ||
        iron_guard_cast_sequence_ == 0U) {
        return {};
    }
    return {
        true,
        iron_guard_cast_sequence_,
        iron_guard_wave_damage_,
        iron_guard_wave_radius_,
        iron_guard_energy_refund_,
    };
}

auto PlayerAbilityEffects::intercept_iron_guard_damage(
    float incoming_damage) noexcept
    -> IronGuardDamageInterceptionResult {
    const auto absorption =
        status_effects_.absorb_first_hit(
            kPlayerStatusEffectTargetId,
            incoming_damage,
            kIronGuardStatusStackTag);
    IronGuardDamageInterceptionResult result {};
    result.accepted = absorption.accepted;
    result.absorbed = absorption.absorbed;
    result.requested_damage =
        absorption.requested_damage;
    result.absorbed_damage =
        absorption.absorbed_damage;
    result.remaining_damage =
        absorption.remaining_damage;
    if (!absorption.absorbed ||
        iron_guard_cast_sequence_ == 0U) {
        return result;
    }

    result.reactive = {
        true,
        iron_guard_cast_sequence_,
        iron_guard_wave_damage_,
        iron_guard_wave_radius_,
        iron_guard_energy_refund_,
    };
    return result;
}

auto PlayerAbilityEffects::iron_guard_active() const noexcept
    -> bool {
    return status_effects_.has_effect(
        kPlayerStatusEffectTargetId,
        StatusEffectKind::DamageReduction,
        kIronGuardStatusStackTag);
}

auto PlayerAbilityEffects::iron_guard_cast_sequence() const noexcept
    -> AbilityCastSequence {
    return iron_guard_cast_sequence_;
}

auto PlayerAbilityEffects::clear_slow_effects() noexcept
    -> std::size_t {
    return status_effects_.clear_kind(
        kPlayerStatusEffectTargetId,
        StatusEffectKind::Slow);
}

auto PlayerAbilityEffects::snapshot() const noexcept
    -> PlayerAbilityEffectsSnapshot {
    return {
        status_effects_.snapshot(),
        iron_guard_cast_sequence_,
        iron_guard_wave_damage_,
        iron_guard_wave_radius_,
        iron_guard_energy_refund_,
    };
}

auto PlayerAbilityEffects::load_state(
    const PlayerAbilityEffectsSnapshot& requested) noexcept
    -> PlayerAbilityEffectsLoadResult {
    PlayerAbilityEffects staged {};
    PlayerAbilityEffectsLoadResult result {};
    result.status_effects =
        staged.status_effects_.load_state(
            requested.status_effects);
    result.sanitized =
        result.status_effects.sanitized;

    const auto normalized_status =
        staged.status_effects_.snapshot();
    const auto inspection =
        inspect_iron_guard_snapshot(
            normalized_status);
    const auto valid_metadata =
        requested.iron_guard_cast_sequence != 0U &&
        std::isfinite(
            requested.iron_guard_wave_damage) &&
        requested.iron_guard_wave_damage >= 0.0F &&
        std::isfinite(
            requested.iron_guard_wave_radius) &&
        requested.iron_guard_wave_radius >= 0.0F &&
        std::isfinite(
            requested.iron_guard_energy_refund) &&
        requested.iron_guard_energy_refund >= 0.0F;

    if (inspection.coherent &&
        valid_metadata) {
        staged.iron_guard_cast_sequence_ =
            requested
                .iron_guard_cast_sequence;
        staged.iron_guard_wave_damage_ =
            requested
                .iron_guard_wave_damage;
        staged.iron_guard_wave_radius_ =
            requested
                .iron_guard_wave_radius;
        staged.iron_guard_energy_refund_ =
            requested
                .iron_guard_energy_refund;
        result.iron_guard_restored = true;
    } else if (inspection.has_any_effect) {
        // Je supprime toute garde composite incohérente : je ne restaure
        // jamais une réduction orpheline ou un jeton sans métadonnées.
        staged.status_effects_.clear_stack(
            kPlayerStatusEffectTargetId,
            kIronGuardStatusStackTag);
        result.status_effects
            .restored_effect_count -=
            std::min(
                result.status_effects
                    .restored_effect_count,
                inspection.effect_count);
        result.status_effects
            .discarded_effect_count +=
            inspection.effect_count;
        result.status_effects.sanitized =
            true;
        result.sanitized = true;
    } else if (
        requested.iron_guard_cast_sequence != 0U ||
        requested.iron_guard_wave_damage != 0.0F ||
        requested.iron_guard_wave_radius != 0.0F ||
        requested.iron_guard_energy_refund != 0.0F) {
        result.sanitized = true;
    }

    *this = staged;
    return result;
}

void PlayerAbilityEffects::clear() noexcept {
    status_effects_.clear();
    iron_guard_cast_sequence_ = 0U;
    iron_guard_wave_damage_ = 0.0F;
    iron_guard_wave_radius_ = 0.0F;
    iron_guard_energy_refund_ = 0.0F;
}

} // namespace valcraft
