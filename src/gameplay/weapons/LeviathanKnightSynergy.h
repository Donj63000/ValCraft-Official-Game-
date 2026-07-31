#pragma once

#include "gameplay/progression/PlayerBuildState.h"
#include "gameplay/weapons/ColossalWeaponDefinition.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace valcraft {

inline constexpr std::uint8_t kLeviathanSynergyMaximumTargets = 12U;
inline constexpr std::size_t kLeviathanSynergyEventCapacity = 32U;
inline constexpr float kLeviathanSynergyMaximumDurationSeconds = 30.0F;
inline constexpr float kLeviathanSynergyMaximumAdvanceSeconds = 3'600.0F;

enum class LeviathanKnightSynergyKind : std::uint8_t {
    None = 0,
    RuneStrike,
    IronGuard,
    BulwarkCharge,
    PerfectRiposte,
    SteelTempest,
    LivingFortress,
    TitanJudgment,
    Count,
};

enum class LeviathanSynergyStatus : std::uint8_t {
    NoEffect = 0,
    Activated,
    Replaced,
    Applied,
    Replayed,
    Deactivated,
    Expired,
    AlreadyConsumed,
    RejectedUnsupportedAbility,
    RejectedNotLearned,
    RejectedNotEquipped,
    RejectedUnconfirmedCast,
    RejectedInvalidSequence,
    RejectedStaleSequence,
    RejectedInvalidInput,
    RejectedMismatchedActivation,
    RejectedNotActive,
};

enum class LeviathanSynergyEventType : std::uint8_t {
    Activated = 0,
    Replaced,
    Deactivated,
    Expired,
    Consumed,
    AttackModified,
    GuardModified,
};

enum class LeviathanImpactVariant : std::uint8_t {
    Standard = 0,
    TitanBlade,
};

struct LeviathanSynergyEvent {
    std::uint64_t event_id = 0U;
    LeviathanSynergyEventType type =
        LeviathanSynergyEventType::Activated;
    LeviathanKnightSynergyKind synergy =
        LeviathanKnightSynergyKind::None;
    AbilityId ability = AbilityId::None;
    std::uint64_t activation_sequence = 0U;
    std::uint64_t action_sequence = 0U;
    ColossalAttackKind attack = ColossalAttackKind::None;
    float primary_value = 0.0F;
    float secondary_value = 0.0F;
    std::uint8_t rank = 0U;
    std::uint8_t count = 0U;

    auto operator==(const LeviathanSynergyEvent&) const -> bool = default;
};

struct LeviathanSynergyActivationRequest {
    AbilityId ability = AbilityId::None;
    std::uint64_t cast_sequence = 0U;
    float duration_seconds = 0.0F;
    bool cast_succeeded = false;
    bool effect_active = false;
};

struct LeviathanSynergyActivationResult {
    LeviathanSynergyStatus status =
        LeviathanSynergyStatus::RejectedInvalidInput;
    LeviathanKnightSynergyKind synergy =
        LeviathanKnightSynergyKind::None;
    AbilityId ability = AbilityId::None;
    std::uint64_t cast_sequence = 0U;
    float effective_duration_seconds = 0.0F;
    std::uint8_t rank = 0U;

    [[nodiscard]] constexpr auto accepted() const noexcept -> bool {
        return status == LeviathanSynergyStatus::Activated ||
               status == LeviathanSynergyStatus::Replaced ||
               status == LeviathanSynergyStatus::Replayed;
    }
};

struct LeviathanSynergyDeactivationRequest {
    AbilityId ability = AbilityId::None;
    std::uint64_t cast_sequence = 0U;
};

struct LeviathanAttackSynergyRequest {
    std::uint64_t attack_sequence = 0U;
    ColossalAttackKind attack = ColossalAttackKind::None;
    std::uint8_t base_maximum_targets = 0U;
};

struct LeviathanAttackSynergyResult {
    LeviathanSynergyStatus status = LeviathanSynergyStatus::NoEffect;
    std::uint64_t attack_sequence = 0U;
    ColossalAttackKind attack = ColossalAttackKind::None;
    float additional_shockwave_damage = 0.0F;
    float additional_shockwave_radius_blocks = 0.0F;
    std::uint8_t maximum_targets = 0U;
    bool rune_wave_applied = false;
    bool steel_tempest_applied = false;
};

struct LeviathanGuardSynergyRequest {
    float base_stability_loss = 0.0F;
    std::uint8_t allies_behind = 0U;
    bool guard_active = false;
    bool frontal_attack_blocked = false;
    bool projectile = false;
};

