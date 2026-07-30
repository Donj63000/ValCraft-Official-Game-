#include "gameplay/progression/StatusEffectSystem.h"

#include <doctest/doctest.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace valcraft {

namespace {

[[nodiscard]] auto effect(
    StatusEffectTargetId target,
    StatusEffectStackTag tag,
    StatusEffectKind kind,
    float value,
    float duration = 2.0F) noexcept -> StatusEffectSpec {
    return {
        target,
        tag,
        kind,
        value,
        duration,
    };
}

} // namespace

TEST_CASE("les effets refusent toutes les entrées non finies et les identifiants invalides") {
    StatusEffectSystem system {};

    CHECK(
        system.apply(
            effect(
                0U,
                1U,
                StatusEffectKind::DamageReduction,
                0.2F))
            .error ==
        StatusEffectApplyError::InvalidTarget);
    CHECK(
        system.apply(
            effect(
                1U,
                0U,
                StatusEffectKind::DamageReduction,
                0.2F))
            .error ==
        StatusEffectApplyError::InvalidStackTag);
    CHECK(
        system.apply(
            effect(
                1U,
                1U,
                static_cast<StatusEffectKind>(255U),
                0.2F))
            .error ==
        StatusEffectApplyError::InvalidKind);

    for (const auto value : {
             0.0F,
             -0.1F,
             std::numeric_limits<float>::quiet_NaN(),
             std::numeric_limits<float>::infinity(),
         }) {
        CHECK_FALSE(
            system.apply(
                effect(
                    1U,
                    1U,
                    StatusEffectKind::DamageReduction,
                    value))
                .applied);
    }
    for (const auto duration : {
             0.0F,
             -0.1F,
             std::numeric_limits<float>::quiet_NaN(),
             std::numeric_limits<float>::infinity(),
             kStatusEffectMaximumDurationSeconds + 1.0F,
         }) {
        CHECK_FALSE(
            system.apply(
                effect(
                    1U,
                    1U,
                    StatusEffectKind::DamageReduction,
                    0.2F,
                    duration))
                .applied);
    }

    CHECK_FALSE(
        system.update(
            std::numeric_limits<float>::quiet_NaN())
            .accepted);
    CHECK_FALSE(
        system.update(
            std::numeric_limits<float>::infinity())
            .accepted);
    CHECK_FALSE(system.update(-0.1F).accepted);
    CHECK(system.active_effect_count() == 0U);

    const auto invalid_guard =
        system.apply_iron_guard({
            1U,
            1U,
            3.0F,
            0.25F,
            -0.10F,
            0.15F,
            true,
        });
    CHECK_FALSE(invalid_guard.applied);
    CHECK(
        invalid_guard.error ==
        StatusEffectApplyError::InvalidValue);
    CHECK(system.active_effect_count() == 0U);
}

TEST_CASE("une même couche conserve la valeur la plus forte et rafraîchit sa durée") {
    StatusEffectSystem system {};
    constexpr auto target = StatusEffectTargetId {7U};
    constexpr auto tag = StatusEffectStackTag {11U};

    const auto first =
        system.apply(
            effect(
                target,
                tag,
                StatusEffectKind::DamageReduction,
                0.35F,
                2.0F));
    REQUIRE(first.inserted);
    REQUIRE(system.update(1.0F).accepted);

    const auto weaker =
        system.apply(
            effect(
                target,
                tag,
                StatusEffectKind::DamageReduction,
                0.20F,
                2.0F));
    CHECK(weaker.refreshed);
    CHECK(weaker.effective_value == doctest::Approx(0.35F));
    CHECK(weaker.remaining_seconds == doctest::Approx(2.0));
    CHECK(system.active_effect_count(target) == 1U);

    const auto stronger =
        system.apply(
            effect(
                target,
                tag,
                StatusEffectKind::DamageReduction,
                0.50F,
                0.5F));
    CHECK(stronger.refreshed);
    CHECK(stronger.effective_value == doctest::Approx(0.50F));
    CHECK(stronger.remaining_seconds == doctest::Approx(2.0));
    CHECK(
        system.aggregate(target, 100.0F).damage_reduction ==
        doctest::Approx(0.50F));
}

