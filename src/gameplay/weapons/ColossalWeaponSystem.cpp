#include "gameplay/weapons/ColossalWeaponSystem.h"

#include "gameplay/weapons/LeviathanKnightSynergy.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace valcraft {
namespace {

inline constexpr float kMinimumTimedStateSeconds = 0.0001F;
inline constexpr std::size_t kMaximumTransitionsPerUpdate = 64U;

[[nodiscard]] auto finite_duration(
    float value,
    float fallback = 0.0F) noexcept -> float {
    return std::isfinite(value) && value >= 0.0F
               ? value
               : fallback;
}

[[nodiscard]] auto combo_attack(
    std::uint8_t combo_step) noexcept -> ColossalAttackKind {
    switch (combo_step % 3U) {
    case 0U:
        return ColossalAttackKind::FirstSweep;
    case 1U:
        return ColossalAttackKind::SecondSweep;
    case 2U:
        return ColossalAttackKind::Earthbreaker;
    default:
        return ColossalAttackKind::FirstSweep;
    }
}

[[nodiscard]] auto is_timed_state(
    ColossalWeaponState state) noexcept -> bool {
    switch (state) {
    case ColossalWeaponState::Drawing:
    case ColossalWeaponState::Windup:
    case ColossalWeaponState::Active:
    case ColossalWeaponState::Recovery:
    case ColossalWeaponState::GuardBroken:
    case ColossalWeaponState::Impact:
    case ColossalWeaponState::Sheathing:
        return true;
    case ColossalWeaponState::Holstered:
    case ColossalWeaponState::Idle:
    case ColossalWeaponState::Guard:
    case ColossalWeaponState::Charge:
        return false;
    }
    return false;
}

[[nodiscard]] auto is_offensive_state(
    ColossalWeaponState state) noexcept -> bool {
    switch (state) {
    case ColossalWeaponState::Windup:
    case ColossalWeaponState::Active:
    case ColossalWeaponState::Recovery:
    case ColossalWeaponState::Charge:
    case ColossalWeaponState::Impact:
        return true;
    case ColossalWeaponState::Holstered:
    case ColossalWeaponState::Drawing:
    case ColossalWeaponState::Idle:
    case ColossalWeaponState::Guard:
    case ColossalWeaponState::GuardBroken:
    case ColossalWeaponState::Sheathing:
        return false;
    }
    return false;
}

[[nodiscard]] auto clamped_momentum(
    std::uint8_t momentum,
    const ColossalWeaponDefinition& definition) noexcept
    -> std::uint8_t {
    return std::min(
        momentum,
        definition.maximum_momentum);
}

[[nodiscard]] constexpr auto valid_override_attack(
    ColossalAttackKind attack) noexcept -> bool {
    return attack == ColossalAttackKind::FirstSweep ||
           attack == ColossalAttackKind::SecondSweep;
}

[[nodiscard]] constexpr auto valid_attack_shape(
    ColossalAttackShape shape) noexcept -> bool {
    return shape >= ColossalAttackShape::HorizontalArc &&
           shape <= ColossalAttackShape::DiagonalArc;
}

[[nodiscard]] constexpr auto override_safe_state(
    ColossalWeaponState state) noexcept -> bool {
    return state == ColossalWeaponState::Idle ||
           state == ColossalWeaponState::Guard;
}

[[nodiscard]] auto finite_bounded_multiplier(
    float value,
    float maximum) noexcept -> float {
    return std::clamp(value, 1.0F, maximum);
}

[[nodiscard]] constexpr auto combo_step_for_attack(
    ColossalAttackKind attack,
    std::uint8_t fallback) noexcept -> std::uint8_t {
    switch (attack) {
    case ColossalAttackKind::FirstSweep:
        return 0U;
    case ColossalAttackKind::SecondSweep:
        return 1U;
    case ColossalAttackKind::Earthbreaker:
        return 2U;
    case ColossalAttackKind::RunningCleave:
    case ColossalAttackKind::ChargedExecution:
    case ColossalAttackKind::None:
        return fallback;
    }
    return fallback;
}

} // namespace

ColossalWeaponSystem::ColossalWeaponSystem(
    ColossalWeaponDefinition definition) noexcept
    : definition_(definition) {
    reset();
}

auto ColossalWeaponSystem::update(
    const ColossalWeaponInput& input,
    const ColossalWeaponUpdateContext& context,
    float elapsed_seconds) noexcept
    -> std::span<const ColossalWeaponEvent> {
    clear_events();
    refresh_mastery(context);

    if (!std::isfinite(elapsed_seconds) ||
        elapsed_seconds < 0.0F) {
        reject(ColossalWeaponRejection::InvalidInput);
        return events();
    }

    const auto elapsed =
        std::min(
            elapsed_seconds,
            kColossalWeaponMaximumUpdateSeconds);
    handle_input_before_timing(input, context);
    advance_state(context, elapsed);
    handle_input_after_timing(input, context);
    return events();
}

