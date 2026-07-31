#include "gameplay/scenarios/IssouArenaScenario.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace valcraft {

namespace {

auto finite_non_negative(float value) noexcept -> float {
    return std::isfinite(value)
               ? std::max(value, 0.0F)
               : 0.0F;
}

auto crossed(
    float previous,
    float current,
    float threshold) noexcept -> bool {
    return previous < threshold &&
           current >= threshold;
}

auto countdown_crossed(
    float previous,
    float current,
    float threshold) noexcept -> bool {
    return previous > threshold &&
           current <= threshold;
}

} // namespace

auto IssouArenaScenario::enter(
    const IssouArenaLayout& layout,
    std::uint32_t run_sequence) noexcept -> bool {
    if (active()) {
        return false;
    }

    const auto gore = state_.gore_mode;
    const auto awakening =
        state_.awakening_override;
    state_ = {};
    state_.phase = IssouArenaPhase::Arrival;
    state_.layout = layout;
    state_.gore_mode = gore;
    state_.awakening_override =
        std::min<std::uint8_t>(
            awakening,
            3U);
    state_.run_sequence =
        run_sequence == 0U
            ? 1U
            : run_sequence;
    state_.crowd_excitement = 0.08F;
    event_count_ = 0U;
    next_event_sequence_ = 1U;
    queue_event(
        IssouArenaEventKind::Entered,
        0.25F);
    queue_event(
        IssouArenaEventKind::Horn,
        0.70F);
    return true;
}

auto IssouArenaScenario::reset() noexcept -> bool {
    if (!active() ||
        state_.phase ==
            IssouArenaPhase::ExitRequested) {
        return false;
    }

    const auto layout = state_.layout;
    const auto gore = state_.gore_mode;
    const auto awakening =
        state_.awakening_override;
    const auto next_run =
        state_.run_sequence ==
                std::numeric_limits<
                    std::uint32_t>::max()
            ? 1U
            : state_.run_sequence + 1U;
    state_ = {};
    state_.phase = IssouArenaPhase::Arrival;
    state_.layout = layout;
    state_.gore_mode = gore;
    state_.awakening_override = awakening;
    state_.run_sequence = next_run;
    state_.crowd_excitement = 0.08F;
    event_count_ = 0U;
    queue_event(
        IssouArenaEventKind::Reset,
        0.5F);
    queue_event(
        IssouArenaEventKind::Horn,
        0.7F);
    return true;
}

auto IssouArenaScenario::request_exit() noexcept -> bool {
    if (!active() ||
        state_.phase ==
            IssouArenaPhase::ExitRequested) {
        return false;
    }
    state_.phase =
        IssouArenaPhase::ExitRequested;
    state_.phase_seconds = 0.0F;
    queue_event(
        IssouArenaEventKind::ExitRequested);
    return true;
}

auto IssouArenaScenario::skip_countdown() noexcept -> bool {
    if (state_.phase !=
            IssouArenaPhase::Countdown &&
        state_.phase !=
            IssouArenaPhase::Arrival) {
        return false;
    }
    if (state_.phase ==
        IssouArenaPhase::Arrival) {
        begin_countdown();
    }
    state_.countdown_seconds = 0.0F;
    begin_combat();
    return true;
}

auto IssouArenaScenario::set_gore_mode(
    IssouGoreMode mode) noexcept -> bool {
    state_.gore_mode = mode;
    return true;
}

auto IssouArenaScenario::set_awakening_override(
    std::uint8_t awakening) noexcept -> bool {
    if (awakening > 3U) {
        return false;
    }
    state_.awakening_override = awakening;
    return true;
}

void IssouArenaScenario::update(
    float dt) noexcept {
    const auto safe_dt =
        std::clamp(
            finite_non_negative(dt),
            0.0F,
            0.25F);
    if (safe_dt <= 0.0F ||
        !active() ||
        state_.phase ==
            IssouArenaPhase::ExitRequested) {
        return;
    }

    const auto previous_seconds =
        state_.phase_seconds;
    state_.phase_seconds += safe_dt;
    switch (state_.phase) {
    case IssouArenaPhase::Arrival:
        update_arrival(previous_seconds);
        break;
    case IssouArenaPhase::Countdown: {
        const auto previous_countdown =
            state_.countdown_seconds;
        state_.countdown_seconds =
            std::max(
                0.0F,
                state_.countdown_seconds -
                    safe_dt);
        update_countdown(
            previous_countdown);
        if (state_.countdown_seconds <=
            0.0F) {
            begin_combat();
        }
        break;
    }
    case IssouArenaPhase::Combat:
        state_.statistics.combat_seconds +=
            safe_dt;
        state_.crowd_excitement =
            std::max(
                0.16F,
                state_.crowd_excitement -
                    safe_dt * 0.012F);
        break;
    case IssouArenaPhase::Victory:
        state_.crowd_excitement =
            std::min(
                1.0F,
                state_.crowd_excitement +
                    safe_dt * 0.18F);
        break;
    case IssouArenaPhase::Defeat:
        state_.crowd_excitement =
            std::max(
                0.12F,
                state_.crowd_excitement -
                    safe_dt * 0.08F);
        break;
    case IssouArenaPhase::Inactive:
    case IssouArenaPhase::ExitRequested:
    default:
        break;
    }
}