struct LeviathanGuardSynergyResult {
    LeviathanSynergyStatus status = LeviathanSynergyStatus::NoEffect;
    float stability_loss = 0.0F;
    float stability_loss_multiplier = 1.0F;
    float ally_damage_reduction = 0.0F;
    float ally_guard_range_blocks = 0.0F;
    float ally_guard_half_angle_degrees = 0.0F;
    std::uint8_t maximum_protected_allies = 0U;
    std::uint8_t protected_ally_count = 0U;
    bool iron_guard_applied = false;
    bool ally_protection_enabled = false;
};

struct LeviathanBulwarkSweepRequest {
    std::uint64_t cast_sequence = 0U;
    bool charge_completed = false;
};

struct LeviathanBulwarkSweepResult {
    LeviathanSynergyStatus status = LeviathanSynergyStatus::NoEffect;
    ColossalAttackKind forced_attack = ColossalAttackKind::None;
    ColossalAttackShape forced_shape =
        ColossalAttackShape::HorizontalArc;
    float range_multiplier = 1.0F;
    float arc_multiplier = 1.0F;
    float stagger_multiplier = 1.0F;
    std::uint8_t maximum_targets = 0U;
    bool massive_sweep_requested = false;
};

struct LeviathanPerfectRiposteRequest {
    std::uint64_t cast_sequence = 0U;
    bool counter_triggered = false;
    bool perfect_guard_confirmed = false;
};

struct LeviathanPerfectRiposteResult {
    LeviathanSynergyStatus status = LeviathanSynergyStatus::NoEffect;
    ColossalAttackKind forced_attack = ColossalAttackKind::None;
    ColossalAttackShape forced_shape =
        ColossalAttackShape::ReverseHorizontalArc;
    bool second_combo_attack_requested = false;
};

struct LeviathanTitanImpactRequest {
    std::uint64_t cast_sequence = 0U;
    bool impact_confirmed = false;
};

struct LeviathanTitanImpactResult {
    LeviathanSynergyStatus status = LeviathanSynergyStatus::NoEffect;
    LeviathanImpactVariant variant = LeviathanImpactVariant::Standard;
    float damage_multiplier = 1.0F;
    float stagger_multiplier = 1.0F;
    bool colossal_blade_kinematics_requested = false;
};

struct LeviathanKnightSynergyView {
    std::array<
        bool,
        static_cast<std::size_t>(
            LeviathanKnightSynergyKind::Count)> active {};
    std::array<
        std::uint64_t,
        static_cast<std::size_t>(
            LeviathanKnightSynergyKind::Count)> activation_sequences {};
    std::array<
        float,
        static_cast<std::size_t>(
            LeviathanKnightSynergyKind::Count)> remaining_seconds {};
    std::array<
        std::uint8_t,
        static_cast<std::size_t>(
            LeviathanKnightSynergyKind::Count)> ranks {};
    bool event_overflowed = false;
};

[[nodiscard]] constexpr auto leviathan_synergy_kind_for_ability(
    AbilityId ability) noexcept -> LeviathanKnightSynergyKind {
    switch (ability) {
    case AbilityId::KnightVanguardStrike:
        // Je raccorde ici la « Frappe runique » du plan à la frappe
        // d'avant-garde déjà déclarée dans le catalogue.
        return LeviathanKnightSynergyKind::RuneStrike;
    case AbilityId::KnightIronGuard:
        return LeviathanKnightSynergyKind::IronGuard;
    case AbilityId::KnightBulwarkCharge:
        return LeviathanKnightSynergyKind::BulwarkCharge;
    case AbilityId::KnightPerfectRiposte:
        return LeviathanKnightSynergyKind::PerfectRiposte;
    case AbilityId::KnightColossusFury:
        // Je raccorde la « Tempête d'acier » à l'emplacement de la Fureur
        // du colosse, qui est son talent offensif temporaire existant.
        return LeviathanKnightSynergyKind::SteelTempest;
    case AbilityId::KnightLivingFortress:
        return LeviathanKnightSynergyKind::LivingFortress;
    case AbilityId::KnightTitanJudgment:
        return LeviathanKnightSynergyKind::TitanJudgment;
    default:
        return LeviathanKnightSynergyKind::None;
    }
}

