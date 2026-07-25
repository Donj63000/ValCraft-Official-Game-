#include "creatures/CreatureSystem.h"

#include "TestUtils.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <limits>

namespace valcraft {

namespace {

auto make_guard_test_creature(CreatureSpecies species,
                              const BlockCoord& ground,
                              CreaturePhase phase,
                              float morph_factor) -> CreatureInstance {
    CreatureInstance creature {};
    creature.anchor.chunk = {
        World::floor_div(ground.x, kChunkSizeX),
        World::floor_div(ground.z, kChunkSizeZ),
    };
    creature.anchor.ground_block = ground;
    creature.anchor.spawn_position = {
        static_cast<float>(ground.x) + 0.5F,
        static_cast<float>(ground.y) + 1.001F,
        static_cast<float>(ground.z) + 0.5F,
    };
    creature.anchor.species = species;
    creature.position = creature.anchor.spawn_position;
    creature.phase = phase;
    creature.morph_factor = morph_factor;
    creature.health = creature_max_health(species);
    creature.hit_direction = {0.0F, 0.0F, 1.0F};
    return creature;
}

} // namespace

TEST_CASE("old guard world rays distinguish opaque visibility from collidable projectiles") {
    World world(96001, 1);
    test::make_chunk_empty(world, {0, 0});
    world.set_block(2, 10, 0, to_block_id(BlockType::Glass));
    world.set_block(4, 10, 0, to_block_id(BlockType::Stone));

    const glm::vec3 origin {0.5F, 10.5F, 0.5F};
    const glm::vec3 direction {1.0F, 0.0F, 0.0F};

    const auto selection = world.raycast(origin, direction, 8.0F);
    const auto visibility = world.raycast_visibility(origin, direction, 8.0F);
    const auto projectile = world.raycast_collidable(origin, direction, 8.0F);

    REQUIRE(selection.hit);
    CHECK(selection.block == BlockCoord {2, 10, 0});
    REQUIRE(visibility.hit);
    CHECK(visibility.block == BlockCoord {4, 10, 0});
    REQUIRE(projectile.hit);
    CHECK(projectile.block == BlockCoord {2, 10, 0});
}

TEST_CASE("old guard disposition accepts only fully transformed night wildlife") {
    const auto night_pig =
        make_guard_test_creature(CreatureSpecies::Pig, {0, 12, 0}, CreaturePhase::Night, 1.0F);
    auto night_cow = night_pig;
    night_cow.anchor.species = CreatureSpecies::Cow;
    auto night_sheep = night_pig;
    night_sheep.anchor.species = CreatureSpecies::Sheep;
    auto incomplete = night_pig;
    incomplete.morph_factor = 0.998F;
    auto dusk = night_pig;
    dusk.phase = CreaturePhase::DuskMorph;
    auto dawn = night_pig;
    dawn.phase = CreaturePhase::DawnRecover;
    auto villager = night_pig;
    villager.anchor.species = CreatureSpecies::Villager;

    CHECK(is_hostile_creature(night_pig));
    CHECK(is_hostile_creature(night_cow));
    CHECK(is_hostile_creature(night_sheep));
    CHECK_FALSE(is_hostile_creature(incomplete));
    CHECK_FALSE(is_hostile_creature(dusk));
    CHECK_FALSE(is_hostile_creature(dawn));
    CHECK_FALSE(is_hostile_creature(villager));
}

TEST_CASE("creature ids stay anchored and first-hit rays preserve neutral blockers") {
    auto neutral =
        make_guard_test_creature(CreatureSpecies::Pig, {0, 12, 3}, CreaturePhase::Day, 0.0F);
    auto hostile =
        make_guard_test_creature(CreatureSpecies::Cow, {0, 12, 6}, CreaturePhase::Night, 1.0F);
    const auto neutral_id = creature_id_from_anchor(neutral.anchor);
    auto moved_anchor = neutral.anchor;
    moved_anchor.spawn_position += glm::vec3 {9.0F, 3.0F, -4.0F};

    CHECK(neutral_id != 0U);
    CHECK(creature_id_from_anchor(moved_anchor) == neutral_id);
    CHECK(creature_id_from_anchor(hostile.anchor) != neutral_id);

    CreatureSystem system {};
    system.load_creatures({neutral, hostile}, EnvironmentClock::compute_state(23.0F));
    const glm::vec3 origin {0.5F, 13.50F, 0.5F};
    const auto first = system.raycast_first_creature(origin, {0.0F, 0.0F, 1.0F}, 50.0F);

    REQUIRE(first.hit);
    CHECK(first.id == neutral_id);
    CHECK(first.disposition == CreatureDisposition::Neutral);

    const auto neutral_death = system.apply_damage(
        first.id,
        20.0F,
        CreatureDamageSource::OldGuard,
        first.position - origin);
    REQUIRE(neutral_death.hit);
    CHECK(neutral_death.killed);
    CHECK_FALSE(neutral_death.grants_player_rewards);

    const auto second = system.raycast_first_creature(origin, {0.0F, 0.0F, 1.0F}, 50.0F);
    REQUIRE(second.hit);
    CHECK(second.id == creature_id_from_anchor(hostile.anchor));
    CHECK(second.disposition == CreatureDisposition::Hostile);
}

TEST_CASE("generic creature damage keeps player rewards source-specific") {
    auto creature =
        make_guard_test_creature(CreatureSpecies::Sheep, {0, 12, 4}, CreaturePhase::Night, 1.0F);
    CreatureSystem system {};
    system.load_creatures({creature}, EnvironmentClock::compute_state(23.0F));

    const auto invalid = system.apply_damage(
        creature_id_from_anchor(creature.anchor),
        std::numeric_limits<float>::quiet_NaN(),
        CreatureDamageSource::Player);
    CHECK_FALSE(invalid.hit);

    const auto player_death = system.try_damage_by_ray(
        {0.5F, 13.50F, 0.5F},
        {0.0F, 0.0F, 1.0F},
        50.0F,
        20.0F,
        CreatureDamageSource::Player);
    REQUIRE(player_death.hit);
    CHECK(player_death.killed);
    CHECK(player_death.grants_player_rewards);
    CHECK(player_death.source == CreatureDamageSource::Player);
}

TEST_CASE("secondary population interest is bounded sanitized and inactive by default") {
    CreatureSystem system {};
    CHECK_FALSE(system.secondary_population_interest().has_value());

    system.set_secondary_population_interest({136.0F, 13.0F, 8.0F}, 99);
    REQUIRE(system.secondary_population_interest().has_value());
    CHECK(system.secondary_population_interest()->radius_chunks == 16);

    system.set_secondary_population_interest({136.5F, 13.0F, 8.5F}, 4);
    REQUIRE(system.secondary_population_interest().has_value());
    CHECK(system.secondary_population_interest()->radius_chunks == 4);
    CHECK(system.secondary_population_interest()->center.x == doctest::Approx(136.5F));

    system.set_secondary_population_interest(
        {std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F},
        4);
    CHECK_FALSE(system.secondary_population_interest().has_value());

    const auto finite_max = std::numeric_limits<float>::max();
    system.set_secondary_population_interest({finite_max, finite_max, -finite_max}, 4);
    REQUIRE(system.secondary_population_interest().has_value());
    CHECK(system.secondary_population_interest()->center.x ==
          doctest::Approx(kCreaturePopulationInterestCoordinateLimit));
    CHECK(system.secondary_population_interest()->center.y ==
          doctest::Approx(kCreaturePopulationInterestCoordinateLimit));
    CHECK(system.secondary_population_interest()->center.z ==
          doctest::Approx(-kCreaturePopulationInterestCoordinateLimit));
}

TEST_CASE("secondary population interest activates loaded wildlife around a distant ship") {
    CreatureSystem system {};
    World world(7001, 1);
    const ChunkCoord ship_chunk {8, 0};
    test::make_chunk_surface(
        world,
        ship_chunk,
        12,
        to_block_id(BlockType::Grass),
        to_block_id(BlockType::Dirt));
    REQUIRE(system.spawn_anchor_for_chunk(world, ship_chunk).has_value());

    const auto environment = EnvironmentClock::compute_state(23.0F);
    const auto cycle = EnvironmentClock::classify_creature_cycle(23.0F);
    const glm::vec3 player_position {0.5F, 13.001F, 0.5F};
    system.update(0.0F, world, player_position, environment, cycle);
    CHECK(std::none_of(
        system.active_creatures().begin(),
        system.active_creatures().end(),
        [&](const CreatureInstance& creature) {
            return creature.anchor.chunk == ship_chunk;
        }));

    system.set_secondary_population_interest({136.0F, 13.001F, 8.0F}, 4);
    system.update(0.0F, world, player_position, environment, cycle);
    CHECK(std::any_of(
        system.active_creatures().begin(),
        system.active_creatures().end(),
        [&](const CreatureInstance& creature) {
            return creature.anchor.chunk == ship_chunk;
        }));
}

} // namespace valcraft
