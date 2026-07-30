#include "gameplay/progression/CommanderAbilityEffects.h"

#include <doctest/doctest.h>

#include <glm/geometric.hpp>

#include <array>
#include <cmath>
#include <limits>
#include <vector>

namespace valcraft {
namespace {

[[nodiscard]] auto target(
    CommanderEntityId id,
    float x,
    float y = 0.0F,
    float z = 0.0F) -> CommanderTarget {
    return {
        id,
        glm::vec3 {x, y, z},
    };
}

} // namespace

TEST_CASE("les quatre capacités du Commandant exposent les valeurs exactes du plan") {
    const auto assault_one =
        assault_order_spec(
            CommanderRank::RankOne);
    const auto assault_two =
        assault_order_spec(
            CommanderRank::RankTwo);
    const auto assault_three =
        assault_order_spec(
            CommanderRank::RankThree);
    REQUIRE(assault_one.has_value());
    REQUIRE(assault_two.has_value());
    REQUIRE(assault_three.has_value());
    CHECK(assault_one->energy_cost == doctest::Approx(10.0F));
    CHECK(assault_one->cooldown_seconds == doctest::Approx(10.0F));
    CHECK(assault_one->duration_seconds == doctest::Approx(6.0F));
    CHECK(assault_one->movement_speed_bonus == doctest::Approx(0.20F));
    CHECK(assault_one->damage_bonus == doctest::Approx(0.15F));
    CHECK(assault_two->cooldown_seconds == doctest::Approx(8.0F));
    CHECK(assault_two->duration_seconds == doctest::Approx(8.0F));
    CHECK(assault_two->movement_speed_bonus == doctest::Approx(0.30F));
    CHECK(assault_two->damage_bonus == doctest::Approx(0.20F));
    CHECK(assault_three->cooldown_seconds == doctest::Approx(6.0F));
    CHECK(assault_three->duration_seconds == doctest::Approx(10.0F));
    CHECK(assault_three->movement_speed_bonus == doctest::Approx(0.40F));
    CHECK(assault_three->damage_bonus == doctest::Approx(0.25F));
    CHECK(assault_three->invocation_vulnerability == doctest::Approx(0.10F));
    CHECK(assault_three->vulnerability_seconds == doctest::Approx(5.0F));

    const auto shooter_one =
        fleet_shooter_spec(
            CommanderRank::RankOne);
    const auto shooter_two =
        fleet_shooter_spec(
            CommanderRank::RankTwo);
    const auto shooter_three =
        fleet_shooter_spec(
            CommanderRank::RankThree);
    REQUIRE(shooter_one.has_value());
    REQUIRE(shooter_two.has_value());
    REQUIRE(shooter_three.has_value());
    CHECK(shooter_one->energy_cost == doctest::Approx(30.0F));
    CHECK(shooter_one->cooldown_seconds == doctest::Approx(24.0F));
    CHECK(shooter_one->duration_seconds == doctest::Approx(20.0F));
    CHECK(shooter_one->base_health == doctest::Approx(9.0F));
    CHECK(shooter_one->range == doctest::Approx(14.0F));
    CHECK(shooter_one->base_damage == doctest::Approx(4.0F));
    CHECK(shooter_one->attack_interval_seconds == doctest::Approx(2.4F));
    CHECK(shooter_two->cooldown_seconds == doctest::Approx(22.0F));
    CHECK(shooter_two->duration_seconds == doctest::Approx(25.0F));
    CHECK(shooter_two->base_health == doctest::Approx(12.0F));
    CHECK(shooter_two->range == doctest::Approx(16.0F));
    CHECK(shooter_two->base_damage == doctest::Approx(5.0F));
    CHECK(shooter_two->attack_interval_seconds == doctest::Approx(2.2F));
    CHECK(shooter_three->cooldown_seconds == doctest::Approx(20.0F));
    CHECK(shooter_three->duration_seconds == doctest::Approx(30.0F));
    CHECK(shooter_three->base_health == doctest::Approx(15.0F));
    CHECK(shooter_three->range == doctest::Approx(18.0F));
    CHECK(shooter_three->base_damage == doctest::Approx(6.0F));
    CHECK(shooter_three->attack_interval_seconds == doctest::Approx(2.0F));
    CHECK(shooter_three->piercing_shot_period == 4U);
    CHECK(shooter_three->piercing_damage_multiplier == doctest::Approx(0.60F));

    const auto banner_one =
        war_banner_spec(
            CommanderRank::RankOne);
    const auto banner_two =
        war_banner_spec(
            CommanderRank::RankTwo);
    const auto banner_three =
        war_banner_spec(
            CommanderRank::RankThree);
    REQUIRE(banner_one.has_value());
    REQUIRE(banner_two.has_value());
    REQUIRE(banner_three.has_value());
    CHECK(banner_one->energy_cost == doctest::Approx(30.0F));
    CHECK(banner_one->cooldown_seconds == doctest::Approx(28.0F));
    CHECK(banner_one->duration_seconds == doctest::Approx(12.0F));
    CHECK(banner_one->radius == doctest::Approx(6.0F));
    CHECK(banner_one->base_health == doctest::Approx(12.0F));
    CHECK(banner_one->ally_damage_bonus == doctest::Approx(0.10F));
    CHECK(banner_one->invocation_healing_per_second == doctest::Approx(0.25F));
    CHECK(banner_one->player_healing_limit == doctest::Approx(3.0F));
    CHECK(banner_two->cooldown_seconds == doctest::Approx(24.0F));
    CHECK(banner_two->duration_seconds == doctest::Approx(15.0F));
    CHECK(banner_two->radius == doctest::Approx(8.0F));
    CHECK(banner_two->base_health == doctest::Approx(16.0F));
    CHECK(banner_two->ally_damage_bonus == doctest::Approx(0.15F));
    CHECK(banner_two->invocation_healing_per_second == doctest::Approx(0.50F));
    CHECK(banner_two->player_healing_limit == doctest::Approx(4.0F));
    CHECK(banner_three->cooldown_seconds == doctest::Approx(20.0F));
    CHECK(banner_three->duration_seconds == doctest::Approx(18.0F));
    CHECK(banner_three->radius == doctest::Approx(10.0F));
    CHECK(banner_three->base_health == doctest::Approx(20.0F));
    CHECK(banner_three->ally_damage_bonus == doctest::Approx(0.20F));
    CHECK(banner_three->invocation_healing_per_second == doctest::Approx(0.75F));
    CHECK(banner_three->player_healing_limit == doctest::Approx(5.0F));

    const auto rampart_one =
        rampart_formation_spec(
            CommanderRank::RankOne);
    const auto rampart_two =
        rampart_formation_spec(
            CommanderRank::RankTwo);
    const auto rampart_three =
        rampart_formation_spec(
            CommanderRank::RankThree);
    REQUIRE(rampart_one.has_value());
    REQUIRE(rampart_two.has_value());
    REQUIRE(rampart_three.has_value());
    CHECK(rampart_one->energy_cost == doctest::Approx(25.0F));
    CHECK(rampart_one->cooldown_seconds == doctest::Approx(20.0F));
    CHECK(rampart_one->duration_seconds == doctest::Approx(6.0F));
    CHECK(rampart_one->frontal_unit_damage_reduction == doctest::Approx(0.25F));
    CHECK(rampart_one->protected_ally_damage_reduction == doctest::Approx(0.15F));
    CHECK(rampart_two->cooldown_seconds == doctest::Approx(18.0F));
    CHECK(rampart_two->duration_seconds == doctest::Approx(8.0F));
    CHECK(rampart_two->frontal_unit_damage_reduction == doctest::Approx(0.35F));
    CHECK(rampart_two->protected_ally_damage_reduction == doctest::Approx(0.25F));
    CHECK(rampart_three->cooldown_seconds == doctest::Approx(16.0F));
    CHECK(rampart_three->duration_seconds == doctest::Approx(10.0F));
    CHECK(rampart_three->frontal_unit_damage_reduction == doctest::Approx(0.45F));
    CHECK(rampart_three->protected_ally_damage_reduction == doctest::Approx(0.30F));
    CHECK(rampart_three->crossing_projectile_block_chance == doctest::Approx(0.60F));

    const auto invalid =
        static_cast<CommanderRank>(0U);
    CHECK_FALSE(commander_rank_is_valid(invalid));
    CHECK_FALSE(assault_order_spec(invalid).has_value());
    CHECK_FALSE(fleet_shooter_spec(invalid).has_value());
    CHECK_FALSE(war_banner_spec(invalid).has_value());
    CHECK_FALSE(rampart_formation_spec(invalid).has_value());
}

TEST_CASE("les multiplicateurs d'invocation suivent le niveau et la Sagesse bornée") {
    CHECK(
        commander_summon_health_multiplier(
            1U,
            0U) ==
        doctest::Approx(1.0F));
    CHECK(
        commander_summon_damage_multiplier(
            1U,
            0U) ==
        doctest::Approx(1.0F));
    CHECK(
        commander_summon_health_multiplier(
            50U,
            10U) ==
        doctest::Approx(1.445F));
    CHECK(
        commander_summon_damage_multiplier(
            50U,
            10U) ==
        doctest::Approx(1.347F));
    CHECK(
        commander_summon_health_multiplier(
            500U,
            255U) ==
        doctest::Approx(1.795F));
    CHECK(
        commander_summon_damage_multiplier(
            500U,
            255U) ==
        doctest::Approx(1.597F));
    CHECK(
        commander_summon_health_multiplier(
            0U,
            15U) ==
        doctest::Approx(1.0F));
}

TEST_CASE("Ordre Assaut valide sa cible avant tout changement d'état") {
    AssaultOrderSystem system {};
    AssaultOrderCallbacks callbacks {};
    callbacks.validate_target =
        [](const CommanderTarget&) {
            return false;
        };

    const auto rejected =
        system.activate(
            {
                CommanderRank::RankOne,
                false,
                target(10U, 2.0F),
                {},
            },
            callbacks);
    CHECK_FALSE(rejected.activated);
    CHECK(rejected.error == CommanderEffectError::InvalidTarget);
    CHECK_FALSE(system.state().active);

    callbacks.validate_target =
        [](const CommanderTarget&) {
            return true;
        };
    callbacks.dispatch_order =
        [](const AssaultOrderDispatch&) {
            return false;
        };
    const auto callback_rejected =
        system.activate(
            {
                CommanderRank::RankOne,
                false,
                target(10U, 2.0F),
                {},
            },
            callbacks);
    CHECK_FALSE(callback_rejected.activated);
    CHECK(callback_rejected.error == CommanderEffectError::CallbackRejected);
    CHECK_FALSE(system.state().active);

    const auto nan =
        std::numeric_limits<float>::quiet_NaN();
    const auto invalid_position =
        system.activate(
            {
                CommanderRank::RankOne,
                false,
                std::nullopt,
                {nan, 0.0F, 0.0F},
            },
            {});
    CHECK_FALSE(invalid_position.activated);
    CHECK(invalid_position.error == CommanderEffectError::InvalidPosition);
}

TEST_CASE("Ordre Assaut applique ses bonus et une vulnérabilité par unité au rang trois") {
    AssaultOrderSystem system {};
    std::vector<AssaultVulnerabilityRequest> vulnerabilities {};
    AssaultOrderCallbacks callbacks {};
    callbacks.apply_invocation_vulnerability =
        [&](const AssaultVulnerabilityRequest& request) {
            vulnerabilities.push_back(request);
        };

    const auto activation =
        system.activate(
            {
                CommanderRank::RankThree,
                false,
                target(100U, 5.0F),
                {},
            },
            callbacks);
    REQUIRE(activation.activated);
    const auto active =
        system.state();
    CHECK(active.movement_speed_bonus == doctest::Approx(0.40F));
    CHECK(active.damage_bonus == doctest::Approx(0.25F));
    CHECK(active.remaining_seconds == doctest::Approx(10.0F));

    for (CommanderUnitId unit_id = 1U;
         unit_id <= kCommanderMaximumCombatUnits;
         ++unit_id) {
        CHECK(
            system.notify_unit_attack(
                      unit_id,
                      200U + unit_id,
                      callbacks)
                .vulnerability_applied);
        CHECK_FALSE(
            system.notify_unit_attack(
                      unit_id,
                      300U + unit_id,
                      callbacks)
                .vulnerability_applied);
    }
    CHECK_FALSE(
        system.notify_unit_attack(
                  99U,
                  999U,
                  callbacks)
            .vulnerability_applied);
    REQUIRE(
        vulnerabilities.size() ==
        kCommanderMaximumCombatUnits);
    for (const auto& vulnerability :
         vulnerabilities) {
        CHECK(vulnerability.amount == doctest::Approx(0.10F));
        CHECK(vulnerability.duration_seconds == doctest::Approx(5.0F));
    }
}

TEST_CASE("Poursuite victorieuse ne prolonge et ne rembourse qu'une fois") {
    AssaultOrderSystem system {};
    std::size_t retarget_searches = 0U;
    std::size_t retarget_dispatches = 0U;
    float refunded = 0.0F;
    AssaultOrderCallbacks callbacks {};
    callbacks.acquire_replacement_target =
        [&](const AssaultOrderRetargetRequest& request)
        -> std::optional<CommanderTarget> {
        ++retarget_searches;
        CHECK(request.search_radius == doctest::Approx(8.0F));
        return target(20U, 7.0F);
    };
    callbacks.dispatch_order =
        [&](const AssaultOrderDispatch& dispatch) {
            if (dispatch.retarget) {
                ++retarget_dispatches;
            }
            return true;
        };
    callbacks.refund_energy =
        [&](float amount) {
            refunded += amount;
        };

    REQUIRE(
        system.activate(
                  {
                      CommanderRank::RankThree,
                      true,
                      target(10U, 0.0F),
                      {},
                  },
                  callbacks)
            .activated);
    CHECK_FALSE(system.update(4.0F, callbacks));
    const auto retargeted =
        system.notify_target_defeated(
            10U,
            {0.0F, 0.0F, 0.0F},
            callbacks);
    REQUIRE(retargeted.retargeted);
    CHECK(retargeted.duration_extended);
    CHECK(retargeted.energy_refund == doctest::Approx(5.0F));
    CHECK(system.state().remaining_seconds == doctest::Approx(9.0F));
    CHECK(system.state().target->entity_id == 20U);
    CHECK(refunded == doctest::Approx(5.0F));
    CHECK(retarget_searches == 1U);
    CHECK(retarget_dispatches == 1U);

    CHECK_FALSE(
        system.notify_target_defeated(
                  20U,
                  {7.0F, 0.0F, 0.0F},
                  callbacks)
            .retargeted);
    CHECK(retarget_searches == 1U);
    CHECK(refunded == doctest::Approx(5.0F));
}

TEST_CASE("Poursuite victorieuse refuse une cible trouvée hors du rayon") {
    AssaultOrderSystem system {};
    AssaultOrderCallbacks callbacks {};
    callbacks.acquire_replacement_target =
        [](const AssaultOrderRetargetRequest&)
        -> std::optional<CommanderTarget> {
        return target(20U, 8.01F);
    };
    REQUIRE(
        system.activate(
                  {
                      CommanderRank::RankOne,
                      true,
                      target(10U, 0.0F),
                      {},
                  },
                  callbacks)
            .activated);
    const auto result =
        system.notify_target_defeated(
            10U,
            {0.0F, 0.0F, 0.0F},
            callbacks);
    CHECK(result.handled);
    CHECK_FALSE(result.retargeted);
    CHECK(system.state().remaining_seconds == doctest::Approx(6.0F));
    CHECK(system.state().mastery_retarget_used);
}

TEST_CASE("Ordre Assaut expire pareil en un pas ou à soixante hertz") {
    AssaultOrderSystem single {};
    AssaultOrderSystem split {};
    REQUIRE(
        single.activate(
                  {
                      CommanderRank::RankOne,
                      false,
                      std::nullopt,
                      {1.0F, 0.0F, 1.0F},
                  },
                  {})
            .activated);
    REQUIRE(
        split.activate(
                 {
                     CommanderRank::RankOne,
                     false,
                     std::nullopt,
                     {1.0F, 0.0F, 1.0F},
                 },
                 {})
            .activated);
    CHECK(single.update(6.0F, {}));
    for (auto tick = 0U;
         tick < 359U;
         ++tick) {
        CHECK_FALSE(
            split.update(
                1.0F / 60.0F,
                {}));
    }
    CHECK(
        split.update(
            1.0F / 60.0F,
            {}));
    CHECK_FALSE(single.state().active);
    CHECK_FALSE(split.state().active);
}

TEST_CASE("Tireur de la flotte applique les formules et la limite de huit invocations") {
    FleetShooterSystem limited {};
    const auto rejected =
        limited.summon(
            {
                1U,
                {0.0F, 0.0F, 0.0F},
                CommanderRank::RankOne,
                1U,
                0U,
                8U,
                false,
            },
            {});
    CHECK_FALSE(rejected.activated);
    CHECK(rejected.error == CommanderEffectError::LimitReached);

    FleetShooterSystem shooter {};
    const auto summoned =
        shooter.summon(
            {
                7U,
                {1.0F, 2.0F, 3.0F},
                CommanderRank::RankThree,
                50U,
                10U,
                7U,
                true,
            },
            {});
    REQUIRE(summoned.activated);
    const auto state =
        shooter.state();
    CHECK(state.maximum_health == doctest::Approx(21.68F));
    CHECK(state.health == doctest::Approx(21.68F));
    CHECK(state.damage == doctest::Approx(8.08F));
    CHECK(state.range == doctest::Approx(18.0F));
    CHECK(state.remaining_seconds == doctest::Approx(30.0F));
    CHECK(state.first_salvo_available);

    const auto duplicate =
        shooter.summon(
            {
                7U,
                {},
                CommanderRank::RankOne,
                1U,
                0U,
                0U,
                false,
            },
            {});
    CHECK_FALSE(duplicate.activated);
    CHECK(duplicate.error == CommanderEffectError::AlreadyActive);
}

TEST_CASE("Tireur de la flotte respecte la priorité de cible et Première salve") {
    FleetShooterSystem shooter {};
    REQUIRE(
        shooter.summon(
                   {
                       1U,
                       {},
                       CommanderRank::RankOne,
                       1U,
                       0U,
                       0U,
                       true,
                   },
                   {})
            .activated);

    std::vector<FleetShooterTargetPriority> priorities {};
    FleetShooterFireRequest fired {};
    FleetShooterCallbacks callbacks {};
    callbacks.acquire_target =
        [&](const FleetShooterAcquireRequest& request)
        -> std::optional<CommanderTarget> {
        priorities.push_back(request.priority);
        if (request.priority ==
            FleetShooterTargetPriority::MarkedByOrder) {
            return target(9U, 100.0F);
        }
        if (request.priority ==
            FleetShooterTargetPriority::AttackingPlayer) {
            return target(10U, 10.0F);
        }
        return target(11U, 2.0F);
    };
    callbacks.fire_shot =
        [&](const FleetShooterFireRequest& request) {
            fired = request;
            return FleetShooterFireResult {
                true,
                true,
                false,
                false,
                false,
                request.damage,
                0.0F,
            };
        };

    const auto update =
        shooter.update(
            2.4F,
            callbacks);
    REQUIRE(update.shot_count == 1U);
    REQUIRE(priorities.size() == 2U);
    CHECK(priorities[0U] == FleetShooterTargetPriority::MarkedByOrder);
    CHECK(priorities[1U] == FleetShooterTargetPriority::AttackingPlayer);
    CHECK(fired.target.entity_id == 10U);
    CHECK(fired.damage == doctest::Approx(8.0F));
    CHECK(fired.interrupts_light_target);
    CHECK_FALSE(fired.pierces_first_target);
    CHECK_FALSE(shooter.state().first_salvo_available);
}

TEST_CASE("Tireur de rang trois traverse chaque quatrième tir à soixante pour cent") {
    FleetShooterSystem shooter {};
    REQUIRE(
        shooter.summon(
                   {
                       1U,
                       {},
                       CommanderRank::RankThree,
                       1U,
                       0U,
                       0U,
                       false,
                   },
                   {})
            .activated);
    FleetShooterCallbacks callbacks {};
    callbacks.acquire_target =
        [](const FleetShooterAcquireRequest&)
        -> std::optional<CommanderTarget> {
        return target(10U, 2.0F);
    };
    callbacks.fire_shot =
        [](const FleetShooterFireRequest& request) {
            return FleetShooterFireResult {
                true,
                true,
                false,
                request.pierces_first_target,
                false,
                request.damage,
                request.pierces_first_target
                    ? request.damage *
                          request.secondary_damage_multiplier
                    : 0.0F,
            };
        };

    const auto update =
        shooter.update(
            8.0F,
            callbacks);
    REQUIRE(update.shot_count == 4U);
    for (std::size_t index = 0U;
         index < 3U;
         ++index) {
        CHECK_FALSE(update.shots[index].piercing_shot);
    }
    CHECK(update.shots[3U].shot_number == 4U);
    CHECK(update.shots[3U].piercing_shot);
    CHECK(
        update.shots[3U]
                .result
                .secondary_applied_damage ==
            doctest::Approx(3.6F));
}

TEST_CASE("Première salve reste disponible tant qu'aucun tir n'est réellement effectué") {
    FleetShooterSystem shooter {};
    REQUIRE(
        shooter.summon(
                   {
                       1U,
                       {},
                       CommanderRank::RankOne,
                       1U,
                       0U,
                       0U,
                       true,
                   },
                   {})
            .activated);
    FleetShooterCallbacks callbacks {};
    callbacks.acquire_target =
        [](const FleetShooterAcquireRequest&)
        -> std::optional<CommanderTarget> {
        return target(10U, 2.0F);
    };
    callbacks.fire_shot =
        [](const FleetShooterFireRequest&) {
            return FleetShooterFireResult {};
        };
    REQUIRE(
        shooter.update(
                   2.4F,
                   callbacks)
                .shot_count ==
            1U);
    CHECK(shooter.state().shots_fired == 0U);
    CHECK(shooter.state().first_salvo_available);
}

TEST_CASE("Tireur de la flotte assainit les dégâts renvoyés par le monde") {
    FleetShooterSystem shooter {};
    REQUIRE(
        shooter.summon(
                   {
                       1U,
                       {},
                       CommanderRank::RankThree,
                       1U,
                       0U,
                       0U,
                       false,
                   },
                   {})
            .activated);
    FleetShooterCallbacks callbacks {};
    callbacks.acquire_target =
        [](const FleetShooterAcquireRequest&)
        -> std::optional<CommanderTarget> {
        return target(10U, 2.0F);
    };
    callbacks.fire_shot =
        [](const FleetShooterFireRequest&) {
            FleetShooterFireResult result {};
            result.fired = true;
            result.primary_hit = true;
            result.primary_applied_damage =
                std::numeric_limits<float>::quiet_NaN();
            result.secondary_hit = true;
            result.secondary_killed = true;
            result.secondary_applied_damage =
                std::numeric_limits<float>::infinity();
            return result;
        };

    const auto first =
        shooter.update(
            2.0F,
            callbacks);
    REQUIRE(first.shot_count == 1U);
    CHECK(first.shots[0U].result.primary_applied_damage == doctest::Approx(0.0F));
    CHECK_FALSE(first.shots[0U].result.secondary_hit);
    CHECK_FALSE(first.shots[0U].result.secondary_killed);
    CHECK(first.shots[0U].result.secondary_applied_damage == doctest::Approx(0.0F));

    const auto fourth =
        shooter.update(
            6.0F,
            callbacks);
    REQUIRE(fourth.shot_count == 3U);
    REQUIRE(fourth.shots[2U].piercing_shot);
    CHECK(fourth.shots[2U].result.secondary_applied_damage == doctest::Approx(0.0F));
}

TEST_CASE("la cadence du tireur est déterministe entre un grand pas et soixante hertz") {
    FleetShooterSystem single {};
    FleetShooterSystem split {};
    const FleetShooterSpawnRequest request {
        1U,
        {},
        CommanderRank::RankThree,
        1U,
        0U,
        0U,
        false,
    };
    REQUIRE(single.summon(request, {}).activated);
    REQUIRE(split.summon(request, {}).activated);

    auto callback_set = [](
                            std::vector<std::uint32_t>& shots) {
        FleetShooterCallbacks callbacks {};
        callbacks.acquire_target =
            [](const FleetShooterAcquireRequest&)
            -> std::optional<CommanderTarget> {
            return target(10U, 1.0F);
        };
        callbacks.fire_shot =
            [&](const FleetShooterFireRequest& request) {
                shots.push_back(request.shot_number);
                return FleetShooterFireResult {
                    true,
                };
            };
        return callbacks;
    };

    std::vector<std::uint32_t> single_shots {};
    std::vector<std::uint32_t> split_shots {};
    auto single_callbacks =
        callback_set(single_shots);
    auto split_callbacks =
        callback_set(split_shots);
    const auto single_update =
        single.update(
            30.0F,
            single_callbacks);
    CHECK(single_update.expired);
    for (auto tick = 0U;
         tick < 1800U;
         ++tick) {
        static_cast<void>(
            split.update(
                1.0F / 60.0F,
                split_callbacks));
    }
    CHECK(single_shots == split_shots);
    CHECK(single_shots.size() == 15U);
    CHECK_FALSE(single.state().active);
    CHECK_FALSE(split.state().active);
}

TEST_CASE("Bannière de guerre échantillonne uniquement les bénéficiaires prévus") {
    WarBannerSystem banner {};
    REQUIRE(
        banner.place(
                   {
                       1U,
                       {2.0F, 0.0F, 0.0F},
                       CommanderRank::RankThree,
                       1U,
                       0U,
                       true,
                   },
                   {})
            .activated);

    const auto invocation =
        banner.sample_aura(
            {12.0F, 0.0F, 0.0F},
            CommanderAuraRecipient::Invocation);
    CHECK(invocation.inside);
    CHECK(invocation.ally_damage_bonus == doctest::Approx(0.20F));
    CHECK(invocation.invocation_healing_per_second == doctest::Approx(0.75F));
    CHECK(invocation.player_energy_regeneration_bonus == doctest::Approx(0.0F));

    const auto crew =
        banner.sample_aura(
            {2.0F, 0.0F, 0.0F},
            CommanderAuraRecipient::Crew);
    CHECK(crew.ally_damage_bonus == doctest::Approx(0.20F));
    CHECK(crew.invocation_healing_per_second == doctest::Approx(0.0F));

    const auto player =
        banner.sample_aura(
            {2.0F, 0.0F, 0.0F},
            CommanderAuraRecipient::Player);
    CHECK(player.ally_damage_bonus == doctest::Approx(0.0F));
    CHECK(
        player.player_energy_regeneration_bonus ==
        doctest::Approx(0.15F));

    CHECK_FALSE(
        banner.sample_aura(
                  {12.01F, 0.0F, 0.0F},
                  CommanderAuraRecipient::Invocation)
            .inside);
}

TEST_CASE("Bannière borne le soin total du joueur et soigne les invocations par seconde") {
    WarBannerSystem banner {};
    REQUIRE(
        banner.place(
                  {
                      1U,
                      {},
                      CommanderRank::RankThree,
                      1U,
                      0U,
                      false,
                  },
                  {})
            .activated);
    std::vector<WarBannerPulseRequest> pulses {};
    WarBannerCallbacks callbacks {};
    callbacks.apply_healing_pulse =
        [&](const WarBannerPulseRequest& request) {
            pulses.push_back(request);
            return WarBannerPulseResult {
                std::numeric_limits<float>::infinity(),
            };
        };
    const auto rejected_non_finite =
        banner.update(
            1.0F,
            {},
            callbacks);
    CHECK(rejected_non_finite.requested_player_healing == doctest::Approx(0.75F));
    CHECK(rejected_non_finite.applied_player_healing == doctest::Approx(0.0F));

    callbacks.apply_healing_pulse =
        [&](const WarBannerPulseRequest& request) {
            pulses.push_back(request);
            return WarBannerPulseResult {
                request.requested_player_healing + 100.0F,
            };
        };
    const auto remaining =
        banner.update(
            17.0F,
            {},
            callbacks);
    CHECK(remaining.expired);
    CHECK(remaining.requested_player_healing == doctest::Approx(5.0F));
    CHECK(remaining.applied_player_healing == doctest::Approx(5.0F));
    REQUIRE(pulses.size() == 2U);
    CHECK(pulses[0U].invocation_healing_per_unit == doctest::Approx(0.75F));
    CHECK(pulses[1U].invocation_healing_per_unit == doctest::Approx(12.75F));
}

TEST_CASE("une nouvelle Bannière remplace proprement l'ancienne après validation") {
    WarBannerSystem banner {};
    std::vector<CommanderEffectEndReason> reasons {};
    WarBannerCallbacks callbacks {};
    callbacks.validate_placement =
        [](const WarBannerPlacementRequest& request) {
            return request.position.x >= 0.0F;
        };
    callbacks.banner_ended =
        [&](CommanderActivationId, CommanderEffectEndReason reason) {
            reasons.push_back(reason);
        };

    REQUIRE(
        banner.place(
                  {
                      1U,
                      {1.0F, 0.0F, 0.0F},
                      CommanderRank::RankOne,
                      1U,
                      0U,
                      false,
                  },
                  callbacks)
            .activated);
    const auto first_id =
        banner.state().banner_id;
    CHECK_FALSE(
        banner.place(
                  {
                      1U,
                      {-1.0F, 0.0F, 0.0F},
                      CommanderRank::RankTwo,
                      1U,
                      0U,
                      false,
                  },
                  callbacks)
            .activated);
    CHECK(banner.state().banner_id == first_id);
    CHECK(reasons.empty());

    REQUIRE(
        banner.place(
                  {
                      1U,
                      {2.0F, 0.0F, 0.0F},
                      CommanderRank::RankTwo,
                      1U,
                      0U,
                      false,
                  },
                  callbacks)
            .activated);
    CHECK(banner.state().banner_id != first_id);
    REQUIRE(reasons.size() == 1U);
    CHECK(reasons[0U] == CommanderEffectEndReason::Replaced);
}

TEST_CASE("la durabilité de la Bannière est finie, bornée et destructible") {
    WarBannerSystem banner {};
    REQUIRE(
        banner.place(
                  {
                      1U,
                      {},
                      CommanderRank::RankOne,
                      1U,
                      0U,
                      false,
                  },
                  {})
            .activated);
    CHECK_FALSE(
        banner.apply_damage(
                  std::numeric_limits<float>::quiet_NaN(),
                  {})
            .handled);
    CHECK(banner.state().health == doctest::Approx(12.0F));
    const auto destroyed =
        banner.apply_damage(
            50.0F,
            {});
    CHECK(destroyed.handled);
    CHECK(destroyed.destroyed);
    CHECK(destroyed.applied_damage == doctest::Approx(12.0F));
    CHECK_FALSE(banner.state().active);
}

TEST_CASE("Formation du rempart valide une escouade bornée et normalise son orientation") {
    RampartFormationSystem formation {};
    const std::array<CommanderUnitId, 3U> units {
        1U,
        2U,
        3U,
    };
    RampartFormationDispatch dispatched {};
    RampartFormationCallbacks callbacks {};
    callbacks.dispatch_formation =
        [&](const RampartFormationDispatch& request) {
            dispatched = request;
            return true;
        };
    const auto activation =
        formation.activate(
            {
                CommanderRank::RankThree,
                true,
                {2.0F, 0.0F, 3.0F},
                {0.0F, 0.0F, 4.0F},
                units,
                true,
            },
            callbacks);
    REQUIRE(activation.activated);
    CHECK(dispatched.unit_count == 3U);
    CHECK(glm::length(dispatched.forward) == doctest::Approx(1.0F));
    CHECK(formation.state().on_dynamic_ship);
    CHECK(formation.state().remaining_seconds == doctest::Approx(10.0F));

    const std::array<CommanderUnitId, 2U> duplicate {
        1U,
        1U,
    };
    RampartFormationSystem invalid {};
    CHECK_FALSE(
        invalid.activate(
                   {
                       CommanderRank::RankOne,
                       false,
                       {},
                       {0.0F, 0.0F, 1.0F},
                       duplicate,
                       false,
                   },
                   {})
            .activated);

    const std::array<CommanderUnitId, 9U> too_many {
        1U,
        2U,
        3U,
        4U,
        5U,
        6U,
        7U,
        8U,
        9U,
    };
    CHECK(
        invalid.activate(
                   {
                       CommanderRank::RankOne,
                       false,
                       {},
                       {0.0F, 0.0F, 1.0F},
                       too_many,
                       false,
                   },
                   {})
            .error ==
        CommanderEffectError::LimitReached);
}

TEST_CASE("Formation du rempart protège uniquement la face et les alliés derrière la ligne") {
    RampartFormationSystem formation {};
    const std::array<CommanderUnitId, 2U> units {
        10U,
        20U,
    };
    REQUIRE(
        formation.activate(
                     {
                         CommanderRank::RankTwo,
                         false,
                         {},
                         {0.0F, 0.0F, 1.0F},
                         units,
                         false,
                     },
                     {})
            .activated);

    const auto frontal =
        formation.sample_unit_defense(
            10U,
            true);
    CHECK(frontal.in_formation);
    CHECK(frontal.stop_distant_pursuit);
    CHECK(frontal.frontal_damage_reduction == doctest::Approx(0.35F));
    CHECK(
        formation.sample_unit_defense(
                     10U,
                     false)
            .frontal_damage_reduction ==
        doctest::Approx(0.0F));
    CHECK_FALSE(
        formation.sample_unit_defense(
                     99U,
                     true)
            .in_formation);

    const auto protected_ally =
        formation.sample_ally_defense(true);
    CHECK(protected_ally.protected_by_line);
    CHECK(protected_ally.damage_reduction == doctest::Approx(0.25F));
    CHECK_FALSE(
        formation.sample_ally_defense(false)
            .protected_by_line);
}

TEST_CASE("Boucliers levés annule la première attaque à distance de chaque unité") {
    RampartFormationSystem formation {};
    const std::array<CommanderUnitId, 2U> units {
        10U,
        20U,
    };
    REQUIRE(
        formation.activate(
                     {
                         CommanderRank::RankThree,
                         true,
                         {},
                         {0.0F, 0.0F, 1.0F},
                         units,
                         false,
                     },
                     {})
            .activated);

    const auto boss =
        formation.resolve_unit_ranged_attack(
            10U,
            true);
    CHECK(boss.handled);
    CHECK_FALSE(boss.completely_blocked);
    CHECK_FALSE(boss.mastery_consumed);

    const auto first =
        formation.resolve_unit_ranged_attack(
            10U,
            false);
    CHECK(first.completely_blocked);
    CHECK(first.mastery_consumed);
    CHECK_FALSE(
        formation.resolve_unit_ranged_attack(
                      10U,
                      false)
            .completely_blocked);
    CHECK(
        formation.resolve_unit_ranged_attack(
                      20U,
                      false)
            .completely_blocked);
    CHECK_FALSE(
        formation.resolve_unit_ranged_attack(
                      99U,
                      false)
            .handled);
}

TEST_CASE("le rang trois bloque de façon déterministe soixante pour cent des traversées") {
    RampartFormationSystem formation {};
    const std::array<CommanderUnitId, 1U> units {
        10U,
    };
    REQUIRE(
        formation.activate(
                     {
                         CommanderRank::RankThree,
                         false,
                         {},
                         {0.0F, 0.0F, 1.0F},
                         units,
                         false,
                     },
                     {})
            .activated);

    CHECK_FALSE(
        formation.resolve_crossing_projectile(
                     1U,
                     false)
            .handled);
    CHECK_FALSE(
        formation.resolve_crossing_projectile(
                     0U,
                     true)
            .handled);

    std::size_t blocked = 0U;
    for (CommanderProjectileId projectile = 1U;
         projectile <= 10'000U;
         ++projectile) {
        const auto first =
            formation.resolve_crossing_projectile(
                projectile,
                true);
        const auto second =
            formation.resolve_crossing_projectile(
                projectile,
                true);
        REQUIRE(first.handled);
        CHECK(first.block_chance == doctest::Approx(0.60F));
        CHECK(first.blocked == second.blocked);
        blocked += first.blocked ? 1U : 0U;
    }
    CHECK(blocked > 5'850U);
    CHECK(blocked < 6'150U);
}

TEST_CASE("les mises à jour invalides ne contaminent aucun état Commandant") {
    const auto nan =
        std::numeric_limits<float>::quiet_NaN();

    AssaultOrderSystem assault {};
    REQUIRE(
        assault.activate(
                   {
                       CommanderRank::RankOne,
                       false,
                       std::nullopt,
                       {},
                   },
                   {})
            .activated);
    CHECK_FALSE(assault.update(nan, {}));
    CHECK(assault.state().remaining_seconds == doctest::Approx(6.0F));

    FleetShooterSystem shooter {};
    REQUIRE(
        shooter.summon(
                   {
                       1U,
                       {},
                       CommanderRank::RankOne,
                       1U,
                       0U,
                       0U,
                       false,
                   },
                   {})
            .activated);
    CHECK(shooter.update(nan, {}).shot_count == 0U);
    CHECK(shooter.state().remaining_seconds == doctest::Approx(20.0F));
    CHECK_FALSE(shooter.apply_damage(nan, {}).handled);

    WarBannerSystem banner {};
    REQUIRE(
        banner.place(
                  {
                      1U,
                      {},
                      CommanderRank::RankOne,
                      1U,
                      0U,
                      false,
                  },
                  {})
            .activated);
    CHECK_FALSE(banner.update(nan, {}, {}).expired);
    CHECK(banner.state().remaining_seconds == doctest::Approx(12.0F));

    RampartFormationSystem formation {};
    const std::array<CommanderUnitId, 1U> units {
        1U,
    };
    REQUIRE(
        formation.activate(
                     {
                         CommanderRank::RankOne,
                         false,
                         {},
                         {0.0F, 0.0F, 1.0F},
                         units,
                         false,
                     },
                     {})
            .activated);
    CHECK_FALSE(formation.update(nan, {}));
    CHECK(formation.state().remaining_seconds == doctest::Approx(6.0F));
}

} // namespace valcraft
