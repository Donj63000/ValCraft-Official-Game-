#include "gameplay/weapons/LeviathanKnightSynergy.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace valcraft {

namespace {

struct RuneWaveProfile {
    float damage = 0.0F;
    float radius_blocks = 0.0F;
};

struct BulwarkSweepProfile {
    float range_multiplier = 1.0F;
    float arc_multiplier = 1.0F;
    float stagger_multiplier = 1.0F;
    std::uint8_t additional_targets = 0U;
};

struct LivingFortressProfile {
    float melee_damage_reduction = 0.0F;
    float projectile_damage_reduction = 0.0F;
    float range_blocks = 0.0F;
    float half_angle_degrees = 0.0F;
    std::uint8_t maximum_allies = 0U;
};

constexpr std::array<RuneWaveProfile, kAbilityRankCount>
    kRuneWaveProfiles {{
        {3.0F, 1.50F},
        {4.0F, 1.75F},
        {5.0F, 2.00F},
    }};

constexpr std::array<float, kAbilityRankCount>
    kIronGuardStabilityMultipliers {{
        0.75F,
        0.70F,
        0.65F,
    }};

constexpr std::array<BulwarkSweepProfile, kAbilityRankCount>
    kBulwarkSweepProfiles {{
        {1.10F, 1.15F, 1.10F, 2U},
        {1.15F, 1.25F, 1.20F, 3U},
        {1.20F, 1.35F, 1.30F, 4U},
    }};

constexpr std::array<std::uint8_t, kAbilityRankCount>
    kSteelTempestAdditionalTargets {{
        2U,
        4U,
        6U,
    }};

constexpr std::array<LivingFortressProfile, kAbilityRankCount>
    kLivingFortressProfiles {{
        {0.55F, 0.65F, 3.0F, 35.0F, 2U},
        {0.625F, 0.725F, 3.5F, 45.0F, 4U},
        {0.70F, 0.80F, 4.0F, 55.0F, 6U},
    }};

constexpr std::array<
    float,
    static_cast<std::size_t>(
        LeviathanKnightSynergyKind::Count)>
    kDefaultDurations {{
        0.0F,
        8.0F,
        3.0F,
        3.0F,
        0.35F,
        8.0F,
        8.0F,
        4.0F,
    }};

[[nodiscard]] constexpr auto profile_index(
    std::uint8_t rank) noexcept -> std::size_t {
    return rank >= 1U && rank <= kAbilityRankCount
               ? static_cast<std::size_t>(rank - 1U)
               : 0U;
}

[[nodiscard]] auto default_duration(
    LeviathanKnightSynergyKind kind,
    std::uint8_t rank) noexcept -> float {
    const auto ability =
        leviathan_synergy_ability(kind);
    const auto* rank_definition =
        ability_rank_definition(ability, rank);
    if (rank_definition != nullptr &&
        std::isfinite(rank_definition->duration_seconds) &&
        rank_definition->duration_seconds > 0.0F) {
        return std::min(
            rank_definition->duration_seconds,
            kLeviathanSynergyMaximumDurationSeconds);
    }

    const auto index =
        static_cast<std::size_t>(kind);
    return index < kDefaultDurations.size()
               ? kDefaultDurations[index]
               : 0.0F;
}

[[nodiscard]] constexpr auto is_sweeping_attack(
    ColossalAttackKind attack) noexcept -> bool {
    return attack == ColossalAttackKind::FirstSweep ||
           attack == ColossalAttackKind::SecondSweep ||
           attack == ColossalAttackKind::RunningCleave;
}

[[nodiscard]] constexpr auto bounded_target_count(
    std::uint16_t target_count) noexcept -> std::uint8_t {
    return static_cast<std::uint8_t>(
        std::min<std::uint16_t>(
            target_count,
            kLeviathanSynergyMaximumTargets));
}

} // namespace

auto LeviathanKnightSynergyRuntime::slot(
    LeviathanKnightSynergyKind kind) noexcept -> EffectSlot& {
    const auto index = kind_index(kind);
    return slots_[
        index < slots_.size()
            ? index
            : 0U];
}

auto LeviathanKnightSynergyRuntime::slot(
    LeviathanKnightSynergyKind kind) const noexcept
    -> const EffectSlot& {
    const auto index = kind_index(kind);
    return slots_[
        index < slots_.size()
            ? index
            : 0U];
}

