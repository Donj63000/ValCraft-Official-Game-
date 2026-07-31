#include "gameplay/weapons/LeviathanKnightSynergy.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace valcraft {
namespace {

[[nodiscard]] auto learned_and_equipped(
    AbilityId ability,
    std::uint8_t rank = 1U,
    std::size_t slot = 0U) noexcept -> PlayerBuildState {
    PlayerBuildState build {};
    build.ability_ranks[ability_index(ability)] =
        rank;
    build.equipped_abilities[
        std::min(
            slot,
            build.equipped_abilities.size() - 1U)] =
        ability;
    return build;
}

[[nodiscard]] auto confirmed_activation(
    AbilityId ability,
    std::uint64_t sequence,
    float duration_seconds = 0.0F) noexcept
    -> LeviathanSynergyActivationRequest {
    return {
        ability,
        sequence,
        duration_seconds,
        true,
        true,
    };
}

[[nodiscard]] constexpr auto synergy_index(
    LeviathanKnightSynergyKind kind) noexcept
    -> std::size_t {
    return static_cast<std::size_t>(kind);
}

TEST_CASE(
    "les synergies raccordent uniquement les sept talents du chevalier") {
    CHECK(
        leviathan_synergy_kind_for_ability(
            AbilityId::KnightVanguardStrike) ==
        LeviathanKnightSynergyKind::RuneStrike);
    CHECK(
        leviathan_synergy_kind_for_ability(
            AbilityId::KnightIronGuard) ==
        LeviathanKnightSynergyKind::IronGuard);
    CHECK(
        leviathan_synergy_kind_for_ability(
            AbilityId::KnightBulwarkCharge) ==
        LeviathanKnightSynergyKind::BulwarkCharge);
    CHECK(
        leviathan_synergy_kind_for_ability(
            AbilityId::KnightPerfectRiposte) ==
        LeviathanKnightSynergyKind::PerfectRiposte);
    CHECK(
        leviathan_synergy_kind_for_ability(
            AbilityId::KnightColossusFury) ==
        LeviathanKnightSynergyKind::SteelTempest);
    CHECK(
        leviathan_synergy_kind_for_ability(
            AbilityId::KnightLivingFortress) ==
        LeviathanKnightSynergyKind::LivingFortress);
    CHECK(
        leviathan_synergy_kind_for_ability(
            AbilityId::KnightTitanJudgment) ==
        LeviathanKnightSynergyKind::TitanJudgment);
    CHECK(
        leviathan_synergy_kind_for_ability(
            AbilityId::NinjaBladeDance) ==
        LeviathanKnightSynergyKind::None);

    for (auto raw = std::uint8_t {1U};
         raw <
         static_cast<std::uint8_t>(
             LeviathanKnightSynergyKind::Count);
         ++raw) {
        const auto kind =
            static_cast<LeviathanKnightSynergyKind>(raw);
        CHECK(
            leviathan_synergy_kind_for_ability(
                leviathan_synergy_ability(kind)) ==
            kind);
    }
}

TEST_CASE(
    "aucun pouvoir ne vient d'un talent absent déséquipé ou non confirmé") {
    LeviathanKnightSynergyRuntime runtime {};
    const auto ability =
        AbilityId::KnightVanguardStrike;

    auto request =
        confirmed_activation(ability, 1U);
    const auto absent =
        runtime.activate(PlayerBuildState {}, request);
    CHECK(
        absent.status ==
        LeviathanSynergyStatus::RejectedNotLearned);

    auto learned =
        learned_and_equipped(ability);
    learned.equipped_abilities.fill(AbilityId::None);
    const auto unequipped =
        runtime.activate(learned, request);
    CHECK(
        unequipped.status ==
        LeviathanSynergyStatus::RejectedNotEquipped);

    auto eligible =
        learned_and_equipped(ability);
    request.cast_succeeded = false;
    const auto failed =
        runtime.activate(eligible, request);
    CHECK(
        failed.status ==
        LeviathanSynergyStatus::
            RejectedUnconfirmedCast);

    request.cast_succeeded = true;
    request.effect_active = false;
    const auto inactive =
        runtime.activate(eligible, request);
    CHECK(
        inactive.status ==
        LeviathanSynergyStatus::
            RejectedUnconfirmedCast);

    const auto foreign =
        runtime.activate(
            learned_and_equipped(
                AbilityId::NinjaBladeDance),
            confirmed_activation(
                AbilityId::NinjaBladeDance,
                1U));
    CHECK(
        foreign.status ==
        LeviathanSynergyStatus::
            RejectedUnsupportedAbility);
    CHECK(runtime.peek_events().empty());
}

TEST_CASE(
    "la frappe runique attend le prochain balayage puis se consomme une fois") {
    LeviathanKnightSynergyRuntime runtime {};
    const auto build =
        learned_and_equipped(
            AbilityId::KnightVanguardStrike,
            2U);
    const auto activation =
        runtime.activate(
            build,
            confirmed_activation(
                AbilityId::KnightVanguardStrike,
                10U));
    REQUIRE(activation.accepted());
    CHECK(
        activation.effective_duration_seconds ==
        doctest::Approx(8.0F));

    const auto vertical =
        runtime.prepare_attack(
            build,
            {
                1U,
                ColossalAttackKind::Earthbreaker,
                0U,
            });
    CHECK(
        vertical.status ==
        LeviathanSynergyStatus::NoEffect);
    CHECK_FALSE(vertical.rune_wave_applied);
    CHECK(
        runtime.view().active[
            synergy_index(
                LeviathanKnightSynergyKind::RuneStrike)]);

    const auto sweep =
        runtime.prepare_attack(
            build,
            {
                2U,
                ColossalAttackKind::FirstSweep,
                0U,
            });
    CHECK(
        sweep.status ==
        LeviathanSynergyStatus::Applied);
    CHECK(sweep.rune_wave_applied);
    CHECK(
        sweep.additional_shockwave_damage ==
        doctest::Approx(4.0F));
    CHECK(
        sweep.additional_shockwave_radius_blocks ==
        doctest::Approx(1.75F));
    CHECK_FALSE(
        runtime.view().active[
            synergy_index(
                LeviathanKnightSynergyKind::RuneStrike)]);

    const auto replay =
        runtime.prepare_attack(
            build,
            {
                2U,
                ColossalAttackKind::FirstSweep,
                1U,
            });
    CHECK(
        replay.status ==
        LeviathanSynergyStatus::Replayed);
    CHECK(replay.rune_wave_applied);
    CHECK(
        replay.additional_shockwave_damage ==
        doctest::Approx(
            sweep.additional_shockwave_damage));

    const auto next =
        runtime.prepare_attack(
            build,
            {
                3U,
                ColossalAttackKind::SecondSweep,
                0U,
            });
    CHECK_FALSE(next.rune_wave_applied);

    auto consumed_events = std::size_t {0U};
    for (const auto& event : runtime.peek_events()) {
        if (event.type ==
                LeviathanSynergyEventType::Consumed &&
            event.synergy ==
                LeviathanKnightSynergyKind::RuneStrike) {
            ++consumed_events;
            CHECK(event.action_sequence == 2U);
        }
    }
    CHECK(consumed_events == 1U);
}

TEST_CASE(
    "garde de fer réduit exactement la stabilité selon le rang et expire") {
    constexpr std::array<float, 3U> expected {{
        75.0F,
        70.0F,
        65.0F,
    }};
    constexpr std::array<float, 3U> durations {{
        3.0F,
        3.5F,
        4.0F,
    }};

    for (auto rank = std::uint8_t {1U};
         rank <= 3U;
         ++rank) {
        CAPTURE(rank);
        LeviathanKnightSynergyRuntime runtime {};
        const auto build =
            learned_and_equipped(
                AbilityId::KnightIronGuard,
                rank);
        REQUIRE(
            runtime.activate(
                       build,
                       confirmed_activation(
                           AbilityId::KnightIronGuard,
                           rank))
                .accepted());

        const auto unblocked =
            runtime.modify_guard(
                build,
                {100.0F, 0U, true, false, false});
        CHECK_FALSE(unblocked.iron_guard_applied);
        CHECK(
            unblocked.stability_loss ==
            doctest::Approx(100.0F));

        const auto guarded =
            runtime.modify_guard(
                build,
                {100.0F, 0U, true, true, false});
        CHECK(guarded.iron_guard_applied);
        CHECK(
            guarded.stability_loss ==
            doctest::Approx(
                expected[rank - 1U]));

        CHECK(
            runtime.advance(
                build,
                durations[rank - 1U]) ==
            LeviathanSynergyStatus::Expired);
        const auto after_expiry =
            runtime.modify_guard(
                build,
                {100.0F, 0U, true, true, false});
        CHECK_FALSE(after_expiry.iron_guard_applied);
        CHECK(
            after_expiry.stability_loss ==
            doctest::Approx(100.0F));
    }
}

TEST_CASE(
    "un déséquipement coupe immédiatement une synergie déjà active") {
    LeviathanKnightSynergyRuntime runtime {};
    auto build =
        learned_and_equipped(
            AbilityId::KnightIronGuard,
            3U);
    REQUIRE(
        runtime.activate(
                   build,
                   confirmed_activation(
                       AbilityId::KnightIronGuard,
                       21U,
                       10.0F))
            .accepted());

    build.equipped_abilities.fill(AbilityId::None);
    const auto guard =
        runtime.modify_guard(
            build,
            {40.0F, 0U, true, true, false});
    CHECK_FALSE(guard.iron_guard_applied);
    CHECK(
        guard.stability_loss ==
        doctest::Approx(40.0F));
    CHECK_FALSE(
        runtime.view().active[
            synergy_index(
                LeviathanKnightSynergyKind::IronGuard)]);
    REQUIRE_FALSE(runtime.peek_events().empty());
    CHECK(
        runtime.peek_events().back().type ==
        LeviathanSynergyEventType::Deactivated);
}

TEST_CASE(
    "charge du rempart produit un unique balayage massif et borné") {
    LeviathanKnightSynergyRuntime runtime {};
    const auto build =
        learned_and_equipped(
            AbilityId::KnightBulwarkCharge,
            3U);
    REQUIRE(
        runtime.activate(
                   build,
                   confirmed_activation(
                       AbilityId::KnightBulwarkCharge,
                       30U))
            .accepted());

    const auto mismatch =
        runtime.complete_bulwark_charge(
            build,
            {29U, true});
    CHECK(
        mismatch.status ==
        LeviathanSynergyStatus::
            RejectedMismatchedActivation);

    const auto unfinished =
        runtime.complete_bulwark_charge(
            build,
            {30U, false});
    CHECK(
        unfinished.status ==
        LeviathanSynergyStatus::NoEffect);

    const auto completed =
        runtime.complete_bulwark_charge(
            build,
            {30U, true});
    CHECK(
        completed.status ==
        LeviathanSynergyStatus::Applied);
    CHECK(completed.massive_sweep_requested);
    CHECK(
        completed.forced_attack ==
        ColossalAttackKind::FirstSweep);
    CHECK(
        completed.forced_shape ==
        ColossalAttackShape::HorizontalArc);
    CHECK(
        completed.maximum_targets == 10U);
    CHECK(
        completed.range_multiplier ==
        doctest::Approx(1.20F));
    CHECK(
        completed.arc_multiplier ==
        doctest::Approx(1.35F));
    CHECK(
        completed.stagger_multiplier ==
        doctest::Approx(1.30F));

    CHECK(
        runtime.complete_bulwark_charge(
                   build,
                   {30U, true})
            .status ==
        LeviathanSynergyStatus::AlreadyConsumed);
}

TEST_CASE(
    "riposte parfaite appelle le deuxième coup seulement après une parade confirmée") {
    LeviathanKnightSynergyRuntime runtime {};
    const auto build =
        learned_and_equipped(
            AbilityId::KnightPerfectRiposte,
            3U);
    const auto activated =
        runtime.activate(
            build,
            confirmed_activation(
                AbilityId::KnightPerfectRiposte,
                40U));
    REQUIRE(activated.accepted());
    CHECK(
        activated.effective_duration_seconds ==
        doctest::Approx(0.55F));

    CHECK(
        runtime.consume_perfect_riposte(
                   build,
                   {40U, true, false})
            .status ==
        LeviathanSynergyStatus::NoEffect);
    CHECK(
        runtime.consume_perfect_riposte(
                   build,
                   {40U, false, true})
            .status ==
        LeviathanSynergyStatus::NoEffect);

    const auto counter =
        runtime.consume_perfect_riposte(
            build,
            {40U, true, true});
    CHECK(
        counter.status ==
        LeviathanSynergyStatus::Applied);
    CHECK(counter.second_combo_attack_requested);
    CHECK(
        counter.forced_attack ==
        ColossalAttackKind::SecondSweep);
    CHECK(
        counter.forced_shape ==
        ColossalAttackShape::ReverseHorizontalArc);
    CHECK(
        runtime.consume_perfect_riposte(
                   build,
                   {40U, true, true})
            .status ==
        LeviathanSynergyStatus::AlreadyConsumed);
}

TEST_CASE(
    "tempête d'acier augmente temporairement les cibles sans cumul ni dépassement") {
    LeviathanKnightSynergyRuntime runtime {};
    const auto build =
        learned_and_equipped(
            AbilityId::KnightColossusFury,
            3U);
    REQUIRE(
        runtime.activate(
                   build,
                   confirmed_activation(
                       AbilityId::KnightColossusFury,
                       50U,
                       2.0F))
            .accepted());

    const auto first =
        runtime.prepare_attack(
            build,
            {
                1U,
                ColossalAttackKind::FirstSweep,
                6U,
            });
    CHECK(first.steel_tempest_applied);
    CHECK(first.maximum_targets == 12U);

    const auto replacement =
        runtime.activate(
            build,
            confirmed_activation(
                AbilityId::KnightColossusFury,
                51U,
                1.0F));
    CHECK(
        replacement.status ==
        LeviathanSynergyStatus::Replaced);
    const auto second =
        runtime.prepare_attack(
            build,
            {
                2U,
                ColossalAttackKind::Earthbreaker,
                10U,
            });
    CHECK(second.maximum_targets == 12U);

    CHECK(
        runtime.advance(build, 1.0F) ==
        LeviathanSynergyStatus::Expired);
    const auto expired =
        runtime.prepare_attack(
            build,
            {
                3U,
                ColossalAttackKind::FirstSweep,
                6U,
            });
    CHECK_FALSE(expired.steel_tempest_applied);
    CHECK(expired.maximum_targets == 6U);

    const auto externally_excessive =
        runtime.prepare_attack(
            build,
            {
                4U,
                ColossalAttackKind::FirstSweep,
                255U,
            });
    CHECK(
        externally_excessive.maximum_targets ==
        kLeviathanSynergyMaximumTargets);
}

TEST_CASE(
    "forteresse vivante décrit une protection arrière bornée pour les alliés") {
    LeviathanKnightSynergyRuntime runtime {};
    const auto build =
        learned_and_equipped(
            AbilityId::KnightLivingFortress,
            3U);
    REQUIRE(
        runtime.activate(
                   build,
                   confirmed_activation(
                       AbilityId::KnightLivingFortress,
                       60U))
            .accepted());

    const auto lowered =
        runtime.modify_guard(
            build,
            {10.0F, 12U, false, true, true});
    CHECK_FALSE(lowered.ally_protection_enabled);

    const auto melee =
        runtime.modify_guard(
            build,
            {10.0F, 12U, true, true, false});
    CHECK(melee.ally_protection_enabled);
    CHECK(melee.maximum_protected_allies == 6U);
    CHECK(melee.protected_ally_count == 6U);
    CHECK(
        melee.ally_damage_reduction ==
        doctest::Approx(0.70F));
    CHECK(
        melee.ally_guard_range_blocks ==
        doctest::Approx(4.0F));
    CHECK(
        melee.ally_guard_half_angle_degrees ==
        doctest::Approx(55.0F));

    const auto projectile =
        runtime.modify_guard(
            build,
            {10.0F, 1U, true, true, true});
    CHECK(projectile.protected_ally_count == 1U);
    CHECK(
        projectile.ally_damage_reduction ==
        doctest::Approx(0.80F));
}

TEST_CASE(
    "jugement du titan change seulement le variant d'impact de la lame") {
    LeviathanKnightSynergyRuntime runtime {};
    const auto build =
        learned_and_equipped(
            AbilityId::KnightTitanJudgment,
            2U,
            4U);
    REQUIRE(
        runtime.activate(
                   build,
                   confirmed_activation(
                       AbilityId::KnightTitanJudgment,
                       70U))
            .accepted());

    CHECK(
        runtime.prepare_titan_impact(
                   build,
                   {70U, false})
            .status ==
        LeviathanSynergyStatus::NoEffect);
    const auto impact =
        runtime.prepare_titan_impact(
            build,
            {70U, true});
    CHECK(
        impact.status ==
        LeviathanSynergyStatus::Applied);
    CHECK(
        impact.variant ==
        LeviathanImpactVariant::TitanBlade);
    CHECK(impact.colossal_blade_kinematics_requested);
    CHECK(
        impact.damage_multiplier ==
        doctest::Approx(1.0F));
    CHECK(
        impact.stagger_multiplier ==
        doctest::Approx(1.0F));
    CHECK(
        runtime.prepare_titan_impact(
                   build,
                   {70U, true})
            .status ==
        LeviathanSynergyStatus::AlreadyConsumed);
}

TEST_CASE(
    "les synergies compatibles se combinent en conservant toutes les bornes") {
    LeviathanKnightSynergyRuntime runtime {};
    auto build =
        learned_and_equipped(
            AbilityId::KnightVanguardStrike,
            3U,
            0U);
    build.ability_ranks[
        ability_index(
            AbilityId::KnightColossusFury)] = 3U;
    build.equipped_abilities[1U] =
        AbilityId::KnightColossusFury;

    REQUIRE(
        runtime.activate(
                   build,
                   confirmed_activation(
                       AbilityId::KnightVanguardStrike,
                       80U))
            .accepted());
    REQUIRE(
        runtime.activate(
                   build,
                   confirmed_activation(
                       AbilityId::KnightColossusFury,
                       81U))
            .accepted());

    const auto combined =
        runtime.prepare_attack(
            build,
            {
                1U,
                ColossalAttackKind::RunningCleave,
                9U,
            });
    CHECK(combined.rune_wave_applied);
    CHECK(combined.steel_tempest_applied);
    CHECK(
        combined.additional_shockwave_damage ==
        doctest::Approx(5.0F));
    CHECK(
        combined.maximum_targets ==
        kLeviathanSynergyMaximumTargets);
}

TEST_CASE(
    "les identifiants rendent les activations et attaques déterministes") {
    LeviathanKnightSynergyRuntime runtime {};
    const auto build =
        learned_and_equipped(
            AbilityId::KnightVanguardStrike);

    CHECK(
        runtime.activate(
                   build,
                   confirmed_activation(
                       AbilityId::KnightVanguardStrike,
                       0U))
            .status ==
        LeviathanSynergyStatus::
            RejectedInvalidSequence);

    auto invalid_duration =
        confirmed_activation(
            AbilityId::KnightVanguardStrike,
            1U);
    invalid_duration.duration_seconds =
        std::numeric_limits<float>::quiet_NaN();
    CHECK(
        runtime.activate(build, invalid_duration).status ==
        LeviathanSynergyStatus::RejectedInvalidInput);

    const auto first =
        runtime.activate(
            build,
            confirmed_activation(
                AbilityId::KnightVanguardStrike,
                10U,
                100.0F));
    REQUIRE(first.accepted());
    CHECK(
        first.effective_duration_seconds ==
        doctest::Approx(
            kLeviathanSynergyMaximumDurationSeconds));
    CHECK(
        runtime.activate(
                   build,
                   confirmed_activation(
                       AbilityId::KnightVanguardStrike,
                       10U))
            .status ==
        LeviathanSynergyStatus::Replayed);
    CHECK(
        runtime.activate(
                   build,
                   confirmed_activation(
                       AbilityId::KnightVanguardStrike,
                       9U))
            .status ==
        LeviathanSynergyStatus::
            RejectedStaleSequence);

    const auto attack =
        runtime.prepare_attack(
            build,
            {
                10U,
                ColossalAttackKind::FirstSweep,
                6U,
            });
    REQUIRE(attack.rune_wave_applied);
    CHECK(
        runtime.prepare_attack(
                   build,
                   {
                       9U,
                       ColossalAttackKind::FirstSweep,
                       6U,
                   })
            .status ==
        LeviathanSynergyStatus::
            RejectedStaleSequence);
    CHECK(
        runtime.advance(
                   build,
                   std::numeric_limits<float>::infinity()) ==
        LeviathanSynergyStatus::RejectedInvalidInput);
}

TEST_CASE(
    "désactivation et drainage conservent des transitions ordonnées") {
    LeviathanKnightSynergyRuntime runtime {};
    const auto build =
        learned_and_equipped(
            AbilityId::KnightLivingFortress,
            2U);
    REQUIRE(
        runtime.activate(
                   build,
                   confirmed_activation(
                       AbilityId::KnightLivingFortress,
                       90U,
                       5.0F))
            .accepted());

    CHECK(
        runtime.deactivate({
            AbilityId::KnightLivingFortress,
            89U,
        }) ==
        LeviathanSynergyStatus::
            RejectedMismatchedActivation);
    CHECK(
        runtime.view().active[
            synergy_index(
                LeviathanKnightSynergyKind::
                    LivingFortress)]);
    CHECK(
        runtime.deactivate({
            AbilityId::KnightLivingFortress,
            90U,
        }) ==
        LeviathanSynergyStatus::Deactivated);
    CHECK_FALSE(
        runtime.view().active[
            synergy_index(
                LeviathanKnightSynergyKind::
                    LivingFortress)]);
    CHECK(
        runtime.deactivate({
            AbilityId::KnightLivingFortress,
            90U,
        }) ==
        LeviathanSynergyStatus::RejectedNotActive);

    std::array<LeviathanSynergyEvent, 1U> first {};
    REQUIRE(runtime.drain_events(first) == 1U);
    CHECK(first.front().event_id == 1U);
    CHECK(
        first.front().type ==
        LeviathanSynergyEventType::Activated);
    REQUIRE(runtime.peek_events().size() == 1U);
    CHECK(runtime.peek_events().front().event_id == 2U);
    CHECK(
        runtime.peek_events().front().type ==
        LeviathanSynergyEventType::Deactivated);

    std::array<LeviathanSynergyEvent, 4U> rest {};
    CHECK(runtime.drain_events(rest) == 1U);
    CHECK(runtime.peek_events().empty());
}

TEST_CASE(
    "reset restaure un runtime vierge y compris après saturation des événements") {
    LeviathanKnightSynergyRuntime runtime {};
    const auto build =
        learned_and_equipped(
            AbilityId::KnightIronGuard);

    for (auto sequence = std::uint64_t {1U};
         sequence <= 40U;
         ++sequence) {
        const auto activation =
            runtime.activate(
                build,
                confirmed_activation(
                    AbilityId::KnightIronGuard,
                    sequence,
                    1.0F));
        REQUIRE(activation.accepted());
        CHECK(
            runtime.deactivate({
                AbilityId::KnightIronGuard,
                sequence,
            }) ==
            LeviathanSynergyStatus::Deactivated);
    }

    CHECK(
        runtime.peek_events().size() ==
        kLeviathanSynergyEventCapacity);
    CHECK(runtime.view().event_overflowed);

    runtime.reset();
    const auto cleared = runtime.view();
    CHECK_FALSE(cleared.event_overflowed);
    CHECK(runtime.peek_events().empty());
    for (const auto active : cleared.active) {
        CHECK_FALSE(active);
    }

    const auto after_reset =
        runtime.activate(
            build,
            confirmed_activation(
                AbilityId::KnightIronGuard,
                1U));
    CHECK(
        after_reset.status ==
        LeviathanSynergyStatus::Activated);
    REQUIRE_FALSE(runtime.peek_events().empty());
    CHECK(runtime.peek_events().front().event_id == 1U);
}

} // namespace
} // namespace valcraft
