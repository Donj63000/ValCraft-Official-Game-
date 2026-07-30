#include "gameplay/progression/KnightAdvancedAbilitySystem.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace valcraft {
namespace {

struct ChargeTestContext {
    std::array<KnightChargeStepProbeResult, kKnightMaximumChargeSteps> probes {};
    std::size_t configured_probe_count = 0U;
    std::array<KnightChargeStepProbeRequest, kKnightMaximumChargeSteps> requests {};
    std::size_t probe_count = 0U;
    KnightBulwarkChargeCommitRequest committed {};
    std::size_t commit_count = 0U;
    bool accept_commit = true;
};

auto charge_probe(
    void* user_data,
    const KnightChargeStepProbeRequest& request) noexcept
    -> KnightChargeStepProbeResult {
    auto& context = *static_cast<ChargeTestContext*>(user_data);
    if (context.probe_count < context.requests.size()) {
        context.requests[context.probe_count] = request;
    }
    const auto index = context.probe_count++;
    return index < context.configured_probe_count
               ? context.probes[index]
               : KnightChargeStepProbeResult {};
}

auto charge_commit(
    void* user_data,
    const KnightBulwarkChargeCommitRequest& request) noexcept -> bool {
    auto& context = *static_cast<ChargeTestContext*>(user_data);
    ++context.commit_count;
    context.committed = request;
    return context.accept_commit;
}

struct WallTestContext {
    KnightWallImpactCommitRequest committed {};
    std::size_t commit_count = 0U;
    bool accept = true;
};

auto wall_commit(
    void* user_data,
    const KnightWallImpactCommitRequest& request) noexcept -> bool {
    auto& context = *static_cast<WallTestContext*>(user_data);
    ++context.commit_count;
    context.committed = request;
    return context.accept;
}

struct BreachTestContext {
    KnightBreachMeleeCommitRequest committed {};
    std::size_t commit_count = 0U;
    bool accept = true;
};

auto breach_commit(
    void* user_data,
    const KnightBreachMeleeCommitRequest& request) noexcept -> bool {
    auto& context = *static_cast<BreachTestContext*>(user_data);
    ++context.commit_count;
    context.committed = request;
    return context.accept;
}

struct CryTestContext {
    KnightNearbyQueryResult nearby {};
    KnightNearbyQueryRequest query {};
    KnightChampionCryCommitRequest committed {};
    std::size_t query_count = 0U;
    std::size_t commit_count = 0U;
    bool accept_commit = true;
};

auto cry_query(
    void* user_data,
    const KnightNearbyQueryRequest& request) noexcept
    -> KnightNearbyQueryResult {
    auto& context = *static_cast<CryTestContext*>(user_data);
    ++context.query_count;
    context.query = request;
    return context.nearby;
}

auto cry_commit(
    void* user_data,
    const KnightChampionCryCommitRequest& request) noexcept -> bool {
    auto& context = *static_cast<CryTestContext*>(user_data);
    ++context.commit_count;
    context.committed = request;
    return context.accept_commit;
}

struct RiposteTestContext {
    KnightPerfectRiposteCommitRequest committed {};
    std::size_t commit_count = 0U;
    bool accept = true;
};

auto riposte_commit(
    void* user_data,
    const KnightPerfectRiposteCommitRequest& request) noexcept -> bool {
    auto& context = *static_cast<RiposteTestContext*>(user_data);
    ++context.commit_count;
    context.committed = request;
    return context.accept;
}

[[nodiscard]] auto charge_callbacks(ChargeTestContext& context) noexcept
    -> KnightBulwarkChargeCallbacks {
    return {
        &context,
        &charge_probe,
        &charge_commit,
    };
}

[[nodiscard]] auto cry_callbacks(CryTestContext& context) noexcept
    -> KnightChampionCryCallbacks {
    return {
        &context,
        &cry_query,
        &cry_commit,
    };
}

[[nodiscard]] auto parry_callbacks(RiposteTestContext& context) noexcept
    -> KnightPerfectRiposteCallbacks {
    return {
        &context,
        &riposte_commit,
    };
}

void configure_contact(
    ChargeTestContext& context,
    std::size_t probe_index,
    KnightEntityId target_id,
    KnightTargetWeight weight,
    bool blocks = false) {
    context.configured_probe_count =
        std::max(context.configured_probe_count, probe_index + 1U);
    auto& probe = context.probes[probe_index];
    probe.contacts[probe.contact_count++] = {
        target_id,
        weight,
    };
    probe.enemy_blocks_path = blocks;
}

} // namespace