auto LeviathanKnightSynergyRuntime::eligible_rank(
    const PlayerBuildState& build,
    LeviathanKnightSynergyKind kind) const noexcept
    -> std::uint8_t {
    const auto ability =
        leviathan_synergy_ability(kind);
    const auto* definition =
        ability_definition(ability);
    if (kind == LeviathanKnightSynergyKind::None ||
        kind == LeviathanKnightSynergyKind::Count ||
        definition == nullptr ||
        definition->path != AbilityPath::Knight ||
        definition->category == AbilityCategory::Passive) {
        return 0U;
    }

    const auto rank =
        player_ability_rank(build, ability);
    if (rank == 0U ||
        !player_ability_is_equipped(build, ability)) {
        return 0U;
    }
    return rank;
}

void LeviathanKnightSynergyRuntime::push_event(
    const LeviathanSynergyEvent& requested) noexcept {
    if (event_count_ >= events_.size() ||
        next_event_id_ == 0U) {
        // Je laisse le gameplay avancer même si un consommateur visuel ne
        // draine pas sa télémétrie ; la saturation ne modifie aucun effet.
        event_overflowed_ = true;
        return;
    }

    auto event = requested;
    event.event_id = next_event_id_;
    if (next_event_id_ ==
        std::numeric_limits<std::uint64_t>::max()) {
        next_event_id_ = 0U;
    } else {
        ++next_event_id_;
    }
    events_[event_count_] = event;
    ++event_count_;
}

void LeviathanKnightSynergyRuntime::clear_slot(
    LeviathanKnightSynergyKind kind,
    LeviathanSynergyEventType event_type) noexcept {
    auto& effect = slot(kind);
    if (!effect.active) {
        return;
    }

    push_event({
        0U,
        event_type,
        kind,
        leviathan_synergy_ability(kind),
        effect.activation_sequence,
        0U,
        ColossalAttackKind::None,
        effect.remaining_seconds,
        0.0F,
        effect.rank,
        0U,
    });
    effect = {};
}

void LeviathanKnightSynergyRuntime::consume_slot(
    LeviathanKnightSynergyKind kind,
    std::uint64_t action_sequence,
    ColossalAttackKind attack) noexcept {
    auto& effect = slot(kind);
    if (!effect.active) {
        return;
    }

    const auto activation_sequence =
        effect.activation_sequence;
    const auto rank = effect.rank;
    last_consumed_sequences_[kind_index(kind)] =
        activation_sequence;
    effect = {};
    push_event({
        0U,
        LeviathanSynergyEventType::Consumed,
        kind,
        leviathan_synergy_ability(kind),
        activation_sequence,
        action_sequence,
        attack,
        0.0F,
        0.0F,
        rank,
        0U,
    });
}

void LeviathanKnightSynergyRuntime::reconcile(
    const PlayerBuildState& build) noexcept {
    for (auto raw_kind = std::uint8_t {1U};
         raw_kind <
         static_cast<std::uint8_t>(
             LeviathanKnightSynergyKind::Count);
         ++raw_kind) {
        const auto kind =
            static_cast<LeviathanKnightSynergyKind>(
                raw_kind);
        if (slot(kind).active &&
            eligible_rank(build, kind) == 0U) {
            // Je coupe immédiatement un effet si son talent a été désappris
            // ou déséquipé ; l'arme ne conserve ainsi aucun pouvoir gratuit.
            clear_slot(
                kind,
                LeviathanSynergyEventType::Deactivated);
        }
    }
}

