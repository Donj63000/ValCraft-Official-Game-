#include "gameplay/combat/DamageZones.h"
#include "gameplay/combat/DismembermentSystem.h"
#include "gameplay/combat/StaggerSystem.h"
#include "gameplay/weapons/ColossalWeaponCombat.h"

#include <doctest/doctest.h>

#include <array>

namespace valcraft {
namespace {

TEST_CASE(
    "l'exécution chargée traverse la zone remplit le stagger puis sectionne le membre") {
    DamageZones zones {};
    const std::array zone_definitions {
        DamageZoneDefinition {
            1U,
            DamageZoneKind::LeftArm,
            65.0F,
            1.0F,
            2.0F,
            1.0F,
        },
    };
    REQUIRE(
        zones.configure(zone_definitions).configured);

    StaggerSystem stagger {};
    REQUIRE(
        stagger
            .configure({
                120.0F,
                12.0F,
                1.5F,
                2.0F,
            })
            .configured);

    DismembermentSystem dismemberment {};
    const std::array part_definitions {
        DismembermentPartDefinition {
            1U,
            100.0F,
            1U << 0U,
            false,
            0.10F,
        },
    };
    REQUIRE(
        dismemberment
            .configure(part_definitions)
            .configured);

    ColossalDamageRequest request {};
    request.attack =
        ColossalAttackKind::ChargedExecution;
    request.target_weight =
        ColossalTargetWeight::Boss;
    request.strength = 6U;
    request.momentum = 3U;
    const auto combat =
        resolve_colossal_damage(request);
    const auto zone_hit =
        zones.apply_hit({
            1U,
            combat.direct_damage,
            combat.stagger_power,
        });
    REQUIRE(zone_hit.accepted);
    CHECK(zone_hit.depleted_now);
    CHECK(
        zone_hit.condition ==
        DamageZoneCondition::Depleted);

    const auto stagger_hit =
        stagger.apply(zone_hit.stagger_damage);
    REQUIRE(stagger_hit.accepted);
    CHECK(stagger_hit.triggered);
    CHECK(stagger.state().staggered);

    const auto section =
        dismemberment.try_section({
            1U,
            zone_hit.condition,
            combat.sever_power,
            0.70F,
            true,
            false,
            true,
            stagger.state().staggered,
            true,
            GorePresentationMode::Reduced,
        });
    REQUIRE(section.accepted);
    CHECK(section.severed_now);
    CHECK(section.gameplay_neutralized);
    CHECK(
        section.visual_action ==
        DismembermentVisualAction::HideWithMutedEffect);
    CHECK(
        dismemberment.capability_is_disabled(
            1U << 0U));
}

TEST_CASE(
    "un premier balayage blesse le membre sans contourner sa résistance locale") {
    DamageZones zones {};
    const std::array zone_definitions {
        DamageZoneDefinition {
            1U,
            DamageZoneKind::RightLeg,
            85.0F,
            1.0F,
            1.0F,
            1.0F,
        },
    };
    REQUIRE(
        zones.configure(zone_definitions).configured);
    DismembermentSystem dismemberment {};
    const std::array part_definitions {
        DismembermentPartDefinition {
            1U,
            85.0F,
            1U << 1U,
            false,
            0.10F,
        },
    };
    REQUIRE(
        dismemberment
            .configure(part_definitions)
            .configured);

    ColossalDamageRequest request {};
    request.attack =
        ColossalAttackKind::FirstSweep;
    request.strength = 6U;
    const auto combat =
        resolve_colossal_damage(request);
    const auto zone_hit =
        zones.apply_hit({
            1U,
            combat.direct_damage,
            combat.stagger_power,
        });
    REQUIRE(zone_hit.accepted);
    CHECK(
        zone_hit.condition ==
        DamageZoneCondition::Wounded);

    const auto section =
        dismemberment.try_section({
            1U,
            zone_hit.condition,
            combat.sever_power,
            0.50F,
            true,
            false,
            true,
            false,
            false,
            GorePresentationMode::Full,
        });
    CHECK(section.accepted);
    CHECK(
        section.reason ==
        DismembermentBlockReason::
            LocalResistanceRemaining);
    CHECK_FALSE(
        dismemberment.capability_is_disabled(
            1U << 1U));
}

} // namespace
} // namespace valcraft