TEST_CASE("les trois rangs avances du Chevalier gardent le contrat du plan") {
    const auto* charge_one =
        knight_bulwark_charge_definition(KnightAbilityRank::RankOne);
    const auto* charge_two =
        knight_bulwark_charge_definition(KnightAbilityRank::RankTwo);
    const auto* charge_three =
        knight_bulwark_charge_definition(KnightAbilityRank::RankThree);
    REQUIRE(charge_one != nullptr);
    REQUIRE(charge_two != nullptr);
    REQUIRE(charge_three != nullptr);
    CHECK(charge_one->energy_cost == doctest::Approx(18.0F));
    CHECK(charge_one->cooldown_seconds == doctest::Approx(9.0F));
    CHECK(charge_one->distance_meters == doctest::Approx(5.0F));
    CHECK(charge_one->weapon_damage_multiplier == doctest::Approx(0.80F));
    CHECK(charge_one->maximum_targets == 1U);
    CHECK(charge_two->cooldown_seconds == doctest::Approx(8.5F));
    CHECK(charge_two->distance_meters == doctest::Approx(6.0F));
    CHECK(charge_two->weapon_damage_multiplier == doctest::Approx(1.0F));
    CHECK(charge_two->wall_impact_window_seconds == doctest::Approx(1.0F));
    CHECK(charge_two->wall_stun_seconds == doctest::Approx(1.0F));
    CHECK(charge_three->cooldown_seconds == doctest::Approx(8.0F));
    CHECK(charge_three->distance_meters == doctest::Approx(7.0F));
    CHECK(charge_three->weapon_damage_multiplier == doctest::Approx(1.20F));
    CHECK(charge_three->maximum_targets == 3U);

    const auto* cry_one =
        knight_champion_cry_definition(KnightAbilityRank::RankOne);
    const auto* cry_two =
        knight_champion_cry_definition(KnightAbilityRank::RankTwo);
    const auto* cry_three =
        knight_champion_cry_definition(KnightAbilityRank::RankThree);
    REQUIRE(cry_one != nullptr);
    REQUIRE(cry_two != nullptr);
    REQUIRE(cry_three != nullptr);
    CHECK(cry_one->energy_cost == doctest::Approx(25.0F));
    CHECK(cry_one->cooldown_seconds == doctest::Approx(20.0F));
    CHECK(cry_one->radius_meters == doctest::Approx(6.0F));
    CHECK(cry_one->duration_seconds == doctest::Approx(7.0F));
    CHECK(cry_one->self_melee_damage_bonus == doctest::Approx(0.15F));
    CHECK(cry_one->ally_melee_damage_bonus == doctest::Approx(0.10F));
    CHECK(cry_two->cooldown_seconds == doctest::Approx(18.0F));
    CHECK(cry_two->radius_meters == doctest::Approx(7.0F));
    CHECK(cry_two->duration_seconds == doctest::Approx(8.0F));
    CHECK(cry_two->self_melee_damage_bonus == doctest::Approx(0.20F));
    CHECK(cry_two->ally_melee_damage_bonus == doctest::Approx(0.15F));
    CHECK(cry_three->cooldown_seconds == doctest::Approx(16.0F));
    CHECK(cry_three->radius_meters == doctest::Approx(8.0F));
    CHECK(cry_three->duration_seconds == doctest::Approx(9.0F));
    CHECK(cry_three->self_melee_damage_bonus == doctest::Approx(0.25F));
    CHECK(cry_three->ally_melee_damage_bonus == doctest::Approx(0.20F));
    CHECK(cry_three->immediate_self_heal == doctest::Approx(3.0F));

    const auto* parry_one =
        knight_perfect_riposte_definition(KnightAbilityRank::RankOne);
    const auto* parry_two =
        knight_perfect_riposte_definition(KnightAbilityRank::RankTwo);
    const auto* parry_three =
        knight_perfect_riposte_definition(KnightAbilityRank::RankThree);
    REQUIRE(parry_one != nullptr);
    REQUIRE(parry_two != nullptr);
    REQUIRE(parry_three != nullptr);
    CHECK(parry_one->energy_cost == doctest::Approx(15.0F));
    CHECK(parry_one->cooldown_seconds == doctest::Approx(12.0F));
    CHECK(parry_one->parry_window_seconds == doctest::Approx(0.35F));
    CHECK(parry_one->counter_weapon_damage_multiplier ==
          doctest::Approx(1.80F));
    CHECK(parry_two->cooldown_seconds == doctest::Approx(10.0F));
    CHECK(parry_two->parry_window_seconds == doctest::Approx(0.45F));
    CHECK(parry_two->counter_weapon_damage_multiplier ==
          doctest::Approx(2.20F));
    CHECK(parry_two->energy_refund == doctest::Approx(5.0F));
    CHECK(parry_three->cooldown_seconds == doctest::Approx(8.0F));
    CHECK(parry_three->parry_window_seconds == doctest::Approx(0.55F));
    CHECK(parry_three->counter_weapon_damage_multiplier ==
          doctest::Approx(2.60F));
    CHECK(parry_three->energy_refund == doctest::Approx(5.0F));
    CHECK(parry_three->secondary_cone_damage == doctest::Approx(4.0F));
    CHECK(parry_three->light_target_stun_seconds == doctest::Approx(1.0F));

    CHECK(knight_bulwark_charge_definition(
              static_cast<KnightAbilityRank>(0U)) == nullptr);
    CHECK(knight_champion_cry_definition(
              static_cast<KnightAbilityRank>(4U)) == nullptr);
    CHECK(knight_perfect_riposte_definition(
              static_cast<KnightAbilityRank>(255U)) == nullptr);
}