auto LeviathanKnightSynergyRuntime::activate(
    const PlayerBuildState& build,
    const LeviathanSynergyActivationRequest& request) noexcept
    -> LeviathanSynergyActivationResult {
    LeviathanSynergyActivationResult result {};
    result.ability = request.ability;
    result.cast_sequence = request.cast_sequence;
    result.synergy =
        leviathan_synergy_kind_for_ability(
            request.ability);

    if (result.synergy ==
        LeviathanKnightSynergyKind::None) {
        result.status =
            LeviathanSynergyStatus::
                RejectedUnsupportedAbility;
        return result;
    }
    if (!request.cast_succeeded ||
        !request.effect_active) {
        result.status =
            LeviathanSynergyStatus::
                RejectedUnconfirmedCast;
        return result;
    }
    if (request.cast_sequence == 0U) {
        result.status =
            LeviathanSynergyStatus::
                RejectedInvalidSequence;
        return result;
    }
    if (!std::isfinite(request.duration_seconds) ||
        request.duration_seconds < 0.0F) {
        result.status =
            LeviathanSynergyStatus::RejectedInvalidInput;
        return result;
    }

    const auto rank =
        player_ability_rank(build, request.ability);
    if (rank == 0U) {
        result.status =
            LeviathanSynergyStatus::RejectedNotLearned;
        return result;
    }
    if (!player_ability_is_equipped(
            build,
            request.ability)) {
        result.status =
            LeviathanSynergyStatus::RejectedNotEquipped;
        return result;
    }
    if (eligible_rank(build, result.synergy) == 0U) {
        result.status =
            LeviathanSynergyStatus::
                RejectedUnsupportedAbility;
        return result;
    }

    const auto index =
        kind_index(result.synergy);
    const auto last_sequence =
        last_activation_sequences_[index];
    if (request.cast_sequence < last_sequence) {
        result.status =
            LeviathanSynergyStatus::RejectedStaleSequence;
        return result;
    }
    if (request.cast_sequence == last_sequence) {
        const auto& current = slot(result.synergy);
        if (current.active &&
            current.activation_sequence ==
                request.cast_sequence) {
            result.status =
                LeviathanSynergyStatus::Replayed;
            result.rank = current.rank;
            result.effective_duration_seconds =
                current.remaining_seconds;
        } else {
            result.status =
                LeviathanSynergyStatus::AlreadyConsumed;
            result.rank = rank;
        }
        return result;
    }

    const auto duration =
        request.duration_seconds > 0.0F
            ? std::min(
                  request.duration_seconds,
                  kLeviathanSynergyMaximumDurationSeconds)
            : default_duration(result.synergy, rank);
    if (!(duration > 0.0F) ||
        !std::isfinite(duration)) {
        result.status =
            LeviathanSynergyStatus::RejectedInvalidInput;
        return result;
    }

    const auto replacing =
        slot(result.synergy).active;
    if (replacing) {
        clear_slot(
            result.synergy,
            LeviathanSynergyEventType::Replaced);
    }

    auto& effect = slot(result.synergy);
    effect.activation_sequence =
        request.cast_sequence;
    effect.remaining_seconds = duration;
    effect.rank = rank;
    effect.active = true;
    last_activation_sequences_[index] =
        request.cast_sequence;

    result.status =
        replacing
            ? LeviathanSynergyStatus::Replaced
            : LeviathanSynergyStatus::Activated;
    result.rank = rank;
    result.effective_duration_seconds = duration;
    push_event({
        0U,
        LeviathanSynergyEventType::Activated,
        result.synergy,
        request.ability,
        request.cast_sequence,
        0U,
        ColossalAttackKind::None,
        duration,
        0.0F,
        rank,
        0U,
    });
    return result;
}

auto LeviathanKnightSynergyRuntime::deactivate(
    const LeviathanSynergyDeactivationRequest& request) noexcept
    -> LeviathanSynergyStatus {
    const auto kind =
        leviathan_synergy_kind_for_ability(
            request.ability);
    if (kind == LeviathanKnightSynergyKind::None) {
        return LeviathanSynergyStatus::
            RejectedUnsupportedAbility;
    }
    if (request.cast_sequence == 0U) {
        return LeviathanSynergyStatus::
            RejectedInvalidSequence;
    }

    const auto index = kind_index(kind);
    if (last_consumed_sequences_[index] ==
        request.cast_sequence) {
        return LeviathanSynergyStatus::AlreadyConsumed;
    }

    const auto& effect = slot(kind);
    if (!effect.active) {
        return LeviathanSynergyStatus::RejectedNotActive;
    }
    if (effect.activation_sequence !=
        request.cast_sequence) {
        return LeviathanSynergyStatus::
            RejectedMismatchedActivation;
    }
    clear_slot(
        kind,
        LeviathanSynergyEventType::Deactivated);
    return LeviathanSynergyStatus::Deactivated;
}

auto LeviathanKnightSynergyRuntime::advance(
    const PlayerBuildState& build,
    float delta_seconds) noexcept -> LeviathanSynergyStatus {
    if (!std::isfinite(delta_seconds) ||
        delta_seconds < 0.0F ||
        delta_seconds >
            kLeviathanSynergyMaximumAdvanceSeconds) {
        return LeviathanSynergyStatus::RejectedInvalidInput;
    }

    reconcile(build);
    auto expired = false;
    if (delta_seconds <= 0.0F) {
        return LeviathanSynergyStatus::NoEffect;
    }

    for (auto raw_kind = std::uint8_t {1U};
         raw_kind <
         static_cast<std::uint8_t>(
             LeviathanKnightSynergyKind::Count);
         ++raw_kind) {
        const auto kind =
            static_cast<LeviathanKnightSynergyKind>(
                raw_kind);
        auto& effect = slot(kind);
        if (!effect.active) {
            continue;
        }
        effect.remaining_seconds =
            std::max(
                0.0F,
                effect.remaining_seconds -
                    delta_seconds);
        if (effect.remaining_seconds <= 0.0F) {
            clear_slot(
                kind,
                LeviathanSynergyEventType::Expired);
            expired = true;
        }
    }

    return expired
               ? LeviathanSynergyStatus::Expired
               : LeviathanSynergyStatus::NoEffect;
}

