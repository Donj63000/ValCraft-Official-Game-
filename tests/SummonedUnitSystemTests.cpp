#include "gameplay/progression/SummonedUnitSystem.h"

#include <doctest/doctest.h>

#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

namespace valcraft {
namespace {

[[nodiscard]] auto fixed_target_callbacks(
    std::size_t& strike_count,
    std::size_t& taunt_count,
    std::vector<float>* damages = nullptr,
    std::vector<float>* event_times = nullptr)
    -> SummonedUnitCallbacks {
    SummonedUnitCallbacks callbacks {};
    callbacks.acquire_target =
        [event_times](
            const SummonedUnitAcquireRequest& request)
            -> std::optional<SummonedUnitTarget> {
        if (event_times != nullptr) {
            event_times->push_back(
                request.simulation_time_seconds);
        }
        return SummonedUnitTarget {
            42U,
            request.origin +
                glm::vec3 {1.0F, 0.0F, 0.0F},
        };
    };
    callbacks.strike_target =
        [&strike_count, damages](
            const SummonedUnitStrikeRequest& request) {
        ++strike_count;
        if (damages != nullptr) {
            damages->push_back(request.damage);
        }
        return SummonedUnitStrikeResult {
            true,
            strike_count == 2U,
            request.damage,
        };
    };
    callbacks.taunt =
        [&taunt_count](
            const SummonedUnitTauntRequest&) {
        ++taunt_count;
    };
    return callbacks;
}

} // namespace

TEST_CASE("les trois rangs du soldat gardent leurs valeurs contractuelles") {
    const auto rank_one =
        summoned_unit_stats(
            SummonedUnitRank::RankOne);
    CHECK(rank_one.duration_seconds == doctest::Approx(20.0F));
    CHECK(rank_one.maximum_health == doctest::Approx(14.0F));
    CHECK(rank_one.attack_damage == doctest::Approx(3.0F));
    CHECK(rank_one.attack_interval_seconds == doctest::Approx(1.2F));
    CHECK_FALSE(rank_one.has_light_taunt);
    CHECK_FALSE(rank_one.has_projectile_block);

    const auto rank_two =
        summoned_unit_stats(
            SummonedUnitRank::RankTwo);
    CHECK(rank_two.duration_seconds == doctest::Approx(25.0F));
    CHECK(rank_two.maximum_health == doctest::Approx(18.0F));
    CHECK(rank_two.attack_damage == doctest::Approx(4.0F));
    CHECK(rank_two.has_light_taunt);
    CHECK_FALSE(rank_two.has_projectile_block);

    const auto rank_three =
        summoned_unit_stats(
            SummonedUnitRank::RankThree);
    CHECK(rank_three.duration_seconds == doctest::Approx(30.0F));
    CHECK(rank_three.maximum_health == doctest::Approx(22.0F));
    CHECK(rank_three.attack_damage == doctest::Approx(5.0F));
    CHECK(rank_three.has_light_taunt);
    CHECK(rank_three.has_projectile_block);
}

TEST_CASE("la puissance module les points de vie et les degats au centieme") {
    SummonedUnitSystem system {};
    REQUIRE(
        system.summon({
            5U,
            {},
            SummonedUnitRank::RankThree,
            false,
            1.23456F,
        }).spawned);

    CHECK(system.state().maximum_health ==
          doctest::Approx(27.16F));
    CHECK(system.state().health ==
          doctest::Approx(27.16F));

    std::size_t strike_count = 0U;
    std::size_t taunt_count = 0U;
    std::vector<float> damages {};
    const auto callbacks =
        fixed_target_callbacks(
            strike_count,
            taunt_count,
            &damages);
    const auto frame =
        system.update(
            kSummonedUnitAttackIntervalSeconds,
            callbacks);

    REQUIRE(frame.attack_count == 1U);
    REQUIRE(damages.size() == 1U);
    CHECK(damages.front() == doctest::Approx(6.17F));
    CHECK(frame.attack_results().front().requested_damage ==
          doctest::Approx(6.17F));
}

TEST_CASE("les puissances specialisees separent les points de vie et les degats") {
    SummonedUnitSystem system {};
    REQUIRE(
        system.summon({
            15U,
            {},
            SummonedUnitRank::RankThree,
            false,
            1.75F,
            1.23456F,
            1.43210F,
        }).spawned);

    CHECK(system.state().maximum_health ==
          doctest::Approx(27.16F));
    CHECK(system.state().health ==
          doctest::Approx(27.16F));

    std::size_t strike_count = 0U;
    std::size_t taunt_count = 0U;
    std::vector<float> damages {};
    const auto callbacks =
        fixed_target_callbacks(
            strike_count,
            taunt_count,
            &damages);
    const auto frame =
        system.update(
            kSummonedUnitAttackIntervalSeconds,
            callbacks);

    REQUIRE(frame.attack_count == 1U);
    REQUIRE(damages.size() == 1U);
    CHECK(damages.front() == doctest::Approx(7.16F));
    CHECK(frame.attack_results().front().requested_damage ==
          doctest::Approx(7.16F));
}

TEST_CASE("une puissance specialisee absente herite du facteur commun") {
    SummonedUnitSystem system {};
    SummonedUnitSpawnRequest request {
        16U,
        {},
        SummonedUnitRank::RankTwo,
        false,
        1.25F,
    };
    request.health_power_multiplier = 1.50F;
    REQUIRE(system.summon(request).spawned);

    CHECK(system.state().maximum_health ==
          doctest::Approx(27.0F));

    std::size_t strike_count = 0U;
    std::size_t taunt_count = 0U;
    std::vector<float> damages {};
    const auto callbacks =
        fixed_target_callbacks(
            strike_count,
            taunt_count,
            &damages);
    (void)system.update(
        kSummonedUnitAttackIntervalSeconds,
        callbacks);

    REQUIRE(damages.size() == 1U);
    CHECK(damages.front() == doctest::Approx(5.0F));
}

TEST_CASE("la puissance un conserve exactement les valeurs historiques") {
    for (const auto rank : {
             SummonedUnitRank::RankOne,
             SummonedUnitRank::RankTwo,
             SummonedUnitRank::RankThree,
         }) {
        SummonedUnitSystem system {};
        REQUIRE(
            system.summon({
                8U,
                {},
                rank,
                false,
                1.0F,
            }).spawned);

        const auto expected =
            summoned_unit_stats(rank);
        CHECK(system.state().maximum_health ==
              expected.maximum_health);
        CHECK(system.state().health ==
              expected.maximum_health);

        std::size_t strike_count = 0U;
        std::size_t taunt_count = 0U;
        std::vector<float> damages {};
        const auto callbacks =
            fixed_target_callbacks(
                strike_count,
                taunt_count,
                &damages);
        (void)system.update(
            kSummonedUnitAttackIntervalSeconds,
            callbacks);
        REQUIRE(damages.size() == 1U);
        CHECK(damages.front() ==
              expected.attack_damage);
    }
}

TEST_CASE("la puissance invalide revient a un et les valeurs finies sont bornees") {
    const auto check_maximum_health =
        [](float multiplier,
           float expected_health) {
        SummonedUnitSystem system {};
        REQUIRE(
            system.summon({
                3U,
                {},
                SummonedUnitRank::RankOne,
                false,
                multiplier,
            }).spawned);
        CHECK(system.state().maximum_health ==
              doctest::Approx(expected_health));
        CHECK(std::isfinite(system.state().maximum_health));
    };

    check_maximum_health(
        std::numeric_limits<float>::quiet_NaN(),
        14.0F);
    check_maximum_health(
        std::numeric_limits<float>::infinity(),
        14.0F);
    check_maximum_health(
        -2.0F,
        14.0F);
    check_maximum_health(
        0.0F,
        14.0F);
    check_maximum_health(
        0.10F,
        3.50F);
    check_maximum_health(
        12.0F,
        56.0F);
}

TEST_CASE("les puissances specialisees invalides et extremes sont assainies separement") {
    const auto check_scaling =
        [](float health_multiplier,
           float damage_multiplier,
           float expected_health,
           float expected_damage) {
        SummonedUnitSystem system {};
        SummonedUnitSpawnRequest request {
            17U,
            {},
            SummonedUnitRank::RankOne,
            false,
            2.0F,
        };
        request.health_power_multiplier =
            health_multiplier;
        request.attack_power_multiplier =
            damage_multiplier;
        REQUIRE(system.summon(request).spawned);
        CHECK(system.state().maximum_health ==
              doctest::Approx(expected_health));
        CHECK(std::isfinite(system.state().maximum_health));

        std::size_t strike_count = 0U;
        std::size_t taunt_count = 0U;
        std::vector<float> damages {};
        const auto callbacks =
            fixed_target_callbacks(
                strike_count,
                taunt_count,
                &damages);
        (void)system.update(
            kSummonedUnitAttackIntervalSeconds,
            callbacks);
        REQUIRE(damages.size() == 1U);
        CHECK(damages.front() ==
              doctest::Approx(expected_damage));
        CHECK(std::isfinite(damages.front()));
    };

    check_scaling(
        std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity(),
        14.0F,
        3.0F);
    check_scaling(
        0.0F,
        -4.0F,
        14.0F,
        3.0F);
    check_scaling(
        0.10F,
        12.0F,
        3.50F,
        12.0F);
}

TEST_CASE("le rang un frappe toutes les 1,2 secondes puis expire a 20 secondes") {
    SummonedUnitSystem system {};
    const auto spawn =
        system.summon({
            7U,
            {2.0F, 3.0F, 4.0F},
            SummonedUnitRank::RankOne,
            false,
        });
    REQUIRE(spawn.spawned);
    REQUIRE(system.active());

    std::size_t strike_count = 0U;
    std::size_t taunt_count = 0U;
    auto callbacks =
        fixed_target_callbacks(
            strike_count,
            taunt_count);

    const auto early =
        system.update(
            1.19F,
            callbacks);
    CHECK(early.attack_window_count == 0U);
    CHECK(strike_count == 0U);

    const auto first =
        system.update(
            0.01F,
            callbacks);
    CHECK(first.attack_window_count == 1U);
    CHECK(first.attack_count == 1U);
    CHECK(first.attack_results().front().source ==
          SummonedUnitDamageSource::PlayerSummon);
    CHECK(strike_count == 1U);

    std::size_t all_windows =
        first.attack_window_count;
    bool expired = false;
    for (int step = 0;
         step < 1'128 && !expired;
         ++step) {
        const auto frame =
            system.update(
                1.0F / 60.0F,
                callbacks);
        all_windows +=
            frame.attack_window_count;
        expired = frame.expired;
    }

    CHECK(expired);
    CHECK_FALSE(system.active());
    CHECK(system.render_snapshots().empty());
    CHECK(all_windows == 16U);
    CHECK(strike_count == 16U);
    CHECK(taunt_count == 0U);
}

TEST_CASE("le rang deux provoque un taunt leger de six metres toutes les six secondes") {
    SummonedUnitSystem system {};
    REQUIRE(
        system.summon({
            9U,
            {-3.0F, 2.0F, 8.0F},
            SummonedUnitRank::RankTwo,
            false,
        }).spawned);

    std::size_t strike_count = 0U;
    std::size_t taunt_count = 0U;
    std::vector<float> damages {};
    float observed_radius = 0.0F;
    auto callbacks =
        fixed_target_callbacks(
            strike_count,
            taunt_count,
            &damages);
    callbacks.taunt =
        [&](const SummonedUnitTauntRequest& request) {
        ++taunt_count;
        observed_radius = request.radius;
        CHECK_FALSE(request.mastery_triggered);
        CHECK(request.owner_id == 9U);
    };

    const auto frame =
        system.update(
            12.0F,
            callbacks);
    CHECK(frame.taunt_count == 2U);
    CHECK(taunt_count == 2U);
    CHECK(frame.kill_count == 1U);
    CHECK(observed_radius == doctest::Approx(6.0F));
    REQUIRE_FALSE(damages.empty());
    for (const auto damage : damages) {
        CHECK(damage == doctest::Approx(4.0F));
    }
    auto observed_player_summon_kill = false;
    for (const auto& attack :
         frame.attack_results()) {
        if (attack.killed) {
            observed_player_summon_kill =
                attack.source ==
                SummonedUnitDamageSource::PlayerSummon;
        }
    }
    CHECK(observed_player_summon_kill);
}

TEST_CASE("le rang trois bloque un projectile puis recharge son blocage en six secondes") {
    SummonedUnitSystem system {};
    REQUIRE(
        system.summon({
            1U,
            {},
            SummonedUnitRank::RankThree,
            false,
        }).spawned);
    REQUIRE(system.render_snapshots().size() == 1U);
    CHECK(system.render_snapshots().front().projectile_block_ready);

    const auto first =
        system.apply_damage({
            7.0F,
            SummonedUnitDamageKind::Projectile,
        });
    CHECK(first.handled);
    CHECK(first.blocked);
    CHECK(first.applied_damage == doctest::Approx(0.0F));
    CHECK(first.remaining_health == doctest::Approx(22.0F));

    const auto second =
        system.apply_damage({
            7.0F,
            SummonedUnitDamageKind::Projectile,
        });
    CHECK(second.handled);
    CHECK_FALSE(second.blocked);
    CHECK(second.remaining_health == doctest::Approx(15.0F));

    const SummonedUnitCallbacks no_callbacks {};
    (void)system.update(
        6.0F,
        no_callbacks);
    CHECK(system.render_snapshots().front().projectile_block_ready);
    const auto recharged =
        system.apply_damage({
            20.0F,
            SummonedUnitDamageKind::Projectile,
        });
    CHECK(recharged.blocked);
    CHECK(recharged.remaining_health == doctest::Approx(15.0F));
}

TEST_CASE("la maitrise refuse la premiere mort provoque puis reduit les degats de moitie") {
    SummonedUnitSystem system {};
    REQUIRE(
        system.summon({
            77U,
            {1.0F, 2.0F, 3.0F},
            SummonedUnitRank::RankOne,
            true,
        }).spawned);

    const auto lethal =
        system.apply_damage({
            100.0F,
            SummonedUnitDamageKind::Melee,
        });
    CHECK(lethal.death_refused);
    CHECK_FALSE(lethal.killed);
    CHECK(lethal.remaining_health == doctest::Approx(1.0F));
    CHECK(system.state().death_refusal_used);
    CHECK(system.state().mastery_damage_reduction_seconds ==
          doctest::Approx(kSummonedUnitMasteryDamageReductionSeconds));

    std::size_t taunt_count = 0U;
    bool mastery_taunt = false;
    SummonedUnitCallbacks callbacks {};
    callbacks.taunt =
        [&](const SummonedUnitTauntRequest& request) {
        ++taunt_count;
        mastery_taunt = request.mastery_triggered;
        CHECK(request.radius == doctest::Approx(6.0F));
    };
    const auto taunt_frame =
        system.update(
            0.0F,
            callbacks);
    CHECK(taunt_frame.taunt_count == 1U);
    CHECK(taunt_count == 1U);
    CHECK(mastery_taunt);

    const auto reduced =
        system.apply_damage({
            1.0F,
            SummonedUnitDamageKind::Melee,
        });
    CHECK(reduced.applied_damage == doctest::Approx(0.5F));
    CHECK(reduced.remaining_health == doctest::Approx(0.5F));

    const auto second_death =
        system.apply_damage({
            1.0F,
            SummonedUnitDamageKind::Melee,
        });
    CHECK_FALSE(second_death.death_refused);
    CHECK(second_death.killed);
    CHECK_FALSE(system.active());
}

TEST_CASE("les memes evenements restent deterministes avec des decoupages fixed step differents") {
    SummonedUnitSystem sliced {};
    SummonedUnitSystem batched {};
    const SummonedUnitSpawnRequest spawn {
        55U,
        {4.0F, 5.0F, -2.0F},
        SummonedUnitRank::RankThree,
        false,
        1.30F,
    };
    REQUIRE(sliced.summon(spawn).spawned);
    REQUIRE(batched.summon(spawn).spawned);

    std::size_t sliced_strikes = 0U;
    std::size_t sliced_taunts = 0U;
    std::size_t batched_strikes = 0U;
    std::size_t batched_taunts = 0U;
    std::vector<float> sliced_damage {};
    std::vector<float> batched_damage {};
    std::vector<float> sliced_times {};
    std::vector<float> batched_times {};
    auto sliced_callbacks =
        fixed_target_callbacks(
            sliced_strikes,
            sliced_taunts,
            &sliced_damage,
            &sliced_times);
    auto batched_callbacks =
        fixed_target_callbacks(
            batched_strikes,
            batched_taunts,
            &batched_damage,
            &batched_times);

    for (int step = 0; step < 720; ++step) {
        (void)sliced.update(
            1.0F / 60.0F,
            sliced_callbacks);
    }
    const auto batched_result =
        batched.update(
            12.0F,
            batched_callbacks);

    CHECK(sliced_strikes == batched_strikes);
    CHECK(sliced_taunts == batched_taunts);
    CHECK(batched_result.attack_window_count == 10U);
    CHECK(batched_result.taunt_count == 2U);
    CHECK(sliced_damage == batched_damage);
    REQUIRE_FALSE(sliced_damage.empty());
    CHECK(sliced_damage.front() ==
          doctest::Approx(6.50F));
    REQUIRE(sliced_times.size() == batched_times.size());
    for (std::size_t index = 0U;
         index < sliced_times.size();
         ++index) {
        CHECK(sliced_times[index] ==
              doctest::Approx(
                  batched_times[index])
                  .epsilon(0.00001));
    }

    const auto sliced_state =
        sliced.state();
    const auto batched_state =
        batched.state();
    CHECK(sliced_state.health ==
          doctest::Approx(batched_state.health));
    CHECK(sliced_state.remaining_seconds ==
          doctest::Approx(
              batched_state.remaining_seconds)
              .epsilon(0.00001));
    REQUIRE(sliced.render_snapshots().size() == 1U);
    REQUIRE(batched.render_snapshots().size() == 1U);
    CHECK(sliced.render_snapshots().front().yaw_radians ==
          doctest::Approx(
              batched.render_snapshots().front().yaw_radians));
    CHECK(sliced.render_snapshots().front().attack_amount ==
          doctest::Approx(
              batched.render_snapshots().front().attack_amount)
              .epsilon(0.00001));
    CHECK(sliced.render_snapshots().front().taunt_amount ==
          doctest::Approx(
              batched.render_snapshots().front().taunt_amount)
              .epsilon(0.00001));
}

TEST_CASE("les entrees non finies ne contaminent ni la position ni les temporisations") {
    SummonedUnitSystem system {};
    CHECK_FALSE(
        system.summon({
            1U,
            {
                std::numeric_limits<float>::quiet_NaN(),
                0.0F,
                0.0F,
            },
            SummonedUnitRank::RankOne,
            false,
        }).spawned);

    REQUIRE(
        system.summon({
            1U,
            {3.0F, 4.0F, 5.0F},
            SummonedUnitRank::RankOne,
            false,
        }).spawned);
    const auto before =
        system.state();
    (void)system.update(
        std::numeric_limits<float>::infinity(),
        {});
    system.set_position({
        0.0F,
        std::numeric_limits<float>::quiet_NaN(),
        0.0F,
    });
    const auto after =
        system.state();
    CHECK(after.position == before.position);
    CHECK(after.remaining_seconds ==
          doctest::Approx(before.remaining_seconds));
    const auto invalid_damage =
        system.apply_damage({
            std::numeric_limits<float>::quiet_NaN(),
            SummonedUnitDamageKind::Melee,
        });
    CHECK_FALSE(invalid_damage.handled);
    CHECK(std::isfinite(system.state().health));
}

TEST_CASE("les identifiants automatiques sont uniques entre les instances") {
    SummonedUnitSystem first {};
    SummonedUnitSystem second {};
    const auto first_spawn =
        first.summon({
            101U,
            {1.0F, 0.0F, 0.0F},
            SummonedUnitRank::RankOne,
            false,
        });
    const auto second_spawn =
        second.summon({
            202U,
            {2.0F, 0.0F, 0.0F},
            SummonedUnitRank::RankOne,
            false,
        });
    REQUIRE(first_spawn.spawned);
    REQUIRE(second_spawn.spawned);
    CHECK(first_spawn.unit_id != 0U);
    CHECK(second_spawn.unit_id != 0U);
    CHECK(first_spawn.unit_id != second_spawn.unit_id);

    SummonedUnitSpawnRequest injected_request {};
    injected_request.owner_id = 303U;
    injected_request.unit_id = 1'000'000'000'000ULL;
    injected_request.cast_sequence = 987U;
    SummonedUnitSystem injected {};
    const auto injected_spawn =
        injected.summon(injected_request);
    REQUIRE(injected_spawn.spawned);
    CHECK(injected_spawn.unit_id ==
          *injected_request.unit_id);
    CHECK(injected.state().cast_sequence == 987U);
    CHECK(next_summoned_unit_id() >=
          injected_spawn.unit_id + 1U);

    constexpr auto restored_next_id =
        SummonedUnitId {1'000'000'000'100ULL};
    reserve_next_summoned_unit_id(
        restored_next_id);
    CHECK(next_summoned_unit_id() >=
          restored_next_id);

    SummonedUnitSystem after_reservation {};
    const auto following_spawn =
        after_reservation.summon({});
    REQUIRE(following_spawn.spawned);
    CHECK(following_spawn.unit_id >=
          restored_next_id);
}

TEST_CASE("les statistiques resolues injectees pilotent tout le fantassin") {
    SummonedUnitStats resolved_stats {};
    resolved_stats.duration_seconds = 8.0F;
    resolved_stats.maximum_health = 30.0F;
    resolved_stats.attack_damage = 8.0F;
    resolved_stats.attack_interval_seconds = 0.5F;
    resolved_stats.has_light_taunt = true;
    resolved_stats.has_projectile_block = true;
    resolved_stats.taunt_interval_seconds = 1.0F;
    resolved_stats.taunt_radius = 4.0F;
    resolved_stats.projectile_block_interval_seconds = 2.0F;
    resolved_stats.mastery_survival_health = 2.0F;
    resolved_stats.mastery_damage_reduction = 0.25F;
    resolved_stats.mastery_damage_reduction_seconds = 1.5F;

    SummonedUnitSpawnRequest request {};
    request.owner_id = 707U;
    request.rank = SummonedUnitRank::RankThree;
    request.mastered = true;
    request.stats = resolved_stats;
    request.unit_id = 70'007U;
    request.cast_sequence = 808U;

    SummonedUnitSystem system {};
    REQUIRE(system.summon(request).spawned);
    CHECK(system.state().maximum_health ==
          doctest::Approx(30.0F));
    CHECK(system.state().cast_sequence == 808U);

    std::size_t strike_count = 0U;
    std::size_t taunt_count = 0U;
    std::vector<float> damages {};
    auto callbacks =
        fixed_target_callbacks(
            strike_count,
            taunt_count,
            &damages);
    float taunt_radius = 0.0F;
    callbacks.taunt =
        [&taunt_count, &taunt_radius](
            const SummonedUnitTauntRequest& taunt) {
        ++taunt_count;
        taunt_radius = taunt.radius;
    };
    const auto frame =
        system.update(
            1.0F,
            callbacks);
    CHECK(frame.attack_window_count == 2U);
    CHECK(frame.attack_count == 2U);
    CHECK(frame.taunt_count == 1U);
    REQUIRE(damages.size() == 2U);
    CHECK(damages.front() == doctest::Approx(8.0F));
    CHECK(taunt_radius == doctest::Approx(4.0F));

    const auto projectile =
        system.apply_damage({
            5.0F,
            SummonedUnitDamageKind::Projectile,
        });
    REQUIRE(projectile.blocked);
    CHECK(system.state().projectile_block_cooldown ==
          doctest::Approx(2.0F));

    const auto lethal =
        system.apply_damage({
            100.0F,
            SummonedUnitDamageKind::Melee,
        });
    REQUIRE(lethal.death_refused);
    CHECK(lethal.remaining_health == doctest::Approx(2.0F));
    CHECK(system.state().mastery_damage_reduction_seconds ==
          doctest::Approx(1.5F));

    bool mastery_taunt = false;
    SummonedUnitCallbacks mastery_callbacks {};
    mastery_callbacks.taunt =
        [&mastery_taunt, &taunt_radius](
            const SummonedUnitTauntRequest& taunt) {
        mastery_taunt = taunt.mastery_triggered;
        taunt_radius = taunt.radius;
    };
    CHECK(system.update(0.0F, mastery_callbacks).taunt_count == 1U);
    CHECK(mastery_taunt);
    CHECK(taunt_radius == doctest::Approx(4.0F));

    const auto reduced =
        system.apply_damage({
            1.0F,
            SummonedUnitDamageKind::Melee,
        });
    CHECK(reduced.applied_damage == doctest::Approx(0.75F));
    CHECK(reduced.remaining_health == doctest::Approx(1.25F));
}

TEST_CASE("le snapshot du fantassin reprend exactement tous les timers et jetons") {
    SummonedUnitSpawnRequest request {};
    request.owner_id = 900U;
    request.position = {3.0F, 2.0F, -4.0F};
    request.rank = SummonedUnitRank::RankThree;
    request.mastered = true;
    request.unit_id = 900'001U;
    request.cast_sequence = 123'456U;

    SummonedUnitSystem source {};
    REQUIRE(source.summon(request).spawned);
    std::size_t strikes = 0U;
    std::size_t taunts = 0U;
    auto callbacks =
        fixed_target_callbacks(
            strikes,
            taunts);
    REQUIRE(
        source.update(
                  1.3F,
                  callbacks)
            .attack_window_count == 1U);
    REQUIRE(
        source.apply_damage({
                  3.0F,
                  SummonedUnitDamageKind::Projectile,
              })
            .blocked);
    REQUIRE(
        source.apply_damage({
                  100.0F,
                  SummonedUnitDamageKind::Melee,
              })
            .death_refused);
    const auto saved =
        source.snapshot();
    REQUIRE(saved.pending_mastery_taunt);

    SummonedUnitSystem restored {};
    const auto load =
        restored.load_state(saved);
    CHECK(load.restored);
    CHECK_FALSE(load.sanitized);
    CHECK_FALSE(load.expired);
    CHECK(restored.snapshot() == saved);
    CHECK(restored.state().cast_sequence == 123'456U);

    std::size_t source_mastery_taunts = 0U;
    std::size_t restored_mastery_taunts = 0U;
    SummonedUnitCallbacks source_callbacks {};
    source_callbacks.taunt =
        [&source_mastery_taunts](
            const SummonedUnitTauntRequest& taunt) {
        source_mastery_taunts +=
            taunt.mastery_triggered ? 1U : 0U;
    };
    SummonedUnitCallbacks restored_callbacks {};
    restored_callbacks.taunt =
        [&restored_mastery_taunts](
            const SummonedUnitTauntRequest& taunt) {
        restored_mastery_taunts +=
            taunt.mastery_triggered ? 1U : 0U;
    };
    CHECK(source.update(0.0F, source_callbacks).taunt_count == 1U);
    CHECK(restored.update(0.0F, restored_callbacks).taunt_count == 1U);
    CHECK(source_mastery_taunts == 1U);
    CHECK(restored_mastery_taunts == 1U);
    CHECK(restored.snapshot() == source.snapshot());
}

TEST_CASE("le chargement du fantassin assainit ou rejette les donnees corrompues") {
    SummonedUnitSpawnRequest request {};
    request.owner_id = 11U;
    request.rank = SummonedUnitRank::RankThree;
    request.mastered = true;
    request.unit_id = 11'001U;

    SummonedUnitSystem source {};
    REQUIRE(source.summon(request).spawned);
    auto corrupted =
        source.snapshot();
    corrupted.health =
        corrupted.stats.maximum_health + 50.0F;
    corrupted.next_attack_seconds =
        std::numeric_limits<double>::quiet_NaN();
    corrupted.projectile_block_cooldown =
        std::numeric_limits<float>::infinity();
    corrupted.yaw_radians =
        std::numeric_limits<float>::quiet_NaN();
    corrupted.animation_time =
        corrupted.stats.duration_seconds + 1.0F;
    corrupted.pending_mastery_taunt = true;

    SummonedUnitSystem restored {};
    const auto sanitized =
        restored.load_state(corrupted);
    CHECK(sanitized.restored);
    CHECK(sanitized.sanitized);
    const auto normalized =
        restored.snapshot();
    CHECK(normalized.health ==
          doctest::Approx(
              normalized.stats.maximum_health));
    CHECK(std::isfinite(normalized.next_attack_seconds));
    CHECK(normalized.projectile_block_cooldown ==
          doctest::Approx(0.0F));
    CHECK(normalized.yaw_radians == doctest::Approx(0.0F));
    CHECK(normalized.animation_time == doctest::Approx(0.0F));
    CHECK_FALSE(normalized.pending_mastery_taunt);

    auto invalid =
        source.snapshot();
    invalid.unit_id = 0U;
    const auto rejected =
        restored.load_state(invalid);
    CHECK_FALSE(rejected.restored);
    CHECK(rejected.sanitized);
    CHECK_FALSE(restored.active());

    auto expired =
        source.snapshot();
    expired.age_seconds =
        expired.stats.duration_seconds;
    const auto expiration =
        restored.load_state(expired);
    CHECK_FALSE(expiration.restored);
    CHECK(expiration.sanitized);
    CHECK(expiration.expired);
    CHECK_FALSE(restored.active());

    SummonedUnitSystemSnapshot stale_inactive {};
    stale_inactive.owner_id = 999U;
    stale_inactive.health = 12.0F;
    const auto inactive =
        restored.load_state(stale_inactive);
    CHECK_FALSE(inactive.restored);
    CHECK(inactive.sanitized);
    CHECK(restored.snapshot() ==
          SummonedUnitSystemSnapshot {});
}

TEST_CASE("des statistiques resolues invalides ne modifient pas le fantassin") {
    SummonedUnitSystem system {};
    SummonedUnitSpawnRequest valid {};
    valid.owner_id = 1U;
    valid.unit_id = 33'001U;
    REQUIRE(system.summon(valid).spawned);
    const auto before =
        system.snapshot();

    auto invalid_stats =
        summoned_unit_stats(
            SummonedUnitRank::RankThree);
    invalid_stats.attack_interval_seconds = 0.0F;
    SummonedUnitSpawnRequest invalid {};
    invalid.owner_id = 2U;
    invalid.rank = SummonedUnitRank::RankThree;
    invalid.stats = invalid_stats;
    invalid.unit_id = 33'002U;
    CHECK_FALSE(system.summon(invalid).spawned);
    CHECK(system.snapshot() == before);

    auto vanishing_stats =
        summoned_unit_stats(
            SummonedUnitRank::RankOne);
    vanishing_stats.maximum_health = 0.001F;
    vanishing_stats.mastery_survival_health = 0.001F;
    SummonedUnitSpawnRequest vanishing {};
    vanishing.owner_id = 3U;
    vanishing.stats = vanishing_stats;
    vanishing.health_power_multiplier = 0.25F;
    vanishing.unit_id = 33'003U;
    CHECK_FALSE(system.summon(vanishing).spawned);
    CHECK(system.snapshot() == before);
}

TEST_CASE("le seuil de survie reste sauvegardable apres la mise a l'echelle") {
    auto stats =
        summoned_unit_stats(
            SummonedUnitRank::RankOne);
    stats.maximum_health = 2.0F;
    stats.mastery_survival_health = 2.0F;

    SummonedUnitSpawnRequest request {};
    request.owner_id = 44U;
    request.mastered = true;
    request.stats = stats;
    request.health_power_multiplier = 0.25F;
    request.unit_id = 44'001U;

    SummonedUnitSystem source {};
    REQUIRE(source.summon(request).spawned);
    const auto saved =
        source.snapshot();
    CHECK(saved.stats.maximum_health ==
          doctest::Approx(0.5F));
    CHECK(saved.stats.mastery_survival_health ==
          doctest::Approx(0.5F));

    SummonedUnitSystem restored {};
    const auto load =
        restored.load_state(saved);
    CHECK(load.restored);
    CHECK_FALSE(load.sanitized);
    CHECK(restored.snapshot() == saved);
}

} // namespace valcraft