TEST_CASE("les poids controlent exactement la projection de la charge") {
    CHECK(knight_knockback_multiplier(KnightTargetWeight::Light) ==
          doctest::Approx(1.0F));
    CHECK(knight_knockback_multiplier(KnightTargetWeight::Normal) ==
          doctest::Approx(0.70F));
    CHECK(knight_knockback_multiplier(KnightTargetWeight::Heavy) ==
          doctest::Approx(0.20F));
    CHECK(knight_knockback_multiplier(KnightTargetWeight::Boss) ==
          doctest::Approx(0.0F));
}

TEST_CASE("la charge decoupe tout le trajet en pas surs de 0,45 bloc") {
    KnightAdvancedAbilitySystem system {};
    ChargeTestContext context {};
    const auto result = system.execute_bulwark_charge(
        {
            1U,
            9U,
            KnightAbilityRank::RankOne,
            {2.0F, 4.0F, 3.0F},
            {4.0F, 8.0F, 0.0F},
            10.0F,
            false,
        },
        charge_callbacks(context));

    REQUIRE(result.succeeded());
    REQUIRE(context.commit_count == 1U);
    CHECK(context.probe_count == 12U);
    CHECK(context.committed.safe_path_count == 12U);
    CHECK(context.committed.travelled_distance_meters ==
          doctest::Approx(5.0F));
    CHECK(context.committed.final_position.x == doctest::Approx(7.0F));
    CHECK(context.committed.final_position.y == doctest::Approx(4.0F));
    CHECK(context.committed.final_position.z == doctest::Approx(3.0F));
    CHECK(context.committed.hit_count == 0U);
    for (std::size_t index = 0U; index < context.probe_count; ++index) {
        CHECK(context.requests[index].step_distance_meters <=
              kKnightMaximumChargeStepMeters);
        CHECK(context.requests[index].step_distance_meters > 0.0F);
    }
}

TEST_CASE("la charge reste a la derniere position sure devant mur ou chunk absent") {
    for (const auto traversal : {
             KnightChargeTraversal::WorldBlocked,
             KnightChargeTraversal::ChunkNotReady,
         }) {
        KnightAdvancedAbilitySystem system {};
        ChargeTestContext context {};
        context.configured_probe_count = 3U;
        context.probes[2U].traversal = traversal;

        const auto result = system.execute_bulwark_charge(
            {
                traversal == KnightChargeTraversal::WorldBlocked ? 2U : 3U,
                1U,
                KnightAbilityRank::RankThree,
                {},
                {1.0F, 0.0F, 0.0F},
                10.0F,
                false,
            },
            charge_callbacks(context));
        REQUIRE(result.succeeded());
        CHECK(context.committed.safe_path_count == 2U);
        CHECK(context.committed.travelled_distance_meters ==
              doctest::Approx(0.90F));
        CHECK(context.committed.final_position.x ==
              doctest::Approx(0.90F));
        CHECK(context.committed.stopped_by_world ==
              (traversal == KnightChargeTraversal::WorldBlocked));
        CHECK(context.committed.stopped_by_unready_chunk ==
              (traversal == KnightChargeTraversal::ChunkNotReady));
    }
}

TEST_CASE("les rangs un et trois respectent leurs cibles et degats") {
    {
        KnightAdvancedAbilitySystem system {};
        ChargeTestContext context {};
        configure_contact(
            context,
            2U,
            41U,
            KnightTargetWeight::Normal);
        configure_contact(
            context,
            2U,
            42U,
            KnightTargetWeight::Light);

        const auto result = system.execute_bulwark_charge(
            {
                4U,
                1U,
                KnightAbilityRank::RankOne,
                {},
                {1.0F, 0.0F, 0.0F},
                10.0F,
                false,
            },
            charge_callbacks(context));
        REQUIRE(result.succeeded());
        REQUIRE(context.committed.hit_count == 1U);
        CHECK(context.committed.hits[0U].target_id == 41U);
        CHECK(context.committed.hits[0U].damage ==
              doctest::Approx(8.0F));
        CHECK(context.committed.hits[0U].knockback_multiplier ==
              doctest::Approx(0.70F));
        CHECK(context.committed.ended_in_enemy_contact);
    }

    {
        KnightAdvancedAbilitySystem system {};
        ChargeTestContext context {};
        configure_contact(
            context,
            0U,
            51U,
            KnightTargetWeight::Light);
        configure_contact(
            context,
            1U,
            51U,
            KnightTargetWeight::Boss);
        configure_contact(
            context,
            2U,
            52U,
            KnightTargetWeight::Normal);
        configure_contact(
            context,
            3U,
            53U,
            KnightTargetWeight::Heavy);

        const auto result = system.execute_bulwark_charge(
            {
                5U,
                1U,
                KnightAbilityRank::RankThree,
                {},
                {1.0F, 0.0F, 0.0F},
                10.0F,
                false,
            },
            charge_callbacks(context));
        REQUIRE(result.succeeded());
        REQUIRE(context.committed.hit_count == 3U);
        CHECK(context.committed.hits[0U].target_id == 51U);
        CHECK(context.committed.hits[1U].target_id == 52U);
        CHECK(context.committed.hits[2U].target_id == 53U);
        for (const auto& hit : context.committed.targets_hit()) {
            CHECK(hit.damage == doctest::Approx(12.0F));
        }
        CHECK(context.committed.hits[0U].knockback_multiplier ==
              doctest::Approx(1.0F));
        CHECK(context.committed.hits[1U].knockback_multiplier ==
              doctest::Approx(0.70F));
        CHECK(context.committed.hits[2U].knockback_multiplier ==
              doctest::Approx(0.20F));
        CHECK(context.committed.ended_in_enemy_contact);
    }
}