auto LeviathanKnightSynergyRuntime::prepare_attack(
    const PlayerBuildState& build,
    const LeviathanAttackSynergyRequest& request) noexcept
    -> LeviathanAttackSynergyResult {
    LeviathanAttackSynergyResult result {};
    result.attack_sequence = request.attack_sequence;
    result.attack = request.attack;

    const auto* definition =
        colossal_attack_definition(request.attack);
    if (request.attack_sequence == 0U ||
        definition == nullptr) {
        result.status =
            LeviathanSynergyStatus::RejectedInvalidInput;
        return result;
    }

    reconcile(build);
    if (request.attack_sequence ==
        last_attack_sequence_) {
        result = last_attack_result_;
        result.status =
            LeviathanSynergyStatus::Replayed;
        return result;
    }
    if (request.attack_sequence <
        last_attack_sequence_) {
        result.status =
            LeviathanSynergyStatus::RejectedStaleSequence;
        return result;
    }

    const auto base_targets =
        request.base_maximum_targets == 0U
            ? definition->maximum_targets
            : request.base_maximum_targets;
    result.maximum_targets =
        bounded_target_count(base_targets);

    const auto& steel =
        slot(LeviathanKnightSynergyKind::SteelTempest);
    if (steel.active) {
        const auto bonus =
            kSteelTempestAdditionalTargets[
                profile_index(steel.rank)];
        result.maximum_targets =
            bounded_target_count(
                static_cast<std::uint16_t>(
                    result.maximum_targets) +
                bonus);
        result.steel_tempest_applied = true;
    }

    const auto& rune =
        slot(LeviathanKnightSynergyKind::RuneStrike);
    if (rune.active &&
        is_sweeping_attack(request.attack)) {
        const auto profile =
            kRuneWaveProfiles[
                profile_index(rune.rank)];
        result.additional_shockwave_damage =
            profile.damage;
        result.additional_shockwave_radius_blocks =
            profile.radius_blocks;
        result.rune_wave_applied = true;
        consume_slot(
            LeviathanKnightSynergyKind::RuneStrike,
            request.attack_sequence,
            request.attack);
    }

    if (result.rune_wave_applied ||
        result.steel_tempest_applied) {
        result.status =
            LeviathanSynergyStatus::Applied;
        push_event({
            0U,
            LeviathanSynergyEventType::AttackModified,
            LeviathanKnightSynergyKind::None,
            AbilityId::None,
            0U,
            request.attack_sequence,
            request.attack,
            result.additional_shockwave_damage,
            result.additional_shockwave_radius_blocks,
            0U,
            result.maximum_targets,
        });
    } else {
        result.status =
            LeviathanSynergyStatus::NoEffect;
    }

    last_attack_sequence_ =
        request.attack_sequence;
    last_attack_result_ = result;
    return result;
}