auto ColossalWeaponSystem::notify_attack_resolution(
    const ColossalAttackResolutionReport& report) noexcept
    -> std::span<const ColossalWeaponEvent> {
    if (state_ != ColossalWeaponState::Active) {
        return events();
    }

    const auto* attack = current_attack_definition();
    if (attack == nullptr) {
        return events();
    }
    const auto new_hits =
        std::min(
            report.newly_hit_targets,
            attack->maximum_targets);
    if (new_hits > 0U && !attack_has_hit_) {
        attack_has_hit_ = true;
        momentum_idle_seconds_ = 0.0F;
        set_momentum(
            static_cast<std::uint8_t>(
                std::min<std::uint16_t>(
                    static_cast<std::uint16_t>(momentum_) + 1U,
                    definition_.maximum_momentum)));
    }

    if (report.wall_hit) {
        pending_recovery_penalty_seconds_ =
            std::max(
                pending_recovery_penalty_seconds_,
                definition_.wall_recovery_penalty_seconds);
        set_momentum(0U);
    }

    if (new_hits == 0U && !report.wall_hit) {
        return events();
    }

    auto impact_stop = 0.0F;
    if (new_hits > 0U) {
        impact_stop =
            colossal_impact_stop_seconds(
                report.heaviest_target,
                report.severed_limb,
                definition_);
    }
    if (report.wall_hit) {
        impact_stop =
            std::max(
                impact_stop,
                definition_.wall_impact_stop_seconds);
    }

    ColossalWeaponEvent impact_event {};
    impact_event.type =
        ColossalWeaponEventType::AttackImpact;
    impact_event.state = state_;
    impact_event.attack = attack_;
    impact_event.attack_shape = attack_shape_;
    impact_event.attack_sequence = attack_sequence_;
    impact_event.primary_value = impact_stop;
    impact_event.secondary_value =
        report.wall_hit
            ? definition_.wall_recovery_penalty_seconds
            : 0.0F;
    impact_event.count = new_hits;
    impact_event.detail_code =
        static_cast<std::uint8_t>(report.material);
    impact_event.protected_surface =
        report.protected_surface;
    push_event(impact_event);

    impact_resume_state_ =
        ColossalWeaponState::Active;
    impact_resume_elapsed_seconds_ =
        state_elapsed_seconds_;
    impact_resume_duration_seconds_ =
        state_duration_seconds_;
    transition_to(
        ColossalWeaponState::Impact,
        std::max(
            impact_stop,
            kMinimumTimedStateSeconds));
    return events();
}

auto ColossalWeaponSystem::intercept_incoming_attack(
    const ColossalGuardRequest& request) noexcept
    -> ColossalGuardResult {
    auto effective_request = request;
    effective_request.guard_active =
        state_ == ColossalWeaponState::Guard;
    effective_request.current_stability =
        stability_;
    effective_request.guard_elapsed_seconds =
        state_elapsed_seconds_;
    const auto result =
        resolve_colossal_guard(
            effective_request,
            definition_);
    if (!result.blocked) {
        return result;
    }

    stability_regeneration_delay_seconds_ =
        definition_.stability_regeneration_delay_seconds;
    set_stability(result.stability_after);
    if (result.perfect) {
        momentum_idle_seconds_ = 0.0F;
        set_momentum(
            static_cast<std::uint8_t>(
                std::min<std::uint16_t>(
                    static_cast<std::uint16_t>(momentum_) + 1U,
                    definition_.maximum_momentum)));
        ColossalWeaponEvent event {};
        event.type =
            ColossalWeaponEventType::PerfectGuard;
        event.state = state_;
        event.primary_value = result.attacker_stagger;
        event.secondary_value = result.stability_lost;
        push_event(event);
    }

    if (result.guard_broken) {
        set_momentum(0U);
        clear_attack_override();
        transition_to(
            ColossalWeaponState::GuardBroken,
            definition_.guard_break_seconds);
        ColossalWeaponEvent event {};
        event.type =
            ColossalWeaponEventType::GuardBroken;
        event.state = state_;
        event.primary_value =
            definition_.guard_break_seconds;
        push_event(event);
    }
    return result;
}

auto ColossalWeaponSystem::queue_next_attack_override(
    const ColossalAttackOverrideRequest& request) noexcept
    -> ColossalAttackOverrideResult {
    ColossalAttackOverrideResult result {};
    if (request.request_sequence == 0U ||
        !valid_override_attack(request.forced_attack) ||
        !valid_attack_shape(request.forced_shape) ||
        !std::isfinite(request.range_multiplier) ||
        !std::isfinite(request.arc_multiplier) ||
        !std::isfinite(request.stagger_multiplier) ||
        request.range_multiplier <= 0.0F ||
        request.arc_multiplier <= 0.0F ||
        request.stagger_multiplier <= 0.0F) {
        result.status =
            ColossalAttackOverrideStatus::RejectedInvalidRequest;
        return result;
    }
    if (!override_safe_state(state_)) {
        result.status =
            ColossalAttackOverrideStatus::RejectedUnsafeState;
        return result;
    }
    if (request.request_sequence <=
        last_attack_override_request_sequence_) {
        result.status =
            ColossalAttackOverrideStatus::RejectedReplay;
        return result;
    }

    auto effective = request;
    effective.range_multiplier =
        finite_bounded_multiplier(
            request.range_multiplier,
            kColossalAttackOverrideMaximumRangeMultiplier);
    effective.arc_multiplier =
        finite_bounded_multiplier(
            request.arc_multiplier,
            kColossalAttackOverrideMaximumArcMultiplier);
    effective.stagger_multiplier =
        finite_bounded_multiplier(
            request.stagger_multiplier,
            kColossalAttackOverrideMaximumStaggerMultiplier);
    const auto* attack =
        colossal_attack_definition(effective.forced_attack);
    if (attack == nullptr) {
        result.status =
            ColossalAttackOverrideStatus::RejectedInvalidRequest;
        return result;
    }
    effective.maximum_targets =
        request.maximum_targets == 0U
            ? attack->maximum_targets
            : std::clamp(
                  request.maximum_targets,
                  std::uint8_t {1U},
                  kColossalAttackOverrideMaximumTargets);

    const auto replaced = attack_override_queued_;
    queued_attack_override_ = effective;
    attack_override_queued_ = true;
    last_attack_override_request_sequence_ =
        request.request_sequence;
    result.status =
        replaced
            ? ColossalAttackOverrideStatus::Replaced
            : ColossalAttackOverrideStatus::Queued;
    result.effective_request = effective;
    return result;
}