TEST_CASE("le choc mural reste valable une seconde et precede la resistance") {
    KnightAdvancedAbilitySystem system {};
    ChargeTestContext charge {};
    configure_contact(
        charge,
        0U,
        61U,
        KnightTargetWeight::Normal);
    REQUIRE(system.execute_bulwark_charge(
                       {
                           6U,
                           1U,
                           KnightAbilityRank::RankTwo,
                           {},
                           {1.0F, 0.0F, 0.0F},
                           10.0F,
                           false,
                       },
                       charge_callbacks(charge))
                .succeeded());
    CHECK(system.snapshot().pending_wall_impact_count == 1U);

    for (auto tick = 0U; tick < 59U; ++tick) {
        REQUIRE(system.update(kKnightAbilityFixedStepSeconds).accepted);
    }
    WallTestContext wall {};
    const auto impact = system.notify_charge_wall_impact(
        61U,
        &wall,
        &wall_commit);
    REQUIRE(impact.succeeded());
    CHECK(wall.commit_count == 1U);
    CHECK(wall.committed.activation_id == 6U);
    CHECK(wall.committed.target_id == 61U);
    CHECK(wall.committed.stun_seconds == doctest::Approx(1.0F));
    CHECK(wall.committed.apply_before_control_resistance);
    CHECK(system.snapshot().pending_wall_impact_count == 0U);

    ChargeTestContext second_charge {};
    configure_contact(
        second_charge,
        0U,
        62U,
        KnightTargetWeight::Heavy);
    REQUIRE(system.execute_bulwark_charge(
                       {
                           7U,
                           1U,
                           KnightAbilityRank::RankTwo,
                           {},
                           {1.0F, 0.0F, 0.0F},
                           10.0F,
                           false,
                       },
                       charge_callbacks(second_charge))
                .succeeded());
    const auto expiration = system.update(1.0F);
    CHECK(expiration.expired_wall_impact_count == 1U);
    CHECK(system.notify_charge_wall_impact(
                    62U,
                    &wall,
                    &wall_commit)
              .error ==
          KnightAdvancedAbilityError::NoMatchingWindow);
}

TEST_CASE("Breche ne s'arme qu'au contact et ne renforce qu'une melee") {
    KnightAdvancedAbilitySystem system {};
    ChargeTestContext charge {};
    configure_contact(
        charge,
        0U,
        71U,
        KnightTargetWeight::Light);
    REQUIRE(system.execute_bulwark_charge(
                       {
                           8U,
                           1U,
                           KnightAbilityRank::RankThree,
                           {},
                           {1.0F, 0.0F, 0.0F},
                           10.0F,
                           true,
                       },
                       charge_callbacks(charge))
                .succeeded());
    CHECK_FALSE(system.snapshot().breach_armed);

    ChargeTestContext contact_charge {};
    configure_contact(
        contact_charge,
        0U,
        72U,
        KnightTargetWeight::Light,
        true);
    REQUIRE(system.execute_bulwark_charge(
                       {
                           9U,
                           1U,
                           KnightAbilityRank::RankThree,
                           {},
                           {1.0F, 0.0F, 0.0F},
                           10.0F,
                           true,
                       },
                       charge_callbacks(contact_charge))
                .succeeded());
    CHECK(system.snapshot().breach_armed);
    CHECK(system.snapshot().breach_remaining_seconds ==
          doctest::Approx(2.0F));

    BreachTestContext breach {};
    breach.accept = false;
    CHECK(system.resolve_breach_melee_hit(
                    73U,
                    10.0F,
                    &breach,
                    &breach_commit)
              .error ==
          KnightAdvancedAbilityError::ExternalCommitRejected);
    CHECK(system.snapshot().breach_armed);

    breach.accept = true;
    const auto hit = system.resolve_breach_melee_hit(
        73U,
        10.0F,
        &breach,
        &breach_commit);
    REQUIRE(hit.succeeded());
    CHECK(breach.committed.total_damage == doctest::Approx(14.0F));
    CHECK(breach.committed.bonus_damage_multiplier ==
          doctest::Approx(0.40F));
    CHECK(breach.committed.physical_vulnerability ==
          doctest::Approx(0.10F));
    CHECK(breach.committed.vulnerability_duration_seconds ==
          doctest::Approx(5.0F));
    CHECK(breach.committed.vulnerability_stack_tag ==
          kKnightBreachVulnerabilityStackTag);
    CHECK_FALSE(system.snapshot().breach_armed);
    CHECK(system.resolve_breach_melee_hit(
                    73U,
                    10.0F,
                    &breach,
                    &breach_commit)
              .error ==
          KnightAdvancedAbilityError::NotActive);
}