auto LeviathanKnightSynergyRuntime::modify_guard(
    const PlayerBuildState& build,
    const LeviathanGuardSynergyRequest& request) noexcept
    -> LeviathanGuardSynergyResult {
    LeviathanGuardSynergyResult result {};
    result.stability_loss =
        request.base_stability_loss;
    if (!std::isfinite(request.base_stability_loss) ||
        request.base_stability_loss < 0.0F) {
        result.status =
            LeviathanSynergyStatus::RejectedInvalidInput;
        result.stability_loss = 0.0F;
        return result;
    }

    reconcile(build);
    if (!request.guard_active ||
        !request.frontal_attack_blocked) {
        return result;
    }

    const auto& iron =
        slot(LeviathanKnightSynergyKind::IronGuard);
    if (iron.active) {
        result.stability_loss_multiplier =
            kIronGuardStabilityMultipliers[
                profile_index(iron.rank)];
        result.stability_loss *=
            result.stability_loss_multiplier;
        result.iron_guard_applied = true;
        result.status =
            LeviathanSynergyStatus::Applied;
        push_event({
            0U,
            LeviathanSynergyEventType::GuardModified,
            LeviathanKnightSynergyKind::IronGuard,
            AbilityId::KnightIronGuard,
            iron.activation_sequence,
            0U,
            ColossalAttackKind::None,
            result.stability_loss,
            result.stability_loss_multiplier,
            iron.rank,
            0U,
        });
    }

    const auto& fortress =
        slot(LeviathanKnightSynergyKind::LivingFortress);
    if (fortress.active) {
        const auto profile =
            kLivingFortressProfiles[
                profile_index(fortress.rank)];
        result.ally_damage_reduction =
            request.projectile
                ? profile.projectile_damage_reduction
                : profile.melee_damage_reduction;
        result.ally_guard_range_blocks =
            profile.range_blocks;
        result.ally_guard_half_angle_degrees =
            profile.half_angle_degrees;
        result.maximum_protected_allies =
            profile.maximum_allies;
        result.protected_ally_count =
            std::min(
                request.allies_behind,
                profile.maximum_allies);
        result.ally_protection_enabled = true;
        result.status =
            LeviathanSynergyStatus::Applied;
        push_event({
            0U,
            LeviathanSynergyEventType::GuardModified,
            LeviathanKnightSynergyKind::LivingFortress,
            AbilityId::KnightLivingFortress,
            fortress.activation_sequence,
            0U,
            ColossalAttackKind::None,
            result.ally_damage_reduction,
            result.ally_guard_range_blocks,
            fortress.rank,
            result.protected_ally_count,
        });
    }
    return result;
}

auto LeviathanKnightSynergyRuntime::complete_bulwark_charge(
    const PlayerBuildState& build,
    const LeviathanBulwarkSweepRequest& request) noexcept
    -> LeviathanBulwarkSweepResult {
    LeviathanBulwarkSweepResult result {};
    if (request.cast_sequence == 0U) {
        result.status =
            LeviathanSynergyStatus::RejectedInvalidSequence;
        return result;
    }

    reconcile(build);
    const auto kind =
        LeviathanKnightSynergyKind::BulwarkCharge;
    const auto index = kind_index(kind);
    if (last_consumed_sequences_[index] ==
        request.cast_sequence) {
        result.status =
            LeviathanSynergyStatus::AlreadyConsumed;
        return result;
    }

    const auto& effect = slot(kind);
    if (!effect.active) {
        result.status =
            LeviathanSynergyStatus::RejectedNotActive;
        return result;
    }
    if (effect.activation_sequence !=
        request.cast_sequence) {
        result.status =
            LeviathanSynergyStatus::
                RejectedMismatchedActivation;
        return result;
    }
    if (!request.charge_completed) {
        result.status =
            LeviathanSynergyStatus::NoEffect;
        return result;
    }

    const auto rank = effect.rank;
    const auto profile =
        kBulwarkSweepProfiles[
            profile_index(rank)];
    const auto* base =
        colossal_attack_definition(
            ColossalAttackKind::FirstSweep);
    const auto base_targets =
        base != nullptr
            ? base->maximum_targets
            : 0U;

    result.status =
        LeviathanSynergyStatus::Applied;
    result.forced_attack =
        ColossalAttackKind::FirstSweep;
    result.forced_shape =
        ColossalAttackShape::HorizontalArc;
    result.range_multiplier =
        profile.range_multiplier;
    result.arc_multiplier =
        profile.arc_multiplier;
    result.stagger_multiplier =
        profile.stagger_multiplier;
    result.maximum_targets =
        bounded_target_count(
            static_cast<std::uint16_t>(
                base_targets) +
            profile.additional_targets);
    result.massive_sweep_requested = true;
    consume_slot(
        kind,
        request.cast_sequence,
        result.forced_attack);
    return result;
}

