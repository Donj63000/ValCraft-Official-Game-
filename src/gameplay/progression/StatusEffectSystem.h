#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace valcraft {

using StatusEffectTargetId = std::uint64_t;
using StatusEffectStackTag = std::uint64_t;

inline constexpr float kStatusEffectFixedStepSeconds = 1.0F / 60.0F;
inline constexpr float kStatusEffectMaximumDurationSeconds = 86'400.0F;
inline constexpr std::uint64_t kStatusEffectMaximumDurationTicks =
    5'184'000U;
inline constexpr std::size_t kMaximumStatusEffects = 256U;
inline constexpr std::size_t kMaximumStatusEffectsPerTarget = 32U;

inline constexpr float kMaximumDamageReduction = 0.80F;
inline constexpr float kMaximumKnockbackResistance = 1.0F;
inline constexpr float kMaximumTemporaryMovementSpeedBonus = 0.50F;
inline constexpr float kMaximumTemporaryRecoverySpeedBonus = 0.40F;
inline constexpr float kMaximumSlow = 0.60F;
inline constexpr float kMaximumShieldHealthRatio = 1.0F;

enum class StatusEffectKind : std::uint8_t {
    DamageReduction = 0,
    KnockbackResistance = 1,
    FrontalProjectileReduction = 2,
    MovementSpeedBonus = 3,
    RecoverySpeedBonus = 4,
    Slow = 5,
    Shield = 6,
    FirstAbsorption = 7,
};

enum class StatusEffectApplyError : std::uint8_t {
    None = 0,
    InvalidTarget = 1,
    InvalidStackTag = 2,
    InvalidKind = 3,
    InvalidValue = 4,
    InvalidDuration = 5,
    TargetCapacityReached = 6,
    GlobalCapacityReached = 7,
};

struct StatusEffectSpec {
    StatusEffectTargetId target_id = 0U;
    StatusEffectStackTag stack_tag = 0U;
    StatusEffectKind kind = StatusEffectKind::DamageReduction;
    float value = 0.0F;
    float duration_seconds = 0.0F;
};

struct StatusEffectApplyResult {
    bool applied = false;
    bool inserted = false;
    bool refreshed = false;
    StatusEffectApplyError error = StatusEffectApplyError::None;
    float effective_value = 0.0F;
    double remaining_seconds = 0.0;
};

struct StatusEffectUpdateResult {
    bool accepted = false;
    std::uint64_t advanced_ticks = 0U;
    std::size_t expired_effect_count = 0U;
};

struct StatusEffectAggregate {
    float damage_reduction = 0.0F;
    float knockback_resistance = 0.0F;
    float frontal_projectile_reduction = 0.0F;
    float movement_speed_bonus = 0.0F;
    float recovery_speed_bonus = 0.0F;
    float slow = 0.0F;
    float shield_health = 0.0F;
    bool first_absorption_available = false;

    [[nodiscard]] auto damage_multiplier(
        bool frontal_projectile) const noexcept -> float;
    [[nodiscard]] auto knockback_multiplier() const noexcept -> float;
    [[nodiscard]] auto movement_speed_multiplier() const noexcept -> float;
    [[nodiscard]] auto recovery_speed_multiplier() const noexcept -> float;
};

struct IronGuardStatusRequest {
    StatusEffectTargetId target_id = 0U;
    StatusEffectStackTag stack_tag = 0U;
    float duration_seconds = 0.0F;
    float damage_reduction = 0.0F;
    float knockback_resistance = 0.0F;
    float frontal_projectile_reduction = 0.0F;
    bool grants_first_absorption = false;
};

struct IronGuardStatusResult {
    bool applied = false;
    StatusEffectApplyError error = StatusEffectApplyError::None;
    std::size_t inserted_effect_count = 0U;
    std::size_t refreshed_effect_count = 0U;
};

struct FirstAbsorptionConsumeResult {
    bool consumed = false;
    StatusEffectStackTag stack_tag = 0U;
};

struct FirstHitAbsorptionResult {
    bool accepted = false;
    bool absorbed = false;
    StatusEffectStackTag stack_tag = 0U;
    float requested_damage = 0.0F;
    float absorbed_damage = 0.0F;
    float remaining_damage = 0.0F;
};

struct ShieldAbsorptionResult {
    bool accepted = false;
    float requested_damage = 0.0F;
    float absorbed_damage = 0.0F;
    float remaining_shield = 0.0F;
};

struct StatusEffectSnapshotEntry {
    StatusEffectTargetId target_id = 0U;
    StatusEffectStackTag stack_tag = 0U;
    StatusEffectKind kind = StatusEffectKind::DamageReduction;
    float value = 0.0F;
    std::uint64_t remaining_ticks = 0U;
    std::uint64_t sequence = 0U;
    bool active = false;