auto ColossalWeaponSystem::queue_next_attack_override(
    const LeviathanBulwarkSweepResult& result,
    std::uint64_t request_sequence) noexcept
    -> ColossalAttackOverrideResult {
    if (result.status != LeviathanSynergyStatus::Applied ||
        !result.massive_sweep_requested) {
        return {
            ColossalAttackOverrideStatus::RejectedInactiveSynergy,
            {},
        };
    }
    return queue_next_attack_override({
        request_sequence,
        result.forced_attack,
        result.forced_shape,
        result.range_multiplier,
        result.arc_multiplier,
        result.stagger_multiplier,
        result.maximum_targets,
    });
}

auto ColossalWeaponSystem::queue_next_attack_override(
    const LeviathanPerfectRiposteResult& result,
    std::uint64_t request_sequence) noexcept
    -> ColossalAttackOverrideResult {
    if (result.status != LeviathanSynergyStatus::Applied ||
        !result.second_combo_attack_requested) {
        return {
            ColossalAttackOverrideStatus::RejectedInactiveSynergy,
            {},
        };
    }
    return queue_next_attack_override({
        request_sequence,
        result.forced_attack,
        result.forced_shape,
        1.0F,
        1.0F,
        1.0F,
        0U,
    });
}

auto ColossalWeaponSystem::attack_override_view() const noexcept
    -> ColossalAttackOverrideView {
    if (attack_override_queued_) {
        return {
            queued_attack_override_,
            true,
            false,
        };
    }
    if (attack_override_active_) {
        return {
            active_attack_override_,
            false,
            true,
        };
    }
    return {};
}

void ColossalWeaponSystem::interrupt() noexcept {
    clear_attack_override();
    if (state_ == ColossalWeaponState::Holstered ||
        state_ == ColossalWeaponState::Sheathing ||
        (state_ == ColossalWeaponState::Charge &&
         charge_committed_)) {
        return;
    }
    set_momentum(0U);
    attack_buffered_ = false;
    attack_has_hit_ = false;
    charge_committed_ = false;
    charge_seconds_ = 0.0F;
    pending_recovery_penalty_seconds_ = 0.0F;
    attack_ = ColossalAttackKind::None;
    transition_to(
        ColossalWeaponState::Recovery,
        0.45F);
}

void ColossalWeaponSystem::reset() noexcept {
    state_ = ColossalWeaponState::Holstered;
    impact_resume_state_ =
        ColossalWeaponState::Active;
    attack_ = ColossalAttackKind::None;
    attack_shape_ =
        ColossalAttackShape::HorizontalArc;
    effective_attack_definition_ = {};
    last_rejection_ =
        ColossalWeaponRejection::None;
    attack_sequence_ = 0U;
    state_elapsed_seconds_ = 0.0F;
    state_duration_seconds_ = 0.0F;
    impact_resume_elapsed_seconds_ = 0.0F;
    impact_resume_duration_seconds_ = 0.0F;
    charge_seconds_ = 0.0F;
    charge_cooldown_seconds_ = 0.0F;
    maximum_stability_ =
        std::max(
            0.0F,
            finite_duration(
                definition_.maximum_stability,
                kLeviathanSpineDefinition.maximum_stability));
    stability_ = maximum_stability_;
    stability_regeneration_delay_seconds_ = 0.0F;
    momentum_idle_seconds_ = 0.0F;
    progression_damage_multiplier_ = 1.0F;
    pending_recovery_penalty_seconds_ = 0.0F;
    momentum_ = 0U;
    combo_step_ = 0U;
    attack_momentum_ = 0U;
    attack_buffered_ = false;
    attack_has_hit_ = false;
    charge_committed_ = false;
    charge_rejection_emitted_ = false;
    contextual_vertical_ = false;
    press_started_sprinting_ = false;
    auto_sheathing_ = false;
    clear_attack_override(true);
    clear_events();
}

auto ColossalWeaponSystem::snapshot() const noexcept
    -> ColossalWeaponStateSnapshot {
    ColossalWeaponStateSnapshot result {};
    result.state = state_;
    result.attack = attack_;
    result.attack_shape = attack_shape_;
    result.last_rejection = last_rejection_;
    result.attack_sequence = attack_sequence_;
    result.state_elapsed_seconds =
        state_elapsed_seconds_;
    result.state_duration_seconds =
        state_duration_seconds_;
    result.state_progress =
        current_state_progress();
    result.charge_seconds = charge_seconds_;
    result.charge_progress =
        definition_.charge_maximum_seconds >
                kMinimumTimedStateSeconds
            ? std::clamp(
                  charge_seconds_ /
                      definition_.charge_maximum_seconds,
                  0.0F,
                  1.0F)
            : 0.0F;
    result.charge_cooldown_seconds =
        charge_cooldown_seconds_;
    result.stability = stability_;
    result.maximum_stability =
        maximum_stability_;
    result.stability_regeneration_delay_seconds =
        stability_regeneration_delay_seconds_;
    result.movement_multiplier =
        current_movement_multiplier();
    result.damage_multiplier =
        colossal_final_damage_multiplier(
            progression_damage_multiplier_,
            mastery_.strength_damage_multiplier,
            1.0F,
            1.0F);
    result.stagger_multiplier =
        1.0F +
        static_cast<float>(attack_momentum_) *
            definition_.momentum_stagger_bonus_per_stack;
    result.momentum = momentum_;
    result.combo_step = combo_step_;
    result.attack_buffered =
        attack_buffered_;
    result.attack_has_hit =
        attack_has_hit_;
    result.charge_committed =
        charge_committed_;
    result.contextual_vertical =
        contextual_vertical_;
    result.can_sprint =
        state_ == ColossalWeaponState::Holstered ||
        state_ == ColossalWeaponState::Idle ||
        state_ == ColossalWeaponState::Drawing ||
        state_ == ColossalWeaponState::Sheathing;
    result.can_change_equipment =
        state_ == ColossalWeaponState::Holstered ||
        state_ == ColossalWeaponState::Idle;
    result.event_overflowed =
        event_overflowed_;
    return result;
}