void IssouArenaScenario::notify_combat_event(
    IssouArenaCombatEvent event,
    float value,
    std::uint8_t count) noexcept {
    if (state_.phase !=
            IssouArenaPhase::Combat &&
        event !=
            IssouArenaCombatEvent::WeaponDrawn) {
        return;
    }

    const auto safe_value =
        finite_non_negative(value);
    switch (event) {
    case IssouArenaCombatEvent::WeaponDrawn:
        queue_event(
            IssouArenaEventKind::CrowdCheer,
            0.38F);
        state_.crowd_excitement =
            std::max(
                state_.crowd_excitement,
                0.28F);
        break;
    case IssouArenaCombatEvent::AttackHit:
        state_.statistics.damage_dealt +=
            safe_value;
        state_.statistics
                .maximum_targets_hit =
            std::max(
                state_.statistics
                    .maximum_targets_hit,
                count);
        state_.crowd_excitement =
            std::min(
                1.0F,
                state_.crowd_excitement +
                    0.08F);
        queue_event(
            IssouArenaEventKind::CrowdApplause,
            0.35F);
        break;
    case IssouArenaCombatEvent::AttackMissed:
        if (state_.statistics
                .missed_attacks !=
            std::numeric_limits<
                std::uint32_t>::max()) {
            ++state_.statistics
                  .missed_attacks;
        }
        state_.crowd_excitement =
            std::max(
                0.0F,
                state_.crowd_excitement -
                    0.06F);
        queue_event(
            IssouArenaEventKind::CrowdBoo,
            0.22F);
        break;
    case IssouArenaCombatEvent::
        ComboFinisherHit:
        state_.statistics.damage_dealt +=
            safe_value;
        state_.crowd_excitement =
            std::min(
                1.0F,
                state_.crowd_excitement +
                    0.14F);
        queue_event(
            IssouArenaEventKind::CrowdCheer,
            0.58F);
        break;
    case IssouArenaCombatEvent::
        ChargedAttackHit:
        state_.statistics.damage_dealt +=
            safe_value;
        state_.crowd_excitement =
            std::min(
                1.0F,
                state_.crowd_excitement +
                    0.22F);
        queue_event(
            IssouArenaEventKind::CrowdRoar,
            0.82F);
        break;
    case IssouArenaCombatEvent::PerfectGuard:
        if (state_.statistics
                .perfect_guards !=
            std::numeric_limits<
                std::uint32_t>::max()) {
            ++state_.statistics
                  .perfect_guards;
        }
        state_.crowd_excitement =
            std::min(
                1.0F,
                state_.crowd_excitement +
                    0.12F);
        queue_event(
            IssouArenaEventKind::CrowdCheer,
            0.62F);
        break;
    case IssouArenaCombatEvent::PlayerHit:
        state_.crowd_excitement =
            std::min(
                1.0F,
                state_.crowd_excitement +
                    0.04F);
        queue_event(
            IssouArenaEventKind::CrowdBoo,
            0.28F);
        break;
    case IssouArenaCombatEvent::ArmorBroken:
        state_.crowd_excitement =
            std::min(
                1.0F,
                state_.crowd_excitement +
                    0.16F);
        queue_event(
            IssouArenaEventKind::CrowdCheer,
            0.72F);
        break;
    case IssouArenaCombatEvent::LimbSevered:
        if (state_.statistics
                .limbs_severed !=
            std::numeric_limits<
                std::uint32_t>::max()) {
            ++state_.statistics
                  .limbs_severed;
        }
        state_.crowd_excitement = 1.0F;
        queue_event(
            IssouArenaEventKind::CrowdRoar,
            1.0F);
        break;
    case IssouArenaCombatEvent::
        BossBelowQuarterHealth:
        state_.crowd_excitement =
            std::max(
                state_.crowd_excitement,
                0.84F);
        break;
    case IssouArenaCombatEvent::
        ExecutionStarted:
        queue_event(
            IssouArenaEventKind::CrowdSilence,
            1.0F);
        break;
    case IssouArenaCombatEvent::BossKilled:
    case IssouArenaCombatEvent::BossExecuted:
        state_.statistics.executed =
            event ==
            IssouArenaCombatEvent::
                BossExecuted;
        state_.phase =
            IssouArenaPhase::Victory;
        state_.phase_seconds = 0.0F;
        state_.crowd_excitement = 1.0F;
        state_.tips_visible = false;
        queue_event(
            IssouArenaEventKind::Victory,
            1.0F);
        queue_event(
            IssouArenaEventKind::CrowdRoar,
            1.0F);
        break;
    default:
        break;
    }
}

void IssouArenaScenario::notify_player_death() noexcept {
    if (state_.phase !=
        IssouArenaPhase::Combat) {
        return;
    }
    state_.phase =
        IssouArenaPhase::Defeat;
    state_.phase_seconds = 0.0F;
    state_.tips_visible = false;
    queue_event(
        IssouArenaEventKind::Defeat,
        0.65F);
}

