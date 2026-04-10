#include "creatures/CreatureGeometry.h"
#include "creatures/CreatureSystem.h"

#include "TestUtils.h"

#include <doctest/doctest.h>
#include <glm/geometric.hpp>
#include <glm/vec2.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace valcraft {

namespace {

void add_tree_patch(World& world, int base_x, int base_z, int trunk_y) {
    for (int y = 1; y <= 3; ++y) {
        world.set_block(base_x, trunk_y + y, base_z, to_block_id(BlockType::Wood));
    }

    for (int dz = -1; dz <= 1; ++dz) {
        for (int dx = -1; dx <= 1; ++dx) {
            world.set_block(base_x + dx, trunk_y + 4, base_z + dz, to_block_id(BlockType::Leaves));
        }
    }
}

void sculpt_sheep_hills(World& world, const ChunkCoord& coord, int base_height) {
    const auto origin_x = coord.x * kChunkSizeX;
    const auto origin_z = coord.z * kChunkSizeZ;
    for (int local_z = 0; local_z < kChunkSizeZ; ++local_z) {
        for (int local_x = 0; local_x < kChunkSizeX; ++local_x) {
            const auto world_x = origin_x + local_x;
            const auto world_z = origin_z + local_z;
            const auto extra_layers = ((local_x + local_z) % 3 == 0) ? 2 : (((local_x * 2 + local_z) % 5 == 0) ? 1 : 0);
            for (int layer = 1; layer <= extra_layers; ++layer) {
                world.set_block(world_x, base_height + layer, world_z, to_block_id(BlockType::Grass));
            }
        }
    }
}

auto is_hostile_state(CreatureBehaviorState state) -> bool {
    return state == CreatureBehaviorState::Chase || state == CreatureBehaviorState::Strike;
}

auto horizontal_distance_squared(const glm::vec3& lhs, const glm::vec3& rhs) -> float {
    const auto dx = lhs.x - rhs.x;
    const auto dz = lhs.z - rhs.z;
    return dx * dx + dz * dz;
}

auto yaw_direction(float yaw_radians) -> glm::vec2 {
    return {std::cos(yaw_radians), std::sin(yaw_radians)};
}

auto make_test_creature(const CreatureSpawnAnchor& anchor, const glm::vec3& position) -> CreatureInstance {
    CreatureInstance creature {};
    creature.anchor = anchor;
    creature.position = position;
    creature.yaw_radians = 0.0F;
    creature.wander_heading = 0.0F;
    creature.behavior_seed = 42U;
    creature.appearance_seed = 7U;
    creature.attack_cooldown = 0.0F;
    return creature;
}

auto tile_average_rgba(const std::vector<std::uint8_t>& atlas, CreatureAtlasTile tile) -> std::array<float, 4> {
    const auto coordinates = creature_atlas_tile_coordinates(tile);
    const auto start_x = coordinates[0] * kCreatureAtlasTileSize;
    const auto start_y = coordinates[1] * kCreatureAtlasTileSize;

    std::array<float, 4> accum {{0.0F, 0.0F, 0.0F, 0.0F}};
    for (int y = 0; y < kCreatureAtlasTileSize; ++y) {
        for (int x = 0; x < kCreatureAtlasTileSize; ++x) {
            const auto index = static_cast<std::size_t>(((start_y + y) * kCreatureAtlasSize + (start_x + x)) * 4);
            accum[0] += static_cast<float>(atlas[index + 0]);
            accum[1] += static_cast<float>(atlas[index + 1]);
            accum[2] += static_cast<float>(atlas[index + 2]);
            accum[3] += static_cast<float>(atlas[index + 3]);
        }
    }

    const auto texel_count = static_cast<float>(kCreatureAtlasTileSize * kCreatureAtlasTileSize);
    for (auto& channel : accum) {
        channel /= texel_count;
    }
    return accum;
}

auto tile_alpha_coverage(const std::vector<std::uint8_t>& atlas, CreatureAtlasTile tile, std::uint8_t threshold = 1) -> float {
    const auto coordinates = creature_atlas_tile_coordinates(tile);
    const auto start_x = coordinates[0] * kCreatureAtlasTileSize;
    const auto start_y = coordinates[1] * kCreatureAtlasTileSize;

    int alpha_pixels = 0;
    for (int y = 0; y < kCreatureAtlasTileSize; ++y) {
        for (int x = 0; x < kCreatureAtlasTileSize; ++x) {
            const auto index = static_cast<std::size_t>(((start_y + y) * kCreatureAtlasSize + (start_x + x)) * 4);
            alpha_pixels += atlas[index + 3] >= threshold ? 1 : 0;
        }
    }

    const auto texel_count = static_cast<float>(kCreatureAtlasTileSize * kCreatureAtlasTileSize);
    return static_cast<float>(alpha_pixels) / texel_count;
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

auto max_position_delta(const CreatureMeshData& lhs, const CreatureMeshData& rhs) -> float {
    if (lhs.vertices.size() != rhs.vertices.size()) {
        return std::numeric_limits<float>::max();
    }
    float max_delta = 0.0F;
    for (std::size_t index = 0; index < lhs.vertices.size(); ++index) {
        const auto& a = lhs.vertices[index];
        const auto& b = rhs.vertices[index];
        max_delta = std::max(max_delta, std::abs(a.x - b.x));
        max_delta = std::max(max_delta, std::abs(a.y - b.y));
        max_delta = std::max(max_delta, std::abs(a.z - b.z));
    }
    return max_delta;
}

auto all_vertex_attributes_are_bounded(const CreatureMeshData& mesh) -> bool {
    return std::all_of(mesh.vertices.begin(), mesh.vertices.end(), [](const CreatureVertex& vertex) {
        return vertex.material_class >= 0.0F &&
               vertex.material_class <= 1.0F &&
               vertex.cavity_mask >= 0.0F &&
               vertex.cavity_mask <= 1.0F &&
               vertex.emissive_strength >= 0.0F &&
               vertex.emissive_strength <= 1.0F;
    });
}

auto max_material_class(const CreatureMeshData& mesh) -> float {
    float maximum = 0.0F;
    for (const auto& vertex : mesh.vertices) {
        maximum = std::max(maximum, vertex.material_class);
    }
    return maximum;
}

auto has_emissive_vertices(const CreatureMeshData& mesh) -> bool {
    return std::any_of(mesh.vertices.begin(), mesh.vertices.end(), [](const CreatureVertex& vertex) {
        return vertex.emissive_strength > 0.5F;
    });
}

auto body_volume_proxy(const CreatureMeshData& mesh) -> float {
    MeshBounds bounds {
        glm::vec3 {std::numeric_limits<float>::max()},
        glm::vec3 {std::numeric_limits<float>::lowest()},
    };
    bool found = false;
    for (const auto& vertex : mesh.vertices) {
        if (vertex.y < 0.20F || vertex.y > 1.10F) {
            continue;
        }
        found = true;
        bounds.min.x = std::min(bounds.min.x, vertex.x);
        bounds.min.y = std::min(bounds.min.y, vertex.y);
        bounds.min.z = std::min(bounds.min.z, vertex.z);
        bounds.max.x = std::max(bounds.max.x, vertex.x);
        bounds.max.y = std::max(bounds.max.y, vertex.y);
        bounds.max.z = std::max(bounds.max.z, vertex.z);
    }

    if (!found) {
        return 0.0F;
    }

    return (bounds.max.x - bounds.min.x) * (bounds.max.y - bounds.min.y) * (bounds.max.z - bounds.min.z);
}

auto band_volume_proxy(const CreatureMeshData& mesh, float min_y, float max_y) -> float {
    MeshBounds bounds {
        glm::vec3 {std::numeric_limits<float>::max()},
        glm::vec3 {std::numeric_limits<float>::lowest()},
    };
    bool found = false;
    for (const auto& vertex : mesh.vertices) {
        if (vertex.y < min_y || vertex.y > max_y) {
            continue;
        }
        found = true;
        bounds.min.x = std::min(bounds.min.x, vertex.x);
        bounds.min.y = std::min(bounds.min.y, vertex.y);
        bounds.min.z = std::min(bounds.min.z, vertex.z);
        bounds.max.x = std::max(bounds.max.x, vertex.x);
        bounds.max.y = std::max(bounds.max.y, vertex.y);
        bounds.max.z = std::max(bounds.max.z, vertex.z);
    }

    if (!found) {
        return 0.0F;
    }

    return (bounds.max.x - bounds.min.x) * (bounds.max.y - bounds.min.y) * (bounds.max.z - bounds.min.z);
}

auto band_depth_span(const CreatureMeshData& mesh, float min_y, float max_y) -> float {
    float min_z = std::numeric_limits<float>::max();
    float max_z = std::numeric_limits<float>::lowest();
    bool found = false;
    for (const auto& vertex : mesh.vertices) {
        if (vertex.y < min_y || vertex.y > max_y) {
            continue;
        }
        found = true;
        min_z = std::min(min_z, vertex.z);
        max_z = std::max(max_z, vertex.z);
    }

    return found ? (max_z - min_z) : 0.0F;
}

} // namespace

TEST_CASE("creature cycle classification uses explicit dusk night and dawn boundaries") {
    const auto dusk_start = EnvironmentClock::classify_creature_cycle(18.0F);
    const auto dusk_mid = EnvironmentClock::classify_creature_cycle(18.5F);
    const auto night_start = EnvironmentClock::classify_creature_cycle(19.0F);
    const auto night_end = EnvironmentClock::classify_creature_cycle(4.99F);
    const auto dawn_start = EnvironmentClock::classify_creature_cycle(5.0F);
    const auto dawn_mid = EnvironmentClock::classify_creature_cycle(5.5F);
    const auto day = EnvironmentClock::classify_creature_cycle(6.0F);

    CHECK(dusk_start.phase == CreaturePhase::DuskMorph);
    CHECK(dusk_start.morph_factor == doctest::Approx(0.0F));
    CHECK(dusk_mid.phase == CreaturePhase::DuskMorph);
    CHECK(dusk_mid.morph_factor == doctest::Approx(0.5F));
    CHECK(night_start.phase == CreaturePhase::Night);
    CHECK(night_start.morph_factor == doctest::Approx(1.0F));
    CHECK(night_end.phase == CreaturePhase::Night);
    CHECK(dawn_start.phase == CreaturePhase::DawnRecover);
    CHECK(dawn_start.morph_factor == doctest::Approx(1.0F));
    CHECK(dawn_mid.phase == CreaturePhase::DawnRecover);
    CHECK(dawn_mid.morph_factor == doctest::Approx(0.5F));
    CHECK(day.phase == CreaturePhase::Day);
    CHECK(day.morph_factor == doctest::Approx(0.0F));
}

TEST_CASE("creature spawn anchors map grass chunks to pig cow sheep and reject desert chunks") {
    CreatureSystem system {};
    World world(7001, 2);

    test::make_chunk_surface(world, {0, 0}, 12, to_block_id(BlockType::Grass), to_block_id(BlockType::Dirt));
    test::make_chunk_surface(world, {1, 0}, 13, to_block_id(BlockType::Grass), to_block_id(BlockType::Dirt));
    test::make_chunk_surface(world, {2, 0}, 52, to_block_id(BlockType::Grass), to_block_id(BlockType::Dirt));
    test::make_chunk_surface(world, {3, 0}, 12, to_block_id(BlockType::Sand), to_block_id(BlockType::Sand));

    for (const auto x : {18, 21, 24, 27}) {
        add_tree_patch(world, x, 6 + (x % 3), 13);
    }
    sculpt_sheep_hills(world, {2, 0}, 52);

    const auto cow_anchor = system.spawn_anchor_for_chunk(world, {0, 0});
    const auto pig_anchor = system.spawn_anchor_for_chunk(world, {1, 0});
    const auto sheep_anchor = system.spawn_anchor_for_chunk(world, {2, 0});
    const auto desert_anchor = system.spawn_anchor_for_chunk(world, {3, 0});

    REQUIRE(cow_anchor.has_value());
    REQUIRE(pig_anchor.has_value());
    REQUIRE(sheep_anchor.has_value());
    CHECK(cow_anchor->species == CreatureSpecies::Cow);
    CHECK(pig_anchor->species == CreatureSpecies::Pig);
    CHECK(sheep_anchor->species == CreatureSpecies::Sheep);
    CHECK_FALSE(desert_anchor.has_value());
}

TEST_CASE("day creatures stay passive grounded and emit no attack events") {
    CreatureSystem system {};
    World world(9001, 1);
    test::make_chunk_surface(world, {0, 0}, 12, to_block_id(BlockType::Grass), to_block_id(BlockType::Dirt));

    const auto environment = EnvironmentClock::compute_state(12.0F);
    const auto cycle = EnvironmentClock::classify_creature_cycle(12.0F);
    const glm::vec3 player_position {2.5F, 13.001F, 2.5F};

    for (int frame = 0; frame < 240; ++frame) {
        system.update(1.0F / 60.0F, world, player_position, environment, cycle);
    }

    const auto creatures = system.active_creatures();
    REQUIRE(creatures.size() == 1);
    CHECK(creatures.front().phase == CreaturePhase::Day);
    CHECK(creatures.front().morph_factor == doctest::Approx(0.0F));
    CHECK_FALSE(is_hostile_state(creatures.front().behavior_state));
    CHECK(system.recent_attacks().empty());
    CHECK(creatures.front().position.y == doctest::Approx(13.001F).epsilon(0.01F));
    CHECK(horizontal_distance_squared(creatures.front().position, creatures.front().anchor.spawn_position) < 26.5F);
}

TEST_CASE("settlement residents can share a chunk and stay passive through the night") {
    CreatureSystem system {};
    World world(90101, 1);
    test::make_chunk_surface(world, {0, 0}, 12, to_block_id(BlockType::Grass), to_block_id(BlockType::Dirt));

    system.set_settlement_residents({
        {
            {0, 0},
            {2, 12, 2},
            {2.5F, 13.001F, 2.5F},
            CreatureSpecies::Villager,
        },
        {
            {0, 0},
            {10, 12, 10},
            {10.5F, 13.001F, 10.5F},
            CreatureSpecies::Villager,
        },
    });

    const auto environment = EnvironmentClock::compute_state(23.0F);
    const auto cycle = EnvironmentClock::classify_creature_cycle(23.0F);
    const glm::vec3 player_position {7.5F, 13.001F, 7.5F};

    for (int frame = 0; frame < 180; ++frame) {
        system.update(1.0F / 60.0F, world, player_position, environment, cycle);
    }

    const auto creatures = system.active_creatures();
    REQUIRE(creatures.size() == 2);
    CHECK(std::all_of(creatures.begin(), creatures.end(), [](const CreatureInstance& creature) {
        return creature.anchor.species == CreatureSpecies::Villager;
    }));
    CHECK(std::all_of(creatures.begin(), creatures.end(), [](const CreatureInstance& creature) {
        return creature.phase == CreaturePhase::Day;
    }));
    CHECK(std::all_of(creatures.begin(), creatures.end(), [](const CreatureInstance& creature) {
        return !is_hostile_state(creature.behavior_state);
    }));
    CHECK(system.recent_attacks().empty());
    CHECK(std::all_of(creatures.begin(), creatures.end(), [](const CreatureInstance& creature) {
        return horizontal_distance_squared(creature.position, creature.anchor.spawn_position) < 52.0F;
    }));
}

TEST_CASE("settlement residents stay anchored to the village floor even under a roof overhang") {
    CreatureSystem system {};
    World world(90102, 1);
    test::make_chunk_surface(world, {0, 0}, 12, to_block_id(BlockType::Grass), to_block_id(BlockType::Dirt));
    world.set_block(2, 16, 2, to_block_id(BlockType::Cobblestone));

    system.set_settlement_residents({
        {
            {0, 0},
            {2, 12, 2},
            {2.5F, 13.001F, 2.5F},
            CreatureSpecies::Villager,
        },
    });

    const auto environment = EnvironmentClock::compute_state(14.0F);
    const auto cycle = EnvironmentClock::classify_creature_cycle(14.0F);
    const glm::vec3 player_position {24.0F, 13.001F, 24.0F};

    for (int frame = 0; frame < 120; ++frame) {
        system.update(1.0F / 60.0F, world, player_position, environment, cycle);
    }

    const auto creatures = system.active_creatures();
    REQUIRE(creatures.size() == 1);
    const auto& villager = creatures.front();
    CHECK(villager.anchor.species == CreatureSpecies::Villager);
    CHECK(villager.position.y == doctest::Approx(13.001F).epsilon(0.01F));
    CHECK(villager.position.y < 15.0F);
    CHECK(horizontal_distance_squared(villager.position, villager.anchor.spawn_position) > 0.25F);
    CHECK(horizontal_distance_squared(villager.position, villager.anchor.spawn_position) < 30.0F);
}

TEST_CASE("settlement residents use village patrol anchors to live across the settlement") {
    CreatureSystem system {};
    World world(90103, 2);
    test::make_chunk_surface(world, {0, 0}, 12, to_block_id(BlockType::Grass), to_block_id(BlockType::Dirt));

    CreatureSpawnAnchor resident {};
    resident.chunk = {0, 0};
    resident.ground_block = {2, 12, 2};
    resident.spawn_position = {2.5F, 13.001F, 2.5F};
    resident.species = CreatureSpecies::Villager;
    resident.roam_radius = 22.0F;
    resident.patrol_points[0] = resident.spawn_position;
    resident.patrol_points[1] = {4.5F, 13.001F, 4.5F};
    resident.patrol_points[2] = {12.5F, 13.001F, 2.5F};
    resident.patrol_points[3] = {12.5F, 13.001F, 12.5F};
    resident.patrol_point_count = static_cast<std::uint8_t>(resident.patrol_points.size());

    system.set_settlement_residents({resident});

    const auto environment = EnvironmentClock::compute_state(13.0F);
    const auto cycle = EnvironmentClock::classify_creature_cycle(13.0F);
    const glm::vec3 player_position {28.0F, 13.001F, 28.0F};
    float max_distance_squared = 0.0F;

    for (int frame = 0; frame < 600; ++frame) {
        system.update(1.0F / 60.0F, world, player_position, environment, cycle);

        const auto live_creatures = system.active_creatures();
        const auto live_villager_it = std::find_if(live_creatures.begin(), live_creatures.end(), [](const CreatureInstance& creature) {
            return creature.anchor.species == CreatureSpecies::Villager;
        });
        REQUIRE(live_villager_it != live_creatures.end());
        max_distance_squared = std::max(max_distance_squared, horizontal_distance_squared(live_villager_it->position, resident.spawn_position));
    }

    const auto creatures = system.active_creatures();
    const auto villager_it = std::find_if(creatures.begin(), creatures.end(), [](const CreatureInstance& creature) {
        return creature.anchor.species == CreatureSpecies::Villager;
    });
    REQUIRE(villager_it != creatures.end());
    const auto villager_count = std::count_if(creatures.begin(), creatures.end(), [](const CreatureInstance& creature) {
        return creature.anchor.species == CreatureSpecies::Villager;
    });
    CHECK(villager_count == 1);
    const auto& villager = *villager_it;
    CHECK(villager.anchor.species == CreatureSpecies::Villager);
    CHECK(villager.position.y == doctest::Approx(13.001F).epsilon(0.01F));
    CHECK(max_distance_squared > 16.0F);
    CHECK(horizontal_distance_squared(villager.position, resident.spawn_position) < resident.roam_radius * resident.roam_radius);
}

TEST_CASE("creature locomotion keeps body yaw aligned with realised travel direction") {
    CreatureSystem system {};
    World world(90011, 1);
    test::make_chunk_surface(world, {0, 0}, 12, to_block_id(BlockType::Grass), to_block_id(BlockType::Dirt));

    const auto environment = EnvironmentClock::compute_state(12.0F);
    const auto cycle = EnvironmentClock::classify_creature_cycle(12.0F);
    const auto anchor = system.spawn_anchor_for_chunk(world, {0, 0});
    REQUIRE(anchor.has_value());

    auto creature = make_test_creature(*anchor, anchor->spawn_position);
    creature.behavior_state = CreatureBehaviorState::Wander;
    creature.behavior_timer = 0.75F;
    creature.yaw_radians = 0.0F;
    creature.wander_heading = std::atan2(1.0F, 0.0F);
    system.load_creatures({creature}, environment);

    const auto before = system.active_creatures().front().position;
    system.update(0.25F, world, before + glm::vec3 {24.0F, 0.0F, 24.0F}, environment, cycle);

    REQUIRE(system.active_creatures().size() == 1);
    const auto& updated = system.active_creatures().front();
    const glm::vec2 displacement {
        updated.position.x - before.x,
        updated.position.z - before.z,
    };
    REQUIRE(glm::dot(displacement, displacement) > 1.0e-4F);

    const auto travel_direction = glm::normalize(displacement);
    const auto facing_direction = yaw_direction(updated.yaw_radians);
    CHECK(glm::dot(travel_direction, facing_direction) > 0.97F);
}

TEST_CASE("creatures enter chase exactly at 19 and stop attacking immediately at dawn") {
    CreatureSystem system {};
    World world(9002, 1);
    test::make_chunk_surface(world, {0, 0}, 12, to_block_id(BlockType::Grass), to_block_id(BlockType::Dirt));

    const auto night_environment = EnvironmentClock::compute_state(19.0F);
    const auto night_cycle = EnvironmentClock::classify_creature_cycle(19.0F);
    system.update(0.0F, world, {14.5F, 13.001F, 14.5F}, night_environment, night_cycle);
    REQUIRE(system.active_creatures().size() == 1);

    const auto spawn_position = system.active_creatures().front().position;
    const auto chase_player_position = spawn_position + glm::vec3 {2.4F, 0.0F, 0.0F};
    system.update(1.0F / 60.0F, world, chase_player_position, night_environment, night_cycle);

    REQUIRE(system.active_creatures().size() == 1);
    CHECK(system.active_creatures().front().phase == CreaturePhase::Night);
    CHECK(system.active_creatures().front().behavior_state == CreatureBehaviorState::Chase);
    CHECK(system.render_instances().front().attack_amount > 0.15F);

    const auto strike_player_position = spawn_position + glm::vec3 {0.75F, 0.0F, 0.0F};
    bool attacked = false;
    for (int frame = 0; frame < 90; ++frame) {
        system.update(1.0F / 60.0F, world, strike_player_position, night_environment, night_cycle);
        if (!system.recent_attacks().empty()) {
            attacked = true;
            break;
        }
    }
    REQUIRE(attacked);
    CHECK(system.active_creatures().front().behavior_state == CreatureBehaviorState::Strike);

    const auto dawn_environment = EnvironmentClock::compute_state(5.0F);
    const auto dawn_cycle = EnvironmentClock::classify_creature_cycle(5.0F);
    system.update(1.0F / 60.0F, world, strike_player_position, dawn_environment, dawn_cycle);

    REQUIRE(system.active_creatures().size() == 1);
    CHECK(system.active_creatures().front().phase == CreaturePhase::DawnRecover);
    CHECK_FALSE(is_hostile_state(system.active_creatures().front().behavior_state));
    CHECK(system.recent_attacks().empty());
    CHECK(system.render_instances().front().attack_amount < 0.45F);
}

TEST_CASE("night chase keeps pressure after the player briefly leaves detection range") {
    CreatureSystem system {};
    World world(90021, 1);
    test::make_chunk_surface(world, {0, 0}, 12, to_block_id(BlockType::Grass), to_block_id(BlockType::Dirt));

    const auto environment = EnvironmentClock::compute_state(23.0F);
    const auto cycle = EnvironmentClock::classify_creature_cycle(23.0F);
    const auto anchor = system.spawn_anchor_for_chunk(world, {0, 0});
    REQUIRE(anchor.has_value());

    auto creature = make_test_creature(*anchor, anchor->spawn_position);
    creature.yaw_radians = std::atan2(1.0F, 0.0F);
    system.load_creatures({creature}, environment);

    const auto visible_player = anchor->spawn_position + glm::vec3 {4.0F, 0.0F, 0.0F};
    system.update(1.0F / 60.0F, world, visible_player, environment, cycle);
    REQUIRE(system.active_creatures().size() == 1);
    REQUIRE(system.active_creatures().front().behavior_state == CreatureBehaviorState::Chase);

    const auto chase_position = system.active_creatures().front().position;
    const auto far_player = anchor->spawn_position + glm::vec3 {40.0F, 0.0F, 0.0F};
    for (int frame = 0; frame < 20; ++frame) {
        system.update(1.0F / 60.0F, world, far_player, environment, cycle);
    }

    REQUIRE(system.active_creatures().size() == 1);
    CHECK(system.active_creatures().front().behavior_state == CreatureBehaviorState::Chase);
    CHECK(horizontal_distance_squared(system.active_creatures().front().position, chase_position) > 0.01F);
}

TEST_CASE("night melee attacks emit stable zombie damage and aggressive render signals") {
    CreatureSystem system {};
    World world(9003, 1);
    test::make_chunk_surface(world, {0, 0}, 12, to_block_id(BlockType::Grass), to_block_id(BlockType::Dirt));

    const auto environment = EnvironmentClock::compute_state(23.0F);
    const auto cycle = EnvironmentClock::classify_creature_cycle(23.0F);
    system.update(0.0F, world, {14.5F, 13.001F, 14.5F}, environment, cycle);
    REQUIRE(system.active_creatures().size() == 1);

    const auto spawn_position = system.active_creatures().front().position;
    const auto close_player_position = spawn_position + glm::vec3 {0.8F, 0.0F, 0.0F};
    float max_attack_amount = 0.0F;
    bool attacked = false;

    for (int frame = 0; frame < 120; ++frame) {
        system.update(1.0F / 60.0F, world, close_player_position, environment, cycle);
        REQUIRE(system.render_instances().size() == 1);
        max_attack_amount = std::max(max_attack_amount, system.render_instances().front().attack_amount);
        if (system.recent_attacks().empty()) {
            continue;
        }

        attacked = true;
        CHECK(system.recent_attacks().front().damage == doctest::Approx(3.0F));
        CHECK(system.recent_attacks().front().species == system.active_creatures().front().anchor.species);
        CHECK(system.active_creatures().front().behavior_state == CreatureBehaviorState::Strike);
        break;
    }

    REQUIRE(attacked);
    CHECK(max_attack_amount > 0.6F);
}

TEST_CASE("night melee attacks require the player to stay on the same floor layer") {
    CreatureSystem system {};
    World world(90031, 1);
    test::make_chunk_surface(world, {0, 0}, 12, to_block_id(BlockType::Grass), to_block_id(BlockType::Dirt));

    const auto environment = EnvironmentClock::compute_state(23.0F);
    const auto cycle = EnvironmentClock::classify_creature_cycle(23.0F);
    const auto anchor = system.spawn_anchor_for_chunk(world, {0, 0});
    REQUIRE(anchor.has_value());

    const auto raised_block_x = anchor->ground_block.x + 1;
    const auto raised_block_z = anchor->ground_block.z;
    world.set_block(raised_block_x, anchor->ground_block.y + 1, raised_block_z, to_block_id(BlockType::Stone));

    system.load_creatures({make_test_creature(*anchor, anchor->spawn_position)}, environment);
    const glm::vec3 player_position {
        static_cast<float>(raised_block_x) + 0.5F,
        static_cast<float>(anchor->ground_block.y + 2) + 0.001F,
        static_cast<float>(raised_block_z) + 0.5F,
    };

    bool attacked = false;
    for (int frame = 0; frame < 120; ++frame) {
        system.update(1.0F / 60.0F, world, player_position, environment, cycle);
        if (!system.recent_attacks().empty()) {
            attacked = true;
            break;
        }
    }

    CHECK_FALSE(attacked);
    REQUIRE(system.active_creatures().size() == 1);
    CHECK(system.active_creatures().front().behavior_state != CreatureBehaviorState::Strike);
}

TEST_CASE("night melee attacks cannot pass through solid walls") {
    CreatureSystem system {};
    World world(90032, 1);
    test::make_chunk_surface(world, {0, 0}, 12, to_block_id(BlockType::Grass), to_block_id(BlockType::Dirt));

    const auto environment = EnvironmentClock::compute_state(23.0F);
    const auto cycle = EnvironmentClock::classify_creature_cycle(23.0F);
    const auto anchor = system.spawn_anchor_for_chunk(world, {0, 0});
    REQUIRE(anchor.has_value());

    const auto wall_x = anchor->ground_block.x + 1;
    const auto wall_z = anchor->ground_block.z;
    world.set_block(wall_x, anchor->ground_block.y + 1, wall_z, to_block_id(BlockType::Stone));
    world.set_block(wall_x, anchor->ground_block.y + 2, wall_z, to_block_id(BlockType::Stone));

    const glm::vec3 creature_position {
        static_cast<float>(wall_x) - 0.01F,
        anchor->spawn_position.y,
        anchor->spawn_position.z,
    };
    system.load_creatures({make_test_creature(*anchor, creature_position)}, environment);

    const glm::vec3 player_position {
        static_cast<float>(wall_x + 1) + 0.01F,
        anchor->spawn_position.y,
        anchor->spawn_position.z,
    };

    bool attacked = false;
    for (int frame = 0; frame < 120; ++frame) {
        system.update(1.0F / 60.0F, world, player_position, environment, cycle);
        if (!system.recent_attacks().empty()) {
            attacked = true;
            break;
        }
    }

    CHECK_FALSE(attacked);
    REQUIRE(system.active_creatures().size() == 1);
    CHECK(system.active_creatures().front().behavior_state != CreatureBehaviorState::Strike);
}

TEST_CASE("night chase steers around a frontal wall instead of stalling") {
    CreatureSystem system {};
    World world(90033, 1);
    test::make_chunk_surface(world, {0, 0}, 12, to_block_id(BlockType::Grass), to_block_id(BlockType::Dirt));

    const auto environment = EnvironmentClock::compute_state(23.0F);
    const auto cycle = EnvironmentClock::classify_creature_cycle(23.0F);
    const auto anchor = system.spawn_anchor_for_chunk(world, {0, 0});
    REQUIRE(anchor.has_value());

    const auto wall_x = anchor->ground_block.x + 1;
    const auto wall_z = anchor->ground_block.z;
    world.set_block(wall_x, anchor->ground_block.y + 1, wall_z, to_block_id(BlockType::Stone));
    world.set_block(wall_x, anchor->ground_block.y + 2, wall_z, to_block_id(BlockType::Stone));

    const glm::vec3 creature_position {
        static_cast<float>(wall_x) - 0.01F,
        anchor->spawn_position.y,
        anchor->spawn_position.z,
    };
    auto creature = make_test_creature(*anchor, creature_position);
    creature.yaw_radians = 0.0F;
    system.load_creatures({creature}, environment);

    const glm::vec3 player_position {
        static_cast<float>(wall_x + 3) + 0.25F,
        anchor->spawn_position.y,
        anchor->spawn_position.z,
    };

    const auto initial_distance_sq = horizontal_distance_squared(creature_position, player_position);
    for (int frame = 0; frame < 90; ++frame) {
        system.update(1.0F / 60.0F, world, player_position, environment, cycle);
    }

    REQUIRE(system.active_creatures().size() == 1);
    const auto& updated = system.active_creatures().front();
    const auto still_aggressive =
        updated.behavior_state == CreatureBehaviorState::Chase || updated.behavior_state == CreatureBehaviorState::Strike;
    CHECK(still_aggressive);
    CHECK(horizontal_distance_squared(updated.position, creature_position) > 0.03F);
    CHECK(horizontal_distance_squared(updated.position, player_position) <= initial_distance_sq + 0.05F);
    CHECK(std::abs(updated.position.z - creature_position.z) > 0.10F);
}

TEST_CASE("dense grassy spawn regions still cap active creature counts") {
    CreatureSystem system {};
    World world(9004, 4);

    for (int chunk_z = -4; chunk_z <= 4; ++chunk_z) {
        for (int chunk_x = -4; chunk_x <= 4; ++chunk_x) {
            test::make_chunk_surface(
                world,
                {chunk_x, chunk_z},
                12,
                to_block_id(BlockType::Grass),
                to_block_id(BlockType::Dirt));
        }
    }

    const auto environment = EnvironmentClock::compute_state(12.0F);
    const auto cycle = EnvironmentClock::classify_creature_cycle(12.0F);
    const glm::vec3 player_position {0.5F, 13.001F, 0.5F};

    system.update(0.0F, world, player_position, environment, cycle);

    CHECK(system.active_creatures().size() == kCreatureMaxActiveCount);
    CHECK(system.render_instances().size() == kCreatureMaxActiveCount);
}

TEST_CASE("creature atlas exposes distinct farm animals and emissive zombie details") {
    const auto atlas = build_creature_atlas_pixels();
    REQUIRE(atlas.size() == static_cast<std::size_t>(kCreatureAtlasSize * kCreatureAtlasSize * 4));

    const auto pig_average = tile_average_rgba(atlas, CreatureAtlasTile::PigHide);
    const auto cow_average = tile_average_rgba(atlas, CreatureAtlasTile::CowHide);
    const auto sheep_average = tile_average_rgba(atlas, CreatureAtlasTile::SheepWool);
    const auto flesh_average = tile_average_rgba(atlas, CreatureAtlasTile::ZombieFlesh);
    const auto bone_average = tile_average_rgba(atlas, CreatureAtlasTile::ZombieBone);
    const auto eye_average = tile_average_rgba(atlas, CreatureAtlasTile::ZombieEye);
    const auto scar_average = tile_average_rgba(atlas, CreatureAtlasTile::ZombieScar);

    CHECK(pig_average[0] > cow_average[0] + 18.0F);
    CHECK(sheep_average[2] > pig_average[2] + 32.0F);
    CHECK(tile_average_rgba(atlas, CreatureAtlasTile::PigSnout)[0] > 180.0F);
    CHECK(tile_average_rgba(atlas, CreatureAtlasTile::CowHorn)[0] > 120.0F);
    CHECK(tile_average_rgba(atlas, CreatureAtlasTile::SheepHoof)[0] < 90.0F);
    CHECK(flesh_average[1] > 118.0F);
    CHECK(bone_average[0] > flesh_average[0] + 36.0F);
    CHECK(eye_average[0] > eye_average[2] + 70.0F);
    CHECK(eye_average[3] > 60.0F);
    CHECK(scar_average[3] > 5.0F);
    CHECK(tile_alpha_coverage(atlas, CreatureAtlasTile::ZombieEye) > 0.35F);
    CHECK(tile_alpha_coverage(atlas, CreatureAtlasTile::ZombieScar) > 0.04F);
    CHECK(tile_alpha_coverage(atlas, CreatureAtlasTile::ZombieScar) < 0.27F);
    CHECK(tile_alpha_coverage(atlas, CreatureAtlasTile::ZombieVein) > 0.08F);
    CHECK(tile_alpha_coverage(atlas, CreatureAtlasTile::ZombieVein) < 0.50F);
}

TEST_CASE("villager atlas and geometry build a readable passive NPC silhouette") {
    const auto atlas = build_creature_atlas_pixels();
    REQUIRE(atlas.size() == static_cast<std::size_t>(kCreatureAtlasSize * kCreatureAtlasSize * 4));

    const auto cloth_average = tile_average_rgba(atlas, CreatureAtlasTile::VillagerCloth);
    const auto skin_average = tile_average_rgba(atlas, CreatureAtlasTile::VillagerSkin);
    const auto hair_average = tile_average_rgba(atlas, CreatureAtlasTile::VillagerHair);
    const auto eye_coverage = tile_alpha_coverage(atlas, CreatureAtlasTile::VillagerEye);

    CHECK(cloth_average[2] > cloth_average[0] + 24.0F);
    CHECK(skin_average[0] > hair_average[0] + 80.0F);
    CHECK(eye_coverage > 0.02F);
    CHECK(eye_coverage < 0.50F);

    const CreatureRenderInstance villager {
        CreatureSpecies::Villager,
        {0.0F, 0.0F, 0.0F},
        0.0F,
        0.65F,
        0.0F,
        1.0F,
        0.22F,
        31337U,
        CreatureBehaviorState::Wander,
        CreaturePhase::Day,
        0.55F,
        0.68F,
        0.0F,
    };

    const auto mesh = build_creature_mesh(villager);
    const auto bounds = mesh_bounds(mesh);

    CHECK_FALSE(mesh.empty());
    CHECK(mesh.part_count >= 10);
    CHECK((bounds.max.y - bounds.min.y) > 1.75F);
    CHECK((bounds.max.x - bounds.min.x) > 0.30F);
    CHECK(band_volume_proxy(mesh, 0.90F, 1.90F) > 0.10F);
    CHECK(all_vertex_attributes_are_bounded(mesh));
    CHECK_FALSE(has_emissive_vertices(mesh));
}

TEST_CASE("creature geometry stretches day animals into deterministic long-limbed zombies") {
    for (const auto species : {CreatureSpecies::Pig, CreatureSpecies::Cow, CreatureSpecies::Sheep}) {
        const CreatureRenderInstance day {
            species,
            {0.0F, 0.0F, 0.0F},
            0.0F,
            0.50F,
            0.0F,
            1.0F,
            0.10F,
            1234U,
            CreatureBehaviorState::Idle,
            CreaturePhase::Day,
            0.10F,
            0.20F,
            0.0F,
        };
        const CreatureRenderInstance night {
            species,
            {0.0F, 0.0F, 0.0F},
            0.0F,
            0.80F,
            1.0F,
            0.18F,
            0.95F,
            1234U,
            CreatureBehaviorState::Strike,
            CreaturePhase::Night,
            0.85F,
            0.92F,
            1.0F,
        };

        const auto day_mesh = build_creature_mesh(day);
        const auto night_mesh = build_creature_mesh(night);
        const auto day_bounds = mesh_bounds(day_mesh);
        const auto night_bounds = mesh_bounds(night_mesh);

        CAPTURE(static_cast<int>(species));
        CHECK_FALSE(day_mesh.empty());
        CHECK_FALSE(night_mesh.empty());
        CHECK(night_mesh.part_count > day_mesh.part_count);
        CHECK(night_mesh.vertices.size() > day_mesh.vertices.size());
        CHECK((night_bounds.max.y - night_bounds.min.y) > (day_bounds.max.y - day_bounds.min.y) + 0.18F);
        CHECK(max_material_class(day_mesh) < 0.70F);
        CHECK(max_material_class(night_mesh) > 0.85F);
        CHECK(has_emissive_vertices(night_mesh));
        CHECK(all_vertex_attributes_are_bounded(day_mesh));
        CHECK(all_vertex_attributes_are_bounded(night_mesh));
    }
}

TEST_CASE("night creature silhouettes keep readable torso mass and depth") {
    for (const auto species : {CreatureSpecies::Pig, CreatureSpecies::Cow, CreatureSpecies::Sheep}) {
        const CreatureRenderInstance night {
            species,
            {0.0F, 0.0F, 0.0F},
            0.0F,
            0.85F,
            1.0F,
            0.18F,
            0.92F,
            4242U,
            CreatureBehaviorState::Strike,
            CreaturePhase::Night,
            0.90F,
            0.88F,
            1.0F,
        };

        const auto night_mesh = build_creature_mesh(night);
        CAPTURE(static_cast<int>(species));
        CHECK(band_volume_proxy(night_mesh, 0.90F, 1.85F) > 0.12F);
        CHECK(band_depth_span(night_mesh, 0.90F, 1.85F) > 0.22F);
    }
}

TEST_CASE("day species silhouettes differ and appearance variation remains deterministic per seed") {
    CreatureMeshData pig_mesh {};
    CreatureMeshData cow_mesh {};
    CreatureMeshData sheep_mesh {};

    for (const auto species : {CreatureSpecies::Pig, CreatureSpecies::Cow, CreatureSpecies::Sheep}) {
        const CreatureRenderInstance seed_a {
            species,
            {0.0F, 0.0F, 0.0F},
            0.0F,
            0.70F,
            0.45F,
            0.70F,
            0.42F,
            2222U,
            CreatureBehaviorState::Lurk,
            CreaturePhase::DuskMorph,
            0.45F,
            0.52F,
            0.25F,
        };
        auto seed_b = seed_a;
        seed_b.appearance_seed = 7788U;
        seed_b.animation_time = 1.05F;

        const auto mesh_a = build_creature_mesh(seed_a);
        const auto mesh_a_repeat = build_creature_mesh(seed_a);
        const auto mesh_b = build_creature_mesh(seed_b);

        CAPTURE(static_cast<int>(species));
        CHECK(meshes_match_exactly(mesh_a, mesh_a_repeat));
        CHECK(mesh_a.part_count == mesh_b.part_count);
        CHECK_FALSE(meshes_match_exactly(mesh_a, mesh_b));
        CHECK(max_position_delta(mesh_a, mesh_b) > 0.003F);

        switch (species) {
        case CreatureSpecies::Pig:
            pig_mesh = build_creature_mesh({
                species, {0.0F, 0.0F, 0.0F}, 0.0F, 0.30F, 0.0F, 1.0F, 0.10F, 999U,
                CreatureBehaviorState::Idle, CreaturePhase::Day, 0.10F, 0.10F, 0.0F
            });
            break;
        case CreatureSpecies::Cow:
            cow_mesh = build_creature_mesh({
                species, {0.0F, 0.0F, 0.0F}, 0.0F, 0.30F, 0.0F, 1.0F, 0.10F, 999U,
                CreatureBehaviorState::Idle, CreaturePhase::Day, 0.10F, 0.10F, 0.0F
            });
            break;
        case CreatureSpecies::Sheep:
            sheep_mesh = build_creature_mesh({
                species, {0.0F, 0.0F, 0.0F}, 0.0F, 0.30F, 0.0F, 1.0F, 0.10F, 999U,
                CreatureBehaviorState::Idle, CreaturePhase::Day, 0.10F, 0.10F, 0.0F
            });
            break;
        case CreatureSpecies::Villager:
            break;
        }
    }

    CHECK_FALSE(meshes_match_exactly(pig_mesh, cow_mesh));
    CHECK_FALSE(meshes_match_exactly(pig_mesh, sheep_mesh));
    CHECK(body_volume_proxy(cow_mesh) > body_volume_proxy(pig_mesh));
    CHECK(body_volume_proxy(sheep_mesh) > body_volume_proxy(pig_mesh));
}

TEST_CASE("creature parts tessellate identically to the public mesh builder") {
    const std::array<CreatureSpecies, 3> species_list {{
        CreatureSpecies::Pig,
        CreatureSpecies::Cow,
        CreatureSpecies::Sheep,
    }};

    for (const auto species : species_list) {
        const CreatureRenderInstance creature {
            species,
            {1.5F, 0.25F, -2.0F},
            0.65F,
            1.15F,
            0.58F,
            0.18F,
            0.72F,
            4242U,
            CreatureBehaviorState::Strike,
            CreaturePhase::Night,
            0.64F,
            0.48F,
            0.82F,
        };

        const auto parts = build_creature_parts(creature);
        const auto tessellated_mesh = build_creature_mesh(std::span<const CreaturePartInstance>(parts.data(), parts.size()));
        const auto public_mesh = build_creature_mesh(creature);

        CAPTURE(static_cast<int>(species));
        CHECK_FALSE(parts.empty());
        CHECK(public_mesh.part_count == parts.size());
        CHECK(tessellated_mesh.part_count == parts.size());
        CHECK(meshes_match_exactly(tessellated_mesh, public_mesh));
    }
}

TEST_CASE("render instances surface motion gaze and attack signals across day and night") {
    CreatureSystem system {};
    World world(9005, 1);
    test::make_chunk_surface(world, {0, 0}, 12, to_block_id(BlockType::Grass), to_block_id(BlockType::Dirt));

    const auto day_environment = EnvironmentClock::compute_state(12.0F);
    const auto day_cycle = EnvironmentClock::classify_creature_cycle(12.0F);
    system.update(0.0F, world, {15.5F, 13.001F, 15.5F}, day_environment, day_cycle);
    REQUIRE(system.active_creatures().size() == 1);

    const auto baseline_motion = system.render_instances().front().motion_amount;
    const auto creature_position = system.active_creatures().front().position;
    const auto close_player_position = creature_position + glm::vec3 {0.20F, 0.0F, 0.0F};

    for (int frame = 0; frame < 30; ++frame) {
        system.update(1.0F / 60.0F, world, close_player_position, day_environment, day_cycle);
    }

    REQUIRE(system.render_instances().size() == 1);
    CHECK(system.render_instances().front().motion_amount > baseline_motion + 0.08F);
    CHECK(system.render_instances().front().attack_amount == doctest::Approx(0.0F).epsilon(0.05F));

    const auto night_environment = EnvironmentClock::compute_state(23.0F);
    const auto night_cycle = EnvironmentClock::classify_creature_cycle(23.0F);
    for (int frame = 0; frame < 60; ++frame) {
        system.update(1.0F / 60.0F, world, close_player_position, night_environment, night_cycle);
    }

    REQUIRE(system.render_instances().size() == 1);
    CHECK(system.render_instances().front().phase == CreaturePhase::Night);
    CHECK(system.render_instances().front().gaze_weight > 0.75F);
    CHECK(system.render_instances().front().attack_amount > 0.40F);
}

} // namespace valcraft
