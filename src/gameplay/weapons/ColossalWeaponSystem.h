#pragma once

#include "gameplay/weapons/ColossalWeaponCombat.h"
#include "gameplay/weapons/ColossalWeaponState.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace valcraft {

inline constexpr std::size_t kColossalWeaponMaximumEvents = 32U;
inline constexpr float kColossalWeaponMaximumUpdateSeconds = 86'400.0F;
inline constexpr float kColossalAttackOverrideMaximumRangeMultiplier = 1.50F;
inline constexpr float kColossalAttackOverrideMaximumArcMultiplier = 1.50F;
inline constexpr float kColossalAttackOverrideMaximumStaggerMultiplier = 2.0F;
inline constexpr std::uint8_t kColossalAttackOverrideMaximumTargets = 12U;

struct LeviathanBulwarkSweepResult;
struct LeviathanPerfectRiposteResult;

enum class ColossalAttackOverrideStatus : std::uint8_t {
    Queued = 0,
    Replaced,
    RejectedInvalidRequest,
    RejectedUnsafeState,
    RejectedReplay,
    RejectedInactiveSynergy,
};

struct ColossalAttackOverrideRequest {
    std::uint64_t request_sequence = 0U;
    ColossalAttackKind forced_attack = ColossalAttackKind::None;
    ColossalAttackShape forced_shape =
        ColossalAttackShape::HorizontalArc;
    float range_multiplier = 1.0F;
    float arc_multiplier = 1.0F;
    float stagger_multiplier = 1.0F;
    std::uint8_t maximum_targets = 0U;
};

struct ColossalAttackOverrideResult {
    ColossalAttackOverrideStatus status =
        ColossalAttackOverrideStatus::RejectedInvalidRequest;
    ColossalAttackOverrideRequest effective_request {};

    [[nodiscard]] constexpr auto accepted() const noexcept -> bool {
        return status == ColossalAttackOverrideStatus::Queued ||
               status == ColossalAttackOverrideStatus::Replaced;
    }
};

struct ColossalAttackOverrideView {
    ColossalAttackOverrideRequest request {};
    bool queued = false;
    bool active = false;
};

class ColossalWeaponSystem {
public:
    explicit ColossalWeaponSystem(
        ColossalWeaponDefinition definition =
            kLeviathanSpineDefinition) noexcept;

    [[nodiscard]] auto update(
        const ColossalWeaponInput& input,
        const ColossalWeaponUpdateContext& context,
        float elapsed_seconds) noexcept
        -> std::span<const ColossalWeaponEvent>;

    // Je résous les contacts après l'update afin que le monde utilise la pose
    // finale de la lame et puisse encore enrichir les événements de la frame.
    [[nodiscard]] auto notify_attack_resolution(
        const ColossalAttackResolutionReport& report) noexcept
        -> std::span<const ColossalWeaponEvent>;

    [[nodiscard]] auto intercept_incoming_attack(
        const ColossalGuardRequest& request) noexcept
        -> ColossalGuardResult;

    // J'arme ici une seule séquence offensive sans exposer les transitions
    // internes de l'arme au coordinateur de jeu.
    [[nodiscard]] auto queue_next_attack_override(
        const ColossalAttackOverrideRequest& request) noexcept
        -> ColossalAttackOverrideResult;
    [[nodiscard]] auto queue_next_attack_override(
        const LeviathanBulwarkSweepResult& result,
        std::uint64_t request_sequence) noexcept
        -> ColossalAttackOverrideResult;
    [[nodiscard]] auto queue_next_attack_override(
        const LeviathanPerfectRiposteResult& result,
        std::uint64_t request_sequence) noexcept
        -> ColossalAttackOverrideResult;
    [[nodiscard]] auto attack_override_view() const noexcept
        -> ColossalAttackOverrideView;

    void interrupt() noexcept;
    void reset() noexcept;