void IssouArenaScenario::
    acknowledge_first_successful_action() noexcept {
    state_.tips_visible = false;
}

auto IssouArenaScenario::state() const noexcept
    -> const IssouArenaState& {
    return state_;
}

auto IssouArenaScenario::active() const noexcept
    -> bool {
    return state_.phase !=
        IssouArenaPhase::Inactive;
}

auto IssouArenaScenario::saving_suspended() const noexcept
    -> bool {
    return active();
}

auto IssouArenaScenario::
    permanent_rewards_allowed() const noexcept -> bool {
    return !active();
}

auto IssouArenaScenario::hud_view() const noexcept
    -> IssouArenaHudView {
    return {
        state_.phase,
        state_.countdown_seconds,
        state_.crowd_excitement,
        active() &&
            state_.phase !=
                IssouArenaPhase::ExitRequested,
        state_.phase ==
            IssouArenaPhase::Countdown,
        state_.tips_visible &&
            (state_.phase ==
                 IssouArenaPhase::Countdown ||
             state_.phase ==
                 IssouArenaPhase::Combat),
        state_.phase ==
                IssouArenaPhase::Victory ||
            state_.phase ==
                IssouArenaPhase::Defeat,
        state_.statistics,
    };
}

auto IssouArenaScenario::consume_events() noexcept
    -> std::span<const IssouArenaEvent> {
    const auto result =
        std::span<const IssouArenaEvent> {
            events_.data(),
            event_count_,
        };
    event_count_ = 0U;
    return result;
}

void IssouArenaScenario::begin_countdown() noexcept {
    state_.phase =
        IssouArenaPhase::Countdown;
    state_.phase_seconds = 0.0F;
    state_.countdown_seconds =
        kIssouArenaCountdownSeconds;
    queue_event(
        IssouArenaEventKind::WeaponTitle,
        0.55F);
    queue_event(
        IssouArenaEventKind::CountdownStarted,
        0.45F);
    queue_event(
        IssouArenaEventKind::CrowdMurmur,
        0.2F);
}

void IssouArenaScenario::begin_combat() noexcept {
    if (state_.phase ==
        IssouArenaPhase::Combat) {
        return;
    }
    state_.phase =
        IssouArenaPhase::Combat;
    state_.phase_seconds = 0.0F;
    state_.countdown_seconds = 0.0F;
    state_.chains_visible = false;
    state_.colossus_invulnerable = false;
    state_.crowd_excitement =
        std::max(
            state_.crowd_excitement,
            0.62F);
    queue_event(
        IssouArenaEventKind::ChainsBroken,
        1.0F);
    queue_event(
        IssouArenaEventKind::ColossusRoar,
        0.95F);
    queue_event(
        IssouArenaEventKind::CombatStarted,
        0.85F);
}

void IssouArenaScenario::queue_event(
    IssouArenaEventKind kind,
    float intensity) noexcept {
    if (event_count_ >=
        events_.size()) {
        return;
    }
    events_[event_count_++] = {
        kind,
        std::clamp(
            finite_non_negative(intensity),
            0.0F,
            1.0F),
        next_event_sequence_++,
    };
    if (next_event_sequence_ == 0U) {
        next_event_sequence_ = 1U;
    }
}

void IssouArenaScenario::update_arrival(
    float previous_seconds) noexcept {
    if (crossed(
            previous_seconds,
            state_.phase_seconds,
            0.35F)) {
        queue_event(
            IssouArenaEventKind::CameraCrowd,
            0.35F);
    }
    if (crossed(
            previous_seconds,
            state_.phase_seconds,
            0.90F)) {
        queue_event(
            IssouArenaEventKind::CameraColossus,
            0.65F);
    }
    if (crossed(
            previous_seconds,
            state_.phase_seconds,
            1.65F)) {
        queue_event(
            IssouArenaEventKind::CameraPlayer,
            0.45F);
    }
    if (state_.phase_seconds >= 2.0F) {
        begin_countdown();
    }
}

void IssouArenaScenario::update_countdown(
    float previous_countdown) noexcept {
    const auto current =
        state_.countdown_seconds;
    if (countdown_crossed(
            previous_countdown,
            current,
            7.0F)) {
        queue_event(
            IssouArenaEventKind::ChainStrain,
            0.42F);
    }
    if (countdown_crossed(
            previous_countdown,
            current,
            5.0F)) {
        queue_event(
            IssouArenaEventKind::MusicStarted,
            0.55F);
        state_.crowd_excitement =
            std::max(
                state_.crowd_excitement,
                0.32F);
    }
    if (countdown_crossed(
            previous_countdown,
            current,
            3.0F)) {
        queue_event(
            IssouArenaEventKind::ColossusRoar,
            0.68F);
        queue_event(
            IssouArenaEventKind::ChainCrack,
            0.58F);
    }
    if (countdown_crossed(
            previous_countdown,
            current,
            1.0F)) {
        queue_event(
            IssouArenaEventKind::BriefSilence,
            0.9F);
    }
}

} // namespace valcraft
