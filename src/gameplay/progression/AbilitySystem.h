#pragma once

#include "gameplay/progression/AbilityEvents.h"
#include "gameplay/progression/PlayerBuildState.h"

#include <array>
#include <cstdint>

namespace valcraft {

inline constexpr float kAbilityFixedStepSeconds = 1.0F / 60.0F;
inline constexpr float kPlayerBaseMaximumValEnergy = 100.0F;
inline constexpr float kPlayerBaseValEnergyRegenerationPerSecond = 8.0F;
inline constexpr float kPlayerValEnergyRegenerationDelaySeconds = 1.5F;
inline constexpr float kAbilityGlobalCooldownSeconds = 0.25F;

struct AbilityEnergyParameters {
    float maximum_energy = kPlayerBaseMaximumValEnergy;
    float regeneration_per_second =
        kPlayerBaseValEnergyRegenerationPerSecond;

    auto operator==(const AbilityEnergyParameters&) const -> bool = default;
};

[[nodiscard]] auto player_ability_energy_parameters(
    const PlayerBuildState& state,
    std::uint8_t wisdom_equipment_bonus = 0U) noexcept
    -> AbilityEnergyParameters;

enum class AbilityEffectFlag : std::uint32_t {
    None = 0U,
    VanguardSecondaryImpact = 1U << 0U,
    VanguardBlockSynergy = 1U << 1U,
    WindBlade = 1U << 2U,
    WindMasteryCleanseSlow = 1U << 3U,
    WindMasteryDodge = 1U << 4U,
    FootmanLightTaunt = 1U << 5U,
    FootmanProjectileBlock = 1U << 6U,
    FootmanMasterySurvival = 1U << 7U,
    FootmanMasteryDamageReduction = 1U << 8U,
    ConstructionMirror = 1U << 9U,
};

[[nodiscard]] constexpr auto operator|(
    AbilityEffectFlag lhs,
    AbilityEffectFlag rhs) noexcept -> AbilityEffectFlag {
    return static_cast<AbilityEffectFlag>(
        static_cast<std::uint32_t>(lhs) |
        static_cast<std::uint32_t>(rhs));
}

constexpr auto operator|=(
    AbilityEffectFlag& lhs,
    AbilityEffectFlag rhs) noexcept -> AbilityEffectFlag& {
    lhs = lhs | rhs;
    return lhs;
}

[[nodiscard]] constexpr auto ability_effects_contain(
    AbilityEffectFlag effects,
    AbilityEffectFlag expected) noexcept -> bool {
    const auto expected_bits =
        static_cast<std::uint32_t>(expected);
    return (
               static_cast<std::uint32_t>(effects) &
               expected_bits) ==
           expected_bits;
}

struct AbilityCastRequest {
    AbilityId id = AbilityId::None;
    float target_distance_meters = 0.0F;
    // Je fournis la portée réelle de l'arme pour les compétences qui
    // héritent de l'équipement au lieu d'une distance fixe du catalogue.
    float effective_range_meters = 0.0F;
    bool target_valid = false;
    bool ground_target_valid = false;
    bool on_moving_ship = false;
    std::uint8_t construction_cell_count = 0U;
    float seconds_since_successful_shield_block = -1.0F;
    AbilityEventPayload event_payload {};
};

struct AbilityCastResolution {
    AbilityId id = AbilityId::None;
    AbilityCastSequence cast_sequence = 0U;
    std::uint8_t rank = 0U;
    float energy_cost = 0.0F;
    float cooldown_seconds = 0.0F;
    float range_meters = 0.0F;
    float duration_seconds = 0.0F;
    std::array<float, kAbilityValueCount> values {};
    AbilityEffectFlag effects = AbilityEffectFlag::None;
    ConstructionPlan construction_plan {};
    std::uint8_t maximum_construction_cells = 0U;
    bool mastery_active = false;
};

enum class AbilityCastFailure : std::uint8_t {
    None = 0,
    InvalidAbility = 1,
    UnimplementedAbility = 2,
    PassiveAbility = 3,
    AbilityNotLearned = 4,
    AbilityNotEquipped = 5,
    GlobalCooldown = 6,
    Cooldown = 7,
    NoCharges = 8,
    InsufficientEnergy = 9,
    InvalidTarget = 10,
    TargetOutOfRange = 11,
    MovingShipConstruction = 12,
    InvalidConstructionPlan = 13,
    MissingCommitter = 14,
    ExternalValidationRejected = 15,
    ExternalCommitRejected = 16,
};

struct AbilityCastResult {
    AbilityCastFailure failure = AbilityCastFailure::None;
    AbilityCastResolution resolution {};

    [[nodiscard]] constexpr auto succeeded() const noexcept -> bool {
        return failure == AbilityCastFailure::None;
    }
};

using AbilityCastValidator = bool (*)(
    void* user_data,
    const AbilityCastRequest& request,
    const AbilityCastResolution& resolution) noexcept;

using AbilityCastCommitter = bool (*)(
    void* user_data,
    const AbilityCastRequest& request,
    const AbilityCastResolution& resolution) noexcept;

struct AbilityCastCallbacks {
    void* user_data = nullptr;
    AbilityCastValidator validate = nullptr;
    AbilityCastCommitter commit = nullptr;
};

[[nodiscard]] auto prepare_player_ability_cast(
    const PlayerBuildState& state,
    const AbilityCastRequest& request) noexcept -> AbilityCastResult;

class AbilitySystem {
public:
    void update(
        PlayerBuildState& state,
        float elapsed_seconds) noexcept;

    void update(
        PlayerBuildState& state,
        float elapsed_seconds,
        const AbilityEnergyParameters& energy_parameters) noexcept;

    void simulate_fixed_step(
        PlayerBuildState& state) noexcept;

    void simulate_fixed_step(
        PlayerBuildState& state,
        const AbilityEnergyParameters& energy_parameters) noexcept;

    [[nodiscard]] auto try_cast(
        PlayerBuildState& state,
        const AbilityCastRequest& request,
        const AbilityCastCallbacks& callbacks) noexcept
        -> AbilityCastResult;

    void reset_timing() noexcept;

    [[nodiscard]] auto pending_time_seconds() const noexcept -> double;
    [[nodiscard]] auto logical_events() const noexcept
        -> std::span<const AbilityLogicalEvent>;
    [[nodiscard]] auto publish_logical_event(
        AbilityEventType type,
        AbilityCastSequence cast_sequence,
        const AbilityEventPayload& payload = {}) noexcept
        -> AbilityEventPublishResult;
    [[nodiscard]] auto drain_logical_events(
        std::span<AbilityLogicalEvent> destination) noexcept
        -> std::size_t;
    [[nodiscard]] auto next_cast_sequence() const noexcept
        -> AbilityCastSequence;
    void reserve_next_cast_sequence(
        AbilityCastSequence minimum_next_sequence) noexcept;

private:
    double fixed_step_accumulator_seconds_ = 0.0;
    AbilityEventBuffer logical_events_ {};
};

} // namespace valcraft