TEST_CASE("les réductions de tags distincts se combinent multiplicativement puis respectent leurs plafonds") {
    StatusEffectSystem system {};
    constexpr auto target = StatusEffectTargetId {9U};

    REQUIRE(
        system.apply(
            effect(
                target,
                1U,
                StatusEffectKind::DamageReduction,
                0.50F))
            .applied);
    REQUIRE(
        system.apply(
            effect(
                target,
                2U,
                StatusEffectKind::DamageReduction,
                0.50F))
            .applied);
    CHECK(
        system.aggregate(target, 100.0F).damage_reduction ==
        doctest::Approx(0.75F));

    REQUIRE(
        system.apply(
            effect(
                target,
                3U,
                StatusEffectKind::DamageReduction,
                0.90F))
            .applied);
    CHECK(
        system.aggregate(target, 100.0F).damage_reduction ==
        doctest::Approx(kMaximumDamageReduction));
}

TEST_CASE("tous les bonus temporaires et le bouclier restent dans leurs plafonds") {
    StatusEffectSystem system {};
    constexpr auto target = StatusEffectTargetId {19U};

    for (const auto tag : {1U, 2U}) {
        REQUIRE(
            system.apply(
                effect(
                    target,
                    tag,
                    StatusEffectKind::MovementSpeedBonus,
                    0.40F))
                .applied);
        REQUIRE(
            system.apply(
                effect(
                    target,
                    tag,
                    StatusEffectKind::RecoverySpeedBonus,
                    0.30F))
                .applied);
        REQUIRE(
            system.apply(
                effect(
                    target,
                    tag,
                    StatusEffectKind::Slow,
                    0.50F))
                .applied);
        REQUIRE(
            system.apply(
                effect(
                    target,
                    tag,
                    StatusEffectKind::Shield,
                    80.0F))
                .applied);
    }

    const auto aggregate = system.aggregate(target, 100.0F);
    CHECK(
        aggregate.movement_speed_bonus ==
        doctest::Approx(kMaximumTemporaryMovementSpeedBonus));
    CHECK(
        aggregate.recovery_speed_bonus ==
        doctest::Approx(kMaximumTemporaryRecoverySpeedBonus));
    CHECK(aggregate.slow == doctest::Approx(kMaximumSlow));
    CHECK(aggregate.shield_health == doctest::Approx(100.0F));
    CHECK(
        aggregate.movement_speed_multiplier() ==
        doctest::Approx(0.60F));
    CHECK(
        aggregate.recovery_speed_multiplier() ==
        doctest::Approx(1.40F));

    const auto absorption =
        system.absorb_with_shield(target, 40.0F, 100.0F);
    CHECK(absorption.accepted);
    CHECK(absorption.absorbed_damage == doctest::Approx(40.0F));
    CHECK(absorption.remaining_shield == doctest::Approx(60.0F));
    CHECK(
        system.aggregate(target, 100.0F).shield_health ==
        doctest::Approx(60.0F));
}

