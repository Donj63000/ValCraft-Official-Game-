#include "creatures/CreatureGeometry.h"
#include "creatures/CreatureSystem.h"
#include "gameplay/StartingVillage.h"

#include "TestUtils.h"

#include <doctest/doctest.h>
#include <glm/geometric.hpp>
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <span>
#include <vector>

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

auto make_test_resident_anchor(const BlockCoord& ground_block) -> CreatureSpawnAnchor {
    CreatureSpawnAnchor anchor {};
    anchor.chunk = {ground_block.x / kChunkSizeX, ground_block.z / kChunkSizeZ};
    anchor.ground_block = ground_block;
    anchor.spawn_position = {
        static_cast<float>(ground_block.x) + 0.5F,
        static_cast<float>(ground_block.y) + 1.001F,
        static_cast<float>(ground_block.z) + 0.5F,
    };
    anchor.species = CreatureSpecies::Villager;
    anchor.roam_radius = 12.0F;
    anchor.patrol_points.fill(anchor.spawn_position);
    anchor.patrol_point_count = static_cast<std::uint8_t>(anchor.patrol_points.size());
    return anchor;
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

using PartBounds = MeshBounds;

auto part_bounds(const CreaturePartInstance& part) -> PartBounds {
    static constexpr std::array<glm::vec3, 8> kCorners {{
        {-0.5F, -0.5F, -0.5F},
        {-0.5F, -0.5F, 0.5F},
        {-0.5F, 0.5F, -0.5F},
        {-0.5F, 0.5F, 0.5F},
        {0.5F, -0.5F, -0.5F},
        {0.5F, -0.5F, 0.5F},
        {0.5F, 0.5F, -0.5F},
        {0.5F, 0.5F, 0.5F},
    }};

    PartBounds bounds {
        glm::vec3 {std::numeric_limits<float>::max()},
        glm::vec3 {std::numeric_limits<float>::lowest()},
    };
    for (const auto& corner : kCorners) {
        const auto world_position = part.transform * glm::vec4 {corner, 1.0F};
        bounds.min.x = std::min(bounds.min.x, world_position.x);
        bounds.min.y = std::min(bounds.min.y, world_position.y);
        bounds.min.z = std::min(bounds.min.z, world_position.z);
        bounds.max.x = std::max(bounds.max.x, world_position.x);
        bounds.max.y = std::max(bounds.max.y, world_position.y);
        bounds.max.z = std::max(bounds.max.z, world_position.z);
    }
    return bounds;
}

auto max_vertical_gap_at_z(std::span<const CreaturePartInstance> parts,
                           float min_y,
                           float max_y,
                           float center_z,
                           float half_depth) -> float {
    std::vector<std::array<float, 2>> intervals {};
    for (const auto& part : parts) {
        const auto bounds = part_bounds(part);
        if (bounds.max.z < center_z - half_depth ||
            bounds.min.z > center_z + half_depth ||
            bounds.max.y < min_y ||
            bounds.min.y > max_y) {
            continue;
        }
        intervals.push_back({
            std::max(bounds.min.y, min_y),
            std::min(bounds.max.y, max_y),
        });
    }

    if (intervals.empty()) {
        return max_y - min_y;
    }

    std::sort(intervals.begin(), intervals.end(), [](const auto& lhs, const auto& rhs) {
        return lhs[0] < rhs[0];
    });

    auto cursor = min_y;
    auto largest_gap = 0.0F;
    for (const auto& interval : intervals) {
        if (interval[0] > cursor) {
            largest_gap = std::max(largest_gap, interval[0] - cursor);
        }
        cursor = std::max(cursor, interval[1]);
    }

    return std::max(largest_gap, max_y - cursor);
}

auto max_vertical_gap_at_xz(std::span<const CreaturePartInstance> parts,
                            float min_y,
                            float max_y,
                            float center_x,
                            float half_width,
                            float center_z,
                            float half_depth) -> float {
    std::vector<std::array<float, 2>> intervals {};
    for (const auto& part : parts) {
        const auto bounds = part_bounds(part);
        if (bounds.max.x < center_x - half_width ||
            bounds.min.x > center_x + half_width ||
            bounds.max.z < center_z - half_depth ||
            bounds.min.z > center_z + half_depth ||
            bounds.max.y < min_y ||
            bounds.min.y > max_y) {
            continue;
        }
        intervals.push_back({
            std::max(bounds.min.y, min_y),
            std::min(bounds.max.y, max_y),
        });
    }

    if (intervals.empty()) {
        return max_y - min_y;
    }

    std::sort(intervals.begin(), intervals.end(), [](const auto& lhs, const auto& rhs) {
        return lhs[0] < rhs[0];
    });

    auto cursor = min_y;
    auto largest_gap = 0.0F;
    for (const auto& interval : intervals) {
        if (interval[0] > cursor) {
            largest_gap = std::max(largest_gap, interval[0] - cursor);
        }
        cursor = std::max(cursor, interval[1]);
    }

    return std::max(largest_gap, max_y - cursor);
}

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

TEST_CASE("generated starting village residents patrol the applied village terrain") {
    constexpr int seed = 90115;
    World world(seed, 4);
    StartingVillageGenerator generator(seed);
    const auto layout = generator.build_layout();
    REQUIRE_FALSE(layout.residents.empty());
    generator.apply(world, layout);

    CreatureSystem system {};
    system.set_settlement_residents(layout.residents);

    const auto environment = EnvironmentClock::compute_state(13.25F);
    const auto cycle = EnvironmentClock::classify_creature_cycle(13.25F);
    const auto player_position = layout.player_spawn + glm::vec3 {3.0F, 0.0F, 3.0F};

    for (int frame = 0; frame < 480; ++frame) {
        system.update(1.0F / 60.0F, world, player_position, environment, cycle);
    }

    const auto creatures = system.active_creatures();
    const auto villager_count = static_cast<std::size_t>(
        std::count_if(creatures.begin(), creatures.end(), [](const CreatureInstance& creature) {
            return creature.anchor.species == CreatureSpecies::Villager;
        }));
    const auto moved_villagers = static_cast<std::size_t>(
        std::count_if(creatures.begin(), creatures.end(), [](const CreatureInstance& creature) {
            return creature.anchor.species == CreatureSpecies::Villager &&
                   horizontal_distance_squared(creature.position, creature.anchor.spawn_position) > 0.35F;
        }));

    REQUIRE(villager_count >= std::min<std::size_t>(layout.residents.size(), std::size_t {3}));
    CHECK(moved_villagers >= std::min<std::size_t>(villager_count, std::size_t {2}));
    CHECK(std::all_of(creatures.begin(), creatures.end(), [](const CreatureInstance& creature) {
        return creature.anchor.species != CreatureSpecies::Villager ||
               horizontal_distance_squared(creature.position, creature.anchor.spawn_position) <=
                   creature.anchor.roam_radius * creature.anchor.roam_radius;
    }));
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

TEST_CASE("day animals take partial steps instead of freezing against a close obstacle") {
    CreatureSystem system {};
    World world(90113, 1);
    test::make_chunk_surface(world, {0, 0}, 12, to_block_id(BlockType::Grass), to_block_id(BlockType::Dirt));

    const auto environment = EnvironmentClock::compute_state(12.0F);
    const auto cycle = EnvironmentClock::classify_creature_cycle(12.0F);
    const auto anchor = system.spawn_anchor_for_chunk(world, {0, 0});
    REQUIRE(anchor.has_value());

    world.set_block(anchor->ground_block.x + 1, anchor->ground_block.y + 1, anchor->ground_block.z, to_block_id(BlockType::Stone));
    world.set_block(anchor->ground_block.x + 1, anchor->ground_block.y + 2, anchor->ground_block.z, to_block_id(BlockType::Stone));

    auto creature = make_test_creature(*anchor, anchor->spawn_position);
    creature.behavior_state = CreatureBehaviorState::Wander;
    creature.behavior_timer = 3.0F;
    creature.yaw_radians = 0.0F;
    creature.wander_heading = 0.0F;
    system.load_creatures({creature}, environment);

    const auto before = system.active_creatures().front().position;
    system.update(0.80F, world, before + glm::vec3 {20.0F, 0.0F, 20.0F}, environment, cycle);

    REQUIRE(system.active_creatures().size() == 1);
    const auto& updated = system.active_creatures().front();
    CHECK(updated.position.x > before.x + 0.08F);
    CHECK(static_cast<int>(std::floor(updated.position.x)) == anchor->ground_block.x);
    CHECK(updated.position.y == doctest::Approx(anchor->spawn_position.y).epsilon(0.01F));
}

TEST_CASE("creature loading sanitizes corrupted saved state") {
    constexpr auto nan = std::numeric_limits<float>::quiet_NaN();
    constexpr auto infinity = std::numeric_limits<float>::infinity();

    CreatureInstance creature {};
    creature.anchor.chunk = {std::numeric_limits<int>::min(), std::numeric_limits<int>::max()};
    creature.anchor.ground_block = {3, 12, 4};
    creature.anchor.spawn_position = {nan, infinity, -infinity};
    creature.anchor.species = static_cast<CreatureSpecies>(255U);
    creature.anchor.roam_radius = infinity;
    creature.anchor.patrol_point_count = 99U;
    creature.anchor.patrol_points[0] = {nan, 4.0F, 2.0F};
    creature.position = {infinity, nan, -infinity};
    creature.yaw_radians = infinity;
    creature.behavior_timer = -infinity;
    creature.animation_time = nan;
    creature.wander_heading = -infinity;
    creature.nervous_intensity = infinity;
    creature.behavior_state = static_cast<CreatureBehaviorState>(255U);
    creature.phase = static_cast<CreaturePhase>(255U);
    creature.morph_factor = infinity;
    creature.motion_amount = nan;
    creature.gaze_weight = -infinity;
    creature.attack_cooldown = infinity;
    creature.attack_amount = infinity;
    creature.hurt_timer = nan;
    creature.health = nan;
    creature.hit_direction = {nan, 0.0F, infinity};
    creature.resident_target_index = 99U;

    CreatureSystem system {};
    const auto environment = EnvironmentClock::compute_state(12.0F);
    system.load_creatures({creature}, environment);

    REQUIRE(system.active_creatures().size() == 1);
    const auto& loaded = system.active_creatures().front();
    CHECK(loaded.anchor.species == CreatureSpecies::Pig);
    CHECK(loaded.anchor.patrol_point_count == kCreatureResidentPatrolPointCount);
    CHECK(loaded.anchor.roam_radius == doctest::Approx(0.0F));
    CHECK(std::isfinite(loaded.anchor.spawn_position.x));
    CHECK(std::isfinite(loaded.anchor.spawn_position.y));
    CHECK(std::isfinite(loaded.anchor.spawn_position.z));
    CHECK(std::isfinite(loaded.position.x));
    CHECK(std::isfinite(loaded.position.y));
    CHECK(std::isfinite(loaded.position.z));
    CHECK(std::isfinite(loaded.yaw_radians));
    CHECK(std::isfinite(loaded.wander_heading));
    CHECK(loaded.behavior_timer == doctest::Approx(0.0F));
    CHECK(loaded.animation_time == doctest::Approx(0.0F));
    CHECK(loaded.nervous_intensity == doctest::Approx(0.0F));
    CHECK(loaded.behavior_state == CreatureBehaviorState::Idle);
    CHECK(loaded.phase == CreaturePhase::Day);
    CHECK(loaded.health == doctest::Approx(creature_max_health(CreatureSpecies::Pig)));
    CHECK(loaded.resident_target_index == kCreatureResidentPatrolPointCount - 1U);

    REQUIRE(system.render_instances().size() == 1);
    const auto& render = system.render_instances().front();
    CHECK(std::isfinite(render.position.x));
    CHECK(std::isfinite(render.position.y));
    CHECK(std::isfinite(render.position.z));
    CHECK(std::isfinite(render.yaw_radians));
    CHECK(std::isfinite(render.hit_direction.x));
    CHECK(std::isfinite(render.hit_direction.y));
    CHECK(std::isfinite(render.hit_direction.z));

    World world(90112, 1);
    system.update(nan, world, {nan, infinity, -infinity}, environment, EnvironmentClock::classify_creature_cycle(12.0F));
    CHECK(system.active_creatures().empty());
}

TEST_CASE("day animals keep their rendered facing direction aligned with every realised step") {
    const auto environment = EnvironmentClock::compute_state(12.0F);
    const auto cycle = EnvironmentClock::classify_creature_cycle(12.0F);
    struct SpeciesSpawn {
        CreatureSpecies species;
        ChunkCoord chunk;
    };

    const std::array<SpeciesSpawn, 3> species_to_check {{
        {CreatureSpecies::Cow, {0, 0}},
        {CreatureSpecies::Pig, {1, 0}},
        {CreatureSpecies::Sheep, {2, 0}},
    }};

    for (const auto& spawn : species_to_check) {
        CreatureSystem system {};
        World world(90110, 3);
        test::make_chunk_surface(world, {0, 0}, 12, to_block_id(BlockType::Grass), to_block_id(BlockType::Dirt));
        test::make_chunk_surface(world, {1, 0}, 13, to_block_id(BlockType::Grass), to_block_id(BlockType::Dirt));
        test::make_chunk_surface(world, {2, 0}, 52, to_block_id(BlockType::Grass), to_block_id(BlockType::Dirt));

        for (int x = 18; x < 30; x += 3) {
            add_tree_patch(world, x, 6 + (x % 3), 14);
        }
        sculpt_sheep_hills(world, {2, 0}, 52);

        const auto anchor = system.spawn_anchor_for_chunk(world, spawn.chunk);
        REQUIRE(anchor.has_value());
        REQUIRE(anchor->species == spawn.species);

        auto creature = make_test_creature(*anchor, anchor->spawn_position);
        creature.behavior_state = CreatureBehaviorState::Wander;
        creature.behavior_timer = 3.0F;
        creature.yaw_radians = -1.45F;
        creature.wander_heading = 0.15F;
        creature.motion_amount = 0.0F;
        system.load_creatures({creature}, environment);

        auto previous_position = system.active_creatures().front().position;
        int moved_frames = 0;
        const auto far_player_position = anchor->spawn_position + glm::vec3 {8.0F, 0.0F, 8.0F};
        for (int frame = 0; frame < 120; ++frame) {
            system.update(1.0F / 30.0F, world, far_player_position, environment, cycle);
            const auto active_creatures = system.active_creatures();
            const auto updated_it = std::find_if(active_creatures.begin(),
                                                 active_creatures.end(),
                                                 [&](const CreatureInstance& active) {
                                                     return active.anchor == *anchor;
                                                 });
            REQUIRE(updated_it != active_creatures.end());
            const auto updated_index = static_cast<std::size_t>(std::distance(active_creatures.begin(), updated_it));
            REQUIRE(system.render_instances().size() > updated_index);

            const auto& updated = *updated_it;
            const glm::vec2 displacement {
                updated.position.x - previous_position.x,
                updated.position.z - previous_position.z,
            };
            const auto distance_squared = glm::dot(displacement, displacement);
            if (distance_squared > 1.0e-7F) {
                const auto travel_direction = glm::normalize(displacement);
                const auto live_facing = yaw_direction(updated.yaw_radians);
                const auto render_facing = yaw_direction(system.render_instances()[updated_index].yaw_radians);
                CAPTURE(static_cast<int>(spawn.species));
                CAPTURE(frame);
                CHECK(glm::dot(travel_direction, live_facing) > 0.94F);
                CHECK(glm::dot(travel_direction, render_facing) > 0.94F);
                ++moved_frames;
            }
            previous_position = updated.position;
        }

        CAPTURE(static_cast<int>(spawn.species));
        CHECK(moved_frames > 12);
    }
}

TEST_CASE("settlement residents keep rendered facing direction aligned with every realised step") {
    CreatureSystem system {};
    World world(90111, 1);
    test::make_chunk_surface(world, {0, 0}, 12, to_block_id(BlockType::Grass), to_block_id(BlockType::Dirt));

    const auto environment = EnvironmentClock::compute_state(12.0F);
    const auto cycle = EnvironmentClock::classify_creature_cycle(12.0F);
    const auto anchor = make_test_resident_anchor({4, 12, 4});
    system.set_settlement_residents({anchor});

    auto resident = make_test_creature(anchor, anchor.spawn_position);
    resident.behavior_state = CreatureBehaviorState::Wander;
    resident.behavior_timer = 2.0F;
    resident.yaw_radians = -1.30F;
    resident.wander_heading = 0.22F;
    system.load_creatures({resident}, environment);

    REQUIRE(system.active_creatures().size() == 1);
    REQUIRE(system.render_instances().size() == 1);
    auto previous_position = system.active_creatures().front().position;
    int moved_frames = 0;
    for (int frame = 0; frame < 90; ++frame) {
        system.update(1.0F / 30.0F, world, {12.5F, anchor.spawn_position.y, 12.5F}, environment, cycle);

        REQUIRE(system.active_creatures().size() == 1);
        REQUIRE(system.render_instances().size() == 1);
        const auto& updated = system.active_creatures().front();
        const glm::vec2 displacement {
            updated.position.x - previous_position.x,
            updated.position.z - previous_position.z,
        };
        const auto distance_squared = glm::dot(displacement, displacement);
        if (distance_squared > 1.0e-7F) {
            const auto travel_direction = glm::normalize(displacement);
            const auto live_facing = yaw_direction(updated.yaw_radians);
            const auto render_facing = yaw_direction(system.render_instances().front().yaw_radians);
            CAPTURE(frame);
            CHECK(glm::dot(travel_direction, live_facing) > 0.97F);
            CHECK(glm::dot(travel_direction, render_facing) > 0.97F);
            ++moved_frames;
        }
        previous_position = updated.position;
    }

    CHECK(moved_frames > 12);
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
    const auto chase_player_position = spawn_position + glm::vec3 {3.4F, 0.0F, 0.0F};
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

TEST_CASE("night monsters pursue a nearby player beyond their passive roam ring") {
    CreatureSystem system {};
    World world(90114, 3);
    test::make_chunk_surface(world, {0, 0}, 12, to_block_id(BlockType::Grass), to_block_id(BlockType::Dirt));
    test::make_chunk_surface(world, {1, 0}, 12, to_block_id(BlockType::Stone), to_block_id(BlockType::Stone));
    test::make_chunk_surface(world, {2, 0}, 12, to_block_id(BlockType::Stone), to_block_id(BlockType::Stone));

    const auto environment = EnvironmentClock::compute_state(23.0F);
    const auto cycle = EnvironmentClock::classify_creature_cycle(23.0F);
    const auto anchor = system.spawn_anchor_for_chunk(world, {0, 0});
    REQUIRE(anchor.has_value());

    auto creature = make_test_creature(*anchor, anchor->spawn_position + glm::vec3 {5.0F, 0.0F, 0.0F});
    creature.behavior_state = CreatureBehaviorState::Lurk;
    creature.phase = CreaturePhase::Night;
    creature.morph_factor = 1.0F;
    system.load_creatures({creature}, environment);

    const auto player_position = creature.position + glm::vec3 {9.40F, 0.0F, 0.0F};
    auto closest_distance_sq = horizontal_distance_squared(creature.position, player_position);
    auto attacked = false;
    for (int frame = 0; frame < 420; ++frame) {
        system.update(1.0F / 60.0F, world, player_position, environment, cycle);
        REQUIRE_FALSE(system.active_creatures().empty());
        const auto& updated = system.active_creatures().front();
        closest_distance_sq = std::min(closest_distance_sq, horizontal_distance_squared(updated.position, player_position));
        if (!system.recent_attacks().empty()) {
            attacked = true;
            break;
        }
    }

    CHECK(closest_distance_sq < 8.20F);
    CHECK(attacked);
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

TEST_CASE("player weapon ray damages and kills targeted creatures") {
    CreatureSystem system {};
    const auto environment = EnvironmentClock::compute_state(12.0F);

    auto anchor = make_test_resident_anchor({0, 12, 2});
    anchor.spawn_position = {0.5F, 13.001F, 3.0F};
    auto villager = make_test_creature(anchor, anchor.spawn_position);
    villager.health = creature_max_health(CreatureSpecies::Villager);
    system.load_creatures({villager}, environment);

    const glm::vec3 weapon_origin {0.5F, 13.751F, 0.5F};
    const glm::vec3 weapon_direction {0.0F, 0.0F, 1.0F};
    const auto hit = system.try_damage_from_player(weapon_origin, weapon_direction, 4.0F, 5.0F);

    REQUIRE(hit.hit);
    CHECK_FALSE(hit.killed);
    CHECK(hit.species == CreatureSpecies::Villager);
    CHECK(hit.remaining_health == doctest::Approx(creature_max_health(CreatureSpecies::Villager) - 5.0F));
    CHECK(hit.distance == doctest::Approx(2.02F).epsilon(0.02F));
    REQUIRE(system.active_creatures().size() == 1);
    CHECK(system.active_creatures().front().health == doctest::Approx(hit.remaining_health));
    CHECK(system.active_creatures().front().behavior_state == CreatureBehaviorState::Flee);

    const auto kill = system.try_damage_from_player(weapon_origin, weapon_direction, 4.0F, 20.0F);

    REQUIRE(kill.hit);
    CHECK(kill.killed);
    CHECK(kill.species == CreatureSpecies::Villager);
    CHECK(kill.remaining_health == doctest::Approx(0.0F));
    CHECK(system.active_creatures().empty());
    CHECK(system.render_instances().empty());
}

TEST_CASE("player weapon ray can hit the giant night monster torso and head") {
    CreatureSystem system {};
    const auto environment = EnvironmentClock::compute_state(23.0F);

    CreatureSpawnAnchor anchor {};
    anchor.chunk = {0, 0};
    anchor.ground_block = {0, 12, 4};
    anchor.spawn_position = {0.5F, 13.001F, 4.0F};
    anchor.species = CreatureSpecies::Cow;
    anchor.roam_radius = 8.0F;

    auto day_creature = make_test_creature(anchor, anchor.spawn_position);
    day_creature.health = creature_max_health(anchor.species);
    day_creature.phase = CreaturePhase::Day;
    day_creature.morph_factor = 0.0F;
    system.load_creatures({day_creature}, environment);

    const glm::vec3 high_weapon_origin {
        anchor.spawn_position.x,
        anchor.spawn_position.y + 3.24F,
        anchor.spawn_position.z - 3.0F,
    };
    const glm::vec3 weapon_direction {0.0F, 0.0F, 1.0F};
    CHECK_FALSE(system.try_damage_from_player(high_weapon_origin, weapon_direction, 5.0F, 1.0F).hit);

    auto night_creature = day_creature;
    night_creature.phase = CreaturePhase::Night;
    night_creature.morph_factor = 1.0F;
    night_creature.health = creature_max_health(anchor.species);
    system.load_creatures({night_creature}, environment);

    const auto torso_hit = system.try_damage_from_player(high_weapon_origin, weapon_direction, 5.0F, 2.0F);
    REQUIRE(torso_hit.hit);
    CHECK_FALSE(torso_hit.killed);
    CHECK(torso_hit.species == anchor.species);
    CHECK(torso_hit.distance == doctest::Approx(2.18F).epsilon(0.05F));

    const glm::vec3 head_weapon_origin {
        anchor.spawn_position.x,
        anchor.spawn_position.y + 4.22F,
        anchor.spawn_position.z - 3.0F,
    };
    const auto head_hit = system.try_damage_from_player(head_weapon_origin, weapon_direction, 5.0F, 2.0F);
    CHECK(head_hit.hit);
}

TEST_CASE("player weapon range clipped by a world raycast does not hit creatures behind blocks") {
    CreatureSystem system {};
    World world(90041, 1);
    test::make_chunk_surface(world, {0, 0}, 12, to_block_id(BlockType::Grass), to_block_id(BlockType::Dirt));
    world.set_block(0, 13, 2, to_block_id(BlockType::Stone));

    auto anchor = make_test_resident_anchor({0, 12, 4});
    anchor.spawn_position = {0.5F, 13.001F, 4.0F};
    auto villager = make_test_creature(anchor, anchor.spawn_position);
    villager.health = creature_max_health(CreatureSpecies::Villager);
    system.load_creatures({villager}, EnvironmentClock::compute_state(12.0F));

    const glm::vec3 weapon_origin {0.5F, 13.751F, 0.5F};
    const glm::vec3 weapon_direction {0.0F, 0.0F, 1.0F};
    const auto block_hit = world.raycast(weapon_origin, weapon_direction, 4.0F);

    REQUIRE(block_hit.hit);
    CHECK(block_hit.block == BlockCoord {0, 13, 2});
    CHECK(block_hit.distance == doctest::Approx(1.5F));
    CHECK_FALSE(system.try_damage_from_player(weapon_origin, weapon_direction, block_hit.distance, 6.0F).hit);

    const auto unobstructed_hit = system.try_damage_from_player(weapon_origin, weapon_direction, 4.0F, 6.0F);
    REQUIRE(unobstructed_hit.hit);
    CHECK(unobstructed_hit.species == CreatureSpecies::Villager);
}

TEST_CASE("killed settlement residents do not respawn during the current session") {
    CreatureSystem system {};
    World world(90042, 1);
    test::make_chunk_surface(world, {0, 0}, 12, to_block_id(BlockType::Grass), to_block_id(BlockType::Dirt));

    const auto anchor = make_test_resident_anchor({4, 12, 4});
    const auto environment = EnvironmentClock::compute_state(12.0F);
    const auto cycle = EnvironmentClock::classify_creature_cycle(12.0F);
    system.set_settlement_residents({anchor});

    auto villager = make_test_creature(anchor, anchor.spawn_position);
    villager.health = creature_max_health(CreatureSpecies::Villager);
    system.load_creatures({villager}, environment);

    const glm::vec3 weapon_origin {
        anchor.spawn_position.x,
        anchor.spawn_position.y + 0.75F,
        anchor.spawn_position.z - 2.0F,
    };
    const glm::vec3 weapon_direction {0.0F, 0.0F, 1.0F};
    const auto kill = system.try_damage_from_player(weapon_origin, weapon_direction, 4.0F, 99.0F);
    REQUIRE(kill.hit);
    REQUIRE(kill.killed);
    CHECK(system.active_creatures().empty());

    for (int frame = 0; frame < 8; ++frame) {
        system.update(1.0F / 60.0F, world, anchor.spawn_position, environment, cycle);
    }
    CHECK(system.active_creatures().empty());
    REQUIRE(system.render_instances().size() == 1);
    CHECK(system.render_instances().front().death_amount > 0.0F);

    for (int frame = 0; frame < 80; ++frame) {
        system.update(1.0F / 60.0F, world, anchor.spawn_position, environment, cycle);
    }
    CHECK(system.active_creatures().empty());
    CHECK(system.render_instances().empty());
}

TEST_CASE("giant night melee reaches an elevated player when the path is clear") {
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

    REQUIRE(attacked);
    REQUIRE(system.active_creatures().size() == 1);
    CHECK(system.active_creatures().front().behavior_state == CreatureBehaviorState::Strike);
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

TEST_CASE("giant night chase respects its tall body under low overhangs") {
    CreatureSystem system {};
    World world(90036, 1);
    test::make_chunk_surface(world, {0, 0}, 12, to_block_id(BlockType::Grass), to_block_id(BlockType::Dirt));

    const auto environment = EnvironmentClock::compute_state(23.0F);
    const auto cycle = EnvironmentClock::classify_creature_cycle(23.0F);
    const auto anchor = system.spawn_anchor_for_chunk(world, {0, 0});
    REQUIRE(anchor.has_value());

    const auto travel_sign = anchor->spawn_position.x < static_cast<float>(kChunkSizeX) * 0.5F ? 1.0F : -1.0F;
    const auto overhang_near_x = static_cast<int>(std::floor(anchor->spawn_position.x + travel_sign * 2.0F));
    const auto overhang_far_x = static_cast<int>(std::floor(anchor->spawn_position.x + travel_sign * 7.0F));
    const auto min_overhang_x = std::min(overhang_near_x, overhang_far_x);
    const auto max_overhang_x = std::max(overhang_near_x, overhang_far_x);
    const auto ceiling_y = anchor->ground_block.y + 4;
    for (int z = 0; z < kChunkSizeZ; ++z) {
        for (int x = min_overhang_x; x <= max_overhang_x; ++x) {
            world.set_block(x, ceiling_y, z, to_block_id(BlockType::Stone));
        }
    }

    auto creature = make_test_creature(*anchor, anchor->spawn_position);
    creature.yaw_radians = 0.0F;
    system.load_creatures({creature}, environment);

    const glm::vec3 player_position {
        anchor->spawn_position.x + travel_sign * 9.0F,
        anchor->spawn_position.y,
        anchor->spawn_position.z,
    };

    auto max_forward_progress = 0.0F;
    for (int frame = 0; frame < 180; ++frame) {
        system.update(1.0F / 60.0F, world, player_position, environment, cycle);
        REQUIRE(system.active_creatures().size() == 1);
        const auto forward_progress =
            (system.active_creatures().front().position.x - anchor->spawn_position.x) * travel_sign;
        max_forward_progress = std::max(max_forward_progress, forward_progress);
    }

    CHECK(max_forward_progress < 1.72F);
    CHECK(system.recent_attacks().empty());
}

TEST_CASE("settlement residents keep routine behavior until their timer expires") {
    CreatureSystem system {};
    World world(90034, 1);
    test::make_chunk_surface(world, {0, 0}, 12, to_block_id(BlockType::Grass), to_block_id(BlockType::Dirt));

    const auto anchor = make_test_resident_anchor({4, 12, 4});
    system.set_settlement_residents({anchor});

    const auto environment = EnvironmentClock::compute_state(21.0F);
    const auto cycle = EnvironmentClock::classify_creature_cycle(12.0F);
    auto resident = make_test_creature(anchor, anchor.spawn_position);
    resident.behavior_state = CreatureBehaviorState::Idle;
    resident.behavior_timer = 1.0F;
    resident.yaw_radians = 0.35F;
    resident.wander_heading = 0.35F;
    system.load_creatures({resident}, environment);

    constexpr float dt = 1.0F / 60.0F;
    system.update(dt, world, {12.5F, 13.001F, 12.5F}, environment, cycle);

    REQUIRE(system.active_creatures().size() == 1);
    const auto& updated = system.active_creatures().front();
    CHECK(updated.behavior_state == CreatureBehaviorState::Idle);
    CHECK(updated.behavior_timer == doctest::Approx(1.0F - dt).epsilon(0.001F));
    CHECK(updated.yaw_radians == doctest::Approx(0.35F).epsilon(0.001F));
}

TEST_CASE("settlement residents separate when their personal space overlaps") {
    CreatureSystem system {};
    World world(90037, 1);
    test::make_chunk_surface(world, {0, 0}, 12, to_block_id(BlockType::Grass), to_block_id(BlockType::Dirt));

    const auto first_anchor = make_test_resident_anchor({4, 12, 4});
    const auto second_anchor = make_test_resident_anchor({5, 12, 4});
    system.set_settlement_residents({first_anchor, second_anchor});

    const auto environment = EnvironmentClock::compute_state(12.0F);
    const auto cycle = EnvironmentClock::classify_creature_cycle(12.0F);
    auto first = make_test_creature(first_anchor, first_anchor.spawn_position);
    first.behavior_state = CreatureBehaviorState::Idle;
    first.behavior_timer = 2.0F;
    auto second = make_test_creature(second_anchor, second_anchor.spawn_position);
    second.behavior_state = CreatureBehaviorState::Idle;
    second.behavior_timer = 2.0F;
    second.behavior_seed = 77U;
    system.load_creatures({first, second}, environment);

    const auto initial_distance_sq = horizontal_distance_squared(first.position, second.position);
    system.update(0.25F, world, {24.0F, first_anchor.spawn_position.y, 24.0F}, environment, cycle);

    const auto residents = system.active_creatures();
    REQUIRE(residents.size() == 2);
    const auto separated_distance_sq = horizontal_distance_squared(residents[0].position, residents[1].position);
    CHECK(separated_distance_sq > initial_distance_sq + 0.01F);
    CHECK(residents[0].behavior_state == CreatureBehaviorState::Idle);
    CHECK(residents[1].behavior_state == CreatureBehaviorState::Idle);
}

TEST_CASE("settlement residents stay on the village floor while steering around obstructions") {
    CreatureSystem system {};
    World world(90035, 1);
    test::make_chunk_surface(world, {0, 0}, 12, to_block_id(BlockType::Grass), to_block_id(BlockType::Dirt));

    auto anchor = make_test_resident_anchor({4, 12, 4});
    anchor.patrol_points[2] = {12.5F, anchor.spawn_position.y, 4.5F};
    system.set_settlement_residents({anchor});

    world.set_block(5, 13, 4, to_block_id(BlockType::Stone));

    const auto environment = EnvironmentClock::compute_state(15.0F);
    const auto cycle = EnvironmentClock::classify_creature_cycle(12.0F);
    for (int frame = 0; frame < 210; ++frame) {
        system.update(1.0F / 60.0F, world, {1.5F, anchor.spawn_position.y, 1.5F}, environment, cycle);
    }

    REQUIRE(system.active_creatures().size() == 1);
    const auto& updated = system.active_creatures().front();
    CHECK(updated.position.y == doctest::Approx(anchor.spawn_position.y).epsilon(0.001F));
    CHECK(horizontal_distance_squared(updated.position, anchor.spawn_position) > 0.35F);
    CHECK(std::abs(updated.position.z - anchor.spawn_position.z) > 0.05F);
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

TEST_CASE("creature atlas exposes distinct farm animals and red eyed charcoal night monster details") {
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
    CHECK(flesh_average[0] < 72.0F);
    CHECK(flesh_average[1] < 72.0F);
    CHECK(bone_average[0] > flesh_average[0] + 18.0F);
    CHECK(bone_average[0] < 112.0F);
    CHECK(eye_average[0] > eye_average[1] + 150.0F);
    CHECK(eye_average[0] > eye_average[2] + 165.0F);
    CHECK(eye_average[3] > 120.0F);
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
    const auto apron_average = tile_average_rgba(atlas, CreatureAtlasTile::VillagerApron);
    const auto skin_average = tile_average_rgba(atlas, CreatureAtlasTile::VillagerSkin);
    const auto hair_average = tile_average_rgba(atlas, CreatureAtlasTile::VillagerHair);
    const auto eye_coverage = tile_alpha_coverage(atlas, CreatureAtlasTile::VillagerEye);

    CHECK(cloth_average[0] > 190.0F);
    CHECK(cloth_average[1] > 175.0F);
    CHECK(cloth_average[0] > cloth_average[2] + 28.0F);
    CHECK(apron_average[0] > apron_average[1] + 45.0F);
    CHECK(apron_average[1] > apron_average[2] + 30.0F);
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
    CHECK(mesh.part_count >= 18);
    CHECK((bounds.max.y - bounds.min.y) > 1.95F);
    CHECK((bounds.max.x - bounds.min.x) > 0.48F);
    CHECK((bounds.max.z - bounds.min.z) > 0.48F);
    CHECK(band_depth_span(mesh, 1.55F, 2.05F) > 0.34F);
    CHECK(band_volume_proxy(mesh, 1.55F, 2.05F) > 0.06F);
    CHECK(band_volume_proxy(mesh, 0.90F, 1.90F) > 0.10F);
    CHECK(all_vertex_attributes_are_bounded(mesh));
    CHECK_FALSE(has_emissive_vertices(mesh));
}

TEST_CASE("villager walking animation keeps articulated body parts connected") {
    const std::array<float, 5> animation_times {{0.0F, 0.22F, 0.48F, 0.73F, 1.05F}};

    for (const auto animation_time : animation_times) {
        const CreatureRenderInstance villager {
            CreatureSpecies::Villager,
            {0.0F, 0.0F, 0.0F},
            0.0F,
            animation_time,
            0.0F,
            1.0F,
            0.18F,
            31337U,
            CreatureBehaviorState::Wander,
            CreaturePhase::Day,
            1.0F,
            0.35F,
            0.0F,
        };

        const auto parts = build_creature_parts(villager);
        CAPTURE(animation_time);
        CHECK(max_vertical_gap_at_z(std::span<const CreaturePartInstance>(parts.data(), parts.size()),
                                    0.04F,
                                    0.66F,
                                    -0.11F,
                                    0.08F) < 0.035F);
        CHECK(max_vertical_gap_at_z(std::span<const CreaturePartInstance>(parts.data(), parts.size()),
                                    0.04F,
                                    0.66F,
                                    0.11F,
                                    0.08F) < 0.035F);
        CHECK(max_vertical_gap_at_z(std::span<const CreaturePartInstance>(parts.data(), parts.size()),
                                    1.43F,
                                    1.58F,
                                    0.0F,
                                    0.10F) < 0.025F);
    }
}

TEST_CASE("day animal walking animation keeps quadruped legs attached to the body") {
    struct LegProbe {
        CreatureSpecies species;
        float front_x;
        float rear_x;
        float front_z;
        float rear_z;
        float max_y;
    };

    const std::array<LegProbe, 3> probes {{
        {CreatureSpecies::Pig, 0.18F, -0.22F, 0.14F, 0.15F, 0.70F},
        {CreatureSpecies::Cow, 0.22F, -0.34F, 0.16F, 0.17F, 0.76F},
        {CreatureSpecies::Sheep, 0.16F, -0.24F, 0.14F, 0.15F, 0.72F},
    }};
    const std::array<float, 5> animation_times {{0.0F, 0.22F, 0.48F, 0.73F, 1.05F}};

    for (const auto& probe : probes) {
        for (const auto animation_time : animation_times) {
            const CreatureRenderInstance animal {
                probe.species,
                {0.0F, 0.0F, 0.0F},
                0.0F,
                animation_time,
                0.0F,
                1.0F,
                0.20F,
                42420U,
                CreatureBehaviorState::Wander,
                CreaturePhase::Day,
                1.0F,
                0.25F,
                0.0F,
            };

            const auto parts = build_creature_parts(animal);
            const auto part_span = std::span<const CreaturePartInstance>(parts.data(), parts.size());
            for (const auto side : std::array<float, 2> {-1.0F, 1.0F}) {
                CAPTURE(static_cast<int>(probe.species));
                CAPTURE(animation_time);
                CAPTURE(side);
                CHECK(max_vertical_gap_at_xz(part_span,
                                             0.0F,
                                             probe.max_y,
                                             probe.front_x,
                                             0.18F,
                                             side * probe.front_z,
                                             0.075F) < 0.065F);
                CHECK(max_vertical_gap_at_xz(part_span,
                                             0.0F,
                                             probe.max_y,
                                             probe.rear_x,
                                             0.18F,
                                             side * probe.rear_z,
                                             0.075F) < 0.065F);
            }
        }
    }
}

TEST_CASE("day grazing and resident activity states produce distinct procedural poses") {
    for (const auto species : {CreatureSpecies::Pig, CreatureSpecies::Cow, CreatureSpecies::Sheep}) {
        const CreatureRenderInstance idle_animal {
            species,
            {0.0F, 0.0F, 0.0F},
            0.0F,
            0.55F,
            0.0F,
            1.0F,
            0.15F,
            42420U,
            CreatureBehaviorState::Idle,
            CreaturePhase::Day,
            0.10F,
            0.25F,
            0.0F,
        };
        auto graze_animal = idle_animal;
        graze_animal.behavior_state = CreatureBehaviorState::Graze;
        graze_animal.motion_amount = 0.20F;
        graze_animal.gaze_weight = 0.68F;

        const auto idle_mesh = build_creature_mesh(idle_animal);
        const auto graze_mesh = build_creature_mesh(graze_animal);
        CAPTURE(static_cast<int>(species));
        CHECK_FALSE(meshes_match_exactly(idle_mesh, graze_mesh));
        CHECK(max_position_delta(idle_mesh, graze_mesh) > 0.01F);
        CHECK(all_vertex_attributes_are_bounded(graze_mesh));
    }

    const CreatureRenderInstance idle_villager {
        CreatureSpecies::Villager,
        {0.0F, 0.0F, 0.0F},
        0.0F,
        0.65F,
        0.0F,
        1.0F,
        0.20F,
        31337U,
        CreatureBehaviorState::Idle,
        CreaturePhase::Day,
        0.24F,
        0.34F,
        0.0F,
    };
    const auto idle_mesh = build_creature_mesh(idle_villager);
    for (const auto state : {
             CreatureBehaviorState::Graze,
             CreatureBehaviorState::Work,
             CreatureBehaviorState::Socialize,
             CreatureBehaviorState::Sleep,
             CreatureBehaviorState::ReturnHome,
         }) {
        auto active_villager = idle_villager;
        active_villager.behavior_state = state;
        active_villager.motion_amount = state == CreatureBehaviorState::ReturnHome ? 0.70F : 0.18F;
        active_villager.attack_amount = state == CreatureBehaviorState::Work ? 0.58F : 0.34F;

        const auto active_mesh = build_creature_mesh(active_villager);
        CAPTURE(static_cast<int>(state));
        CHECK_FALSE(meshes_match_exactly(idle_mesh, active_mesh));
        CHECK_FALSE(active_mesh.empty());
        CHECK(all_vertex_attributes_are_bounded(active_mesh));
    }
}

TEST_CASE("creature geometry transforms night animals into one giant red eyed skeletal monster") {
    CreatureMeshData reference_night_mesh {};

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
        const auto night_height = night_bounds.max.y - night_bounds.min.y;

        CAPTURE(static_cast<int>(species));
        CHECK_FALSE(day_mesh.empty());
        CHECK_FALSE(night_mesh.empty());
        CHECK(night_mesh.part_count > day_mesh.part_count);
        CHECK(night_mesh.vertices.size() > day_mesh.vertices.size());
        CHECK(night_height > 4.45F);
        CHECK(night_bounds.max.y > 4.35F);
        CHECK(night_height > (day_bounds.max.y - day_bounds.min.y) + 2.55F);
        CHECK(band_depth_span(night_mesh, 0.22F, 1.12F) > 0.88F);
        CHECK(band_volume_proxy(night_mesh, 2.02F, 3.12F) > 0.30F);
        CHECK(max_material_class(day_mesh) < 0.70F);
        CHECK(max_material_class(night_mesh) > 0.85F);
        CHECK(has_emissive_vertices(night_mesh));
        CHECK(all_vertex_attributes_are_bounded(day_mesh));
        CHECK(all_vertex_attributes_are_bounded(night_mesh));

        if (reference_night_mesh.empty()) {
            reference_night_mesh = night_mesh;
        } else {
            CHECK(meshes_match_exactly(reference_night_mesh, night_mesh));
        }
    }
}

TEST_CASE("night creature silhouettes keep the shared photo reference proportions") {
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
        const auto bounds = mesh_bounds(night_mesh);
        const auto height = bounds.max.y - bounds.min.y;
        CAPTURE(static_cast<int>(species));
        CHECK(height > 4.45F);
        CHECK((bounds.max.x - bounds.min.x) > 1.05F);
        CHECK((bounds.max.z - bounds.min.z) > 1.05F);
        CHECK(band_volume_proxy(night_mesh, 2.00F, 3.10F) > 0.30F);
        CHECK(band_depth_span(night_mesh, 2.00F, 3.10F) > 0.58F);
        CHECK(band_depth_span(night_mesh, 0.18F, 1.08F) > 0.88F);
        CHECK(band_volume_proxy(night_mesh, 3.38F, 4.25F) > 0.15F);
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
