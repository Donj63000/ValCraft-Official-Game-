#pragma once

#include "gameplay/weapons/ColossalWeaponDefinition.h"

#include <cstdint>

namespace valcraft {

enum class ColossalTargetWeight : std::uint8_t {
    Light = 0,
    Normal,
    Heavy,
    Boss,
};

enum class ColossalIncomingAttackKind : std::uint8_t {
    Melee = 0,
    Projectile,
    GroundHazard,
    Magic,
};

enum class ColossalImpactMaterial : std::uint8_t {
    Unknown = 0,
    Organic,
    Earth,
    Stone,
    Wood,
    Metal,
    Glass,
    Ship,
    ProtectedStructure,
};

struct ColossalDamageRequest {
    ColossalAttackKind attack = ColossalAttackKind::None;
    ColossalTargetWeight target_weight =
        ColossalTargetWeight::Normal;
    float progression_multiplier = 1.0F;
    std::uint8_t strength = 0U;
    float target_multiplier = 1.0F;
    float awakening_multiplier = 1.0F;
    float exceptional_vulnerability_multiplier = 1.0F;
    std::uint8_t momentum = 0U;
    bool corrupted_target = false;
    bool first_awakening_active = false;
};

struct ColossalDamageResult {
    float direct_damage = 0.0F;
    float shockwave_damage = 0.0F;
    float shockwave_radius_blocks = 0.0F;
    float stagger_power = 0.0F;
    float sever_power = 0.0F;
    float knockback_multiplier = 0.0F;
    std::uint8_t maximum_targets = 0U;
    bool destroys_fragile_cells = false;
};

struct ColossalGuardRequest {
    float raw_damage = 0.0F;
    float attack_coefficient = 1.0F;
    float frontal_alignment = -1.0F;
    float current_stability = 0.0F;
    float guard_elapsed_seconds = 0.0F;
    ColossalTargetWeight attacker_weight =
        ColossalTargetWeight::Normal;
    ColossalIncomingAttackKind attack_kind =
        ColossalIncomingAttackKind::Melee;
    bool guard_active = false;
    bool explicitly_unblockable = false;
};

struct ColossalGuardResult {
    float resulting_damage = 0.0F;
    float stability_lost = 0.0F;
    float stability_after = 0.0F;
    float attacker_stagger = 0.0F;
    bool blocked = false;
    bool perfect = false;
    bool guard_broken = false;
};

struct ColossalAttackResolutionReport {
    std::uint8_t newly_hit_targets = 0U;
    ColossalTargetWeight heaviest_target =
        ColossalTargetWeight::Light;
    ColossalImpactMaterial material =
        ColossalImpactMaterial::Unknown;
    bool wall_hit = false;
    bool protected_surface = false;
    bool severed_limb = false;
};

[[nodiscard]] constexpr auto colossal_knockback_multiplier(
    ColossalTargetWeight weight) noexcept -> float {
    switch (weight) {
    case ColossalTargetWeight::Light:
        return 1.0F;
    case ColossalTargetWeight::Normal:
        return 0.70F;
    case ColossalTargetWeight::Heavy:
        return 0.20F;
    case ColossalTargetWeight::Boss:
        return 0.0F;
    }
    return 0.0F;
}

[[nodiscard]] constexpr auto colossal_guard_weight_coefficient(
    ColossalTargetWeight weight) noexcept -> float {
    switch (weight) {
    case ColossalTargetWeight::Light:
        return 0.75F;
    case ColossalTargetWeight::Normal:
        return 1.0F;
    case ColossalTargetWeight::Heavy:
        return 1.35F;
    case ColossalTargetWeight::Boss:
        return 1.75F;
    }
    return 1.0F;
}

[[nodiscard]] auto resolve_colossal_damage(
    const ColossalDamageRequest& request,
    const ColossalWeaponDefinition& weapon =
        kLeviathanSpineDefinition) noexcept -> ColossalDamageResult;

[[nodiscard]] auto resolve_colossal_guard(
    const ColossalGuardRequest& request,
    const ColossalWeaponDefinition& weapon =
        kLeviathanSpineDefinition) noexcept -> ColossalGuardResult;

[[nodiscard]] auto colossal_impact_stop_seconds(
    ColossalTargetWeight weight,
    bool severed_limb,
    const ColossalWeaponDefinition& weapon =
        kLeviathanSpineDefinition) noexcept -> float;

} // namespace valcraft