    auto operator==(const StatusEffectSnapshotEntry&) const -> bool = default;
};

struct StatusEffectSystemSnapshot {
    std::array<StatusEffectSnapshotEntry, kMaximumStatusEffects> entries {};
    // Je conserve la fraction de tick, et non des secondes, pour reprendre
    // exactement la simulation déterministe après un chargement.
    double fractional_tick_accumulator = 0.0;
    std::uint64_t next_sequence = 1U;

    auto operator==(const StatusEffectSystemSnapshot&) const -> bool = default;
};

struct StatusEffectLoadResult {
    std::size_t restored_effect_count = 0U;
    std::size_t discarded_effect_count = 0U;
    bool sanitized = false;
};

class StatusEffectSystem {
public:
    [[nodiscard]] auto apply(
        const StatusEffectSpec& spec) noexcept -> StatusEffectApplyResult;
    [[nodiscard]] auto apply_iron_guard(
        const IronGuardStatusRequest& request) noexcept
        -> IronGuardStatusResult;

    [[nodiscard]] auto update(float dt) noexcept
        -> StatusEffectUpdateResult;

    [[nodiscard]] auto aggregate(
        StatusEffectTargetId target_id,
        float maximum_health) const noexcept -> StatusEffectAggregate;
    [[nodiscard]] auto consume_first_absorption(
        StatusEffectTargetId target_id,
        std::optional<StatusEffectStackTag> stack_tag = std::nullopt) noexcept
        -> FirstAbsorptionConsumeResult;
    [[nodiscard]] auto absorb_first_hit(
        StatusEffectTargetId target_id,
        float incoming_damage,
        std::optional<StatusEffectStackTag> stack_tag = std::nullopt) noexcept
        -> FirstHitAbsorptionResult;
    [[nodiscard]] auto absorb_with_shield(
        StatusEffectTargetId target_id,
        float incoming_damage,
        float maximum_health) noexcept -> ShieldAbsorptionResult;

    [[nodiscard]] auto has_effect(
        StatusEffectTargetId target_id,
        StatusEffectKind kind,
        StatusEffectStackTag stack_tag) const noexcept -> bool;
    [[nodiscard]] auto remaining_seconds(
        StatusEffectTargetId target_id,
        StatusEffectKind kind,
        StatusEffectStackTag stack_tag) const noexcept
        -> std::optional<double>;
    [[nodiscard]] auto active_effect_count() const noexcept -> std::size_t;
    [[nodiscard]] auto active_effect_count(
        StatusEffectTargetId target_id) const noexcept -> std::size_t;

    [[nodiscard]] auto snapshot() const noexcept
        -> StatusEffectSystemSnapshot;
    [[nodiscard]] auto load_state(
        const StatusEffectSystemSnapshot& snapshot) noexcept
        -> StatusEffectLoadResult;

    void clear_target(StatusEffectTargetId target_id) noexcept;
    void clear_stack(
        StatusEffectTargetId target_id,
        StatusEffectStackTag stack_tag) noexcept;
    [[nodiscard]] auto clear_kind(
        StatusEffectTargetId target_id,
        StatusEffectKind kind) noexcept -> std::size_t;
    void clear() noexcept;

private:
    struct Entry {
        StatusEffectTargetId target_id = 0U;
        StatusEffectStackTag stack_tag = 0U;
        StatusEffectKind kind = StatusEffectKind::DamageReduction;
        float value = 0.0F;
        std::uint64_t remaining_ticks = 0U;
        std::uint64_t sequence = 0U;
        bool active = false;
    };

    [[nodiscard]] auto find_entry(
        StatusEffectTargetId target_id,
        StatusEffectKind kind,
        StatusEffectStackTag stack_tag) noexcept -> Entry*;
    [[nodiscard]] auto find_entry(
        StatusEffectTargetId target_id,
        StatusEffectKind kind,
        StatusEffectStackTag stack_tag) const noexcept -> const Entry*;
    [[nodiscard]] auto find_free_entry() noexcept -> Entry*;
    [[nodiscard]] auto validate(
        const StatusEffectSpec& spec) const noexcept
        -> StatusEffectApplyError;
    [[nodiscard]] static auto duration_ticks(float seconds) noexcept
        -> std::uint64_t;

    // Je garde tous les effets dans une capacité fixe : aucun pointeur de
    // gameplay ni aucune donnée transitoire ne peut se retrouver sauvegardé.
    std::array<Entry, kMaximumStatusEffects> entries_ {};
    double tick_accumulator_ = 0.0;
    std::uint64_t next_sequence_ = 1U;
};

} // namespace valcraft
