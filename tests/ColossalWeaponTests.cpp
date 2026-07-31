#include "gameplay/weapons/ColossalWeaponCombat.h"
#include "gameplay/weapons/ColossalWeaponDefinition.h"
#include "gameplay/weapons/ColossalWeaponSystem.h"
#include "gameplay/weapons/LeviathanKnightSynergy.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <span>

namespace valcraft {
namespace {

[[nodiscard]] auto normal_context() noexcept
    -> ColossalWeaponUpdateContext {
    ColossalWeaponUpdateContext context {};
    context.player_level = 35U;
    context.strength = 6U;
    context.available_val_energy = 100.0F;
    return context;
}

void draw_weapon(
    ColossalWeaponSystem& system,
    const ColossalWeaponUpdateContext& context) {
    ColossalWeaponInput draw {};
    draw.toggle_draw_pressed = true;
    static_cast<void>(
        system.update(draw, context, 0.0F));
    static_cast<void>(
        system.update(
            {},
            context,
            system.snapshot().state_duration_seconds));
    REQUIRE(
        system.snapshot().state ==
        ColossalWeaponState::Idle);
}

void start_tap_attack(
    ColossalWeaponSystem& system,
    const ColossalWeaponUpdateContext& context) {
    ColossalWeaponInput press {};
    press.primary_pressed = true;
    press.primary_held = true;
    static_cast<void>(
        system.update(press, context, 0.0F));
    REQUIRE(
        system.snapshot().state ==
        ColossalWeaponState::Charge);

    ColossalWeaponInput release {};
    release.primary_released = true;
    static_cast<void>(
        system.update(release, context, 0.0F));
    REQUIRE(
        system.snapshot().state ==
        ColossalWeaponState::Windup);
}

void advance_to_active(
    ColossalWeaponSystem& system,
    const ColossalWeaponUpdateContext& context) {
    static_cast<void>(
        system.update(
            {},
            context,
            system.snapshot().state_duration_seconds));
    REQUIRE(
        system.snapshot().state ==
        ColossalWeaponState::Active);
}

[[nodiscard]] auto contains_event(
    std::span<const ColossalWeaponEvent> events,
    ColossalWeaponEventType expected) noexcept -> bool {
    return std::any_of(
        events.begin(),
        events.end(),
        [expected](const ColossalWeaponEvent& event) {
            return event.type == expected;
        });
}

TEST_CASE(
    "les définitions colossales conservent exactement les valeurs de conception") {
    const auto* first =
        colossal_attack_definition(
            ColossalAttackKind::FirstSweep);
    const auto* second =
        colossal_attack_definition(
            ColossalAttackKind::SecondSweep);
    const auto* third =
        colossal_attack_definition(
            ColossalAttackKind::Earthbreaker);
    const auto* charged =
        colossal_attack_definition(
            ColossalAttackKind::ChargedExecution);
    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);
    REQUIRE(third != nullptr);
    REQUIRE(charged != nullptr);