TEST_CASE("le cri filtre la portee les doublons et les provocations interdites") {
    KnightAdvancedAbilitySystem system {};
    CryTestContext context {};
    context.nearby.targets[0U] = {
        101U,
        {3.0F, 0.0F, 0.0F},
        KnightTargetRelation::Ally,
        false,
    };
    context.nearby.targets[1U] = {
        101U,
        {3.0F, 0.0F, 0.0F},
        KnightTargetRelation::Ally,
        false,
    };
    context.nearby.targets[2U] = {
        102U,
        {9.0F, 0.0F, 0.0F},
        KnightTargetRelation::Ally,
        false,
    };
    context.nearby.targets[3U] = {
        201U,
        {4.0F, 0.0F, 0.0F},
        KnightTargetRelation::Enemy,
        true,
    };
    context.nearby.targets[4U] = {
        202U,
        {4.0F, 0.0F, 0.0F},
        KnightTargetRelation::Enemy,
        false,
    };
    context.nearby.targets[5U] = {
        1U,
        {},
        KnightTargetRelation::Ally,
        false,
    };
    context.nearby.target_count = 6U;

    const auto result = system.activate_champion_cry(
        {
            20U,
            1U,
            KnightAbilityRank::RankThree,
            {},
            true,
        },
        cry_callbacks(context));
    REQUIRE(result.succeeded());
    REQUIRE(context.query_count == 1U);
    REQUIRE(context.commit_count == 1U);
    CHECK(context.query.radius_meters == doctest::Approx(8.0F));
    CHECK(context.committed.duration_seconds == doctest::Approx(9.0F));
    CHECK(context.committed.self_melee_damage_bonus ==
          doctest::Approx(0.25F));
    CHECK(context.committed.immediate_self_heal ==
          doctest::Approx(3.0F));
    CHECK(context.committed.melee_damage_stack_tag ==
          kKnightMeleeDamageBuffStackTag);
    CHECK(context.committed.stack_policy ==
          KnightEffectStackPolicy::Strongest);
    REQUIRE(context.committed.ally_count == 1U);
    CHECK(context.committed.allies[0U].target_id == 101U);
    CHECK(context.committed.allies[0U].melee_damage_bonus ==
          doctest::Approx(0.20F));
    CHECK(context.committed.allies[0U].movement_speed_bonus ==
          doctest::Approx(0.10F));
    CHECK(context.committed.allies[0U].ignore_first_interruption);
    REQUIRE(context.committed.taunt_count == 1U);
    CHECK(context.committed.taunts[0U].target_id == 201U);
    CHECK(context.committed.taunts[0U].priority_target_id == 1U);
    CHECK(context.committed.taunts[0U].duration_seconds ==
          doctest::Approx(9.0F));
}

TEST_CASE("Moral inebranlable ignore une interruption puis expire avec le cri") {
    KnightAdvancedAbilitySystem system {};
    CryTestContext context {};
    context.nearby.targets[0U] = {
        301U,
        {1.0F, 0.0F, 0.0F},
        KnightTargetRelation::Ally,
        false,
    };
    context.nearby.target_count = 1U;
    REQUIRE(system.activate_champion_cry(
                      {
                          21U,
                          1U,
                          KnightAbilityRank::RankOne,
                          {},
                          true,
                      },
                      cry_callbacks(context))
                .succeeded());

    CHECK(system.melee_damage_bonus(1U) == doctest::Approx(0.15F));
    CHECK(system.melee_damage_bonus(301U) == doctest::Approx(0.10F));
    CHECK(system.movement_speed_bonus(301U) == doctest::Approx(0.10F));
    CHECK(system.consume_champion_ally_interruption(301U));
    CHECK_FALSE(system.consume_champion_ally_interruption(301U));

    for (auto tick = 0U; tick < 419U; ++tick) {
        REQUIRE(system.update(kKnightAbilityFixedStepSeconds).accepted);
    }
    CHECK(system.snapshot().champion_cry_active);
    const auto expiration = system.update(kKnightAbilityFixedStepSeconds);
    CHECK(expiration.champion_cry_expired);
    CHECK_FALSE(system.snapshot().champion_cry_active);
    CHECK(system.melee_damage_bonus(1U) == doctest::Approx(0.0F));
    CHECK(system.movement_speed_bonus(301U) == doctest::Approx(0.0F));
    CHECK_FALSE(system.consume_champion_ally_interruption(301U));
}

