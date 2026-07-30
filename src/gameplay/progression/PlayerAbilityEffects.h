#pragma once

#include "gameplay/progression/AbilitySystem.h"
#include "gameplay/progression/StatusEffectSystem.h"

#include <cstdint>

namespace valcraft {

inline constexpr StatusEffectTargetId
    kPlayerStatusEffectTargetId = 1U;
inline constexpr StatusEffectStackTag
    kIronGuardStatusStackTag = 0x49524F4E47554152ULL;

struct IronGuardActivationResult {
    bool applied = false;
    StatusEffectApplyError error =
        StatusEffectApplyError::None;
};

struct IronGuardReactiveResult {
    bool triggered = false;
    AbilityCastSequence cast_sequence = 0U;
    float wave_damage = 0.0F;
    float wave_radius = 0.0F;
    float energy_refund = 0.0F;
};

struct IronGuardDamageInterceptionResult {
    bool accepted = false;
    bool absorbed = false;
    float requested_damage = 0.0F;
    float absorbed_damage = 0.0F;
    float remaining_damage = 0.0F;
    IronGuardReactiveResult reactive {};
};

struct PlayerAbilityEffectsUpdateResult {
    bool iron_guard_expired = false;
    AbilityCastSequence iron_guard_cast_sequence = 0U;
    std::size_t expired_effect_count = 0U;
};

struct PlayerAbilityEffectsSnapshot {
    StatusEffectSystemSnapshot status_effects {};
    AbilityCastSequence iron_guard_cast_sequence = 0U;
    float iron_guard_wave_damage = 0.0F;
    float iron_guard_wave_radius = 0.0F;
    float iron_guard_energy_refund = 0.0F;

    auto operator==(const PlayerAbilityEffectsSnapshot&) const
        -> bool = default;
};

struct PlayerAbilityEffectsLoadResult {
    StatusEffectLoadResult status_effects {};
    bool iron_guard_restored = false;
    bool sanitized = false;
};

class PlayerAbilityEffects {
public:
    [[nodiscard]] auto activate_iron_guard(
        const AbilityCastResolution& resolution,
        bool shield_equipped) noexcept
        -> IronGuardActivationResult;

    [[nodiscard]] auto update(float dt) noexcept
        -> PlayerAbilityEffectsUpdateResult;

    [[nodiscard]] auto aggregate(
        float maximum_health) const noexcept
        -> StatusEffectAggregate;

    [[nodiscard]] auto consume_iron_guard_absorption() noexcept
        -> IronGuardReactiveResult;
    // J'appelle cette interception avant de muter les PV : le premier coup
    // maîtrisé ressort avec remaining_damage à zéro et déclenche sa réaction.
    [[nodiscard]] auto intercept_iron_guard_damage(
        float incoming_damage) noexcept
        -> IronGuardDamageInterceptionResult;

    [[nodiscard]] auto iron_guard_active() const noexcept -> bool;
    [[nodiscard]] auto iron_guard_cast_sequence() const noexcept
        -> AbilityCastSequence;
    [[nodiscard]] auto clear_slow_effects() noexcept -> std::size_t;

    [[nodiscard]] auto snapshot() const noexcept
        -> PlayerAbilityEffectsSnapshot;
    [[nodiscard]] auto load_state(
        const PlayerAbilityEffectsSnapshot& snapshot) noexcept
        -> PlayerAbilityEffectsLoadResult;

    void clear() noexcept;

private:
    StatusEffectSystem status_effects_ {};
    AbilityCastSequence iron_guard_cast_sequence_ = 0U;
    float iron_guard_wave_damage_ = 0.0F;
    float iron_guard_wave_radius_ = 0.0F;
    float iron_guard_energy_refund_ = 0.0F;
};

} // namespace valcraft
