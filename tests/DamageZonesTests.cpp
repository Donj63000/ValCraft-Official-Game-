#include "gameplay/combat/DamageZones.h"

#include <doctest/doctest.h>

#include <array>
#include <limits>

namespace valcraft {

namespace {

[[nodiscard]] constexpr auto colossus_damage_zones()
    -> std::array<DamageZoneDefinition, 3> {
    return {{
        {
            1U,
            DamageZoneKind::LeftArm,
            65.0F,
            1.50F,
            1.0F,
            1.25F,
        },
        {
            2U,
            DamageZoneKind::RightArm,
            75.0F,
            1.0F,
            1.0F,
            1.0F,
        },
        {
            3U,
            DamageZoneKind::Head,
            50.0F,
            2.0F,
            0.50F,
            1.50F,
        },
    }};
}

} // namespace

TEST_CASE("damage zones configure atomically and reject malformed definitions") {
    DamageZones zones {};
    const auto valid = colossus_damage_zones();
    REQUIRE(zones.configure(valid).configured);
    REQUIRE(zones.apply_hit({1U, 10.0F, 0.0F}).accepted);
    REQUIRE(zones.zone(1U).has_value());
    const auto resistance_before =
        zones.zone(1U)->remaining_local_resistance;

    auto duplicate = valid;
    duplicate[2].id = duplicate[0].id;
    const auto duplicate_result =
        zones.configure(duplicate);
    CHECK_FALSE(duplicate_result.configured);
    CHECK(
        duplicate_result.error ==
        DamageZonesConfigureError::DuplicateId);
    CHECK(duplicate_result.failing_definition_index == 2U);

    // Je vérifie que la configuration refusée n'efface pas le combat actif.
    REQUIRE(zones.zone(1U).has_value());
    CHECK(
        zones.zone(1U)->remaining_local_resistance ==
        doctest::Approx(resistance_before));
    CHECK(zones.zone_count() == valid.size());

    auto invalid_resistance = valid;
    invalid_resistance[0].maximum_local_resistance =
        std::numeric_limits<float>::quiet_NaN();
    CHECK(
        zones.configure(invalid_resistance).error ==
        DamageZonesConfigureError::InvalidResistance);

    auto invalid_multiplier = valid;
    invalid_multiplier[0].stagger_multiplier =
        kMaximumDamageZoneMultiplier + 0.01F;
    CHECK(
        zones.configure(invalid_multiplier).error ==
        DamageZonesConfigureError::InvalidMultiplier);

    std::array<
        DamageZoneDefinition,
        kMaximumDamageZones + 1U>
        too_many {};
    for (std::size_t index = 0U;
         index < too_many.size();
         ++index) {
        too_many[index].id =
            static_cast<DamageZoneId>(index + 1U);
    }
    CHECK(
        zones.configure(too_many).error ==
        DamageZonesConfigureError::CapacityExceeded);
}

TEST_CASE("damage zones keep local resistance independent from health and stagger damage") {
    DamageZones zones {};
    const auto definitions = colossus_damage_zones();
    REQUIRE(zones.configure(definitions).configured);

    const auto first =
        zones.apply_hit({1U, 20.0F, 30.0F});
    REQUIRE(first.accepted);
    CHECK(first.kind == DamageZoneKind::LeftArm);
    CHECK(first.health_damage == doctest::Approx(30.0F));
    CHECK(
        first.local_resistance_damage ==
        doctest::Approx(20.0F));
    CHECK(first.stagger_damage == doctest::Approx(37.5F));
    CHECK(
        first.previous_condition ==
        DamageZoneCondition::Intact);
    CHECK(first.condition == DamageZoneCondition::Wounded);
    CHECK(
        first.remaining_local_resistance ==
        doctest::Approx(45.0F));
    CHECK_FALSE(first.depleted_now);

    const auto second =
        zones.apply_hit({1U, 60.0F, 0.0F});
    REQUIRE(second.accepted);
    CHECK(
        second.local_resistance_damage ==
        doctest::Approx(45.0F));
    CHECK(
        second.remaining_local_resistance ==
        doctest::Approx(0.0F));
    CHECK(second.condition == DamageZoneCondition::Depleted);
    CHECK(second.depleted_now);

    const auto after_depletion =
        zones.apply_hit({1U, 10.0F, 8.0F});
    REQUIRE(after_depletion.accepted);
    CHECK(
        after_depletion.health_damage ==
        doctest::Approx(15.0F));
    CHECK(
        after_depletion.local_resistance_damage ==
        doctest::Approx(0.0F));
    CHECK(
        after_depletion.stagger_damage ==
        doctest::Approx(10.0F));
    CHECK_FALSE(after_depletion.depleted_now);

    REQUIRE(zones.zone(2U).has_value());
    CHECK(
        zones.zone(2U)->remaining_local_resistance ==
        doctest::Approx(75.0F));
    CHECK(
        zones.zone(2U)->condition ==
        DamageZoneCondition::Intact);
}

TEST_CASE("damage zones reject invalid hits without mutating state") {
    DamageZones zones {};
    const auto definitions = colossus_damage_zones();
    REQUIRE(zones.configure(definitions).configured);

    CHECK(
        zones.apply_hit({0U, 5.0F, 5.0F}).error ==
        DamageZoneHitError::InvalidId);
    CHECK(
        zones.apply_hit({99U, 5.0F, 5.0F}).error ==
        DamageZoneHitError::UnknownZone);
    CHECK(
        zones.apply_hit({
                1U,
                std::numeric_limits<float>::infinity(),
                0.0F,
            })
            .error ==
        DamageZoneHitError::InvalidDamage);
    CHECK(
        zones.apply_hit({
                1U,
                0.0F,
                std::numeric_limits<float>::quiet_NaN(),
            })
            .error ==
        DamageZoneHitError::InvalidStagger);

    REQUIRE(zones.zone(1U).has_value());
    CHECK(
        zones.zone(1U)->remaining_local_resistance ==
        doctest::Approx(65.0F));

    const auto no_op = zones.apply_hit({1U, 0.0F, 0.0F});
    CHECK(no_op.accepted);
    CHECK(
        no_op.condition ==
        DamageZoneCondition::Intact);
}

TEST_CASE("damage zones reset and clear have explicit semantics") {
    DamageZones zones {};
    const auto definitions = colossus_damage_zones();
    REQUIRE(zones.configure(definitions).configured);
    REQUIRE(zones.apply_hit({3U, 30.0F, 10.0F}).accepted);
    REQUIRE(zones.zone(3U).has_value());
    CHECK(
        zones.zone(3U)->condition ==
        DamageZoneCondition::Wounded);

    zones.reset();
    REQUIRE(zones.zone(3U).has_value());
    CHECK(
        zones.zone(3U)->remaining_local_resistance ==
        doctest::Approx(50.0F));
    CHECK(
        zones.zone(3U)->condition ==
        DamageZoneCondition::Intact);

    zones.clear();
    CHECK(zones.zone_count() == 0U);
    CHECK_FALSE(zones.zone(3U).has_value());
}

} // namespace valcraft
