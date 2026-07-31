#include "creatures/bosses/ChainedColossus.h"

#include <doctest/doctest.h>
#include <glm/geometric.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>

namespace valcraft {

namespace {

void release_and_advance(
    ChainedColossus& colossus,
    float seconds,
    const glm::vec3& player = glm::vec3 {
        0.0F,
        0.0F,
        3.0F,
    }) {
    colossus.release();
    const auto steps =
        static_cast<int>(
            seconds * 60.0F);
    for (auto index = 0;
         index < steps;
         ++index) {
        colossus.update(
            1.0F / 60.0F,
            player);
    }
}

auto exhaust_zone(
    ChainedColossus& colossus,
    DamageZoneId zone,
    float damage = 24.0F) {
    ColossusHitResult result {};
    for (auto index = 0;
         index < 12;
         ++index) {
        result = colossus.apply_hit({
            zone,
            damage,
            0.0F,
            0.0F,
            GorePresentationMode::Full,
            true,
            false,
        });
        if (result.zone.condition ==
            DamageZoneCondition::Depleted) {
            break;
        }
    }
    return result;
}

auto limb_view(
    const ChainedColossus& colossus,
    DamageZoneId zone_id)
    -> ChainedColossusLimbView {
    const auto views = colossus.limb_views();
    const auto found =
        std::find_if(
            views.begin(),
            views.end(),
            [zone_id](
                const ChainedColossusLimbView&
                    view) {
                return view.zone_id == zone_id;
            });
    return found != views.end()
               ? *found
               : ChainedColossusLimbView {};
}

auto advance_until_attack_event(
    ChainedColossus& colossus,
    const glm::vec3& player_position,
    float maximum_seconds = 8.0F)
    -> std::optional<
        ChainedColossusAttackEvent> {
    const auto steps =
        static_cast<int>(
            maximum_seconds * 60.0F);
    for (auto index = 0;
         index < steps;
         ++index) {
        colossus.update(
            1.0F / 60.0F,
            player_position);
        const auto events =
            colossus.consume_attack_events();
        if (!events.empty()) {
            return events.front();
        }
    }
    return std::nullopt;
}

auto section_limb(
    ChainedColossus& colossus,
    DamageZoneId zone_id)
    -> ColossusHitResult {
    static_cast<void>(
        exhaust_zone(
            colossus,
            zone_id));
    return colossus.apply_hit({
        zone_id,
        0.0F,
        0.0F,
        70.0F,
        GorePresentationMode::Full,
        true,
        false,
    });
}

} // namespace

TEST_CASE("le Colosse reste invulnerable tant que ses chaines tiennent") {
    ChainedColossus colossus {};
    colossus.reset(
        glm::vec3 {0.0F, 72.0F, 0.0F},
        44U);

    const auto hit =
        colossus.apply_hit({
            kColossusTorsoZone,
            100.0F,
            100.0F,
            70.0F,
        });
    CHECK_FALSE(hit.accepted);
    CHECK(
        hit.failure ==
        ColossusHitFailure::Invulnerable);
    CHECK(
        colossus.state().health ==
        doctest::Approx(
            kChainedColossusMaximumHealth));

    colossus.release();
    CHECK_FALSE(colossus.state().chained);
    CHECK_FALSE(
        colossus.state().invulnerable);
}

TEST_CASE("les attaques du Colosse sont telegraphiees et espacees") {
    ChainedColossus colossus {};
    colossus.reset(
        {0.0F, 72.0F, 0.0F},
        8U);
    release_and_advance(
        colossus,
        5.0F,
        glm::vec3 {0.0F, 72.0F, 3.0F});

    const auto events =
        colossus.consume_attack_events();
    REQUIRE_FALSE(events.empty());
    for (const auto& event : events) {
        CHECK(event.damage >= 2.0F);
        CHECK(event.damage <= 6.0F);
        CHECK(event.sequence != 0U);
        CHECK(event.radius > 0.0F);
    }
}

TEST_CASE("un bras sectionne supprime reellement son attaque") {
    ChainedColossus colossus {};
    colossus.reset(
        glm::vec3 {0.0F},
        11U);
    colossus.release();

    // Je fais passer la vie en phase deux avant de finir la resistance locale.
    static_cast<void>(
        colossus.apply_hit({
            kColossusTorsoZone,
            150.0F,
            0.0F,
            0.0F,
        }));
    static_cast<void>(
        exhaust_zone(
            colossus,
            kColossusRightArmZone));
    const auto sever =
        colossus.apply_hit({
            kColossusRightArmZone,
            1.0F,
            0.0F,
            70.0F,
            GorePresentationMode::Full,
            true,
            false,
        });

    REQUIRE(sever.limb_severed);
    CHECK_FALSE(
        colossus.attack_available(
            ChainedColossusAttack::
                ChainSlam));
    CHECK(
        colossus.attack_available(
            ChainedColossusAttack::
                ArmSweep));
}

TEST_CASE("une jambe neutralisee retire la charge et ralentit le boss") {
    ChainedColossus colossus {};
    colossus.reset(
        glm::vec3 {0.0F},
        21U);
    colossus.release();
    static_cast<void>(
        colossus.apply_hit({
            kColossusTorsoZone,
            240.0F,
            0.0F,
            0.0F,
        }));
    static_cast<void>(
        exhaust_zone(
            colossus,
            kColossusLeftLegZone));
    const auto sever =
        colossus.apply_hit({
            kColossusLeftLegZone,
            1.0F,
            0.0F,
            70.0F,
        });
    REQUIRE(sever.limb_severed);
    CHECK_FALSE(
        colossus.attack_available(
            ChainedColossusAttack::
                SlowCharge));

    const auto before =
        colossus.state().position;
    for (auto index = 0;
         index < 30;
         ++index) {
        colossus.update(
            1.0F / 60.0F,
            glm::vec3 {0.0F, 0.0F, 20.0F});
    }
    const auto distance =
        glm::length(
            colossus.state().position -
            before);
    CHECK(distance < 0.50F);
}

TEST_CASE("une jambe gravement blessee retire la charge avant le sectionnement") {
    ChainedColossus colossus {};
    colossus.reset(glm::vec3 {0.0F}, 23U);
    colossus.release();
    static_cast<void>(
        colossus.apply_hit({
            kColossusTorsoZone,
            240.0F,
            0.0F,
            0.0F,
        }));
    const auto wounded =
        exhaust_zone(
            colossus,
            kColossusRightLegZone);

    REQUIRE(
        wounded.zone.condition ==
        DamageZoneCondition::Depleted);
    CHECK_FALSE(wounded.limb_severed);
    CHECK_FALSE(
        colossus.attack_available(
            ChainedColossusAttack::
                SlowCharge));
}

TEST_CASE("une attaque de bras est annulee si le membre tombe pendant sa preparation") {
    ChainedColossus colossus {};
    colossus.reset(glm::vec3 {0.0F}, 10U);
    colossus.release();
    static_cast<void>(
        colossus.apply_hit({
            kColossusTorsoZone,
            150.0F,
            0.0F,
            0.0F,
        }));
    static_cast<void>(
        exhaust_zone(
            colossus,
            kColossusLeftArmZone));

    for (auto index = 0; index < 61; ++index) {
        colossus.update(
            1.0F / 60.0F,
            glm::vec3 {0.0F, 0.0F, 3.0F});
    }
    REQUIRE(
        colossus.state().attack ==
        ChainedColossusAttack::ArmSweep);
    REQUIRE(
        colossus.state().attack_stage ==
        ChainedColossusAttackStage::Windup);

    const auto sever =
        colossus.apply_hit({
            kColossusLeftArmZone,
            1.0F,
            0.0F,
            70.0F,
            GorePresentationMode::Full,
            true,
            false,
        });
    REQUIRE(sever.limb_severed);
    colossus.update(
        1.0F / 60.0F,
        glm::vec3 {0.0F, 0.0F, 3.0F});

    CHECK(
        colossus.state().attack ==
        ChainedColossusAttack::None);
    CHECK(
        colossus.consume_attack_events()
            .empty());
}

TEST_CASE("le Colosse peut mourir sans execution") {
    ChainedColossus colossus {};
    colossus.reset(glm::vec3 {0.0F}, 31U);
    colossus.release();
    const auto result =
        colossus.apply_hit({
            kColossusTorsoZone,
            1000.0F,
            0.0F,
            0.0F,
        });

    CHECK(result.killed);
    CHECK_FALSE(
        result.execution_completed);
    CHECK(
        colossus.state().phase ==
        ChainedColossusPhase::Dead);
}

TEST_CASE("l'execution exige tete epuisee vie basse et desequilibre") {
    ChainedColossus colossus {};
    colossus.reset(glm::vec3 {0.0F}, 41U);
    colossus.release();

    static_cast<void>(
        colossus.apply_hit({
            kColossusTorsoZone,
            370.0F,
            0.0F,
            0.0F,
        }));
    static_cast<void>(
        exhaust_zone(
            colossus,
            kColossusHeadZone,
            30.0F));
    CHECK_FALSE(colossus.can_execute());

    const auto stagger =
        colossus.apply_hit({
            kColossusTorsoZone,
            0.0F,
            150.0F,
            0.0F,
        });
    REQUIRE(stagger.stagger_triggered);
    REQUIRE(colossus.can_execute());

    const auto execution =
        colossus.apply_hit({
            kColossusHeadZone,
            1.0F,
            0.0F,
            70.0F,
            GorePresentationMode::Reduced,
            true,
            true,
        });
    CHECK(execution.limb_severed);
    CHECK(execution.execution_completed);
    CHECK(execution.killed);
    CHECK(
        execution.dismemberment
            .visual_action ==
        DismembermentVisualAction::
            HideWithMutedEffect);
}

TEST_CASE("le niveau de sang suit les phases sans dependre du gore") {
    ChainedColossus colossus {};
    colossus.reset(glm::vec3 {0.0F}, 51U);
    colossus.release();
    CHECK(
        colossus.state().blood_level ==
        0U);

    static_cast<void>(
        colossus.apply_hit({
            kColossusTorsoZone,
            300.0F,
            0.0F,
            0.0F,
            GorePresentationMode::Disabled,
        }));
    CHECK(
        colossus.state().blood_level >=
        2U);
    CHECK(
        colossus.state()
            .bleeding_intensity >
        0.0F);
}

TEST_CASE("chaque membre conserve sa resistance locale propre") {
    struct ExpectedLimb {
        DamageZoneId zone_id = 0U;
        float resistance = 0.0F;
    };
    constexpr std::array<ExpectedLimb, 6U>
        kExpected {{
            {kColossusLeftArmZone, 65.0F},
            {kColossusRightArmZone, 75.0F},
            {kColossusLeftLegZone, 85.0F},
            {kColossusRightLegZone, 85.0F},
            {kColossusHornZone, 45.0F},
            {kColossusHeadZone, 90.0F},
        }};

    ChainedColossus colossus {};
    colossus.reset(glm::vec3 {0.0F}, 61U);
    colossus.release();

    for (const auto& expected : kExpected) {
        const auto before =
            limb_view(
                colossus,
                expected.zone_id);
        REQUIRE(before.zone_id == expected.zone_id);
        CHECK(
            before.maximum_resistance ==
            doctest::Approx(expected.resistance));
        CHECK(
            before.remaining_resistance ==
            doctest::Approx(expected.resistance));

        const auto hit =
            colossus.apply_hit({
                expected.zone_id,
                1.0F,
                0.0F,
                0.0F,
            });
        REQUIRE(hit.accepted);
        const auto after =
            limb_view(
                colossus,
                expected.zone_id);
        CHECK(
            after.remaining_resistance <
            before.remaining_resistance);
        CHECK(
            after.maximum_resistance ==
            doctest::Approx(
                before.maximum_resistance));
    }
}

TEST_CASE("un membre ne se detache ni avant epuisement ni avant sa phase") {
    ChainedColossus colossus {};
    colossus.reset(glm::vec3 {0.0F}, 63U);
    colossus.release();

    const auto too_early =
        colossus.apply_hit({
            kColossusLeftArmZone,
            1.0F,
            0.0F,
            70.0F,
        });
    CHECK_FALSE(too_early.limb_severed);
    CHECK(
        too_early.dismemberment.reason ==
        DismembermentBlockReason::
            LocalResistanceRemaining);

    const auto exhausted =
        exhaust_zone(
            colossus,
            kColossusLeftArmZone);
    REQUIRE(
        exhausted.zone.condition ==
        DamageZoneCondition::Depleted);
    const auto phase_locked =
        colossus.apply_hit({
            kColossusLeftArmZone,
            0.0F,
            0.0F,
            70.0F,
        });
    CHECK_FALSE(phase_locked.limb_severed);
    CHECK(
        phase_locked.dismemberment.reason ==
        DismembermentBlockReason::PhaseLocked);

    static_cast<void>(
        colossus.apply_hit({
            kColossusTorsoZone,
            150.0F,
            0.0F,
            0.0F,
        }));
    const auto allowed =
        colossus.apply_hit({
            kColossusLeftArmZone,
            0.0F,
            0.0F,
            70.0F,
        });
    CHECK(allowed.limb_severed);
}

TEST_CASE("l'intelligence artificielle respecte la portee de chaque attaque") {
    ChainedColossus medium {};
    medium.reset(glm::vec3 {0.0F}, 4U);
    medium.release();
    const auto medium_event =
        advance_until_attack_event(
            medium,
            glm::vec3 {0.0F, 0.0F, 3.0F});
    REQUIRE(medium_event.has_value());
    CHECK(
        medium_event->attack ==
        ChainedColossusAttack::ArmSweep);
    CHECK(
        medium_event->attack !=
        ChainedColossusAttack::
            ShoulderBash);

    ChainedColossus distant {};
    distant.reset(glm::vec3 {0.0F}, 66U);
    distant.release();
    static_cast<void>(
        distant.apply_hit({
            kColossusTorsoZone,
            240.0F,
            0.0F,
            0.0F,
        }));
    const auto wounded =
        exhaust_zone(
            distant,
            kColossusRightLegZone);
    REQUIRE(
        wounded.zone.condition ==
        DamageZoneCondition::Depleted);
    const auto before =
        distant.state().position;
    const auto distant_event =
        advance_until_attack_event(
            distant,
            glm::vec3 {0.0F, 0.0F, 20.0F},
            3.0F);
    CHECK_FALSE(distant_event.has_value());
    CHECK(
        distant.state().position.z >
        before.z);
}

TEST_CASE("la perte du bras droit affaiblit les attaques restantes") {
    ChainedColossus intact {};
    intact.reset(glm::vec3 {0.0F}, 10U);
    intact.release();
    const auto intact_event =
        advance_until_attack_event(
            intact,
            glm::vec3 {0.0F, 0.0F, 3.0F});
    REQUIRE(intact_event.has_value());
    REQUIRE(
        intact_event->attack ==
        ChainedColossusAttack::ArmSweep);
    CHECK(
        intact_event->damage ==
        doctest::Approx(3.0F));

    ChainedColossus weakened {};
    weakened.reset(glm::vec3 {0.0F}, 10U);
    weakened.release();
    static_cast<void>(
        weakened.apply_hit({
            kColossusTorsoZone,
            150.0F,
            0.0F,
            0.0F,
        }));
    const auto sever =
        section_limb(
            weakened,
            kColossusRightArmZone);
    REQUIRE(sever.limb_severed);
    const auto weakened_event =
        advance_until_attack_event(
            weakened,
            glm::vec3 {0.0F, 0.0F, 3.0F});
    REQUIRE(weakened_event.has_value());
    REQUIRE(
        weakened_event->attack ==
        ChainedColossusAttack::ArmSweep);
    CHECK(
        weakened_event->damage ==
        doctest::Approx(
            3.0F *
            kChainedColossusRightArmDamageMultiplier));
    CHECK(
        weakened_event->damage <
        intact_event->damage);
}

TEST_CASE("une requete de degats invalide est rejetee atomiquement") {
    ChainedColossus colossus {};
    colossus.reset(glm::vec3 {0.0F}, 68U);
    colossus.release();
    const auto state_before =
        colossus.state();
    const auto stagger_before =
        colossus.stagger_state();
    const auto limbs_before =
        colossus.limb_views();

    const std::array<ColossusHitRequest, 5U>
        invalid_requests {{
            {
                kColossusLeftArmZone,
                kMaximumDamageZoneInput + 1.0F,
                0.0F,
                0.0F,
            },
            {
                kColossusLeftArmZone,
                0.0F,
                kMaximumDamageZoneInput + 1.0F,
                0.0F,
            },
            {
                kColossusLeftArmZone,
                0.0F,
                0.0F,
                kMaximumSeveringPower + 1.0F,
            },
            {
                kColossusLeftArmZone,
                1.0F,
                0.0F,
                0.0F,
                static_cast<
                    GorePresentationMode>(255U),
            },
            {
                kColossusLeftArmZone,
                std::numeric_limits<
                    float>::quiet_NaN(),
                0.0F,
                0.0F,
            },
        }};

    for (const auto& request :
         invalid_requests) {
        const auto rejected =
            colossus.apply_hit(request);
        CHECK_FALSE(rejected.accepted);
        CHECK(
            rejected.failure ==
            ColossusHitFailure::
                InvalidRequest);
        CHECK(colossus.state() == state_before);
        CHECK(
            colossus.stagger_state() ==
            stagger_before);

        const auto limbs_after =
            colossus.limb_views();
        for (std::size_t index = 0U;
             index < limbs_before.size();
             ++index) {
            CHECK(
                limbs_after[index].zone_id ==
                limbs_before[index].zone_id);
            CHECK(
                limbs_after[index]
                    .remaining_resistance ==
                limbs_before[index]
                    .remaining_resistance);
            CHECK(
                limbs_after[index].part_state ==
                limbs_before[index].part_state);
            CHECK(
                limbs_after[index].armor ==
                limbs_before[index].armor);
        }
    }

    const auto unknown =
        colossus.apply_hit({
            static_cast<DamageZoneId>(999U),
            1.0F,
            0.0F,
            0.0F,
        });
    CHECK_FALSE(unknown.accepted);
    CHECK(
        unknown.failure ==
        ColossusHitFailure::UnknownZone);
    CHECK(colossus.state() == state_before);
}

TEST_CASE("l'execution exige strictement moins de dix pour cent de vie") {
    ChainedColossus colossus {};
    colossus.reset(glm::vec3 {0.0F}, 71U);
    colossus.release();

    const auto head =
        exhaust_zone(
            colossus,
            kColossusHeadZone);
    REQUIRE(
        head.zone.condition ==
        DamageZoneCondition::Depleted);
    const auto armor_break =
        colossus.apply_hit({
            kColossusTorsoZone,
            300.0F,
            0.0F,
            0.0F,
        });
    REQUIRE(armor_break.armor_broken_now);

    const auto threshold =
        kChainedColossusMaximumHealth *
        kChainedColossusExecutionHealthRatio;
    REQUIRE(
        colossus.state().health >
        threshold);
    static_cast<void>(
        colossus.apply_hit({
            kColossusTorsoZone,
            colossus.state().health -
                threshold,
            0.0F,
            0.0F,
        }));
    REQUIRE(
        colossus.state().health ==
        doctest::Approx(threshold));

    const auto stagger =
        colossus.apply_hit({
            kColossusTorsoZone,
            0.0F,
            150.0F,
            0.0F,
        });
    REQUIRE(stagger.stagger_triggered);
    CHECK_FALSE(colossus.can_execute());
    const auto boundary_attempt =
        colossus.apply_hit({
            kColossusHeadZone,
            0.0F,
            0.0F,
            70.0F,
            GorePresentationMode::Full,
            true,
            true,
        });
    CHECK_FALSE(
        boundary_attempt.limb_severed);
    CHECK(
        boundary_attempt.dismemberment
            .reason ==
        DismembermentBlockReason::PhaseLocked);

    static_cast<void>(
        colossus.apply_hit({
            kColossusTorsoZone,
            0.1F,
            0.0F,
            0.0F,
        }));
    REQUIRE(
        colossus.state().health <
        threshold);
    REQUIRE(colossus.can_execute());
    const auto execution =
        colossus.apply_hit({
            kColossusHeadZone,
            0.0F,
            0.0F,
            70.0F,
            GorePresentationMode::Full,
            true,
            true,
        });
    CHECK(execution.execution_completed);
    CHECK(execution.killed);
}

TEST_CASE("la chronologie de mort continue sans reactiver le boss") {
    ChainedColossus colossus {};
    colossus.reset(glm::vec3 {2.0F, 0.0F, 3.0F}, 72U);
    colossus.release();
    const auto killed =
        colossus.apply_hit({
            kColossusTorsoZone,
            1000.0F,
            0.0F,
            0.0F,
        });
    REQUIRE(killed.killed);

    const auto position =
        colossus.state().position;
    const auto animation_before =
        colossus.state().animation_seconds;
    colossus.update(
        1.0F,
        glm::vec3 {20.0F, 0.0F, 20.0F});
    CHECK(
        colossus.state()
            .death_elapsed_seconds ==
        doctest::Approx(0.25F));
    CHECK(
        colossus.state().animation_seconds ==
        doctest::Approx(
            animation_before + 0.25F));

    colossus.update(
        0.25F,
        glm::vec3 {20.0F, 0.0F, 20.0F});
    CHECK(
        colossus.state()
            .death_elapsed_seconds ==
        doctest::Approx(0.50F));
    CHECK(colossus.state().position == position);
    CHECK(
        colossus.state().attack ==
        ChainedColossusAttack::None);
    CHECK(
        colossus.consume_attack_events()
            .empty());
}

TEST_CASE("les deux jambes produisent des locomotions distinctes et deterministes") {
    ChainedColossus left_a {};
    ChainedColossus left_b {};
    for (auto* colossus :
         {&left_a, &left_b}) {
        colossus->reset(
            glm::vec3 {0.0F},
            70U);
        colossus->release();
        static_cast<void>(
            colossus->apply_hit({
                kColossusTorsoZone,
                240.0F,
                0.0F,
                0.0F,
            }));
        const auto depleted =
            exhaust_zone(
                *colossus,
                kColossusLeftLegZone);
        REQUIRE(
            depleted.zone.condition ==
            DamageZoneCondition::Depleted);
        REQUIRE(
            colossus->state().locomotion ==
            ChainedColossusLocomotion::
                LeftLegLimp);
    }

    auto first_fall_tick = -1;
    auto saw_fall = false;
    for (auto index = 0;
         index < 180;
         ++index) {
        left_a.update(
            1.0F / 60.0F,
            glm::vec3 {0.0F, 0.0F, 20.0F});
        left_b.update(
            1.0F / 60.0F,
            glm::vec3 {0.0F, 0.0F, 20.0F});
        if (left_a.state()
                .left_leg_fall_count >
                0U &&
            first_fall_tick < 0) {
            first_fall_tick = index;
        }
        saw_fall =
            saw_fall ||
            left_a.state().locomotion ==
                ChainedColossusLocomotion::
                    LeftLegFall;
        CHECK(
            left_a.state()
                .left_leg_fall_count ==
            left_b.state()
                .left_leg_fall_count);
        CHECK(
            left_a.state().position ==
            left_b.state().position);
    }
    CHECK(first_fall_tick >= 0);
    CHECK(saw_fall);
    CHECK(
        left_a.state()
            .left_leg_fall_count ==
        1U);

    ChainedColossus right {};
    right.reset(glm::vec3 {0.0F}, 70U);
    right.release();
    static_cast<void>(
        right.apply_hit({
            kColossusTorsoZone,
            240.0F,
            0.0F,
            0.0F,
        }));
    const auto right_depleted =
        exhaust_zone(
            right,
            kColossusRightLegZone);
    REQUIRE(
        right_depleted.zone.condition ==
        DamageZoneCondition::Depleted);
    for (auto index = 0;
         index < 30;
         ++index) {
        right.update(
            1.0F / 60.0F,
            glm::vec3 {0.0F, 0.0F, 20.0F});
    }
    CHECK(
        right.state().locomotion ==
        ChainedColossusLocomotion::
            RightLegLimp);
    CHECK(
        right.state().left_leg_fall_count ==
        0U);
    CHECK(
        std::abs(
            right.state().position.x) >
        0.001F);
    CHECK(right.state().position.z > 0.0F);
}

TEST_CASE("la phase quatre degrade la precision de facon bornee et reproductible") {
    ChainedColossus first {};
    ChainedColossus second {};
    for (auto* colossus :
         {&first, &second}) {
        colossus->reset(
            glm::vec3 {0.0F},
            77U);
        colossus->release();
        static_cast<void>(
            colossus->apply_hit({
                kColossusTorsoZone,
                420.0F,
                0.0F,
                0.0F,
            }));
        REQUIRE(
            colossus->state().phase ==
            ChainedColossusPhase::
                PhaseFour);
    }

    for (auto index = 0;
         index < 90 &&
         first.state().attack ==
             ChainedColossusAttack::None;
         ++index) {
        first.update(
            1.0F / 60.0F,
            glm::vec3 {0.0F, 0.0F, 2.5F});
        second.update(
            1.0F / 60.0F,
            glm::vec3 {0.0F, 0.0F, 2.5F});
    }
    REQUIRE(
        first.state().attack !=
        ChainedColossusAttack::None);
    REQUIRE(
        second.state().attack ==
        first.state().attack);
    CHECK(
        first.state()
            .attack_aim_error_radians ==
        second.state()
            .attack_aim_error_radians);
    CHECK(
        first.state()
            .locked_attack_direction ==
        second.state()
            .locked_attack_direction);

    const auto absolute_error =
        std::abs(
            first.state()
                .attack_aim_error_radians);
    CHECK(
        absolute_error >=
        kChainedColossusPhaseFourMaximumAimErrorRadians *
            0.50F);
    CHECK(
        absolute_error <=
        kChainedColossusPhaseFourMaximumAimErrorRadians);
    const auto readability =
        glm::dot(
            first.state()
                .locked_attack_direction,
            glm::vec3 {0.0F, 0.0F, 1.0F});
    CHECK(
        readability >=
        std::cos(
            kChainedColossusPhaseFourMaximumAimErrorRadians));
}

TEST_CASE("le boss continue avec plusieurs membres absents sans attaque impossible") {
    ChainedColossus colossus {};
    colossus.reset(glm::vec3 {0.0F}, 73U);
    colossus.release();
    static_cast<void>(
        colossus.apply_hit({
            kColossusTorsoZone,
            150.0F,
            0.0F,
            0.0F,
        }));

    REQUIRE(
        section_limb(
            colossus,
            kColossusLeftArmZone)
            .limb_severed);
    REQUIRE(
        section_limb(
            colossus,
            kColossusRightArmZone)
            .limb_severed);
    REQUIRE(
        section_limb(
            colossus,
            kColossusLeftLegZone)
            .limb_severed);
    REQUIRE(
        section_limb(
            colossus,
            kColossusRightLegZone)
            .limb_severed);
    REQUIRE(
        colossus.state().phase !=
        ChainedColossusPhase::Dead);

    CHECK_FALSE(
        colossus.attack_available(
            ChainedColossusAttack::
                ArmSweep));
    CHECK_FALSE(
        colossus.attack_available(
            ChainedColossusAttack::
                ChainSlam));
    CHECK_FALSE(
        colossus.attack_available(
            ChainedColossusAttack::
                SlowCharge));

    const auto distant_event =
        advance_until_attack_event(
            colossus,
            glm::vec3 {0.0F, 0.0F, 20.0F},
            4.0F);
    CHECK_FALSE(distant_event.has_value());

    const auto close_player =
        colossus.state().position +
        glm::vec3 {0.0F, 0.0F, 1.5F};
    const auto possible_event =
        advance_until_attack_event(
            colossus,
            close_player,
            8.0F);
    REQUIRE(possible_event.has_value());
    const auto remaining_attack_is_valid =
        possible_event->attack ==
            ChainedColossusAttack::Stomp ||
        possible_event->attack ==
            ChainedColossusAttack::
                ShoulderBash;
    CHECK(remaining_attack_is_valid);
}

TEST_CASE("l'armure et les temporisations du Colosse restent quantifiees") {
    ChainedColossus armored {};
    armored.reset(glm::vec3 {0.0F}, 80U);
    armored.release();
    const auto first =
        armored.apply_hit({
            kColossusTorsoZone,
            100.0F,
            0.0F,
            0.0F,
        });
    CHECK(
        first.health_damage ==
        doctest::Approx(76.0F));
    CHECK(
        armored.state().armor_states[0] ==
        ColossusArmorState::Intact);
    const auto second =
        armored.apply_hit({
            kColossusTorsoZone,
            100.0F,
            0.0F,
            0.0F,
        });
    CHECK(
        second.health_damage ==
        doctest::Approx(76.0F));
    CHECK(
        armored.state().armor_states[0] ==
        ColossusArmorState::Cracked);
    const auto third =
        armored.apply_hit({
            kColossusTorsoZone,
            100.0F,
            0.0F,
            0.0F,
        });
    REQUIRE(third.armor_broken_now);
    CHECK(
        third.health_damage ==
        doctest::Approx(90.0F));
    const auto unarmored =
        armored.apply_hit({
            kColossusTorsoZone,
            10.0F,
            0.0F,
            0.0F,
        });
    CHECK(
        unarmored.health_damage ==
        doctest::Approx(10.0F));

    ChainedColossus timed {};
    timed.reset(glm::vec3 {0.0F}, 10U);
    timed.release();
    for (auto index = 0;
         index < 90 &&
         timed.state().attack ==
             ChainedColossusAttack::None;
         ++index) {
        timed.update(
            1.0F / 60.0F,
            glm::vec3 {0.0F, 0.0F, 2.5F});
    }
    REQUIRE(
        timed.state().attack ==
        ChainedColossusAttack::ArmSweep);
    REQUIRE(
        timed.state().attack_stage ==
        ChainedColossusAttackStage::Windup);
    CHECK(
        timed.consume_attack_events()
            .empty());
    for (auto index = 0;
         index < 50;
         ++index) {
        timed.update(
            1.0F / 60.0F,
            glm::vec3 {0.0F, 0.0F, 2.5F});
    }
    CHECK(
        timed.consume_attack_events()
            .empty());
    const auto first_event =
        advance_until_attack_event(
            timed,
            glm::vec3 {0.0F, 0.0F, 2.5F},
            1.0F);
    REQUIRE(first_event.has_value());

    for (auto index = 0;
         index < 180 &&
         timed.state().attack !=
             ChainedColossusAttack::None;
         ++index) {
        timed.update(
            1.0F / 60.0F,
            glm::vec3 {0.0F, 0.0F, 2.5F});
        static_cast<void>(
            timed.consume_attack_events());
    }
    REQUIRE(
        timed.state().attack ==
        ChainedColossusAttack::None);
    CHECK(
        timed.state()
            .attack_cooldown_seconds ==
        doctest::Approx(2.2F));

    for (auto index = 0;
         index < 120;
         ++index) {
        timed.update(
            1.0F / 60.0F,
            glm::vec3 {0.0F, 0.0F, 2.5F});
    }
    CHECK(
        timed.state().attack ==
        ChainedColossusAttack::None);
    for (auto index = 0;
         index < 20 &&
         timed.state().attack ==
             ChainedColossusAttack::None;
         ++index) {
        timed.update(
            1.0F / 60.0F,
            glm::vec3 {0.0F, 0.0F, 2.5F});
    }
    CHECK(
        timed.state().attack !=
        ChainedColossusAttack::None);
}

} // namespace valcraft