auto ColossalWeaponSystem::events() const noexcept
    -> std::span<const ColossalWeaponEvent> {
    return {
        event_buffer_.data(),
        std::min(event_count_, event_buffer_.size()),
    };
}

auto ColossalWeaponSystem::definition() const noexcept
    -> const ColossalWeaponDefinition& {
    return definition_;
}

auto ColossalWeaponSystem::current_attack_definition() const noexcept
    -> const ColossalAttackDefinition* {
    if (effective_attack_definition_valid_ &&
        effective_attack_definition_.kind == attack_) {
        return &effective_attack_definition_;
    }
    return colossal_attack_definition(attack_);
}

void ColossalWeaponSystem::clear_events() noexcept {
    event_count_ = 0U;
    event_overflowed_ = false;
}

void ColossalWeaponSystem::push_event(
    ColossalWeaponEvent event) noexcept {
    if (event_count_ >= event_buffer_.size()) {
        event_overflowed_ = true;
        return;
    }
    event_buffer_[event_count_] = event;
    ++event_count_;
}

void ColossalWeaponSystem::reject(
    ColossalWeaponRejection rejection,
    ColossalWeaponEventType type) noexcept {
    last_rejection_ = rejection;
    ColossalWeaponEvent event {};
    event.type = type;
    event.state = state_;
    event.attack = attack_;
    event.rejection = rejection;
    event.attack_sequence = attack_sequence_;
    push_event(event);
}

void ColossalWeaponSystem::transition_to(
    ColossalWeaponState state,
    float duration_seconds) noexcept {
    const auto previous = state_;
    state_ = state;
    state_elapsed_seconds_ = 0.0F;
    state_duration_seconds_ =
        finite_duration(duration_seconds);
    ColossalWeaponEvent event {};
    event.type =
        ColossalWeaponEventType::StateChanged;
    event.previous_state = previous;
    event.state = state_;
    event.attack = attack_;
    event.attack_shape = attack_shape_;
    event.attack_sequence = attack_sequence_;
    event.primary_value =
        state_duration_seconds_;
    push_event(event);
}

void ColossalWeaponSystem::set_momentum(
    std::uint8_t momentum) noexcept {
    const auto next =
        clamped_momentum(
            momentum,
            definition_);
    if (next == momentum_) {
        return;
    }
    momentum_ = next;
    ColossalWeaponEvent event {};
    event.type =
        ColossalWeaponEventType::MomentumChanged;
    event.state = state_;
    event.attack = attack_;
    event.attack_sequence = attack_sequence_;
    event.count = momentum_;
    push_event(event);
}

void ColossalWeaponSystem::set_stability(
    float stability) noexcept {
    const auto next =
        std::clamp(
            finite_duration(stability),
            0.0F,
            maximum_stability_);
    if (std::abs(next - stability_) <=
        std::numeric_limits<float>::epsilon()) {
        return;
    }
    stability_ = next;
    ColossalWeaponEvent event {};
    event.type =
        ColossalWeaponEventType::StabilityChanged;
    event.state = state_;
    event.primary_value = stability_;
    event.secondary_value =
        maximum_stability_;
    push_event(event);
}

void ColossalWeaponSystem::begin_normal_attack(
    const ColossalWeaponUpdateContext& context) noexcept {
    if (press_started_sprinting_) {
        begin_attack(
            ColossalAttackKind::RunningCleave,
            context,
            0U);
        return;
    }
    begin_attack(
        combo_attack(combo_step_),
        context,
        combo_step_);
}

void ColossalWeaponSystem::begin_attack(
    ColossalAttackKind attack,
    const ColossalWeaponUpdateContext& context,
    std::uint8_t combo_step) noexcept {
    const auto applies_override =
        attack_override_queued_;
    if (applies_override) {
        attack = queued_attack_override_.forced_attack;
        combo_step =
            combo_step_for_attack(
                attack,
                combo_step);
    }
    const auto* attack_definition =
        colossal_attack_definition(attack);
    if (attack_definition == nullptr) {
        reject(ColossalWeaponRejection::InvalidInput);
        transition_to(
            ColossalWeaponState::Idle,
            0.0F);
        return;
    }

    effective_attack_definition_ =
        *attack_definition;
    effective_attack_definition_valid_ = true;
    attack_override_active_ = applies_override;
    if (applies_override) {
        active_attack_override_ =
            queued_attack_override_;
        attack_override_queued_ = false;
        queued_attack_override_ = {};
        effective_attack_definition_.shape =
            active_attack_override_.forced_shape;
        effective_attack_definition_.range_blocks *=
            active_attack_override_.range_multiplier;
        effective_attack_definition_.arc_degrees =
            std::min(
                360.0F,
                effective_attack_definition_.arc_degrees *
                    active_attack_override_.arc_multiplier);
        effective_attack_definition_.stagger_power *=
            active_attack_override_.stagger_multiplier;
        effective_attack_definition_.maximum_targets =
            active_attack_override_.maximum_targets;
    } else {
        active_attack_override_ = {};
    }
    attack_definition =
        &effective_attack_definition_;
    attack_ = attack;
    combo_step_ = combo_step % 3U;
    attack_shape_ =
        attack_definition->shape;
    contextual_vertical_ =
        context.narrow_tunnel &&
        (attack == ColossalAttackKind::FirstSweep ||
         attack == ColossalAttackKind::SecondSweep);
    if (contextual_vertical_) {
        attack_shape_ =
            ColossalAttackShape::VerticalArc;
    }
    effective_attack_definition_.shape =
        attack_shape_;
    ++attack_sequence_;
    if (attack_sequence_ == 0U) {
        ++attack_sequence_;
    }
    attack_has_hit_ = false;
    attack_buffered_ = false;
    attack_momentum_ = momentum_;
    momentum_idle_seconds_ = 0.0F;
    charge_seconds_ = 0.0F;
    charge_committed_ = false;
    charge_rejection_emitted_ = false;
    pending_recovery_penalty_seconds_ = 0.0F;
    const auto momentum_windup_multiplier =
        std::max(
            0.0F,
            1.0F -
                static_cast<float>(attack_momentum_) *
                    definition_
                        .momentum_windup_reduction_per_stack);
    const auto windup =
        attack_definition->windup_seconds *
        mastery_.windup_multiplier *
        momentum_windup_multiplier;
    transition_to(
        ColossalWeaponState::Windup,
        std::max(windup, kMinimumTimedStateSeconds));

    ColossalWeaponEvent event {};
    event.type =
        ColossalWeaponEventType::AttackStarted;
    event.state = state_;
    event.attack = attack_;
    event.attack_shape = attack_shape_;
    event.attack_sequence = attack_sequence_;
    event.primary_value = windup;
    event.secondary_value =
        attack_definition->base_damage;
    event.count =
        attack_definition->maximum_targets;
    push_event(event);

    if (attack ==
        ColossalAttackKind::RunningCleave) {
        ColossalWeaponEvent advance_event {};
        advance_event.type =
            ColossalWeaponEventType::RunningAdvanceRequested;
        advance_event.state = state_;
        advance_event.attack = attack_;
        advance_event.attack_shape = attack_shape_;
        advance_event.attack_sequence =
            attack_sequence_;
        advance_event.primary_value =
            attack_definition->forward_advance_blocks;
        advance_event.secondary_value = 0.25F;
        push_event(advance_event);
    }
}