    [[nodiscard]] auto snapshot() const noexcept
        -> ColossalWeaponStateSnapshot;
    [[nodiscard]] auto events() const noexcept
        -> std::span<const ColossalWeaponEvent>;
    [[nodiscard]] auto definition() const noexcept
        -> const ColossalWeaponDefinition&;
    [[nodiscard]] auto current_attack_definition() const noexcept
        -> const ColossalAttackDefinition*;

private:
    void clear_events() noexcept;
    void push_event(
        ColossalWeaponEvent event) noexcept;
    void reject(
        ColossalWeaponRejection rejection,
        ColossalWeaponEventType type =
            ColossalWeaponEventType::ActionRejected) noexcept;
    void transition_to(
        ColossalWeaponState state,
        float duration_seconds) noexcept;
    void set_momentum(
        std::uint8_t momentum) noexcept;
    void set_stability(
        float stability) noexcept;
    void begin_normal_attack(
        const ColossalWeaponUpdateContext& context) noexcept;
    void begin_attack(
        ColossalAttackKind attack,
        const ColossalWeaponUpdateContext& context,
        std::uint8_t combo_step) noexcept;
    void begin_charged_execution(
        const ColossalWeaponUpdateContext& context) noexcept;
    void clear_attack_override(
        bool clear_replay_guard = false) noexcept;
    void finish_active_phase() noexcept;
    void finish_recovery() noexcept;
    void advance_state(
        const ColossalWeaponUpdateContext& context,
        float elapsed_seconds) noexcept;
    void advance_charge(
        const ColossalWeaponUpdateContext& context,
        float& remaining_seconds) noexcept;
    void complete_timed_state(
        const ColossalWeaponUpdateContext& context) noexcept;
    void update_passive_resources(
        float elapsed_seconds) noexcept;
    void refresh_mastery(
        const ColossalWeaponUpdateContext& context) noexcept;
    void handle_input_before_timing(
        const ColossalWeaponInput& input,
        const ColossalWeaponUpdateContext& context) noexcept;
    void handle_input_after_timing(
        const ColossalWeaponInput& input,
        const ColossalWeaponUpdateContext& context) noexcept;
    [[nodiscard]] auto is_attack_locked_state() const noexcept -> bool;
    [[nodiscard]] auto current_movement_multiplier() const noexcept -> float;
    [[nodiscard]] auto current_state_progress() const noexcept -> float;

    ColossalWeaponDefinition definition_ {};
    ColossalMasteryProfile mastery_ {};
    ColossalWeaponState state_ =
        ColossalWeaponState::Holstered;
    ColossalWeaponState impact_resume_state_ =
        ColossalWeaponState::Active;
    ColossalAttackKind attack_ =
        ColossalAttackKind::None;
    ColossalAttackShape attack_shape_ =
        ColossalAttackShape::HorizontalArc;
    ColossalAttackDefinition effective_attack_definition_ {};
    ColossalAttackOverrideRequest queued_attack_override_ {};
    ColossalAttackOverrideRequest active_attack_override_ {};
    ColossalWeaponRejection last_rejection_ =
        ColossalWeaponRejection::None;
    std::uint64_t last_attack_override_request_sequence_ = 0U;
    std::uint64_t attack_sequence_ = 0U;
    float state_elapsed_seconds_ = 0.0F;
    float state_duration_seconds_ = 0.0F;
    float impact_resume_elapsed_seconds_ = 0.0F;
    float impact_resume_duration_seconds_ = 0.0F;
    float charge_seconds_ = 0.0F;
    float charge_cooldown_seconds_ = 0.0F;
    float stability_ = 100.0F;
    float maximum_stability_ = 100.0F;
    float stability_regeneration_delay_seconds_ = 0.0F;
    float momentum_idle_seconds_ = 0.0F;
    float progression_damage_multiplier_ = 1.0F;
    float pending_recovery_penalty_seconds_ = 0.0F;
    std::uint8_t momentum_ = 0U;
    std::uint8_t combo_step_ = 0U;
    std::uint8_t attack_momentum_ = 0U;
    bool attack_buffered_ = false;
    bool attack_has_hit_ = false;
    bool charge_committed_ = false;
    bool charge_rejection_emitted_ = false;
    bool contextual_vertical_ = false;
    bool press_started_sprinting_ = false;
    bool auto_sheathing_ = false;
    bool attack_override_queued_ = false;
    bool attack_override_active_ = false;
    bool effective_attack_definition_valid_ = false;
    bool event_overflowed_ = false;
    std::array<ColossalWeaponEvent, kColossalWeaponMaximumEvents>
        event_buffer_ {};
    std::size_t event_count_ = 0U;
};

} // namespace valcraft