TEST_CASE("la garde de fer applique atomiquement ses protections et sa première absorption") {
    StatusEffectSystem system {};
    constexpr auto target = StatusEffectTargetId {27U};
    constexpr auto tag = StatusEffectStackTag {500U};

    const auto application =
        system.apply_iron_guard({
            target,
            tag,
            5.0F,
            0.30F,
            0.50F,
            0.40F,
            true,
        });
    REQUIRE(application.applied);
    CHECK(application.inserted_effect_count == 4U);
    CHECK(application.refreshed_effect_count == 0U);

    const auto aggregate = system.aggregate(target, 100.0F);
    CHECK(aggregate.damage_reduction == doctest::Approx(0.30F));
    CHECK(
        aggregate.knockback_resistance ==
        doctest::Approx(0.50F));
    CHECK(
        aggregate.frontal_projectile_reduction ==
        doctest::Approx(0.40F));
    CHECK(aggregate.first_absorption_available);
    CHECK(
        aggregate.damage_multiplier(true) ==
        doctest::Approx(0.42F));
    CHECK(
        aggregate.knockback_multiplier() ==
        doctest::Approx(0.50F));

    REQUIRE(
        system.apply_iron_guard({
            28U,
            tag,
            5.0F,
            0.35F,
            0.90F,
            0.15F,
            false,
        })
            .applied);
    CHECK(
        system.aggregate(28U, 100.0F)
            .knockback_resistance ==
        doctest::Approx(0.90F));

    REQUIRE(
        system.apply(
            effect(
                target,
                901U,
                StatusEffectKind::DamageReduction,
                0.80F))
            .applied);
    REQUIRE(
        system.apply(
            effect(
                target,
                902U,
                StatusEffectKind::FrontalProjectileReduction,
                0.80F))
            .applied);
    CHECK(
        system.aggregate(target, 100.0F)
            .damage_multiplier(true) ==
        doctest::Approx(1.0F - kMaximumDamageReduction));

    const auto consumed =
        system.consume_first_absorption(target);
    CHECK(consumed.consumed);
    CHECK(consumed.stack_tag == tag);
    CHECK_FALSE(
        system.aggregate(target, 100.0F)
            .first_absorption_available);
    CHECK_FALSE(
        system.consume_first_absorption(target).consumed);

    const auto reapplied =
        system.apply_iron_guard({
            target,
            tag,
            5.0F,
            0.30F,
            0.50F,
            0.40F,
            true,
        });
    REQUIRE(reapplied.applied);
    CHECK(reapplied.inserted_effect_count == 1U);
    CHECK(reapplied.refreshed_effect_count == 3U);
    CHECK(
        system.consume_first_absorption(target, tag)
            .consumed);
}

TEST_CASE("le pas fixe à soixante hertz donne le même résultat pour toutes les découpes de dt") {
    StatusEffectSystem single_update {};
    StatusEffectSystem split_update {};
    const auto spec =
        effect(
            41U,
            9U,
            StatusEffectKind::DamageReduction,
            0.25F,
            1.5F);
    REQUIRE(single_update.apply(spec).applied);
    REQUIRE(split_update.apply(spec).applied);

    REQUIRE(single_update.update(1.0F).accepted);
    for (std::size_t index = 0U; index < 60U; ++index) {
        REQUIRE(
            split_update.update(1.0F / 60.0F)
                .accepted);
    }
    CHECK(
        single_update.remaining_seconds(
            41U,
            StatusEffectKind::DamageReduction,
            9U) ==
        split_update.remaining_seconds(
            41U,
            StatusEffectKind::DamageReduction,
            9U));

    REQUIRE(single_update.update(0.5F).accepted);
    for (std::size_t index = 0U; index < 30U; ++index) {
        REQUIRE(
            split_update.update(1.0F / 60.0F)
                .accepted);
    }
    CHECK_FALSE(
        single_update.has_effect(
            41U,
            StatusEffectKind::DamageReduction,
            9U));
    CHECK_FALSE(
        split_update.has_effect(
            41U,
            StatusEffectKind::DamageReduction,
            9U));

    StatusEffectSystem half_steps {};
    REQUIRE(
        half_steps.apply(
            effect(
                2U,
                2U,
                StatusEffectKind::Slow,
                0.2F,
                kStatusEffectFixedStepSeconds))
            .applied);
    CHECK(half_steps.update(0.5F / 60.0F).advanced_ticks == 0U);
    CHECK(half_steps.update(0.5F / 60.0F).advanced_ticks == 1U);
    CHECK(half_steps.active_effect_count() == 0U);
}

