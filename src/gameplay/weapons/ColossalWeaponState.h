#pragma once

#include "gameplay/weapons/ColossalWeaponDefinition.h"

#include <cstdint>

namespace valcraft {

enum class ColossalWeaponState : std::uint8_t {
    Holstered = 0,
    Drawing,
    Idle,
    Windup,
    Active,
    Recovery,
    Guard,
    GuardBroken,
    Charge,
    Impact,
    Sheathing,
};

enum class ColossalWeaponRejection : std::uint8_t {
    None = 0,
    RequirementsNotMet,
    Busy,
    FullyImmersed,
    InsufficientEnergy,
    ChargeOnCooldown,
    InvalidInput,
};

enum class ColossalWeaponEventType : std::uint8_t {
    StateChanged = 0,
    ActionRejected,
    DrawCompleted,
    SheathCompleted,
    AutoSheathed,
    AttackStarted,
    AttackBecameActive,
    AttackFinished,
    AttackMissed,
    AttackImpact,
    MomentumChanged,
    StabilityChanged,
    GuardStarted,
    GuardEnded,
    PerfectGuard,
    GuardBroken,
    ChargeResourceCommit,
    ChargeRejected,
    RunningAdvanceRequested,
};

struct ColossalWeaponInput {
    bool toggle_draw_pressed = false;
    bool primary_pressed = false;
    bool primary_held = false;
    bool primary_released = false;
    bool guard_pressed = false;
    bool guard_held = false;
    bool guard_released = false;
    bool cancel_pressed = false;
};

struct ColossalWeaponUpdateContext {
    std::uint16_t player_level = 1U;
    std::uint8_t strength = 0U;
    float progression_damage_multiplier = 1.0F;
    float available_val_energy = 0.0F;
    bool scenario_override = false;
    bool fully_immersed = false;
    bool sprinting = false;
    bool narrow_tunnel = false;
};

struct ColossalWeaponEvent {
    ColossalWeaponEventType type =
        ColossalWeaponEventType::StateChanged;
    ColossalWeaponState state = ColossalWeaponState::Holstered;
    ColossalWeaponState previous_state =
        ColossalWeaponState::Holstered;
    ColossalAttackKind attack = ColossalAttackKind::None;
    ColossalAttackShape attack_shape =
        ColossalAttackShape::HorizontalArc;
    ColossalWeaponRejection rejection =
        ColossalWeaponRejection::None;
    std::uint64_t attack_sequence = 0U;
    float primary_value = 0.0F;
    float secondary_value = 0.0F;
    std::uint8_t count = 0U;
    std::uint8_t detail_code = 0U;
    bool protected_surface = false;
};

struct ColossalWeaponStateSnapshot {
    ColossalWeaponState state = ColossalWeaponState::Holstered;
    ColossalAttackKind attack = ColossalAttackKind::None;
    ColossalAttackShape attack_shape =
        ColossalAttackShape::HorizontalArc;
    ColossalWeaponRejection last_rejection =
        ColossalWeaponRejection::None;
    std::uint64_t attack_sequence = 0U;
    float state_elapsed_seconds = 0.0F;
    float state_duration_seconds = 0.0F;
    float state_progress = 0.0F;
    float charge_seconds = 0.0F;
    float charge_progress = 0.0F;
    float charge_cooldown_seconds = 0.0F;
    float stability = 100.0F;
    float maximum_stability = 100.0F;
    float stability_regeneration_delay_seconds = 0.0F;
    float movement_multiplier = 1.0F;
    float damage_multiplier = 1.0F;
    float stagger_multiplier = 1.0F;
    std::uint8_t momentum = 0U;
    std::uint8_t combo_step = 0U;
    bool attack_buffered = false;
    bool attack_has_hit = false;
    bool charge_committed = false;
    bool contextual_vertical = false;
    bool can_sprint = true;
    bool can_change_equipment = true;
    bool event_overflowed = false;
};

} // namespace valcraft