void ColossalWeaponSystem::begin_charged_execution(
    const ColossalWeaponUpdateContext& context) noexcept {
    const auto* attack_definition =
        colossal_attack_definition(
            ColossalAttackKind::ChargedExecution);
    if (attack_definition == nullptr) {
        reject(ColossalWeaponRejection::InvalidInput);
        transition_to(
            ColossalWeaponState::Idle,
            0.0F);
        return;
    }

    effective_attack_definition_ =
        *attack_definition;
    effective_attack_definition_valid_ = true;
    attack_override_active_ = false;
    active_attack_override_ = {};
    attack_ =
        ColossalAttackKind::ChargedExecution;
    combo_step_ = 0U;
    attack_shape_ =
        ColossalAttackShape::VerticalArc;
    contextual_vertical_ =
        context.narrow_tunnel;
    ++attack_sequence_;
    if (attack_sequence_ == 0U) {
        ++attack_sequence_;
    }
    attack_has_hit_ = false;
    attack_buffered_ = false;
    attack_momentum_ = momentum_;
    momentum_idle_seconds_ = 0.0F;
    pending_recovery_penalty_seconds_ = 0.0F;
    transition_to(
        ColossalWeaponState::Active,
        std::max(
            attack_definition->active_seconds,
            kMinimumTimedStateSeconds));

    ColossalWeaponEvent started {};
    started.type =
        ColossalWeaponEventType::AttackStarted;
    started.state = state_;
    started.attack = attack_;
    started.attack_shape = attack_shape_;
    started.attack_sequence = attack_sequence_;
    started.primary_value = charge_seconds_;
    started.secondary_value =
        attack_definition->base_damage;
    started.count =
        attack_definition->maximum_targets;
    push_event(started);

    auto active = started;
    active.type =
        ColossalWeaponEventType::AttackBecameActive;
    active.primary_value =
        attack_definition->active_seconds;
    push_event(active);
}

void ColossalWeaponSystem::clear_attack_override(
    bool clear_replay_guard) noexcept {
    queued_attack_override_ = {};
    active_attack_override_ = {};
    attack_override_queued_ = false;
    attack_override_active_ = false;
    effective_attack_definition_ = {};
    effective_attack_definition_valid_ = false;
    if (clear_replay_guard) {
        last_attack_override_request_sequence_ = 0U;
    }
}

void ColossalWeaponSystem::finish_active_phase() noexcept {
    const auto* attack_definition =
        current_attack_definition();
    if (attack_definition == nullptr) {
        transition_to(
            ColossalWeaponState::Idle,
            0.0F);
        return;
    }

    if (!attack_has_hit_) {
        set_momentum(0U);
        ColossalWeaponEvent miss {};
        miss.type =
            ColossalWeaponEventType::AttackMissed;
        miss.state = state_;
        miss.attack = attack_;
        miss.attack_shape = attack_shape_;
        miss.attack_sequence = attack_sequence_;
        push_event(miss);
    }
    const auto recovery =
        attack_definition->recovery_seconds *
            mastery_.recovery_multiplier +
        pending_recovery_penalty_seconds_;
    transition_to(
        ColossalWeaponState::Recovery,
        std::max(
            recovery,
            kMinimumTimedStateSeconds));
}

void ColossalWeaponSystem::finish_recovery() noexcept {
    ColossalWeaponEvent event {};
    event.type =
        ColossalWeaponEventType::AttackFinished;
    event.state = state_;
    event.attack = attack_;
    event.attack_shape = attack_shape_;
    event.attack_sequence = attack_sequence_;
    push_event(event);
}