TEST_CASE("les capacités par cible et globale restent strictement bornées") {
    StatusEffectSystem system {};
    constexpr auto first_target = StatusEffectTargetId {100U};

    for (std::size_t index = 0U;
         index < kMaximumStatusEffectsPerTarget;
         ++index) {
        REQUIRE(
            system.apply(
                effect(
                    first_target,
                    static_cast<StatusEffectStackTag>(index + 1U),
                    StatusEffectKind::MovementSpeedBonus,
                    0.01F))
                .applied);
    }
    CHECK(
        system.apply(
            effect(
                first_target,
                1000U,
                StatusEffectKind::MovementSpeedBonus,
                0.01F))
            .error ==
        StatusEffectApplyError::TargetCapacityReached);

    const auto target_count =
        kMaximumStatusEffects /
        kMaximumStatusEffectsPerTarget;
    for (std::size_t target_index = 1U;
         target_index < target_count;
         ++target_index) {
        for (std::size_t effect_index = 0U;
             effect_index < kMaximumStatusEffectsPerTarget;
             ++effect_index) {
            REQUIRE(
                system.apply(
                    effect(
                        first_target + target_index,
                        static_cast<StatusEffectStackTag>(
                            effect_index + 1U),
                        StatusEffectKind::RecoverySpeedBonus,
                        0.01F))
                    .applied);
        }
    }
    CHECK(system.active_effect_count() == kMaximumStatusEffects);
    CHECK(
        system.apply(
            effect(
                999U,
                1U,
                StatusEffectKind::Slow,
                0.1F))
            .error ==
        StatusEffectApplyError::GlobalCapacityReached);
}

TEST_CASE("les nettoyages prouvent que les états sont uniquement transitoires") {
    StatusEffectSystem system {};
    REQUIRE(
        system.apply(
            effect(
                1U,
                1U,
                StatusEffectKind::DamageReduction,
                0.2F))
            .applied);
    REQUIRE(
        system.apply(
            effect(
                2U,
                1U,
                StatusEffectKind::DamageReduction,
                0.2F))
            .applied);

    system.clear_target(1U);
    CHECK(system.active_effect_count(1U) == 0U);
    CHECK(system.active_effect_count(2U) == 1U);
    system.clear();
    CHECK(system.active_effect_count() == 0U);
    CHECK(system.update(0.0F).accepted);
}

TEST_CASE("l'absorption de bouclier refuse les dégâts et PV invalides") {
    StatusEffectSystem system {};
    REQUIRE(
        system.apply(
            effect(
                1U,
                1U,
                StatusEffectKind::Shield,
                10.0F))
            .applied);

    CHECK_FALSE(
        system.absorb_with_shield(0U, 1.0F, 100.0F)
            .accepted);
    CHECK_FALSE(
        system.absorb_with_shield(
            1U,
            std::numeric_limits<float>::quiet_NaN(),
            100.0F)
            .accepted);
    CHECK_FALSE(
        system.absorb_with_shield(1U, -1.0F, 100.0F)
            .accepted);
    CHECK_FALSE(
        system.absorb_with_shield(
            1U,
            1.0F,
            std::numeric_limits<float>::infinity())
            .accepted);
    CHECK(
        system.aggregate(1U, 100.0F).shield_health ==
        doctest::Approx(10.0F));
}

TEST_CASE("la première absorption annule le coup avant toute application de dégâts") {
    StatusEffectSystem system {};
    constexpr auto target =
        StatusEffectTargetId {81U};
    constexpr auto tag =
        StatusEffectStackTag {91U};
    REQUIRE(
        system.apply(
            effect(
                target,
                tag,
                StatusEffectKind::FirstAbsorption,
                1.0F))
            .applied);

    const auto invalid =
        system.absorb_first_hit(
            target,
            std::numeric_limits<float>::quiet_NaN(),
            tag);
    CHECK_FALSE(invalid.accepted);
    CHECK(
        system.aggregate(target, 100.0F)
            .first_absorption_available);

    const auto zero =
        system.absorb_first_hit(
            target,
            0.0F,
            tag);
    CHECK(zero.accepted);
    CHECK_FALSE(zero.absorbed);
    CHECK(
        system.aggregate(target, 100.0F)
            .first_absorption_available);

    const auto first =
        system.absorb_first_hit(
            target,
            37.5F,
            tag);
    REQUIRE(first.accepted);
    REQUIRE(first.absorbed);
    CHECK(first.stack_tag == tag);
    CHECK(first.absorbed_damage ==
          doctest::Approx(37.5F));
    CHECK(first.remaining_damage ==
          doctest::Approx(0.0F));

    const auto second =
        system.absorb_first_hit(
            target,
            12.0F,
            tag);
    CHECK(second.accepted);
    CHECK_FALSE(second.absorbed);
    CHECK(second.remaining_damage ==
          doctest::Approx(12.0F));
}

