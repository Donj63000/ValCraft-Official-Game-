#include "app/InventoryMenu.h"
#include "app/Hotbar.h"
#include "creatures/CreatureSystem.h"
#include "gameplay/ItemDropSystem.h"
#include "gameplay/PlayerController.h"
#include "player/PlayerGeometry.h"

#include "TestUtils.h"

#include <doctest/doctest.h>
#include <glm/geometric.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>

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

auto vertex_in_camera_space(const CreatureVertex& vertex, const PlayerController& player) -> glm::vec3 {
    auto camera_forward = player.look_direction();
    if (glm::dot(camera_forward, camera_forward) <= 1.0e-6F) {
        camera_forward = glm::vec3 {0.0F, 0.0F, -1.0F};
    } else {
        camera_forward = glm::normalize(camera_forward);
    }

    auto camera_right = glm::cross(camera_forward, glm::vec3 {0.0F, 1.0F, 0.0F});
    if (glm::dot(camera_right, camera_right) <= 1.0e-6F) {
        camera_right = glm::vec3 {1.0F, 0.0F, 0.0F};
    } else {
        camera_right = glm::normalize(camera_right);
    }
    const auto camera_up = glm::normalize(glm::cross(camera_right, camera_forward));

    const auto relative = glm::vec3 {vertex.x, vertex.y, vertex.z} - player.eye_position();
    return {
        glm::dot(relative, camera_right),
        glm::dot(relative, camera_up),
        glm::dot(relative, camera_forward),
    };
}

auto meshes_match_in_camera_space(const CreatureMeshData& lhs,
                                  const PlayerController& lhs_player,
                                  const CreatureMeshData& rhs,
                                  const PlayerController& rhs_player,
                                  float epsilon = 1.0e-4F) -> bool {
    if (lhs.part_count != rhs.part_count || lhs.indices != rhs.indices || lhs.vertices.size() != rhs.vertices.size()) {
        return false;
    }

    for (std::size_t index = 0; index < lhs.vertices.size(); ++index) {
        const auto a = vertex_in_camera_space(lhs.vertices[index], lhs_player);
        const auto b = vertex_in_camera_space(rhs.vertices[index], rhs_player);
        if (std::abs(a.x - b.x) > epsilon ||
            std::abs(a.y - b.y) > epsilon ||
            std::abs(a.z - b.z) > epsilon) {
            return false;
        }
    }

    return true;
}

auto angle_distance_degrees(float lhs, float rhs) -> float {
    return static_cast<float>(std::abs(std::remainder(lhs - rhs, 360.0F)));
}

auto item_socket_in_camera_space(const PlayerViewModelMesh& viewmodel, const PlayerController& player) -> glm::vec3 {
    const auto socket_world = glm::vec3 {
        viewmodel.pose.item_socket_transform[3].x,
        viewmodel.pose.item_socket_transform[3].y,
        viewmodel.pose.item_socket_transform[3].z,
    };
    auto camera_forward = player.look_direction();
    if (glm::dot(camera_forward, camera_forward) <= 1.0e-6F) {
        camera_forward = glm::vec3 {0.0F, 0.0F, -1.0F};
    } else {
        camera_forward = glm::normalize(camera_forward);
    }

    auto camera_right = glm::cross(camera_forward, glm::vec3 {0.0F, 1.0F, 0.0F});
    if (glm::dot(camera_right, camera_right) <= 1.0e-6F) {
        camera_right = glm::vec3 {1.0F, 0.0F, 0.0F};
    } else {
        camera_right = glm::normalize(camera_right);
    }
    const auto camera_up = glm::normalize(glm::cross(camera_right, camera_forward));

    const auto relative = socket_world - player.eye_position();
    return {
        glm::dot(relative, camera_right),
        glm::dot(relative, camera_up),
        glm::dot(relative, camera_forward),
    };
}

void settle_viewmodel(PlayerController& player, const World& world, int frames = 18) {
    for (int i = 0; i < frames; ++i) {
        player.update(PlayerInput {}, 1.0F / 60.0F, world);
    }
}

auto find_unloaded_generated_face_block(const World& world, int world_x) -> std::optional<BlockCoord> {
    for (int world_z = 0; world_z < kChunkSizeZ; ++world_z) {
        for (int world_y = 2; world_y < kWorldMaxY; ++world_y) {
            if (!is_block_collidable(world.peek_block_or_generated(world_x, world_y, world_z))) {
                continue;
            }
            return BlockCoord {world_x, world_y, world_z};
        }
    }

    return std::nullopt;
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
    for (int y = 1; y <= 6; ++y) {
        world.set_block(0, y, 0, to_block_id(BlockType::Water));
    }

    PlayerController player({0.5F, 1.001F, 0.5F});

    for (int i = 0; i < 900; ++i) {
        player.update(PlayerInput {}, 1.0F / 60.0F, world);
    }

    CHECK(player.state().head_underwater);
    CHECK(player.state().air_seconds <= 0.1F);
    CHECK(player.state().health < player.max_health());
}