void ColossalWeaponSystem::advance_state(
    const ColossalWeaponUpdateContext& context,
    float elapsed_seconds) noexcept {
    auto remaining =
        finite_duration(elapsed_seconds);
    for (std::size_t transition_count = 0U;
         remaining > 0.0F &&
         transition_count <
             kMaximumTransitionsPerUpdate;
         ++transition_count) {
        if (state_ == ColossalWeaponState::Charge) {
            const auto previous_remaining = remaining;
            advance_charge(context, remaining);
            if (state_ == ColossalWeaponState::Charge &&
                remaining >= previous_remaining) {
                break;
            }
            continue;
        }
        if (state_ == ColossalWeaponState::Guard) {
            update_passive_resources(remaining);
            state_elapsed_seconds_ += remaining;
            remaining = 0.0F;
            break;
        }
        if (!is_timed_state(state_)) {
            update_passive_resources(remaining);
            remaining = 0.0F;
            break;
        }

        const auto time_to_boundary =
            std::max(
                0.0F,
                state_duration_seconds_ -
                    state_elapsed_seconds_);
        if (time_to_boundary > remaining) {
            update_passive_resources(remaining);
            state_elapsed_seconds_ += remaining;
            remaining = 0.0F;
            break;
        }

        update_passive_resources(time_to_boundary);
        state_elapsed_seconds_ +=
            time_to_boundary;
        remaining -= time_to_boundary;
        complete_timed_state(context);
    }
}

void ColossalWeaponSystem::advance_charge(
    const ColossalWeaponUpdateContext& context,
    float& remaining_seconds) noexcept {
    const auto commit_seconds =
        std::max(
            definition_.charge_commit_seconds,
            kMinimumTimedStateSeconds);
    const auto maximum_seconds =
        std::max(
            definition_.charge_maximum_seconds,
            commit_seconds);

    auto boundary = maximum_seconds;
    if (!charge_committed_ &&
        !charge_rejection_emitted_ &&
        charge_seconds_ < commit_seconds) {
        boundary = commit_seconds;
    }
    const auto time_to_boundary =
        std::max(
            0.0F,
            boundary - charge_seconds_);
    if (time_to_boundary > remaining_seconds) {
        update_passive_resources(remaining_seconds);
        charge_seconds_ += remaining_seconds;
        state_elapsed_seconds_ =
            charge_seconds_;
        remaining_seconds = 0.0F;
        return;
    }

    update_passive_resources(time_to_boundary);
    charge_seconds_ += time_to_boundary;
    state_elapsed_seconds_ =
        charge_seconds_;
    remaining_seconds -= time_to_boundary;

    if (!charge_committed_ &&
        !charge_rejection_emitted_ &&
        charge_seconds_ >= commit_seconds) {
        charge_rejection_emitted_ = true;
        if (charge_cooldown_seconds_ > 0.0F) {
            reject(
                ColossalWeaponRejection::ChargeOnCooldown,
                ColossalWeaponEventType::ChargeRejected);
        } else if (
            !std::isfinite(context.available_val_energy) ||
            context.available_val_energy <
                definition_.charge_energy_cost) {
            reject(
                ColossalWeaponRejection::InsufficientEnergy,
                ColossalWeaponEventType::ChargeRejected);
        } else {
            charge_committed_ = true;
            last_rejection_ =
                ColossalWeaponRejection::None;
            charge_cooldown_seconds_ =
                definition_.charge_cooldown_seconds;
            ColossalWeaponEvent event {};
            event.type =
                ColossalWeaponEventType::ChargeResourceCommit;
            event.state = state_;
            event.attack =
                ColossalAttackKind::ChargedExecution;
            event.primary_value =
                definition_.charge_energy_cost;
            event.secondary_value =
                definition_.charge_cooldown_seconds;
            push_event(event);
        }
    }

    if (charge_seconds_ >= maximum_seconds) {
        if (charge_committed_) {
            begin_charged_execution(context);
        } else {
            begin_normal_attack(context);
        }
    }
}

void ColossalWeaponSystem::complete_timed_state(
    const ColossalWeaponUpdateContext& context) noexcept {
    switch (state_) {
    case ColossalWeaponState::Drawing: {
        transition_to(
            ColossalWeaponState::Idle,
            0.0F);
        ColossalWeaponEvent event {};
        event.type =
            ColossalWeaponEventType::DrawCompleted;
        event.state = state_;
        push_event(event);
        return;
    }
    case ColossalWeaponState::Windup: {
        const auto* attack_definition =
            current_attack_definition();
        if (attack_definition == nullptr) {
            transition_to(
                ColossalWeaponState::Idle,
                0.0F);
            return;
        }
        transition_to(
            ColossalWeaponState::Active,
            std::max(
                attack_definition->active_seconds,
                kMinimumTimedStateSeconds));
        ColossalWeaponEvent event {};
        event.type =
            ColossalWeaponEventType::AttackBecameActive;
        event.state = state_;
        event.attack = attack_;
        event.attack_shape = attack_shape_;
        event.attack_sequence = attack_sequence_;
        event.primary_value =
            attack_definition->active_seconds;
        push_event(event);
        return;
    }
    case ColossalWeaponState::Active:
        finish_active_phase();
        return;
    case ColossalWeaponState::Recovery: {
        finish_recovery();
        if (attack_buffered_) {
            const auto finished_attack = attack_;
            const auto next_combo_step =
                finished_attack ==
                        ColossalAttackKind::RunningCleave
                    ? static_cast<std::uint8_t>(1U)
                    : static_cast<std::uint8_t>(
                          (combo_step_ + 1U) % 3U);
            attack_buffered_ = false;
            if (finished_attack ==
                ColossalAttackKind::ChargedExecution) {
                combo_step_ = 0U;
                attack_ =
                    ColossalAttackKind::None;
                charge_seconds_ = 0.0F;
                charge_committed_ = false;
                charge_rejection_emitted_ = false;
                transition_to(
                    ColossalWeaponState::Idle,
                    0.0F);
            } else {
                begin_attack(
                    combo_attack(next_combo_step),
                    context,
                    next_combo_step);
            }
        } else {
            combo_step_ = 0U;
            attack_ = ColossalAttackKind::None;
            charge_seconds_ = 0.0F;
            charge_committed_ = false;
            charge_rejection_emitted_ = false;
            contextual_vertical_ = false;
            clear_attack_override();
            transition_to(
                ColossalWeaponState::Idle,
                0.0F);
        }
        return;
    }
    case ColossalWeaponState::GuardBroken:
        transition_to(
            ColossalWeaponState::Idle,
            0.0F);
        return;
    case ColossalWeaponState::Impact: {
        const auto resume_state =
            impact_resume_state_;
        const auto resume_elapsed =
            impact_resume_elapsed_seconds_;
        const auto resume_duration =
            impact_resume_duration_seconds_;
        transition_to(
            resume_state,
            resume_duration);
        state_elapsed_seconds_ =
            std::min(
                resume_elapsed,
                resume_duration);
        return;
    }
    case ColossalWeaponState::Sheathing: {
        transition_to(
            ColossalWeaponState::Holstered,
            0.0F);
        attack_ = ColossalAttackKind::None;
        combo_step_ = 0U;
        charge_seconds_ = 0.0F;
        charge_committed_ = false;
        set_momentum(0U);
        ColossalWeaponEvent event {};
        event.type =
            ColossalWeaponEventType::SheathCompleted;
        event.state = state_;
        event.primary_value =
            auto_sheathing_ ? 1.0F : 0.0F;
        push_event(event);
        auto_sheathing_ = false;
        return;
    }
    case ColossalWeaponState::Holstered:
    case ColossalWeaponState::Idle:
    case ColossalWeaponState::Guard:
    case ColossalWeaponState::Charge:
        return;
    }
}