TEST_CASE("le snapshot des statuts reprend exactement les ticks et l'ordre") {
    StatusEffectSystem source {};
    REQUIRE(
        source.apply(
            effect(
                5U,
                10U,
                StatusEffectKind::DamageReduction,
                0.25F,
                3.0F))
            .applied);
    REQUIRE(
        source.apply(
            effect(
                5U,
                11U,
                StatusEffectKind::Shield,
                18.0F,
                4.0F))
            .applied);
    REQUIRE(
        source.update(
            0.5F / 60.0F)
            .accepted);
    const auto saved =
        source.snapshot();
    CHECK(saved.fractional_tick_accumulator ==
          doctest::Approx(0.5));

    StatusEffectSystem restored {};
    const auto load =
        restored.load_state(saved);
    CHECK_FALSE(load.sanitized);
    CHECK(load.discarded_effect_count == 0U);
    CHECK(load.restored_effect_count == 2U);
    CHECK(restored.snapshot() == saved);

    const auto source_update =
        source.update(
            0.5F / 60.0F);
    const auto restored_update =
        restored.update(
            0.5F / 60.0F);
    CHECK(source_update.advanced_ticks ==
          restored_update.advanced_ticks);
    CHECK(restored.snapshot() ==
          source.snapshot());
}

TEST_CASE("le chargement des statuts assainit les entrées corrompues") {
    StatusEffectSystemSnapshot requested {};
    requested.entries[0U] = {
        7U,
        8U,
        StatusEffectKind::FirstAbsorption,
        0.25F,
        60U,
        9U,
        true,
    };
    requested.entries[1U] = {
        0U,
        12U,
        StatusEffectKind::Shield,
        10.0F,
        60U,
        10U,
        true,
    };
    requested.entries[2U] = {
        7U,
        8U,
        StatusEffectKind::FirstAbsorption,
        1.0F,
        60U,
        11U,
        true,
    };
    requested.entries[3U] = {
        7U,
        13U,
        StatusEffectKind::Shield,
        5.0F,
        60U,
        9U,
        true,
    };
    requested.fractional_tick_accumulator =
        std::numeric_limits<double>::infinity();
    requested.next_sequence = 1U;

    StatusEffectSystem restored {};
    const auto load =
        restored.load_state(requested);
    CHECK(load.sanitized);
    CHECK(load.restored_effect_count == 1U);
    CHECK(load.discarded_effect_count == 3U);
    const auto normalized =
        restored.snapshot();
    CHECK(normalized.entries[0U].value ==
          doctest::Approx(1.0F));
    CHECK(normalized.fractional_tick_accumulator ==
          doctest::Approx(0.0));
    CHECK(normalized.next_sequence == 10U);

    restored.clear_stack(7U, 8U);
    CHECK(restored.active_effect_count() == 0U);
}

TEST_CASE("le compteur de sequence se compacte avant tout debordement") {
    StatusEffectSystem source {};
    REQUIRE(
        source.apply(
            effect(
                1U,
                1U,
                StatusEffectKind::Shield,
                3.0F))
            .applied);
    auto exhausted =
        source.snapshot();
    exhausted.next_sequence =
        std::numeric_limits<std::uint64_t>::max();

    StatusEffectSystem restored {};
    const auto load =
        restored.load_state(exhausted);
    REQUIRE_FALSE(load.sanitized);
    REQUIRE(restored.snapshot() == exhausted);
    REQUIRE(
        restored.apply(
            effect(
                1U,
                2U,
                StatusEffectKind::Shield,
                4.0F))
            .applied);

    const auto compacted =
        restored.snapshot();
    CHECK(compacted.entries[0U].sequence == 1U);
    CHECK(compacted.entries[1U].sequence == 2U);
    CHECK(compacted.next_sequence == 3U);
}

} // namespace valcraft