TEST_CASE("le cri rejete les lots debordes et les commits partiels") {
    KnightAdvancedAbilitySystem system {};
    CryTestContext overflow {};
    overflow.nearby.target_count = kKnightMaximumNearbyTargets + 1U;
    CHECK(system.activate_champion_cry(
                    {
                        22U,
                        1U,
                        KnightAbilityRank::RankOne,
                        {},
                        false,
                    },
                    cry_callbacks(overflow))
              .error ==
          KnightAdvancedAbilityError::CapacityExceeded);
    CHECK(overflow.commit_count == 0U);
    CHECK_FALSE(system.snapshot().champion_cry_active);

    CryTestContext rejected {};
    rejected.accept_commit = false;
    CHECK(system.activate_champion_cry(
                    {
                        23U,
                        1U,
                        KnightAbilityRank::RankOne,
                        {},
                        false,
                    },
                    cry_callbacks(rejected))
              .error ==
          KnightAdvancedAbilityError::ExternalCommitRejected);
    CHECK_FALSE(system.snapshot().champion_cry_active);
}

TEST_CASE("la riposte applique les contres remboursements et cone de chaque rang") {
    struct Expected {
        KnightAbilityRank rank;
        float multiplier;
        float refund;
        float cone_damage;
        float light_stun;
    };
    constexpr std::array expected {
        Expected {KnightAbilityRank::RankOne, 1.80F, 0.0F, 0.0F, 0.0F},
        Expected {KnightAbilityRank::RankTwo, 2.20F, 5.0F, 0.0F, 0.0F},
        Expected {KnightAbilityRank::RankThree, 2.60F, 5.0F, 4.0F, 1.0F},
    };

    auto activation_id = KnightActivationId {30U};
    for (const auto& values : expected) {
        KnightAdvancedAbilitySystem system {};
        const auto activation = system.arm_perfect_riposte(
            {
                activation_id,
                1U,
                values.rank,
                10.0F,
                false,
            });
        REQUIRE(activation.succeeded());

        RiposteTestContext context {};
        const auto parry = system.resolve_incoming_attack(
            {
                activation_id + 100U,
                2U,
                8.0F,
                true,
            },
            parry_callbacks(context));
        REQUIRE(parry.succeeded());
        CHECK(parry.incoming_attack_cancelled);
        CHECK(context.committed.cancel_incoming_attack);
        CHECK(context.committed.cancelled_incoming_damage ==
              doctest::Approx(8.0F));
        CHECK(context.committed.counter_weapon_damage_multiplier ==
              doctest::Approx(values.multiplier));
        CHECK(context.committed.counter_damage ==
              doctest::Approx(10.0F * values.multiplier));
        CHECK(context.committed.energy_refund ==
              doctest::Approx(values.refund));
        CHECK(context.committed.emit_secondary_cone ==
              (values.cone_damage > 0.0F));
        CHECK(context.committed.exclude_primary_from_secondary_cone);
        CHECK(context.committed.secondary_cone_damage ==
              doctest::Approx(values.cone_damage));
        CHECK(context.committed.stun_light_targets_only);
        CHECK(context.committed.light_target_stun_seconds ==
              doctest::Approx(values.light_stun));
        CHECK_FALSE(system.snapshot().perfect_riposte_armed);
        ++activation_id;
    }
}

TEST_CASE("une attaque imparable traverse la fenetre sans la consommer") {
    KnightAdvancedAbilitySystem system {};
    REQUIRE(system.arm_perfect_riposte(
                      {
                          40U,
                          1U,
                          KnightAbilityRank::RankOne,
                          10.0F,
                          false,
                      })
                .succeeded());
    RiposteTestContext context {};
    const auto scripted = system.resolve_incoming_attack(
        {
            1U,
            2U,
            100.0F,
            false,
        },
        parry_callbacks(context));
    CHECK(scripted.error == KnightAdvancedAbilityError::NotParryable);
    CHECK_FALSE(scripted.incoming_attack_cancelled);
    CHECK(context.commit_count == 0U);
    CHECK(system.snapshot().perfect_riposte_armed);

    const auto ordinary = system.resolve_incoming_attack(
        {
            2U,
            2U,
            10.0F,
            true,
        },
        parry_callbacks(context));
    REQUIRE(ordinary.succeeded());
    CHECK(ordinary.incoming_attack_cancelled);
}

TEST_CASE("Duel du maitre remet Vanguard et accorde sa couche temporaire") {
    KnightAdvancedAbilitySystem system {};
    REQUIRE(system.arm_perfect_riposte(
                      {
                          41U,
                          1U,
                          KnightAbilityRank::RankThree,
                          10.0F,
                          true,
                      })
                .succeeded());
    RiposteTestContext context {};
    const auto result = system.resolve_incoming_attack(
        {
            3U,
            2U,
            10.0F,
            true,
        },
        parry_callbacks(context));
    REQUIRE(result.succeeded());
    CHECK(context.committed.reset_vanguard_strike_cooldown);
    CHECK(context.committed.mastery_damage_reduction ==
          doctest::Approx(0.20F));
    CHECK(context.committed.mastery_damage_reduction_seconds ==
          doctest::Approx(1.5F));
    CHECK(context.committed.mastery_stack_tag ==
          kKnightRiposteMasteryStackTag);
    CHECK(system.snapshot().mastery_damage_reduction ==
          doctest::Approx(0.20F));

    for (auto tick = 0U; tick < 89U; ++tick) {
        REQUIRE(system.update(kKnightAbilityFixedStepSeconds).accepted);
    }
    CHECK(system.snapshot().mastery_damage_reduction ==
          doctest::Approx(0.20F));
    const auto expiration = system.update(kKnightAbilityFixedStepSeconds);
    CHECK(expiration.mastery_damage_reduction_expired);
    CHECK(system.snapshot().mastery_damage_reduction ==
          doctest::Approx(0.0F));
}