TEST_CASE("deep water enables swimming and upward movement") {
    World world(1521, 1);
    test::make_chunk_empty(world, {0, 0});
    test::make_flat_floor(world, -2, 2, 0, -2, 2);
    for (int y = 1; y <= 4; ++y) {
        world.set_block(0, y, 0, to_block_id(BlockType::Water));
    }

    PlayerController player({0.5F, 1.001F, 0.5F});
    PlayerInput input {};
    input.move_up = 1.0F;

    const auto starting_y = player.position().y;
    for (int i = 0; i < 30; ++i) {
        player.update(input, 1.0F / 60.0F, world);
    }

    CHECK(player.state().swimming);
    CHECK(player.position().y > starting_y + 0.20F);
    CHECK(player.state().velocity.y > 0.0F);
}

TEST_CASE("partial overlap with deep water still counts as swimming") {
    World world(1522, 1);
    test::make_chunk_empty(world, {0, 0});
    test::make_flat_floor(world, -2, 3, 0, -2, 2);
    for (int y = 1; y <= 4; ++y) {
        world.set_block(1, y, 0, to_block_id(BlockType::Water));
    }

    PlayerController player({0.79F, 1.001F, 0.5F});
    player.update(PlayerInput {}, 1.0F / 60.0F, world);

    CHECK(player.state().swimming);
    CHECK(player.state().head_underwater);
}

TEST_CASE("shallow water slows movement without entering swimming state") {
    World world(1523, 1);
    test::make_chunk_empty(world, {0, 0});
    test::make_flat_floor(world, -2, 12, 0, -10, 2);
    for (int z = -10; z <= 2; ++z) {
        for (int x = -1; x <= 1; ++x) {
            world.set_block(x, 1, z, to_block_id(BlockType::Water));
        }
    }

    PlayerController shallow_player({0.5F, 1.001F, 0.5F});
    PlayerController dry_player({3.5F, 1.001F, 0.5F});
    PlayerInput input {};
    input.move_forward = 1.0F;

    for (int i = 0; i < 60; ++i) {
        shallow_player.update(input, 1.0F / 60.0F, world);
        dry_player.update(input, 1.0F / 60.0F, world);
    }

    const auto shallow_distance = std::abs(shallow_player.position().x - 0.5F);
    const auto dry_distance = std::abs(dry_player.position().x - 3.5F);

    CHECK_FALSE(shallow_player.state().swimming);
    CHECK_FALSE(shallow_player.state().head_underwater);
    CHECK(shallow_distance <= dry_distance);
}

