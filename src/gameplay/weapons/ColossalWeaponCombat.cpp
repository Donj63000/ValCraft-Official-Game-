#include "gameplay/weapons/ColossalWeaponCombat.h"

#include <algorithm>
#include <cmath>

namespace valcraft {
namespace {

[[nodiscard]] auto finite_non_negative(
    float value,
    float fallback = 0.0F) noexcept -> float {
    return std::isfinite(value) && value >= 0.0F
               ? value
               : fallback;
}

[[nodiscard]] auto target_weight_rank(
    ColossalTargetWeight weight) noexcept -> std::uint8_t {
    return static_cast<std::uint8_t>(weight);
}

} // namespace

auto resolve_colossal_damage(
    const ColossalDamageRequest& request,
    const ColossalWeaponDefinition& weapon) noexcept
    -> ColossalDamageResult {
    const auto* attack =
        colossal_attack_definition(request.attack);
    if (attack == nullptr) {
        return {};
    }

    const auto profile =
        resolve_colossal_mastery_profile(
            weapon.minimum_level,
            request.strength,
            false,
            weapon);
    auto awakening_multiplier =
        finite_non_negative(request.awakening_multiplier, 1.0F);
    if (request.corrupted_target &&
        request.first_awakening_active) {
        awakening_multiplier *= 1.15F;
    }

    const auto capped_multiplier =
        colossal_final_damage_multiplier(
            request.progression_multiplier,
            profile.strength_damage_multiplier,
            request.target_multiplier,
            awakening_multiplier);
    const auto vulnerability =
        finite_non_negative(
            request.exceptional_vulnerability_multiplier,
            1.0F);
    const auto momentum = static_cast<float>(
        std::min(
            request.momentum,
            weapon.maximum_momentum));
    const auto stagger_multiplier =
        1.0F +
        momentum *
            weapon.momentum_stagger_bonus_per_stack;

    ColossalDamageResult result {};
    result.direct_damage =
        attack->base_damage *
        capped_multiplier *
        vulnerability;
    result.shockwave_damage =
        attack->shockwave_damage *
        capped_multiplier *
        vulnerability;
    result.shockwave_radius_blocks =
        attack->shockwave_radius_blocks;
    result.stagger_power =
        attack->stagger_power *
        stagger_multiplier;
    result.sever_power = attack->sever_power;
    result.knockback_multiplier =
        colossal_knockback_multiplier(
            request.target_weight);
    result.maximum_targets =
        attack->maximum_targets;
    result.destroys_fragile_cells =
        attack->destroys_fragile_cells;
    return result;
}

auto resolve_colossal_guard(
    const ColossalGuardRequest& request,
    const ColossalWeaponDefinition& weapon) noexcept
    -> ColossalGuardResult {
    ColossalGuardResult result {};
    result.resulting_damage =
        finite_non_negative(request.raw_damage);
    result.stability_after =
        finite_non_negative(request.current_stability);

    const auto protected_angle =
        std::isfinite(request.frontal_alignment) &&
        request.frontal_alignment >= 0.50F;
    const auto protected_kind =
        request.attack_kind ==
            ColossalIncomingAttackKind::Melee ||
        request.attack_kind ==
            ColossalIncomingAttackKind::Projectile;
    if (!request.guard_active ||
        request.explicitly_unblockable ||
        !protected_angle ||
        !protected_kind ||
        result.resulting_damage <= 0.0F) {
        return result;
    }

    result.blocked = true;
    const auto coefficient =
        finite_non_negative(
            request.attack_coefficient,
            1.0F);
    const auto base_stability_loss =
        result.resulting_damage *
        coefficient *
        colossal_guard_weight_coefficient(
            request.attacker_weight);
    result.perfect =
        std::isfinite(request.guard_elapsed_seconds) &&
        request.guard_elapsed_seconds >= 0.0F &&
        request.guard_elapsed_seconds <=
            weapon.perfect_guard_window_seconds;

    if (result.perfect) {
        result.resulting_damage = 0.0F;
        result.stability_lost =
            base_stability_loss *
            weapon.perfect_guard_stability_multiplier;
        result.attacker_stagger =
            std::clamp(
                base_stability_loss * 1.50F,
                25.0F,
                40.0F);
    } else {
        const auto reduction =
            request.attack_kind ==
                    ColossalIncomingAttackKind::Projectile
                ? weapon.guard_projectile_reduction
                : weapon.guard_damage_reduction;
        result.resulting_damage *=
            1.0F - std::clamp(reduction, 0.0F, 1.0F);
        result.stability_lost =
            base_stability_loss;
    }

    result.stability_lost =
        std::min(
            result.stability_lost,
            result.stability_after);
    result.stability_after =
        std::max(
            0.0F,
            result.stability_after -
                result.stability_lost);
    result.guard_broken =
        result.stability_after <= 0.0F;
    return result;
}

auto colossal_impact_stop_seconds(
    ColossalTargetWeight weight,
    bool severed_limb,
    const ColossalWeaponDefinition& weapon) noexcept -> float {
    if (severed_limb) {
        return 0.09F;
    }
    switch (target_weight_rank(weight)) {
    case 0U:
        return weapon.normal_impact_stop_seconds;
    case 1U:
        return 0.045F;
    case 2U:
        return 0.065F;
    case 3U:
        return 0.080F;
    default:
        return weapon.normal_impact_stop_seconds;
    }
}

} // namespace valcraft