TEST_CASE("les fenetres de parade font exactement 21 27 et 33 ticks") {
    struct Window {
        KnightAbilityRank rank;
        std::uint32_t ticks;
    };
    constexpr std::array windows {
        Window {KnightAbilityRank::RankOne, 21U},
        Window {KnightAbilityRank::RankTwo, 27U},
        Window {KnightAbilityRank::RankThree, 33U},
    };

    auto activation_id = KnightActivationId {50U};
    for (const auto& window : windows) {
        KnightAdvancedAbilitySystem system {};
        REQUIRE(system.arm_perfect_riposte(
                          {
                              activation_id++,
                              1U,
                              window.rank,
                              10.0F,
                              false,
                          })
                    .succeeded());
        for (auto tick = 1U; tick < window.ticks; ++tick) {
            const auto update =
                system.update(kKnightAbilityFixedStepSeconds);
            REQUIRE(update.accepted);
            CHECK_FALSE(update.perfect_riposte_expired);
        }
        CHECK(system.snapshot().perfect_riposte_armed);
        const auto expiration =
            system.update(kKnightAbilityFixedStepSeconds);
        CHECK(expiration.perfect_riposte_expired);
        CHECK_FALSE(system.snapshot().perfect_riposte_armed);
    }
}

TEST_CASE("un commit de riposte refuse ne peut jamais annuler l'attaque") {
    KnightAdvancedAbilitySystem system {};
    REQUIRE(system.arm_perfect_riposte(
                      {
                          60U,
                          1U,
                          KnightAbilityRank::RankTwo,
                          10.0F,
                          false,
                      })
                .succeeded());
    RiposteTestContext context {};
    context.accept = false;
    const auto rejected = system.resolve_incoming_attack(
        {
            1U,
            2U,
            10.0F,
            true,
        },
        parry_callbacks(context));
    CHECK(rejected.error ==
          KnightAdvancedAbilityError::ExternalCommitRejected);
    CHECK_FALSE(rejected.incoming_attack_cancelled);
    CHECK(system.snapshot().perfect_riposte_armed);

    context.accept = true;
    const auto accepted = system.resolve_incoming_attack(
        {
            1U,
            2U,
            10.0F,
            true,
        },
        parry_callbacks(context));
    REQUIRE(accepted.succeeded());
    CHECK(accepted.incoming_attack_cancelled);
}

TEST_CASE("les donnees non finies et callbacks absents ne contaminent rien") {
    KnightAdvancedAbilitySystem system {};
    ChargeTestContext charge {};
    auto invalid_charge = KnightBulwarkChargeRequest {
        70U,
        1U,
        KnightAbilityRank::RankOne,
        {},
        {1.0F, 0.0F, 0.0F},
        std::numeric_limits<float>::quiet_NaN(),
        true,
    };
    CHECK(system.execute_bulwark_charge(
                    invalid_charge,
                    charge_callbacks(charge))
              .error ==
          KnightAdvancedAbilityError::InvalidInput);
    CHECK(charge.commit_count == 0U);

    invalid_charge.weapon_damage = 10.0F;
    invalid_charge.direction.x =
        std::numeric_limits<float>::infinity();
    CHECK(system.execute_bulwark_charge(
                    invalid_charge,
                    charge_callbacks(charge))
              .error ==
          KnightAdvancedAbilityError::InvalidInput);

    CryTestContext cry {};
    CHECK(system.activate_champion_cry(
                    {
                        71U,
                        1U,
                        KnightAbilityRank::RankOne,
                        {
                            std::numeric_limits<float>::quiet_NaN(),
                            0.0F,
                            0.0F,
                        },
                        false,
                    },
                    cry_callbacks(cry))
              .error ==
          KnightAdvancedAbilityError::InvalidInput);

    CHECK(system.arm_perfect_riposte(
                    {
                        72U,
                        1U,
                        KnightAbilityRank::RankOne,
                        std::numeric_limits<float>::infinity(),
                        false,
                    })
              .error ==
          KnightAdvancedAbilityError::InvalidInput);
    CHECK_FALSE(system.snapshot().perfect_riposte_armed);

    CHECK_FALSE(system.update(
                          std::numeric_limits<float>::quiet_NaN())
                    .accepted);
    CHECK_FALSE(system.update(
                          std::numeric_limits<float>::infinity())
                    .accepted);
    CHECK_FALSE(system.update(-1.0F).accepted);
    CHECK_FALSE(system.update(kKnightMaximumUpdateSeconds + 1.0F).accepted);
}