TEST_CASE("falling into deep water prevents fall damage") {
    World world(1524, 1);
    test::make_chunk_empty(world, {0, 0});
    test::make_flat_floor(world, -2, 2, 0, -2, 2);
    for (int y = 1; y <= 5; ++y) {
        world.set_block(0, y, 0, to_block_id(BlockType::Water));
    }

    PlayerController player({0.5F, 12.0F, 0.5F});
    for (int i = 0; i < 240; ++i) {
        player.update(PlayerInput {}, 1.0F / 60.0F, world);
    }

    CHECK(player.state().swimming);
    CHECK(player.state().health == doctest::Approx(player.max_health()));
    CHECK_FALSE(player.state().dead);
    CHECK(player.state().death_cause == PlayerDeathCause::None);
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

TEST_CASE("player collision still sees generated solid blocks in unloaded chunks") {
    World world(2201, 1);
    test::make_chunk_empty(world, {0, 0});

    REQUIRE(world.find_chunk({1, 0}) == nullptr);
    const auto generated_block = find_unloaded_generated_face_block(world, kChunkSizeX);
    REQUIRE(generated_block.has_value());

    PlayerController player({
        static_cast<float>(generated_block->x) + 0.5F,
        static_cast<float>(generated_block->y),
        static_cast<float>(generated_block->z) + 0.5F,
    });

    CHECK(player.collides_at(world, player.position()));
}

TEST_CASE("player cannot walk into generated terrain before the neighbor chunk finishes streaming") {
    World world(2202, 1);
    test::make_chunk_empty(world, {0, 0});

    REQUIRE(world.find_chunk({1, 0}) == nullptr);
    const auto generated_block = find_unloaded_generated_face_block(world, kChunkSizeX);
    REQUIRE(generated_block.has_value());
    REQUIRE(generated_block->y >= 2);

    const auto floor_y = generated_block->y - 1;
    const auto floor_min_z = std::max(0, generated_block->z - 1);
    const auto floor_max_z = std::min(kChunkSizeZ - 1, generated_block->z + 1);
    test::make_flat_floor(world, kChunkSizeX - 4, kChunkSizeX - 1, floor_y, floor_min_z, floor_max_z);

    PlayerController player({
        static_cast<float>(kChunkSizeX) - 0.55F,
        static_cast<float>(floor_y + 1) + 0.001F,
        static_cast<float>(generated_block->z) + 0.5F,
    });
    PlayerInput input {};
    input.move_right = 1.0F;

    for (int frame = 0; frame < 20; ++frame) {
        player.update(input, 1.0F / 60.0F, world);
    }

    CHECK(player.position().x < static_cast<float>(generated_block->x) - 0.29F);
    CHECK_FALSE(player.collides_at(world, player.position()));
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

TEST_CASE("sprint boosts only intentional forward ground movement") {
    World world(231, 1);
    test::make_chunk_empty(world, {0, 0});
    test::make_chunk_empty(world, {0, -1});
    test::make_flat_floor(world, -4, 4, 0, -10, 10);

    PlayerController walk_player({0.5F, 1.001F, 0.5F});
    PlayerController sprint_player({0.5F, 1.001F, 0.5F});
    PlayerController reverse_player({0.5F, 1.001F, 0.5F});
    PlayerController reverse_sprint_player({0.5F, 1.001F, 0.5F});

    PlayerInput walk_input {};
    walk_input.move_forward = 1.0F;
    PlayerInput sprint_input = walk_input;
    sprint_input.sprint = true;
    PlayerInput reverse_input {};
    reverse_input.move_forward = -1.0F;
    PlayerInput reverse_sprint_input = reverse_input;
    reverse_sprint_input.sprint = true;

    for (int frame = 0; frame < 30; ++frame) {
        walk_player.update(walk_input, 1.0F / 60.0F, world);
        sprint_player.update(sprint_input, 1.0F / 60.0F, world);
        reverse_player.update(reverse_input, 1.0F / 60.0F, world);
        reverse_sprint_player.update(reverse_sprint_input, 1.0F / 60.0F, world);
    }

    const auto walk_distance = std::abs(walk_player.position().z - 0.5F);
    const auto sprint_distance = std::abs(sprint_player.position().z - 0.5F);
    const auto reverse_distance = std::abs(reverse_player.position().z - 0.5F);
    const auto reverse_sprint_distance = std::abs(reverse_sprint_player.position().z - 0.5F);

    CHECK(sprint_distance > walk_distance + 0.5F);
    CHECK(reverse_sprint_distance == doctest::Approx(reverse_distance).epsilon(0.01));
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

TEST_CASE("player can still jump briefly after leaving a ledge") {
    World world(241, 1);
    test::make_chunk_empty(world, {0, 0});
    test::make_flat_floor(world, -2, 2, 0, -2, 2);

    PlayerController player({0.5F, 1.001F, 0.5F});
    player.update(PlayerInput {}, 1.0F / 60.0F, world);
    REQUIRE(player.state().on_ground);

    world.set_block(0, 0, 0, to_block_id(BlockType::Air));
    PlayerInput jump_input {};
    jump_input.jump = true;
    player.update(jump_input, 1.0F / 60.0F, world);

    CHECK_FALSE(player.state().on_ground);
    CHECK(player.state().velocity.y > 0.0F);
    CHECK(player.position().y > 1.001F);
}

TEST_CASE("jump input just before landing is buffered") {
    World world(242, 1);
    test::make_chunk_empty(world, {0, 0});
    test::make_flat_floor(world, -2, 2, 0, -2, 2);

    PlayerController player({0.5F, 1.35F, 0.5F});
    player.set_velocity({0.0F, -7.0F, 0.0F});

    PlayerInput early_jump {};
    early_jump.jump = true;
    player.update(early_jump, 1.0F / 60.0F, world);
    REQUIRE_FALSE(player.state().on_ground);
    REQUIRE(player.state().velocity.y < 0.0F);

    bool buffered_jump_triggered = false;
    for (int frame = 0; frame < 8; ++frame) {
        player.update(PlayerInput {}, 1.0F / 60.0F, world);
        if (!player.state().on_ground && player.state().velocity.y > 0.0F) {
            buffered_jump_triggered = true;
            break;
        }
    }

    CHECK(buffered_jump_triggered);
    CHECK(player.position().y >= 1.001F);
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

TEST_CASE("torches can be placed on walls like minecraft") {
    World world(352, 1);
    test::make_chunk_empty(world, {0, 0});
    world.set_block(1, 5, 0, to_block_id(BlockType::Stone));

    PlayerController player({3.5F, 3.88F, 0.5F});
    auto state = player.state();
    state.position = {3.5F, 3.88F, 0.5F};
    state.yaw_degrees = 180.0F;
    state.pitch_degrees = 0.0F;
    state.body_yaw_degrees = 180.0F;
    player.load_state(state);
    player.set_selected_block(to_block_id(BlockType::Torch));

    const auto hit = player.current_target(world, 6.0F);
    REQUIRE(hit.hit);
    CHECK(hit.block == BlockCoord {1, 5, 0});
    CHECK(hit.adjacent == BlockCoord {2, 5, 0});

    const auto placed = player.try_place_block(world, 6.0F);
    REQUIRE(placed.has_value());
    world.rebuild_lighting();

    CHECK(placed->block == BlockCoord {2, 5, 0});
    CHECK(placed->block_id == to_block_id(BlockType::TorchWallNegativeX));
    CHECK(world.get_block(2, 5, 0) == to_block_id(BlockType::TorchWallNegativeX));
    CHECK(world.get_block_light(2, 5, 0) == 14);
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
    CHECK(world.get_block(0, 5, -1) == to_block_id(BlockType::Air));
    CHECK(world.has_water(0, 5, -1));
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

TEST_CASE("the deepest world layer cannot be broken instantly") {
    World world(36101, 1);
    test::make_chunk_empty(world, {0, -1});
    world.set_block(0, kWorldMinY, -1, to_block_id(BlockType::Stone));

    PlayerController player({0.5F, 1.001F, -0.5F});
    PlayerInput aim_input {};
    aim_input.look_delta_y = 2000.0F;
    player.update(aim_input, 0.0F, world);

    const auto hit = player.current_target(world, 4.0F);
    REQUIRE(hit.hit);
    CHECK(hit.block == BlockCoord {0, kWorldMinY, -1});
    CHECK(hit.block_id == to_block_id(BlockType::Stone));

    CHECK_FALSE(player.try_break_block(world, 4.0F).has_value());
    CHECK(world.get_block(0, kWorldMinY, -1) == to_block_id(BlockType::Stone));
}

TEST_CASE("the deepest world layer does not start held breaking progress") {
    World world(36102, 1);
    test::make_chunk_empty(world, {0, -1});
    world.set_block(0, kWorldMinY, -1, to_block_id(BlockType::Stone));

    PlayerController player({0.5F, 1.001F, -0.5F});
    PlayerInput aim_input {};
    aim_input.look_delta_y = 2000.0F;
    player.update(aim_input, 0.0F, world);

    CHECK_FALSE(player.update_block_breaking(world, 1.0F, true, 4.0F).has_value());
    CHECK_FALSE(player.block_break_progress().active);
    CHECK(world.get_block(0, kWorldMinY, -1) == to_block_id(BlockType::Stone));
}

TEST_CASE("held block breaking waits for the configured duration before removing the target") {
    World world(3611, 1);
    test::make_chunk_empty(world, {0, -1});
    world.set_block(0, 4, -1, to_block_id(BlockType::Stone));

    PlayerController player({0.5F, 5.001F, -0.5F});
    PlayerInput aim_input {};
    aim_input.look_delta_y = 2000.0F;
    player.update(aim_input, 0.0F, world);

    const auto stone_duration = block_break_duration_seconds(to_block_id(BlockType::Stone));
    REQUIRE(stone_duration > 1.0F);

    const auto first_attempt = player.update_block_breaking(world, stone_duration - 0.05F, true, 4.0F);
    CHECK_FALSE(first_attempt.has_value());
    CHECK(world.get_block(0, 4, -1) == to_block_id(BlockType::Stone));
    CHECK(player.block_break_progress().active);
    CHECK(player.block_break_progress().progress < 1.0F);

    const auto completed_break = player.update_block_breaking(world, 0.05F, true, 4.0F);
    REQUIRE(completed_break.has_value());
    CHECK(completed_break->block == BlockCoord {0, 4, -1});
    CHECK(completed_break->block_id == to_block_id(BlockType::Stone));
    CHECK(world.get_block(0, 4, -1) == to_block_id(BlockType::Air));
    CHECK_FALSE(player.block_break_progress().active);
}

TEST_CASE("releasing the break input cancels the current breaking progress") {
    World world(3612, 1);
    test::make_chunk_empty(world, {0, -1});
    world.set_block(0, 4, -1, to_block_id(BlockType::Dirt));

    PlayerController player({0.5F, 5.001F, -0.5F});
    PlayerInput aim_input {};
    aim_input.look_delta_y = 2000.0F;
    player.update(aim_input, 0.0F, world);

    const auto dirt_duration = block_break_duration_seconds(to_block_id(BlockType::Dirt));
    REQUIRE(dirt_duration > 0.2F);

    CHECK_FALSE(player.update_block_breaking(world, dirt_duration * 0.45F, true, 4.0F).has_value());
    REQUIRE(player.block_break_progress().active);
    CHECK(player.block_break_progress().progress > 0.0F);

    CHECK_FALSE(player.update_block_breaking(world, 0.0F, false, 4.0F).has_value());
    CHECK_FALSE(player.block_break_progress().active);
    CHECK(world.get_block(0, 4, -1) == to_block_id(BlockType::Dirt));

    CHECK_FALSE(player.update_block_breaking(world, 0.10F, true, 4.0F).has_value());
    REQUIRE(player.block_break_progress().active);
    CHECK(player.block_break_progress().elapsed_seconds == doctest::Approx(0.10F));
}

TEST_CASE("changing the targeted block resets the breaking progress to the new material duration") {
    World world(3613, 1);
    test::make_chunk_empty(world, {0, -1});
    world.set_block(0, 4, -1, to_block_id(BlockType::Stone));

    PlayerController player({0.5F, 5.001F, -0.5F});
    PlayerInput aim_input {};
    aim_input.look_delta_y = 2000.0F;
    player.update(aim_input, 0.0F, world);

    CHECK_FALSE(player.update_block_breaking(world, 0.45F, true, 4.0F).has_value());
    REQUIRE(player.block_break_progress().active);
    CHECK(player.block_break_progress().block_id == to_block_id(BlockType::Stone));
    CHECK(player.block_break_progress().elapsed_seconds == doctest::Approx(0.45F));

    world.set_block(0, 4, -1, to_block_id(BlockType::Dirt));

    CHECK_FALSE(player.update_block_breaking(world, 0.10F, true, 4.0F).has_value());
    REQUIRE(player.block_break_progress().active);
    CHECK(player.block_break_progress().block_id == to_block_id(BlockType::Dirt));
    CHECK(player.block_break_progress().duration_seconds ==
          doctest::Approx(block_break_duration_seconds(to_block_id(BlockType::Dirt))));
    CHECK(player.block_break_progress().elapsed_seconds == doctest::Approx(0.10F));
    CHECK(player.block_break_progress().elapsed_seconds < 0.45F);
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

TEST_CASE("item drops collide with generated terrain before the chunk is loaded") {
    World world(364, 1);
    const int world_x = 48;
    const int world_z = 48;
    const auto chunk_coord = world.world_to_chunk(world_x, world_z);
    REQUIRE(world.find_chunk(chunk_coord) == nullptr);

    int surface_y = kWorldMinY - 1;
    for (int y = kWorldMaxY; y >= kWorldMinY; --y) {
        if (is_block_collidable(world.peek_block_or_generated(world_x, y, world_z))) {
            surface_y = y;
            break;
        }
    }

    REQUIRE(surface_y >= kWorldMinY);
    REQUIRE(world.find_chunk(chunk_coord) == nullptr);

    ItemDropSystem drop_system {};
    HotbarState hotbar {};
    InventoryMenuState inventory {};
    drop_system.spawn_drop(
        inventory_make_slot(to_block_id(BlockType::Wood), 1),
        {static_cast<float>(world_x) + 0.5F, static_cast<float>(surface_y) + 3.0F, static_cast<float>(world_z) + 0.5F},
        {0.0F, 0.0F, 0.0F});

    constexpr float kStep = 1.0F / 60.0F;
    for (int frame = 0; frame < 240; ++frame) {
        drop_system.update(kStep, world, {1000.0F, 80.0F, 1000.0F}, inventory, hotbar);
    }

    REQUIRE(drop_system.active_drop_count() == 1);
    const auto& drop = drop_system.drops().front();
    CHECK(drop.position.y > static_cast<float>(surface_y) + 0.75F);
    CHECK(drop.position.y < static_cast<float>(surface_y) + 3.0F);
    CHECK(world.find_chunk(chunk_coord) == nullptr);
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

TEST_CASE("equipped resistance reduces external survival damage") {
    World world(1541, 1);
    test::make_chunk_empty(world, {0, 0});
    test::make_flat_floor(world, -2, 2, 0, -2, 2);

    PlayerController player({0.5F, 1.001F, 0.5F});

    player.set_damage_resistance_percent(25.0F);
    CHECK(player.damage_resistance_percent() == doctest::Approx(25.0F));
    player.apply_external_damage(8.0F, PlayerDeathCause::Zombie);
    CHECK(player.state().health == doctest::Approx(player.max_health() - 6.0F));

    player.update(PlayerInput {}, 0.60F, world);
    player.set_damage_resistance_percent(120.0F);
    CHECK(player.damage_resistance_percent() == doctest::Approx(85.0F));
    player.apply_external_damage(10.0F, PlayerDeathCause::Zombie);
    CHECK(player.state().health == doctest::Approx(player.max_health() - 7.5F));
    CHECK_FALSE(player.state().dead);
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

TEST_CASE("player body yaw follows real movement and eases back toward the camera") {
    World world(156, 1);
    test::make_chunk_empty(world, {0, 0});
    test::make_flat_floor(world, -8, 8, 0, -8, 8);

    SUBCASE("forward movement stays aligned with the current camera heading") {
        PlayerController player({0.5F, 1.001F, 0.5F});
        PlayerInput input {};
        input.move_forward = 1.0F;

        for (int frame = 0; frame < 10; ++frame) {
            player.update(input, 1.0F / 60.0F, world);
        }

        CHECK(angle_distance_degrees(player.state().body_yaw_degrees, -90.0F) < 0.1F);
    }

    SUBCASE("backward movement rotates toward the real retreat direction") {
        PlayerController player({0.5F, 1.001F, 0.5F});
        PlayerInput input {};
        input.move_forward = -1.0F;

        for (int frame = 0; frame < 20; ++frame) {
            player.update(input, 1.0F / 60.0F, world);
        }

        CHECK(angle_distance_degrees(player.state().body_yaw_degrees, 90.0F) < 0.1F);
    }

    SUBCASE("strafe rotation is smoothed and idle frames bring the body back to the camera") {
        PlayerController player({0.5F, 1.001F, 0.5F});
        PlayerInput strafe_input {};
        strafe_input.move_right = 1.0F;

        player.update(strafe_input, 1.0F / 60.0F, world);
        CHECK(player.state().body_yaw_degrees == doctest::Approx(-81.0F));

        for (int frame = 0; frame < 19; ++frame) {
            player.update(strafe_input, 1.0F / 60.0F, world);
        }
        CHECK(angle_distance_degrees(player.state().body_yaw_degrees, 0.0F) < 0.1F);

        PlayerInput turn_camera_input {};
        turn_camera_input.look_delta_x = 2250.0F;
        player.update(turn_camera_input, 1.0F / 60.0F, world);
        CHECK(player.state().body_yaw_degrees == doctest::Approx(6.0F));

        for (int frame = 0; frame < 19; ++frame) {
            player.update(PlayerInput {}, 1.0F / 60.0F, world);
        }
        CHECK(angle_distance_degrees(player.state().body_yaw_degrees, 90.0F) < 0.1F);
    }
}

TEST_CASE("player first person viewmodel stays camera locked while the world avatar stays separate") {
    World world(156, 1);
    test::make_chunk_empty(world, {0, 0});
    test::make_flat_floor(world, -4, 4, 0, -4, 4);

    PlayerController looking_forward({0.5F, 1.001F, 0.5F});
    PlayerController looking_right({0.5F, 1.001F, 0.5F});
    PlayerController looking_up({0.5F, 1.001F, 0.5F});
    PlayerController looking_slightly_down({0.5F, 1.001F, 0.5F});
    PlayerController looking_down({0.5F, 1.001F, 0.5F});
    PlayerController looking_down_clean({0.5F, 1.001F, 0.5F});

    settle_viewmodel(looking_forward, world);

    PlayerInput look_right_input {};
    look_right_input.look_delta_x = 1125.0F;
    looking_right.update(look_right_input, 0.0F, world);
    settle_viewmodel(looking_right, world);

    PlayerInput look_up_input {};
    look_up_input.look_delta_y = -600.0F;
    looking_up.update(look_up_input, 0.0F, world);
    settle_viewmodel(looking_up, world);

    PlayerInput look_slightly_down_input {};
    look_slightly_down_input.look_delta_y = 88.0F;
    looking_slightly_down.update(look_slightly_down_input, 0.0F, world);
    settle_viewmodel(looking_slightly_down, world);

    PlayerInput look_down_input {};
    look_down_input.look_delta_y = 800.0F;
    looking_down.update(look_down_input, 0.0F, world);
    looking_down_clean.update(look_down_input, 0.0F, world);
    settle_viewmodel(looking_down, world);
    settle_viewmodel(looking_down_clean, world);

    looking_down.set_velocity({1.2F, 0.0F, 0.0F});
    looking_down_clean.set_velocity({1.2F, 0.0F, 0.0F});
    looking_down.apply_external_damage(2.0F, PlayerDeathCause::Zombie);

    const auto atlas = build_player_atlas_pixels();
    const auto forward_viewmodel = build_player_viewmodel_mesh(looking_forward);
    const auto right_viewmodel = build_player_viewmodel_mesh(looking_right);
    const auto up_viewmodel = build_player_viewmodel_mesh(looking_up);
    const auto slightly_down_viewmodel = build_player_viewmodel_mesh(looking_slightly_down);
    const auto down_viewmodel = build_player_viewmodel_mesh(looking_down);
    const auto clean_down_viewmodel = build_player_viewmodel_mesh(looking_down_clean);
    const auto down_viewmodel_repeat = build_player_viewmodel_mesh(looking_down);
    const auto world_avatar_mesh = build_player_world_avatar_mesh(looking_down);
    const auto down_bounds = mesh_bounds(down_viewmodel.mesh);
    const auto world_avatar_bounds = mesh_bounds(world_avatar_mesh);
    const auto forward_socket = item_socket_in_camera_space(forward_viewmodel, looking_forward);
    const auto right_socket = item_socket_in_camera_space(right_viewmodel, looking_right);
    const auto up_socket = item_socket_in_camera_space(up_viewmodel, looking_up);

    REQUIRE(atlas.size() == static_cast<std::size_t>(kPlayerAtlasSize * kPlayerAtlasSize * 4));
    CHECK(player_tile_average_rgba(atlas, PlayerAtlasTile::Shirt)[2] > player_tile_average_rgba(atlas, PlayerAtlasTile::Pants)[2]);
    CHECK(player_tile_average_rgba(atlas, PlayerAtlasTile::Hair)[0] < player_tile_average_rgba(atlas, PlayerAtlasTile::Skin)[0]);
    CHECK(player_tile_average_rgba(atlas, PlayerAtlasTile::Hurt)[0] > player_tile_average_rgba(atlas, PlayerAtlasTile::Shirt)[0] + 80.0F);
    CHECK(player_tile_average_rgba(atlas, PlayerAtlasTile::Face)[0] > player_tile_average_rgba(atlas, PlayerAtlasTile::HairShadow)[0]);
    CHECK(player_tile_average_rgba(atlas, PlayerAtlasTile::SwordEdge)[0] > player_tile_average_rgba(atlas, PlayerAtlasTile::SwordGrip)[0] + 80.0F);

    CHECK_FALSE(forward_viewmodel.empty());
    CHECK_FALSE(right_viewmodel.empty());
    CHECK_FALSE(up_viewmodel.empty());
    CHECK_FALSE(slightly_down_viewmodel.empty());
    CHECK_FALSE(down_viewmodel.empty());
    CHECK_FALSE(clean_down_viewmodel.empty());
    CHECK_FALSE(world_avatar_mesh.empty());
    CHECK(meshes_match_in_camera_space(forward_viewmodel.mesh, looking_forward, right_viewmodel.mesh, looking_right));
    CHECK(meshes_match_in_camera_space(forward_viewmodel.mesh, looking_forward, up_viewmodel.mesh, looking_up));
    CHECK(meshes_match_exactly(down_viewmodel.mesh, down_viewmodel_repeat.mesh));
    CHECK_FALSE(meshes_match_exactly(down_viewmodel.mesh, clean_down_viewmodel.mesh));
    CHECK(forward_viewmodel.mesh.part_count >= 4);
    CHECK(right_viewmodel.mesh.part_count == forward_viewmodel.mesh.part_count);
    CHECK(up_viewmodel.mesh.part_count == forward_viewmodel.mesh.part_count);
    CHECK(slightly_down_viewmodel.mesh.part_count == forward_viewmodel.mesh.part_count);
    CHECK(down_viewmodel.mesh.part_count == forward_viewmodel.mesh.part_count);
    CHECK(world_avatar_mesh.part_count > down_viewmodel.mesh.part_count);
    CHECK(down_bounds.min.y > looking_down.position().y + 0.35F);
    CHECK(world_avatar_bounds.min.y < looking_down.position().y + 0.10F);
    CHECK(forward_socket.x == doctest::Approx(right_socket.x).epsilon(1.0e-3F));
    CHECK(forward_socket.y == doctest::Approx(right_socket.y).epsilon(1.0e-3F));
    CHECK(forward_socket.z == doctest::Approx(right_socket.z).epsilon(1.0e-3F));
    CHECK(forward_socket.x == doctest::Approx(up_socket.x).epsilon(1.0e-3F));
    CHECK(forward_socket.y == doctest::Approx(up_socket.y).epsilon(1.0e-3F));
    CHECK(forward_socket.z == doctest::Approx(up_socket.z).epsilon(1.0e-3F));
    CHECK(forward_socket.x > 0.10F);
    CHECK(forward_socket.y < -0.05F);
    CHECK(forward_socket.z > 0.20F);
}

TEST_CASE("player first person viewmodel adds a sword model when the held item is a sword") {
    World world(6021, 1);
    test::make_chunk_empty(world, {0, 0});
    test::make_flat_floor(world, -2, 2, 0, -2, 2);

    PlayerController player({0.5F, 1.001F, 0.5F});
    settle_viewmodel(player, world);

    const auto empty_parts = build_player_viewmodel_parts(player);
    const auto stone_parts = build_player_viewmodel_parts(player, to_block_id(BlockType::Stone));
    const auto sword_parts = build_player_viewmodel_parts(player, to_block_id(BlockType::Sword));
    const auto empty_mesh = build_player_viewmodel_mesh(player);
    const auto sword_mesh = build_player_viewmodel_mesh(player, to_block_id(BlockType::Sword));

    REQUIRE_FALSE(empty_parts.empty());
    REQUIRE_FALSE(sword_parts.empty());
    CHECK(stone_parts.parts.size() == empty_parts.parts.size());
    CHECK(sword_parts.parts.size() == empty_parts.parts.size() + 14U);
    CHECK(sword_mesh.mesh.part_count == sword_parts.parts.size());
    CHECK(sword_mesh.mesh.vertices.size() == sword_parts.parts.size() * 24U);
    CHECK(sword_mesh.mesh.indices.size() == sword_parts.parts.size() * 36U);
    CHECK_FALSE(meshes_match_exactly(empty_mesh.mesh, sword_mesh.mesh));

    const auto socket_position = glm::vec3 {
        sword_parts.pose.item_socket_transform[3].x,
        sword_parts.pose.item_socket_transform[3].y,
        sword_parts.pose.item_socket_transform[3].z,
    };
    const auto blade_position = glm::vec3 {
        sword_parts.parts[empty_parts.parts.size()].transform[3].x,
        sword_parts.parts[empty_parts.parts.size()].transform[3].y,
        sword_parts.parts[empty_parts.parts.size()].transform[3].z,
    };
    CHECK(glm::distance(socket_position, blade_position) > 0.20F);
    CHECK(glm::distance(socket_position, blade_position) < 0.70F);
}

TEST_CASE("player avatar action triggers deterministically change the first person pose") {
    PlayerController idle_player({0.5F, 1.001F, 0.5F});
    PlayerController mining_player({0.5F, 1.001F, 0.5F});
    PlayerController placing_player({0.5F, 1.001F, 0.5F});

    mining_player.trigger_primary_action();
    placing_player.trigger_secondary_action();

    const auto idle_viewmodel = build_player_viewmodel_mesh(idle_player);
    const auto mining_viewmodel = build_player_viewmodel_mesh(mining_player);
    const auto placing_viewmodel = build_player_viewmodel_mesh(placing_player);

    CHECK_FALSE(idle_viewmodel.empty());
    CHECK_FALSE(mining_viewmodel.empty());
    CHECK_FALSE(placing_viewmodel.empty());
    CHECK_FALSE(meshes_match_exactly(idle_viewmodel.mesh, mining_viewmodel.mesh));
    CHECK_FALSE(meshes_match_exactly(idle_viewmodel.mesh, placing_viewmodel.mesh));
    CHECK_FALSE(meshes_match_exactly(mining_viewmodel.mesh, placing_viewmodel.mesh));
    CHECK(mining_viewmodel.pose.action_swing > idle_viewmodel.pose.action_swing);
    CHECK(placing_viewmodel.pose.action_swing > idle_viewmodel.pose.action_swing);
}

TEST_CASE("player parts tessellate identically to the public mesh builders") {
    World world(6011, 1);
    test::make_chunk_empty(world, {0, 0});
    test::make_flat_floor(world, -2, 2, 0, -2, 2);

    PlayerController player({0.5F, 1.001F, 0.5F});
    settle_viewmodel(player, world);

    const auto world_parts = build_player_world_avatar_parts(player);
    const auto world_mesh = build_player_world_avatar_mesh(player);
    const auto tessellated_world_mesh = build_creature_mesh(std::span<const CreaturePartInstance>(world_parts.data(), world_parts.size()));

    REQUIRE_FALSE(world_parts.empty());
    CHECK(world_mesh.part_count == world_parts.size());
    CHECK(tessellated_world_mesh.part_count == world_parts.size());
    CHECK(meshes_match_exactly(world_mesh, tessellated_world_mesh));

    const auto viewmodel_parts = build_player_viewmodel_parts(player);
    const auto viewmodel_mesh = build_player_viewmodel_mesh(player);
    const auto tessellated_viewmodel_mesh =
        build_creature_mesh(std::span<const CreaturePartInstance>(viewmodel_parts.parts.data(), viewmodel_parts.parts.size()));

    REQUIRE_FALSE(viewmodel_parts.empty());
    CHECK(viewmodel_mesh.mesh.part_count == viewmodel_parts.parts.size());
    CHECK(tessellated_viewmodel_mesh.part_count == viewmodel_parts.parts.size());
    CHECK(meshes_match_exactly(viewmodel_mesh.mesh, tessellated_viewmodel_mesh));
    CHECK(viewmodel_mesh.pose.root_position.x == doctest::Approx(viewmodel_parts.pose.root_position.x));
    CHECK(viewmodel_mesh.pose.root_position.y == doctest::Approx(viewmodel_parts.pose.root_position.y));
    CHECK(viewmodel_mesh.pose.root_position.z == doctest::Approx(viewmodel_parts.pose.root_position.z));
    CHECK(viewmodel_mesh.pose.wrist_position.x == doctest::Approx(viewmodel_parts.pose.wrist_position.x));
    CHECK(viewmodel_mesh.pose.wrist_position.y == doctest::Approx(viewmodel_parts.pose.wrist_position.y));
    CHECK(viewmodel_mesh.pose.wrist_position.z == doctest::Approx(viewmodel_parts.pose.wrist_position.z));
    CHECK(viewmodel_mesh.pose.item_socket_transform[3].x == doctest::Approx(viewmodel_parts.pose.item_socket_transform[3].x));
    CHECK(viewmodel_mesh.pose.item_socket_transform[3].y == doctest::Approx(viewmodel_parts.pose.item_socket_transform[3].y));
    CHECK(viewmodel_mesh.pose.item_socket_transform[3].z == doctest::Approx(viewmodel_parts.pose.item_socket_transform[3].z));
}

} // namespace valcraft
