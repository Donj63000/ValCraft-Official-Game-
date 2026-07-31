#include "creatures/legendary/LegendaryEnemySystem.h"

#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <limits>

namespace valcraft {

namespace {

[[nodiscard]] auto contains_event(
    LegendaryEnemySystem& system,
    LegendaryEnemyEventKind expected) {
    std::array<LegendaryEnemyEvent, kMaximumLegendaryEnemyEvents>
        events {};
    const auto count = system.consume_events(events);
    for (std::size_t index = 0U; index < count; ++index) {
        if (events[index].kind == expected) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] auto spawn_enemy(
    LegendaryEnemySystem& system,
    LegendaryEnemyArchetype archetype,
    std::uint32_t seed = 17U,
    glm::vec3 position = {}) -> LegendaryEnemyId {
    const auto result = system.spawn({
        archetype,
        seed,
        position,
        0.0F,
    });
    REQUIRE(result.spawned);
    return result.id;
}

} // namespace

TEST_CASE("les profils legendaires couvrent les plages et roles demandes") {
    const auto brute =
        legendary_enemy_profile(
            LegendaryEnemyArchetype::CorruptedBrute);
    CHECK(brute.minimum_health == doctest::Approx(60.0F));
    CHECK(brute.maximum_health == doctest::Approx(100.0F));
    CHECK(brute.weight == EntityWeight::Heavy);
    CHECK(brute.corrupted);
    CHECK(brute.attack_windup_seconds >= 0.8F);

    const auto hunter =
        legendary_enemy_profile(
            LegendaryEnemyArchetype::SwiftHunter);
    CHECK(hunter.minimum_health == doctest::Approx(25.0F));
    CHECK(hunter.maximum_health == doctest::Approx(45.0F));
    CHECK(hunter.movement_speed > brute.movement_speed * 2.0F);

    const auto guard =
        legendary_enemy_profile(
            LegendaryEnemyArchetype::ArmoredGuard);
    CHECK(guard.minimum_health == doctest::Approx(100.0F));
    CHECK(guard.maximum_health == doctest::Approx(160.0F));
    CHECK(guard.maximum_armor > 0.0F);
    CHECK(guard.frontal_armor_reduction > 0.6F);

    const auto guardian =
        legendary_enemy_profile(
            LegendaryEnemyArchetype::ForgeGuardian);
    CHECK(guardian.weight == EntityWeight::Boss);
    CHECK(guardian.reward.experience_points == 900U);

    const auto astral_boss =
        legendary_enemy_profile(
            LegendaryEnemyArchetype::AstralBoss);
    CHECK(astral_boss.weight == EntityWeight::Boss);
    CHECK(astral_boss.astral);
    CHECK(astral_boss.minimum_health >= 450.0F);
    CHECK(astral_boss.maximum_stagger >= 120.0F);

    const auto minion =
        legendary_enemy_profile(
            LegendaryEnemyArchetype::ArenaMinion);
    CHECK(minion.minimum_health == doctest::Approx(8.0F));
    CHECK(minion.maximum_health == doctest::Approx(12.0F));
    CHECK(minion.reward.experience_points == 0U);
    CHECK(minion.temporary_reward_suppressed);
}

TEST_CASE("le tirage de points de vie et l IA restent deterministes") {
    LegendaryEnemySystem first {};
    LegendaryEnemySystem second {};
    const auto first_spawn = first.spawn({
        LegendaryEnemyArchetype::CorruptedBrute,
        0xA51CEU,
        glm::vec3 {-3.0F, 4.0F, 1.0F},
        0.4F,
    });
    const auto second_spawn = second.spawn({
        LegendaryEnemyArchetype::CorruptedBrute,
        0xA51CEU,
        glm::vec3 {-3.0F, 4.0F, 1.0F},
        0.4F,
    });
    REQUIRE(first_spawn.spawned);
    REQUIRE(second_spawn.spawned);
    CHECK(
        first_spawn.maximum_health ==
        doctest::Approx(second_spawn.maximum_health));
    CHECK(first_spawn.maximum_health >= 60.0F);
    CHECK(first_spawn.maximum_health <= 100.0F);

    const LegendaryEnemyWorldInput input {
        glm::vec3 {5.0F, 4.0F, 2.0F},
        true,
        false,
        false,
    };
    for (auto index = 0; index < 240; ++index) {
        CHECK(first.update(kLegendaryEnemyFixedStepSeconds, input).accepted);
        CHECK(second.update(kLegendaryEnemyFixedStepSeconds, input).accepted);
    }
    const auto first_view =
        first.render_snapshot(first_spawn.id);
    const auto second_view =
        second.render_snapshot(second_spawn.id);
    REQUIRE(first_view.has_value());
    REQUIRE(second_view.has_value());
    CHECK(first_view->position.x == doctest::Approx(second_view->position.x));
    CHECK(first_view->position.y == doctest::Approx(second_view->position.y));
    CHECK(first_view->position.z == doctest::Approx(second_view->position.z));
    CHECK(first_view->behavior == second_view->behavior);
}

TEST_CASE("la brute annonce lisiblement son attaque avant de la rendre active") {
    LegendaryEnemySystem system {};
    static_cast<void>(spawn_enemy(
        system,
        LegendaryEnemyArchetype::CorruptedBrute,
        1U,
        glm::vec3 {0.0F, 0.0F, 0.0F}));
    std::array<LegendaryEnemyEvent, 8U> discarded {};
    static_cast<void>(system.consume_events(discarded));

    const LegendaryEnemyWorldInput input {
        glm::vec3 {0.0F, 0.0F, 2.0F},
        true,
        false,
        false,
    };
    CHECK(system.update(kLegendaryEnemyFixedStepSeconds, input).accepted);
    CHECK(contains_event(
        system,
        LegendaryEnemyEventKind::AttackTelegraphed));
    for (auto index = 0; index < 60; ++index) {
        static_cast<void>(
            system.update(kLegendaryEnemyFixedStepSeconds, input));
    }
    CHECK(contains_event(
        system,
        LegendaryEnemyEventKind::AttackActive));
}

TEST_CASE("le chasseur esquive lateralement un grand coup annonce") {
    LegendaryEnemySystem system {};
    const auto id =
        spawn_enemy(
            system,
            LegendaryEnemyArchetype::SwiftHunter,
            2U);
    const auto before = system.render_snapshot(id);
    REQUIRE(before.has_value());

    const auto update = system.update(
        kLegendaryEnemyFixedStepSeconds,
        {
            glm::vec3 {0.0F, 0.0F, 3.0F},
            true,
            true,
            false,
        });
    REQUIRE(update.accepted);
    const auto after = system.render_snapshot(id);
    REQUIRE(after.has_value());
    CHECK(after->behavior == LegendaryEnemyBehavior::Strafe);
    CHECK(std::abs(after->position.x - before->position.x) > 1.0F);
    CHECK(contains_event(system, LegendaryEnemyEventKind::Dodged));
}

TEST_CASE("les plaques du garde reduisent le front puis peuvent etre brisees") {
    LegendaryEnemySystem front {};
    LegendaryEnemySystem rear {};
    const auto front_id =
        spawn_enemy(
            front,
            LegendaryEnemyArchetype::ArmoredGuard,
            77U);
    const auto rear_id =
        spawn_enemy(
            rear,
            LegendaryEnemyArchetype::ArmoredGuard,
            77U);

    const auto front_hit =
        front.apply_hit(front_id, {20.0F, 0.0F, true, 0U});
    const auto rear_hit =
        rear.apply_hit(rear_id, {20.0F, 0.0F, false, 0U});
    REQUIRE(front_hit.accepted);
    REQUIRE(rear_hit.accepted);
    CHECK(front_hit.applied_health_damage < rear_hit.applied_health_damage);
    CHECK(front_hit.applied_armor_damage > rear_hit.applied_armor_damage);

    const auto breaking_hit =
        front.apply_hit(front_id, {100.0F, 0.0F, true, 0U});
    REQUIRE(breaking_hit.accepted);
    CHECK(breaking_hit.armor_broken_now);
    CHECK(breaking_hit.remaining_armor == doctest::Approx(0.0F));
    const auto unarmored_hit =
        front.apply_hit(front_id, {10.0F, 0.0F, true, 0U});
    REQUIRE(unarmored_hit.accepted);
    CHECK(unarmored_hit.applied_health_damage == doctest::Approx(10.0F));
}

TEST_CASE("la jauge de desequilibre interrompt puis se remet proprement") {
    LegendaryEnemySystem system {};
    const auto id =
        spawn_enemy(
            system,
            LegendaryEnemyArchetype::CorruptedBrute,
            91U);
    const auto hit =
        system.apply_hit(id, {0.0F, 80.0F, true, 0U});
    REQUIRE(hit.accepted);
    CHECK(hit.staggered_now);
    const auto staggered = system.render_snapshot(id);
    REQUIRE(staggered.has_value());
    CHECK(staggered->behavior == LegendaryEnemyBehavior::Staggered);
    CHECK(staggered->stagger_ratio == doctest::Approx(1.0F));

    for (auto index = 0; index < 75; ++index) {
        static_cast<void>(system.update(
            kLegendaryEnemyFixedStepSeconds,
            {
                glm::vec3 {0.0F, 0.0F, 20.0F},
                true,
                false,
                false,
            }));
    }
    const auto recovered = system.render_snapshot(id);
    REQUIRE(recovered.has_value());
    CHECK(recovered->behavior != LegendaryEnemyBehavior::Staggered);
    CHECK(recovered->stagger_ratio == doctest::Approx(0.0F));
}

TEST_CASE("la creature astrale devient pleinement vulnerable a l eveil deux") {
    LegendaryEnemySystem dormant {};
    LegendaryEnemySystem astral {};
    const auto dormant_id =
        spawn_enemy(
            dormant,
            LegendaryEnemyArchetype::AstralCreature,
            32U);
    const auto astral_id =
        spawn_enemy(
            astral,
            LegendaryEnemyArchetype::AstralCreature,
            32U);
    const auto deflected =
        dormant.apply_hit(dormant_id, {40.0F, 0.0F, true, 0U});
    const auto effective =
        astral.apply_hit(astral_id, {40.0F, 0.0F, true, 2U});
    REQUIRE(deflected.accepted);
    REQUIRE(effective.accepted);
    CHECK(
        deflected.astral_interaction ==
        AstralHitInteraction::Deflected);
    CHECK(
        effective.astral_interaction ==
        AstralHitInteraction::FullyEffective);
    CHECK(
        effective.applied_health_damage >
        deflected.applied_health_damage * 10.0F);
}

TEST_CASE("la projection deplace les ennemis legers mais jamais un boss") {
    LegendaryEnemySystem system {};
    const auto light_id =
        spawn_enemy(
            system,
            LegendaryEnemyArchetype::SwiftHunter,
            7U);
    const auto boss_id =
        spawn_enemy(
            system,
            LegendaryEnemyArchetype::ForgeGuardian,
            8U,
            {5.0F, 0.0F, 0.0F});
    const auto before = system.render_snapshot(light_id);
    REQUIRE(before.has_value());
    REQUIRE(system.apply_knockback(
        light_id,
        {1.0F, 4.0F, 0.0F},
        2.0F));
    const auto after = system.render_snapshot(light_id);
    REQUIRE(after.has_value());
    CHECK(after->position.x ==
          doctest::Approx(before->position.x + 2.0F));
    CHECK(after->position.y == doctest::Approx(before->position.y));
    CHECK_FALSE(system.apply_knockback(
        boss_id,
        {1.0F, 0.0F, 0.0F},
        2.0F));
}

TEST_CASE("les petites creatures meurent en un balayage sans recompense") {
    LegendaryEnemySystem system {};
    const auto id =
        spawn_enemy(
            system,
            LegendaryEnemyArchetype::ArenaMinion,
            5U);
    const auto hit =
        system.apply_hit(id, {14.0F, 20.0F, true, 3U});
    REQUIRE(hit.accepted);
    CHECK(hit.killed_now);
    CHECK(hit.reward.experience_points == 0U);
    CHECK_FALSE(contains_event(
        system,
        LegendaryEnemyEventKind::RewardAvailable));
}

TEST_CASE("la capacite et les entrees invalides sont refusees sans mutation") {
    LegendaryEnemySystem system {};
    for (std::size_t index = 0U;
         index < kMaximumLegendaryEnemies;
         ++index) {
        const auto spawn = system.spawn({
            LegendaryEnemyArchetype::ArenaMinion,
            static_cast<std::uint32_t>(index),
            glm::vec3 {static_cast<float>(index), 0.0F, 0.0F},
            0.0F,
        });
        REQUIRE(spawn.spawned);
    }
    CHECK(system.size() == kMaximumLegendaryEnemies);
    CHECK(
        system.spawn({}).error ==
        LegendaryEnemySpawnError::CapacityReached);
    const auto invalid_update = system.update(
        std::numeric_limits<float>::quiet_NaN(),
        {});
    CHECK_FALSE(invalid_update.accepted);
    CHECK(system.size() == kMaximumLegendaryEnemies);
    CHECK(
        LegendaryEnemySystem::persistence_policy() ==
        TemporaryPersistencePolicy::NeverSaved);
}

} // namespace valcraft
