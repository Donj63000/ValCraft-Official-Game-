#include "gameplay/weapons/ColossalWeaponDefinition.h"

#include <algorithm>
#include <cmath>

namespace valcraft {
namespace {

[[nodiscard]] auto finite_non_negative(
    float value,
    float fallback) noexcept -> float {
    return std::isfinite(value) && value >= 0.0F
               ? value
               : fallback;
}

} // namespace

auto resolve_colossal_mastery_profile(
    std::uint16_t player_level,
    std::uint8_t strength,
    bool scenario_override,
    const ColossalWeaponDefinition& definition) noexcept
    -> ColossalMasteryProfile {
    ColossalMasteryProfile profile {};
    profile.effective_level =
        scenario_override
            ? std::max(player_level, definition.minimum_level)
            : player_level;
    profile.effective_strength =
        scenario_override
            ? std::max(strength, definition.demonstration_strength)
            : strength;
    profile.requirements_met =
        scenario_override ||
        (profile.effective_level >= definition.minimum_level &&
         profile.effective_strength >= definition.minimum_strength);

    const auto strength_points =
        static_cast<float>(profile.effective_strength);
    profile.strength_damage_multiplier =
        1.0F + std::min(strength_points * 0.03F, 0.30F);
    profile.recovery_multiplier =
        1.0F - std::min(strength_points * 0.02F, 0.20F);
    profile.movement_multiplier =
        definition.drawn_movement_multiplier;

    if (profile.effective_strength <
        definition.recommended_strength) {
        profile.windup_multiplier = 1.12F;
        profile.movement_multiplier = 0.82F;
        profile.stability_multiplier = 0.90F;
    } else if (profile.effective_strength >= 8U) {
        profile.windup_multiplier = 0.94F;
        profile.movement_multiplier = 0.90F;
        profile.stability_multiplier = 1.10F;
    }

    return profile;
}

auto colossal_final_damage_multiplier(
    float progression_multiplier,
    float strength_multiplier,
    float target_multiplier,
    float awakening_multiplier) noexcept -> float {
    const auto progression =
        finite_non_negative(progression_multiplier, 1.0F);
    const auto strength =
        finite_non_negative(strength_multiplier, 1.0F);
    const auto target =
        finite_non_negative(target_multiplier, 1.0F);
    const auto awakening =
        finite_non_negative(awakening_multiplier, 1.0F);
    return std::clamp(
        progression * strength * target * awakening,
        0.0F,
        2.40F);
}

} // namespace valcraft