    CHECK(first->base_damage == doctest::Approx(14.0F));
    CHECK(first->range_blocks == doctest::Approx(3.25F));
    CHECK(first->arc_degrees == doctest::Approx(150.0F));
    CHECK(first->maximum_targets == 6U);
    CHECK(second->base_damage == doctest::Approx(16.0F));
    CHECK(second->windup_seconds == doctest::Approx(0.24F));
    CHECK(third->base_damage == doctest::Approx(22.0F));
    CHECK(third->shockwave_damage == doctest::Approx(6.0F));
    CHECK(third->shockwave_radius_blocks == doctest::Approx(2.5F));
    CHECK(charged->base_damage == doctest::Approx(32.0F));
    CHECK(charged->shockwave_damage == doctest::Approx(10.0F));
    CHECK(charged->shockwave_radius_blocks == doctest::Approx(4.0F));
    CHECK(charged->stagger_power == doctest::Approx(100.0F));
    CHECK(charged->destroys_fragile_cells);
    CHECK(kLeviathanSpineDefinition.charge_energy_cost ==
          doctest::Approx(35.0F));
    CHECK(kLeviathanSpineDefinition.charge_cooldown_seconds ==
          doctest::Approx(10.0F));
    CHECK(kLeviathanSpineDefinition.maximum_fragile_cells == 12U);
}

TEST_CASE(
    "le niveau la force et le scénario produisent un profil de maîtrise borné") {
    const auto locked =
        resolve_colossal_mastery_profile(
            34U,
            10U,
            false);
    CHECK_FALSE(locked.requirements_met);

    const auto weak =
        resolve_colossal_mastery_profile(
            35U,
            4U,
            false);
    CHECK(weak.requirements_met);
    CHECK(weak.windup_multiplier == doctest::Approx(1.12F));
    CHECK(weak.movement_multiplier == doctest::Approx(0.82F));
    CHECK(weak.strength_damage_multiplier ==
          doctest::Approx(1.12F));

    const auto mastered =
        resolve_colossal_mastery_profile(
            35U,
            20U,
            false);
    CHECK(mastered.requirements_met);
    CHECK(mastered.strength_damage_multiplier ==
          doctest::Approx(1.30F));
    CHECK(mastered.recovery_multiplier ==
          doctest::Approx(0.80F));
    CHECK(mastered.stability_multiplier ==
          doctest::Approx(1.10F));

    const auto demonstration =
        resolve_colossal_mastery_profile(
            1U,
            0U,
            true);
    CHECK(demonstration.requirements_met);
    CHECK(demonstration.effective_level == 35U);
    CHECK(demonstration.effective_strength == 8U);
}

TEST_CASE(
    "les dégâts plafonnent la progression mais gardent les vulnérabilités exceptionnelles") {
    ColossalDamageRequest request {};
    request.attack =
        ColossalAttackKind::FirstSweep;
    request.target_weight =
        ColossalTargetWeight::Boss;
    request.progression_multiplier = 4.0F;
    request.strength = 20U;
    request.target_multiplier = 2.0F;
    request.awakening_multiplier = 2.0F;
    request.exceptional_vulnerability_multiplier = 1.5F;
    request.momentum = 3U;
    const auto result =
        resolve_colossal_damage(request);

    CHECK(result.direct_damage ==
          doctest::Approx(14.0F * 2.40F * 1.50F));
    CHECK(result.stagger_power ==
          doctest::Approx(39.0F));
    CHECK(result.knockback_multiplier ==
          doctest::Approx(0.0F));

    request.progression_multiplier = 1.0F;
    request.target_multiplier = 1.0F;
    request.awakening_multiplier = 1.0F;
    request.exceptional_vulnerability_multiplier = 1.0F;
    request.strength = 0U;
    request.momentum = 0U;
    request.corrupted_target = true;
    request.first_awakening_active = true;
    CHECK(
        resolve_colossal_damage(request).direct_damage ==
        doctest::Approx(14.0F * 1.15F));
}

TEST_CASE(
    "la garde distingue face projectile parade parfaite et attaque imblocable") {
    ColossalGuardRequest request {};
    request.raw_damage = 20.0F;
    request.attack_coefficient = 1.0F;
    request.frontal_alignment = 1.0F;
    request.current_stability = 100.0F;
    request.guard_elapsed_seconds = 0.30F;
    request.guard_active = true;
    auto result =
        resolve_colossal_guard(request);
    CHECK(result.blocked);
    CHECK_FALSE(result.perfect);
    CHECK(result.resulting_damage == doctest::Approx(6.0F));
    CHECK(result.stability_after == doctest::Approx(80.0F));

    request.attack_kind =
        ColossalIncomingAttackKind::Projectile;
    result = resolve_colossal_guard(request);
    CHECK(result.resulting_damage == doctest::Approx(4.0F));

    request.attack_kind =
        ColossalIncomingAttackKind::Melee;
    request.guard_elapsed_seconds = 0.16F;
    result = resolve_colossal_guard(request);
    CHECK(result.perfect);
    CHECK(result.resulting_damage == doctest::Approx(0.0F));
    CHECK(result.stability_lost == doctest::Approx(2.0F));
    CHECK(result.attacker_stagger == doctest::Approx(30.0F));

    request.frontal_alignment = 0.49F;
    result = resolve_colossal_guard(request);
    CHECK_FALSE(result.blocked);
    CHECK(result.resulting_damage == doctest::Approx(20.0F));

    request.frontal_alignment = 1.0F;
    request.attack_kind =
        ColossalIncomingAttackKind::GroundHazard;
    result = resolve_colossal_guard(request);
    CHECK_FALSE(result.blocked);
}

TEST_CASE(
    "le dégainement respecte les prérequis et le scénario les remplace proprement") {
    ColossalWeaponSystem system {};
    auto context = normal_context();
    context.player_level = 34U;
    ColossalWeaponInput draw {};
    draw.toggle_draw_pressed = true;
    const auto rejected =
        system.update(draw, context, 0.0F);
    CHECK(
        system.snapshot().state ==
        ColossalWeaponState::Holstered);
    CHECK(
        system.snapshot().last_rejection ==
        ColossalWeaponRejection::RequirementsNotMet);
    CHECK(
        contains_event(
            rejected,
            ColossalWeaponEventType::ActionRejected));

    context.player_level = 1U;
    context.strength = 0U;
    context.scenario_override = true;
    static_cast<void>(
        system.update(draw, context, 0.0F));
    CHECK(
        system.snapshot().state ==
        ColossalWeaponState::Drawing);
    static_cast<void>(
        system.update({}, context, 0.75F));
    CHECK(
        system.snapshot().state ==
        ColossalWeaponState::Idle);
    CHECK(system.snapshot().movement_multiplier ==
          doctest::Approx(0.90F));
    CHECK(system.snapshot().maximum_stability ==
          doctest::Approx(110.0F));
}

TEST_CASE(
    "un coup réussi alimente le momentum et le buffer enchaîne le second coup") {
    ColossalWeaponSystem system {};
    const auto context = normal_context();
    draw_weapon(system, context);
    start_tap_attack(system, context);
    CHECK(
        system.snapshot().attack ==
        ColossalAttackKind::FirstSweep);
    CHECK(
        system.snapshot().state_duration_seconds ==
        doctest::Approx(0.36F));
    advance_to_active(system, context);

    ColossalAttackResolutionReport hit {};
    hit.newly_hit_targets = 3U;
    hit.heaviest_target =
        ColossalTargetWeight::Heavy;
    const auto impact_events =
        system.notify_attack_resolution(hit);
    CHECK(system.snapshot().momentum == 1U);
    CHECK(
        system.snapshot().state ==
        ColossalWeaponState::Impact);
    CHECK(
        contains_event(
            impact_events,
            ColossalWeaponEventType::AttackImpact));

    static_cast<void>(
        system.update(
            {},
            context,
            system.snapshot().state_duration_seconds));
    REQUIRE(
        system.snapshot().state ==
        ColossalWeaponState::Active);
    static_cast<void>(
        system.update(
            {},
            context,
            system.snapshot().state_duration_seconds));
    REQUIRE(
        system.snapshot().state ==
        ColossalWeaponState::Recovery);

    ColossalWeaponInput buffer {};
    buffer.primary_pressed = true;
    static_cast<void>(
        system.update(buffer, context, 0.0F));
    CHECK(system.snapshot().attack_buffered);
    static_cast<void>(
        system.update(
            {},
            context,
            system.snapshot().state_duration_seconds));
    CHECK(
        system.snapshot().state ==
        ColossalWeaponState::Windup);
    CHECK(
        system.snapshot().attack ==
        ColossalAttackKind::SecondSweep);
    CHECK(system.snapshot().state_duration_seconds ==
          doctest::Approx(0.24F * 0.93F));
}

TEST_CASE(
    "un balayage raté retire tout le momentum puis la décroissance agit hors combat") {
    ColossalWeaponSystem system {};
    const auto context = normal_context();
    draw_weapon(system, context);
    start_tap_attack(system, context);
    advance_to_active(system, context);

    ColossalAttackResolutionReport hit {};
    hit.newly_hit_targets = 1U;
    static_cast<void>(
        system.notify_attack_resolution(hit));
    static_cast<void>(
        system.update(
            {},
            context,
            system.snapshot().state_duration_seconds));
    static_cast<void>(
        system.update(
            {},
            context,
            system.snapshot().state_duration_seconds));
    static_cast<void>(
        system.update(
            {},
            context,
            system.snapshot().state_duration_seconds));
    REQUIRE(
        system.snapshot().state ==
        ColossalWeaponState::Idle);
    REQUIRE(system.snapshot().momentum == 1U);

    start_tap_attack(system, context);
    advance_to_active(system, context);
    const auto miss_events =
        system.update(
            {},
            context,
            system.snapshot().state_duration_seconds);
    CHECK(
        system.snapshot().state ==
        ColossalWeaponState::Recovery);
    CHECK(system.snapshot().momentum == 0U);
    CHECK(
        contains_event(
            miss_events,
            ColossalWeaponEventType::AttackMissed));

    static_cast<void>(
        system.update(
            {},
            context,
            system.snapshot().state_duration_seconds));
    start_tap_attack(system, context);
    advance_to_active(system, context);
    static_cast<void>(
        system.notify_attack_resolution(hit));
    static_cast<void>(
        system.update(
            {},
            context,
            system.snapshot().state_duration_seconds));
    static_cast<void>(
        system.update(
            {},
            context,
            system.snapshot().state_duration_seconds));
    static_cast<void>(
        system.update(
            {},
            context,
            system.snapshot().state_duration_seconds));
    REQUIRE(system.snapshot().momentum == 1U);
    static_cast<void>(
        system.update({}, context, 1.99F));
    CHECK(system.snapshot().momentum == 1U);
    static_cast<void>(
        system.update({}, context, 0.01F));
    CHECK(system.snapshot().momentum == 0U);
}

TEST_CASE(
    "la charge engage une seule dépense puis ignore l'annulation tardive") {
    ColossalWeaponSystem system {};
    const auto context = normal_context();
    draw_weapon(system, context);

    ColossalWeaponInput press {};
    press.primary_pressed = true;
    press.primary_held = true;
    static_cast<void>(
        system.update(press, context, 0.0F));
    static_cast<void>(
        system.update(
            ColossalWeaponInput {
                .primary_held = true,
            },
            context,
            1.19F));
    CHECK_FALSE(system.snapshot().charge_committed);
    const auto commit_events =
        system.update(
            ColossalWeaponInput {
                .primary_held = true,
            },
            context,
            0.01F);
    CHECK(system.snapshot().charge_committed);
    CHECK(
        contains_event(
            commit_events,
            ColossalWeaponEventType::ChargeResourceCommit));
    const auto commit = std::find_if(
        commit_events.begin(),
        commit_events.end(),
        [](const ColossalWeaponEvent& event) {
            return event.type ==
                   ColossalWeaponEventType::ChargeResourceCommit;
        });
    REQUIRE(commit != commit_events.end());
    CHECK(commit->primary_value == doctest::Approx(35.0F));
    CHECK(commit->secondary_value == doctest::Approx(10.0F));

    ColossalWeaponInput cancel {};
    cancel.cancel_pressed = true;
    static_cast<void>(
        system.update(cancel, context, 0.0F));
    CHECK(
        system.snapshot().state ==
        ColossalWeaponState::Charge);

    ColossalWeaponInput release {};
    release.primary_released = true;
    static_cast<void>(
        system.update(release, context, 0.0F));
    CHECK(
        system.snapshot().state ==
        ColossalWeaponState::Active);
    CHECK(
        system.snapshot().attack ==
        ColossalAttackKind::ChargedExecution);
    CHECK(system.snapshot().charge_cooldown_seconds ==
          doctest::Approx(10.0F));
}

TEST_CASE(
    "une charge sans énergie retombe sur le combo sans consommer ni démarrer le cooldown") {
    ColossalWeaponSystem system {};
    auto context = normal_context();
    context.available_val_energy = 34.99F;
    draw_weapon(system, context);
    ColossalWeaponInput press {};
    press.primary_pressed = true;
    press.primary_held = true;
    static_cast<void>(
        system.update(press, context, 0.0F));
    const auto rejected =
        system.update(
            ColossalWeaponInput {
                .primary_held = true,
            },
            context,
            1.20F);
    CHECK_FALSE(system.snapshot().charge_committed);
    CHECK(
        contains_event(
            rejected,
            ColossalWeaponEventType::ChargeRejected));
    CHECK(
        system.snapshot().last_rejection ==
        ColossalWeaponRejection::InsufficientEnergy);

    ColossalWeaponInput release {};
    release.primary_released = true;
    static_cast<void>(
        system.update(release, context, 0.0F));
    CHECK(
        system.snapshot().state ==
        ColossalWeaponState::Windup);
    CHECK(
        system.snapshot().attack ==
        ColossalAttackKind::FirstSweep);
    CHECK(system.snapshot().charge_cooldown_seconds ==
          doctest::Approx(0.0F));
}

TEST_CASE(
    "la garde parfaite donne du momentum et la rupture verrouille réellement le joueur") {
    ColossalWeaponSystem system {};
    const auto context = normal_context();
    draw_weapon(system, context);

    ColossalWeaponInput guard {};
    guard.guard_pressed = true;
    guard.guard_held = true;
    static_cast<void>(
        system.update(guard, context, 0.10F));
    REQUIRE(
        system.snapshot().state ==
        ColossalWeaponState::Guard);
    ColossalGuardRequest incoming {};
    incoming.raw_damage = 20.0F;
    incoming.frontal_alignment = 1.0F;
    const auto perfect =
        system.intercept_incoming_attack(incoming);
    CHECK(perfect.perfect);
    CHECK(system.snapshot().momentum == 1U);
    CHECK(system.snapshot().stability ==
          doctest::Approx(98.0F));

    static_cast<void>(
        system.update(
            ColossalWeaponInput {
                .guard_held = true,
            },
            context,
            0.20F));
    incoming.raw_damage = 100.0F;
    incoming.attacker_weight =
        ColossalTargetWeight::Boss;
    const auto broken =
        system.intercept_incoming_attack(incoming);
    CHECK(broken.guard_broken);
    CHECK(
        system.snapshot().state ==
        ColossalWeaponState::GuardBroken);
    CHECK(system.snapshot().momentum == 0U);
    CHECK(system.snapshot().movement_multiplier ==
          doctest::Approx(0.0F));

    static_cast<void>(
        system.update({}, context, 1.19F));
    CHECK(
        system.snapshot().state ==
        ColossalWeaponState::GuardBroken);
    static_cast<void>(
        system.update({}, context, 0.01F));
    CHECK(
        system.snapshot().state ==
        ColossalWeaponState::Idle);
}

TEST_CASE(
    "la stabilité attend une seconde complète avant de se régénérer") {
    ColossalWeaponSystem system {};
    const auto context = normal_context();
    draw_weapon(system, context);
    ColossalWeaponInput guard {};
    guard.guard_pressed = true;
    guard.guard_held = true;
    static_cast<void>(
        system.update(guard, context, 0.30F));

    ColossalGuardRequest incoming {};
    incoming.raw_damage = 20.0F;
    incoming.frontal_alignment = 1.0F;
    const auto blocked =
        system.intercept_incoming_attack(incoming);
    REQUIRE(blocked.blocked);
    REQUIRE_FALSE(blocked.perfect);
    const auto stability_after =
        system.snapshot().stability;

    ColossalWeaponInput release {};
    release.guard_released = true;
    static_cast<void>(
        system.update(release, context, 0.0F));
    static_cast<void>(
        system.update({}, context, 0.99F));
    CHECK(system.snapshot().stability ==
          doctest::Approx(stability_after));
    static_cast<void>(
        system.update({}, context, 0.01F));
    CHECK(system.snapshot().stability ==
          doctest::Approx(stability_after));
    static_cast<void>(
        system.update({}, context, 0.50F));
    CHECK(system.snapshot().stability ==
          doctest::Approx(stability_after + 14.0F));
}

TEST_CASE(
    "la course demande une avance collisionnée et le tunnel force un arc vertical") {
    ColossalWeaponSystem system {};
    auto context = normal_context();
    context.sprinting = true;
    context.narrow_tunnel = true;
    draw_weapon(system, context);

    ColossalWeaponInput press {};
    press.primary_pressed = true;
    press.primary_held = true;
    static_cast<void>(
        system.update(press, context, 0.0F));
    ColossalWeaponInput release {};
    release.primary_released = true;
    const auto events =
        system.update(release, context, 0.0F);
    CHECK(
        system.snapshot().attack ==
        ColossalAttackKind::RunningCleave);
    CHECK(
        contains_event(
            events,
            ColossalWeaponEventType::RunningAdvanceRequested));

    static_cast<void>(
        system.update(
            {},
            context,
            system.snapshot().state_duration_seconds));
    static_cast<void>(
        system.update(
            {},
            context,
            system.snapshot().state_duration_seconds));
    static_cast<void>(
        system.update(
            {},
            context,
            system.snapshot().state_duration_seconds));
    context.sprinting = false;
    start_tap_attack(system, context);
    CHECK(
        system.snapshot().attack_shape ==
        ColossalAttackShape::VerticalArc);
    CHECK(system.snapshot().contextual_vertical);
}

TEST_CASE(
    "une immersion complète annule l'action et range automatiquement l'arme") {
    ColossalWeaponSystem system {};
    auto context = normal_context();
    draw_weapon(system, context);
    start_tap_attack(system, context);
    context.fully_immersed = true;
    const auto events =
        system.update({}, context, 0.0F);
    CHECK(
        system.snapshot().state ==
        ColossalWeaponState::Sheathing);
    CHECK(
        contains_event(
            events,
            ColossalWeaponEventType::AutoSheathed));
    static_cast<void>(
        system.update(
            {},
            context,
            system.snapshot().state_duration_seconds));
    CHECK(
        system.snapshot().state ==
        ColossalWeaponState::Holstered);
    CHECK(system.snapshot().momentum == 0U);
}

TEST_CASE(
    "les entrées temporelles invalides sont refusées sans corrompre l'état") {
    ColossalWeaponSystem system {};
    const auto context = normal_context();
    const auto events =
        system.update({}, context, -1.0F);
    CHECK(
        system.snapshot().state ==
        ColossalWeaponState::Holstered);
    CHECK(
        system.snapshot().last_rejection ==
        ColossalWeaponRejection::InvalidInput);
    CHECK(
        contains_event(
            events,
            ColossalWeaponEventType::ActionRejected));
}

TEST_CASE(
    "la riposte parfaite arme une seule seconde frappe depuis la garde") {
    ColossalWeaponSystem system {};
    const auto context = normal_context();
    draw_weapon(system, context);

    static_cast<void>(
        system.update(
            ColossalWeaponInput {
                .guard_pressed = true,
                .guard_held = true,
            },
            context,
            0.0F));
    REQUIRE(
        system.snapshot().state ==
        ColossalWeaponState::Guard);

    LeviathanPerfectRiposteResult riposte {};
    riposte.status = LeviathanSynergyStatus::Applied;
    riposte.forced_attack =
        ColossalAttackKind::SecondSweep;
    riposte.forced_shape =
        ColossalAttackShape::ReverseHorizontalArc;
    riposte.second_combo_attack_requested = true;
    const auto queued =
        system.queue_next_attack_override(
            riposte,
            41U);
    REQUIRE(queued.accepted());
    CHECK(
        queued.status ==
        ColossalAttackOverrideStatus::Queued);
    CHECK(system.attack_override_view().queued);
    CHECK(
        system.attack_override_view()
            .request.request_sequence == 41U);

    static_cast<void>(
        system.update(
            ColossalWeaponInput {
                .guard_released = true,
            },
            context,
            0.0F));
    REQUIRE(
        system.snapshot().state ==
        ColossalWeaponState::Idle);
    static_cast<void>(
        system.update(
            ColossalWeaponInput {
                .primary_pressed = true,
            },
            context,
            0.0F));

    REQUIRE(
        system.snapshot().state ==
        ColossalWeaponState::Windup);
    CHECK(
        system.snapshot().attack ==
        ColossalAttackKind::SecondSweep);
    CHECK(
        system.snapshot().attack_shape ==
        ColossalAttackShape::ReverseHorizontalArc);
    CHECK(system.snapshot().combo_step == 1U);
    CHECK_FALSE(system.attack_override_view().queued);
    CHECK(system.attack_override_view().active);
    const auto* effective =
        system.current_attack_definition();
    REQUIRE(effective != nullptr);
    CHECK(
        effective->kind ==
        ColossalAttackKind::SecondSweep);
    CHECK(effective->maximum_targets == 6U);

    static_cast<void>(
        system.update(
            {},
            context,
            system.snapshot().state_duration_seconds));
    static_cast<void>(
        system.update(
            {},
            context,
            system.snapshot().state_duration_seconds));
    static_cast<void>(
        system.update(
            {},
            context,
            system.snapshot().state_duration_seconds));
    REQUIRE(
        system.snapshot().state ==
        ColossalWeaponState::Idle);
    CHECK_FALSE(system.attack_override_view().active);
    CHECK(
        system.queue_next_attack_override(
                  riposte,
                  41U)
            .status ==
        ColossalAttackOverrideStatus::RejectedReplay);
}

TEST_CASE(
    "le balayage du rempart borne tous ses multiplicateurs avant consommation") {
    ColossalWeaponSystem system {};
    const auto context = normal_context();
    draw_weapon(system, context);

    LeviathanBulwarkSweepResult bulwark {};
    bulwark.status = LeviathanSynergyStatus::Applied;
    bulwark.forced_attack =
        ColossalAttackKind::FirstSweep;
    bulwark.forced_shape =
        ColossalAttackShape::HorizontalArc;
    bulwark.range_multiplier = 99.0F;
    bulwark.arc_multiplier = 99.0F;
    bulwark.stagger_multiplier = 99.0F;
    bulwark.maximum_targets =
        std::numeric_limits<std::uint8_t>::max();
    bulwark.massive_sweep_requested = true;
    const auto queued =
        system.queue_next_attack_override(
            bulwark,
            7U);
    REQUIRE(queued.accepted());
    CHECK(
        queued.effective_request.range_multiplier ==
        doctest::Approx(
            kColossalAttackOverrideMaximumRangeMultiplier));
    CHECK(
        queued.effective_request.arc_multiplier ==
        doctest::Approx(
            kColossalAttackOverrideMaximumArcMultiplier));
    CHECK(
        queued.effective_request.stagger_multiplier ==
        doctest::Approx(
            kColossalAttackOverrideMaximumStaggerMultiplier));
    CHECK(
        queued.effective_request.maximum_targets ==
        kColossalAttackOverrideMaximumTargets);

    static_cast<void>(
        system.update(
            ColossalWeaponInput {
                .primary_pressed = true,
            },
            context,
            0.0F));
    REQUIRE(
        system.snapshot().state ==
        ColossalWeaponState::Windup);
    const auto* effective =
        system.current_attack_definition();
    const auto* base =
        colossal_attack_definition(
            ColossalAttackKind::FirstSweep);
    REQUIRE(effective != nullptr);
    REQUIRE(base != nullptr);
    CHECK(
        effective->range_blocks ==
        doctest::Approx(
            base->range_blocks *
            kColossalAttackOverrideMaximumRangeMultiplier));
    CHECK(
        effective->arc_degrees ==
        doctest::Approx(
            base->arc_degrees *
            kColossalAttackOverrideMaximumArcMultiplier));
    CHECK(
        effective->stagger_power ==
        doctest::Approx(
            base->stagger_power *
            kColossalAttackOverrideMaximumStaggerMultiplier));
    CHECK(
        effective->maximum_targets ==
        kColossalAttackOverrideMaximumTargets);
}

TEST_CASE(
    "l'adaptateur refuse les résultats inactifs invalides et les états offensifs") {
    ColossalWeaponSystem system {};
    const auto context = normal_context();
    draw_weapon(system, context);

    LeviathanPerfectRiposteResult inactive {};
    CHECK(
        system.queue_next_attack_override(
                  inactive,
                  1U)
            .status ==
        ColossalAttackOverrideStatus::
            RejectedInactiveSynergy);

    ColossalAttackOverrideRequest invalid {};
    invalid.request_sequence = 1U;
    invalid.forced_attack =
        ColossalAttackKind::FirstSweep;
    invalid.range_multiplier =
        std::numeric_limits<float>::quiet_NaN();
    CHECK(
        system.queue_next_attack_override(invalid)
            .status ==
        ColossalAttackOverrideStatus::
            RejectedInvalidRequest);

    start_tap_attack(system, context);
    ColossalAttackOverrideRequest busy {};
    busy.request_sequence = 2U;
    busy.forced_attack =
        ColossalAttackKind::SecondSweep;
    busy.forced_shape =
        ColossalAttackShape::ReverseHorizontalArc;
    CHECK(
        system.queue_next_attack_override(busy)
            .status ==
        ColossalAttackOverrideStatus::
            RejectedUnsafeState);
    CHECK_FALSE(system.attack_override_view().queued);
}

TEST_CASE(
    "interrompre ou réinitialiser efface l'adaptation éphémère") {
    ColossalWeaponSystem system {};
    const auto context = normal_context();
    draw_weapon(system, context);

    ColossalAttackOverrideRequest request {};
    request.request_sequence = 9U;
    request.forced_attack =
        ColossalAttackKind::FirstSweep;
    REQUIRE(
        system.queue_next_attack_override(request)
            .accepted());
    static_cast<void>(
        system.update(
            ColossalWeaponInput {
                .primary_pressed = true,
            },
            context,
            0.0F));
    REQUIRE(system.attack_override_view().active);
    system.interrupt();
    CHECK_FALSE(system.attack_override_view().queued);
    CHECK_FALSE(system.attack_override_view().active);
    CHECK(
        system.current_attack_definition() ==
        nullptr);

    system.reset();
    draw_weapon(system, context);
    CHECK(
        system.queue_next_attack_override(request)
            .accepted());
    system.reset();
    CHECK_FALSE(system.attack_override_view().queued);
    CHECK_FALSE(system.attack_override_view().active);
}

} // namespace
} // namespace valcraft