TEST_CASE("les reponses externes invalides sont rejetees avant tout commit") {
    {
        KnightAdvancedAbilitySystem system {};
        ChargeTestContext context {};
        context.configured_probe_count = 1U;
        context.probes[0U].contacts[0U] = {
            0U,
            KnightTargetWeight::Light,
        };
        context.probes[0U].contact_count = 1U;
        CHECK(system.execute_bulwark_charge(
                        {
                            75U,
                            1U,
                            KnightAbilityRank::RankTwo,
                            {},
                            {1.0F, 0.0F, 0.0F},
                            10.0F,
                            true,
                        },
                        charge_callbacks(context))
                  .error ==
              KnightAdvancedAbilityError::InvalidCallbackResult);
        CHECK(context.commit_count == 0U);
        CHECK_FALSE(system.snapshot().breach_armed);
        CHECK(system.snapshot().pending_wall_impact_count == 0U);
    }

    {
        KnightAdvancedAbilitySystem system {};
        CryTestContext context {};
        context.nearby.targets[0U] = {
            2U,
            {
                std::numeric_limits<float>::infinity(),
                0.0F,
                0.0F,
            },
            KnightTargetRelation::Ally,
            false,
        };
        context.nearby.target_count = 1U;
        CHECK(system.activate_champion_cry(
                        {
                            76U,
                            1U,
                            KnightAbilityRank::RankOne,
                            {},
                            true,
                        },
                        cry_callbacks(context))
                  .error ==
              KnightAdvancedAbilityError::InvalidCallbackResult);
        CHECK(context.commit_count == 0U);
        CHECK_FALSE(system.snapshot().champion_cry_active);
    }
}

TEST_CASE("un commit de charge refuse ne laisse ni Breche ni choc mural") {
    KnightAdvancedAbilitySystem system {};
    ChargeTestContext context {};
    configure_contact(
        context,
        0U,
        77U,
        KnightTargetWeight::Normal,
        true);
    context.accept_commit = false;
    CHECK(system.execute_bulwark_charge(
                    {
                        77U,
                        1U,
                        KnightAbilityRank::RankThree,
                        {},
                        {1.0F, 0.0F, 0.0F},
                        10.0F,
                        true,
                    },
                    charge_callbacks(context))
              .error ==
          KnightAdvancedAbilityError::ExternalCommitRejected);
    CHECK_FALSE(system.snapshot().breach_armed);
    CHECK(system.snapshot().pending_wall_impact_count == 0U);

    context.accept_commit = true;
    context.probe_count = 0U;
    context.commit_count = 0U;
    const auto retry = system.execute_bulwark_charge(
        {
            77U,
            1U,
            KnightAbilityRank::RankThree,
            {},
            {1.0F, 0.0F, 0.0F},
            10.0F,
            true,
        },
        charge_callbacks(context));
    REQUIRE(retry.succeeded());
    CHECK(system.snapshot().breach_armed);
    CHECK(system.snapshot().pending_wall_impact_count == 1U);
}

TEST_CASE("le pas fixe donne le meme etat en mise a jour groupee ou decoupee") {
    KnightAdvancedAbilitySystem split {};
    KnightAdvancedAbilitySystem grouped {};
    REQUIRE(split.arm_perfect_riposte(
                      {
                          80U,
                          1U,
                          KnightAbilityRank::RankThree,
                          10.0F,
                          false,
                      })
                .succeeded());
    REQUIRE(grouped.arm_perfect_riposte(
                        {
                            80U,
                            1U,
                            KnightAbilityRank::RankThree,
                            10.0F,
                            false,
                        })
                .succeeded());

    for (auto tick = 0U; tick < 20U; ++tick) {
        REQUIRE(split.update(kKnightAbilityFixedStepSeconds).accepted);
    }
    const auto grouped_update =
        grouped.update(20.0F * kKnightAbilityFixedStepSeconds);
    REQUIRE(grouped_update.accepted);
    CHECK(split.snapshot().perfect_riposte_remaining_seconds ==
          doctest::Approx(
              grouped.snapshot().perfect_riposte_remaining_seconds));
    CHECK(split.snapshot().perfect_riposte_armed ==
          grouped.snapshot().perfect_riposte_armed);
}

TEST_CASE("clear supprime tous les effets transitoires et autorise une nouvelle session") {
    KnightAdvancedAbilitySystem system {};
    CryTestContext cry {};
    REQUIRE(system.activate_champion_cry(
                      {
                          90U,
                          1U,
                          KnightAbilityRank::RankOne,
                          {},
                          false,
                      },
                      cry_callbacks(cry))
                .succeeded());
    REQUIRE(system.arm_perfect_riposte(
                      {
                          91U,
                          1U,
                          KnightAbilityRank::RankOne,
                          10.0F,
                          false,
                      })
                .succeeded());
    system.clear();
    CHECK_FALSE(system.snapshot().champion_cry_active);
    CHECK_FALSE(system.snapshot().perfect_riposte_armed);

    CryTestContext next_cry {};
    CHECK(system.activate_champion_cry(
                    {
                        90U,
                        1U,
                        KnightAbilityRank::RankOne,
                        {},
                        false,
                    },
                    cry_callbacks(next_cry))
              .succeeded());
}

} // namespace valcraft
