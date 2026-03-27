#include "app/InventoryMenu.h"
#include "app/Hotbar.h"
#include "creatures/CreatureSystem.h"
#include "gameplay/ItemDropSystem.h"
#include "gameplay/PlayerController.h"
#include "player/PlayerGeometry.h"

#include "TestUtils.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <limits>

namespace valcraft {

namespace {

auto player_tile_average_rgba(const std::vector<std::uint8_t>& atlas, PlayerAtlasTile tile) -> std::array<float, 4> {
    const auto coordinates = player_atlas_tile_coordinates(tile);
    const auto start_x = coordinates[0] * kPlayerAtlasTileSize;
    const auto start_y = coordinates[1] * kPlayerAtlasTileSize;

    std::array<float, 4> accum {{0.0F, 0.0F, 0.0F, 0.0F}};
    for (int y = 0; y < kPlayerAtlasTileSize; ++y) {
        for (int x = 0; x < kPlayerAtlasTileSize; ++x) {
            const auto index = static_cast<std::size_t>(((start_y + y) * kPlayerAtlasSize + (start_x + x)) * 4);
            accum[0] += static_cast<float>(atlas[index + 0]);
            accum[1] += static_cast<float>(atlas[index + 1]);
            accum[2] += static_cast<float>(atlas[index + 2]);
            accum[3] += static_cast<float>(atlas[index + 3]);
        }
    }

    const auto texel_count = static_cast<float>(kPlayerAtlasTileSize * kPlayerAtlasTileSize);
    for (auto& channel : accum) {
        channel /= texel_count;
    }
    return accum;
}

struct MeshBounds {
    glm::vec3 min {0.0F};
    glm::vec3 max {0.0F};
};

auto mesh_bounds(const CreatureMeshData& mesh) -> MeshBounds {
    MeshBounds bounds {
        glm::vec3 {std::numeric_limits<float>::max()},
        glm::vec3 {std::numeric_limits<float>::lowest()},
    };

    for (const auto& vertex : mesh.vertices) {
        bounds.min.x = std::min(bounds.min.x, vertex.x);
        bounds.min.y = std::min(bounds.min.y, vertex.y);
        bounds.min.z = std::min(bounds.min.z, vertex.z);
        bounds.max.x = std::max(bounds.max.x, vertex.x);
        bounds.max.y = std::max(bounds.max.y, vertex.y);
        bounds.max.z = std::max(bounds.max.z, vertex.z);
    }
    return bounds;
}

auto meshes_match_exactly(const CreatureMeshData& lhs, const CreatureMeshData& rhs) -> bool {
    if (lhs.part_count != rhs.part_count || lhs.indices != rhs.indices || lhs.vertices.size() != rhs.vertices.size()) {
        return false;
    }

    for (std::size_t index = 0; index < lhs.vertices.size(); ++index) {
        const auto& a = lhs.vertices[index];
        const auto& b = rhs.vertices[index];
        if (a.x != b.x || a.y != b.y || a.z != b.z ||
            a.u != b.u || a.v != b.v ||
            a.nx != b.nx || a.ny != b.ny || a.nz != b.nz ||
            a.nightmare_factor != b.nightmare_factor ||
            a.tension != b.tension ||
            a.material_class != b.material_class ||
            a.cavity_mask != b.cavity_mask ||
            a.emissive_strength != b.emissive_strength) {
            return false;
        }
    }

    return true;
}

} // namespace

TEST_CASE("player falls onto the ground and stays grounded") {
    World world(15, 1);
    test::make_chunk_empty(world, {0, 0});
    test::make_flat_floor(world, -2, 2, 0, -2, 2);

    PlayerController player({0.5F, 3.0F, 0.5F});
    const PlayerInput input {};

    for (int i = 0; i < 180; ++i) {
        player.update(input, 1.0F / 60.0F, world);
    }

    CHECK(player.state().on_ground);
    CHECK(player.position().y == doctest::Approx(1.001F).epsilon(0.02));
}

TEST_CASE("large frame deltas do not tunnel the player through the ground") {
    World world(1501, 1);
    test::make_chunk_empty(world, {0, 0});
    test::make_flat_floor(world, -2, 2, 0, -2, 2);

    PlayerController player({0.5F, 1.001F, 0.5F});

    player.update(PlayerInput {}, 0.60F, world);

    CHECK(player.state().on_ground);
    CHECK(player.position().y == doctest::Approx(1.001F).epsilon(0.02));
    CHECK(player.state().health == doctest::Approx(player.max_health()));
    CHECK(player.state().death_cause == PlayerDeathCause::None);
}

TEST_CASE("falling from a height deals survival damage to the player") {
    World world(151, 1);
    test::make_chunk_empty(world, {0, 0});
    test::make_flat_floor(world, -2, 2, 0, -2, 2);

    PlayerController player({0.5F, 8.0F, 0.5F});

    for (int i = 0; i < 240; ++i) {
        player.update(PlayerInput {}, 1.0F / 60.0F, world);
    }

    CHECK(player.state().on_ground);
    CHECK(player.state().health < player.max_health());
    CHECK_FALSE(player.state().dead);
    CHECK(player.state().death_cause == PlayerDeathCause::None);
}

TEST_CASE("underwater players lose air and eventually take drowning damage") {
    World world(152, 1);
    test::make_chunk_empty(world, {0, 0});
    test::make_flat_floor(world, -2, 2, 0, -2, 2);
    for (int y = 1; y <= 3; ++y) {
        world.set_block(0, y, 0, to_block_id(BlockType::Water));
    }

    PlayerController player({0.5F, 1.001F, 0.5F});

    for (int i = 0; i < 780; ++i) {
        player.update(PlayerInput {}, 1.0F / 60.0F, world);
    }

    CHECK(player.state().head_underwater);
    CHECK(player.state().air_seconds <= 0.1F);
    CHECK(player.state().health < player.max_health());
}

TEST_CASE("player cannot move through a solid wall") {
    World world(22, 1);
    test::make_chunk_empty(world, {0, 0});
    test::make_flat_floor(world, -2, 2, 0, -2, 2);
    for (int y = 1; y <= 3; ++y) {
        world.set_block(1, y, 0, to_block_id(BlockType::Stone));
    }

    PlayerController player({0.5F, 1.001F, 0.5F});
    PlayerInput input {};
    input.move_right = 1.0F;

    for (int i = 0; i < 60; ++i) {
        player.update(input, 1.0F / 60.0F, world);
    }

    CHECK(player.position().x < 0.71F);
    CHECK(player.state().on_ground);
}

TEST_CASE("positive and negative strafe inputs move on the expected horizontal side") {
    World world(23, 1);
    test::make_chunk_empty(world, {0, 0});
    test::make_flat_floor(world, -8, 8, 0, -8, 8);

    PlayerController right_player({0.5F, 1.001F, 0.5F});
    PlayerController left_player({0.5F, 1.001F, 0.5F});
    PlayerInput right_input {};
    PlayerInput left_input {};
    right_input.move_right = 1.0F;
    left_input.move_right = -1.0F;

    for (int i = 0; i < 10; ++i) {
        right_player.update(right_input, 1.0F / 60.0F, world);
        left_player.update(left_input, 1.0F / 60.0F, world);
    }

    CHECK(right_player.position().x > 0.55F);
    CHECK(left_player.position().x < 0.45F);
}

TEST_CASE("player jump from the ground increases vertical position") {
    World world(24, 1);
    test::make_chunk_empty(world, {0, 0});
    test::make_flat_floor(world, -2, 2, 0, -2, 2);

    PlayerController player({0.5F, 1.001F, 0.5F});
    PlayerInput input {};
    input.jump = true;

    const auto starting_y = player.position().y;
    player.update(input, 1.0F / 60.0F, world);
    player.update(PlayerInput {}, 1.0F / 60.0F, world);

    CHECK(player.position().y > starting_y);
}

TEST_CASE("player cannot place a block inside the player volume") {
    World world(33, 1);
    test::make_chunk_empty(world, {0, 0});
    world.set_block(0, 2, -1, to_block_id(BlockType::Stone));

    PlayerController player({0.5F, 1.001F, 0.5F});
    player.set_selected_block(to_block_id(BlockType::Stone));

    CHECK_FALSE(player.try_place_block(world, 4.0F));
    CHECK(world.get_block(0, 2, 0) == to_block_id(BlockType::Air));
}

TEST_CASE("player cannot place a block above the world ceiling") {
    World world(34, 1);
    test::make_chunk_empty(world, {0, -1});
    world.set_block(0, kWorldMaxY, -1, to_block_id(BlockType::Stone));

    PlayerController player({0.5F, static_cast<float>(kWorldMaxY) + 1.0F, -0.5F});
    player.set_selected_block(to_block_id(BlockType::Wood));

    PlayerInput aim_input {};
    aim_input.look_delta_y = 2000.0F;
    player.update(aim_input, 0.0F, world);

    const auto hit = player.current_target(world, 4.0F);
    REQUIRE(hit.hit);
    CHECK(hit.block == BlockCoord {0, kWorldMaxY, -1});
    CHECK(hit.adjacent == BlockCoord {0, kWorldMaxY + 1, -1});

    CHECK_FALSE(player.try_place_block(world, 4.0F));
    CHECK(world.get_block(0, kWorldMaxY, -1) == to_block_id(BlockType::Stone));
}

TEST_CASE("hotbar torch slot places a torch that emits light") {
    World world(35, 1);
    test::make_chunk_empty(world, {0, -1});
    world.set_block(0, 4, -1, to_block_id(BlockType::Stone));

    PlayerController player({0.5F, 5.001F, -0.5F});
    PlayerInput aim_input {};
    aim_input.look_delta_y = 2000.0F;
    player.update(aim_input, 0.0F, world);

    auto hotbar = make_default_hotbar_state();
    select_hotbar_index(hotbar, 6);
    player.set_selected_block(selected_hotbar_block(hotbar));

    const auto hit = player.current_target(world, 4.0F);
    REQUIRE(hit.hit);
    CHECK(hit.block == BlockCoord {0, 4, -1});
    CHECK(hit.adjacent == BlockCoord {0, 5, -1});

    REQUIRE(player.try_place_block(world, 4.0F));
    world.rebuild_lighting();

    CHECK(world.get_block(0, 5, -1) == to_block_id(BlockType::Torch));
    CHECK(world.get_block_light(0, 5, -1) == 14);
}

TEST_CASE("torches cannot be placed inside water") {
    World world(351, 1);
    test::make_chunk_empty(world, {0, -1});
    world.set_block(0, 4, -1, to_block_id(BlockType::Stone));
    world.set_block(0, 5, -1, to_block_id(BlockType::Water));

    PlayerController player({0.5F, 8.001F, -0.5F});
    PlayerInput aim_input {};
    aim_input.look_delta_y = 2000.0F;
    player.update(aim_input, 0.0F, world);
    player.set_selected_block(to_block_id(BlockType::Torch));

    const auto hit = player.current_target(world, 6.0F);
    REQUIRE(hit.hit);
    CHECK(hit.block == BlockCoord {0, 5, -1});
    CHECK(hit.block_id == to_block_id(BlockType::Water));

    CHECK_FALSE(player.try_place_block(world, 6.0F));
    CHECK(world.get_block(0, 5, -1) == to_block_id(BlockType::Water));
}

TEST_CASE("an empty hotbar slot maps to empty hands and does not place blocks") {
    World world(36, 1);
    PlayerController player({0.5F, 1.001F, 0.5F});
    auto hotbar = make_default_hotbar_state();
    select_hotbar_index(hotbar, 8);
    player.set_selected_block(selected_hotbar_block(hotbar));

    CHECK(player.selected_block() == to_block_id(BlockType::Air));
    CHECK_FALSE(player.try_place_block(world, 4.0F));
}

TEST_CASE("breaking a block reports the harvested block type and clears the world block") {
    World world(361, 1);
    test::make_chunk_empty(world, {0, -1});
    world.set_block(0, 4, -1, to_block_id(BlockType::Stone));

    PlayerController player({0.5F, 5.001F, -0.5F});
    PlayerInput aim_input {};
    aim_input.look_delta_y = 2000.0F;
    player.update(aim_input, 0.0F, world);

    const auto broken = player.try_break_block(world, 4.0F);
    REQUIRE(broken.has_value());
    CHECK(broken->block == BlockCoord {0, 4, -1});
    CHECK(broken->block_id == to_block_id(BlockType::Stone));
    CHECK(world.get_block(0, 4, -1) == to_block_id(BlockType::Air));
}

TEST_CASE("item drops are picked up into inventory and respect 64 item stacks") {
    World world(362, 1);
    test::make_chunk_empty(world, {0, 0});
    test::make_flat_floor(world, -2, 2, 0, -2, 2);

    ItemDropSystem drop_system {};
    HotbarState hotbar {};
    InventoryMenuState inventory {};
    hotbar.slots[0] = inventory_make_slot(to_block_id(BlockType::Stone), 63);

    drop_system.spawn_drop(
        inventory_make_slot(to_block_id(BlockType::Stone), 3),
        {0.5F, 1.08F, 0.5F},
        {0.0F, 0.0F, 0.0F});

    drop_system.update(0.25F, world, {0.5F, 1.001F, 0.5F}, inventory, hotbar);

    CHECK(hotbar.slots[0].count == 64);
    CHECK(hotbar.slots[1].block_id == to_block_id(BlockType::Stone));
    CHECK(hotbar.slots[1].count == 2);
    CHECK(drop_system.active_drop_count() == 0);
}

TEST_CASE("item drops stay in the world when the inventory is full") {
    World world(363, 1);
    test::make_chunk_empty(world, {0, 0});
    test::make_flat_floor(world, -2, 2, 0, -2, 2);

    ItemDropSystem drop_system {};
    HotbarState hotbar {};
    InventoryMenuState inventory {};
    for (auto& slot : hotbar.slots) {
        slot = inventory_make_slot(to_block_id(BlockType::Stone), 64);
    }
    for (auto& slot : inventory.storage_slots) {
        slot = inventory_make_slot(to_block_id(BlockType::Dirt), 64);
    }

    drop_system.spawn_drop(
        inventory_make_slot(to_block_id(BlockType::Wood), 4),
        {0.5F, 1.08F, 0.5F},
        {0.0F, 0.0F, 0.0F});

    drop_system.update(0.25F, world, {0.5F, 1.001F, 0.5F}, inventory, hotbar);

    CHECK(drop_system.active_drop_count() == 1);
    CHECK(hotbar.slots[0].block_id == to_block_id(BlockType::Stone));
    CHECK(hotbar.slots[0].count == 64);
}

TEST_CASE("placing a solid block can replace decorative flora") {
    World world(37, 1);
    test::make_chunk_empty(world, {0, -1});
    world.set_block(0, 4, -1, to_block_id(BlockType::Stone));
    world.set_block(0, 5, -1, to_block_id(BlockType::TallGrass));

    PlayerController player({0.5F, 8.001F, -0.5F});
    PlayerInput aim_input {};
    aim_input.look_delta_y = 2000.0F;
    player.update(aim_input, 0.0F, world);
    player.set_selected_block(to_block_id(BlockType::Cobblestone));

    const auto hit = player.current_target(world, 6.0F);
    REQUIRE(hit.hit);
    CHECK(hit.block == BlockCoord {0, 5, -1});
    CHECK(hit.block_id == to_block_id(BlockType::TallGrass));

    REQUIRE(player.try_place_block(world, 6.0F));
    CHECK(world.get_block(0, 5, -1) == to_block_id(BlockType::Cobblestone));
}

TEST_CASE("respawn restores survival state after death") {
    World world(153, 1);
    test::make_chunk_empty(world, {0, 0});
    test::make_flat_floor(world, -2, 2, 0, -2, 2);

    PlayerController player({0.5F, 40.0F, 0.5F});

    for (int i = 0; i < 420; ++i) {
        player.update(PlayerInput {}, 1.0F / 60.0F, world);
        if (player.state().dead) {
            break;
        }
    }

    REQUIRE(player.state().dead);
    CHECK(player.state().death_cause == PlayerDeathCause::Fall);

    player.respawn({2.5F, 1.001F, 2.5F});

    CHECK_FALSE(player.state().dead);
    CHECK(player.state().health == doctest::Approx(player.max_health()));
    CHECK(player.state().air_seconds == doctest::Approx(player.max_air_seconds()));
    CHECK(player.position().x == doctest::Approx(2.5F));
    CHECK(player.position().y == doctest::Approx(1.001F));
    CHECK(player.position().z == doctest::Approx(2.5F));
}

TEST_CASE("external zombie damage reuses invulnerability and death handling") {
    World world(154, 1);
    test::make_chunk_empty(world, {0, 0});
    test::make_flat_floor(world, -2, 2, 0, -2, 2);

    PlayerController player({0.5F, 1.001F, 0.5F});

    player.apply_external_damage(3.0F, PlayerDeathCause::Zombie);
    CHECK(player.state().health == doctest::Approx(player.max_health() - 3.0F));
    CHECK(player.state().hurt_timer > 0.0F);
    CHECK(player.state().damage_cooldown > 0.0F);
    CHECK_FALSE(player.state().dead);

    player.apply_external_damage(3.0F, PlayerDeathCause::Zombie);
    CHECK(player.state().health == doctest::Approx(player.max_health() - 3.0F));

    player.update(PlayerInput {}, 0.60F, world);
    player.apply_external_damage(40.0F, PlayerDeathCause::Zombie);

    CHECK(player.state().dead);
    CHECK(player.state().death_cause == PlayerDeathCause::Zombie);
}

TEST_CASE("day creatures never damage the player but night zombies do") {
    World world(155, 1);
    test::make_chunk_surface(world, {0, 0}, 12, to_block_id(BlockType::Grass), to_block_id(BlockType::Dirt));

    CreatureSystem day_system {};
    PlayerController day_player({2.5F, 13.001F, 2.5F});
    const auto day_environment = EnvironmentClock::compute_state(12.0F);
    const auto day_cycle = EnvironmentClock::classify_creature_cycle(12.0F);

    for (int frame = 0; frame < 180; ++frame) {
        day_system.update(1.0F / 60.0F, world, day_player.position(), day_environment, day_cycle);
        for (const auto& attack : day_system.recent_attacks()) {
            day_player.apply_external_damage(attack.damage, PlayerDeathCause::Zombie);
        }
    }

    CHECK(day_player.state().health == doctest::Approx(day_player.max_health()));

    CreatureSystem night_system {};
    PlayerController night_player({14.5F, 13.001F, 14.5F});
    const auto night_environment = EnvironmentClock::compute_state(23.0F);
    const auto night_cycle = EnvironmentClock::classify_creature_cycle(23.0F);
    night_system.update(0.0F, world, night_player.position(), night_environment, night_cycle);
    REQUIRE(night_system.active_creatures().size() == 1);

    const auto close_position = night_system.active_creatures().front().position + glm::vec3 {0.8F, 0.0F, 0.0F};
    night_player.set_position(close_position);

    bool took_damage = false;
    for (int frame = 0; frame < 120; ++frame) {
        night_system.update(1.0F / 60.0F, world, night_player.position(), night_environment, night_cycle);
        for (const auto& attack : night_system.recent_attacks()) {
            night_player.apply_external_damage(attack.damage, PlayerDeathCause::Zombie);
            took_damage = true;
        }
        night_player.update(PlayerInput {}, 1.0F / 60.0F, world);
        if (took_damage) {
            break;
        }
    }

    REQUIRE(took_damage);
    CHECK(night_player.state().health < night_player.max_health());
    CHECK_FALSE(night_player.state().dead);
}

TEST_CASE("player geometry builds a deterministic Steve-like FPS body and hurt feedback") {
    World world(156, 1);
    test::make_chunk_empty(world, {0, 0});
    test::make_flat_floor(world, -4, 4, 0, -4, 4);

    PlayerController looking_forward({0.5F, 1.001F, 0.5F});
    PlayerController looking_down({0.5F, 1.001F, 0.5F});

    PlayerInput look_up_input {};
    look_up_input.look_delta_y = -400.0F;
    looking_forward.update(look_up_input, 0.0F, world);

    PlayerInput look_down_input {};
    look_down_input.look_delta_y = 400.0F;
    looking_down.update(look_down_input, 0.0F, world);

    looking_down.set_velocity({1.2F, 0.0F, 0.0F});
    looking_down.apply_external_damage(2.0F, PlayerDeathCause::Zombie);

    const auto atlas = build_player_atlas_pixels();
    const auto forward_mesh = build_player_mesh(looking_forward);
    const auto down_mesh = build_player_mesh(looking_down);
    const auto down_mesh_repeat = build_player_mesh(looking_down);
    const auto forward_bounds = mesh_bounds(forward_mesh);
    const auto down_bounds = mesh_bounds(down_mesh);

    REQUIRE(atlas.size() == static_cast<std::size_t>(kPlayerAtlasSize * kPlayerAtlasSize * 4));
    CHECK(player_tile_average_rgba(atlas, PlayerAtlasTile::Shirt)[2] > player_tile_average_rgba(atlas, PlayerAtlasTile::Pants)[2]);
    CHECK(player_tile_average_rgba(atlas, PlayerAtlasTile::Hair)[0] < player_tile_average_rgba(atlas, PlayerAtlasTile::Skin)[0]);
    CHECK(player_tile_average_rgba(atlas, PlayerAtlasTile::Hurt)[0] > player_tile_average_rgba(atlas, PlayerAtlasTile::Shirt)[0] + 80.0F);

    CHECK_FALSE(forward_mesh.empty());
    CHECK_FALSE(down_mesh.empty());
    CHECK(meshes_match_exactly(down_mesh, down_mesh_repeat));
    CHECK(down_mesh.part_count > forward_mesh.part_count);
    CHECK((down_bounds.max.y - down_bounds.min.y) > (forward_bounds.max.y - forward_bounds.min.y) + 0.10F);
    CHECK(down_bounds.min.z < looking_down.position().z - 0.20F);
}

} // namespace valcraft