void ColossalWeaponSystem::update_passive_resources(
    float elapsed_seconds) noexcept {
    charge_cooldown_seconds_ =
        std::max(
            0.0F,
            charge_cooldown_seconds_ -
                elapsed_seconds);

    if (state_ != ColossalWeaponState::Guard &&
        state_ != ColossalWeaponState::GuardBroken) {
        const auto delay_consumed =
            std::min(
                stability_regeneration_delay_seconds_,
                elapsed_seconds);
        stability_regeneration_delay_seconds_ =
            std::max(
                0.0F,
                stability_regeneration_delay_seconds_ -
                    elapsed_seconds);
        const auto regeneration_seconds =
            elapsed_seconds - delay_consumed;
        if (regeneration_seconds > 0.0F &&
            stability_ < maximum_stability_) {
            set_stability(
                stability_ +
                definition_
                        .stability_regeneration_per_second *
                    regeneration_seconds);
        }
    }

    if (momentum_ == 0U) {
        momentum_idle_seconds_ = 0.0F;
        return;
    }
    if (is_offensive_state(state_)) {
        momentum_idle_seconds_ = 0.0F;
        return;
    }
    momentum_idle_seconds_ += elapsed_seconds;
    const auto delay =
        std::max(
            0.0F,
            definition_.momentum_decay_delay_seconds);
    const auto interval =
        std::max(
            definition_.momentum_decay_interval_seconds,
            kMinimumTimedStateSeconds);
    if (momentum_idle_seconds_ < delay) {
        return;
    }

    const auto elapsed_after_delay =
        momentum_idle_seconds_ - delay;
    const auto completed_intervals =
        static_cast<std::uint32_t>(
            std::floor(
                elapsed_after_delay / interval));
    const auto stacks_to_remove =
        static_cast<std::uint32_t>(1U) +
        completed_intervals;
    const auto remaining_momentum =
        stacks_to_remove >= momentum_
            ? static_cast<std::uint8_t>(0U)
            : static_cast<std::uint8_t>(
                  static_cast<std::uint32_t>(momentum_) -
                  stacks_to_remove);
    set_momentum(remaining_momentum);
    momentum_idle_seconds_ =
        remaining_momentum == 0U
            ? 0.0F
            : delay +
                  std::fmod(
                      elapsed_after_delay,
                      interval);
}

void ColossalWeaponSystem::refresh_mastery(
    const ColossalWeaponUpdateContext& context) noexcept {
    mastery_ =
        resolve_colossal_mastery_profile(
            context.player_level,
            context.strength,
            context.scenario_override,
            definition_);
    progression_damage_multiplier_ =
        std::isfinite(
            context.progression_damage_multiplier)
            ? std::max(
                  0.0F,
                  context.progression_damage_multiplier)
            : 1.0F;
    const auto next_maximum =
        definition_.maximum_stability *
        mastery_.stability_multiplier;
    if (std::isfinite(next_maximum) &&
        next_maximum > 0.0F &&
        std::abs(
            next_maximum -
            maximum_stability_) >
            std::numeric_limits<float>::epsilon()) {
        const auto ratio =
            maximum_stability_ > 0.0F
                ? stability_ /
                      maximum_stability_
                : 1.0F;
        maximum_stability_ =
            next_maximum;
        stability_ =
            std::clamp(
                maximum_stability_ * ratio,
                0.0F,
                maximum_stability_);
    }
}