auto LeviathanKnightSynergyRuntime::consume_perfect_riposte(
    const PlayerBuildState& build,
    const LeviathanPerfectRiposteRequest& request) noexcept
    -> LeviathanPerfectRiposteResult {
    LeviathanPerfectRiposteResult result {};
    if (request.cast_sequence == 0U) {
        result.status =
            LeviathanSynergyStatus::RejectedInvalidSequence;
        return result;
    }

    reconcile(build);
    const auto kind =
        LeviathanKnightSynergyKind::PerfectRiposte;
    const auto index = kind_index(kind);
    if (last_consumed_sequences_[index] ==
        request.cast_sequence) {
        result.status =
            LeviathanSynergyStatus::AlreadyConsumed;
        return result;
    }

    const auto& effect = slot(kind);
    if (!effect.active) {
        result.status =
            LeviathanSynergyStatus::RejectedNotActive;
        return result;
    }
    if (effect.activation_sequence !=
        request.cast_sequence) {
        result.status =
            LeviathanSynergyStatus::
                RejectedMismatchedActivation;
        return result;
    }
    if (!request.counter_triggered ||
        !request.perfect_guard_confirmed) {
        result.status =
            LeviathanSynergyStatus::NoEffect;
        return result;
    }

    result.status =
        LeviathanSynergyStatus::Applied;
    result.forced_attack =
        ColossalAttackKind::SecondSweep;
    result.forced_shape =
        ColossalAttackShape::ReverseHorizontalArc;
    result.second_combo_attack_requested = true;
    consume_slot(
        kind,
        request.cast_sequence,
        result.forced_attack);
    return result;
}

auto LeviathanKnightSynergyRuntime::prepare_titan_impact(
    const PlayerBuildState& build,
    const LeviathanTitanImpactRequest& request) noexcept
    -> LeviathanTitanImpactResult {
    LeviathanTitanImpactResult result {};
    if (request.cast_sequence == 0U) {
        result.status =
            LeviathanSynergyStatus::RejectedInvalidSequence;
        return result;
    }

    reconcile(build);
    const auto kind =
        LeviathanKnightSynergyKind::TitanJudgment;
    const auto index = kind_index(kind);
    if (last_consumed_sequences_[index] ==
        request.cast_sequence) {
        result.status =
            LeviathanSynergyStatus::AlreadyConsumed;
        return result;
    }

    const auto& effect = slot(kind);
    if (!effect.active) {
        result.status =
            LeviathanSynergyStatus::RejectedNotActive;
        return result;
    }
    if (effect.activation_sequence !=
        request.cast_sequence) {
        result.status =
            LeviathanSynergyStatus::
                RejectedMismatchedActivation;
        return result;
    }
    if (!request.impact_confirmed) {
        result.status =
            LeviathanSynergyStatus::NoEffect;
        return result;
    }

    // Je fournis un variant d'impact, jamais un bonus caché : le calcul de
    // dégâts reste strictement identique et indépendant de l'animation.
    result.status =
        LeviathanSynergyStatus::Applied;
    result.variant =
        LeviathanImpactVariant::TitanBlade;
    result.damage_multiplier = 1.0F;
    result.stagger_multiplier = 1.0F;
    result.colossal_blade_kinematics_requested = true;
    consume_slot(
        kind,
        request.cast_sequence,
        ColossalAttackKind::Earthbreaker);
    return result;
}

auto LeviathanKnightSynergyRuntime::view() const noexcept
    -> LeviathanKnightSynergyView {
    LeviathanKnightSynergyView result {};
    for (auto index = std::size_t {0U};
         index < slots_.size();
         ++index) {
        result.active[index] =
            slots_[index].active;
        result.activation_sequences[index] =
            slots_[index].activation_sequence;
        result.remaining_seconds[index] =
            slots_[index].remaining_seconds;
        result.ranks[index] =
            slots_[index].rank;
    }
    result.event_overflowed =
        event_overflowed_;
    return result;
}

auto LeviathanKnightSynergyRuntime::peek_events() const noexcept
    -> std::span<const LeviathanSynergyEvent> {
    return {
        events_.data(),
        event_count_,
    };
}

auto LeviathanKnightSynergyRuntime::drain_events(
    std::span<LeviathanSynergyEvent> destination) noexcept
    -> std::size_t {
    const auto drained =
        std::min(destination.size(), event_count_);
    std::copy_n(
        events_.begin(),
        drained,
        destination.begin());
    std::move(
        events_.begin() +
            static_cast<std::ptrdiff_t>(drained),
        events_.begin() +
            static_cast<std::ptrdiff_t>(event_count_),
        events_.begin());
    event_count_ -= drained;
    std::fill(
        events_.begin() +
            static_cast<std::ptrdiff_t>(event_count_),
        events_.end(),
        LeviathanSynergyEvent {});
    return drained;
}

void LeviathanKnightSynergyRuntime::reset() noexcept {
    slots_ = {};
    last_activation_sequences_ = {};
    last_consumed_sequences_ = {};
    events_ = {};
    event_count_ = 0U;
    next_event_id_ = 1U;
    last_attack_sequence_ = 0U;
    last_attack_result_ = {};
    event_overflowed_ = false;
}

} // namespace valcraft
