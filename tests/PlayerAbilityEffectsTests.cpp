#include "gameplay/progression/PlayerAbilityEffects.h"

#include <doctest/doctest.h>

#include <limits>

namespace valcraft {

namespace {

[[nodiscard]] auto iron_guard_resolution(
    std::uint8_t rank,
    bool mastery,
    AbilityCastSequence cast_sequence = 7U) noexcept
    -> AbilityCastResolution {
    const auto* definition =
        ability_rank_definition(
            AbilityId::KnightIronGuard,
            rank);
    if (definition == nullptr) {
        return {};
    }

    AbilityCastResolution resolution {};
    resolution.id =
        AbilityId::KnightIronGuard;
    resolution.cast_sequence =
        cast_sequence;
    resolution.rank = rank;
    resolution.duration_seconds =
        definition->duration_seconds;
    resolution.values =
        definition->values;
    resolution.mastery_active =
        mastery;
    return resolution;
}

} // namespace

TEST_CASE("garde de fer applique les trois rangs sans dépendre de Game") {
    for (const auto rank : {
             std::uint8_t {1U},
             std::uint8_t {2U},
             std::uint8_t {3U},
         }) {
        PlayerAbilityEffects effects {};
        const auto resolution =
            iron_guard_resolution(
                rank,
                false);
        REQUIRE(
            effects.activate_iron_guard(
                       resolution,
                       true)
                .applied);

        const auto aggregate =
            effects.aggregate(
                20.0F);
        CHECK(
            aggregate.damage_reduction ==
            doctest::Approx(
                resolution.values[0U]));
        CHECK(
            aggregate.knockback_resistance ==
            doctest::Approx(
                resolution.values[1U]));
        CHECK(
            aggregate.frontal_projectile_reduction ==
            doctest::Approx(
                rank == 3U
                    ? resolution.values[2U]
                    : 0.0F));
        CHECK_FALSE(
            aggregate
                .first_absorption_available);
    }
}

TEST_CASE("la réduction frontale du rang trois exige un bouclier") {
    PlayerAbilityEffects effects {};
    REQUIRE(
        effects.activate_iron_guard(
                   iron_guard_resolution(
                       3U,
                       false),
                   false)
            .applied);
    const auto aggregate =
        effects.aggregate(
            20.0F);
    CHECK(
        aggregate.damage_reduction ==
        doctest::Approx(0.35F));
    CHECK(
        aggregate.frontal_projectile_reduction ==
        doctest::Approx(0.0F));
    CHECK(
        aggregate.damage_multiplier(true) ==
        doctest::Approx(0.65F));
}

TEST_CASE("fer réactif ne se déclenche qu'une fois par activation") {
    PlayerAbilityEffects effects {};
    REQUIRE(
        effects.activate_iron_guard(
                   iron_guard_resolution(
                       3U,
                       true,
                       42U),
                   true)
            .applied);

    const auto first =
        effects.consume_iron_guard_absorption();
    REQUIRE(first.triggered);
    CHECK(first.cast_sequence == 42U);
    CHECK(first.wave_damage == doctest::Approx(4.0F));
    CHECK(first.wave_radius == doctest::Approx(2.0F));
    CHECK(first.energy_refund == doctest::Approx(5.0F));
    CHECK_FALSE(
        effects.consume_iron_guard_absorption()
            .triggered);
}

TEST_CASE("l'expiration est déterministe et rend la séquence du cast") {
    PlayerAbilityEffects split {};
    PlayerAbilityEffects single {};
    const auto resolution =
        iron_guard_resolution(
            1U,
            true,
            99U);
    REQUIRE(
        split.activate_iron_guard(
                 resolution,
                 false)
            .applied);
    REQUIRE(
        single.activate_iron_guard(
                  resolution,
                  false)
            .applied);

    for (auto tick = 0U;
         tick < 179U;
         ++tick) {
        CHECK_FALSE(
            split.update(
                     1.0F / 60.0F)
                .iron_guard_expired);
    }
    CHECK(split.iron_guard_active());
    const auto split_expiration =
        split.update(
            1.0F / 60.0F);
    const auto single_expiration =
        single.update(
            3.0F);
    REQUIRE(
        split_expiration
            .iron_guard_expired);
    REQUIRE(
        single_expiration
            .iron_guard_expired);
    CHECK(
        split_expiration
            .iron_guard_cast_sequence ==
        99U);
    CHECK(
        single_expiration
            .iron_guard_cast_sequence ==
        99U);
    CHECK_FALSE(split.iron_guard_active());
    CHECK_FALSE(single.iron_guard_active());
}

TEST_CASE("les activations invalides n'altèrent aucun statut") {
    PlayerAbilityEffects effects {};
    auto invalid =
        iron_guard_resolution(
            1U,
            true);
    invalid.cast_sequence = 0U;
    CHECK_FALSE(
        effects.activate_iron_guard(
                   invalid,
                   true)
            .applied);
    CHECK_FALSE(effects.iron_guard_active());

    invalid.cast_sequence = 1U;
    invalid.duration_seconds =
        std::numeric_limits<float>::quiet_NaN();
    CHECK_FALSE(
        effects.activate_iron_guard(
                   invalid,
                   true)
            .applied);
    CHECK_FALSE(effects.iron_guard_active());
}

TEST_CASE("fer reactif annule reellement le premier coup avant les degats") {
    PlayerAbilityEffects effects {};
    REQUIRE(
        effects.activate_iron_guard(
                   iron_guard_resolution(
                       3U,
                       true,
                       42U),
                   true)
            .applied);

    const auto invalid =
        effects.intercept_iron_guard_damage(
            std::numeric_limits<float>::quiet_NaN());
    CHECK_FALSE(invalid.accepted);
    CHECK_FALSE(invalid.absorbed);

    const auto harmless =
        effects.intercept_iron_guard_damage(
            0.0F);
    CHECK(harmless.accepted);
    CHECK_FALSE(harmless.absorbed);
    CHECK(effects.aggregate(20.0F).first_absorption_available);

    const auto first =
        effects.intercept_iron_guard_damage(
            12.5F);
    REQUIRE(first.accepted);
    REQUIRE(first.absorbed);
    CHECK(first.requested_damage == doctest::Approx(12.5F));
    CHECK(first.absorbed_damage == doctest::Approx(12.5F));
    CHECK(first.remaining_damage == doctest::Approx(0.0F));
    REQUIRE(first.reactive.triggered);
    CHECK(first.reactive.cast_sequence == 42U);
    CHECK(first.reactive.wave_damage == doctest::Approx(4.0F));
    CHECK(first.reactive.wave_radius == doctest::Approx(2.0F));
    CHECK(first.reactive.energy_refund == doctest::Approx(5.0F));

    const auto second =
        effects.intercept_iron_guard_damage(
            7.0F);
    REQUIRE(second.accepted);
    CHECK_FALSE(second.absorbed);
    CHECK(second.remaining_damage == doctest::Approx(7.0F));
    CHECK_FALSE(second.reactive.triggered);
}

TEST_CASE("la garde de fer reprend exactement tous ses timers et metadonnees") {
    PlayerAbilityEffects source {};
    REQUIRE(
        source.activate_iron_guard(
                  iron_guard_resolution(
                      3U,
                      true,
                      123U),
                  true)
            .applied);
    REQUIRE(
        source.update(
                  1.0F / 120.0F)
            .expired_effect_count == 0U);
    const auto saved =
        source.snapshot();

    PlayerAbilityEffects restored {};
    const auto load =
        restored.load_state(saved);
    CHECK(load.iron_guard_restored);
    CHECK_FALSE(load.sanitized);
    CHECK(restored.snapshot() == saved);

    const auto source_update =
        source.update(
            1.0F / 120.0F);
    const auto restored_update =
        restored.update(
            1.0F / 120.0F);
    CHECK(restored_update.expired_effect_count ==
          source_update.expired_effect_count);
    CHECK(restored.snapshot() == source.snapshot());
}

TEST_CASE("un chargement incoherent supprime toute la garde composite") {
    PlayerAbilityEffects source {};
    REQUIRE(
        source.activate_iron_guard(
                  iron_guard_resolution(
                      3U,
                      true,
                      55U),
                  true)
            .applied);
    auto corrupted =
        source.snapshot();
    for (auto& entry :
         corrupted.status_effects.entries) {
        if (entry.active &&
            entry.kind ==
                StatusEffectKind::KnockbackResistance) {
            REQUIRE(entry.remaining_ticks > 1U);
            --entry.remaining_ticks;
            break;
        }
    }

    PlayerAbilityEffects restored {};
    const auto load =
        restored.load_state(corrupted);
    CHECK(load.sanitized);
    CHECK_FALSE(load.iron_guard_restored);
    CHECK(load.status_effects.restored_effect_count == 0U);
    CHECK(load.status_effects.discarded_effect_count == 4U);
    CHECK_FALSE(restored.iron_guard_active());
    CHECK(restored.iron_guard_cast_sequence() == 0U);
    CHECK(restored.aggregate(20.0F).damage_reduction ==
          doctest::Approx(0.0F));
    CHECK_FALSE(
        restored.aggregate(20.0F)
            .first_absorption_available);
}

TEST_CASE("une nouvelle garde retire les composantes devenues inactives") {
    PlayerAbilityEffects effects {};
    REQUIRE(
        effects.activate_iron_guard(
                   iron_guard_resolution(
                       3U,
                       true,
                       80U),
                   true)
            .applied);
    REQUIRE(
        effects.aggregate(20.0F)
            .first_absorption_available);
    REQUIRE(
        effects.aggregate(20.0F)
            .frontal_projectile_reduction > 0.0F);

    REQUIRE(
        effects.activate_iron_guard(
                   iron_guard_resolution(
                       1U,
                       false,
                       81U),
                   false)
            .applied);
    const auto aggregate =
        effects.aggregate(20.0F);
    CHECK_FALSE(aggregate.first_absorption_available);
    CHECK(aggregate.frontal_projectile_reduction ==
          doctest::Approx(0.0F));
    CHECK(effects.iron_guard_cast_sequence() == 81U);
}

} // namespace valcraft
