#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace valcraft {

enum class ColossalAttackKind : std::uint8_t {
    None = 0,
    FirstSweep,
    SecondSweep,
    Earthbreaker,
    RunningCleave,
    ChargedExecution,
};

enum class ColossalAttackShape : std::uint8_t {
    HorizontalArc = 0,
    ReverseHorizontalArc,
    VerticalArc,
    DiagonalArc,
};

struct ColossalAttackDefinition {
    ColossalAttackKind kind = ColossalAttackKind::None;
    ColossalAttackShape shape = ColossalAttackShape::HorizontalArc;
    float base_damage = 0.0F;
    float shockwave_damage = 0.0F;
    float shockwave_radius_blocks = 0.0F;
    float range_blocks = 0.0F;
    float arc_degrees = 0.0F;
    float windup_seconds = 0.0F;
    float active_seconds = 0.0F;
    float recovery_seconds = 0.0F;
    float stagger_power = 0.0F;
    float sever_power = 0.0F;
    float forward_advance_blocks = 0.0F;
    std::uint8_t maximum_targets = 0U;
    bool destroys_fragile_cells = false;
};

struct ColossalWeaponDefinition {
    std::uint16_t minimum_level = 35U;
    std::uint8_t minimum_strength = 4U;
    std::uint8_t recommended_strength = 6U;
    std::uint8_t demonstration_strength = 8U;

    float draw_seconds = 0.75F;
    float sheath_seconds = 0.90F;
    float drawn_movement_multiplier = 0.88F;
    float guard_movement_multiplier = 0.35F;
    float charge_movement_multiplier = 0.25F;

    float maximum_stability = 100.0F;
    float stability_regeneration_per_second = 28.0F;
    float stability_regeneration_delay_seconds = 1.0F;
    float guard_damage_reduction = 0.70F;
    float guard_projectile_reduction = 0.80F;
    float perfect_guard_window_seconds = 0.16F;
    float perfect_guard_stability_multiplier = 0.10F;
    float guard_break_seconds = 1.20F;

    std::uint8_t maximum_momentum = 3U;
    float momentum_decay_delay_seconds = 2.0F;
    float momentum_decay_interval_seconds = 1.0F;
    float momentum_windup_reduction_per_stack = 0.07F;
    float momentum_stagger_bonus_per_stack = 0.10F;

    float charge_commit_seconds = 1.20F;
    float charge_maximum_seconds = 2.0F;
    float charge_energy_cost = 35.0F;
    float charge_cooldown_seconds = 10.0F;

    float wall_recovery_penalty_seconds = 0.20F;
    float wall_impact_stop_seconds = 0.055F;
    float normal_impact_stop_seconds = 0.040F;
    std::uint8_t maximum_fragile_cells = 12U;
};

struct ColossalMasteryProfile {
    bool requirements_met = false;
    std::uint16_t effective_level = 1U;
    std::uint8_t effective_strength = 0U;
    float strength_damage_multiplier = 1.0F;
    float windup_multiplier = 1.0F;
    float recovery_multiplier = 1.0F;
    float movement_multiplier = 1.0F;
    float stability_multiplier = 1.0F;
};

inline constexpr ColossalWeaponDefinition kLeviathanSpineDefinition {};

inline constexpr std::array<ColossalAttackDefinition, 5U>
    kColossalAttackDefinitions {{
        {
            ColossalAttackKind::FirstSweep,
            ColossalAttackShape::HorizontalArc,
            14.0F,
            0.0F,
            0.0F,
            3.25F,
            150.0F,
            0.36F,
            0.22F,
            0.48F,
            30.0F,
            15.0F,
            0.0F,
            6U,
            false,
        },
        {
            ColossalAttackKind::SecondSweep,
            ColossalAttackShape::ReverseHorizontalArc,
            16.0F,
            0.0F,
            0.0F,
            3.25F,
            135.0F,
            0.24F,
            0.22F,
            0.42F,
            35.0F,
            20.0F,
            0.0F,
            6U,
            false,
        },
        {
            ColossalAttackKind::Earthbreaker,
            ColossalAttackShape::VerticalArc,
            22.0F,
            6.0F,
            2.50F,
            3.25F,
            45.0F,
            0.48F,
            0.18F,
            0.90F,
            60.0F,
            40.0F,
            0.0F,
            6U,
            false,
        },
        {
            ColossalAttackKind::RunningCleave,
            ColossalAttackShape::DiagonalArc,
            12.0F,
            0.0F,
            0.0F,
            3.25F,
            110.0F,
            0.28F,
            0.20F,
            0.52F,
            40.0F,
            20.0F,
            1.25F,
            6U,
            false,
        },
        {
            ColossalAttackKind::ChargedExecution,
            ColossalAttackShape::VerticalArc,
            32.0F,
            10.0F,
            4.0F,
            3.50F,
            55.0F,
            1.20F,
            0.22F,
            1.05F,
            100.0F,
            100.0F,
            0.0F,
            6U,
            true,
        },
    }};

[[nodiscard]] constexpr auto colossal_attack_definition(
    ColossalAttackKind kind) noexcept
    -> const ColossalAttackDefinition* {
    for (const auto& definition : kColossalAttackDefinitions) {
        if (definition.kind == kind) {
            return &definition;
        }
    }
    return nullptr;
}

[[nodiscard]] auto resolve_colossal_mastery_profile(
    std::uint16_t player_level,
    std::uint8_t strength,
    bool scenario_override,
    const ColossalWeaponDefinition& definition =
        kLeviathanSpineDefinition) noexcept -> ColossalMasteryProfile;

[[nodiscard]] auto colossal_final_damage_multiplier(
    float progression_multiplier,
    float strength_multiplier,
    float target_multiplier,
    float awakening_multiplier) noexcept -> float;

} // namespace valcraft
