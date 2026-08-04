#include "gameplay/BackroomsMarlowWorld.h"

#include "gameplay/PlayerController.h"
#include "world/World.h"

#include "TestUtils.h"

#include <doctest/doctest.h>
#include <glm/geometric.hpp>

#include <array>
#include <cmath>
#include <limits>

namespace valcraft {

namespace {

void prepare_marlow_test_room(World& world) {
    test::make_chunk_empty(world, {0, 0});
    test::make_chunk_empty(world, {1, 0});
    test::make_flat_floor(world, 0, 31, 0, 0, 15);
}

void place_player_wall(World& world, int x, int z) {
    world.set_block(x, 1, z, to_block_id(BlockType::Stone));
    world.set_block(x, 2, z, to_block_id(BlockType::Stone));
}

} // namespace

TEST_CASE("Marlow drag sweep reaches a free target and caps travel at four metres") {
    World world(7401, 1);
    prepare_marlow_test_room(world);
    const PlayerController player({1.5F, 1.001F, 1.5F});

    const auto free = sweep_backrooms_marlow_drag(
        player,
        world,
        player.position(),
        {3.5F, 1.001F, 1.5F},
        4.0F);
    CHECK_FALSE(free.blocked);
    CHECK(free.position.x == doctest::Approx(3.5F));

    const auto capped = sweep_backrooms_marlow_drag(
        player,
        world,
        player.position(),
        {12.5F, 1.001F, 1.5F},
        99.0F);
    CHECK_FALSE(capped.blocked);
    CHECK(glm::length(capped.position - player.position()) ==
          doctest::Approx(kBackroomsMarlowMaximumDragDistance));
}

TEST_CASE("Marlow drag sweep stops before frontal and diagonal walls without tunnelling") {
    World world(7402, 1);
    prepare_marlow_test_room(world);
    const PlayerController player({1.5F, 1.001F, 1.5F});

    place_player_wall(world, 3, 1);
    const auto frontal = sweep_backrooms_marlow_drag(
        player,
        world,
        player.position(),
        {8.5F, 1.001F, 1.5F},
        4.0F);
    CHECK(frontal.blocked);
    CHECK(frontal.position.x < 2.71F);
    CHECK_FALSE(player.collides_at(world, frontal.position));

    world.set_block(3, 1, 1, to_block_id(BlockType::Air));
    world.set_block(3, 2, 1, to_block_id(BlockType::Air));
    place_player_wall(world, 3, 3);
    const auto diagonal = sweep_backrooms_marlow_drag(
        player,
        world,
        player.position(),
        {6.5F, 1.001F, 6.5F},
        4.0F);
    CHECK(diagonal.blocked);
    CHECK(diagonal.position.x < 2.71F);
    CHECK(diagonal.position.z < 2.71F);
    CHECK_FALSE(player.collides_at(world, diagonal.position));
}

TEST_CASE("Marlow drag sweep rejects invalid inputs and detects an invalid start even without travel") {
    constexpr auto nan = std::numeric_limits<float>::quiet_NaN();
    constexpr auto infinity = std::numeric_limits<float>::infinity();
    constexpr auto maximum = std::numeric_limits<float>::max();
    World world(7403, 1);
    prepare_marlow_test_room(world);
    const PlayerController player({1.5F, 1.001F, 1.5F});

    const auto stationary = sweep_backrooms_marlow_drag(
        player,
        world,
        {4.5F, 1.001F, 1.5F},
        {4.5F, 1.001F, 1.5F},
        0.0F);
    CHECK_FALSE(stationary.blocked);
    CHECK(stationary.position.x == doctest::Approx(4.5F));

    place_player_wall(world, 1, 1);
    const auto embedded = sweep_backrooms_marlow_drag(
        player,
        world,
        player.position(),
        player.position(),
        0.0F);
    CHECK(embedded.blocked);

    const auto invalid_current = sweep_backrooms_marlow_drag(
        player,
        world,
        {nan, 1.0F, 1.0F},
        {2.0F, 1.0F, 1.0F},
        1.0F);
    CHECK(invalid_current.blocked);
    CHECK(std::isfinite(invalid_current.position.x));

    const auto invalid_target = sweep_backrooms_marlow_drag(
        player,
        world,
        {4.5F, 1.001F, 1.5F},
        {infinity, 1.0F, 1.0F},
        1.0F);
    CHECK(invalid_target.blocked);
    CHECK(invalid_target.position.x == doctest::Approx(4.5F));

    const auto invalid_distance = sweep_backrooms_marlow_drag(
        player,
        world,
        {4.5F, 1.001F, 1.5F},
        {5.5F, 1.001F, 1.5F},
        nan);
    CHECK(invalid_distance.blocked);

    const auto non_positive_distance = sweep_backrooms_marlow_drag(
        player,
        world,
        {4.5F, 1.001F, 1.5F},
        {5.5F, 1.001F, 1.5F},
        -1.0F);
    CHECK_FALSE(non_positive_distance.blocked);
    CHECK(non_positive_distance.position.x == doctest::Approx(4.5F));

    const auto out_of_world = sweep_backrooms_marlow_drag(
        player,
        world,
        {maximum, 1.0F, 1.0F},
        {2.0F, 1.0F, 1.0F},
        1.0F);
    CHECK(out_of_world.blocked);
    CHECK(std::isfinite(out_of_world.position.x));

    // Je couvre chaque composante et chaque borne du contrat World : aucune
    // conversion flottant-vers-entier dangereuse ne doit atteindre la grille.
    const std::array<glm::vec3, 6U> hostile_positions {{
        {1.0F, nan, 1.0F},
        {1.0F, 1.0F, nan},
        {-maximum, 1.0F, 1.0F},
        {1.0F, -maximum, 1.0F},
        {1.0F, maximum, 1.0F},
        {1.0F, 1.0F, -maximum},
    }};
    for (const auto& hostile_position : hostile_positions) {
        const auto rejected = sweep_backrooms_marlow_drag(
            player,
            world,
            hostile_position,
            {2.0F, 1.0F, 1.0F},
            1.0F);
        CHECK(rejected.blocked);
        CHECK(std::isfinite(rejected.position.x));
        CHECK(std::isfinite(rejected.position.y));
        CHECK(std::isfinite(rejected.position.z));
    }

    const auto out_of_world_z = sweep_backrooms_marlow_drag(
        player,
        world,
        {1.0F, 1.0F, maximum},
        {2.0F, 1.0F, 1.0F},
        1.0F);
    CHECK(out_of_world_z.blocked);
    CHECK(std::isfinite(out_of_world_z.position.z));
}

TEST_CASE("Marlow flashlight ray sees water through glass but not through an opaque wall") {
    World world(7404, 1);
    prepare_marlow_test_room(world);
    const auto eye = glm::vec3 {1.5F, 2.5F, 1.5F};
    const auto direction = glm::vec3 {1.0F, 0.0F, 0.0F};
    world.set_block(5, 2, 1, to_block_id(BlockType::Water));

    CHECK(backrooms_marlow_flashlight_hits_water(
        world, eye, direction, true, 1.0F));
    world.set_block(3, 2, 1, to_block_id(BlockType::Glass));
    CHECK(backrooms_marlow_flashlight_hits_water(
        world, eye, direction, true, 1.0F));
    const auto transparent_hit =
        world.raycast_water_or_opaque(eye, direction, 18.0F);
    REQUIRE(transparent_hit.hit);
    CHECK(transparent_hit.block_id == to_block_id(BlockType::Water));
    world.set_block(3, 2, 1, to_block_id(BlockType::Stone));
    CHECK_FALSE(backrooms_marlow_flashlight_hits_water(
        world, eye, direction, true, 1.0F));
    const auto opaque_hit =
        world.raycast_water_or_opaque(eye, direction, 18.0F);
    REQUIRE(opaque_hit.hit);
    CHECK(opaque_hit.block_id == to_block_id(BlockType::Stone));
}

TEST_CASE("Marlow flashlight ray validates activation direction and its eighteen metre range") {
    constexpr auto nan = std::numeric_limits<float>::quiet_NaN();
    constexpr auto maximum = std::numeric_limits<float>::max();
    World world(7405, 1);
    prepare_marlow_test_room(world);
    const auto eye = glm::vec3 {0.5F, 2.5F, 1.5F};
    world.set_block(18, 2, 1, to_block_id(BlockType::Water));

    CHECK(backrooms_marlow_flashlight_hits_water(
        world, eye, {1.0F, 0.0F, 0.0F}, true, 0.25F));
    CHECK_FALSE(backrooms_marlow_flashlight_hits_water(
        world, eye, {1.0F, 0.0F, 0.0F}, false, 1.0F));
    CHECK_FALSE(backrooms_marlow_flashlight_hits_water(
        world, eye, {1.0F, 0.0F, 0.0F}, true, 0.0F));
    CHECK_FALSE(backrooms_marlow_flashlight_hits_water(
        world, eye, {0.0F, 0.0F, 0.0F}, true, 1.0F));
    CHECK_FALSE(backrooms_marlow_flashlight_hits_water(
        world, eye, {nan, 0.0F, 0.0F}, true, 1.0F));
    CHECK_FALSE(backrooms_marlow_flashlight_hits_water(
        world, eye, {1.0F, 0.0F, 0.0F}, true, nan));
    CHECK_FALSE(backrooms_marlow_flashlight_hits_water(
        world, eye, {maximum, 0.0F, 0.0F}, true, 1.0F));
    CHECK_FALSE(backrooms_marlow_flashlight_hits_water(
        world, eye, {1.0F, 0.0F, 0.0F}, true, 1.0F, nan));
    CHECK_FALSE(backrooms_marlow_flashlight_hits_water(
        world, eye, {1.0F, 0.0F, 0.0F}, true, 1.0F, -1.0F));
    CHECK_FALSE(backrooms_marlow_flashlight_hits_water(
        world,
        {maximum, 2.5F, 1.5F},
        {1.0F, 0.0F, 0.0F},
        true,
        1.0F));

    world.set_block(18, 2, 1, to_block_id(BlockType::Air));
    world.set_block(19, 2, 1, to_block_id(BlockType::Water));
    CHECK_FALSE(backrooms_marlow_flashlight_hits_water(
        world, eye, {1.0F, 0.0F, 0.0F}, true, 1.0F, 99.0F));
}

TEST_CASE("player water snapshot reads authoritative World overrides at feet body and head") {
    World world(7406, 1);
    prepare_marlow_test_room(world);
    const PlayerController player({4.5F, 1.001F, 4.5F});

    auto contact = player.sample_world_water_contact(world, player.position());
    CHECK_FALSE(contact.any_contact());

    world.set_block(4, 1, 4, to_block_id(BlockType::Water));
    contact = player.sample_world_water_contact(world, player.position());
    CHECK(contact.feet_in_water);
    CHECK_FALSE(contact.body_in_water);
    CHECK_FALSE(contact.head_in_water);

    world.set_block(4, 2, 4, to_block_id(BlockType::Water));
    contact = player.sample_world_water_contact(world, player.position());
    CHECK(contact.feet_in_water);
    CHECK(contact.body_in_water);
    CHECK(contact.head_in_water);
    CHECK(contact.swimming);

    world.set_block(4, 1, 4, to_block_id(BlockType::Air));
    world.set_block(4, 2, 4, to_block_id(BlockType::Air));
    contact = player.sample_world_water_contact(world, player.position());
    CHECK_FALSE(contact.any_contact());

    constexpr auto nan = std::numeric_limits<float>::quiet_NaN();
    CHECK_FALSE(player.sample_world_water_contact(
                          world,
                          {nan, 1.0F, 1.0F})
                    .any_contact());
}

TEST_CASE("Marlow death delay preserves its arming frame and consumes the full screamer") {
    auto remaining = advance_backrooms_marlow_death_delay(
        0.85F,
        0.25F,
        true);
    CHECK(remaining == doctest::Approx(0.85F));

    constexpr std::array<float, 4U> expected {{
        0.60F,
        0.35F,
        0.10F,
        0.0F,
    }};
    for (const auto value : expected) {
        remaining = advance_backrooms_marlow_death_delay(
            remaining,
            0.25F,
            false);
        CHECK(remaining == doctest::Approx(value).epsilon(1.0e-5));
    }

    CHECK(advance_backrooms_marlow_death_delay(
              0.50F,
              std::numeric_limits<float>::quiet_NaN(),
              false) == doctest::Approx(0.50F));
    CHECK(advance_backrooms_marlow_death_delay(
              std::numeric_limits<float>::infinity(),
              0.25F,
              false) == doctest::Approx(0.0F));
}

} // namespace valcraft
