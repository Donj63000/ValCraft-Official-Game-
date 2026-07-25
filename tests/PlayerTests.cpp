#include "app/InventoryMenu.h"
#include "app/Hotbar.h"
#include "creatures/CreatureSystem.h"
#include "gameplay/ItemDropSystem.h"
#include "gameplay/PlayerController.h"
#include "gameplay/PlayerProgression.h"
#include "gameplay/SeaAdventure.h"
#include "player/PlayerGeometry.h"
#include "world/OceanAdventureLayout.h"
#include "world/OceanSimulation.h"

#include "TestUtils.h"

#include <doctest/doctest.h>
#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/vec4.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>

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

void place_sea_adventure_underway(SeaAdventureSystem& sea_adventure, int world_seed) {
    // Je place explicitement les anciens tests de physique en pleine mer : ils
    // continuent ainsi a verifier le transport dynamique independamment du
    // nouveau scenario de depart au port.
    auto state = sea_adventure.save_state();
    state.voyage_phase = SeaVoyagePhase::Underway;
    state.voyage_phase_elapsed = 0.0F;
    sea_adventure.load_state(state, world_seed);
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

TEST_CASE("player progression thresholds bonuses and level cap stay coherent") {
    CHECK(player_experience_for_next_level(1U) == 100ULL);
    CHECK(player_experience_for_next_level(2U) == 150ULL);
    CHECK(player_experience_for_next_level(3U) == 225ULL);

    const auto level_30_experience = player_experience_for_next_level(30U);
    CHECK(player_experience_for_next_level(31U) == level_30_experience + (level_30_experience + 1ULL) / 2ULL);
    CHECK(player_experience_for_next_level(99U) < std::numeric_limits<std::uint64_t>::max());
    CHECK(player_experience_for_next_level(100U) == 0ULL);

    PlayerProgression progression {};
    CHECK(progression.level() == 1U);
    CHECK(progression.attack_damage_multiplier() == doctest::Approx(1.0F));
    CHECK(progression.damage_resistance_percent() == doctest::Approx(0.0F));
    CHECK(progression.apnea_resistance_percent() == doctest::Approx(0.0F));
    CHECK(progression.fall_safety_multiplier() == doctest::Approx(1.0F));
    CHECK(progression.movement_speed_multiplier() == doctest::Approx(1.0F));
    CHECK(progression.block_break_speed_multiplier() == doctest::Approx(1.0F));
    CHECK_FALSE(progression.has_super_vision_power());
    CHECK_FALSE(player_has_super_vision_power(29U));
    CHECK(player_has_super_vision_power(30U));
    CHECK_FALSE(progression.has_flight_power());
    CHECK_FALSE(player_has_flight_power(99U));
    CHECK(player_has_flight_power(100U));

    auto gain = progression.add_experience(99ULL);
    CHECK(gain.levels_gained == 0U);
    CHECK(progression.level() == 1U);
    CHECK(progression.experience() == 99ULL);

    gain = progression.add_experience(1ULL);
    CHECK(gain.awarded_experience == 1ULL);
    CHECK(gain.levels_gained == 1U);
    CHECK(progression.level() == 2U);
    CHECK(progression.experience() == 0ULL);
    CHECK(progression.attack_damage_multiplier() == doctest::Approx(1.01F));
    CHECK(progression.damage_resistance_percent() == doctest::Approx(1.0F));
    CHECK(progression.apnea_resistance_percent() == doctest::Approx(1.0F));
    CHECK(progression.fall_safety_multiplier() == doctest::Approx(1.01F));
    CHECK(progression.movement_speed_multiplier() == doctest::Approx(1.01F));
    CHECK(progression.block_break_speed_multiplier() == doctest::Approx(1.01F));
    CHECK_FALSE(progression.has_super_vision_power());
    CHECK_FALSE(progression.has_flight_power());

    progression.load_state({31U, 0ULL});
    CHECK(progression.attack_damage_multiplier() == doctest::Approx(1.30F));
    CHECK(progression.damage_resistance_percent() == doctest::Approx(30.0F));
    CHECK(progression.apnea_resistance_percent() == doctest::Approx(30.0F));
    CHECK(progression.fall_safety_multiplier() == doctest::Approx(1.30F));
    CHECK(progression.movement_speed_multiplier() == doctest::Approx(1.30F));
    CHECK(progression.block_break_speed_multiplier() == doctest::Approx(1.30F));
    CHECK(progression.has_super_vision_power());
    CHECK_FALSE(progression.has_flight_power());

    progression.load_state({99U, player_experience_for_next_level(99U) - 1ULL});
    gain = progression.add_experience(2ULL);
    CHECK(gain.awarded_experience == 1ULL);
    CHECK(gain.levels_gained == 1U);
    CHECK(gain.reached_max_level);
    CHECK(progression.level() == 100U);
    CHECK(progression.experience() == 0ULL);
    CHECK(progression.experience_for_next_level() == 0ULL);
    CHECK(progression.attack_damage_multiplier() == doctest::Approx(1.99F));
    CHECK(progression.damage_resistance_percent() == doctest::Approx(99.0F));
    CHECK(progression.apnea_resistance_percent() == doctest::Approx(99.0F));
    CHECK(progression.fall_safety_multiplier() == doctest::Approx(1.99F));
    CHECK(progression.movement_speed_multiplier() == doctest::Approx(1.99F));
    CHECK(progression.block_break_speed_multiplier() == doctest::Approx(1.99F));
    CHECK(progression.has_super_vision_power());
    CHECK(progression.has_flight_power());

    const auto sanitized_low = sanitize_player_progression_state({0U, 101ULL});
    CHECK(sanitized_low.level == 2U);
    CHECK(sanitized_low.experience == 1ULL);

    const auto sanitized_high = sanitize_player_progression_state({1000U, std::numeric_limits<std::uint64_t>::max()});
    CHECK(sanitized_high.level == 100U);
    CHECK(sanitized_high.experience == 0ULL);
}

TEST_CASE("experience reward rules stay bounded and apply the night surface bonus only on surface") {
    CHECK(block_break_experience(to_block_id(BlockType::Stone)) == 10ULL);
    CHECK(block_break_experience(to_block_id(BlockType::Wood)) == 15ULL);
    CHECK(block_break_experience(to_block_id(BlockType::PineWood)) == 15ULL);
    CHECK(block_break_experience(to_block_id(BlockType::Planks)) == 15ULL);
    CHECK(block_break_experience(to_block_id(BlockType::CoalOre)) == 20ULL);
    CHECK(block_break_experience(to_block_id(BlockType::IronOre)) == 32ULL);
    CHECK(block_break_experience(to_block_id(BlockType::GoldOre)) == 48ULL);
    CHECK(block_break_experience(to_block_id(BlockType::DiamondOre)) == 72ULL);
    CHECK(block_break_experience(to_block_id(BlockType::MetallicAlloyOre)) == 96ULL);
    CHECK(block_break_experience(to_block_id(BlockType::MetallicAlloyOre)) >
          block_break_experience(to_block_id(BlockType::DiamondOre)));
    CHECK(block_break_experience(to_block_id(BlockType::DiamondOre)) >
          block_break_experience(to_block_id(BlockType::GoldOre)));
    CHECK(block_break_experience(to_block_id(BlockType::GoldOre)) >
          block_break_experience(to_block_id(BlockType::IronOre)));
    CHECK(block_break_experience(to_block_id(BlockType::IronOre)) >
          block_break_experience(to_block_id(BlockType::CoalOre)));
    CHECK(block_break_experience(to_block_id(BlockType::Air)) == 0ULL);

    const auto kill_reward = creature_kill_experience(CreatureSpecies::Villager, {2.5F, 13.001F, -4.5F}, 42U);
    CHECK(kill_reward >= 1ULL);
    CHECK(kill_reward <= 100ULL);
    CHECK(creature_kill_experience(CreatureSpecies::Villager, {2.5F, 13.001F, -4.5F}, 42U) == kill_reward);

    const auto day_cycle = EnvironmentClock::classify_creature_cycle(12.0F);
    const auto night_cycle = EnvironmentClock::classify_creature_cycle(23.0F);
    CHECK(experience_multiplier_for_activity(day_cycle, 12, 13) == 1U);
    CHECK(experience_multiplier_for_activity(night_cycle, 12, 13) == 2U);
    CHECK(experience_multiplier_for_activity(night_cycle, 12, 8) == 1U);
    CHECK(experience_multiplier_for_activity(night_cycle, std::nullopt, 13) == 1U);
    CHECK(multiply_experience(15ULL, experience_multiplier_for_activity(night_cycle, 12, 13)) == 30ULL);
    CHECK(multiply_experience(std::numeric_limits<std::uint64_t>::max(), 2U) == std::numeric_limits<std::uint64_t>::max());
}

TEST_CASE("player controller sanitizes non finite loaded state before gameplay math") {
    constexpr auto nan = std::numeric_limits<float>::quiet_NaN();
    constexpr auto infinity = std::numeric_limits<float>::infinity();

    PlayerController player({4.0F, 8.0F, 2.0F});
    PlayerState corrupted {};
    corrupted.position = {nan, infinity, -infinity};
    corrupted.velocity = {infinity, nan, -infinity};
    corrupted.yaw_degrees = 1000000000.0F;
    corrupted.pitch_degrees = infinity;
    corrupted.body_yaw_degrees = nan;
    corrupted.health = nan;
    corrupted.air_seconds = -infinity;
    corrupted.hurt_timer = infinity;
    corrupted.damage_cooldown = nan;
    corrupted.primary_action_progress = infinity;
    corrupted.secondary_action_progress = -infinity;
    corrupted.look_sway_yaw = nan;
    corrupted.look_sway_pitch = infinity;

    player.load_state(corrupted);

    const auto& state = player.state();
    CHECK(state.position.x == doctest::Approx(0.0F));
    CHECK(state.position.y == doctest::Approx(70.0F));
    CHECK(state.position.z == doctest::Approx(0.0F));
    CHECK(state.velocity.x == doctest::Approx(0.0F));
    CHECK(state.velocity.y == doctest::Approx(0.0F));
    CHECK(state.velocity.z == doctest::Approx(0.0F));
    CHECK(std::isfinite(state.yaw_degrees));
    CHECK(state.pitch_degrees == doctest::Approx(-18.0F));
    CHECK(state.body_yaw_degrees == doctest::Approx(state.yaw_degrees));
    CHECK(state.health == doctest::Approx(player.max_health()));
    CHECK(state.air_seconds == doctest::Approx(player.max_air_seconds()));
    CHECK(state.hurt_timer == doctest::Approx(0.0F));
    CHECK(state.damage_cooldown == doctest::Approx(0.0F));
    CHECK(state.primary_action_progress == doctest::Approx(0.0F));
    CHECK(state.secondary_action_progress == doctest::Approx(0.0F));
    CHECK(state.look_sway_yaw == doctest::Approx(0.0F));
    CHECK(state.look_sway_pitch == doctest::Approx(0.0F));

    const auto look = player.look_direction();
    CHECK(std::isfinite(look.x));
    CHECK(std::isfinite(look.y));
    CHECK(std::isfinite(look.z));

    player.apply_external_damage(nan, PlayerDeathCause::Zombie);
    CHECK(player.state().health == doctest::Approx(player.max_health()));
    player.set_damage_resistance_percent(nan);
    player.set_apnea_resistance_percent(infinity);
    player.set_fall_safety_multiplier(nan);
    player.set_movement_speed_multiplier(infinity);
    player.set_block_break_speed_multiplier(-infinity);
    CHECK(player.damage_resistance_percent() == doctest::Approx(0.0F));
    CHECK(player.apnea_resistance_percent() == doctest::Approx(0.0F));
    CHECK(player.fall_safety_multiplier() == doctest::Approx(1.0F));
    CHECK(player.movement_speed_multiplier() == doctest::Approx(1.0F));
    CHECK(player.block_break_speed_multiplier() == doctest::Approx(1.0F));
}

TEST_CASE("player controller normalizes extreme finite loaded visual state before building the viewmodel") {
    PlayerController player({0.5F, 1.001F, 0.5F});
    PlayerState extreme {};
    extreme.position = {0.5F, 1.001F, 0.5F};
    extreme.velocity = {0.0F, 0.0F, 0.0F};
    extreme.animation_time = 1000000000.0F;
    extreme.step_phase = 1000000000.0F;
    extreme.hurt_timer = 1000000000.0F;
    extreme.damage_cooldown = 1000000000.0F;
    extreme.regen_delay = 1000000000.0F;
    extreme.regen_tick_timer = 1000000000.0F;
    extreme.drowning_tick_timer = 1000000000.0F;
    extreme.fall_start_y = -1000000000.0F;
    extreme.landing_impact = 1000000000.0F;
    extreme.airborne_time = 1000000000.0F;
    extreme.look_sway_yaw = 1000000000.0F;
    extreme.look_sway_pitch = -1000000000.0F;

    player.load_state(extreme);

    const auto& state = player.state();
    CHECK(state.animation_time == doctest::Approx(3600.0F));
    CHECK(state.step_phase >= 0.0F);
    CHECK(state.step_phase < 6.2831855F);
    CHECK(state.hurt_timer <= 0.35F);
    CHECK(state.damage_cooldown <= 0.55F);
    CHECK(state.regen_delay <= 6.0F);
    CHECK(state.regen_tick_timer <= 2.5F);
    CHECK(state.drowning_tick_timer <= 1.0F);
    CHECK(state.fall_start_y == doctest::Approx(state.position.y));
    CHECK(state.landing_impact == doctest::Approx(1.0F));
    CHECK(state.airborne_time == doctest::Approx(60.0F));
    CHECK(state.look_sway_yaw == doctest::Approx(1.0F));
    CHECK(state.look_sway_pitch == doctest::Approx(-1.0F));

    const auto viewmodel = build_player_viewmodel_mesh(player);
    REQUIRE_FALSE(viewmodel.empty());
    const auto bounds = mesh_bounds(viewmodel.mesh);
    CHECK(std::isfinite(bounds.min.x));
    CHECK(std::isfinite(bounds.min.y));
    CHECK(std::isfinite(bounds.min.z));
    CHECK(std::isfinite(bounds.max.x));
    CHECK(std::isfinite(bounds.max.y));
    CHECK(std::isfinite(bounds.max.z));
    CHECK(glm::length(bounds.max - bounds.min) < 8.0F);
}

TEST_CASE("player controller ignores non finite frame input") {
    constexpr auto nan = std::numeric_limits<float>::quiet_NaN();
    constexpr auto infinity = std::numeric_limits<float>::infinity();

    World world(1500, 1);
    test::make_chunk_empty(world, {0, 0});
    test::make_flat_floor(world, -2, 2, 0, -2, 2);

    PlayerController player({0.5F, 1.001F, 0.5F});
    PlayerInput input {};
    input.move_forward = nan;
    input.move_right = infinity;
    input.move_up = -infinity;
    input.look_delta_x = nan;
    input.look_delta_y = infinity;
    input.sprint = true;

    player.update(input, nan, world);

    const auto& state = player.state();
    CHECK(std::isfinite(state.position.x));
    CHECK(std::isfinite(state.position.y));
    CHECK(std::isfinite(state.position.z));
    CHECK(std::isfinite(state.velocity.x));
    CHECK(std::isfinite(state.velocity.y));
    CHECK(std::isfinite(state.velocity.z));
    CHECK(std::isfinite(state.yaw_degrees));
    CHECK(std::isfinite(state.pitch_degrees));
    CHECK(std::isfinite(state.look_sway_yaw));
    CHECK(std::isfinite(state.look_sway_pitch));
}

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

TEST_CASE("fall safety progression lets the player fall higher before damage") {
    World world(1511, 1);
    test::make_chunk_empty(world, {0, 0});
    test::make_flat_floor(world, -2, 2, 0, -2, 2);

    PlayerController base_player({0.5F, 7.0F, 0.5F});
    PlayerController protected_player({0.5F, 7.0F, 0.5F});
    protected_player.set_fall_safety_multiplier(2.0F);

    for (int i = 0; i < 240; ++i) {
        base_player.update(PlayerInput {}, 1.0F / 60.0F, world);
        protected_player.update(PlayerInput {}, 1.0F / 60.0F, world);
    }

    CHECK(base_player.state().on_ground);
    CHECK(protected_player.state().on_ground);
    CHECK(base_player.state().health < base_player.max_health());
    CHECK(protected_player.state().health == doctest::Approx(protected_player.max_health()));
    CHECK(protected_player.fall_safety_multiplier() == doctest::Approx(2.0F));
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

TEST_CASE("apnea progression slows underwater air loss before drowning") {
    World world(15201, 1);
    test::make_chunk_empty(world, {0, 0});
    test::make_flat_floor(world, -2, 2, 0, -2, 2);
    for (int y = 1; y <= 6; ++y) {
        world.set_block(0, y, 0, to_block_id(BlockType::Water));
        world.set_block(1, y, 0, to_block_id(BlockType::Water));
    }

    PlayerController base_player({0.5F, 1.001F, 0.5F});
    PlayerController resistant_player({1.5F, 1.001F, 0.5F});
    resistant_player.set_apnea_resistance_percent(90.0F);

    for (int i = 0; i < 300; ++i) {
        base_player.update(PlayerInput {}, 1.0F / 60.0F, world);
        resistant_player.update(PlayerInput {}, 1.0F / 60.0F, world);
    }

    CHECK(base_player.state().head_underwater);
    CHECK(resistant_player.state().head_underwater);
    CHECK(base_player.state().air_seconds == doctest::Approx(5.0F).epsilon(0.08));
    CHECK(resistant_player.state().air_seconds == doctest::Approx(9.5F).epsilon(0.08));
    CHECK(resistant_player.state().air_seconds > base_player.state().air_seconds + 4.0F);
    CHECK(resistant_player.state().health == doctest::Approx(resistant_player.max_health()));
    CHECK(resistant_player.apnea_resistance_percent() == doctest::Approx(90.0F));
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

TEST_CASE("ocean adventure swimming follows tempest crests and troughs") {
    World world(
        1524,
        1,
        WorldGenerationProfile::OceanAdventure);
    EnvironmentState tempest {};
    tempest.wind_strength = 1.0F;
    tempest.storm_intensity = 1.0F;
    tempest.precipitation_intensity = 1.0F;
    tempest.violent_storm_intensity = 1.0F;
    tempest.weather_time_seconds = 2'685.0979F;
    const auto ocean =
        OceanSimulation::evaluate(
            tempest,
            OceanSurfaceProfile::OpenSea);

    auto crest_height =
        std::numeric_limits<float>::lowest();
    auto trough_height =
        std::numeric_limits<float>::max();
    glm::vec2 crest_point {};
    glm::vec2 trough_point {};

    // Je reste dans le corridor maritime profond pour isoler la surface
    // dynamique de toute berge ou colonne de terrain.
    for (int z = 96; z <= 288; z += 2) {
        for (int x = -12; x <= 12; ++x) {
            const glm::vec2 point {
                static_cast<float>(x) + 0.5F,
                static_cast<float>(z) + 0.5F,
            };
            const auto height =
                OceanSimulation::sample(
                    ocean,
                    point,
                    kOceanBuoyancyWaveCount)
                    .height;
            if (height > crest_height) {
                crest_height = height;
                crest_point = point;
            }
            if (height < trough_height) {
                trough_height = height;
                trough_point = point;
            }
        }
    }

    REQUIRE(crest_height > 2.0F);
    REQUIRE(trough_height < -2.0F);
    constexpr float surface_at_rest =
        static_cast<float>(kSeaLevel + 1);

    const glm::vec3 crest_feet {
        crest_point.x,
        surface_at_rest + crest_height - 1.25F,
        crest_point.y,
    };
    PlayerController static_crest_player(crest_feet);
    static_crest_player.update(
        PlayerInput {},
        0.0F,
        world);
    CHECK_FALSE(static_crest_player.state().swimming);

    PlayerController dynamic_crest_player(crest_feet);
    dynamic_crest_player.update(
        PlayerInput {},
        0.0F,
        world,
        nullptr,
        &ocean);
    CHECK(dynamic_crest_player.state().swimming);
    CHECK_FALSE(dynamic_crest_player.state().head_underwater);

    const glm::vec3 trough_feet {
        trough_point.x,
        surface_at_rest + trough_height + 0.45F,
        trough_point.y,
    };
    PlayerController static_trough_player(trough_feet);
    static_trough_player.update(
        PlayerInput {},
        0.0F,
        world);
    CHECK(static_trough_player.state().swimming);

    PlayerController dynamic_trough_player(trough_feet);
    dynamic_trough_player.update(
        PlayerInput {},
        0.0F,
        world,
        nullptr,
        &ocean);
    CHECK_FALSE(dynamic_trough_player.state().swimming);
    CHECK_FALSE(dynamic_trough_player.state().head_underwater);
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

TEST_CASE("progression movement multiplier increases player travel speed") {
    World world(2311, 1);
    test::make_chunk_empty(world, {0, 0});
    test::make_chunk_empty(world, {0, -1});
    test::make_flat_floor(world, -4, 4, 0, -12, 12);

    PlayerController base_player({0.5F, 1.001F, 0.5F});
    PlayerController leveled_player({0.5F, 1.001F, 0.5F});
    leveled_player.set_movement_speed_multiplier(1.20F);
    CHECK(leveled_player.movement_speed_multiplier() == doctest::Approx(1.20F));

    PlayerInput input {};
    input.move_forward = 1.0F;

    for (int frame = 0; frame < 40; ++frame) {
        base_player.update(input, 1.0F / 60.0F, world);
        leveled_player.update(input, 1.0F / 60.0F, world);
    }

    const auto base_distance = std::abs(base_player.position().z - 0.5F);
    const auto leveled_distance = std::abs(leveled_player.position().z - 0.5F);
    CHECK(leveled_distance > base_distance * 1.15F);
}

TEST_CASE("flight mode can be force disabled when progression does not allow it") {
    World world(2312, 1);
    test::make_chunk_empty(world, {0, 0});
    test::make_flat_floor(world, -2, 2, 0, -2, 2);

    PlayerController player({0.5F, 1.001F, 0.5F});
    PlayerInput flight_toggle {};
    flight_toggle.toggle_fly = true;
    player.update(flight_toggle, 1.0F / 60.0F, world);
    REQUIRE(player.state().fly_mode);

    player.set_velocity({1.0F, 2.0F, 3.0F});
    player.set_fly_mode_enabled(false);

    CHECK_FALSE(player.state().fly_mode);
    CHECK(player.state().velocity.x == doctest::Approx(0.0F));
    CHECK(player.state().velocity.y == doctest::Approx(0.0F));
    CHECK(player.state().velocity.z == doctest::Approx(0.0F));
    CHECK(player.state().fall_start_y == doctest::Approx(player.position().y));
}

TEST_CASE("platform translation moves the player without resetting gameplay state") {
    PlayerController player({1.0F, 4.0F, 5.0F});
    PlayerState state {};
    state.position = {1.0F, 4.0F, 5.0F};
    state.velocity = {2.0F, -0.5F, 3.0F};
    state.fall_start_y = 9.0F;
    state.primary_action_active = true;
    state.primary_action_progress = 0.55F;
    state.secondary_action_active = true;
    state.secondary_action_progress = 0.25F;
    state.yaw_degrees = 32.0F;
    state.pitch_degrees = -12.0F;
    player.load_state(state);

    player.translate_platform_delta({0.75F, 0.0F, -1.25F});

    CHECK(player.position().x == doctest::Approx(1.75F));
    CHECK(player.position().y == doctest::Approx(4.0F));
    CHECK(player.position().z == doctest::Approx(3.75F));
    CHECK(player.state().velocity.x == doctest::Approx(2.0F));
    CHECK(player.state().velocity.y == doctest::Approx(-0.5F));
    CHECK(player.state().velocity.z == doctest::Approx(3.0F));
    CHECK(player.state().fall_start_y == doctest::Approx(9.0F));
    CHECK(player.state().primary_action_active);
    CHECK(player.state().primary_action_progress == doctest::Approx(0.55F));
    CHECK(player.state().secondary_action_active);
    CHECK(player.state().secondary_action_progress == doctest::Approx(0.25F));
    CHECK(player.state().yaw_degrees == doctest::Approx(32.0F));

    player.translate_platform_delta({0.0F, 1.5F, 0.0F});
    CHECK(player.position().y == doctest::Approx(5.5F));
    CHECK(player.state().fall_start_y == doctest::Approx(10.5F));
}

TEST_CASE("L'Amelie blueprint exposes a coherent three mast ship with two explorable levels") {
    const auto& blueprint = amelie_ship_blueprint();
    REQUIRE_FALSE(blueprint.parts.empty());
    CHECK(blueprint.name == std::string_view {"L'Am\xC3\xA9lie"});
    CHECK(blueprint.name.size() == 9U);
    CHECK(blueprint.geometry_revision != 0U);
    CHECK(blueprint.bounds.min.x < blueprint.bounds.max.x);
    CHECK(blueprint.bounds.min.y < blueprint.bounds.max.y);
    CHECK(blueprint.bounds.min.z < blueprint.bounds.max.z);
    CHECK(is_ocean_navigation_corridor_column(
        static_cast<int>(std::floor(blueprint.bounds.min.x)),
        static_cast<int>(std::floor(blueprint.bounds.min.z))));
    CHECK(is_ocean_navigation_corridor_column(
        static_cast<int>(std::ceil(blueprint.bounds.max.x)),
        static_cast<int>(std::ceil(blueprint.bounds.max.z))));

    for (const auto& part : blueprint.parts) {
        CAPTURE(static_cast<int>(part.shape));
        CAPTURE(static_cast<int>(part.material));
        CHECK(std::min(part.local_start.x, part.local_end.x) >= blueprint.bounds.min.x - 0.001F);
        CHECK(std::min(part.local_start.y, part.local_end.y) >= blueprint.bounds.min.y - 0.001F);
        CHECK(std::min(part.local_start.z, part.local_end.z) >= blueprint.bounds.min.z - 0.001F);
        CHECK(std::max(part.local_start.x, part.local_end.x) <= blueprint.bounds.max.x + 0.001F);
        CHECK(std::max(part.local_start.y, part.local_end.y) <= blueprint.bounds.max.y + 0.001F);
        CHECK(std::max(part.local_start.z, part.local_end.z) <= blueprint.bounds.max.z + 0.001F);
    }

    const auto tall_mast_count =
        std::count_if(
            blueprint.parts.begin(),
            blueprint.parts.end(),
            [](const ShipPart& part) {
                return
                    part.shape == ShipPartShape::Segment &&
                    part.material == ShipMaterial::SolidGold &&
                    part.thickness >= 0.40F &&
                    std::abs(
                        part.local_end.x -
                        part.local_start.x) < 0.001F &&
                    std::abs(
                        part.local_end.z -
                        part.local_start.z) < 0.001F &&
                    std::abs(
                        part.local_end.y -
                        part.local_start.y) >= 10.0F;
            });

    CHECK(tall_mast_count == 3);

    const auto sail_count =
        std::count_if(
            blueprint.parts.begin(),
            blueprint.parts.end(),
            [](const ShipPart& part) {
                return
                    part.shape == ShipPartShape::Panel &&
                    part.material ==
                        ShipMaterial::BlackCanvas;
            });

    // Six voiles carrées et trois voiles triangulaires.
    CHECK(sail_count == 9);

    for (const auto mast_z :
         std::array<float, 3> {
             -17.5F,
             0.0F,
             18.0F,
         }) {

        const auto square_sails =
            std::count_if(
                blueprint.parts.begin(),
                blueprint.parts.end(),
                [mast_z](const ShipPart& part) {
                    const auto center_z =
                        (
                            part.local_start.z +
                            part.local_end.z
                        ) *
                        0.5F;

                    return
                        part.shape ==
                            ShipPartShape::Panel &&
                        part.material ==
                            ShipMaterial::BlackCanvas &&
                        std::abs(
                            part.orientation.z) >
                            0.90F &&
                        std::abs(
                            center_z -
                            mast_z) <
                            0.50F;
                });

        CHECK(square_sails == 2);
    }

    for (const auto& part : blueprint.parts) {
        if (part.material == ShipMaterial::BlackCanvas ||
            part.material == ShipMaterial::Rope) {

            CHECK_FALSE(part.collidable);
            CHECK_FALSE(part.supports_player);
        }
    }

    auto forward_lantern_count =
        std::size_t {0};

    for (const auto& part : blueprint.parts) {
        const auto center_z =
            (
                part.local_start.z +
                part.local_end.z
            ) *
            0.5F;

        const auto min_y =
            std::min(
                part.local_start.y,
                part.local_end.y);

        if (part.material != ShipMaterial::Lantern ||
            center_z <= 33.0F ||
            min_y < 4.0F) {

            continue;
        }

        ++forward_lantern_count;

        CHECK(
            std::max(
                std::abs(part.local_start.x),
                std::abs(part.local_end.x)) <=
            1.80F);
    }

    CHECK(forward_lantern_count == 2);

    auto stern_engraved_name =
        std::u32string {};

    auto mast_engraved_name =
        std::u32string {};

    auto previous_stern_glyph_min_x =
        std::numeric_limits<float>::infinity();

    auto previous_mast_glyph_min_y =
        std::numeric_limits<float>::infinity();

    for (const auto& part : blueprint.parts) {
        if (part.shape != ShipPartShape::Glyph) {
            continue;
        }

        CHECK_FALSE(part.collidable);

        if (part.local_start.z < -29.0F &&
            part.local_end.z < -29.0F) {

            stern_engraved_name.push_back(
                part.glyph);

            CHECK(
                part.local_end.x <
                previous_stern_glyph_min_x);

            previous_stern_glyph_min_x =
                part.local_start.x;

            continue;
        }

        const auto center_z =
            (
                part.local_start.z +
                part.local_end.z
            ) *
            0.5F;

        CHECK(
            std::abs(center_z) <
            0.30F);

        CHECK(
            part.material ==
            ShipMaterial::Iron);

        CHECK(
            part.local_end.y <
            previous_mast_glyph_min_y);

        previous_mast_glyph_min_y =
            part.local_start.y;

        mast_engraved_name.push_back(
            part.glyph);
    }

    CHECK(
        stern_engraved_name ==
        U"L'Am\u00E9lie");

    CHECK(
        mast_engraved_name ==
        U"L'am\u00E9lie");

    const auto lower_stair_count = std::count_if(
        blueprint.parts.begin(),
        blueprint.parts.end(),
        [](const ShipPart& part) {
            return part.shape == ShipPartShape::Stair && part.supports_player &&
                   std::abs(part.local_start.y - 1.0F) < 0.001F && part.local_start.z < 0.0F;
        });
    const auto forward_stair_count = std::count_if(
        blueprint.parts.begin(),
        blueprint.parts.end(),
        [](const ShipPart& part) {
            return part.shape == ShipPartShape::Stair && part.supports_player &&
                   std::abs(part.local_start.y - 1.0F) < 0.001F && part.local_start.z > 0.0F;
        });
    CHECK(lower_stair_count == 6);
    CHECK(forward_stair_count == 6);
    for (int half_step = 1; half_step <= 6; ++half_step) {
        const auto expected_top = 1.0F + static_cast<float>(half_step) * 0.5F;
        const auto matching_steps = std::count_if(
            blueprint.parts.begin(),
            blueprint.parts.end(),
            [expected_top](const ShipPart& part) {
                return part.shape == ShipPartShape::Stair &&
                       std::abs(part.local_start.y - 1.0F) < 0.001F &&
                       std::abs(part.local_end.y - expected_top) < 0.001F;
            });
        CHECK(matching_steps == 2);
    }

    SeaAdventureSystem sea_adventure {};
    sea_adventure.reset(5520);
    const auto& ship = sea_adventure.ship_entity();
    const auto origin = ship.world_origin();
    const auto& anchors = blueprint.anchors;
    CHECK(sea_adventure.deck_spawn_position() == origin + anchors.safe_spawn);
    const std::array<glm::vec3, 5> interior_anchors {{
        anchors.lower_deck,
        anchors.captain_cabin,
        anchors.crew_quarters,
        anchors.galley,
        anchors.cargo_hold,
    }};
    for (const auto& anchor : interior_anchors) {
        const auto feet = origin + anchor;
        const auto support = ship.support_height(feet);
        REQUIRE(support.has_value());
        CHECK(*support == doctest::Approx(origin.y + 1.0F));
        CHECK(feet.y == doctest::Approx(*support + 0.01F));
        CHECK_FALSE(ship.intersects_aabb(
            feet + glm::vec3 {-0.30F, 0.0F, -0.30F},
            feet + glm::vec3 {0.30F, 1.80F, 0.30F}));
    }

    const auto main_deck_feet = origin + anchors.safe_spawn;
    const auto main_deck_support = ship.support_height(main_deck_feet);
    REQUIRE(main_deck_support.has_value());
    CHECK(*main_deck_support == doctest::Approx(origin.y + 4.0F));
    const auto exact_main_deck_support = ship.support_height_in_range(
        main_deck_feet,
        *main_deck_support,
        *main_deck_support);
    REQUIRE(exact_main_deck_support.has_value());
    CHECK(*exact_main_deck_support == doctest::Approx(*main_deck_support));
    CHECK_FALSE(ship.intersects_aabb(
        main_deck_feet + glm::vec3 {-0.30F, 0.0F, -0.30F},
        main_deck_feet + glm::vec3 {0.30F, 1.80F, 0.30F}));

    const std::array<glm::vec3, 3> upper_anchors {{
        anchors.helm,
        anchors.aft_hatch,
        anchors.fore_hatch,
    }};
    for (const auto& anchor : upper_anchors) {
        const auto feet = origin + anchor;
        const auto support = ship.support_height(feet);
        REQUIRE(support.has_value());
        CHECK(feet.y == doctest::Approx(*support + 0.01F));
        CHECK_FALSE(ship.intersects_aabb(
            feet + glm::vec3 {-0.30F, 0.0F, -0.30F},
            feet + glm::vec3 {0.30F, 1.80F, 0.30F}));
    }

    const auto glass_window = std::find_if(
        blueprint.parts.begin(),
        blueprint.parts.end(),
        [](const ShipPart& part) { return part.material == ShipMaterial::Glass; });
    REQUIRE(glass_window != blueprint.parts.end());
    CHECK(glass_window->collidable);
    const auto window_center = origin + (glass_window->local_start + glass_window->local_end) * 0.5F;
    CHECK(ship.intersects_aabb(
        window_center - glm::vec3 {0.02F},
        window_center + glm::vec3 {0.02F}));

    constexpr float kRefittedBowSampleZ = 34.5F;
    const auto forward_deck_at_bow = std::find_if(
        blueprint.parts.begin(),
        blueprint.parts.end(),
        [kRefittedBowSampleZ](const ShipPart& part) {
            const auto min_z = std::min(part.local_start.z, part.local_end.z);
            const auto max_z = std::max(part.local_start.z, part.local_end.z);
            return part.shape == ShipPartShape::Slab && min_z <= kRefittedBowSampleZ && max_z >= kRefittedBowSampleZ;
        });
    REQUIRE(forward_deck_at_bow != blueprint.parts.end());
    CHECK(std::max(std::abs(forward_deck_at_bow->local_start.x),
                   std::abs(forward_deck_at_bow->local_end.x)) <= 1.60F);
    for (const auto& part : blueprint.parts) {
        const auto min_z = std::min(part.local_start.z, part.local_end.z);
        const auto max_z = std::max(part.local_start.z, part.local_end.z);
        const auto min_y = std::min(part.local_start.y, part.local_end.y);
        if (part.collidable && part.material == ShipMaterial::CleanBeam &&
            min_z <= kRefittedBowSampleZ && max_z >= kRefittedBowSampleZ && min_y >= 5.0F) {
            CHECK(std::max(std::abs(part.local_start.x), std::abs(part.local_end.x)) <= 1.65F);
        }
    }

    const std::array<glm::vec3, 5> safe_interior_anchors {{
        anchors.lower_deck,
        anchors.captain_cabin,
        anchors.crew_quarters,
        anchors.galley,
        anchors.cargo_hold,
    }};
    for (const auto& anchor : safe_interior_anchors) {
        const auto occupant_min = anchor + glm::vec3 {-0.30F, 0.0F, -0.30F};
        const auto occupant_max = anchor + glm::vec3 {0.30F, 1.80F, 0.30F};
        for (const auto& part : blueprint.parts) {
            if (part.material != ShipMaterial::Lantern) {
                continue;
            }
            const auto part_min = glm::min(part.local_start, part.local_end);
            const auto part_max = glm::max(part.local_start, part.local_end);
            const auto overlaps = occupant_min.x < part_max.x && occupant_max.x > part_min.x &&
                                  occupant_min.y < part_max.y && occupant_max.y > part_min.y &&
                                  occupant_min.z < part_max.z && occupant_max.z > part_min.z;
            CHECK_FALSE(overlaps);
        }
    }

    const auto collidable_part = std::find_if(
        blueprint.parts.begin(),
        blueprint.parts.end(),
        [](const ShipPart& part) { return part.collidable; });
    REQUIRE(collidable_part != blueprint.parts.end());
    const auto collidable_center = origin +
                                   (collidable_part->local_start + collidable_part->local_end) * 0.5F;
    CHECK(ship.intersects_aabb(
        collidable_center - glm::vec3 {0.01F},
        collidable_center + glm::vec3 {0.01F}));
    CHECK_FALSE(ship.intersects_aabb(
        origin + blueprint.bounds.max + glm::vec3 {10.0F},
        origin + blueprint.bounds.max + glm::vec3 {11.0F}));
    CHECK_FALSE(ship.intersects_aabb(collidable_center, collidable_center));
}

TEST_CASE("Amelie protection profile keeps ocean and weather outside the inhabited hull") {
    const auto& blueprint =
        amelie_ship_blueprint();
    const auto& profile =
        blueprint.protection_profile;

    CHECK(
        profile.half_width_at(0.0F) ==
        doctest::Approx(8.60F));
    CHECK(
        profile.half_width_at(profile.stern_z) ==
        doctest::Approx(6.35F));
    CHECK(
        profile.half_width_at(profile.bow_z) ==
        doctest::Approx(1.10F));
    CHECK(
        profile.half_width_at(-17.5F) >
        profile.half_width_at(profile.stern_z));
    CHECK(
        profile.half_width_at(18.0F) >
        profile.half_width_at(profile.bow_z));
    auto previous_half_width =
        profile.half_width_at(
            profile.stern_z);
    constexpr auto continuity_samples =
        284;
    for (auto sample = 1;
         sample <= continuity_samples;
         ++sample) {
        const auto progression =
            static_cast<float>(
                sample) /
            static_cast<float>(
                continuity_samples);
        const auto local_z =
            glm::mix(
                profile.stern_z,
                profile.bow_z,
                progression);
        const auto half_width =
            profile.half_width_at(
                local_z);
        CHECK(
            std::isfinite(
                half_width));
        CHECK(
            half_width > 0.0F);
        CHECK(
            std::abs(
                half_width -
                previous_half_width) <
            0.10F);
        previous_half_width =
            half_width;
    }

    const auto& anchors =
        blueprint.anchors;
    const std::array<glm::vec3, 5> interior_anchors {{
        anchors.lower_deck,
        anchors.captain_cabin,
        anchors.crew_quarters,
        anchors.galley,
        anchors.cargo_hold,
    }};
    for (const auto& anchor : interior_anchors) {
        CAPTURE(anchor.x);
        CAPTURE(anchor.y);
        CAPTURE(anchor.z);
        CHECK(
            profile.excludes_ocean_local(
                anchor));
        CHECK(
            profile.shelters_from_weather_local(
                anchor));
    }

    CHECK_FALSE(
        profile.excludes_ocean_local(
            anchors.safe_spawn));
    CHECK_FALSE(
        profile.shelters_from_weather_local(
            anchors.safe_spawn));
    CHECK_FALSE(
        profile.excludes_ocean_local(
            anchors.helm));
    CHECK_FALSE(
        profile.shelters_from_weather_local(
            anchors.helm));
    const std::array<glm::vec3, 6> exposed_weather_points {{
        {0.0F, profile.main_deck_top_y, 0.0F},
        anchors.helm,
        {0.0F, profile.main_deck_top_y, profile.stern_z + 0.25F},
        {0.0F, profile.main_deck_top_y, profile.bow_z - 0.25F},
        {-8.92F, 1.25F, -7.50F},
        {8.92F, 1.25F, -7.50F},
    }};
    for (const auto& point : exposed_weather_points) {
        CAPTURE(point.x);
        CAPTURE(point.y);
        CAPTURE(point.z);
        CHECK_FALSE(
            profile.shelters_from_weather_local(
                point));
    }

    const glm::vec3 exterior_hull_skin {
        profile.half_width_at(0.0F) -
            0.10F,
        1.50F,
        0.0F,
    };
    CHECK(
        profile.excludes_ocean_local(
            exterior_hull_skin));
    CHECK_FALSE(
        profile.shelters_from_weather_local(
            exterior_hull_skin));
    CHECK_FALSE(
        profile.excludes_ocean_local(
            {8.92F, 1.25F, -7.50F}));
    CHECK_FALSE(
        profile.shelters_from_weather_local(
            {8.92F, 1.25F, -7.50F}));
    const auto middle_boundary =
        profile.half_width_at(0.0F) -
        profile.middle_width_inset;
    CHECK(
        profile.excludes_ocean_local(
            {
                middle_boundary,
                profile.middle_hull_min_y,
                0.0F,
            }));
    CHECK_FALSE(
        profile.excludes_ocean_local(
            {
                middle_boundary +
                    profile.boundary_margin +
                    0.01F,
                profile.middle_hull_min_y,
                0.0F,
            }));
    CHECK_FALSE(
        profile.excludes_ocean_local(
            {
                0.0F,
                1.50F,
                profile.stern_z -
                    profile.boundary_margin -
                    0.01F,
            }));
    CHECK_FALSE(
        profile.excludes_ocean_local(
            {
                0.0F,
                1.50F,
                profile.bow_z +
                    profile.boundary_margin +
                    0.01F,
            }));

    CHECK(
        profile.excludes_ocean_local(
            {0.0F, 0.0F, 0.0F}));
    CHECK_FALSE(
        profile.shelters_from_weather_local(
            {0.0F, 0.0F, 0.0F}));
    CHECK_FALSE(
        profile.excludes_ocean_local(
            {0.0F, profile.main_deck_top_y, 0.0F}));
    CHECK_FALSE(
        profile.shelters_from_weather_local(
            {0.0F, profile.main_deck_top_y, 0.0F}));

    ShipEntity ship {};
    ship.set_position(
        {12.5F, 49.0F, 23.5F});
    ship.set_ocean_pose(
        0.42F,
        0.122173048F,
        -0.191986218F);
    for (const auto& anchor : interior_anchors) {
        const auto world_anchor =
            ship.local_to_world_point(
                anchor);
        CHECK(
            ship.excludes_ocean_at(
                world_anchor));
        CHECK(
            ship.is_weather_sheltered_at(
                world_anchor));
    }

    const auto exterior_hull_world =
        ship.local_to_world_point(
            exterior_hull_skin);
    CHECK(
        ship.excludes_ocean_at(
            exterior_hull_world));
    CHECK_FALSE(
        ship.is_weather_sheltered_at(
            exterior_hull_world));

    constexpr auto nan =
        std::numeric_limits<float>::quiet_NaN();
    constexpr auto infinity =
        std::numeric_limits<float>::infinity();
    CHECK(
        profile.half_width_at(nan) ==
        doctest::Approx(0.0F));
    CHECK_FALSE(
        profile.excludes_ocean_local(
            {nan, 1.50F, 0.0F}));
    CHECK_FALSE(
        profile.shelters_from_weather_local(
            {0.0F, infinity, 0.0F}));
    CHECK_FALSE(
        ship.excludes_ocean_at(
            {nan, 49.0F, 0.0F}));
    CHECK_FALSE(
        ship.is_weather_sheltered_at(
            {0.0F, infinity, 0.0F}));

    const ShipProtectionProfile empty_profile {};
    CHECK(
        empty_profile.half_width_at(0.0F) ==
        doctest::Approx(0.0F));
    CHECK_FALSE(
        empty_profile.excludes_ocean_local(
            {0.0F, 1.50F, 0.0F}));
    CHECK_FALSE(
        empty_profile.shelters_from_weather_local(
            {0.0F, 1.50F, 0.0F}));

    auto malformed_profile =
        profile;
    malformed_profile.middle_width_inset =
        infinity;
    CHECK_FALSE(
        malformed_profile.excludes_ocean_local(
            {0.0F, 1.50F, 0.0F}));
    CHECK_FALSE(
        malformed_profile.shelters_from_weather_local(
            {0.0F, 1.50F, 0.0F}));
}

TEST_CASE("ship watertight volume rejects tempest water contacts while adjacent sea remains swimmable") {
    World world(
        1525,
        1,
        WorldGenerationProfile::OceanAdventure);
    ShipEntity ship {};
    ship.set_position(
        {0.5F, 49.0F, 128.5F});

    OceanState synthetic_crest {};
    synthetic_crest.waves[0] = {
        {1.0F, 0.0F},
        3.5F,
        0.01F,
        1.570796327F,
        0.0F,
    };

    const auto& anchors =
        amelie_ship_blueprint().anchors;
    const std::array<glm::vec3, 5> interior_anchors {{
        anchors.lower_deck,
        anchors.captain_cabin,
        anchors.crew_quarters,
        anchors.galley,
        anchors.cargo_hold,
    }};
    const glm::vec3 adjacent_sea_local {
        10.0F,
        1.01F,
        0.0F,
    };
    const std::array<glm::vec3, 3> ocean_poses {{
        {0.40F, 0.0F, 0.0F},
        {-0.10F, 0.025F, 0.0F},
        {-0.10F, 0.0F, -0.040F},
    }};

    // Je rejoue la meme crete avec du pilonnement, du tangage puis du roulis :
    // le volume etanche doit suivre exactement la pose rigide du navire.
    for (const auto& pose : ocean_poses) {
        ship.set_ocean_pose(
            pose.x,
            pose.y,
            pose.z);
        CAPTURE(pose.x);
        CAPTURE(pose.y);
        CAPTURE(pose.z);

        // Je place volontairement la crete au-dessus de la tete : l'interieur
        // doit rester sec alors que la mer voisine conserve toute sa physique.
        for (const auto& anchor : interior_anchors) {
            PlayerController protected_player(
                ship.local_to_world_point(
                    anchor));
            protected_player.update(
                PlayerInput {},
                0.0F,
                world,
                &ship,
                &synthetic_crest);

            CAPTURE(anchor.z);
            CHECK_FALSE(
                protected_player.state().swimming);
            CHECK_FALSE(
                protected_player.state().head_underwater);
        }

        const auto adjacent_sea_world =
            ship.local_to_world_point(
                adjacent_sea_local);
        REQUIRE(
            world.peek_water_level_or_generated(
                static_cast<int>(
                    std::floor(adjacent_sea_world.x)),
                kSeaLevel,
                static_cast<int>(
                    std::floor(adjacent_sea_world.z))) >
            0);
        CHECK_FALSE(
            ship.excludes_ocean_at(
                adjacent_sea_world));

        PlayerController exposed_player(
            adjacent_sea_world);
        exposed_player.update(
            PlayerInput {},
            0.0F,
            world,
            &ship,
            &synthetic_crest);
        CHECK(
            exposed_player.state().swimming);
        CHECK(
            exposed_player.state().head_underwater);
    }
}

TEST_CASE("L'Amelie exposes two mirrored boarding nets without changing ship collisions") {
    const auto& blueprint = amelie_ship_blueprint();
    std::array<const ShipPart*, 2> nets {{nullptr, nullptr}};
    auto net_index = std::size_t {0U};

    for (const auto& part : blueprint.parts) {
        if (part.shape != ShipPartShape::ClimbableNet) {
            continue;
        }
        REQUIRE(net_index < nets.size());
        nets[net_index++] = &part;
    }

    REQUIRE(net_index == nets.size());
    std::sort(nets.begin(), nets.end(), [](const ShipPart* lhs, const ShipPart* rhs) {
        return lhs->local_start.x < rhs->local_start.x;
    });

    for (std::size_t index = 0; index < nets.size(); ++index) {
        const auto& net = *nets[index];
        const auto side = index == 0U ? -1.0F : 1.0F;
        CAPTURE(side);
        CHECK(net.material == ShipMaterial::Rope);
        CHECK_FALSE(net.collidable);
        CHECK_FALSE(net.supports_player);
        CHECK(std::min(net.local_start.x, net.local_end.x) == doctest::Approx(side * 8.92F));
        CHECK(std::max(net.local_start.x, net.local_end.x) == doctest::Approx(side * 8.92F));
        CHECK(std::min(net.local_start.y, net.local_end.y) == doctest::Approx(-1.30F));
        CHECK(std::max(net.local_start.y, net.local_end.y) == doctest::Approx(4.48F));
        CHECK(std::min(net.local_start.z, net.local_end.z) == doctest::Approx(-9.0F));
        CHECK(std::max(net.local_start.z, net.local_end.z) == doctest::Approx(-6.0F));
        CHECK(net.orientation.x == doctest::Approx(side));
        CHECK(net.orientation.y == doctest::Approx(0.0F));
        CHECK(net.orientation.z == doctest::Approx(0.0F));
    }

    // Je garde l'ouverture de la passerelle a babord et je la reproduis a
    // tribord, sans laisser une lisse ou un montant invisible dans le passage.
    const auto boarding_rail_count = std::count_if(
        blueprint.parts.begin(),
        blueprint.parts.end(),
        [](const ShipPart& part) {
            const auto min_x = std::min(part.local_start.x, part.local_end.x);
            const auto max_x = std::max(part.local_start.x, part.local_end.x);
            const auto min_y = std::min(part.local_start.y, part.local_end.y);
            const auto min_z = std::min(part.local_start.z, part.local_end.z);
            const auto max_z = std::max(part.local_start.z, part.local_end.z);
            return part.material == ShipMaterial::CleanBeam && part.collidable &&
                   std::max(std::abs(min_x), std::abs(max_x)) > 7.0F &&
                   min_y >= 4.0F && min_z < -6.0F && max_z > -9.0F;
        });
    CHECK(boarding_rail_count == 0);

    for (const auto side : {-1.0F, 1.0F}) {
        const auto lip = std::find_if(
            blueprint.parts.begin(),
            blueprint.parts.end(),
            [side](const ShipPart& part) {
                const auto min_x = std::min(part.local_start.x, part.local_end.x);
                const auto max_x = std::max(part.local_start.x, part.local_end.x);
                const auto min_z = std::min(part.local_start.z, part.local_end.z);
                const auto max_z = std::max(part.local_start.z, part.local_end.z);
                const auto reaches_side = side < 0.0F ? min_x <= -8.99F : max_x >= 8.99F;
                return part.shape == ShipPartShape::Slab &&
                       part.material == ShipMaterial::LightDeck &&
                       part.collidable && part.supports_player && reaches_side &&
                       std::abs(min_z + 9.0F) < 0.001F &&
                       std::abs(max_z + 6.0F) < 0.001F;
            });
        REQUIRE(lip != blueprint.parts.end());
    }

    ShipEntity ship {};
    const auto origin = ship.world_origin();
    for (const auto side : {-1.0F, 1.0F}) {
        const auto feet = origin + glm::vec3 {side * 9.28F, -1.0F, -7.5F};
        const auto contact = ship.climb_contact(
            feet + glm::vec3 {-0.30F, 0.0F, -0.30F},
            feet + glm::vec3 {0.30F, 1.80F, 0.30F});
        REQUIRE(contact.has_value());
        CHECK(contact->outward_normal.x == doctest::Approx(side));
        CHECK(contact->deck_exit.x == doctest::Approx(origin.x + side * 7.45F));
        CHECK(contact->deck_exit.y == doctest::Approx(origin.y + 4.01F));
        CHECK(contact->deck_exit.z == doctest::Approx(origin.z - 7.5F));
        const auto exit_support = ship.support_height(contact->deck_exit);
        REQUIRE(exit_support.has_value());
        CHECK(contact->deck_exit.y == doctest::Approx(*exit_support + 0.01F));

        const auto net_center = origin + glm::vec3 {side * 8.92F, 2.0F, -7.5F};
        CHECK_FALSE(ship.intersects_aabb(
            net_center - glm::vec3 {0.02F},
            net_center + glm::vec3 {0.02F}));
        CHECK_FALSE(ship.support_height(net_center).has_value());
        CHECK_FALSE(ship.raycast_collidable_distance(
            origin + glm::vec3 {side * 9.05F, 2.0F, -7.5F},
            glm::vec3 {-side, 0.0F, 0.0F},
            0.25F).has_value());

        const auto outside_z = origin + glm::vec3 {side * 9.28F, -1.0F, -5.5F};
        CHECK_FALSE(ship.climb_contact(
            outside_z + glm::vec3 {-0.30F, 0.0F, -0.30F},
            outside_z + glm::vec3 {0.30F, 1.80F, 0.30F}).has_value());
    }

    const auto rope = std::find_if(
        blueprint.parts.begin(),
        blueprint.parts.end(),
        [](const ShipPart& part) {
            return part.shape == ShipPartShape::Segment && part.material == ShipMaterial::Rope;
        });
    REQUIRE(rope != blueprint.parts.end());
    const auto rope_center = origin + (rope->local_start + rope->local_end) * 0.5F;
    CHECK_FALSE(ship.climb_contact(
        rope_center - glm::vec3 {0.05F},
        rope_center + glm::vec3 {0.05F}).has_value());

    const auto nan = std::numeric_limits<float>::quiet_NaN();
    CHECK_FALSE(ship.climb_contact(
        {nan, origin.y, origin.z},
        {origin.x + 1.0F, origin.y + 1.0F, origin.z + 1.0F}).has_value());
}

TEST_CASE("loaded Amelie occupants are reconciled only when the new layout requires it") {
    SeaAdventureSystem sea_adventure {};
    sea_adventure.reset(5522);
    const auto& ship = sea_adventure.ship_entity();
    const auto origin = ship.world_origin();

    const auto embedded_in_galley = origin + glm::vec3 {-3.0F, 1.01F, 5.0F};
    REQUIRE(ship.intersects_aabb(
        embedded_in_galley + glm::vec3 {-0.30F, 0.0F, -0.30F},
        embedded_in_galley + glm::vec3 {0.30F, 1.80F, 0.30F}));
    const auto furniture_result = reconcile_loaded_ship_occupant(
        ship,
        embedded_in_galley,
        0.30F,
        1.80F,
        false);
    REQUIRE(furniture_result.relocated);
    CHECK(furniture_result.position != embedded_in_galley);
    CHECK_FALSE(ship.intersects_aabb(
        furniture_result.position + glm::vec3 {-0.30F, 0.0F, -0.30F},
        furniture_result.position + glm::vec3 {0.30F, 1.80F, 0.30F}));
    const auto furniture_support = ship.support_height(furniture_result.position);
    REQUIRE(furniture_support.has_value());
    CHECK(furniture_result.position.y == doctest::Approx(*furniture_support + 0.01F));

    // Je vérifie que l'agrandissement conserve désormais cet ancien point de
    // pont au lieu de déplacer inutilement son occupant au chargement.
    const auto supported_legacy_deck = origin + glm::vec3 {7.5F, 4.0F, 0.0F};
    REQUIRE(ship.support_height(supported_legacy_deck).has_value());
    const auto legacy_result = reconcile_loaded_ship_occupant(
        ship,
        supported_legacy_deck,
        0.30F,
        1.80F,
        true);
    CHECK_FALSE(legacy_result.relocated);
    CHECK(legacy_result.position == supported_legacy_deck);

    const auto swimmer = origin + glm::vec3 {0.0F, 0.50F, 0.0F};
    const auto swimmer_result = reconcile_loaded_ship_occupant(ship, swimmer, 0.30F, 1.80F, true);
    CHECK_FALSE(swimmer_result.relocated);
    CHECK(swimmer_result.position == swimmer);

    const auto distant_occupant = origin + glm::vec3 {50.0F, 4.0F, 0.0F};
    const auto distant_result = reconcile_loaded_ship_occupant(
        ship,
        distant_occupant,
        0.30F,
        1.80F,
        true);
    CHECK_FALSE(distant_result.relocated);
    CHECK(distant_result.position == distant_occupant);

    const auto legacy_lower_deck_drop = origin + glm::vec3 {0.0F, 2.0F, -5.0F};
    REQUIRE_FALSE(ship.support_height(legacy_lower_deck_drop).has_value());
    const auto legacy_lower_result = reconcile_loaded_ship_occupant(
        ship,
        legacy_lower_deck_drop,
        0.15F,
        0.24F,
        true);
    REQUIRE(legacy_lower_result.relocated);
    CHECK(ship.support_height(legacy_lower_result.position).has_value());

    const auto legacy_cabin_roof = origin + glm::vec3 {0.0F, 9.0F, 20.0F};
    const auto legacy_roof_result = reconcile_loaded_ship_occupant(
        ship,
        legacy_cabin_roof,
        0.30F,
        1.80F,
        true);
    REQUIRE(legacy_roof_result.relocated);
    CHECK(ship.support_height(legacy_roof_result.position).has_value());

    constexpr auto nan = std::numeric_limits<float>::quiet_NaN();
    const auto invalid_drop_result = reconcile_loaded_ship_occupant(
        ship,
        {nan, 2.0F, 0.0F},
        0.15F,
        0.24F,
        true,
        ShipInvalidPositionPolicy::Preserve);
    CHECK_FALSE(invalid_drop_result.relocated);
    CHECK(std::isnan(invalid_drop_result.position.x));

    ItemDrop invalid_drop {};
    invalid_drop.position = invalid_drop_result.position;
    invalid_drop.stack = inventory_make_slot(to_block_id(BlockType::Stone), 1);
    ItemDropSystem drop_system {};
    drop_system.load_drops({invalid_drop});
    CHECK(drop_system.drops().empty());

    const auto extreme_drop_result = reconcile_loaded_ship_occupant(
        ship,
        {(std::numeric_limits<float>::max)(), 2.0F, 0.0F},
        0.15F,
        0.24F,
        true,
        ShipInvalidPositionPolicy::Preserve);
    CHECK_FALSE(extreme_drop_result.relocated);
    ItemDrop extreme_drop {};
    extreme_drop.position = extreme_drop_result.position;
    extreme_drop.stack = inventory_make_slot(to_block_id(BlockType::Stone), 1);
    drop_system.load_drops({extreme_drop});
    CHECK(drop_system.drops().empty());

    const auto stamped_legacy_origin = origin + glm::vec3 {1.0F, 0.0F, 0.0F};
    const auto offset_legacy_deck = stamped_legacy_origin + glm::vec3 {7.5F, 4.0F, 0.0F};
    const auto offset_legacy_result = reconcile_loaded_ship_occupant(
        ship,
        offset_legacy_deck,
        0.30F,
        1.80F,
        true,
        ShipInvalidPositionPolicy::Relocate,
        stamped_legacy_origin);
    REQUIRE(offset_legacy_result.relocated);
    CHECK(ship.support_height(offset_legacy_result.position).has_value());
}

TEST_CASE("ship ocean pose keeps transforms collisions supports and raycasts coherent") {
    constexpr float kRadiansPerDegree =
        0.01745329251994329577F;

    ShipEntity ship {};
    ship.set_position({12.5F, 49.0F, 23.5F});
    ship.set_ocean_pose(0.0F, 0.0F, 0.0F);
    ship.synchronize_motion_history();

    const glm::vec3 tracked_local_point {
        2.35F,
        4.10F,
        -8.25F,
    };
    const auto previous_world_point =
        ship.local_to_world_point(
            tracked_local_point);

    ship.begin_motion_step();
    ship.set_position({13.25F, 49.0F, 25.0F});
    ship.set_ocean_pose(
        0.42F,
        7.0F * kRadiansPerDegree,
        -11.0F * kRadiansPerDegree);

    const auto expected_world_point =
        ship.local_to_world_point(
            tracked_local_point);
    // Je verrouille directement le contrat des matrices transmis au rendu :
    // elles doivent produire les memes points que l'API physique du navire.
    const auto matrix_world_point =
        glm::vec3 {
            ship.model_matrix() *
            glm::vec4 {tracked_local_point, 1.0F},
        };
    const auto previous_matrix_world_point =
        glm::vec3 {
            ship.previous_model_matrix() *
            glm::vec4 {tracked_local_point, 1.0F},
        };
    const auto render_matrix_world_point =
        glm::vec3 {
            ship.render_state(true).model_matrix *
            glm::vec4 {tracked_local_point, 1.0F},
        };
    CHECK(matrix_world_point.x ==
          doctest::Approx(expected_world_point.x).epsilon(0.0001F));
    CHECK(matrix_world_point.y ==
          doctest::Approx(expected_world_point.y).epsilon(0.0001F));
    CHECK(matrix_world_point.z ==
          doctest::Approx(expected_world_point.z).epsilon(0.0001F));
    CHECK(previous_matrix_world_point.x ==
          doctest::Approx(previous_world_point.x).epsilon(0.0001F));
    CHECK(previous_matrix_world_point.y ==
          doctest::Approx(previous_world_point.y).epsilon(0.0001F));
    CHECK(previous_matrix_world_point.z ==
          doctest::Approx(previous_world_point.z).epsilon(0.0001F));
    CHECK(render_matrix_world_point.x ==
          doctest::Approx(expected_world_point.x).epsilon(0.0001F));
    CHECK(render_matrix_world_point.y ==
          doctest::Approx(expected_world_point.y).epsilon(0.0001F));
    CHECK(render_matrix_world_point.z ==
          doctest::Approx(expected_world_point.z).epsilon(0.0001F));
    const auto carried_world_point =
        previous_world_point +
        ship.motion_delta_at(
            previous_world_point);
    CHECK(carried_world_point.x ==
          doctest::Approx(expected_world_point.x).epsilon(0.0001F));
    CHECK(carried_world_point.y ==
          doctest::Approx(expected_world_point.y).epsilon(0.0001F));
    CHECK(carried_world_point.z ==
          doctest::Approx(expected_world_point.z).epsilon(0.0001F));

    const auto round_trip =
        ship.world_to_local_point(
            expected_world_point);
    CHECK(round_trip.x ==
          doctest::Approx(tracked_local_point.x).epsilon(0.0001F));
    CHECK(round_trip.y ==
          doctest::Approx(tracked_local_point.y).epsilon(0.0001F));
    CHECK(round_trip.z ==
          doctest::Approx(tracked_local_point.z).epsilon(0.0001F));

    const glm::vec3 local_direction {
        0.25F,
        -0.35F,
        0.90F,
    };
    const auto direction_round_trip =
        ship.world_to_local_direction(
            ship.local_to_world_direction(
                local_direction));
    CHECK(direction_round_trip.x ==
          doctest::Approx(local_direction.x).epsilon(0.0001F));
    CHECK(direction_round_trip.y ==
          doctest::Approx(local_direction.y).epsilon(0.0001F));
    CHECK(direction_round_trip.z ==
          doctest::Approx(local_direction.z).epsilon(0.0001F));

    const auto& blueprint =
        amelie_ship_blueprint();
    const auto bounds =
        ship.world_bounds();
    const std::array<glm::vec3, 8> local_corners {{
        {blueprint.bounds.min.x, blueprint.bounds.min.y, blueprint.bounds.min.z},
        {blueprint.bounds.max.x, blueprint.bounds.min.y, blueprint.bounds.min.z},
        {blueprint.bounds.min.x, blueprint.bounds.max.y, blueprint.bounds.min.z},
        {blueprint.bounds.max.x, blueprint.bounds.max.y, blueprint.bounds.min.z},
        {blueprint.bounds.min.x, blueprint.bounds.min.y, blueprint.bounds.max.z},
        {blueprint.bounds.max.x, blueprint.bounds.min.y, blueprint.bounds.max.z},
        {blueprint.bounds.min.x, blueprint.bounds.max.y, blueprint.bounds.max.z},
        {blueprint.bounds.max.x, blueprint.bounds.max.y, blueprint.bounds.max.z},
    }};
    for (const auto& local_corner : local_corners) {
        const auto world_corner =
            ship.local_to_world_point(
                local_corner);
        CHECK(world_corner.x >= bounds.min.x - 0.0001F);
        CHECK(world_corner.x <= bounds.max.x + 0.0001F);
        CHECK(world_corner.y >= bounds.min.y - 0.0001F);
        CHECK(world_corner.y <= bounds.max.y + 0.0001F);
        CHECK(world_corner.z >= bounds.min.z - 0.0001F);
        CHECK(world_corner.z <= bounds.max.z + 0.0001F);
    }

    const auto collidable_part =
        std::find_if(
            blueprint.parts.begin(),
            blueprint.parts.end(),
            [](const ShipPart& part) {
                const auto extent =
                    glm::abs(
                        part.local_end -
                        part.local_start);
                return part.collidable &&
                       extent.x >= 0.20F &&
                       extent.y >= 0.20F &&
                       extent.z >= 0.20F;
            });
    REQUIRE(collidable_part != blueprint.parts.end());

    const auto local_part_center =
        (collidable_part->local_start +
         collidable_part->local_end) *
        0.5F;
    const auto world_part_center =
        ship.local_to_world_point(
            local_part_center);
    CHECK(ship.intersects_aabb(
        world_part_center - glm::vec3 {0.02F},
        world_part_center + glm::vec3 {0.02F}));

    const auto current_support =
        ship.support_height(
            expected_world_point);
    REQUIRE(current_support.has_value());

    const auto previous_support =
        ship.previous_support_height(
            previous_world_point);
    REQUIRE(previous_support.has_value());

    // Le meme rayon est lance une fois sur une pose plate et une fois sur la
    // pose inclinee. Une transformation rigide doit conserver sa distance.
    ShipEntity reference_ship {};
    reference_ship.set_position({0.5F, 49.0F, 0.5F});
    reference_ship.set_ocean_pose(0.0F, 0.0F, 0.0F);
    reference_ship.synchronize_motion_history();

    const glm::vec3 local_ray_origin =
        local_part_center -
        glm::vec3 {3.0F, 0.0F, 0.0F};
    const glm::vec3 local_ray_direction {
        1.0F,
        0.0F,
        0.0F,
    };
    const auto reference_hit =
        reference_ship.raycast_collidable_distance(
            reference_ship.local_to_world_point(
                local_ray_origin),
            reference_ship.local_to_world_direction(
                local_ray_direction),
            6.0F);
    const auto transformed_hit =
        ship.raycast_collidable_distance(
            ship.local_to_world_point(
                local_ray_origin),
            ship.local_to_world_direction(
                local_ray_direction),
            6.0F);
    REQUIRE(reference_hit.has_value());
    REQUIRE(transformed_hit.has_value());
    CHECK(*transformed_hit ==
          doctest::Approx(*reference_hit).epsilon(0.0001F));
}

TEST_CASE("ship oriented collision rejects broad phase false positives and keeps exact support height") {
    ShipEntity ship {};
    ship.set_position({12.5F, 49.0F, 23.5F});
    ship.set_ocean_pose(0.35F, 0.31F, -0.43F);
    ship.synchronize_motion_history();

    // Je place cette petite AABB dans l'enveloppe monde du pied de mat, mais
    // hors de sa boite orientee : le SAT doit rejeter le faux positif large.
    const auto query_center =
        ship.local_to_world_point(
            {-0.370F, 8.090F, 0.066F});
    const glm::vec3 query_half_extents {0.080F};
    CHECK_FALSE(ship.intersects_aabb(
        query_center - query_half_extents,
        query_center + query_half_extents));

    // Je limite la plage a la hauteur analytique du pont pour isoler la sonde
    // centrale des quatre sondes laterales de l'empreinte du joueur.
    const auto deck_point =
        ship.local_to_world_point(
            {0.0F, 4.0F, 2.5F});
    const auto support =
        ship.support_height_in_range(
            {deck_point.x, deck_point.y + 0.10F, deck_point.z},
            deck_point.y,
            deck_point.y);
    REQUIRE(support.has_value());
    CHECK(*support ==
          doctest::Approx(deck_point.y).epsilon(0.0001F));
}

TEST_CASE("sea adventure ocean probes and critical springs stay bounded and continuous") {
    constexpr float kStep = 1.0F / 60.0F;
    constexpr float kRadiansPerDegree =
        0.01745329251994329577F;

    World world(5516, 1, WorldGenerationProfile::OceanAdventure);
    SeaAdventureSystem sea_adventure {};
    sea_adventure.reset(world.seed());
    place_sea_adventure_underway(sea_adventure, world.seed());
    PlayerController player(sea_adventure.deck_spawn_position());
    EnvironmentState environment {};

    auto previous_heave = 0.0F;
    auto previous_pitch = 0.0F;
    auto previous_roll = 0.0F;
    for (int frame = 0; frame < 240; ++frame) {
        const auto ratio =
            static_cast<float>(frame) / 239.0F;
        environment.weather_time_seconds =
            static_cast<float>(frame) * kStep;
        environment.wind_strength = ratio;
        environment.precipitation_intensity = ratio;
        environment.storm_intensity = ratio;
        environment.violent_storm_intensity = ratio;

        (void)sea_adventure.update(
            world,
            player,
            environment,
            kStep,
            false);

        const auto& ship = sea_adventure.ship_entity();
        const auto forward =
            ship.local_to_world_direction(
                {0.0F, 0.0F, 1.0F});
        const auto right =
            ship.local_to_world_direction(
                {1.0F, 0.0F, 0.0F});
        const auto up =
            ship.local_to_world_direction(
                {0.0F, 1.0F, 0.0F});
        const auto heave =
            ship.world_origin().y -
            ship.position().y;
        const auto pitch =
            std::atan2(-forward.y, forward.z);
        const auto roll =
            std::atan2(-up.x, right.x);

        REQUIRE(std::isfinite(heave));
        REQUIRE(std::isfinite(pitch));
        REQUIRE(std::isfinite(roll));
        CHECK(std::abs(heave) <= 3.60F);
        CHECK(std::abs(pitch) <=
              14.0F * kRadiansPerDegree + 0.0001F);
        CHECK(std::abs(roll) <=
              22.0F * kRadiansPerDegree + 0.0001F);

        if (frame > 0) {
            CHECK(std::abs(heave - previous_heave) < 0.04F);
            CHECK(std::abs(pitch - previous_pitch) < 0.01F);
            CHECK(std::abs(roll - previous_roll) < 0.01F);
        }
        previous_heave = heave;
        previous_pitch = pitch;
        previous_roll = roll;
    }
}

TEST_CASE("violent tempest keeps ship motion and deck passengers coherent") {
    constexpr float kStep = 1.0F / 60.0F;
    constexpr float kRadiansPerDegree =
        0.01745329251994329577F;

    World world(5517, 1, WorldGenerationProfile::OceanAdventure);
    SeaAdventureSystem sea_adventure {};
    sea_adventure.reset(world.seed());
    place_sea_adventure_underway(sea_adventure, world.seed());

    const glm::vec3 initial_deck_local {0.0F, 4.10F, -8.0F};
    PlayerController player(
        sea_adventure.ship_entity().local_to_world_point(
            initial_deck_local));
    EnvironmentState environment {};
    environment.wind_strength = 1.0F;
    environment.precipitation_intensity = 1.0F;
    environment.storm_intensity = 1.0F;
    environment.violent_storm_intensity = 1.0F;

    for (int frame = 0; frame < 480; ++frame) {
        environment.weather_time_seconds =
            static_cast<float>(frame) * kStep;
        const auto result = sea_adventure.update(
            world,
            player,
            environment,
            kStep,
            false);
        const auto& ship = sea_adventure.ship_entity();
        const auto local_player =
            ship.world_to_local_point(player.position());
        const auto support =
            ship.support_height(player.position());

        REQUIRE(result.ship_moved_player);
        REQUIRE(result.on_ship);
        REQUIRE(support.has_value());
        REQUIRE(std::isfinite(player.position().x));
        REQUIRE(std::isfinite(player.position().y));
        REQUIRE(std::isfinite(player.position().z));
        CHECK(std::abs(player.position().y - *support) <= 0.12F);
        CHECK(local_player.x ==
              doctest::Approx(initial_deck_local.x).epsilon(0.002F));
        CHECK(local_player.z ==
              doctest::Approx(initial_deck_local.z).epsilon(0.002F));
    }

    EnvironmentState calm {};
    calm.wind_strength = 0.0F;
    calm.precipitation_intensity = 0.0F;
    calm.storm_intensity = 0.0F;
    calm.violent_storm_intensity = 0.0F;
    calm.weather_time_seconds =
        environment.weather_time_seconds + kStep;
    const auto calm_result =
        sea_adventure.update(
            world,
            player,
            calm,
            kStep,
            false);
    const auto& calm_ship =
        sea_adventure.ship_entity();
    const auto calm_ocean =
        OceanSimulation::evaluate(
            calm,
            OceanSurfaceProfile::OpenSea);
    const auto calm_forward =
        calm_ship.local_to_world_direction(
            {0.0F, 0.0F, 1.0F});
    const auto calm_right =
        calm_ship.local_to_world_direction(
            {1.0F, 0.0F, 0.0F});
    const auto calm_up =
        calm_ship.local_to_world_direction(
            {0.0F, 1.0F, 0.0F});
    const auto calm_heave =
        calm_ship.world_origin().y -
        calm_ship.position().y;
    const auto calm_pitch =
        std::atan2(
            -calm_forward.y,
            calm_forward.z);
    const auto calm_roll =
        std::atan2(
            -calm_up.x,
            calm_right.x);

    REQUIRE(calm_result.ship_moved_player);
    REQUIRE(calm_result.on_ship);
    CHECK(std::abs(calm_heave) <=
          calm_ocean.maximum_displacement * 0.92F +
              0.0501F);
    CHECK(std::abs(calm_pitch) <=
          2.5F * kRadiansPerDegree + 0.0001F);
    CHECK(std::abs(calm_roll) <=
          4.0F * kRadiansPerDegree + 0.0001F);
}

TEST_CASE("sea adventure buoyancy uses the rendered world water profile") {
    constexpr float kStep = 1.0F / 60.0F;

    World world(
        5518,
        1,
        WorldGenerationProfile::Continental);
    SeaAdventureSystem sea_adventure {};
    sea_adventure.reset(world.seed());
    place_sea_adventure_underway(
        sea_adventure,
        world.seed());
    PlayerController player(
        sea_adventure.deck_spawn_position());
    EnvironmentState environment {};
    environment.wind_strength = 1.0F;
    environment.precipitation_intensity = 1.0F;
    environment.storm_intensity = 1.0F;
    environment.violent_storm_intensity = 1.0F;

    const auto inland_ocean =
        OceanSimulation::evaluate(
            environment,
            OceanSurfaceProfile::InlandWater);
    const auto maximum_inland_heave =
        inland_ocean.maximum_displacement *
            0.92F +
        0.05F;

    for (int frame = 0; frame < 480; ++frame) {
        environment.weather_time_seconds =
            static_cast<float>(frame) *
            kStep;
        (void)sea_adventure.update(
            world,
            player,
            environment,
            kStep,
            false);
        const auto& ship =
            sea_adventure.ship_entity();
        const auto heave =
            ship.world_origin().y -
            ship.position().y;

        CHECK(std::isfinite(heave));
        CHECK(std::abs(heave) <=
              maximum_inland_heave +
                  0.0001F);
    }
}

TEST_CASE("ship save normalization round trips supported occupants from both ends of a tilted deck") {
    constexpr float kRadiansPerDegree =
        0.01745329251994329577F;

    ShipEntity tilted_ship {};
    tilted_ship.set_position({41.5F, 49.0F, 208.5F});
    tilted_ship.set_ocean_pose(
        0.63F,
        9.0F * kRadiansPerDegree,
        -12.0F * kRadiansPerDegree);
    tilted_ship.synchronize_motion_history();

    ShipEntity restored_ship {};
    restored_ship.set_position(tilted_ship.position());
    restored_ship.set_ocean_pose(0.0F, 0.0F, 0.0F);
    restored_ship.synchronize_motion_history();

    // Je couvre la dunette et le gaillard avant : le tangage y produit les
    // plus grands ecarts entre la pose runtime et la pose neutre sauvegardee.
    constexpr std::array<glm::vec3, 2> deck_ends {{
        {0.0F, 4.51F, -30.55F},
        {0.0F, 4.51F, 29.0F},
    }};
    for (const auto& local_position : deck_ends) {
        auto current_position =
            tilted_ship.local_to_world_point(local_position);
        const auto current_support =
            tilted_ship.support_height(current_position);
        REQUIRE(current_support.has_value());
        current_position.y = *current_support;

        auto expected_persisted_position =
            restored_ship.local_to_world_point(
                tilted_ship.world_to_local_point(current_position));
        const auto expected_persisted_support =
            restored_ship.support_height(expected_persisted_position);
        REQUIRE(expected_persisted_support.has_value());
        expected_persisted_position.y =
            *expected_persisted_support;

        PlayerState player_state {};
        player_state.position = current_position;
        player_state.velocity = {1.4F, 0.0F, -0.6F};
        player_state.fall_start_y = current_position.y - 6.0F;
        player_state.airborne_time = 0.8F;
        player_state.landing_impact = 0.7F;
        player_state.on_ground = true;

        REQUIRE(normalize_supported_player_for_ship_save(
            tilted_ship,
            player_state,
            false));
        CHECK(glm::distance(player_state.position, current_position) > 0.10F);
        CHECK(player_state.position.x ==
              doctest::Approx(expected_persisted_position.x).epsilon(0.0001F));
        CHECK(player_state.position.y ==
              doctest::Approx(expected_persisted_position.y).epsilon(0.0001F));
        CHECK(player_state.position.z ==
              doctest::Approx(expected_persisted_position.z).epsilon(0.0001F));
        CHECK(player_state.velocity == glm::vec3 {1.4F, 0.0F, -0.6F});
        CHECK(player_state.fall_start_y ==
              doctest::Approx(player_state.position.y));
        CHECK(player_state.airborne_time == doctest::Approx(0.0F));
        CHECK(player_state.landing_impact == doctest::Approx(0.7F));
        CHECK(player_state.on_ground);
        const auto restored_player_support =
            restored_ship.support_height(player_state.position);
        REQUIRE(restored_player_support.has_value());
        CHECK(std::abs(player_state.position.y - *restored_player_support) <= 0.02F);

        ItemDrop drop {};
        drop.position = current_position + glm::vec3 {0.0F, 0.001F, 0.0F};
        drop.velocity = {0.4F, -0.2F, 0.3F};
        drop.grounded = true;
        drop.sleeping = true;
        drop.sleep_support_valid = true;
        drop.sleep_candidate_seconds = 0.9F;
        drop.sleep_support_check_timer = 0.4F;
        drop.sleep_support_block = {7, 48, -3};
        auto expected_drop_position =
            restored_ship.local_to_world_point(
                tilted_ship.world_to_local_point(drop.position));
        const auto expected_drop_support =
            restored_ship.support_height(expected_drop_position);
        REQUIRE(expected_drop_support.has_value());
        expected_drop_position.y =
            *expected_drop_support + 0.001F;

        REQUIRE(normalize_supported_item_drop_for_ship_save(
            tilted_ship,
            drop));
        CHECK(drop.position.x ==
              doctest::Approx(expected_drop_position.x).epsilon(0.0001F));
        CHECK(drop.position.y ==
              doctest::Approx(expected_drop_position.y).epsilon(0.0001F));
        CHECK(drop.position.z ==
              doctest::Approx(expected_drop_position.z).epsilon(0.0001F));
        CHECK(drop.velocity == glm::vec3 {0.0F});
        CHECK(drop.grounded);
        CHECK_FALSE(drop.sleeping);
        CHECK_FALSE(drop.sleep_support_valid);
        CHECK(drop.sleep_candidate_seconds == doctest::Approx(0.0F));
        CHECK(drop.sleep_support_check_timer == doctest::Approx(0.0F));
        CHECK(drop.sleep_support_block == BlockCoord {});
        const auto restored_drop_support =
            restored_ship.support_height(drop.position);
        REQUIRE(restored_drop_support.has_value());
        CHECK(std::abs(drop.position.y - *restored_drop_support) <= 0.02F);
    }

    const auto current_spawn =
        tilted_ship.local_to_world_point(
            amelie_ship_blueprint().anchors.safe_spawn);
    const auto persisted_spawn =
        tilted_ship.world_point_in_persisted_neutral_pose(current_spawn);
    const auto expected_spawn =
        restored_ship.local_to_world_point(
            amelie_ship_blueprint().anchors.safe_spawn);
    CHECK(persisted_spawn.x == doctest::Approx(expected_spawn.x).epsilon(0.0001F));
    CHECK(persisted_spawn.y == doctest::Approx(expected_spawn.y).epsilon(0.0001F));
    CHECK(persisted_spawn.z == doctest::Approx(expected_spawn.z).epsilon(0.0001F));

    PlayerState swimmer {};
    swimmer.position =
        tilted_ship.local_to_world_point(deck_ends.front());
    swimmer.velocity = {0.2F, 0.0F, 0.3F};
    swimmer.on_ground = true;
    swimmer.swimming = true;
    const auto swimmer_before = swimmer;
    CHECK_FALSE(normalize_supported_player_for_ship_save(
        tilted_ship,
        swimmer,
        false));
    CHECK(swimmer.position == swimmer_before.position);
    CHECK(swimmer.velocity == swimmer_before.velocity);
    CHECK(swimmer.swimming == swimmer_before.swimming);

    PlayerState distant_player {};
    distant_player.position =
        tilted_ship.position() +
        glm::vec3 {90.0F, 3.0F, 90.0F};
    distant_player.on_ground = true;
    const auto distant_player_before = distant_player.position;
    CHECK_FALSE(normalize_supported_player_for_ship_save(
        tilted_ship,
        distant_player,
        false));
    CHECK(distant_player.position == distant_player_before);

    PlayerState climber {};
    climber.position =
        tilted_ship.local_to_world_point(deck_ends.front());
    climber.on_ground = true;
    const auto climber_before = climber.position;
    CHECK_FALSE(normalize_supported_player_for_ship_save(
        tilted_ship,
        climber,
        true));
    CHECK(climber.position == climber_before);

    ItemDrop falling_drop {};
    falling_drop.position =
        tilted_ship.local_to_world_point(deck_ends.back());
    falling_drop.velocity = {0.0F, -2.0F, 0.0F};
    falling_drop.grounded = false;
    const auto falling_drop_before = falling_drop.position;
    CHECK_FALSE(normalize_supported_item_drop_for_ship_save(
        tilted_ship,
        falling_drop));
    CHECK(falling_drop.position == falling_drop_before);
    CHECK(falling_drop.velocity == glm::vec3 {0.0F, -2.0F, 0.0F});

    ItemDrop distant_drop {};
    distant_drop.position =
        tilted_ship.position() +
        glm::vec3 {-90.0F, 2.0F, 90.0F};
    distant_drop.grounded = true;
    const auto distant_drop_before = distant_drop.position;
    CHECK_FALSE(normalize_supported_item_drop_for_ship_save(
        tilted_ship,
        distant_drop));
    CHECK(distant_drop.position == distant_drop_before);
}

TEST_CASE("sea adventure ship entity carries players standing on its deck") {
    EnvironmentState environment {};
    World world(5501, 1, WorldGenerationProfile::OceanAdventure);
    SeaAdventureSystem sea_adventure {};
    sea_adventure.reset(world.seed());
    place_sea_adventure_underway(sea_adventure, world.seed());
    sea_adventure.stamp_ship(world);

    const auto& moving_ship = sea_adventure.ship_entity();
    const glm::vec3 initial_deck_local {0.0F, 4.10F, -8.0F};
    PlayerController deck_player(moving_ship.local_to_world_point(initial_deck_local));
    for (int frame = 0; frame < 8; ++frame) {
        const auto before_player = deck_player.position();
        const auto before_ship = sea_adventure.ship_position();
        const auto result = sea_adventure.update(world, deck_player, environment, 0.25F, false);
        const auto expected_carried_position =
            before_player + moving_ship.motion_delta_at(before_player);

        CHECK(result.ship_moved_player);
        // Le controleur reste vertical : une eventuelle correction de support
        // ne modifie que Y, alors que X/Z suivent exactement la pose rigide.
        CHECK(deck_player.position().x ==
              doctest::Approx(expected_carried_position.x).epsilon(0.001F));
        CHECK(deck_player.position().z ==
              doctest::Approx(expected_carried_position.z).epsilon(0.001F));
        const auto support = moving_ship.support_height(deck_player.position());
        REQUIRE(support.has_value());
        CHECK(std::abs(deck_player.position().y - *support) <= 0.12F);
        CHECK(sea_adventure.ship_position().z ==
              doctest::Approx(before_ship.z + result.ship_delta.z));
    }
    const auto expected_final_position =
        moving_ship.local_to_world_point(initial_deck_local);
    CHECK(deck_player.position().x ==
          doctest::Approx(expected_final_position.x).epsilon(0.001F));
    CHECK(deck_player.position().z ==
          doctest::Approx(expected_final_position.z).epsilon(0.001F));

    SeaAdventureSystem support_sea {};
    support_sea.reset(5502);
    place_sea_adventure_underway(support_sea, 5502);
    support_sea.stamp_ship(world);
    const auto& support_ship = support_sea.ship_entity();
    const auto& anchors = amelie_ship_blueprint().anchors;
    PlayerController raised_deck_player(support_ship.local_to_world_point(anchors.helm));
    const auto support_result = support_sea.update(world, raised_deck_player, environment, 0.25F, false);
    CHECK(support_result.ship_moved_player);
    CHECK(support_result.on_ship);
    const auto expected_helm = support_ship.local_to_world_point(anchors.helm);
    CHECK(raised_deck_player.position().x ==
          doctest::Approx(expected_helm.x).epsilon(0.001F));
    CHECK(raised_deck_player.position().z ==
          doctest::Approx(expected_helm.z).epsilon(0.001F));

    SeaAdventureSystem distant_sea {};
    distant_sea.reset(5503);
    distant_sea.stamp_ship(world);
    PlayerController distant_player(distant_sea.ship_position() + glm::vec3 {40.0F, 4.10F, 0.0F});
    const auto distant_before = distant_player.position();
    const auto distant_result = distant_sea.update(world, distant_player, environment, 0.25F, false);
    CHECK_FALSE(distant_result.ship_moved_player);
    CHECK(distant_player.position().x == doctest::Approx(distant_before.x));
    CHECK(distant_player.position().y == doctest::Approx(distant_before.y));
    CHECK(distant_player.position().z == doctest::Approx(distant_before.z));
}

TEST_CASE("new sea adventure waits on board then leaves the port progressively") {
    World world(5530, 1, WorldGenerationProfile::OceanAdventure);
    EnvironmentState environment {};
    SeaAdventureSystem sea_adventure {};
    sea_adventure.reset(world.seed());

    REQUIRE(sea_adventure.save_state().voyage_phase == SeaVoyagePhase::Moored);
    const auto initial_ship_position = sea_adventure.ship_position();
    PlayerController ashore_player(initial_ship_position + glm::vec3 {40.0F, 4.10F, 0.0F});
    for (int frame = 0; frame < 40; ++frame) {
        const auto result = sea_adventure.update(world, ashore_player, environment, 0.25F, false);
        CHECK(result.ship_delta == glm::vec3 {});
    }
    CHECK(sea_adventure.save_state().voyage_phase_elapsed == doctest::Approx(0.0F));
    CHECK(sea_adventure.ship_position() == initial_ship_position);

    PlayerController deck_player(sea_adventure.deck_spawn_position());
    for (int frame = 0; frame < 16; ++frame) {
        const auto result = sea_adventure.update(world, deck_player, environment, 0.25F, false);
        CHECK_FALSE(result.departure_started);
        CHECK(result.ship_delta == glm::vec3 {});
    }
    CHECK(sea_adventure.save_state().voyage_phase_elapsed == doctest::Approx(4.0F));

    // Je suspends le compteur a terre puis je reprends exactement les quatre
    // secondes restantes : les huit secondes sont bien cumulees a bord.
    deck_player.set_position(initial_ship_position + glm::vec3 {40.0F, 4.10F, 0.0F});
    for (int frame = 0; frame < 20; ++frame) {
        const auto result = sea_adventure.update(world, deck_player, environment, 0.25F, false);
        CHECK_FALSE(result.departure_started);
        CHECK(result.ship_delta == glm::vec3 {});
    }
    CHECK(sea_adventure.save_state().voyage_phase_elapsed == doctest::Approx(4.0F));

    deck_player.set_position(sea_adventure.deck_spawn_position());
    for (int frame = 0; frame < 15; ++frame) {
        const auto result = sea_adventure.update(world, deck_player, environment, 0.25F, false);
        CHECK_FALSE(result.departure_started);
        CHECK(result.ship_delta == glm::vec3 {});
    }
    REQUIRE(sea_adventure.save_state().voyage_phase == SeaVoyagePhase::Moored);
    CHECK(sea_adventure.save_state().voyage_phase_elapsed == doctest::Approx(7.75F));
    CHECK(sea_adventure.hud_state(deck_player).departure_seconds_remaining == doctest::Approx(0.25F));

    const auto departure = sea_adventure.update(world, deck_player, environment, 0.25F, false);
    CHECK(departure.departure_started);
    CHECK(departure.ship_delta == glm::vec3 {});
    REQUIRE(sea_adventure.save_state().voyage_phase == SeaVoyagePhase::Departing);
    CHECK(sea_adventure.hud_state(deck_player).departure_seconds_remaining == doctest::Approx(12.0F));

    // Je quitte volontairement le pont apres le largage : le depart engage ne
    // doit plus se mettre en pause et l'acceleration reste progressive.
    deck_player.set_position(sea_adventure.ship_position() + glm::vec3 {40.0F, 4.10F, 0.0F});
    const auto first_acceleration = sea_adventure.update(world, deck_player, environment, 0.25F, false);
    REQUIRE(first_acceleration.ship_delta.z > 0.0F);
    REQUIRE(first_acceleration.ship_speed > 0.0F);

    auto reached_open_sea = false;
    for (int frame = 0; frame < 47; ++frame) {
        const auto result = sea_adventure.update(world, deck_player, environment, 0.25F, false);
        reached_open_sea = reached_open_sea || result.reached_open_sea;
    }
    REQUIRE(reached_open_sea);
    REQUIRE(sea_adventure.save_state().voyage_phase == SeaVoyagePhase::Underway);
    const auto cruise = sea_adventure.update(world, deck_player, environment, 0.25F, false);
    CHECK(cruise.ship_speed > first_acceleration.ship_speed);
    CHECK(sea_adventure.ship_position().z > initial_ship_position.z);
}

TEST_CASE("sea adventure releases jumping flying and falling players from ship transport") {
    EnvironmentState environment {};
    World world(5510, 1, WorldGenerationProfile::OceanAdventure);
    SeaAdventureSystem sea_adventure {};
    sea_adventure.reset(world.seed());
    place_sea_adventure_underway(sea_adventure, world.seed());
    const auto& ship = sea_adventure.ship_entity();

    PlayerController jumping_player(sea_adventure.deck_spawn_position());
    for (int frame = 0; frame < 8 && !jumping_player.state().on_ground; ++frame) {
        jumping_player.update(PlayerInput {}, 1.0F / 30.0F, world, &ship);
    }
    REQUIRE(jumping_player.state().on_ground);

    PlayerInput jump_input {};
    jump_input.jump = true;
    jumping_player.update(jump_input, 1.0F / 60.0F, world, &ship);
    REQUIRE_FALSE(jumping_player.state().on_ground);
    REQUIRE(jumping_player.state().velocity.y > 0.0F);

    const auto jumping_before = jumping_player.position();
    const auto jump_result = sea_adventure.update(world, jumping_player, environment, 0.25F, false);
    CHECK_FALSE(jump_result.ship_moved_player);
    CHECK_FALSE(jump_result.on_ship);
    CHECK(jumping_player.position().z == doctest::Approx(jumping_before.z));

    PlayerController flying_player(sea_adventure.deck_spawn_position());
    flying_player.set_fly_mode_enabled(true);
    const auto flying_before = flying_player.position();
    const auto flying_result = sea_adventure.update(world, flying_player, environment, 0.25F, false);
    CHECK_FALSE(flying_result.ship_moved_player);
    CHECK_FALSE(flying_result.on_ship);
    CHECK(flying_player.position().z == doctest::Approx(flying_before.z));

    PlayerController falling_player(sea_adventure.ship_position() + glm::vec3 {0.0F, 10.0F, -8.0F});
    falling_player.set_velocity({0.0F, -4.0F, 0.0F});
    const auto falling_before = falling_player.position();
    const auto falling_result = sea_adventure.update(world, falling_player, environment, 0.25F, false);
    CHECK_FALSE(falling_result.ship_moved_player);
    CHECK_FALSE(falling_result.on_ship);
    CHECK(falling_player.position().z == doctest::Approx(falling_before.z));
}

TEST_CASE("sea adventure boards a player on deck only after a real fall collision") {
    EnvironmentState environment {};
    World world(5511, 1, WorldGenerationProfile::OceanAdventure);
    SeaAdventureSystem sea_adventure {};
    sea_adventure.reset(world.seed());
    place_sea_adventure_underway(sea_adventure, world.seed());
    const auto& ship = sea_adventure.ship_entity();
    PlayerController player(ship.world_origin() + glm::vec3 {2.5F, 10.0F, -8.5F});

    auto landed_on_ship = false;
    auto saw_untransported_fall = false;
    for (int frame = 0; frame < 180; ++frame) {
        player.update(PlayerInput {}, 1.0F / 60.0F, world, &ship);
        const auto result = sea_adventure.update(world, player, environment, 1.0F / 60.0F, false);
        if (!player.state().on_ground) {
            saw_untransported_fall = saw_untransported_fall || !result.ship_moved_player;
        }
        if (result.on_ship) {
            CHECK(result.ship_moved_player);
            landed_on_ship = true;
            break;
        }
    }

    REQUIRE(saw_untransported_fall);
    REQUIRE(landed_on_ship);
    CHECK(player.state().on_ground);
    CHECK(player.state().health < player.max_health());
    const auto support_height = ship.support_height(player.position());
    REQUIRE(support_height.has_value());
    CHECK(player.position().y == doctest::Approx(*support_height).epsilon(0.001F));
}

TEST_CASE("sea adventure moving hull pushes a player without leaving an overlap") {
    EnvironmentState environment {};
    World world(5512, 1, WorldGenerationProfile::OceanAdventure);
    SeaAdventureSystem sea_adventure {};
    sea_adventure.reset(world.seed());
    place_sea_adventure_underway(sea_adventure, world.seed());
    const auto& ship = sea_adventure.ship_entity();
    const auto& blueprint = amelie_ship_blueprint();
    const auto bow_closure = std::find_if(
        blueprint.parts.begin(),
        blueprint.parts.end(),
        [](const ShipPart& part) {
            return part.collidable && part.material == ShipMaterial::DarkHull &&
                   part.local_start.x < 0.0F && part.local_end.x > 0.0F &&
                   part.local_end.z >= 30.0F && part.local_start.y < 1.0F && part.local_end.y > 2.0F;
        });
    REQUIRE(bow_closure != blueprint.parts.end());
    PlayerController player(
        ship.world_origin() + glm::vec3 {0.0F, 1.001F, bow_closure->local_end.z + 0.31F});
    player.set_fly_mode_enabled(true);

    REQUIRE_FALSE(player.overlaps_dynamic_obstacle(ship));
    const auto player_before = player.position();
    const auto result = sea_adventure.update(world, player, environment, 0.25F, false);
    const auto expected_position =
        player_before + ship.motion_delta_at(player_before);

    CHECK(result.ship_moved_player);
    CHECK_FALSE(result.on_ship);
    CHECK(player.position().x ==
          doctest::Approx(expected_position.x).epsilon(0.001F));
    CHECK(player.position().y ==
          doctest::Approx(expected_position.y).epsilon(0.001F));
    CHECK(player.position().z ==
          doctest::Approx(expected_position.z).epsilon(0.001F));
    CHECK_FALSE(player.overlaps_dynamic_obstacle(ship));
}

TEST_CASE("sea adventure ship entity moves without rewriting world chunks") {
    EnvironmentState environment {};
    environment.wind_strength = 0.35F;
    World world(5504, 1, WorldGenerationProfile::OceanAdventure);
    SeaAdventureSystem sea_adventure {};
    sea_adventure.reset(world.seed());
    place_sea_adventure_underway(sea_adventure, world.seed());

    REQUIRE(world.chunk_records().empty());
    sea_adventure.stamp_ship(world);
    CHECK(world.chunk_records().empty());
    CHECK(world.modified_chunk_snapshots().empty());

    const auto render_before = sea_adventure.ship_render_state();
    REQUIRE(render_before.visible);
    REQUIRE(render_before.blueprint != nullptr);
    REQUIRE(render_before.parts.size() > 300U);
    const auto collidable_part = std::find_if(
        render_before.parts.begin(),
        render_before.parts.end(),
        [](const ShipPart& part) { return part.collidable; });
    REQUIRE(collidable_part != render_before.parts.end());
    const auto part_center =
        sea_adventure.ship_entity().local_to_world_point(
            (collidable_part->local_start +
             collidable_part->local_end) *
            0.5F);
    CHECK(sea_adventure.ship_entity().intersects_aabb(
        part_center - glm::vec3 {0.02F},
        part_center + glm::vec3 {0.02F}));

    PlayerController player(sea_adventure.ship_position() + glm::vec3 {0.0F, 4.10F, -8.0F});
    for (int frame = 0; frame < 24; ++frame) {
        const auto result = sea_adventure.update(world, player, environment, 1.0F / 60.0F, false);
        CHECK(result.ship_moved_player);
    }

    const auto render_after = sea_adventure.ship_render_state();
    CHECK(render_after.world_origin.z > render_before.world_origin.z);
    CHECK(render_after.parts.data() == render_before.parts.data());
    CHECK(render_after.geometry_revision == render_before.geometry_revision);
    CHECK(world.chunk_records().empty());
    CHECK(world.modified_chunk_snapshots().empty());
    CHECK_FALSE(sea_adventure.save_state().has_stamped_ship);
}

TEST_CASE("legacy sea adventure ship migration restores the generated corridor without overrides") {
    World world(5514, 0, WorldGenerationProfile::OceanAdventure);
    SeaAdventureSystem sea_adventure {};
    sea_adventure.reset(world.seed());

    const auto render_state = sea_adventure.ship_render_state();
    REQUIRE(render_state.visible);
    REQUIRE(render_state.blueprint != nullptr);
    REQUIRE_FALSE(render_state.parts.empty());
    REQUIRE(legacy_ship_voxel_count() == 2814U);

    // Je grave un voxel representatif de l'empreinte v7 historique : la
    // migration doit l'effacer sans reutiliser la geometrie visuelle actuelle.
    constexpr BlockCoord legacy_local {-3, 0, -31};
    constexpr auto legacy_block = to_block_id(BlockType::Wood);
    const auto world_x = static_cast<int>(render_state.world_origin.x) + legacy_local.x;
    const auto world_y = static_cast<int>(render_state.world_origin.y) + legacy_local.y;
    const auto world_z = static_cast<int>(render_state.world_origin.z) + legacy_local.z;
    REQUIRE(is_ocean_navigation_corridor_column(world_x, world_z));
    world.set_block(world_x, world_y, world_z, legacy_block);
    REQUIRE_FALSE(world.modified_chunk_snapshots().empty());

    auto legacy_state = sea_adventure.save_state();
    legacy_state.has_stamped_ship = true;
    legacy_state.stamped_ship_x = static_cast<std::int32_t>(std::floor(legacy_state.ship_position.x));
    legacy_state.stamped_ship_z = static_cast<std::int32_t>(std::floor(legacy_state.ship_position.z));
    sea_adventure.load_state(legacy_state);
    sea_adventure.stamp_ship(world);

    CHECK_FALSE(sea_adventure.save_state().has_stamped_ship);
    CHECK(world.modified_chunk_snapshots().empty());
    WorldGenerator generator(world.seed(), WorldGenerationProfile::OceanAdventure);
    CHECK(world.get_block(world_x, world_y, world_z) == generator.sample_block(world_x, world_y, world_z));
    CHECK(world.water_level(world_x, world_y, world_z) ==
          water_level_from_state(generator.sample_water_state(world_x, world_y, world_z)));
}

TEST_CASE("sea adventure reload keeps route fishing deterministic") {
    constexpr int seed = 5506;
    World world(seed, 1, WorldGenerationProfile::OceanAdventure);
    EnvironmentState environment {};

    SeaAdventureSystem uninterrupted {};
    uninterrupted.reset(seed);
    const auto saved = uninterrupted.save_state();

    SeaAdventureSystem restored {};
    restored.load_state(saved, seed);
    PlayerController uninterrupted_player(uninterrupted.deck_spawn_position());
    PlayerController restored_player(restored.deck_spawn_position());

    const auto uninterrupted_result = uninterrupted.update(world, uninterrupted_player, environment, 0.0F, true);
    const auto restored_result = restored.update(world, restored_player, environment, 0.0F, true);

    REQUIRE(uninterrupted_result.fishing_started);
    REQUIRE(restored_result.fishing_started);
    CHECK(restored.save_state().fishing_target_seconds ==
          doctest::Approx(uninterrupted.save_state().fishing_target_seconds));
}

TEST_CASE("sea adventure cancels fishing when the player leaves the ship") {
    World world(5507, 1, WorldGenerationProfile::OceanAdventure);
    EnvironmentState environment {};
    SeaAdventureSystem sea_adventure {};
    sea_adventure.reset(world.seed());
    place_sea_adventure_underway(sea_adventure, world.seed());
    PlayerController player(sea_adventure.deck_spawn_position());

    const auto started = sea_adventure.update(world, player, environment, 0.0F, true);
    REQUIRE(started.fishing_started);
    REQUIRE(sea_adventure.save_state().fishing_active);

    player.set_position(sea_adventure.ship_position() + glm::vec3 {40.0F, 4.10F, 0.0F});
    const auto left_ship = sea_adventure.update(world, player, environment, 0.1F, false);

    CHECK(left_ship.fishing_failed);
    CHECK_FALSE(sea_adventure.save_state().fishing_active);
    CHECK(sea_adventure.save_state().fishing_progress == doctest::Approx(0.0F));
    CHECK(sea_adventure.save_state().fishing_target_seconds == doctest::Approx(0.0F));
}

TEST_CASE("sea adventure HUD reports the weather adjusted dynamic ship speed") {
    World world(5508, 1, WorldGenerationProfile::OceanAdventure);
    EnvironmentState environment {};
    environment.wind_strength = 0.85F;
    environment.storm_intensity = 0.15F;
    SeaAdventureSystem sea_adventure {};
    sea_adventure.reset(world.seed());
    place_sea_adventure_underway(sea_adventure, world.seed());
    PlayerController player(sea_adventure.deck_spawn_position());

    const auto result = sea_adventure.update(world, player, environment, 0.1F, false);
    const auto hud = sea_adventure.hud_state(player);
    const auto spawn_local =
        sea_adventure.ship_entity().world_to_local_point(
            sea_adventure.deck_spawn_position());
    const auto& expected_spawn =
        amelie_ship_blueprint().anchors.safe_spawn;

    CHECK(std::abs(result.ship_speed - 1.18F) > 0.001F);
    CHECK(hud.ship_speed == doctest::Approx(result.ship_speed));
    CHECK(spawn_local.x == doctest::Approx(expected_spawn.x).epsilon(0.0001F));
    CHECK(spawn_local.y == doctest::Approx(expected_spawn.y).epsilon(0.0001F));
    CHECK(spawn_local.z == doctest::Approx(expected_spawn.z).epsilon(0.0001F));
}

TEST_CASE("sea adventure resource counters saturate instead of wrapping") {
    SeaAdventureSaveState state {};
    state.active = true;
    state.wood = std::numeric_limits<std::uint32_t>::max();
    state.water_flasks = std::numeric_limits<std::uint32_t>::max();
    state.food_rations = std::numeric_limits<std::uint32_t>::max();

    SeaAdventureSystem sea_adventure {};
    sea_adventure.load_state(state, 5509);

    REQUIRE(sea_adventure.collect_resource(to_block_id(BlockType::Wood)));
    REQUIRE(sea_adventure.collect_resource(to_block_id(BlockType::Water)));
    REQUIRE(sea_adventure.record_hunt(CreatureSpecies::Cow));

    CHECK(sea_adventure.save_state().wood == std::numeric_limits<std::uint32_t>::max());
    CHECK(sea_adventure.save_state().water_flasks == std::numeric_limits<std::uint32_t>::max());
    CHECK(sea_adventure.save_state().food_rations == std::numeric_limits<std::uint32_t>::max());
}

TEST_CASE("sea adventure automatically serves complete food and water refills on board") {
    World world(5540, 1, WorldGenerationProfile::OceanAdventure);
    SeaAdventureSaveState state {};
    state.active = true;
    state.voyage_phase = SeaVoyagePhase::Underway;
    state.hunger = 80.0F;
    state.thirst = 80.0F;
    state.food_rations = 2U;
    state.water_flasks = 1U;
    state.fish = 1U;

    SeaAdventureSystem sea_adventure {};
    sea_adventure.load_state(state, world.seed());
    PlayerController player(sea_adventure.deck_spawn_position());

    const auto served = sea_adventure.update(
        world,
        player,
        EnvironmentState {},
        0.0F,
        false);
    const auto& served_state = sea_adventure.save_state();

    REQUIRE(served.on_ship);
    CHECK(served.consumed_food);
    CHECK(served.consumed_water);
    CHECK_FALSE(served.starving);
    CHECK_FALSE(served.dehydrating);
    CHECK(served_state.hunger == doctest::Approx(100.0F));
    CHECK(served_state.thirst == doctest::Approx(100.0F));
    CHECK(served_state.fish == 0U);
    CHECK(served_state.food_rations == 2U);
    CHECK(served_state.water_flasks == 0U);

    const auto full_gauges = sea_adventure.update(
        world,
        player,
        EnvironmentState {},
        0.0F,
        false);

    CHECK_FALSE(full_gauges.consumed_food);
    CHECK_FALSE(full_gauges.consumed_water);
    CHECK(sea_adventure.save_state().food_rations == 2U);
}

TEST_CASE("same frame crew deliveries prevent fatal sea survival damage") {
    World world(5545, 1, WorldGenerationProfile::OceanAdventure);
    SeaAdventureSystem sea_adventure {};
    sea_adventure.reset(world.seed());

    auto state = sea_adventure.save_state();
    state.voyage_phase = SeaVoyagePhase::Underway;
    state.hunger = 0.0F;
    state.thirst = 0.0F;
    state.survival_damage_timer = 1.74F;
    state.food_rations = 0U;
    state.water_flasks = 0U;
    state.fish = 0U;

    const auto station_position =
        [](ShipCrewStation station) -> std::optional<glm::vec3> {
        for (const auto& node :
             amelie_ship_blueprint().crew_navigation_nodes) {
            if (node.station == station) {
                return node.local_position;
            }
        }
        return std::nullopt;
    };

    const auto fish_hold =
        station_position(ShipCrewStation::CargoFish);
    const auto water_hold =
        station_position(ShipCrewStation::CargoWater);
    REQUIRE(fish_hold.has_value());
    REQUIRE(water_hold.has_value());

    auto& fisher = state.crew.members[1];
    fisher.local_position = *fish_hold;
    fisher.current_station = ShipCrewStation::CargoFish;
    fisher.next_station = ShipCrewStation::CargoFish;
    fisher.destination_station = ShipCrewStation::CargoFish;
    fisher.activity = ShipCrewActivity::Carry;
    fisher.cargo = ShipCrewCargo::Fish;
    fisher.routine_step = 1U;

    auto& water_tender = state.crew.members[3];
    water_tender.local_position = *water_hold;
    water_tender.current_station = ShipCrewStation::CargoWater;
    water_tender.next_station = ShipCrewStation::CargoWater;
    water_tender.destination_station = ShipCrewStation::CargoWater;
    water_tender.activity = ShipCrewActivity::Carry;
    water_tender.cargo = ShipCrewCargo::Water;
    water_tender.routine_step = 1U;

    sea_adventure.load_state(state, world.seed());
    PlayerController player(sea_adventure.deck_spawn_position());
    auto vulnerable_player = player.state();
    vulnerable_player.health = 2.0F;
    player.load_state(vulnerable_player);

    const auto result = sea_adventure.update(
        world,
        player,
        EnvironmentState {},
        0.02F,
        false);
    const auto& rescued_state = sea_adventure.save_state();

    REQUIRE(result.on_ship);
    CHECK(result.crew_fish_delivered);
    CHECK(result.crew_water_delivered);
    CHECK(result.consumed_food);
    CHECK(result.consumed_water);
    CHECK_FALSE(result.starving);
    CHECK_FALSE(result.dehydrating);
    CHECK(rescued_state.hunger == doctest::Approx(100.0F));
    CHECK(rescued_state.thirst == doctest::Approx(100.0F));
    CHECK(rescued_state.survival_damage_timer == doctest::Approx(0.0F));
    CHECK(rescued_state.fish == 0U);
    CHECK(rescued_state.water_flasks == 0U);
    CHECK_FALSE(player.is_dead());
    CHECK(player.state().health == doctest::Approx(2.0F));
}

TEST_CASE("sea adventure complete meal falls back to stored rations") {
    World world(5541, 1, WorldGenerationProfile::OceanAdventure);
    SeaAdventureSaveState state {};
    state.active = true;
    state.voyage_phase = SeaVoyagePhase::Underway;
    state.hunger = 79.0F;
    state.thirst = 100.0F;
    state.food_rations = 1U;
    state.water_flasks = 1U;
    state.fish = 0U;

    SeaAdventureSystem sea_adventure {};
    sea_adventure.load_state(state, world.seed());
    PlayerController player(sea_adventure.deck_spawn_position());

    const auto result = sea_adventure.update(
        world,
        player,
        EnvironmentState {},
        0.0F,
        false);
    const auto& fed_state = sea_adventure.save_state();

    REQUIRE(result.on_ship);
    CHECK(result.consumed_food);
    CHECK_FALSE(result.consumed_water);
    CHECK(fed_state.hunger == doctest::Approx(100.0F));
    CHECK(fed_state.food_rations == 0U);
    CHECK(fed_state.water_flasks == 1U);
}

TEST_CASE("sea adventure leaves ship provisions untouched away from the deck") {
    World world(5542, 1, WorldGenerationProfile::OceanAdventure);
    SeaAdventureSaveState state {};
    state.active = true;
    state.voyage_phase = SeaVoyagePhase::Underway;
    state.hunger = 79.0F;
    state.thirst = 79.0F;
    state.food_rations = 1U;
    state.water_flasks = 1U;
    state.fish = 1U;

    SeaAdventureSystem sea_adventure {};
    sea_adventure.load_state(state, world.seed());
    PlayerController player(
        sea_adventure.ship_position() +
        glm::vec3 {40.0F, 4.10F, 0.0F});

    const auto result = sea_adventure.update(
        world,
        player,
        EnvironmentState {},
        0.0F,
        false);
    const auto& away_state = sea_adventure.save_state();

    CHECK_FALSE(result.on_ship);
    CHECK_FALSE(result.consumed_food);
    CHECK_FALSE(result.consumed_water);
    CHECK(away_state.hunger == doctest::Approx(79.0F));
    CHECK(away_state.thirst == doctest::Approx(79.0F));
    CHECK(away_state.food_rations == 1U);
    CHECK(away_state.water_flasks == 1U);
    CHECK(away_state.fish == 1U);
}

TEST_CASE("sea adventure survival gauges decline slowly and favor ship passengers") {
    constexpr float kStepSeconds = 0.25F;
    constexpr int kStepCount = 240;

    SeaAdventureSaveState state {};
    state.active = true;
    state.voyage_phase = SeaVoyagePhase::Underway;
    state.hunger = 100.0F;
    state.thirst = 100.0F;
    state.food_rations = 0U;
    state.water_flasks = 0U;
    state.fish = 0U;

    World aboard_world(5543, 1, WorldGenerationProfile::OceanAdventure);
    SeaAdventureSystem aboard_adventure {};
    aboard_adventure.load_state(state, aboard_world.seed());
    PlayerController aboard_player(aboard_adventure.deck_spawn_position());

    World away_world(5544, 1, WorldGenerationProfile::OceanAdventure);
    SeaAdventureSystem away_adventure {};
    away_adventure.load_state(state, away_world.seed());
    PlayerController away_player(
        away_adventure.ship_position() +
        glm::vec3 {40.0F, 4.10F, 0.0F});

    // Je simule une minute par pas reels, car la boucle maritime borne chaque
    // mise a jour a 250 ms pour rester stable lors d'un ralentissement.
    for (int step = 0; step < kStepCount; ++step) {
        const auto aboard_result = aboard_adventure.update(
            aboard_world,
            aboard_player,
            EnvironmentState {},
            kStepSeconds,
            false);
        const auto away_result = away_adventure.update(
            away_world,
            away_player,
            EnvironmentState {},
            kStepSeconds,
            false);

        REQUIRE(aboard_result.on_ship);
        REQUIRE_FALSE(away_result.on_ship);
    }

    const auto& aboard_state = aboard_adventure.save_state();
    const auto& away_state = away_adventure.save_state();

    CHECK(aboard_state.hunger == doctest::Approx(97.6F).epsilon(0.001F));
    CHECK(aboard_state.thirst == doctest::Approx(97.0F).epsilon(0.001F));
    CHECK(away_state.hunger == doctest::Approx(90.4F).epsilon(0.001F));
    CHECK(away_state.thirst == doctest::Approx(88.0F).epsilon(0.001F));
    CHECK(aboard_state.hunger > away_state.hunger);
    CHECK(aboard_state.thirst > away_state.thirst);
}

TEST_CASE(
    "sea adventure sanitizes non finite weather without corrupting the voyage") {

    World world(
        5515,
        1,
        WorldGenerationProfile::OceanAdventure);

    SeaAdventureSystem sea_adventure {};
    sea_adventure.reset(world.seed());

    place_sea_adventure_underway(
        sea_adventure,
        world.seed());

    PlayerController player(
        sea_adventure.deck_spawn_position());

    EnvironmentState environment {};

    const auto nan =
        std::numeric_limits<float>::quiet_NaN();

    environment.time_of_day = nan;

    environment.precipitation_intensity =
        std::numeric_limits<float>::infinity();

    environment.storm_intensity = nan;

    environment.wind_strength =
        -std::numeric_limits<float>::infinity();

    const auto position_before =
        sea_adventure.ship_position();

    const auto result =
        sea_adventure.update(
            world,
            player,
            environment,
            0.25F,
            true);

    const auto& state =
        sea_adventure.save_state();

    REQUIRE(result.fishing_started);

    CHECK(std::isfinite(result.ship_speed));
    CHECK(std::isfinite(result.ship_distance));
    CHECK(std::isfinite(state.ship_position.z));
    CHECK(std::isfinite(state.route_distance));
    CHECK(std::isfinite(state.hunger));
    CHECK(std::isfinite(state.thirst));
    CHECK(std::isfinite(state.stamina));
    CHECK(std::isfinite(state.fishing_progress));
    CHECK(std::isfinite(state.fishing_target_seconds));

    CHECK(
        state.ship_position.z >
        position_before.z);

    CHECK(
        state.ship_position.z <
        position_before.z + 1.0F);

    CHECK(state.hunger > 99.0F);
    CHECK(state.thirst > 99.0F);
}

TEST_CASE(
    "sea adventure stranded defeat ignores armor and invulnerability") {

    World world(
        5516,
        1,
        WorldGenerationProfile::OceanAdventure);

    SeaAdventureSystem sea_adventure {};
    sea_adventure.reset(world.seed());

    place_sea_adventure_underway(
        sea_adventure,
        world.seed());

    PlayerController player(
        sea_adventure.ship_position() +
        glm::vec3 {
            250.0F,
            4.10F,
            0.0F,
        });

    auto protected_state = player.state();
    protected_state.damage_cooldown = 0.55F;
    player.load_state(protected_state);

    player.set_damage_resistance_percent(99.0F);

    const auto result =
        sea_adventure.update(
            world,
            player,
            EnvironmentState {},
            0.0F,
            false);

    REQUIRE(result.stranded);
    REQUIRE(player.is_dead());

    CHECK(
        player.state().health ==
        doctest::Approx(0.0F));

    CHECK(
        player.state().death_cause ==
        PlayerDeathCause::Stranded);
}

TEST_CASE(
    "sea adventure respawn restores a safe survival floor without refilling stocks") {

    SeaAdventureSaveState state {};
    state.active = true;
    state.voyage_phase =
        SeaVoyagePhase::Underway;

    state.hunger = 0.0F;
    state.thirst = 0.0F;
    state.stamina = 0.0F;
    state.survival_damage_timer = 1.25F;
    state.stranded_warning_timer = 2.0F;

    state.food_rations = 2U;
    state.water_flasks = 3U;
    state.fish = 4U;

    state.fishing_active = true;
    state.fishing_progress = 4.0F;
    state.fishing_target_seconds = 10.0F;

    SeaAdventureSystem sea_adventure {};
    sea_adventure.load_state(state, 5517);

    sea_adventure.on_player_respawn();

    const auto& respawned =
        sea_adventure.save_state();

    CHECK(
        respawned.hunger ==
        doctest::Approx(35.0F));

    CHECK(
        respawned.thirst ==
        doctest::Approx(35.0F));

    CHECK(
        respawned.stamina ==
        doctest::Approx(100.0F));

    CHECK(
        respawned.survival_damage_timer ==
        doctest::Approx(0.0F));

    CHECK(
        respawned.stranded_warning_timer ==
        doctest::Approx(0.0F));

    CHECK_FALSE(respawned.fishing_active);

    CHECK(
        respawned.fishing_progress ==
        doctest::Approx(0.0F));

    CHECK(
        respawned.fishing_target_seconds ==
        doctest::Approx(0.0F));

    CHECK(respawned.food_rations == 2U);
    CHECK(respawned.water_flasks == 3U);
    CHECK(respawned.fish == 4U);
}

TEST_CASE(
    "inactive sea adventure rejects resource mutations") {

    SeaAdventureSystem sea_adventure {};

    REQUIRE_FALSE(sea_adventure.active());

    CHECK_FALSE(
        sea_adventure.collect_resource(
            to_block_id(BlockType::Wood)));

    CHECK_FALSE(
        sea_adventure.record_hunt(
            CreatureSpecies::Cow));

    CHECK(
        sea_adventure.save_state().wood ==
        0U);

    CHECK(
        sea_adventure.save_state().food_rations ==
        6U);
}

TEST_CASE(
    "sea adventure reports empty survival gauges on every frame") {

    SeaAdventureSaveState state {};
    state.active = true;
    state.voyage_phase =
        SeaVoyagePhase::Underway;

    state.hunger = 0.0F;
    state.thirst = 0.0F;
    state.food_rations = 0U;
    state.water_flasks = 0U;
    state.fish = 0U;

    World world(
        5518,
        1,
        WorldGenerationProfile::OceanAdventure);

    SeaAdventureSystem sea_adventure {};
    sea_adventure.load_state(
        state,
        world.seed());

    PlayerController player(
        sea_adventure.deck_spawn_position());

    const auto result =
        sea_adventure.update(
            world,
            player,
            EnvironmentState {},
            0.0F,
            false);

    CHECK(result.starving);
    CHECK(result.dehydrating);
    CHECK_FALSE(player.is_dead());
}

TEST_CASE(
    "sea survival damage keeps its cadence during attack invulnerability") {

    SeaAdventureSaveState state {};
    state.active = true;
    state.voyage_phase =
        SeaVoyagePhase::Underway;

    state.hunger = 0.0F;
    state.food_rations = 0U;
    state.fish = 0U;
    state.survival_damage_timer = 1.70F;

    World world(
        5519,
        1,
        WorldGenerationProfile::OceanAdventure);

    SeaAdventureSystem sea_adventure {};
    sea_adventure.load_state(
        state,
        world.seed());

    PlayerController player(
        sea_adventure.deck_spawn_position());

    auto protected_state = player.state();
    protected_state.damage_cooldown = 0.55F;
    player.load_state(protected_state);

    const auto result =
        sea_adventure.update(
            world,
            player,
            EnvironmentState {},
            0.10F,
            false);

    REQUIRE(result.starving);

    CHECK(
        player.state().health ==
        doctest::Approx(18.0F));

    CHECK_FALSE(player.is_dead());
}

TEST_CASE("player physics uses the dynamic ship as floor and obstacle") {
    World world(5505, 1, WorldGenerationProfile::OceanAdventure);
    SeaAdventureSystem sea_adventure {};
    sea_adventure.reset(world.seed());
    const auto& ship = sea_adventure.ship_entity();
    const auto origin = ship.world_origin();

    PlayerController deck_player(origin + glm::vec3 {2.5F, 4.05F, -8.5F});
    for (int frame = 0; frame < 3; ++frame) {
        deck_player.update(PlayerInput {}, 1.0F / 30.0F, world, &ship);
    }
    CHECK(deck_player.state().on_ground);
    CHECK(deck_player.position().y == doctest::Approx(origin.y + 4.0F + 0.001F));

    PlayerController mast_player(origin + glm::vec3 {-0.61F, 4.001F, 0.0F});
    mast_player.set_fly_mode_enabled(true);
    PlayerInput move_towards_mast {};
    move_towards_mast.move_right = 1.0F;
    const auto initial_x = mast_player.position().x;
    mast_player.update(move_towards_mast, 0.20F, world, &ship);
    CHECK(mast_player.position().x < initial_x + 0.05F);
    CHECK_FALSE(ship.intersects_aabb(
        mast_player.position() + glm::vec3 {-0.3F, 0.0F, -0.3F},
        mast_player.position() + glm::vec3 {0.3F, 1.8F, 0.3F}));

    const auto& parts = amelie_ship_blueprint().parts;
    const auto starboard_hull = std::find_if(
        parts.begin(),
        parts.end(),
        [](const ShipPart& part) {
            return part.collidable && part.material == ShipMaterial::DarkHull &&
                   part.local_start.x > 0.0F && part.local_start.z <= -8.5F &&
                   part.local_end.z >= -8.5F && part.local_start.y < 1.0F && part.local_end.y > 2.0F;
        });
    REQUIRE(starboard_hull != parts.end());
    PlayerController hull_player(
        origin + glm::vec3 {starboard_hull->local_end.x + 0.31F, 1.001F, -8.5F});
    hull_player.set_fly_mode_enabled(true);
    REQUIRE_FALSE(hull_player.overlaps_dynamic_obstacle(ship));
    PlayerInput move_towards_hull {};
    move_towards_hull.move_right = -1.0F;
    const auto hull_initial_x = hull_player.position().x;
    hull_player.update(move_towards_hull, 0.20F, world, &ship);
    CHECK(hull_player.position().x < hull_initial_x);
    CHECK(hull_player.position().x >= origin.x + starboard_hull->local_end.x + 0.299F);
    CHECK_FALSE(hull_player.overlaps_dynamic_obstacle(ship));
}

TEST_CASE("player climbs both Amelie boarding nets from the water without jumping off the deck") {
    World world(5534, 1, WorldGenerationProfile::OceanAdventure);
    SeaAdventureSystem sea_adventure {};
    sea_adventure.reset(world.seed());
    const auto& ship = sea_adventure.ship_entity();
    const auto origin = ship.world_origin();

    for (const auto side : {-1.0F, 1.0F}) {
        CAPTURE(side);
        PlayerController player(origin + glm::vec3 {side * 9.40F, -1.10F, -7.50F});
        PlayerInput climb {};
        climb.move_up = 1.0F;
        climb.jump = true;
        auto grabbed_net = false;
        auto reached_deck = false;

        for (int frame = 0; frame < 240; ++frame) {
            player.update(climb, 1.0F / 60.0F, world, &ship);
            grabbed_net = grabbed_net || player.is_climbing_dynamic_obstacle();
            if (!player.is_climbing_dynamic_obstacle() && player.state().on_ground &&
                player.position().y >= origin.y + 3.99F) {
                reached_deck = true;
                break;
            }
        }

        REQUIRE(grabbed_net);
        REQUIRE(reached_deck);
        CHECK(player.position().x == doctest::Approx(origin.x + side * 7.45F).epsilon(0.001F));
        CHECK(player.position().z == doctest::Approx(origin.z - 7.50F).epsilon(0.001F));
        CHECK(player.position().y == doctest::Approx(origin.y + 4.001F).epsilon(0.02F));
        CHECK(player.state().health == doctest::Approx(player.max_health()));
        CHECK_FALSE(player.overlaps_dynamic_obstacle(ship));
        const auto support = ship.support_height(player.position());
        REQUIRE(support.has_value());

        // Je garde Espace appuye plusieurs frames apres la sortie : le verrou
        // de saut doit poser le joueur sur le pont au lieu de le relancer en l'air.
        for (int frame = 0; frame < 12; ++frame) {
            player.update(climb, 1.0F / 60.0F, world, &ship);
            CHECK_FALSE(player.is_climbing_dynamic_obstacle());
            CHECK(player.state().on_ground);
        }
        CHECK(player.position().y == doctest::Approx(*support + 0.001F).epsilon(0.002F));
        CHECK(player.state().velocity.y == doctest::Approx(0.0F));
    }
}

TEST_CASE("Amelie boarding net holds descends and releases the player predictably") {
    World world(5535, 1, WorldGenerationProfile::OceanAdventure);
    SeaAdventureSystem sea_adventure {};
    sea_adventure.reset(world.seed());
    const auto& ship = sea_adventure.ship_entity();
    const auto origin = ship.world_origin();

    PlayerInput climb {};
    climb.move_up = 1.0F;
    climb.jump = true;
    PlayerController held_player(origin + glm::vec3 {9.40F, 1.25F, -7.50F});
    held_player.update(climb, 1.0F / 60.0F, world, &ship);
    REQUIRE(held_player.is_climbing_dynamic_obstacle());
    const auto held_height = held_player.position().y;

    for (int frame = 0; frame < 30; ++frame) {
        held_player.update(PlayerInput {}, 1.0F / 60.0F, world, &ship);
        REQUIRE(held_player.is_climbing_dynamic_obstacle());
    }
    CHECK(held_player.position().y == doctest::Approx(held_height).epsilon(0.001F));
    CHECK(held_player.state().airborne_time == doctest::Approx(0.0F));

    PlayerInput descend {};
    descend.move_up = -1.0F;
    auto returned_to_water = false;
    for (int frame = 0; frame < 120; ++frame) {
        held_player.update(descend, 1.0F / 60.0F, world, &ship);
        if (!held_player.is_climbing_dynamic_obstacle()) {
            returned_to_water = true;
            break;
        }
    }
    REQUIRE(returned_to_water);
    CHECK(held_player.position().y <= origin.y - 1.29F);
    CHECK(held_player.state().swimming);
    CHECK(held_player.state().health == doctest::Approx(held_player.max_health()));

    // Je sors ensuite par un bord lateral sans transformer la zone de prise en
    // mur invisible. Le deplacement ordinaire reprend des la perte de contact.
    PlayerController lateral_player(origin + glm::vec3 {9.40F, 1.25F, -6.15F});
    lateral_player.update(climb, 1.0F / 60.0F, world, &ship);
    REQUIRE(lateral_player.is_climbing_dynamic_obstacle());
    PlayerInput move_past_edge {};
    move_past_edge.move_forward = -1.0F;
    auto left_lateral_edge = false;
    for (int frame = 0; frame < 40; ++frame) {
        lateral_player.update(move_past_edge, 1.0F / 60.0F, world, &ship);
        if (!lateral_player.is_climbing_dynamic_obstacle()) {
            left_lateral_edge = true;
            break;
        }
    }
    REQUIRE(left_lateral_edge);
    CHECK(lateral_player.position().z > origin.z - 6.0F);

    PlayerController outward_player(origin + glm::vec3 {-9.40F, 1.25F, -7.50F});
    outward_player.update(climb, 1.0F / 60.0F, world, &ship);
    REQUIRE(outward_player.is_climbing_dynamic_obstacle());
    PlayerInput move_outward {};
    move_outward.move_right = -1.0F;
    outward_player.update(move_outward, 1.0F / 60.0F, world, &ship);
    CHECK_FALSE(outward_player.is_climbing_dynamic_obstacle());

    PlayerController distant_player(origin + glm::vec3 {0.0F, -1.10F, -7.50F});
    distant_player.update(climb, 1.0F / 60.0F, world, &ship);
    CHECK_FALSE(distant_player.is_climbing_dynamic_obstacle());

    PlayerController flying_player(origin + glm::vec3 {9.40F, -1.10F, -7.50F});
    flying_player.set_fly_mode_enabled(true);
    flying_player.update(climb, 1.0F / 60.0F, world, &ship);
    CHECK_FALSE(flying_player.is_climbing_dynamic_obstacle());

    PlayerController dead_player(origin + glm::vec3 {9.40F, -1.10F, -7.50F});
    dead_player.force_death(PlayerDeathCause::Drowning);
    dead_player.update(climb, 1.0F / 60.0F, world, &ship);
    CHECK_FALSE(dead_player.is_climbing_dynamic_obstacle());
}

TEST_CASE("active Amelie net climbing resets across every discontinuous player transition") {
    World world(5538, 1, WorldGenerationProfile::OceanAdventure);
    SeaAdventureSystem sea_adventure {};
    sea_adventure.reset(world.seed());
    const auto& ship = sea_adventure.ship_entity();
    const auto start = ship.world_origin() + glm::vec3 {9.40F, 1.25F, -7.50F};

    const auto attach_to_net = [&](PlayerController& player) {
        PlayerInput climb {};
        climb.move_up = 1.0F;
        climb.jump = true;
        player.update(climb, 1.0F / 60.0F, world, &ship);
        REQUIRE(player.is_climbing_dynamic_obstacle());
    };

    // Je repars d'une vraie prise active pour chaque transition afin de verifier
    // que l'etat transitoire ne peut survivre a aucune rupture de simulation.
    SUBCASE("loading a player state") {
        PlayerController player(start);
        attach_to_net(player);
        const auto saved_state = player.state();
        player.load_state(saved_state);
        CHECK_FALSE(player.is_climbing_dynamic_obstacle());
    }

    SUBCASE("forcing a new position") {
        PlayerController player(start);
        attach_to_net(player);
        player.set_position(ship.world_origin() + glm::vec3 {0.0F, 4.01F, 0.0F});
        CHECK_FALSE(player.is_climbing_dynamic_obstacle());
    }

    SUBCASE("respawning") {
        PlayerController player(start);
        attach_to_net(player);
        player.respawn(sea_adventure.deck_spawn_position());
        CHECK_FALSE(player.is_climbing_dynamic_obstacle());
    }

    SUBCASE("dying") {
        PlayerController player(start);
        attach_to_net(player);
        player.force_death(PlayerDeathCause::Drowning);
        CHECK(player.is_dead());
        CHECK_FALSE(player.is_climbing_dynamic_obstacle());
    }

    SUBCASE("entering flight mode") {
        PlayerController player(start);
        attach_to_net(player);
        PlayerInput toggle_flight {};
        toggle_flight.toggle_fly = true;
        player.update(toggle_flight, 1.0F / 60.0F, world, &ship);
        CHECK(player.state().fly_mode);
        CHECK_FALSE(player.is_climbing_dynamic_obstacle());
    }

    SUBCASE("losing the dynamic ship") {
        PlayerController player(start);
        attach_to_net(player);
        player.update(PlayerInput {}, 1.0F / 60.0F, world, nullptr);
        CHECK_FALSE(player.is_climbing_dynamic_obstacle());
    }
}

TEST_CASE("blocked Amelie net exit keeps the climber outside until the deck is clear") {
    World world(5536, 1, WorldGenerationProfile::OceanAdventure);
    SeaAdventureSystem sea_adventure {};
    sea_adventure.reset(world.seed());
    const auto& ship = sea_adventure.ship_entity();
    const auto origin = ship.world_origin();
    const auto feet = origin + glm::vec3 {9.28F, -1.10F, -7.50F};
    const auto contact = ship.climb_contact(
        feet + glm::vec3 {-0.30F, 0.0F, -0.30F},
        feet + glm::vec3 {0.30F, 1.80F, 0.30F});
    REQUIRE(contact.has_value());

    const BlockCoord blocker {
        static_cast<int>(std::floor(contact->deck_exit.x)),
        static_cast<int>(std::floor(contact->deck_exit.y)),
        static_cast<int>(std::floor(contact->deck_exit.z)),
    };
    world.set_block(blocker.x, blocker.y, blocker.z, to_block_id(BlockType::Stone));

    PlayerController player(feet);
    PlayerInput climb {};
    climb.move_up = 1.0F;
    climb.jump = true;
    for (int frame = 0; frame < 180; ++frame) {
        player.update(climb, 1.0F / 60.0F, world, &ship);
    }
    REQUIRE(player.is_climbing_dynamic_obstacle());
    CHECK(player.position().x > origin.x + 9.0F);
    CHECK(player.position().y <= contact->deck_exit.y - 0.019F);
    CHECK_FALSE(player.overlaps_dynamic_obstacle(ship));

    world.set_block(blocker.x, blocker.y, blocker.z, to_block_id(BlockType::Air));
    auto reached_deck = false;
    for (int frame = 0; frame < 30; ++frame) {
        player.update(climb, 1.0F / 60.0F, world, &ship);
        if (!player.is_climbing_dynamic_obstacle() && player.state().on_ground) {
            reached_deck = true;
            break;
        }
    }
    REQUIRE(reached_deck);
    CHECK(player.position().x == doctest::Approx(contact->deck_exit.x).epsilon(0.001F));
}

TEST_CASE("climber follows a moving Amelie without becoming on ship before the deck") {
    EnvironmentState environment {};
    World world(5537, 1, WorldGenerationProfile::OceanAdventure);

    for (const auto side : {-1.0F, 1.0F}) {
        CAPTURE(side);
        SeaAdventureSystem sea_adventure {};
        sea_adventure.reset(world.seed());
        place_sea_adventure_underway(sea_adventure, world.seed());
        const auto& ship = sea_adventure.ship_entity();
        PlayerController player(ship.world_origin() + glm::vec3 {side * 9.40F, 1.25F, -7.50F});
        PlayerInput climb {};
        climb.move_up = 1.0F;
        climb.jump = true;
        player.update(climb, 1.0F / 60.0F, world, &ship);
        REQUIRE(player.is_climbing_dynamic_obstacle());

        const auto initial_local =
            ship.world_to_local_point(
                player.position());
        for (int frame = 0; frame < 8; ++frame) {
            player.update(PlayerInput {}, 1.0F / 60.0F, world, &ship);
            const auto result = sea_adventure.update(
                world,
                player,
                environment,
                0.10F,
                frame == 0);
            CHECK(result.ship_moved_player);
            CHECK_FALSE(result.on_ship);
            CHECK_FALSE(sea_adventure.save_state().fishing_active);
            if (frame == 0) {
                CHECK(result.fishing_failed);
            }

            const auto current_local =
                ship.world_to_local_point(
                    player.position());
            // L'AABB du joueur reste verticale, donc son rayon projete sur le
            // filet change legerement avec le roulis. La prise reste toutefois
            // au meme barreau et ne derive pas le long de la coque.
            CHECK(current_local.x ==
                  doctest::Approx(initial_local.x).epsilon(0.01F));
            CHECK(current_local.y ==
                  doctest::Approx(initial_local.y).epsilon(0.01F));
            CHECK(current_local.z ==
                  doctest::Approx(initial_local.z).epsilon(0.01F));
        }

        auto reached_deck = false;
        for (int frame = 0; frame < 240; ++frame) {
            player.update(climb, 1.0F / 60.0F, world, &ship);
            const auto result = sea_adventure.update(
                world,
                player,
                environment,
                1.0F / 60.0F,
                false);
            if (result.on_ship) {
                CHECK(result.ship_moved_player);
                reached_deck = true;
                break;
            }
        }
        REQUIRE(reached_deck);
        CHECK_FALSE(player.is_climbing_dynamic_obstacle());
        const auto expected_exit =
            ship.local_to_world_point({
                side * 7.45F,
                4.01F,
                -7.50F,
            });
        CHECK(player.position().x ==
              doctest::Approx(expected_exit.x).epsilon(0.002F));
        CHECK(player.position().z ==
              doctest::Approx(expected_exit.z).epsilon(0.002F));
        CHECK(ship.support_height(player.position()).has_value());
    }
}

TEST_CASE("player walks up and down an Amelie half step without losing deck contact") {
    World world(5521, 1, WorldGenerationProfile::OceanAdventure);
    SeaAdventureSystem sea_adventure {};
    sea_adventure.reset(world.seed());
    const auto& ship = sea_adventure.ship_entity();
    const auto origin = ship.world_origin();
    PlayerController player(origin + glm::vec3 {0.0F, 1.001F, -8.5F});

    for (int frame = 0; frame < 3; ++frame) {
        player.update(PlayerInput {}, 1.0F / 60.0F, world, &ship);
    }
    REQUIRE(player.state().on_ground);
    REQUIRE(player.position().y == doctest::Approx(origin.y + 1.001F));

    PlayerInput climb {};
    climb.move_forward = 1.0F;
    const auto lower_z = player.position().z;
    auto climbed = false;
    for (int frame = 0; frame < 20; ++frame) {
        player.update(climb, 1.0F / 60.0F, world, &ship);
        if (player.position().y >= origin.y + 1.40F) {
            climbed = true;
            break;
        }
    }

    REQUIRE(climbed);
    CHECK(player.position().z < lower_z);
    CHECK(player.position().y == doctest::Approx(origin.y + 1.501F).epsilon(0.001F));
    CHECK(player.state().on_ground);
    CHECK_FALSE(player.overlaps_dynamic_obstacle(ship));

    PlayerInput descend {};
    descend.move_forward = -1.0F;
    const auto raised_z = player.position().z;
    auto descended = false;
    for (int frame = 0; frame < 20; ++frame) {
        player.update(descend, 1.0F / 60.0F, world, &ship);
        if (player.position().y <= origin.y + 1.10F) {
            descended = true;
            break;
        }
    }

    REQUIRE(descended);
    CHECK(player.position().z > raised_z);
    CHECK(player.position().y == doctest::Approx(origin.y + 1.001F).epsilon(0.001F));
    CHECK(player.state().on_ground);
    CHECK_FALSE(player.overlaps_dynamic_obstacle(ship));
}

TEST_CASE("player traverses both complete Amelie staircases in both directions") {
    World world(5523, 1, WorldGenerationProfile::OceanAdventure);
    SeaAdventureSystem sea_adventure {};
    sea_adventure.reset(world.seed());
    const auto& ship = sea_adventure.ship_entity();
    const auto origin = ship.world_origin();

    const auto walk_until = [&](PlayerController& player, float forward, const auto& reached) {
        PlayerInput input {};
        input.move_forward = forward;
        for (int frame = 0; frame < 240; ++frame) {
            player.update(input, 1.0F / 60.0F, world, &ship);
            CHECK_FALSE(player.overlaps_dynamic_obstacle(ship));
            if (reached(player.position())) {
                return true;
            }
        }
        return false;
    };

    PlayerController aft_player(origin + glm::vec3 {0.0F, 1.001F, -8.40F});
    REQUIRE(walk_until(aft_player, 1.0F, [&](const glm::vec3& position) {
        return position.y >= origin.y + 4.0F && position.z <= origin.z - 15.25F;
    }));
    REQUIRE(aft_player.state().on_ground);
    REQUIRE(walk_until(aft_player, -1.0F, [&](const glm::vec3& position) {
        return position.y <= origin.y + 1.05F && position.z >= origin.z - 8.65F;
    }));
    CHECK(aft_player.state().on_ground);

    PlayerController fore_player(origin + glm::vec3 {0.0F, 1.001F, 7.50F});
    REQUIRE(walk_until(fore_player, -1.0F, [&](const glm::vec3& position) {
        return position.y >= origin.y + 4.0F && position.z >= origin.z + 14.25F;
    }));
    REQUIRE(fore_player.state().on_ground);
    REQUIRE(walk_until(fore_player, 1.0F, [&](const glm::vec3& position) {
        return position.y <= origin.y + 1.05F && position.z <= origin.z + 7.75F;
    }));
    CHECK(fore_player.state().on_ground);
}

TEST_CASE("Amelie lower deck compartments stay connected in both directions") {
    World world(5525, 1, WorldGenerationProfile::OceanAdventure);
    SeaAdventureSystem sea_adventure {};
    sea_adventure.reset(world.seed());
    const auto& ship = sea_adventure.ship_entity();
    const auto origin = ship.world_origin();

    const auto travel_until = [&](PlayerController& player,
                                  const PlayerInput& input,
                                  const auto& reached) {
        for (int frame = 0; frame < 360; ++frame) {
            player.update(input, 1.0F / 60.0F, world, &ship);
            CHECK_FALSE(player.overlaps_dynamic_obstacle(ship));
            CHECK(player.position().y > origin.y + 0.95F);
            CHECK(player.position().y < origin.y + 1.10F);
            CHECK(player.state().on_ground);
            CHECK_FALSE(player.state().head_underwater);
            if (reached(player.position())) {
                return true;
            }
        }
        return false;
    };

    PlayerInput towards_bow {};
    towards_bow.move_forward = -1.0F;
    PlayerInput towards_stern {};
    towards_stern.move_forward = 1.0F;
    PlayerInput towards_starboard {};
    towards_starboard.move_right = 1.0F;
    PlayerInput towards_port {};
    towards_port.move_right = -1.0F;

    // Je passe a cote de l'escalier avant, je rejoins la cale centrale, puis
    // je reproduis exactement le trajet inverse sans escalader ni tomber.
    PlayerController cargo_player(origin + glm::vec3 {1.55F, 1.001F, 9.0F});
    REQUIRE(travel_until(cargo_player, towards_bow, [&](const glm::vec3& position) {
        return position.z >= origin.z + 14.65F;
    }));
    REQUIRE(travel_until(cargo_player, towards_port, [&](const glm::vec3& position) {
        return position.x <= origin.x + 0.05F;
    }));
    REQUIRE(travel_until(cargo_player, towards_bow, [&](const glm::vec3& position) {
        return position.z >= origin.z + 19.0F;
    }));
    REQUIRE(travel_until(cargo_player, towards_stern, [&](const glm::vec3& position) {
        return position.z <= origin.z + 14.70F;
    }));
    REQUIRE(travel_until(cargo_player, towards_starboard, [&](const glm::vec3& position) {
        return position.x >= origin.x + 1.50F;
    }));
    REQUIRE(travel_until(cargo_player, towards_stern, [&](const glm::vec3& position) {
        return position.z <= origin.z + 9.0F;
    }));

    // Je valide aussi le contournement de l'escalier arriere et la porte de
    // cabine, afin que les deux extremites du pont inferieur restent reliees.
    PlayerController cabin_player(origin + glm::vec3 {1.60F, 1.001F, -8.5F});
    REQUIRE(travel_until(cabin_player, towards_stern, [&](const glm::vec3& position) {
        return position.z <= origin.z - 15.65F;
    }));
    REQUIRE(travel_until(cabin_player, towards_port, [&](const glm::vec3& position) {
        return position.x <= origin.x + 0.05F;
    }));
    REQUIRE(travel_until(cabin_player, towards_stern, [&](const glm::vec3& position) {
        return position.z <= origin.z - 22.0F;
    }));
    REQUIRE(travel_until(cabin_player, towards_bow, [&](const glm::vec3& position) {
        return position.z >= origin.z - 15.65F;
    }));
    REQUIRE(travel_until(cabin_player, towards_starboard, [&](const glm::vec3& position) {
        return position.x >= origin.x + 1.55F;
    }));
    REQUIRE(travel_until(cabin_player, towards_bow, [&](const glm::vec3& position) {
        return position.z >= origin.z - 8.5F;
    }));
}

TEST_CASE("Amelie deck equipment exposes stable dynamic support") {
    World world(5524, 1, WorldGenerationProfile::OceanAdventure);
    SeaAdventureSystem sea_adventure {};
    sea_adventure.reset(world.seed());
    const auto& ship = sea_adventure.ship_entity();
    const auto origin = ship.world_origin();
    const auto& parts = amelie_ship_blueprint().parts;
    const auto capstan = std::find_if(parts.begin(), parts.end(), [](const ShipPart& part) {
        return part.material == ShipMaterial::CleanBeam && part.collidable &&
               std::abs(part.local_start.z - 15.0F) < 0.001F &&
               std::abs(part.local_end.z - 16.2F) < 0.001F;
    });
    REQUIRE(capstan != parts.end());
    REQUIRE(capstan->supports_player);

    const auto capstan_top = origin + glm::vec3 {
        (capstan->local_start.x + capstan->local_end.x) * 0.5F,
        capstan->local_end.y + 0.001F,
        (capstan->local_start.z + capstan->local_end.z) * 0.5F,
    };
    PlayerController player(capstan_top);
    for (int frame = 0; frame < 8; ++frame) {
        player.update(PlayerInput {}, 1.0F / 60.0F, world, &ship);
    }
    CHECK(player.state().on_ground);
    CHECK(player.position().y == doctest::Approx(capstan_top.y));
    CHECK_FALSE(player.overlaps_dynamic_obstacle(ship));
}

TEST_CASE("sea adventure reserves save-safe space ahead of Amelie") {
    SeaAdventureSaveState state {};
    state.active = true;
    state.ship_position.z = 1'000'000.0F;
    const auto sanitized = sanitize_sea_adventure_save_state(state);
    const auto& bounds = amelie_ship_blueprint().bounds;
    const auto forward_world_extent = sanitized.ship_position.z - 0.5F + bounds.max.z;

    CHECK(sanitized.ship_position.z < 1'000'000.0F);
    CHECK(forward_world_extent <= 1'000'000.0F);
}

TEST_CASE("sea adventure keeps precise movement at large world coordinates") {
    World world(5526, 1, WorldGenerationProfile::OceanAdventure);
    SeaAdventureSaveState state {};
    state.active = true;
    state.ship_position = {0.5F, 49.0F, 524'288.0F};
    state.route_distance = 524'287.5F;

    SeaAdventureSystem sea_adventure {};
    sea_adventure.load_state(state, world.seed());
    PlayerController player(sea_adventure.deck_spawn_position());
    EnvironmentState environment {};
    const auto initial_position = sea_adventure.ship_position();
    for (int frame = 0; frame < 10; ++frame) {
        const auto result = sea_adventure.update(world, player, environment, 1.0F / 60.0F, false);
        CHECK(result.ship_speed > 0.0F);
    }
    CHECK(sea_adventure.ship_position().z > initial_position.z);
    CHECK(sea_adventure.save_state().route_distance > state.route_distance);

    SeaAdventureSystem stationary {};
    stationary.reset(world.seed());
    PlayerController stationary_player(stationary.deck_spawn_position());
    const auto stationary_result = stationary.update(world, stationary_player, environment, 0.0F, false);
    CHECK(stationary_result.ship_delta == glm::vec3 {});
    CHECK_FALSE(stationary_result.ship_moved_player);
    CHECK(stationary_result.on_ship);

    SeaAdventureSaveState boundary_state {};
    boundary_state.active = true;
    boundary_state.ship_position = {0.5F, 49.0F, 1'000'000.0F};
    SeaAdventureSystem boundary {};
    boundary.load_state(boundary_state, world.seed());
    PlayerController boundary_player(boundary.deck_spawn_position());
    const auto boundary_position = boundary.ship_position();
    const auto boundary_result = boundary.update(world, boundary_player, environment, 0.25F, false);
    CHECK(boundary.ship_position() == boundary_position);
    CHECK(boundary_result.ship_delta == glm::vec3 {});
    CHECK(boundary_result.ship_speed == doctest::Approx(0.0F));
    // La limite de route ne doit pas faire perdre le contact avec le pont ;
    // la houle peut, elle, produire ou non un petit mouvement a cette phase.
    CHECK(boundary_result.on_ship);
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

TEST_CASE("player cannot place a torch inside the player volume") {
    World world(331, 1);
    test::make_chunk_empty(world, {0, 0});
    world.set_block(0, 2, -1, to_block_id(BlockType::Stone));

    PlayerController player({0.5F, 1.001F, 0.5F});
    player.set_selected_block(to_block_id(BlockType::Torch));

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

    const auto hit = player.current_target(world, 8.0F);
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

    PlayerController player({0.5F, 8.001F, -0.5F});
    PlayerInput aim_input {};
    aim_input.look_delta_y = 2000.0F;
    player.update(aim_input, 0.0F, world);

    auto hotbar = make_default_hotbar_state();
    select_hotbar_index(hotbar, 6);
    player.set_selected_block(selected_hotbar_block(hotbar));

    const auto hit = player.current_target(world, 8.0F);
    REQUIRE(hit.hit);
    CHECK(hit.block == BlockCoord {0, 4, -1});
    CHECK(hit.adjacent == BlockCoord {0, 5, -1});

    REQUIRE(player.try_place_block(world, 8.0F));
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

TEST_CASE("held block breaking reuses a target already raycast by the game loop") {
    World world(36112, 1);
    test::make_chunk_empty(world, {0, -1});
    world.set_block(0, 4, -1, to_block_id(BlockType::Stone));

    PlayerController player({0.5F, 5.001F, -0.5F});
    PlayerInput aim_input {};
    aim_input.look_delta_y = 2000.0F;
    player.update(aim_input, 0.0F, world);

    const auto target = player.current_target(world, 4.0F);
    REQUIRE(target.hit);
    const auto completed_break = player.update_block_breaking(
        world,
        block_break_duration_seconds(target.block_id),
        true,
        target);

    REQUIRE(completed_break.has_value());
    CHECK(completed_break->block == target.block);
    CHECK(completed_break->block_id == target.block_id);
    CHECK(world.get_block(target.block.x, target.block.y, target.block.z) == to_block_id(BlockType::Air));
}

TEST_CASE("progression block break speed multiplier shortens held block breaking") {
    World base_world(36110, 1);
    World leveled_world(36111, 1);
    test::make_chunk_empty(base_world, {0, -1});
    test::make_chunk_empty(leveled_world, {0, -1});
    base_world.set_block(0, 4, -1, to_block_id(BlockType::Stone));
    leveled_world.set_block(0, 4, -1, to_block_id(BlockType::Stone));

    PlayerController base_player({0.5F, 5.001F, -0.5F});
    PlayerController leveled_player({0.5F, 5.001F, -0.5F});
    leveled_player.set_block_break_speed_multiplier(1.25F);
    CHECK(leveled_player.block_break_speed_multiplier() == doctest::Approx(1.25F));

    PlayerInput aim_input {};
    aim_input.look_delta_y = 2000.0F;
    base_player.update(aim_input, 0.0F, base_world);
    leveled_player.update(aim_input, 0.0F, leveled_world);

    const auto stone_duration = block_break_duration_seconds(to_block_id(BlockType::Stone));
    const auto leveled_duration = stone_duration / leveled_player.block_break_speed_multiplier();
    REQUIRE(leveled_duration + 0.02F < stone_duration);

    CHECK_FALSE(base_player.update_block_breaking(base_world, leveled_duration + 0.02F, true, 4.0F).has_value());
    REQUIRE(base_player.block_break_progress().active);
    CHECK(base_player.block_break_progress().duration_seconds == doctest::Approx(stone_duration));
    CHECK(base_world.get_block(0, 4, -1) == to_block_id(BlockType::Stone));

    const auto completed_break =
        leveled_player.update_block_breaking(leveled_world, leveled_duration + 0.02F, true, 4.0F);
    REQUIRE(completed_break.has_value());
    CHECK(completed_break->block == BlockCoord {0, 4, -1});
    CHECK(completed_break->block_id == to_block_id(BlockType::Stone));
    CHECK(leveled_world.get_block(0, 4, -1) == to_block_id(BlockType::Air));
}

TEST_CASE("held tool multiplier shortens block breaking on matching target") {
    World world(36112, 1);
    test::make_chunk_empty(world, {0, -1});
    world.set_block(0, 4, -1, to_block_id(BlockType::Stone));

    PlayerController player({0.5F, 5.001F, -0.5F});
    PlayerInput aim_input {};
    aim_input.look_delta_y = 2000.0F;
    player.update(aim_input, 0.0F, world);

    const auto stone_duration = block_break_duration_seconds(to_block_id(BlockType::Stone));
    const auto pickaxe_multiplier =
        tool_break_speed_multiplier(to_block_id(BlockType::Pickaxe), to_block_id(BlockType::Stone));
    const auto pickaxe_duration = stone_duration / pickaxe_multiplier;
    REQUIRE(pickaxe_multiplier == doctest::Approx(1.5F));
    REQUIRE(pickaxe_duration + 0.10F < stone_duration);

    CHECK_FALSE(player.update_block_breaking(world, pickaxe_duration - 0.04F, true, 4.0F, pickaxe_multiplier).has_value());
    REQUIRE(player.block_break_progress().active);
    CHECK(player.block_break_progress().duration_seconds == doctest::Approx(pickaxe_duration));
    CHECK(world.get_block(0, 4, -1) == to_block_id(BlockType::Stone));

    const auto completed_break = player.update_block_breaking(world, 0.04F, true, 4.0F, pickaxe_multiplier);
    REQUIRE(completed_break.has_value());
    CHECK(completed_break->block == BlockCoord {0, 4, -1});
    CHECK(completed_break->block_id == to_block_id(BlockType::Stone));
    CHECK(world.get_block(0, 4, -1) == to_block_id(BlockType::Air));
}

TEST_CASE("resource ores stay breakable by hand while pickaxe accelerates each ore") {
    const std::array<BlockType, 5> ore_types {{
        BlockType::CoalOre,
        BlockType::IronOre,
        BlockType::GoldOre,
        BlockType::DiamondOre,
        BlockType::MetallicAlloyOre,
    }};
    PlayerInput aim_input {};
    aim_input.look_delta_y = 2000.0F;

    for (std::size_t index = 0; index < ore_types.size(); ++index) {
        const auto ore_id = to_block_id(ore_types[index]);
        CAPTURE(static_cast<int>(ore_types[index]));

        World hand_world(36113 + static_cast<int>(index * 2U), 1);
        test::make_chunk_empty(hand_world, {0, -1});
        hand_world.set_block(0, 4, -1, ore_id);
        PlayerController hand_player({0.5F, 5.001F, -0.5F});
        hand_player.update(aim_input, 0.0F, hand_world);

        const auto ore_duration = block_break_duration_seconds(ore_id);
        REQUIRE(ore_duration > 0.0F);
        const auto hand_break = hand_player.update_block_breaking(hand_world, ore_duration + 0.02F, true, 4.0F);
        REQUIRE(hand_break.has_value());
        CHECK(hand_break->block == BlockCoord {0, 4, -1});
        CHECK(hand_break->block_id == ore_id);
        CHECK(hand_world.get_block(0, 4, -1) == to_block_id(BlockType::Air));

        World pickaxe_world(36114 + static_cast<int>(index * 2U), 1);
        test::make_chunk_empty(pickaxe_world, {0, -1});
        pickaxe_world.set_block(0, 4, -1, ore_id);
        PlayerController pickaxe_player({0.5F, 5.001F, -0.5F});
        pickaxe_player.update(aim_input, 0.0F, pickaxe_world);

        const auto pickaxe_multiplier = tool_break_speed_multiplier(to_block_id(BlockType::Pickaxe), ore_id);
        const auto pickaxe_duration = ore_duration / pickaxe_multiplier;
        REQUIRE(pickaxe_multiplier > 1.0F);
        REQUIRE(pickaxe_duration + 0.10F < ore_duration);

        CHECK_FALSE(
            pickaxe_player
                .update_block_breaking(pickaxe_world, pickaxe_duration - 0.04F, true, 4.0F, pickaxe_multiplier)
                .has_value());
        REQUIRE(pickaxe_player.block_break_progress().active);
        CHECK(pickaxe_player.block_break_progress().duration_seconds == doctest::Approx(pickaxe_duration));
        CHECK(pickaxe_world.get_block(0, 4, -1) == ore_id);

        const auto pickaxe_break =
            pickaxe_player.update_block_breaking(pickaxe_world, 0.04F, true, 4.0F, pickaxe_multiplier);
        REQUIRE(pickaxe_break.has_value());
        CHECK(pickaxe_break->block == BlockCoord {0, 4, -1});
        CHECK(pickaxe_break->block_id == ore_id);
        CHECK(pickaxe_world.get_block(0, 4, -1) == to_block_id(BlockType::Air));
    }
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

TEST_CASE("item drops stay grounded on the moving sea adventure deck") {
    World world(365, 1, WorldGenerationProfile::OceanAdventure);
    SeaAdventureSystem sea_adventure {};
    sea_adventure.reset(world.seed());
    place_sea_adventure_underway(sea_adventure, world.seed());
    EnvironmentState environment {};
    PlayerController off_ship_player(sea_adventure.ship_position() + glm::vec3 {40.0F, 4.10F, 0.0F});

    ItemDropSystem drop_system {};
    HotbarState hotbar {};
    InventoryMenuState inventory {};
    const auto initial_drop_position =
        sea_adventure.ship_entity().world_origin() + glm::vec3 {2.5F, 4.001F, -8.5F};
    drop_system.spawn_drop(
        inventory_make_slot(to_block_id(BlockType::Wood), 1),
        initial_drop_position,
        {});

    const auto initial_local =
        sea_adventure.ship_entity().world_to_local_point(
            initial_drop_position);
    constexpr float kStep = 1.0F / 60.0F;
    for (int frame = 0; frame < 60; ++frame) {
        (void)sea_adventure.update(
            world,
            off_ship_player,
            environment,
            kStep,
            false);
        drop_system.update(
            kStep,
            world,
            {1000.0F, 80.0F, 1000.0F},
            inventory,
            hotbar,
            &sea_adventure.ship_entity());
    }

    REQUIRE(drop_system.active_drop_count() == 1U);
    const auto& drop = drop_system.drops().front();
    const auto& ship = sea_adventure.ship_entity();
    const auto expected_position =
        ship.local_to_world_point(
            initial_local);
    const auto support_height =
        ship.support_height(
            drop.position);
    REQUIRE(support_height.has_value());
    CHECK(drop.grounded);
    CHECK_FALSE(drop.sleeping);
    CHECK(drop.position.y == doctest::Approx(*support_height + 0.001F));
    // Le recalage de support est vertical : la projection X/Z doit conserver
    // exactement le point local du pont, meme en tangage et en roulis.
    CHECK(drop.position.x ==
          doctest::Approx(expected_position.x).epsilon(0.001F));
    CHECK(drop.position.z ==
          doctest::Approx(expected_position.z).epsilon(0.001F));
}

TEST_CASE("settled item drops sleep and skip steady state physics") {
    World world(366, 1);
    test::make_chunk_empty(world, {0, 0});
    test::make_flat_floor(world, -2, 2, 0, -2, 2);

    ItemDropSystem drop_system {};
    HotbarState hotbar {};
    InventoryMenuState inventory {};
    drop_system.spawn_drop(
        inventory_make_slot(to_block_id(BlockType::Wood), 1),
        {0.5F, 1.08F, 0.5F},
        {0.0F, 0.0F, 0.0F});

    constexpr float kStep = 1.0F / 60.0F;
    for (int frame = 0; frame < 180; ++frame) {
        drop_system.update(kStep, world, {100.0F, 80.0F, 100.0F}, inventory, hotbar);
    }
    REQUIRE(drop_system.active_drop_count() == 1);
    REQUIRE(drop_system.drops().front().sleeping);
    (void)drop_system.consume_audit_stats();

    for (int frame = 0; frame < 120; ++frame) {
        drop_system.update(kStep, world, {100.0F, 80.0F, 100.0F}, inventory, hotbar);
    }
    const auto steady_stats = drop_system.consume_audit_stats();
    CHECK(steady_stats.physics_updates == 0U);
    CHECK(steady_stats.support_checks <= 5U);
    CHECK(steady_stats.sleeping_drops == 1U);
    CHECK(drop_system.drops().front().sleeping);
}

TEST_CASE("sleeping item drops wake for magnetism and removed support") {
    constexpr float kStep = 1.0F / 60.0F;

    SUBCASE("magnetism wakes immediately") {
        World world(367, 1);
        test::make_chunk_empty(world, {0, 0});
        test::make_flat_floor(world, -2, 2, 0, -2, 2);
        ItemDropSystem drop_system {};
        HotbarState hotbar {};
        InventoryMenuState inventory {};
        drop_system.spawn_drop(
            inventory_make_slot(to_block_id(BlockType::Wood), 1),
            {0.5F, 1.08F, 0.5F},
            {0.0F, 0.0F, 0.0F});
        for (int frame = 0; frame < 180; ++frame) {
            drop_system.update(kStep, world, {100.0F, 80.0F, 100.0F}, inventory, hotbar);
        }
        REQUIRE(drop_system.drops().front().sleeping);
        (void)drop_system.consume_audit_stats();

        const auto drop_position = drop_system.drops().front().position;
        drop_system.update(kStep, world, drop_position + glm::vec3 {2.0F, 0.0F, 0.0F}, inventory, hotbar);

        REQUIRE(drop_system.active_drop_count() == 1);
        CHECK_FALSE(drop_system.drops().front().sleeping);
        CHECK(drop_system.consume_audit_stats().woken_drops == 1U);
    }

    SUBCASE("support removal wakes on the bounded validation tick") {
        World world(368, 1);
        test::make_chunk_empty(world, {0, 0});
        test::make_flat_floor(world, -2, 2, 0, -2, 2);
        ItemDropSystem drop_system {};
        HotbarState hotbar {};
        InventoryMenuState inventory {};
        drop_system.spawn_drop(
            inventory_make_slot(to_block_id(BlockType::Wood), 1),
            {0.5F, 1.08F, 0.5F},
            {0.0F, 0.0F, 0.0F});
        for (int frame = 0; frame < 180; ++frame) {
            drop_system.update(kStep, world, {100.0F, 80.0F, 100.0F}, inventory, hotbar);
        }
        REQUIRE(drop_system.drops().front().sleeping);
        (void)drop_system.consume_audit_stats();

        world.set_block(0, 0, 0, to_block_id(BlockType::Air));
        for (int frame = 0; frame < 40; ++frame) {
            drop_system.update(kStep, world, {100.0F, 80.0F, 100.0F}, inventory, hotbar);
        }

        const auto wake_stats = drop_system.consume_audit_stats();
        REQUIRE(drop_system.active_drop_count() == 1);
        CHECK_FALSE(drop_system.drops().front().sleeping);
        CHECK(wake_stats.support_checks >= 1U);
        CHECK(wake_stats.woken_drops == 1U);
        CHECK(wake_stats.physics_updates >= 1U);
    }
}

TEST_CASE("item drops sanitize corrupted loaded state") {
    constexpr auto nan = std::numeric_limits<float>::quiet_NaN();
    constexpr auto infinity = std::numeric_limits<float>::infinity();

    ItemDrop valid_drop {};
    valid_drop.position = {0.5F, 2.0F, 0.5F};
    valid_drop.velocity = {infinity, nan, -infinity};
    valid_drop.stack = inventory_make_slot(to_block_id(BlockType::Stone), 96);
    valid_drop.age_seconds = nan;
    valid_drop.pickup_cooldown = -infinity;

    ItemDrop invalid_position = valid_drop;
    invalid_position.position = {nan, 2.0F, 0.5F};

    ItemDrop invalid_stack = valid_drop;
    invalid_stack.stack = inventory_make_slot(static_cast<BlockId>(255U), 5);

    ItemDrop extreme_time = valid_drop;
    extreme_time.position.x = 1.5F;
    extreme_time.age_seconds = (std::numeric_limits<float>::max)();
    extreme_time.pickup_cooldown = (std::numeric_limits<float>::max)();

    ItemDropSystem drop_system {};
    drop_system.load_drops({valid_drop, extreme_time, invalid_position, invalid_stack});

    REQUIRE(drop_system.active_drop_count() == 2);
    const auto& loaded = drop_system.drops().front();
    CHECK(loaded.stack.block_id == to_block_id(BlockType::Stone));
    CHECK(loaded.stack.count == kMaxItemStackCount);
    CHECK(loaded.velocity.x == doctest::Approx(0.0F));
    CHECK(loaded.velocity.y == doctest::Approx(0.0F));
    CHECK(loaded.velocity.z == doctest::Approx(0.0F));
    CHECK(loaded.age_seconds == doctest::Approx(0.0F));
    CHECK(loaded.pickup_cooldown == doctest::Approx(0.0F));
    const auto& loaded_extreme = drop_system.drops()[1];
    CHECK(std::isfinite(loaded_extreme.age_seconds));
    CHECK(loaded_extreme.age_seconds >= 0.0F);
    CHECK(loaded_extreme.age_seconds < 63.0F);
    CHECK(loaded_extreme.pickup_cooldown == doctest::Approx(1.0F));

    World world(365, 1);
    test::make_chunk_empty(world, {0, 0});
    std::vector<ItemDropRenderInstance> render_instances {};
    drop_system.build_render_instances(world, render_instances);
    REQUIRE(render_instances.size() == 2);
    for (const auto& instance : render_instances) {
        CHECK(std::isfinite(instance.position.x));
        CHECK(std::isfinite(instance.position.y));
        CHECK(std::isfinite(instance.position.z));
        CHECK(std::isfinite(instance.age_seconds));
        CHECK(std::isfinite(instance.spin_radians));
    }
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
    CHECK(player.damage_resistance_percent() == doctest::Approx(99.0F));
    player.apply_external_damage(10.0F, PlayerDeathCause::Zombie);
    CHECK(player.state().health == doctest::Approx(player.max_health() - 6.1F).epsilon(0.001F));
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