void ColossalWeaponSystem::handle_input_before_timing(
    const ColossalWeaponInput& input,
    const ColossalWeaponUpdateContext& context) noexcept {
    if (context.fully_immersed) {
        if (state_ != ColossalWeaponState::Holstered &&
            state_ != ColossalWeaponState::Sheathing) {
            auto_sheathing_ = true;
            attack_buffered_ = false;
            attack_has_hit_ = false;
            charge_committed_ = false;
            charge_seconds_ = 0.0F;
            clear_attack_override();
            set_momentum(0U);
            ColossalWeaponEvent event {};
            event.type =
                ColossalWeaponEventType::AutoSheathed;
            event.state = state_;
            push_event(event);
            transition_to(
                ColossalWeaponState::Sheathing,
                definition_.sheath_seconds);
        } else if (
            state_ == ColossalWeaponState::Holstered &&
            (input.toggle_draw_pressed ||
             input.primary_pressed ||
             input.guard_pressed)) {
            reject(
                ColossalWeaponRejection::FullyImmersed);
        }
        return;
    }

    if (state_ == ColossalWeaponState::Holstered) {
        if (input.toggle_draw_pressed) {
            if (!mastery_.requirements_met) {
                reject(
                    ColossalWeaponRejection::
                        RequirementsNotMet);
            } else {
                last_rejection_ =
                    ColossalWeaponRejection::None;
                transition_to(
                    ColossalWeaponState::Drawing,
                    definition_.draw_seconds);
            }
        }
        return;
    }

    if (state_ == ColossalWeaponState::Idle) {
        if (input.toggle_draw_pressed) {
            clear_attack_override();
            transition_to(
                ColossalWeaponState::Sheathing,
                definition_.sheath_seconds);
            return;
        }
        if (input.guard_pressed ||
            input.guard_held) {
            transition_to(
                ColossalWeaponState::Guard,
                0.0F);
            ColossalWeaponEvent event {};
            event.type =
                ColossalWeaponEventType::GuardStarted;
            event.state = state_;
            push_event(event);
            return;
        }
        if (input.primary_pressed) {
            if (!mastery_.requirements_met) {
                reject(
                    ColossalWeaponRejection::
                        RequirementsNotMet);
                return;
            }
            if (attack_override_queued_) {
                press_started_sprinting_ = false;
                begin_normal_attack(context);
                return;
            }
            press_started_sprinting_ =
                context.sprinting;
            charge_seconds_ = 0.0F;
            charge_committed_ = false;
            charge_rejection_emitted_ = false;
            transition_to(
                ColossalWeaponState::Charge,
                definition_.charge_maximum_seconds);
        }
        return;
    }

    if (state_ == ColossalWeaponState::Guard) {
        if (input.toggle_draw_pressed) {
            clear_attack_override();
            stability_regeneration_delay_seconds_ =
                definition_
                    .stability_regeneration_delay_seconds;
            ColossalWeaponEvent event {};
            event.type =
                ColossalWeaponEventType::GuardEnded;
            event.state = state_;
            push_event(event);
            transition_to(
                ColossalWeaponState::Sheathing,
                definition_.sheath_seconds);
            return;
        }
        if (input.guard_released ||
            (!input.guard_held &&
             !input.guard_pressed)) {
            stability_regeneration_delay_seconds_ =
                definition_
                    .stability_regeneration_delay_seconds;
            ColossalWeaponEvent event {};
            event.type =
                ColossalWeaponEventType::GuardEnded;
            event.state = state_;
            push_event(event);
            transition_to(
                ColossalWeaponState::Idle,
                0.0F);
        }
        return;
    }

    if (state_ == ColossalWeaponState::Charge &&
        !charge_committed_) {
        if (input.cancel_pressed) {
            charge_seconds_ = 0.0F;
            transition_to(
                ColossalWeaponState::Idle,
                0.0F);
            return;
        }
        if (input.toggle_draw_pressed) {
            charge_seconds_ = 0.0F;
            transition_to(
                ColossalWeaponState::Sheathing,
                definition_.sheath_seconds);
            return;
        }
        if (input.guard_pressed) {
            charge_seconds_ = 0.0F;
            transition_to(
                ColossalWeaponState::Guard,
                0.0F);
            ColossalWeaponEvent event {};
            event.type =
                ColossalWeaponEventType::GuardStarted;
            event.state = state_;
            push_event(event);
            return;
        }
    }

    if (is_attack_locked_state() &&
        input.primary_pressed) {
        attack_buffered_ = true;
    }
}

void ColossalWeaponSystem::handle_input_after_timing(
    const ColossalWeaponInput& input,
    const ColossalWeaponUpdateContext& context) noexcept {
    if (state_ != ColossalWeaponState::Charge ||
        !input.primary_released) {
        return;
    }
    if (charge_committed_) {
        begin_charged_execution(context);
    } else {
        begin_normal_attack(context);
    }
}

auto ColossalWeaponSystem::is_attack_locked_state() const noexcept
    -> bool {
    switch (state_) {
    case ColossalWeaponState::Windup:
    case ColossalWeaponState::Active:
    case ColossalWeaponState::Recovery:
    case ColossalWeaponState::Impact:
        return true;
    case ColossalWeaponState::Holstered:
    case ColossalWeaponState::Drawing:
    case ColossalWeaponState::Idle:
    case ColossalWeaponState::Guard:
    case ColossalWeaponState::GuardBroken:
    case ColossalWeaponState::Charge:
    case ColossalWeaponState::Sheathing:
        return false;
    }
    return false;
}

auto ColossalWeaponSystem::current_movement_multiplier() const noexcept
    -> float {
    switch (state_) {
    case ColossalWeaponState::Holstered:
        return 1.0F;
    case ColossalWeaponState::Guard:
        return definition_.guard_movement_multiplier;
    case ColossalWeaponState::GuardBroken:
        return 0.0F;
    case ColossalWeaponState::Charge:
        return definition_.charge_movement_multiplier;
    case ColossalWeaponState::Drawing:
    case ColossalWeaponState::Idle:
    case ColossalWeaponState::Windup:
    case ColossalWeaponState::Active:
    case ColossalWeaponState::Recovery:
    case ColossalWeaponState::Impact:
    case ColossalWeaponState::Sheathing:
        return mastery_.movement_multiplier;
    }
    return 1.0F;
}

auto ColossalWeaponSystem::current_state_progress() const noexcept
    -> float {
    if (state_ == ColossalWeaponState::Charge) {
        return definition_.charge_maximum_seconds >
                       kMinimumTimedStateSeconds
                   ? std::clamp(
                         charge_seconds_ /
                             definition_
                                 .charge_maximum_seconds,
                         0.0F,
                         1.0F)
                   : 0.0F;
    }
    if (state_duration_seconds_ <=
        kMinimumTimedStateSeconds) {
        return 0.0F;
    }
    return std::clamp(
        state_elapsed_seconds_ /
            state_duration_seconds_,
        0.0F,
        1.0F);
}

} // namespace valcraft