[[nodiscard]] constexpr auto leviathan_synergy_ability(
    LeviathanKnightSynergyKind synergy) noexcept -> AbilityId {
    switch (synergy) {
    case LeviathanKnightSynergyKind::RuneStrike:
        return AbilityId::KnightVanguardStrike;
    case LeviathanKnightSynergyKind::IronGuard:
        return AbilityId::KnightIronGuard;
    case LeviathanKnightSynergyKind::BulwarkCharge:
        return AbilityId::KnightBulwarkCharge;
    case LeviathanKnightSynergyKind::PerfectRiposte:
        return AbilityId::KnightPerfectRiposte;
    case LeviathanKnightSynergyKind::SteelTempest:
        return AbilityId::KnightColossusFury;
    case LeviathanKnightSynergyKind::LivingFortress:
        return AbilityId::KnightLivingFortress;
    case LeviathanKnightSynergyKind::TitanJudgment:
        return AbilityId::KnightTitanJudgment;
    case LeviathanKnightSynergyKind::None:
    case LeviathanKnightSynergyKind::Count:
        return AbilityId::None;
    }
    return AbilityId::None;
}

class LeviathanKnightSynergyRuntime {
public:
    [[nodiscard]] auto activate(
        const PlayerBuildState& build,
        const LeviathanSynergyActivationRequest& request) noexcept
        -> LeviathanSynergyActivationResult;

    [[nodiscard]] auto deactivate(
        const LeviathanSynergyDeactivationRequest& request) noexcept
        -> LeviathanSynergyStatus;

    [[nodiscard]] auto advance(
        const PlayerBuildState& build,
        float delta_seconds) noexcept -> LeviathanSynergyStatus;

    [[nodiscard]] auto prepare_attack(
        const PlayerBuildState& build,
        const LeviathanAttackSynergyRequest& request) noexcept
        -> LeviathanAttackSynergyResult;

    [[nodiscard]] auto modify_guard(
        const PlayerBuildState& build,
        const LeviathanGuardSynergyRequest& request) noexcept
        -> LeviathanGuardSynergyResult;

    [[nodiscard]] auto complete_bulwark_charge(
        const PlayerBuildState& build,
        const LeviathanBulwarkSweepRequest& request) noexcept
        -> LeviathanBulwarkSweepResult;

    [[nodiscard]] auto consume_perfect_riposte(
        const PlayerBuildState& build,
        const LeviathanPerfectRiposteRequest& request) noexcept
        -> LeviathanPerfectRiposteResult;

    [[nodiscard]] auto prepare_titan_impact(
        const PlayerBuildState& build,
        const LeviathanTitanImpactRequest& request) noexcept
        -> LeviathanTitanImpactResult;

    [[nodiscard]] auto view() const noexcept
        -> LeviathanKnightSynergyView;

    [[nodiscard]] auto peek_events() const noexcept
        -> std::span<const LeviathanSynergyEvent>;

    [[nodiscard]] auto drain_events(
        std::span<LeviathanSynergyEvent> destination) noexcept
        -> std::size_t;

    void reset() noexcept;

private:
    struct EffectSlot {
        std::uint64_t activation_sequence = 0U;
        float remaining_seconds = 0.0F;
        std::uint8_t rank = 0U;
        bool active = false;
    };

    [[nodiscard]] static constexpr auto kind_index(
        LeviathanKnightSynergyKind kind) noexcept -> std::size_t {
        return static_cast<std::size_t>(kind);
    }

    [[nodiscard]] auto slot(
        LeviathanKnightSynergyKind kind) noexcept -> EffectSlot&;
    [[nodiscard]] auto slot(
        LeviathanKnightSynergyKind kind) const noexcept
        -> const EffectSlot&;
    [[nodiscard]] auto eligible_rank(
        const PlayerBuildState& build,
        LeviathanKnightSynergyKind kind) const noexcept
        -> std::uint8_t;
    void reconcile(const PlayerBuildState& build) noexcept;
    void clear_slot(
        LeviathanKnightSynergyKind kind,
        LeviathanSynergyEventType event_type) noexcept;
    void consume_slot(
        LeviathanKnightSynergyKind kind,
        std::uint64_t action_sequence,
        ColossalAttackKind attack) noexcept;
    void push_event(const LeviathanSynergyEvent& event) noexcept;

    std::array<
        EffectSlot,
        static_cast<std::size_t>(
            LeviathanKnightSynergyKind::Count)> slots_ {};
    std::array<
        std::uint64_t,
        static_cast<std::size_t>(
            LeviathanKnightSynergyKind::Count)>
        last_activation_sequences_ {};
    std::array<
        std::uint64_t,
        static_cast<std::size_t>(
            LeviathanKnightSynergyKind::Count)>
        last_consumed_sequences_ {};
    std::array<
        LeviathanSynergyEvent,
        kLeviathanSynergyEventCapacity> events_ {};
    std::size_t event_count_ = 0U;
    std::uint64_t next_event_id_ = 1U;
    std::uint64_t last_attack_sequence_ = 0U;
    LeviathanAttackSynergyResult last_attack_result_ {};
    bool event_overflowed_ = false;
};

} // namespace valcraft
