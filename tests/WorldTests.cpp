#include "app/PerformanceReport.h"
#include "gameplay/StartingVillage.h"
#include "world/World.h"
#include "world/BlockVisuals.h"
#include "world/ChunkMesher.h"
#include "world/Environment.h"
#include "world/WorldGenerator.h"

#include "TestUtils.h"

#include <glm/geometric.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <doctest/doctest.h>
#include <limits>
#include <set>
#include <stdexcept>
#include <utility>

namespace valcraft {

namespace {

struct BiomeVegetationDensity {
    int sampled_columns = 0;
    int tree_candidate_columns = 0;
    int decoration_candidate_columns = 0;
    int tree_columns = 0;
    int decoration_columns = 0;
};

auto is_tree_trunk(BlockId block_id) noexcept -> bool {
    return block_id == to_block_id(BlockType::Wood) || block_id == to_block_id(BlockType::PineWood);
}

auto is_green_decoration(BlockId block_id) noexcept -> bool {
    return block_id == to_block_id(BlockType::TallGrass) ||
           block_id == to_block_id(BlockType::RedFlower) ||
           block_id == to_block_id(BlockType::YellowFlower);
}

auto density_percent(int count, int total) noexcept -> float {
    if (total <= 0) {
        return 0.0F;
    }
    return static_cast<float>(count) * 100.0F / static_cast<float>(total);
}

constexpr std::array<BlockType, 5> kResourceOreTypes {{
    BlockType::CoalOre,
    BlockType::IronOre,
    BlockType::GoldOre,
    BlockType::DiamondOre,
    BlockType::MetallicAlloyOre,
}};

auto resource_ore_index(BlockId block_id) noexcept -> int {
    switch (static_cast<BlockType>(block_item_id(block_id))) {
    case BlockType::CoalOre:
        return 0;
    case BlockType::IronOre:
        return 1;
    case BlockType::GoldOre:
        return 2;
    case BlockType::DiamondOre:
        return 3;
    case BlockType::MetallicAlloyOre:
        return 4;
    default:
        return -1;
    }
}

auto resource_ore_max_y(BlockId block_id) noexcept -> int {
    switch (static_cast<BlockType>(block_item_id(block_id))) {
    case BlockType::CoalOre:
        return 82;
    case BlockType::IronOre:
        return 62;
    case BlockType::GoldOre:
        return 36;
    case BlockType::DiamondOre:
        return 22;
    case BlockType::MetallicAlloyOre:
        return 14;
    default:
        return kWorldMaxY;
    }
}

auto replacement_block_for(BlockId generated_block) noexcept -> BlockId {
    const auto stone = to_block_id(BlockType::Stone);
    const auto cobblestone = to_block_id(BlockType::Cobblestone);
    return generated_block == stone ? cobblestone : stone;
}

auto finite_vec3(const glm::vec3& value) noexcept -> bool {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

auto vec3_components_at_least(const glm::vec3& value, float minimum) noexcept -> bool {
    return value.x >= minimum && value.y >= minimum && value.z >= minimum;
}

auto terrain_surface_height(const WorldGenerator& generator, int world_x, int world_z) noexcept -> int {
    for (int y = kWorldMaxY; y >= kWorldMinY; --y) {
        const auto block = generator.sample_block(world_x, y, world_z);
        if (block != to_block_id(BlockType::Air) && block != to_block_id(BlockType::Water)) {
            return y;
        }
    }
    return kWorldMinY - 1;
}

auto building_wall_height(const StartingVillageBuilding& building) noexcept -> int {
    switch (building.role) {
    case VillageBuildingRole::House:
        return (building.variant_seed % 4U) == 0U ? 4 : 3;
    case VillageBuildingRole::Workshop:
    case VillageBuildingRole::Storehouse:
    case VillageBuildingRole::Lodge:
        return 4;
    }
    return 4;
}

auto roof_surface_y_at_wall(const StartingVillageBuilding& building, int x, int z) noexcept -> int {
    const auto roof_base_y = building.base_y + building_wall_height(building) + 1;
    const auto eave_min_x = building.min_x - 1;
    const auto eave_max_x = building.max_x + 1;
    const auto eave_min_z = building.min_z - 1;
    const auto eave_max_z = building.max_z + 1;
    if (building.facing == VillageFacing::North || building.facing == VillageFacing::South) {
        return roof_base_y + std::min(z - eave_min_z, eave_max_z - z);
    }
    return roof_base_y + std::min(x - eave_min_x, eave_max_x - x);
}

auto doorway_world_cell(const StartingVillageBuilding& building) noexcept -> BlockCoord {
    switch (building.facing) {
    case VillageFacing::South:
        return {building.door_x, building.base_y + 1, building.max_z};
    case VillageFacing::North:
        return {building.door_x, building.base_y + 1, building.min_z};
    case VillageFacing::East:
        return {building.max_x, building.base_y + 1, building.door_z};
    case VillageFacing::West:
    default:
        return {building.min_x, building.base_y + 1, building.door_z};
    }
}

} // namespace

TEST_CASE("air uses terrain as the visual fallback material") {
    CHECK(block_visual_material(to_block_id(BlockType::Air)) == BlockVisualMaterial::Terrain);
}

TEST_CASE("terrain surface sampling matches block and water generation") {
    WorldGenerator generator(424242);
    constexpr std::array<std::pair<int, int>, 5> sample_columns {{
        {0, 0},
        {-32, 24},
        {48, -40},
        {78, 90},
        {-96, -56},
    }};

    for (const auto& [world_x, world_z] : sample_columns) {
        const auto surface = generator.sample_surface(world_x, world_z);
        CAPTURE(world_x);
        CAPTURE(world_z);
        CHECK(surface.surface_height == terrain_surface_height(generator, world_x, world_z));
        CHECK(surface.surface_block == generator.sample_block(world_x, surface.surface_height, world_z));
        if (surface.water_level > surface.surface_height) {
            CHECK(generator.sample_water_state(world_x, surface.water_level, world_z) != 0);
        } else {
            CHECK(generator.sample_water_state(world_x, surface.surface_height + 1, world_z) == 0);
        }
    }
}

TEST_CASE("world generator move operations preserve deterministic sampling") {
    WorldGenerator source(9191);
    const auto expected_surface = source.sample_surface(12, -34);
    const auto expected_block = source.sample_block(12, expected_surface.surface_height, -34);
    const auto expected_water = source.sample_water_state(12, expected_surface.water_level, -34);

    WorldGenerator moved(std::move(source));
    CHECK(moved.seed() == 9191);
    CHECK(moved.sample_surface(12, -34).surface_height == expected_surface.surface_height);
    CHECK(moved.sample_surface(12, -34).surface_block == expected_surface.surface_block);
    CHECK(moved.sample_block(12, expected_surface.surface_height, -34) == expected_block);
    CHECK(moved.sample_water_state(12, expected_surface.water_level, -34) == expected_water);

    WorldGenerator assigned(7);
    assigned = WorldGenerator(9191);
    CHECK(assigned.seed() == 9191);
    CHECK(assigned.sample_surface(12, -34).surface_height == expected_surface.surface_height);
    CHECK(assigned.sample_surface(12, -34).surface_block == expected_surface.surface_block);
    CHECK(assigned.sample_block(12, expected_surface.surface_height, -34) == expected_block);
    CHECK(assigned.sample_water_state(12, expected_surface.water_level, -34) == expected_water);
}

TEST_CASE("resource ores generate underground with low deterministic densities") {
    WorldGenerator generator(424242);
    std::array<int, kResourceOreTypes.size()> ore_counts {};
    int solid_block_count = 0;
    int ore_block_count = 0;

    for (int chunk_z = -3; chunk_z <= 3; ++chunk_z) {
        for (int chunk_x = -3; chunk_x <= 3; ++chunk_x) {
            for (int local_z = 0; local_z < kChunkSizeZ; ++local_z) {
                for (int local_x = 0; local_x < kChunkSizeX; ++local_x) {
                    const auto world_x = chunk_x * kChunkSizeX + local_x;
                    const auto world_z = chunk_z * kChunkSizeZ + local_z;
                    const auto surface = generator.sample_surface(world_x, world_z);
                    const auto max_scan_y = std::min(surface.surface_height - 1, 90);
                    for (int y = kWorldMinY; y <= max_scan_y; ++y) {
                        const auto block = generator.sample_block(world_x, y, world_z);
                        if (block != to_block_id(BlockType::Air) && block != to_block_id(BlockType::Water)) {
                            ++solid_block_count;
                        }

                        const auto ore_index = resource_ore_index(block);
                        if (ore_index < 0) {
                            continue;
                        }

                        ++ore_counts[static_cast<std::size_t>(ore_index)];
                        ++ore_block_count;
                        CAPTURE(world_x);
                        CAPTURE(y);
                        CAPTURE(world_z);
                        CAPTURE(ore_index);
                        CHECK(y <= surface.surface_height - 5);
                        CHECK(y <= resource_ore_max_y(block));
                    }
                }
            }
        }
    }

    REQUIRE(solid_block_count > 0);
    for (std::size_t index = 0; index < ore_counts.size(); ++index) {
        CAPTURE(index);
        CHECK(ore_counts[index] > 0);
    }

    CHECK(ore_block_count * 100 < solid_block_count * 3);
    CHECK(ore_counts[0] > ore_counts[1]);
    CHECK(ore_counts[1] > ore_counts[2]);
    CHECK(ore_counts[2] > ore_counts[3]);
    CHECK(ore_counts[3] > ore_counts[4]);
}

TEST_CASE("generated chunks keep resource ore blocks identical to direct sampling") {
    WorldGenerator generator(515151);
    struct FoundOre {
        bool found = false;
        BlockCoord coord {};
        BlockId block = to_block_id(BlockType::Air);
    };
    FoundOre found {};

    for (int world_z = -96; world_z <= 96 && !found.found; ++world_z) {
        for (int world_x = -96; world_x <= 96 && !found.found; ++world_x) {
            const auto surface = generator.sample_surface(world_x, world_z);
            for (int y = kWorldMinY; y <= std::min(surface.surface_height - 5, 70); ++y) {
                const auto block = generator.sample_block(world_x, y, world_z);
                if (!is_resource_ore(block)) {
                    continue;
                }
                found = {true, {world_x, y, world_z}, block};
                break;
            }
        }
    }

    REQUIRE(found.found);
    const ChunkCoord coord {
        World::floor_div(found.coord.x, kChunkSizeX),
        World::floor_div(found.coord.z, kChunkSizeZ),
    };
    Chunk chunk(coord);
    generator.generate_chunk(chunk);

    const auto local_x = World::positive_mod(found.coord.x, kChunkSizeX);
    const auto local_z = World::positive_mod(found.coord.z, kChunkSizeZ);
    CHECK(chunk.get_local(local_x, found.coord.y, local_z) == found.block);
    CHECK(chunk.get_local(local_x, found.coord.y, local_z) == generator.sample_block(found.coord.x, found.coord.y, found.coord.z));
}

TEST_CASE("chunk stores and retrieves local blocks") {
    Chunk chunk({2, -1});
    chunk.fill(to_block_id(BlockType::Air));

    chunk.set_local(3, 40, 5, to_block_id(BlockType::Stone));
    CHECK(chunk.get_local(3, 40, 5) == to_block_id(BlockType::Stone));
    CHECK(chunk.get_local(0, 0, 0) == to_block_id(BlockType::Air));
}

TEST_CASE("chunk get_local throws when coordinates are out of bounds") {
    Chunk chunk({0, 0});
    CHECK_THROWS_AS(([&]() { static_cast<void>(chunk.get_local(-1, 0, 0)); }()), std::out_of_range);
    CHECK_THROWS_AS(([&]() { static_cast<void>(chunk.get_local(0, kChunkHeight, 0)); }()), std::out_of_range);
    CHECK_THROWS_AS(([&]() { static_cast<void>(chunk.get_local(0, 0, kChunkSizeZ)); }()), std::out_of_range);
}

TEST_CASE("chunk set_local throws when coordinates are out of bounds") {
    Chunk chunk({0, 0});
    CHECK_THROWS_AS(chunk.set_local(kChunkSizeX, 0, 0, to_block_id(BlockType::Stone)), std::out_of_range);
    CHECK_THROWS_AS(chunk.set_local(0, -1, 0, to_block_id(BlockType::Stone)), std::out_of_range);
    CHECK_THROWS_AS(chunk.set_local(0, 0, -1, to_block_id(BlockType::Stone)), std::out_of_range);
}

TEST_CASE("chunk tracks meshable y bounds as blocks are added and removed") {
    Chunk chunk({0, 0});
    CHECK_FALSE(chunk.has_meshable_blocks());
    CHECK(chunk.max_mesh_y() < chunk.min_mesh_y());

    chunk.set_local(1, 12, 1, to_block_id(BlockType::Stone));
    CHECK(chunk.has_meshable_blocks());
    CHECK(chunk.min_mesh_y() == 12);
    CHECK(chunk.max_mesh_y() == 12);

    chunk.set_local(2, 27, 2, to_block_id(BlockType::Torch));
    CHECK(chunk.min_mesh_y() == 12);
    CHECK(chunk.max_mesh_y() == 27);

    chunk.set_local(1, 12, 1, to_block_id(BlockType::Air));
    CHECK(chunk.has_meshable_blocks());
    CHECK(chunk.min_mesh_y() == 27);
    CHECK(chunk.max_mesh_y() == 27);

    chunk.set_local(2, 27, 2, to_block_id(BlockType::Air));
    CHECK_FALSE(chunk.has_meshable_blocks());
    CHECK(chunk.max_mesh_y() < chunk.min_mesh_y());
}

TEST_CASE("chunk dirties only the touched mesh section and adjacent section seams") {
    Chunk chunk({0, 0});
    chunk.fill(to_block_id(BlockType::Air));
    chunk.clear_dirty();

    chunk.set_local(1, 8, 1, to_block_id(BlockType::Stone));
    CHECK(chunk.dirty_section_count() == 1);
    CHECK(chunk.is_section_dirty(0));
    CHECK_FALSE(chunk.is_section_dirty(1));

    chunk.clear_dirty();
    chunk.set_local(1, 15, 1, to_block_id(BlockType::Stone));
    CHECK(chunk.dirty_section_count() == 2);
    CHECK(chunk.is_section_dirty(0));
    CHECK(chunk.is_section_dirty(1));

    chunk.clear_dirty();
    chunk.set_local(1, 16, 1, to_block_id(BlockType::Stone));
    CHECK(chunk.dirty_section_count() == 2);
    CHECK(chunk.is_section_dirty(0));
    CHECK(chunk.is_section_dirty(1));
}

TEST_CASE("chunk caches surface height per column") {
    Chunk chunk({0, 0});
    chunk.fill(to_block_id(BlockType::Air));

    CHECK(chunk.surface_height_local(2, 3) == 0);
    chunk.set_local(2, 4, 3, to_block_id(BlockType::Stone));
    CHECK(chunk.surface_height_local(2, 3) == 4);
    chunk.set_local(2, 6, 3, to_block_id(BlockType::TallGrass));
    CHECK(chunk.surface_height_local(2, 3) == 4);
    chunk.set_local(2, 7, 3, to_block_id(BlockType::Stone));
    CHECK(chunk.surface_height_local(2, 3) == 7);
    chunk.set_local(2, 7, 3, to_block_id(BlockType::Air));
    CHECK(chunk.surface_height_local(2, 3) == 4);
}

TEST_CASE("world converts negative coordinates into chunk and local positions") {
    World world(1234, 1);

    const ChunkCoord expected_chunk_a {-1, -1};
    const ChunkCoord expected_chunk_b {-1, -1};
    const ChunkCoord expected_chunk_c {-2, 1};
    const BlockCoord expected_local_a {15, 12, 15};
    const BlockCoord expected_local_b {15, 9, 15};

    CHECK(world.world_to_chunk(-1, -1) == expected_chunk_a);
    CHECK(world.world_to_chunk(-16, -16) == expected_chunk_b);
    CHECK(world.world_to_chunk(-17, 31) == expected_chunk_c);
    CHECK(world.world_to_local(-1, 12, -1) == expected_local_a);
    CHECK(world.world_to_local(-17, 9, 31) == expected_local_b);
}

TEST_CASE("world local and chunk conversions round-trip to original coordinates") {
    World world(4321, 1);
    const BlockCoord world_position {-17, 22, 31};
    const auto chunk = world.world_to_chunk(world_position.x, world_position.z);
    const auto local = world.world_to_local(world_position.x, world_position.y, world_position.z);
    const auto reconstructed = world.local_to_world(chunk, local);

    CHECK(reconstructed == world_position);
}

TEST_CASE("spawn preload resolves overlapping lighting jobs for a 3x3 chunk area") {
    World world(1337, 1);
    const glm::vec3 focus {0.5F, 70.0F, 0.5F};
    const auto center = world.world_to_chunk(0, 0);

    for (int dz = -1; dz <= 1; ++dz) {
        for (int dx = -1; dx <= 1; ++dx) {
            world.ensure_chunk_loaded({center.x + dx, center.z + dz});
        }
    }
    (void)world.update_streaming(focus);

    WorldWorkBudget budget {};
    budget.chunk_generation_budget = 64U;
    budget.fluid_cell_budget = 8192U;
    budget.mesh_rebuild_budget = 64U;
    budget.light_node_budget = std::numeric_limits<std::size_t>::max() / 8U;
    budget.max_generation_ms = std::numeric_limits<double>::infinity();
    budget.max_fluid_ms = std::numeric_limits<double>::infinity();
    budget.max_lighting_ms = std::numeric_limits<double>::infinity();
    budget.max_meshing_ms = std::numeric_limits<double>::infinity();

    for (int iteration = 0; iteration < 8 && !world.are_chunks_ready(focus, 1); ++iteration) {
        (void)world.process_pending_work(budget);
    }

    CHECK(world.are_chunks_ready(focus, 1));
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dx = -1; dx <= 1; ++dx) {
            const ChunkCoord coord {center.x + dx, center.z + dz};
            const auto* chunk = world.find_chunk(coord);
            REQUIRE(chunk != nullptr);
            CHECK(world.mesh_revision(coord) > 0);
            CHECK_FALSE(chunk->is_dirty());
            CHECK_FALSE(chunk->is_lighting_dirty());
        }
    }
}

TEST_CASE("world get_block outside valid Y returns air") {
    World world(91, 1);
    CHECK(world.get_block(0, -1, 0) == to_block_id(BlockType::Air));
    CHECK(world.get_block(0, kChunkHeight, 0) == to_block_id(BlockType::Air));
}

TEST_CASE("world set_block outside valid Y is a no-op") {
    World world(92, 1);
    test::make_chunk_empty(world, {0, 0});

    world.set_block(1, -1, 1, to_block_id(BlockType::Stone));
    world.set_block(1, kChunkHeight, 1, to_block_id(BlockType::Stone));

    CHECK(world.get_block(1, 0, 1) == to_block_id(BlockType::Air));
    CHECK(world.get_block(1, kWorldMaxY, 1) == to_block_id(BlockType::Air));
}

TEST_CASE("modified chunks survive unload and reload within the same session") {
    World world(12345, 0);
    const ChunkCoord origin {0, 0};
    constexpr BlockCoord target {1, kWorldMaxY, 1};
    const auto replacement_block =
        replacement_block_for(world.peek_block_or_generated(target.x, target.y, target.z));

    world.update_streaming({0.5F, 70.0F, 0.5F});
    world.set_block(target.x, target.y, target.z, replacement_block);
    test::flush_pending_work(world);

    CHECK(world.get_block(target.x, target.y, target.z) == replacement_block);

    world.update_streaming({static_cast<float>(kChunkSizeX * 3) + 0.5F, 70.0F, 0.5F});
    test::flush_pending_work(world);

    CHECK(world.find_chunk(origin) == nullptr);
    CHECK(world.get_block(target.x, target.y, target.z) == replacement_block);

    world.update_streaming({0.5F, 70.0F, 0.5F});
    test::flush_pending_work(world);

    REQUIRE(world.find_chunk(origin) != nullptr);
    CHECK(world.get_block(target.x, target.y, target.z) == replacement_block);
}

TEST_CASE("restoring a block to its generated value clears the chunk override before unload") {
    std::array<BlockId, kChunkVolume> generator_blocks {};
    const ChunkCoord origin {0, 0};
    World generator_world(54321, 0);
    for (int y = 0; y < kChunkHeight; ++y) {
        for (int z = 0; z < kChunkSizeZ; ++z) {
            for (int x = 0; x < kChunkSizeX; ++x) {
                const auto index = static_cast<std::size_t>((y * kChunkSizeZ + z) * kChunkSizeX + x);
                generator_blocks[index] = generator_world.peek_block_or_generated(x, y, z);
            }
        }
    }

    World world(54321, 0);

    world.update_streaming({0.5F, 70.0F, 0.5F});
    test::flush_pending_work(world);

    auto* chunk = world.find_chunk(origin);
    REQUIRE(chunk != nullptr);
    chunk->copy_blocks_from(generator_blocks.data(), generator_blocks.size());
    chunk->clear_lighting();

    constexpr int target_y = 1;
    const auto generated_block = chunk->get_local(1, target_y, 1);
    const auto replacement_block =
        generated_block == to_block_id(BlockType::Stone) ? to_block_id(BlockType::Cobblestone) : to_block_id(BlockType::Stone);

    world.set_block(1, target_y, 1, replacement_block);
    test::flush_pending_work(world);

    CHECK(world.get_block(1, target_y, 1) == replacement_block);
    CHECK(world.modified_chunk_snapshots().size() == 1);

    world.set_block(1, target_y, 1, generated_block);
    test::flush_pending_work(world);

    CHECK(world.get_block(1, target_y, 1) == generated_block);
    CHECK(world.modified_chunk_snapshots().empty());

    world.update_streaming({static_cast<float>(kChunkSizeX * 3) + 0.5F, 70.0F, 0.5F});
    test::flush_pending_work(world);

    CHECK(world.find_chunk(origin) == nullptr);
    CHECK(world.modified_chunk_snapshots().empty());

    world.update_streaming({0.5F, 70.0F, 0.5F});
    test::flush_pending_work(world);

    REQUIRE(world.find_chunk(origin) != nullptr);
    CHECK(world.get_block(1, target_y, 1) == generated_block);
    CHECK(world.modified_chunk_snapshots().empty());
}

TEST_CASE("world chunk snapshots round-trip modified chunks into a fresh world") {
    World source_world(24680, 1);
    constexpr BlockCoord first_target {2, kWorldMaxY, 3};
    constexpr BlockCoord second_target {17, kWorldMaxY, 4};
    const auto first_replacement =
        replacement_block_for(source_world.peek_block_or_generated(first_target.x, first_target.y, first_target.z));
    constexpr auto second_replacement = to_block_id(BlockType::Torch);

    source_world.set_block(first_target.x, first_target.y, first_target.z, first_replacement);
    source_world.set_block(second_target.x, second_target.y, second_target.z, second_replacement);
    test::flush_pending_work(source_world);

    const auto snapshots = source_world.modified_chunk_snapshots();
    CHECK(snapshots.size() == 2);

    World restored_world(24680, 1);
    restored_world.replace_chunk_snapshots(snapshots);

    CHECK(restored_world.get_block(first_target.x, first_target.y, first_target.z) == first_replacement);
    CHECK(restored_world.get_block(second_target.x, second_target.y, second_target.z) == second_replacement);

    restored_world.ensure_chunk_loaded({0, 0});
    restored_world.ensure_chunk_loaded({1, 0});
    test::flush_pending_work(restored_world);

    CHECK(restored_world.get_block(first_target.x, first_target.y, first_target.z) == first_replacement);
    CHECK(restored_world.get_block(second_target.x, second_target.y, second_target.z) == second_replacement);
    CHECK(restored_world.modified_chunk_snapshots().size() == 2);
}

TEST_CASE("starting village generator builds a playable procedural spawn hub") {
    constexpr int kVillageSeed = 424242;
    StartingVillageGenerator generator(kVillageSeed);
    WorldGenerator terrain_generator(kVillageSeed);
    const auto layout = generator.build_layout();

    REQUIRE(layout.buildings.size() >= 10);
    REQUIRE(layout.residents.size() == layout.buildings.size());
    CHECK(layout.base_y >= kSeaLevel + 2);
    CHECK(layout.player_spawn.y > static_cast<float>(layout.base_y));
    CHECK((layout.max_x - layout.min_x) >= 70);
    CHECK((layout.max_z - layout.min_z) >= 60);
    CHECK(std::all_of(layout.residents.begin(), layout.residents.end(), [](const CreatureSpawnAnchor& resident) {
        return resident.species == CreatureSpecies::Villager;
    }));
    CHECK(std::all_of(layout.residents.begin(), layout.residents.end(), [](const CreatureSpawnAnchor& resident) {
        return resident.patrol_point_count == static_cast<std::uint8_t>(kCreatureResidentPatrolPointCount) &&
               resident.roam_radius >= 18.0F;
    }));

    World world(kVillageSeed, 4);
    generator.apply(world, layout);
    test::flush_pending_work(world);

    const auto spawn_x = static_cast<int>(std::floor(layout.player_spawn.x));
    const auto spawn_z = static_cast<int>(std::floor(layout.player_spawn.z));
    int road_columns = 0;
    int glass_blocks = 0;
    int antique_wall_blocks = 0;
    int timber_wall_blocks = 0;
    int portico_column_blocks = 0;
    for (int z = layout.min_z; z <= layout.max_z; ++z) {
        for (int x = layout.min_x; x <= layout.max_x; ++x) {
            const auto ground_block = world.get_block(x, layout.base_y, z);
            road_columns +=
                ground_block == to_block_id(BlockType::Cobblestone) ||
                ground_block == to_block_id(BlockType::Gravel) ||
                ground_block == to_block_id(BlockType::MossyStone) ? 1 : 0;
            for (int y = layout.base_y + 1; y <= layout.base_y + 6; ++y) {
                glass_blocks += world.get_block(x, y, z) == to_block_id(BlockType::Glass) ? 1 : 0;
            }
        }
    }

    for (const auto& building : layout.buildings) {
        const auto wall_height = building_wall_height(building);
        const auto forward = [&]() {
            switch (building.facing) {
            case VillageFacing::South:
                return std::pair<int, int> {0, 1};
            case VillageFacing::North:
                return std::pair<int, int> {0, -1};
            case VillageFacing::East:
                return std::pair<int, int> {1, 0};
            case VillageFacing::West:
            default:
                return std::pair<int, int> {-1, 0};
            }
        }();
        const auto right = [&]() {
            switch (building.facing) {
            case VillageFacing::South:
                return std::pair<int, int> {-1, 0};
            case VillageFacing::North:
                return std::pair<int, int> {1, 0};
            case VillageFacing::East:
                return std::pair<int, int> {0, 1};
            case VillageFacing::West:
            default:
                return std::pair<int, int> {0, -1};
            }
        }();

        for (int z = building.min_z; z <= building.max_z; ++z) {
            for (int x = building.min_x; x <= building.max_x; ++x) {
                const auto edge = x == building.min_x || x == building.max_x || z == building.min_z || z == building.max_z;
                if (!edge) {
                    continue;
                }
                for (int y = building.base_y + 1; y <= building.base_y + wall_height; ++y) {
                    const auto block = world.get_block(x, y, z);
                    antique_wall_blocks +=
                        block == to_block_id(BlockType::Sand) ||
                        block == to_block_id(BlockType::Stone) ||
                        block == to_block_id(BlockType::Cobblestone) ||
                        block == to_block_id(BlockType::MossyStone) ? 1 : 0;
                    timber_wall_blocks +=
                        block == to_block_id(BlockType::Wood) ||
                        block == to_block_id(BlockType::PineWood) ||
                        block == to_block_id(BlockType::Planks) ? 1 : 0;
                }
            }
        }

        for (int offset = -5; offset <= 5; ++offset) {
            if (std::abs(offset) <= 1) {
                continue;
            }
            const auto column_x = building.door_x + right.first * offset;
            const auto column_z = building.door_z + right.second * offset;
            const auto column_base_block = world.get_block(column_x, building.base_y + 1, column_z);
            if (column_base_block != to_block_id(BlockType::Stone) &&
                column_base_block != to_block_id(BlockType::Cobblestone) &&
                column_base_block != to_block_id(BlockType::MossyStone)) {
                continue;
            }
            for (int y = building.base_y + 1; y <= building.base_y + wall_height; ++y) {
                const auto block = world.get_block(column_x, y, column_z);
                portico_column_blocks +=
                    block == to_block_id(BlockType::Stone) ||
                    block == to_block_id(BlockType::Cobblestone) ||
                    block == to_block_id(BlockType::MossyStone) ? 1 : 0;
            }
        }

        CHECK(world.get_block(building.door_x + forward.first, building.base_y, building.door_z + forward.second) == to_block_id(BlockType::Stone));
    }

    CHECK_FALSE(world.modified_chunk_snapshots().empty());
    CHECK(world.has_water(layout.center_x, layout.base_y, layout.center_z));
    CHECK(is_block_collidable(world.get_block(spawn_x, layout.base_y, spawn_z)));
    CHECK(world.get_block(spawn_x, layout.base_y + 1, spawn_z) == to_block_id(BlockType::Air));
    CHECK(world.get_block(spawn_x, layout.base_y + 2, spawn_z) == to_block_id(BlockType::Air));
    CHECK(road_columns > 350);
    CHECK(glass_blocks > 40);
    CHECK(antique_wall_blocks > timber_wall_blocks * 6);
    CHECK(portico_column_blocks >= static_cast<int>(layout.buildings.size()) * 6);

    const auto corner_surface_matches_generated = [&](int world_x, int world_z) {
        const auto generated_surface_y = terrain_surface_height(terrain_generator, world_x, world_z);
        const auto world_surface_y = world.surface_height(world_x, world_z);
        return world_surface_y == generated_surface_y &&
               world.get_block(world_x, world_surface_y, world_z) ==
                   terrain_generator.sample_block(world_x, generated_surface_y, world_z);
    };
    const std::array<std::pair<int, int>, 4> outer_corners {{
        {layout.min_x, layout.min_z},
        {layout.min_x, layout.max_z},
        {layout.max_x, layout.min_z},
        {layout.max_x, layout.max_z},
    }};
    const auto preserved_corner_columns = static_cast<int>(std::count_if(outer_corners.begin(), outer_corners.end(), [&](const auto& corner) {
        return corner_surface_matches_generated(corner.first, corner.second);
    }));
    CHECK(preserved_corner_columns >= 3);

    for (const auto& building : layout.buildings) {
        BlockCoord doorway {};
        switch (building.facing) {
        case VillageFacing::South:
            doorway = {building.door_x, building.base_y + 1, building.max_z};
            break;
        case VillageFacing::North:
            doorway = {building.door_x, building.base_y + 1, building.min_z};
            break;
        case VillageFacing::East:
            doorway = {building.max_x, building.base_y + 1, building.door_z};
            break;
        case VillageFacing::West:
            doorway = {building.min_x, building.base_y + 1, building.door_z};
            break;
        }

        CHECK(world.get_block(doorway.x, doorway.y, doorway.z) == to_block_id(BlockType::Air));
        CHECK(world.get_block(doorway.x, doorway.y + 1, doorway.z) == to_block_id(BlockType::Air));
        CHECK(is_block_collidable(world.get_block(building.interior_x, building.base_y, building.interior_z)));
        CHECK(world.get_block(building.interior_x, building.base_y + 1, building.interior_z) == to_block_id(BlockType::Air));
    }
}

TEST_CASE("starting village buildings seal exterior walls up to the roofline") {
    constexpr int kVillageSeed = 424242;
    StartingVillageGenerator generator(kVillageSeed);
    const auto layout = generator.build_layout();

    World world(kVillageSeed, 4);
    generator.apply(world, layout);
    test::flush_pending_work(world);

    for (const auto& building : layout.buildings) {
        const auto doorway = doorway_world_cell(building);
        for (int z = building.min_z; z <= building.max_z; ++z) {
            for (int x = building.min_x; x <= building.max_x; ++x) {
                const auto edge = x == building.min_x || x == building.max_x || z == building.min_z || z == building.max_z;
                if (!edge) {
                    continue;
                }

                const auto wall_roof_y = roof_surface_y_at_wall(building, x, z);
                for (int y = building.base_y + 1; y < wall_roof_y; ++y) {
                    CAPTURE(static_cast<int>(building.role));
                    CAPTURE(static_cast<int>(building.facing));
                    CAPTURE(x);
                    CAPTURE(y);
                    CAPTURE(z);
                    if (x == doorway.x && z == doorway.z && y <= building.base_y + 2) {
                        CHECK(world.get_block(x, y, z) == to_block_id(BlockType::Air));
                    } else {
                        CHECK(world.get_block(x, y, z) != to_block_id(BlockType::Air));
                    }
                }
            }
        }
    }
}

TEST_CASE("torch block properties are non opaque non collidable and emissive") {
    const auto properties = block_properties(to_block_id(BlockType::Torch));

    CHECK_FALSE(properties.opaque);
    CHECK_FALSE(properties.collidable);
    CHECK(properties.mesh_type == BlockMeshType::Torch);
    CHECK(properties.emissive_level == 14);
}

TEST_CASE("decorative flora blocks are replaceable cross meshes and do not count as ground") {
    const auto properties = block_properties(to_block_id(BlockType::TallGrass));

    CHECK_FALSE(properties.opaque);
    CHECK_FALSE(properties.collidable);
    CHECK_FALSE(properties.surface_support);
    CHECK(properties.replaceable);
    CHECK(properties.mesh_type == BlockMeshType::Cross);
}

TEST_CASE("water block properties are translucent replaceable and non collidable") {
    const auto properties = block_properties(to_block_id(BlockType::Water));

    CHECK_FALSE(properties.opaque);
    CHECK_FALSE(properties.collidable);
    CHECK_FALSE(properties.surface_support);
    CHECK(properties.replaceable);
    CHECK(properties.mesh_type == BlockMeshType::Water);
}

TEST_CASE("resource ore blocks are solid placeable terrain resources") {
    for (const auto ore_type : kResourceOreTypes) {
        const auto block_id = to_block_id(ore_type);
        const auto properties = block_properties(block_id);
        CAPTURE(static_cast<int>(ore_type));

        CHECK(is_resource_ore(block_id));
        CHECK_FALSE(is_inventory_only_item(block_id));
        CHECK(is_placeable_item(block_id));
        CHECK(has_block_mesh(block_id));
        CHECK(properties.opaque);
        CHECK(properties.collidable);
        CHECK(properties.surface_support);
        CHECK_FALSE(properties.replaceable);
        CHECK(properties.mesh_type == BlockMeshType::FullCube);
        CHECK(block_visual_material(block_id) == BlockVisualMaterial::Rock);
    }
}

TEST_CASE("block break durations stay coherent across fragile terrain and hard rock") {
    CHECK(block_break_duration_seconds(to_block_id(BlockType::Air)) == doctest::Approx(0.0F));
    CHECK(block_break_duration_seconds(to_block_id(BlockType::Dirt)) == doctest::Approx(0.80F));
    CHECK(block_break_duration_seconds(to_block_id(BlockType::Stone)) == doctest::Approx(1.30F));
    CHECK(block_break_duration_seconds(to_block_id(BlockType::CoalOre)) >
          block_break_duration_seconds(to_block_id(BlockType::Stone)));
    CHECK(block_break_duration_seconds(to_block_id(BlockType::IronOre)) >
          block_break_duration_seconds(to_block_id(BlockType::CoalOre)));
    CHECK(block_break_duration_seconds(to_block_id(BlockType::GoldOre)) >
          block_break_duration_seconds(to_block_id(BlockType::IronOre)));
    CHECK(block_break_duration_seconds(to_block_id(BlockType::DiamondOre)) >
          block_break_duration_seconds(to_block_id(BlockType::GoldOre)));
    CHECK(block_break_duration_seconds(to_block_id(BlockType::MetallicAlloyOre)) >
          block_break_duration_seconds(to_block_id(BlockType::DiamondOre)));
    CHECK(block_break_duration_seconds(to_block_id(BlockType::Cobblestone)) >
          block_break_duration_seconds(to_block_id(BlockType::Stone)));
    CHECK(block_break_duration_seconds(to_block_id(BlockType::TallGrass)) <
          block_break_duration_seconds(to_block_id(BlockType::Dirt)));
    CHECK(block_break_duration_seconds(to_block_id(BlockType::Torch)) <
          block_break_duration_seconds(to_block_id(BlockType::TallGrass)));
    CHECK(is_block_breakable(to_block_id(BlockType::Stone)));
    CHECK_FALSE(is_block_breakable(to_block_id(BlockType::Air)));
}

TEST_CASE("block atlas expands to 128 square pixels and preserves transparent decorative tiles") {
    const auto pixels = build_block_atlas_pixels();
    REQUIRE(pixels.size() == static_cast<std::size_t>(kBlockAtlasSize * kBlockAtlasSize * 4));

    const auto tall_grass_tile = block_atlas_tile(to_block_id(BlockType::TallGrass), BlockVisualFace::Cross);
    const auto tile_origin_x = tall_grass_tile.x * kBlockAtlasTileSize;
    const auto tile_origin_y = tall_grass_tile.y * kBlockAtlasTileSize;
    const auto transparent_alpha_index =
        static_cast<std::size_t>(((tile_origin_y + 0) * kBlockAtlasSize + (tile_origin_x + 0)) * 4 + 3);
    const auto opaque_alpha_index =
        static_cast<std::size_t>(((tile_origin_y + 10) * kBlockAtlasSize + (tile_origin_x + 7)) * 4 + 3);

    CHECK(pixels[transparent_alpha_index] == 0);
    CHECK(pixels[opaque_alpha_index] == 255);
}

TEST_CASE("block atlas includes progressively denser crack tiles for block breaking") {
    const auto pixels = build_block_atlas_pixels();
    REQUIRE(pixels.size() == static_cast<std::size_t>(kBlockAtlasSize * kBlockAtlasSize * 4));

    const auto opaque_texel_count = [&](std::uint8_t stage) {
        const auto tile = block_break_crack_tile(stage);
        const auto origin_x = tile.x * kBlockAtlasTileSize;
        const auto origin_y = tile.y * kBlockAtlasTileSize;
        auto count = 0;
        for (int y = 0; y < kBlockAtlasTileSize; ++y) {
            for (int x = 0; x < kBlockAtlasTileSize; ++x) {
                const auto alpha_index =
                    static_cast<std::size_t>(((origin_y + y) * kBlockAtlasSize + (origin_x + x)) * 4 + 3);
                count += pixels[alpha_index] > 0 ? 1 : 0;
            }
        }
        return count;
    };

    CHECK(kBlockBreakStageCount == 8);
    CHECK(opaque_texel_count(0) > 0);
    CHECK(opaque_texel_count(kBlockBreakStageCount - 1) > opaque_texel_count(0));
    CHECK(opaque_texel_count(3) >= opaque_texel_count(0));
}

TEST_CASE("block atlas includes a translucent water tile") {
    const auto pixels = build_block_atlas_pixels();
    REQUIRE(pixels.size() == static_cast<std::size_t>(kBlockAtlasSize * kBlockAtlasSize * 4));

    const auto water_tile = block_atlas_tile(to_block_id(BlockType::Water), BlockVisualFace::PositiveY);
    const auto sample_x = water_tile.x * kBlockAtlasTileSize + 8;
    const auto sample_y = water_tile.y * kBlockAtlasTileSize + 8;
    const auto alpha_index = static_cast<std::size_t>((sample_y * kBlockAtlasSize + sample_x) * 4 + 3);

    CHECK(pixels[alpha_index] > 0);
    CHECK(pixels[alpha_index] < 255);
}

TEST_CASE("water top face can use a distinct atlas tile from the side faces") {
    const auto water_top_tile = block_atlas_tile(to_block_id(BlockType::Water), BlockVisualFace::PositiveY);

    CHECK(water_top_tile != block_atlas_tile(to_block_id(BlockType::Water), BlockVisualFace::PositiveX));
    CHECK(water_top_tile != block_atlas_tile(to_block_id(BlockType::Water), BlockVisualFace::NegativeX));
    CHECK(water_top_tile != block_atlas_tile(to_block_id(BlockType::Water), BlockVisualFace::PositiveZ));
    CHECK(water_top_tile != block_atlas_tile(to_block_id(BlockType::Water), BlockVisualFace::NegativeZ));
}

TEST_CASE("water top atlas tile wraps seamlessly across both axes") {
    const auto pixels = build_block_atlas_pixels();
    REQUIRE(pixels.size() == static_cast<std::size_t>(kBlockAtlasSize * kBlockAtlasSize * 4));

    const auto water_tile = block_atlas_tile(to_block_id(BlockType::Water), BlockVisualFace::PositiveY);
    const auto sample_pixel = [&](int local_x, int local_y) {
        const auto sample_x = water_tile.x * kBlockAtlasTileSize + local_x;
        const auto sample_y = water_tile.y * kBlockAtlasTileSize + local_y;
        const auto pixel_index = static_cast<std::size_t>((sample_y * kBlockAtlasSize + sample_x) * 4);
        return std::array<int, 4> {
            static_cast<int>(pixels[pixel_index + 0]),
            static_cast<int>(pixels[pixel_index + 1]),
            static_cast<int>(pixels[pixel_index + 2]),
            static_cast<int>(pixels[pixel_index + 3]),
        };
    };

    for (int y = 0; y < kBlockAtlasTileSize; ++y) {
        const auto left = sample_pixel(0, y);
        const auto right = sample_pixel(kBlockAtlasTileSize - 1, y);
        for (std::size_t channel = 0; channel < left.size(); ++channel) {
            const auto difference =
                left[channel] >= right[channel] ? left[channel] - right[channel] : right[channel] - left[channel];
            CAPTURE(y);
            CAPTURE(channel);
            CAPTURE(left[channel]);
            CAPTURE(right[channel]);
            CHECK(difference <= 1);
        }
    }

    for (int x = 0; x < kBlockAtlasTileSize; ++x) {
        const auto top = sample_pixel(x, 0);
        const auto bottom = sample_pixel(x, kBlockAtlasTileSize - 1);
        for (std::size_t channel = 0; channel < top.size(); ++channel) {
            const auto difference =
                top[channel] >= bottom[channel] ? top[channel] - bottom[channel] : bottom[channel] - top[channel];
            CAPTURE(x);
            CAPTURE(channel);
            CAPTURE(top[channel]);
            CAPTURE(bottom[channel]);
            CHECK(difference <= 1);
        }
    }
}

TEST_CASE("torch atlas separates the icon from world faces and keeps a warm readable head") {
    const auto pixels = build_block_atlas_pixels();
    REQUIRE(pixels.size() == static_cast<std::size_t>(kBlockAtlasSize * kBlockAtlasSize * 4));

    const auto icon_tile = block_hotbar_tile(to_block_id(BlockType::Torch));
    const auto side_tile = block_atlas_tile(to_block_id(BlockType::Torch), BlockVisualFace::PositiveX);
    const auto top_tile = block_atlas_tile(to_block_id(BlockType::Torch), BlockVisualFace::PositiveY);
    const auto bottom_tile = block_atlas_tile(to_block_id(BlockType::Torch), BlockVisualFace::NegativeY);

    CHECK(icon_tile != side_tile);
    CHECK(side_tile != top_tile);
    CHECK(side_tile != bottom_tile);
    CHECK(top_tile != bottom_tile);

    const auto sample_alpha = [&](const BlockAtlasTile& tile, int local_x, int local_y) {
        const auto atlas_x = tile.x * kBlockAtlasTileSize + local_x;
        const auto atlas_y = tile.y * kBlockAtlasTileSize + local_y;
        return pixels[static_cast<std::size_t>((atlas_y * kBlockAtlasSize + atlas_x) * 4 + 3)];
    };
    const auto sample_region_average = [&](const BlockAtlasTile& tile, int y_begin, int y_end) {
        std::array<float, 4> sum {0.0F, 0.0F, 0.0F, 0.0F};
        auto sample_count = 0.0F;
        for (int y = y_begin; y < y_end; ++y) {
            for (int x = 0; x < kBlockAtlasTileSize; ++x) {
                const auto atlas_x = tile.x * kBlockAtlasTileSize + x;
                const auto atlas_y = tile.y * kBlockAtlasTileSize + y;
                const auto pixel_index = static_cast<std::size_t>((atlas_y * kBlockAtlasSize + atlas_x) * 4);
                sum[0] += static_cast<float>(pixels[pixel_index + 0]);
                sum[1] += static_cast<float>(pixels[pixel_index + 1]);
                sum[2] += static_cast<float>(pixels[pixel_index + 2]);
                sum[3] += static_cast<float>(pixels[pixel_index + 3]);
                sample_count += 1.0F;
            }
        }
        return std::array<float, 4> {
            sum[0] / sample_count,
            sum[1] / sample_count,
            sum[2] / sample_count,
            sum[3] / sample_count,
        };
    };
    const auto sample_tile_average = [&](const BlockAtlasTile& tile) {
        return sample_region_average(tile, 0, kBlockAtlasTileSize);
    };

    CHECK(sample_alpha(icon_tile, 0, 0) == 0);
    CHECK(sample_alpha(icon_tile, 15, 15) == 0);
    CHECK(sample_alpha(icon_tile, 8, 10) == 255);
    CHECK(sample_alpha(icon_tile, 8, 1) == 255);

    const auto side_head = sample_region_average(side_tile, 0, 5);
    const auto side_shaft = sample_region_average(side_tile, 9, 16);
    const auto top_average = sample_tile_average(top_tile);
    const auto bottom_average = sample_tile_average(bottom_tile);

    CHECK(side_head[0] > side_shaft[0] + 45.0F);
    CHECK(side_head[1] > side_shaft[1] + 35.0F);
    CHECK(top_average[0] > bottom_average[0] + 55.0F);
    CHECK(top_average[1] > bottom_average[1] + 35.0F);
    CHECK(bottom_average[0] > bottom_average[1]);
    CHECK(bottom_average[1] > bottom_average[2]);
}

TEST_CASE("tree foliage atlas tiles stay solid and opaque") {
    const auto pixels = build_block_atlas_pixels();
    REQUIRE(pixels.size() == static_cast<std::size_t>(kBlockAtlasSize * kBlockAtlasSize * 4));

    const auto check_foliage_tile = [&](BlockId block_id) {
        const auto tile = block_atlas_tile(block_id, BlockVisualFace::PositiveX);
        const auto tile_origin_x = tile.x * kBlockAtlasTileSize;
        const auto tile_origin_y = tile.y * kBlockAtlasTileSize;
        auto min_alpha = 255;
        auto max_alpha = 0;
        auto red_sum = 0;
        auto green_sum = 0;
        auto blue_sum = 0;
        for (int y = 0; y < kBlockAtlasTileSize; ++y) {
            for (int x = 0; x < kBlockAtlasTileSize; ++x) {
                const auto pixel_index =
                    static_cast<std::size_t>(((tile_origin_y + y) * kBlockAtlasSize + (tile_origin_x + x)) * 4);
                const auto red = static_cast<int>(pixels[pixel_index + 0]);
                const auto green = static_cast<int>(pixels[pixel_index + 1]);
                const auto blue = static_cast<int>(pixels[pixel_index + 2]);
                const auto alpha = static_cast<int>(pixels[pixel_index + 3]);

                min_alpha = std::min(min_alpha, alpha);
                max_alpha = std::max(max_alpha, alpha);
                red_sum += red;
                green_sum += green;
                blue_sum += blue;
            }
        }

        CHECK(min_alpha >= 250);
        CHECK(max_alpha == 255);
        CHECK(green_sum > red_sum);
        CHECK(green_sum > blue_sum);
    };

    check_foliage_tile(to_block_id(BlockType::Leaves));
    check_foliage_tile(to_block_id(BlockType::PineLeaves));
}

TEST_CASE("grass and snow side atlas tiles preserve a distinct surface layer over dirt") {
    const auto pixels = build_block_atlas_pixels();
    REQUIRE(pixels.size() == static_cast<std::size_t>(kBlockAtlasSize * kBlockAtlasSize * 4));

    const auto sample_region_average = [&](const BlockAtlasTile& tile, int y_begin, int y_end) {
        std::array<float, 4> sum {0.0F, 0.0F, 0.0F, 0.0F};
        auto sample_count = 0.0F;
        for (int y = y_begin; y < y_end; ++y) {
            for (int x = 0; x < kBlockAtlasTileSize; ++x) {
                const auto atlas_x = tile.x * kBlockAtlasTileSize + x;
                const auto atlas_y = tile.y * kBlockAtlasTileSize + y;
                const auto pixel_index = static_cast<std::size_t>((atlas_y * kBlockAtlasSize + atlas_x) * 4);
                sum[0] += static_cast<float>(pixels[pixel_index + 0]);
                sum[1] += static_cast<float>(pixels[pixel_index + 1]);
                sum[2] += static_cast<float>(pixels[pixel_index + 2]);
                sum[3] += static_cast<float>(pixels[pixel_index + 3]);
                sample_count += 1.0F;
            }
        }
        return std::array<float, 4> {
            sum[0] / sample_count,
            sum[1] / sample_count,
            sum[2] / sample_count,
            sum[3] / sample_count,
        };
    };

    const auto color_distance = [](const std::array<float, 4>& lhs, const std::array<float, 4>& rhs) {
        const auto red = lhs[0] - rhs[0];
        const auto green = lhs[1] - rhs[1];
        const auto blue = lhs[2] - rhs[2];
        return std::sqrt(red * red + green * green + blue * blue);
    };

    const auto dirt_tile = block_atlas_tile(to_block_id(BlockType::Dirt), BlockVisualFace::PositiveX);
    const auto grass_side_tile = block_atlas_tile(to_block_id(BlockType::Grass), BlockVisualFace::PositiveX);
    const auto snow_side_tile = block_atlas_tile(to_block_id(BlockType::Snow), BlockVisualFace::PositiveX);

    const auto dirt_bottom = sample_region_average(dirt_tile, 10, 16);
    const auto grass_top = sample_region_average(grass_side_tile, 0, 5);
    const auto grass_bottom = sample_region_average(grass_side_tile, 10, 16);
    const auto snow_top = sample_region_average(snow_side_tile, 0, 6);
    const auto snow_bottom = sample_region_average(snow_side_tile, 10, 16);

    CHECK(grass_top[1] > grass_bottom[1] + 40.0F);
    CHECK(color_distance(grass_bottom, dirt_bottom) < 1.0F);

    CHECK(snow_top[0] > snow_bottom[0] + 80.0F);
    CHECK(snow_top[1] > snow_bottom[1] + 120.0F);
    CHECK(snow_top[2] > snow_bottom[2] + 180.0F);
    CHECK(color_distance(snow_bottom, dirt_bottom) < 1.0F);
}

TEST_CASE("baked wood and foliage variants stay visually differentiated") {
    const auto pixels = build_block_atlas_pixels();
    REQUIRE(pixels.size() == static_cast<std::size_t>(kBlockAtlasSize * kBlockAtlasSize * 4));

    const auto sample_tile_average = [&](const BlockAtlasTile& tile) {
        std::array<float, 4> sum {0.0F, 0.0F, 0.0F, 0.0F};
        auto sample_count = 0.0F;
        for (int y = 0; y < kBlockAtlasTileSize; ++y) {
            for (int x = 0; x < kBlockAtlasTileSize; ++x) {
                const auto atlas_x = tile.x * kBlockAtlasTileSize + x;
                const auto atlas_y = tile.y * kBlockAtlasTileSize + y;
                const auto pixel_index = static_cast<std::size_t>((atlas_y * kBlockAtlasSize + atlas_x) * 4);
                sum[0] += static_cast<float>(pixels[pixel_index + 0]);
                sum[1] += static_cast<float>(pixels[pixel_index + 1]);
                sum[2] += static_cast<float>(pixels[pixel_index + 2]);
                sum[3] += static_cast<float>(pixels[pixel_index + 3]);
                sample_count += 1.0F;
            }
        }
        return std::array<float, 4> {
            sum[0] / sample_count,
            sum[1] / sample_count,
            sum[2] / sample_count,
            sum[3] / sample_count,
        };
    };

    const auto color_distance = [](const std::array<float, 4>& lhs, const std::array<float, 4>& rhs) {
        const auto red = lhs[0] - rhs[0];
        const auto green = lhs[1] - rhs[1];
        const auto blue = lhs[2] - rhs[2];
        return std::sqrt(red * red + green * green + blue * blue);
    };

    const auto wood_side = sample_tile_average(block_atlas_tile(to_block_id(BlockType::Wood), BlockVisualFace::PositiveX));
    const auto wood_top = sample_tile_average(block_atlas_tile(to_block_id(BlockType::Wood), BlockVisualFace::PositiveY));
    const auto planks = sample_tile_average(block_atlas_tile(to_block_id(BlockType::Planks), BlockVisualFace::PositiveX));
    const auto pine_side = sample_tile_average(block_atlas_tile(to_block_id(BlockType::PineWood), BlockVisualFace::PositiveX));
    const auto leaves = sample_tile_average(block_atlas_tile(to_block_id(BlockType::Leaves), BlockVisualFace::PositiveX));
    const auto pine_leaves = sample_tile_average(block_atlas_tile(to_block_id(BlockType::PineLeaves), BlockVisualFace::PositiveX));

    CHECK(color_distance(wood_side, wood_top) > 20.0F);
    CHECK(color_distance(wood_side, planks) > 20.0F);
    CHECK(color_distance(wood_side, pine_side) > 10.0F);
    CHECK(color_distance(leaves, pine_leaves) > 25.0F);
}

TEST_CASE("block visual material classification keeps key terrain families distinct") {
    CHECK(block_visual_material(to_block_id(BlockType::Grass)) == BlockVisualMaterial::Terrain);
    CHECK(block_visual_material(to_block_id(BlockType::Stone)) == BlockVisualMaterial::Rock);
    CHECK(block_visual_material(to_block_id(BlockType::Wood)) == BlockVisualMaterial::Wood);
    CHECK(block_visual_material(to_block_id(BlockType::Leaves)) == BlockVisualMaterial::Foliage);
    CHECK(block_visual_material(to_block_id(BlockType::TallGrass)) == BlockVisualMaterial::Flora);
    CHECK(block_visual_material(to_block_id(BlockType::Water)) == BlockVisualMaterial::Water);
    CHECK(block_visual_material(to_block_id(BlockType::Torch)) == BlockVisualMaterial::Emissive);
    CHECK(block_visual_material(to_block_id(BlockType::Snow)) == BlockVisualMaterial::Snow);
}

TEST_CASE("accent atlas keeps authored celestial sprites within expected dimensions and alpha ranges") {
    const auto pixels = build_accent_atlas_pixels();
    REQUIRE(pixels.size() == static_cast<std::size_t>(kAccentAtlasSize * kAccentAtlasSize * 4));

    const auto sun_tile = accent_atlas_tile(AccentAtlasSprite::Sun);
    const auto moon_tile = accent_atlas_tile(AccentAtlasSprite::Moon);
    const auto star_tile = accent_atlas_tile(AccentAtlasSprite::Star);
    const auto cloud_tile = accent_atlas_tile(AccentAtlasSprite::Cloud);
    const auto ring_tile = accent_atlas_tile(AccentAtlasSprite::Ring);
    const auto sample_alpha = [&](const AccentAtlasTile& tile, int local_x, int local_y) {
        const auto x = tile.x * kAccentAtlasTileSize + local_x;
        const auto y = tile.y * kAccentAtlasTileSize + local_y;
        return pixels[static_cast<std::size_t>((y * kAccentAtlasSize + x) * 4 + 3)];
    };

    CHECK(sample_alpha(sun_tile, 8, 8) > 0);
    CHECK(sample_alpha(sun_tile, 0, 0) == 0);
    CHECK(sample_alpha(moon_tile, 8, 8) > 0);
    CHECK(sample_alpha(moon_tile, 0, 0) == 0);
    CHECK(sample_alpha(star_tile, 8, 8) > 0);
    CHECK(sample_alpha(star_tile, 0, 0) == 0);
    CHECK(sample_alpha(cloud_tile, 8, 8) > 0);
    CHECK(sample_alpha(cloud_tile, 15, 0) == 0);
    CHECK(sample_alpha(ring_tile, 8, 8) < sample_alpha(ring_tile, 8, 2));
}

TEST_CASE("generation is deterministic for identical seeds") {
    World first(98765, 1);
    World second(98765, 1);
    const ChunkCoord coord {1, -2};

    first.ensure_chunk_loaded(coord);
    second.ensure_chunk_loaded(coord);

    const auto* first_chunk = first.find_chunk(coord);
    const auto* second_chunk = second.find_chunk(coord);
    REQUIRE(first_chunk != nullptr);
    REQUIRE(second_chunk != nullptr);

    for (int y = 0; y < kChunkHeight; ++y) {
        for (int z = 0; z < kChunkSizeZ; ++z) {
            for (int x = 0; x < kChunkSizeX; ++x) {
                CHECK(first_chunk->get_local(x, y, z) == second_chunk->get_local(x, y, z));
            }
        }
    }
}

TEST_CASE("surface_height returns the highest solid block in a column") {
    World world(18, 1);
    test::make_chunk_empty(world, {0, 0});
    world.set_block(3, 4, 5, to_block_id(BlockType::Stone));
    world.set_block(3, 9, 5, to_block_id(BlockType::Stone));
    world.set_block(3, 7, 5, to_block_id(BlockType::Stone));

    CHECK(world.surface_height(3, 5) == 9);
}

TEST_CASE("surface height ignores decorative plants and tree foliage") {
    World world(181, 1);
    test::make_chunk_empty(world, {0, 0});
    world.set_block(3, 4, 5, to_block_id(BlockType::Stone));
    world.set_block(3, 5, 5, to_block_id(BlockType::TallGrass));
    world.set_block(3, 8, 5, to_block_id(BlockType::Leaves));

    CHECK(world.surface_height(3, 5) == 4);
    CHECK(world.loaded_surface_height(3, 5).value_or(-1) == 4);
}

TEST_CASE("surface height ignores water columns above solid ground") {
    World world(182, 1);
    test::make_chunk_empty(world, {0, 0});
    world.set_block(3, 4, 5, to_block_id(BlockType::Stone));
    world.set_block(3, 5, 5, to_block_id(BlockType::Water));

    CHECK(world.surface_height(3, 5) == 4);
    CHECK(world.loaded_surface_height(3, 5).value_or(-1) == 4);
}

TEST_CASE("world generator fills every submerged column up to the global sea level") {
    constexpr std::array<int, 4> seeds {{1337, 2024, 4242, 9001}};

    for (const auto seed : seeds) {
        World world(seed, 2);
        for (int chunk_z = -2; chunk_z <= 2; ++chunk_z) {
            for (int chunk_x = -2; chunk_x <= 2; ++chunk_x) {
                world.ensure_chunk_loaded({chunk_x, chunk_z});
            }
        }

        for (int z = -2 * kChunkSizeZ; z < 3 * kChunkSizeZ; ++z) {
            for (int x = -2 * kChunkSizeX; x < 3 * kChunkSizeX; ++x) {
                const auto surface_y = world.surface_height(x, z);
                if (surface_y >= kSeaLevel) {
                    continue;
                }

                CAPTURE(seed);
                CAPTURE(x);
                CAPTURE(z);
                CAPTURE(surface_y);
                const auto has_sea_water = world.has_water(x, kSeaLevel, z);
                const auto sea_water_level = world.water_level(x, kSeaLevel, z);
                CHECK(has_sea_water);
                CHECK(sea_water_level == kMaxWaterLevel);
                if (kSeaLevel < kWorldMaxY) {
                    const auto has_water_above_sea = world.has_water(x, kSeaLevel + 1, z);
                    CHECK_FALSE(has_water_above_sea);
                }
            }
        }
    }
}

TEST_CASE("pressurized reservoirs fill large adjacent basins without fading by distance") {
    World world(18301, 2);
    test::make_chunk_empty(world, {0, 0});
    test::make_chunk_empty(world, {1, 0});

    constexpr int floor_y = 68;
    constexpr int water_min_y = floor_y + 1;
    constexpr int water_max_y = floor_y + 3;
    constexpr int min_x = 0;
    constexpr int separator_x = 5;
    constexpr int max_x = 24;
    constexpr int min_z = 0;
    constexpr int max_z = 6;
    constexpr int cavity_far_x = 20;
    constexpr int cavity_mid_z = 3;

    const auto stone = to_block_id(BlockType::Stone);

    for (int x = min_x; x <= max_x; ++x) {
        for (int z = min_z; z <= max_z; ++z) {
            world.set_block(x, floor_y, z, stone);
        }
    }

    for (int y = water_min_y; y <= water_max_y + 1; ++y) {
        for (int x = min_x; x <= max_x; ++x) {
            world.set_block(x, y, min_z, stone);
            world.set_block(x, y, max_z, stone);
        }
        for (int z = min_z; z <= max_z; ++z) {
            world.set_block(min_x, y, z, stone);
            world.set_block(max_x, y, z, stone);
            world.set_block(separator_x, y, z, stone);
        }
    }

    test::flush_pending_work(world);

    for (int y = water_min_y; y <= water_max_y; ++y) {
        for (int z = min_z + 1; z < max_z; ++z) {
            for (int x = min_x + 1; x < separator_x; ++x) {
                world.set_block(x, y, z, to_block_id(BlockType::Water));
            }
        }
    }

    test::flush_pending_work(world);
    CHECK_FALSE(world.has_water(cavity_far_x, water_max_y, cavity_mid_z));

    for (int y = water_min_y; y <= water_max_y; ++y) {
        for (int z = min_z + 1; z < max_z; ++z) {
            world.set_block(separator_x, y, z, to_block_id(BlockType::Air));
        }
    }

    test::flush_pending_work(world);

    CHECK(world.water_level(cavity_far_x, water_min_y, cavity_mid_z) == kMaxWaterLevel);
    CHECK(world.water_level(cavity_far_x, water_min_y + 1, cavity_mid_z) == kMaxWaterLevel);
    CHECK(world.water_level(cavity_far_x, water_max_y, cavity_mid_z) == kMaxWaterLevel);
}

TEST_CASE("single isolated source keeps a localized spread on a flat floor") {
    World world(18302, 2);
    test::make_chunk_empty(world, {0, 0});
    test::make_chunk_empty(world, {1, 0});

    constexpr int floor_y = 79;
    constexpr int water_y = floor_y + 1;
    const auto stone = to_block_id(BlockType::Stone);

    for (int x = 0; x < kChunkSizeX * 2; ++x) {
        for (int z = 0; z < kChunkSizeZ; ++z) {
            world.set_block(x, floor_y, z, stone);
        }
    }

    test::flush_pending_work(world);

    world.set_block(2, water_y, 2, to_block_id(BlockType::Water));
    test::flush_pending_work(world);

    CHECK(world.water_level(2, water_y, 2) == kMaxWaterLevel);
    CHECK_FALSE(world.has_water(14, water_y, 2));
}

TEST_CASE("chunk mesher routes water into the dedicated translucent submesh") {
    World world(183, 1);
    test::make_chunk_empty(world, {0, 0});
    world.set_block(3, 5, 5, to_block_id(BlockType::Water));

    ChunkMesher mesher {};
    const auto mesh = mesher.build_mesh(world, {0, 0});

    CHECK(mesh.face_count == 0);
    CHECK(mesh.vertices.empty());
    CHECK(mesh.indices.empty());
    CHECK(mesh.water_face_count == 6);
    CHECK(mesh.water_vertices.size() == 89);
    CHECK(mesh.water_indices.size() == 294);
    CHECK(std::all_of(mesh.water_vertices.begin(), mesh.water_vertices.end(), [](const ChunkVertex& vertex) {
        return vertex.ao == doctest::Approx(1.0F);
    }));
    CHECK_FALSE(mesh.empty());
}

TEST_CASE("chunk mesher keeps top water UVs continuous across adjacent blocks") {
    World world(185, 1);
    test::make_chunk_empty(world, {0, 0});
    world.set_block(0, 5, 0, to_block_id(BlockType::Water));
    world.set_block(1, 5, 0, to_block_id(BlockType::Water));

    ChunkMesher mesher {};
    const auto mesh = mesher.build_mesh(world, {0, 0});

    struct WaterTopVertex {
        float x;
        float z;
        float u;
        float v;
    };

    std::vector<WaterTopVertex> top_vertices;
    for (const auto& vertex : mesh.water_vertices) {
        if (vertex.ny > 0.9F) {
            top_vertices.push_back({vertex.x, vertex.z, vertex.u, vertex.v});
        }
    }

    REQUIRE(top_vertices.size() == 50);

    const auto vertices_at = [&](float x, float z) {
        std::vector<WaterTopVertex> matches;
        for (const auto& vertex : top_vertices) {
            if (vertex.x == doctest::Approx(x) && vertex.z == doctest::Approx(z)) {
                matches.push_back(vertex);
            }
        }
        return matches;
    };

    const auto left_near = vertices_at(0.0F, 0.0F);
    const auto shared_near = vertices_at(1.0F, 0.0F);
    const auto shared_mid = vertices_at(1.0F, 0.5F);
    const auto right_near = vertices_at(2.0F, 0.0F);
    const auto shared_far = vertices_at(1.0F, 1.0F);

    REQUIRE(left_near.size() == 1);
    REQUIRE(shared_near.size() == 2);
    REQUIRE(shared_mid.size() == 2);
    REQUIRE(right_near.size() == 1);
    REQUIRE(shared_far.size() == 2);

    CHECK(shared_near[0].u == doctest::Approx(shared_near[1].u));
    CHECK(shared_near[0].v == doctest::Approx(shared_near[1].v));
    CHECK(shared_mid[0].u == doctest::Approx(shared_mid[1].u));
    CHECK(shared_mid[0].v == doctest::Approx(shared_mid[1].v));
    CHECK(shared_far[0].u == doctest::Approx(shared_far[1].u));
    CHECK(shared_far[0].v == doctest::Approx(shared_far[1].v));

    const auto uv_step = 1.0F / static_cast<float>(kBlockAtlasTilesPerAxis);
    const auto expected_block_delta = uv_step / 8.0F;

    CHECK(shared_near[0].u == doctest::Approx(left_near[0].u + expected_block_delta));
    CHECK(right_near[0].u == doctest::Approx(shared_near[0].u + expected_block_delta));
}

TEST_CASE("chunk mesher keeps water top texel density stable across the repeat boundary") {
    World world(285, 1);
    test::make_chunk_empty(world, {0, 0});
    world.set_block(7, 5, 0, to_block_id(BlockType::Water));
    world.set_block(8, 5, 0, to_block_id(BlockType::Water));

    ChunkMesher mesher {};
    const auto mesh = mesher.build_mesh(world, {0, 0});

    struct WaterTopVertex {
        float x;
        float z;
        float u;
        float v;
    };

    std::vector<WaterTopVertex> top_vertices;
    for (const auto& vertex : mesh.water_vertices) {
        if (vertex.ny > 0.9F) {
            top_vertices.push_back({vertex.x, vertex.z, vertex.u, vertex.v});
        }
    }

    REQUIRE(top_vertices.size() == 50);

    const auto vertices_at = [&](float x, float z) {
        std::vector<WaterTopVertex> matches;
        for (const auto& vertex : top_vertices) {
            if (vertex.x == doctest::Approx(x) && vertex.z == doctest::Approx(z)) {
                matches.push_back(vertex);
            }
        }
        return matches;
    };

    const auto left_near = vertices_at(7.0F, 0.0F);
    auto shared_near = vertices_at(8.0F, 0.0F);
    auto shared_mid = vertices_at(8.0F, 0.5F);
    const auto right_near = vertices_at(9.0F, 0.0F);
    auto shared_far = vertices_at(8.0F, 1.0F);

    REQUIRE(left_near.size() == 1);
    REQUIRE(shared_near.size() == 2);
    REQUIRE(shared_mid.size() == 2);
    REQUIRE(right_near.size() == 1);
    REQUIRE(shared_far.size() == 2);

    std::sort(shared_near.begin(), shared_near.end(), [](const WaterTopVertex& lhs, const WaterTopVertex& rhs) {
        return lhs.u < rhs.u;
    });
    std::sort(shared_mid.begin(), shared_mid.end(), [](const WaterTopVertex& lhs, const WaterTopVertex& rhs) {
        return lhs.u < rhs.u;
    });
    std::sort(shared_far.begin(), shared_far.end(), [](const WaterTopVertex& lhs, const WaterTopVertex& rhs) {
        return lhs.u < rhs.u;
    });

    const auto water_tile = block_atlas_tile(to_block_id(BlockType::Water), BlockVisualFace::PositiveY);
    const auto uv_step = 1.0F / static_cast<float>(kBlockAtlasTilesPerAxis);
    const auto tile_u0 = static_cast<float>(water_tile.x) * uv_step;
    const auto expected_block_delta = uv_step / 8.0F;

    CHECK(left_near[0].u == doctest::Approx(tile_u0 + expected_block_delta * 7.0F));
    CHECK(shared_near[0].u == doctest::Approx(tile_u0));
    CHECK(shared_near[1].u == doctest::Approx(tile_u0 + uv_step));
    CHECK(right_near[0].u == doctest::Approx(tile_u0 + expected_block_delta));

    CHECK(shared_mid[0].u == doctest::Approx(tile_u0));
    CHECK(shared_mid[1].u == doctest::Approx(tile_u0 + uv_step));
    CHECK(shared_far[0].u == doctest::Approx(tile_u0));
    CHECK(shared_far[1].u == doctest::Approx(tile_u0 + uv_step));

    CHECK(shared_near[1].u - left_near[0].u == doctest::Approx(expected_block_delta));
    CHECK(right_near[0].u - shared_near[0].u == doctest::Approx(expected_block_delta));
}

TEST_CASE("chunk mesher tags only exposed water surface vertices for wave animation") {
    World world(186, 1);
    test::make_chunk_empty(world, {0, 0});
    world.set_block(3, 5, 5, to_block_id(BlockType::Water));

    ChunkMesher mesher {};
    const auto mesh = mesher.build_mesh(world, {0, 0});

    const auto animated_vertex_count = std::count_if(mesh.water_vertices.begin(), mesh.water_vertices.end(), [](const ChunkVertex& vertex) {
        return vertex.wave_weight > 0.5F;
    });

    CHECK(animated_vertex_count == 65);
    CHECK(std::all_of(mesh.water_vertices.begin(), mesh.water_vertices.end(), [](const ChunkVertex& vertex) {
        return vertex.wave_weight == 0.0F || vertex.wave_weight == 1.0F;
    }));
}

TEST_CASE("stacked water only animates the topmost surface block") {
    World world(187, 1);
    test::make_chunk_empty(world, {0, 0});
    world.set_block(3, 5, 5, to_block_id(BlockType::Water));
    world.set_block(3, 6, 5, to_block_id(BlockType::Water));

    ChunkMesher mesher {};
    const auto mesh = mesher.build_mesh(world, {0, 0});

    const auto animated_vertex_count = std::count_if(mesh.water_vertices.begin(), mesh.water_vertices.end(), [](const ChunkVertex& vertex) {
        return vertex.wave_weight > 0.5F;
    });
    CHECK(animated_vertex_count == 65);

    CHECK(std::all_of(mesh.water_vertices.begin(), mesh.water_vertices.end(), [](const ChunkVertex& vertex) {
        if (vertex.wave_weight <= 0.5F) {
            return true;
        }
        return vertex.y > 5.85F;
    }));
}

TEST_CASE("chunk mesher handles isolated high blocks without losing geometry") {
    World world(184, 1);
    test::make_chunk_empty(world, {0, 0});
    world.set_block(2, 96, 2, to_block_id(BlockType::Torch));

    ChunkMesher mesher {};
    const auto mesh = mesher.build_mesh(world, {0, 0});

    CHECK(mesh.face_count == 10);
    CHECK(mesh.vertices.size() == 40);
    CHECK(mesh.indices.size() == 60);
    CHECK(mesh.water_face_count == 0);
    REQUIRE_FALSE(mesh.vertices.empty());

    auto min_x = mesh.vertices.front().x;
    auto max_x = mesh.vertices.front().x;
    auto min_y = mesh.vertices.front().y;
    auto max_y = mesh.vertices.front().y;
    auto min_z = mesh.vertices.front().z;
    auto max_z = mesh.vertices.front().z;
    for (const auto& vertex : mesh.vertices) {
        min_x = std::min(min_x, vertex.x);
        max_x = std::max(max_x, vertex.x);
        min_y = std::min(min_y, vertex.y);
        max_y = std::max(max_y, vertex.y);
        min_z = std::min(min_z, vertex.z);
        max_z = std::max(max_z, vertex.z);
    }

    const auto wood_material = block_visual_material_value(BlockVisualMaterial::Wood);
    const auto emissive_material = block_visual_material_value(BlockVisualMaterial::Emissive);
    CHECK(std::any_of(mesh.vertices.begin(), mesh.vertices.end(), [&](const ChunkVertex& vertex) {
        return vertex.material_class == doctest::Approx(wood_material);
    }));
    CHECK(std::any_of(mesh.vertices.begin(), mesh.vertices.end(), [&](const ChunkVertex& vertex) {
        return vertex.material_class == doctest::Approx(emissive_material);
    }));

    CHECK(min_x == doctest::Approx(2.375F));
    CHECK(max_x == doctest::Approx(2.625F));
    CHECK(min_y == doctest::Approx(96.0F));
    CHECK(max_y == doctest::Approx(96.875F));
    CHECK(min_z == doctest::Approx(2.375F));
    CHECK(max_z == doctest::Approx(2.625F));
}

TEST_CASE("wall torch meshes shift toward their support and sit lower than floor torches") {
    struct TorchBounds {
        glm::vec3 min {0.0F};
        glm::vec3 max {0.0F};
    };

    const auto capture_torch_bounds = [](const ChunkMeshData& mesh) {
        const auto wood_material = block_visual_material_value(BlockVisualMaterial::Wood);
        const auto emissive_material = block_visual_material_value(BlockVisualMaterial::Emissive);
        TorchBounds bounds {
            glm::vec3 {std::numeric_limits<float>::max()},
            glm::vec3 {std::numeric_limits<float>::lowest()},
        };

        for (const auto& vertex : mesh.vertices) {
            if (std::abs(vertex.material_class - wood_material) > 1.0e-4F &&
                std::abs(vertex.material_class - emissive_material) > 1.0e-4F) {
                continue;
            }

            bounds.min.x = std::min(bounds.min.x, vertex.x);
            bounds.min.y = std::min(bounds.min.y, vertex.y);
            bounds.min.z = std::min(bounds.min.z, vertex.z);
            bounds.max.x = std::max(bounds.max.x, vertex.x);
            bounds.max.y = std::max(bounds.max.y, vertex.y);
            bounds.max.z = std::max(bounds.max.z, vertex.z);
        }

        return bounds;
    };

    World floor_world(1841, 1);
    test::make_chunk_empty(floor_world, {0, 0});
    floor_world.set_block(2, 9, 2, to_block_id(BlockType::Stone));
    floor_world.set_block(2, 10, 2, to_block_id(BlockType::Torch));
    floor_world.rebuild_dirty_meshes();

    const auto* floor_mesh = floor_world.mesh_for({0, 0});
    REQUIRE(floor_mesh != nullptr);
    const auto floor_bounds = capture_torch_bounds(*floor_mesh);

    World wall_world(1842, 1);
    test::make_chunk_empty(wall_world, {0, 0});
    wall_world.set_block(1, 10, 2, to_block_id(BlockType::Stone));
    wall_world.set_block(2, 10, 2, to_block_id(BlockType::TorchWallNegativeX));
    wall_world.rebuild_dirty_meshes();

    const auto* wall_mesh = wall_world.mesh_for({0, 0});
    REQUIRE(wall_mesh != nullptr);
    const auto wall_bounds = capture_torch_bounds(*wall_mesh);

    CHECK(wall_bounds.min.x < floor_bounds.min.x);
    CHECK(wall_bounds.max.y < floor_bounds.max.y);
}

TEST_CASE("sky light stays at 15 until the first opaque block and 0 below it") {
    World world(19, 1);
    test::make_chunk_empty(world, {0, 0});
    world.set_block(2, 10, 2, to_block_id(BlockType::Stone));

    world.rebuild_lighting();

    CHECK(world.get_sky_light(2, 20, 2) == 15);
    CHECK(world.get_sky_light(2, 11, 2) == 15);
    CHECK(world.get_sky_light(2, 10, 2) == 0);
    CHECK(world.get_sky_light(2, 9, 2) == 0);
}

TEST_CASE("torch light propagates in air and stops on opaque blocks") {
    World world(20, 1);
    test::make_chunk_empty(world, {0, 0});
    world.set_block(4, 1, 4, to_block_id(BlockType::Torch));
    for (int y = 0; y < kChunkHeight; ++y) {
        for (int z = 0; z < kChunkSizeZ; ++z) {
            world.set_block(6, y, z, to_block_id(BlockType::Stone));
        }
    }

    world.rebuild_lighting();

    CHECK(world.get_block_light(4, 1, 4) == 14);
    CHECK(world.get_block_light(5, 1, 4) == 13);
    CHECK(world.get_block_light(6, 1, 4) == 0);
    CHECK(world.get_block_light(7, 1, 4) == 0);
}

TEST_CASE("torch light crosses chunk boundaries") {
    World world(21, 1);
    test::make_chunk_empty(world, {0, 0});
    test::make_chunk_empty(world, {1, 0});
    world.set_block(15, 1, 2, to_block_id(BlockType::Torch));

    world.rebuild_lighting();

    CHECK(world.get_block_light(15, 1, 2) == 14);
    CHECK(world.get_block_light(16, 1, 2) == 13);
    CHECK(world.get_block_light(17, 1, 2) == 12);
}

TEST_CASE("removing the support block removes the torch above it") {
    World world(22, 1);
    test::make_chunk_empty(world, {0, 0});
    world.set_block(3, 0, 3, to_block_id(BlockType::Stone));
    world.set_block(3, 1, 3, to_block_id(BlockType::Torch));
    world.rebuild_lighting();
    REQUIRE(world.get_block(3, 1, 3) == to_block_id(BlockType::Torch));
    REQUIRE(world.get_block_light(3, 1, 3) == 14);

    world.set_block(3, 0, 3, to_block_id(BlockType::Air));
    world.rebuild_lighting();

    CHECK(world.get_block(3, 1, 3) == to_block_id(BlockType::Air));
    CHECK(world.get_block_light(3, 1, 3) == 0);
}

TEST_CASE("removing the wall support removes the wall torch") {
    World world(221, 1);
    test::make_chunk_empty(world, {0, 0});
    world.set_block(1, 5, 3, to_block_id(BlockType::Stone));
    world.set_block(2, 5, 3, to_block_id(BlockType::TorchWallNegativeX));
    world.rebuild_lighting();
    REQUIRE(world.get_block(2, 5, 3) == to_block_id(BlockType::TorchWallNegativeX));
    REQUIRE(world.get_block_light(2, 5, 3) == 14);

    world.set_block(1, 5, 3, to_block_id(BlockType::Air));
    world.rebuild_lighting();

    CHECK(world.get_block(2, 5, 3) == to_block_id(BlockType::Air));
    CHECK(world.get_block_light(2, 5, 3) == 0);
}

TEST_CASE("update_streaming plans the full radius around the player without immediate loads") {
    World world(56, 1);
    world.update_streaming({0.5F, 0.0F, 0.5F});

    CHECK(world.chunk_records().empty());
    CHECK(world.pending_generation_count() == 9);
    CHECK(world.pending_mesh_count() == 0);
    CHECK(world.pending_lighting_count() == 0);
    CHECK(world.has_pending_work());
}

TEST_CASE("world constructor clamps unsafe stream radii") {
    World negative_radius_world(1337, -4);
    CHECK(negative_radius_world.stream_radius() == 0);

    World oversized_radius_world(1337, std::numeric_limits<int>::max());
    CHECK(oversized_radius_world.stream_radius() == kMaxStreamRadius);
}

TEST_CASE("update_streaming is a no-op while the player stays in the same chunk") {
    World world(55, 1);

    const auto first_update = world.update_streaming({0.5F, 0.0F, 0.5F});
    const auto second_update = world.update_streaming({5.5F, 0.0F, 6.5F});

    CHECK(first_update.chunk_changed);
    CHECK(first_update.generation_enqueued == 9);
    CHECK_FALSE(second_update.chunk_changed);
    CHECK(second_update.generation_enqueued == 0);
    CHECK(second_update.generation_pruned == 0);
    CHECK(world.pending_generation_count() == 9);
}

TEST_CASE("process_pending_work respects chunk generation budget and eventually readies nearby chunks") {
    World world(58, 1);
    world.update_streaming({0.5F, 0.0F, 0.5F});

    const auto first_stats = world.process_pending_work({1, 16, 65536});
    CHECK(first_stats.generated_chunks == 1);
    CHECK(world.chunk_records().size() == 1);
    CHECK(world.pending_generation_count() == 8);

    test::flush_pending_work(world);

    CHECK(world.chunk_records().size() == 9);
    CHECK(world.find_chunk({0, 0}) != nullptr);
    CHECK(world.find_chunk({1, 1}) != nullptr);
    CHECK(world.find_chunk({-1, -1}) != nullptr);
    CHECK(world.are_chunks_ready({0.5F, 70.0F, 0.5F}, 1));
}

TEST_CASE("process_pending_work respects zero generation time budget") {
    World world(60, 1);
    world.update_streaming({0.5F, 0.0F, 0.5F});

    WorldWorkBudget budget {};
    budget.chunk_generation_budget = 16;
    budget.mesh_rebuild_budget = 16;
    budget.light_node_budget = 65536;
    budget.max_generation_ms = 0.0;
    budget.max_lighting_ms = 10.0;
    budget.max_meshing_ms = 10.0;

    const auto stats = world.process_pending_work(budget);

    CHECK(stats.generated_chunks == 0);
    CHECK(world.chunk_records().empty());
    CHECK(world.pending_generation_count() == 9);
}

TEST_CASE("spawn preload stays ready while outer streaming work starts") {
    World world(59, kDefaultStreamRadius);
    const auto preload_center = world.world_to_chunk(0, 0);
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dx = -1; dx <= 1; ++dx) {
            world.ensure_chunk_loaded({preload_center.x + dx, preload_center.z + dz});
        }
    }
    world.rebuild_dirty_meshes();

    const glm::vec3 player_position {0.5F, 80.0F, 0.5F};
    REQUIRE(world.are_chunks_ready(player_position, 1));

    world.update_streaming(player_position);
    for (int frame_index = 0; frame_index < 60; ++frame_index) {
        const auto stats = world.process_pending_work({2, 4, 16384});
        (void)stats;
        CAPTURE(frame_index);
        CHECK(world.are_chunks_ready(player_position, 1));
    }
}

TEST_CASE("update_streaming unloads chunks that move outside the unload radius") {
    World world(57, 1);
    world.update_streaming({0.5F, 0.0F, 0.5F});
    test::flush_pending_work(world);
    REQUIRE(world.find_chunk({0, 0}) != nullptr);

    world.update_streaming({static_cast<float>(kChunkSizeX * 5) + 0.5F, 0.0F, 0.5F});
    test::flush_pending_work(world);

    CHECK(world.find_chunk({0, 0}) == nullptr);
    CHECK(world.find_chunk({5, 0}) != nullptr);
}

TEST_CASE("mesher hides internal faces between adjacent solid blocks") {
    World world(77, 1);
    const ChunkCoord coord {0, 0};

    test::make_chunk_empty(world, coord);
    world.set_block(0, 10, 0, to_block_id(BlockType::Stone));
    world.rebuild_dirty_meshes();

    const auto* single_block_mesh = world.mesh_for(coord);
    REQUIRE(single_block_mesh != nullptr);
    CHECK(single_block_mesh->face_count == 6);

    world.set_block(1, 10, 0, to_block_id(BlockType::Stone));
    world.rebuild_dirty_meshes();

    const auto* adjacent_mesh = world.mesh_for(coord);
    REQUIRE(adjacent_mesh != nullptr);
    CHECK(adjacent_mesh->face_count == 10);
}

TEST_CASE("mesher computes full ambient occlusion for an isolated block and darker corners when enclosed") {
    World world(78, 1);
    const ChunkCoord coord {0, 0};
    test::make_chunk_empty(world, coord);
    world.set_block(1, 1, 1, to_block_id(BlockType::Stone));
    world.rebuild_dirty_meshes();

    const auto* isolated_mesh = world.mesh_for(coord);
    REQUIRE(isolated_mesh != nullptr);
    REQUIRE_FALSE(isolated_mesh->vertices.empty());
    CHECK(std::all_of(isolated_mesh->vertices.begin(), isolated_mesh->vertices.end(), [](const ChunkVertex& vertex) {
        return vertex.ao == doctest::Approx(1.0F);
    }));

    test::make_chunk_empty(world, coord);
    world.set_block(1, 1, 1, to_block_id(BlockType::Stone));
    world.set_block(0, 1, 1, to_block_id(BlockType::Stone));
    world.set_block(1, 1, 0, to_block_id(BlockType::Stone));
    world.set_block(0, 1, 0, to_block_id(BlockType::Stone));
    world.rebuild_dirty_meshes();

    const auto* occluded_mesh = world.mesh_for(coord);
    REQUIRE(occluded_mesh != nullptr);
    auto darkest_top_ao = 1.0F;
    for (const auto& vertex : occluded_mesh->vertices) {
        if (vertex.ny > 0.9F && vertex.y > 1.9F) {
            darkest_top_ao = std::min(darkest_top_ao, vertex.ao);
        }
    }
    CHECK(darkest_top_ao < 1.0F);
}

TEST_CASE("mesher renders crossed decorative quads for flora blocks") {
    World world(178, 1);
    const ChunkCoord coord {0, 0};
    test::make_chunk_empty(world, coord);
    world.set_block(2, 1, 2, to_block_id(BlockType::TallGrass));
    world.rebuild_dirty_meshes();

    const auto* mesh = world.mesh_for(coord);
    REQUIRE(mesh != nullptr);
    CHECK(mesh->face_count == 4);
    CHECK(mesh->vertices.size() == 16);
    CHECK(mesh->indices.size() == 24);
}

TEST_CASE("raycast returns first solid block and adjacent placement cell") {
    World world(11, 1);
    const ChunkCoord coord {0, 0};
    test::make_chunk_empty(world, coord);
    world.set_block(1, 10, 0, to_block_id(BlockType::Stone));

    const auto hit = world.raycast({0.5F, 10.5F, 0.5F}, {1.0F, 0.0F, 0.0F}, 8.0F);
    const BlockCoord expected_hit_block {1, 10, 0};
    const BlockCoord expected_adjacent {0, 10, 0};
    REQUIRE(hit.hit);
    CHECK(hit.block == expected_hit_block);
    CHECK(hit.adjacent == expected_adjacent);
    CHECK(hit.block_id == to_block_id(BlockType::Stone));
    CHECK(hit.distance == doctest::Approx(0.5F));
}

TEST_CASE("raycast returns no hit when the path is empty") {
    World world(12, 1);
    test::make_chunk_empty(world, {0, 0});

    const auto hit = world.raycast({0.5F, 10.5F, 0.5F}, {1.0F, 0.0F, 0.0F}, 8.0F);
    CHECK_FALSE(hit.hit);
}

TEST_CASE("raycast returns the current block immediately when starting inside a solid block") {
    World world(13, 1);
    test::make_chunk_empty(world, {0, 0});
    world.set_block(0, 10, 0, to_block_id(BlockType::Stone));

    const auto hit = world.raycast({0.5F, 10.5F, 0.5F}, {1.0F, 0.0F, 0.0F}, 8.0F);
    const BlockCoord expected_current {0, 10, 0};
    REQUIRE(hit.hit);
    CHECK(hit.block == expected_current);
    CHECK(hit.adjacent == expected_current);
    CHECK(hit.distance == doctest::Approx(0.0F));
}

TEST_CASE("raycast can target decorative plants") {
    World world(131, 1);
    test::make_chunk_empty(world, {0, 0});
    world.set_block(1, 10, 0, to_block_id(BlockType::TallGrass));

    const auto hit = world.raycast({0.5F, 10.5F, 0.5F}, {1.0F, 0.0F, 0.0F}, 8.0F);
    REQUIRE(hit.hit);
    CHECK(hit.block == BlockCoord {1, 10, 0});
    CHECK(hit.block_id == to_block_id(BlockType::TallGrass));
}

TEST_CASE("generator exposes all major biome families across a wide sample") {
    WorldGenerator generator(1337);
    std::set<BiomeType> biomes;

    for (int z = -2048; z <= 2048; z += 64) {
        for (int x = -2048; x <= 2048; x += 64) {
            biomes.insert(generator.biome_at(x, z));
        }
    }

    CHECK(biomes.contains(BiomeType::Meadow));
    CHECK(biomes.contains(BiomeType::Forest));
    CHECK(biomes.contains(BiomeType::Desert));
    CHECK(biomes.contains(BiomeType::RockyPeaks));
    CHECK(biomes.contains(BiomeType::Taiga));
}

TEST_CASE("generator keeps meadow and forest vegetation density in the intended range") {
    WorldGenerator generator(1337);
    BiomeVegetationDensity meadow {};
    BiomeVegetationDensity forest {};
    constexpr int kChunkRadius = 8;

    for (int chunk_z = -kChunkRadius; chunk_z <= kChunkRadius; ++chunk_z) {
        for (int chunk_x = -kChunkRadius; chunk_x <= kChunkRadius; ++chunk_x) {
            Chunk chunk({chunk_x, chunk_z});
            generator.generate_chunk(chunk);

            const auto base_world_x = chunk_x * kChunkSizeX;
            const auto base_world_z = chunk_z * kChunkSizeZ;
            for (int local_z = 0; local_z < kChunkSizeZ; ++local_z) {
                for (int local_x = 0; local_x < kChunkSizeX; ++local_x) {
                    const auto world_x = base_world_x + local_x;
                    const auto world_z = base_world_z + local_z;

                    auto* density = static_cast<BiomeVegetationDensity*>(nullptr);
                    switch (generator.biome_at(world_x, world_z)) {
                    case BiomeType::Meadow:
                        density = &meadow;
                        break;
                    case BiomeType::Forest:
                        density = &forest;
                        break;
                    default:
                        break;
                    }

                    if (density == nullptr) {
                        continue;
                    }

                    ++density->sampled_columns;
                    const auto surface_y = terrain_surface_height(generator, world_x, world_z);
                    if (!chunk.in_bounds_local(local_x, surface_y + 1, local_z)) {
                        continue;
                    }

                    const auto above_surface = chunk.get_local(local_x, surface_y + 1, local_z);
                    if (above_surface != to_block_id(BlockType::Water)) {
                        ++density->decoration_candidate_columns;
                        if (surface_y >= 48 && surface_y <= kWorldMaxY - 10) {
                            ++density->tree_candidate_columns;
                        }
                    }

                    if (is_tree_trunk(above_surface)) {
                        ++density->tree_columns;
                    } else if (is_green_decoration(above_surface)) {
                        ++density->decoration_columns;
                    }
                }
            }
        }
    }

    REQUIRE(meadow.sampled_columns > 4000);
    REQUIRE(forest.sampled_columns > 4000);
    REQUIRE(meadow.tree_candidate_columns > 4000);
    REQUIRE(meadow.decoration_candidate_columns > 4000);
    REQUIRE(forest.tree_candidate_columns > 4000);
    REQUIRE(forest.decoration_candidate_columns > 4000);

    const auto meadow_tree_percent = density_percent(meadow.tree_columns, meadow.tree_candidate_columns);
    const auto meadow_decoration_percent = density_percent(meadow.decoration_columns, meadow.decoration_candidate_columns);
    const auto forest_tree_percent = density_percent(forest.tree_columns, forest.tree_candidate_columns);
    const auto forest_decoration_percent = density_percent(forest.decoration_columns, forest.decoration_candidate_columns);

    CAPTURE(meadow.sampled_columns);
    CAPTURE(meadow.tree_candidate_columns);
    CAPTURE(meadow.decoration_candidate_columns);
    CAPTURE(meadow.tree_columns);
    CAPTURE(meadow.decoration_columns);
    CAPTURE(meadow_tree_percent);
    CAPTURE(meadow_decoration_percent);
    CHECK(meadow_tree_percent >= 0.8F);
    CHECK(meadow_tree_percent <= 2.0F);
    CHECK(meadow_decoration_percent >= 5.5F);
    CHECK(meadow_decoration_percent <= 15.0F);

    CAPTURE(forest.sampled_columns);
    CAPTURE(forest.tree_candidate_columns);
    CAPTURE(forest.decoration_candidate_columns);
    CAPTURE(forest.tree_columns);
    CAPTURE(forest.decoration_columns);
    CAPTURE(forest_tree_percent);
    CAPTURE(forest_decoration_percent);
    CHECK(forest_tree_percent >= 2.8F);
    CHECK(forest_tree_percent <= 4.3F);
    CHECK(forest_decoration_percent >= 9.0F);
    CHECK(forest_decoration_percent <= 16.5F);
}

TEST_CASE("coastal sand in meadow and forest biomes stays free of green decorations") {
    WorldGenerator generator(1337);
    int sampled_sandy_columns = 0;
    int green_decorations_on_sand = 0;
    constexpr int kChunkRadius = 10;

    for (int chunk_z = -kChunkRadius; chunk_z <= kChunkRadius; ++chunk_z) {
        for (int chunk_x = -kChunkRadius; chunk_x <= kChunkRadius; ++chunk_x) {
            Chunk chunk({chunk_x, chunk_z});
            generator.generate_chunk(chunk);

            const auto base_world_x = chunk_x * kChunkSizeX;
            const auto base_world_z = chunk_z * kChunkSizeZ;
            for (int local_z = 0; local_z < kChunkSizeZ; ++local_z) {
                for (int local_x = 0; local_x < kChunkSizeX; ++local_x) {
                    const auto world_x = base_world_x + local_x;
                    const auto world_z = base_world_z + local_z;
                    const auto biome = generator.biome_at(world_x, world_z);
                    if (biome != BiomeType::Meadow && biome != BiomeType::Forest) {
                        continue;
                    }

                    const auto surface_y = terrain_surface_height(generator, world_x, world_z);
                    if (!chunk.in_bounds_local(local_x, surface_y + 1, local_z)) {
                        continue;
                    }

                    const auto surface_block = chunk.get_local(local_x, surface_y, local_z);
                    if (surface_block != to_block_id(BlockType::Sand)) {
                        continue;
                    }

                    ++sampled_sandy_columns;
                    const auto above_surface = chunk.get_local(local_x, surface_y + 1, local_z);
                    if (is_green_decoration(above_surface)) {
                        ++green_decorations_on_sand;
                    }
                }
            }
        }
    }

    REQUIRE(sampled_sandy_columns > 1000);
    CHECK(green_decorations_on_sand == 0);
}

TEST_CASE("boundary block edits remesh both chunks touching the border") {
    World world(16, 1);
    const ChunkCoord left {0, 0};
    const ChunkCoord right {1, 0};

    test::make_chunk_empty(world, left);
    test::make_chunk_empty(world, right);
    world.set_block(15, 12, 4, to_block_id(BlockType::Stone));
    world.set_block(16, 12, 4, to_block_id(BlockType::Stone));
    world.rebuild_dirty_meshes();

    const auto left_revision_before = world.mesh_revision(left);
    const auto right_revision_before = world.mesh_revision(right);

    world.set_block(15, 12, 4, to_block_id(BlockType::Air));
    world.rebuild_dirty_meshes();

    CHECK(world.mesh_revision(left) > left_revision_before);
    CHECK(world.mesh_revision(right) > right_revision_before);

    const auto* left_mesh = world.mesh_for(left);
    const auto* right_mesh = world.mesh_for(right);
    REQUIRE(left_mesh != nullptr);
    REQUIRE(right_mesh != nullptr);
    CHECK(left_mesh->face_count == 0);
    CHECK(right_mesh->face_count == 6);
}

TEST_CASE("loading an orthogonal neighbor remeshes an already meshed chunk") {
    World world(83, 2);
    const ChunkCoord origin {0, 0};
    const ChunkCoord east {1, 0};

    test::make_chunk_empty(world, origin);
    world.rebuild_dirty_meshes();

    const auto origin_revision_before = world.mesh_revision(origin);
    REQUIRE(origin_revision_before > 0);

    world.ensure_chunk_loaded(east);
    world.rebuild_dirty_meshes();

    CHECK(world.mesh_revision(origin) > origin_revision_before);
}

TEST_CASE("loading a diagonal neighbor remeshes an already meshed chunk") {
    World world(84, 2);
    const ChunkCoord origin {0, 0};
    const ChunkCoord diagonal {1, 1};

    test::make_chunk_empty(world, origin);
    world.rebuild_dirty_meshes();

    const auto origin_revision_before = world.mesh_revision(origin);
    REQUIRE(origin_revision_before > 0);

    world.ensure_chunk_loaded(diagonal);
    world.rebuild_dirty_meshes();

    CHECK(world.mesh_revision(origin) > origin_revision_before);
}

TEST_CASE("water border meshing matches the eventual generated neighbor even before that chunk is loaded") {
    ChunkMesher mesher {};

    bool found_candidate = false;
    for (const auto seed : std::array<int, 6> {{1337, 2024, 4242, 9001, 12345, 54321}}) {
        for (int chunk_z = -6; chunk_z <= 6 && !found_candidate; ++chunk_z) {
            for (int chunk_x = -6; chunk_x <= 6 && !found_candidate; ++chunk_x) {
                World world(seed, 1);
                const ChunkCoord origin {chunk_x, chunk_z};
                const ChunkCoord east {chunk_x + 1, chunk_z};
                world.ensure_chunk_loaded(origin);
                REQUIRE(world.find_chunk(east) == nullptr);

                bool border_extends_into_generated_neighbor = false;
                for (int local_z = 0; local_z < kChunkSizeZ && !border_extends_into_generated_neighbor; ++local_z) {
                    const auto world_x = origin.x * kChunkSizeX + (kChunkSizeX - 1);
                    const auto world_z = origin.z * kChunkSizeZ + local_z;
                    const auto origin_has_water = world.has_water(world_x, kSeaLevel, world_z);
                    const auto neighbor_water_level = world.peek_water_level_or_generated(world_x + 1, kSeaLevel, world_z);
                    border_extends_into_generated_neighbor = origin_has_water && neighbor_water_level != 0U;
                }

                if (!border_extends_into_generated_neighbor) {
                    continue;
                }

                const auto mesh_before = mesher.build_mesh(world, origin);
                world.ensure_chunk_loaded(east);
                const auto mesh_after = mesher.build_mesh(world, origin);

                CAPTURE(seed);
                CAPTURE(origin.x);
                CAPTURE(origin.z);
                CHECK(mesh_before.water_face_count == mesh_after.water_face_count);
                CHECK(mesh_before.water_vertices.size() == mesh_after.water_vertices.size());
                CHECK(mesh_before.water_indices.size() == mesh_after.water_indices.size());
                found_candidate = true;
            }
        }
    }

    REQUIRE(found_candidate);
}

TEST_CASE("editing a corner block remeshes diagonal neighbors that sample it") {
    World world(193, 2);
    const ChunkCoord origin {0, 0};
    const ChunkCoord diagonal {1, 1};

    test::make_chunk_empty(world, origin);
    test::make_chunk_empty(world, diagonal);
    world.set_block(16, 0, 16, to_block_id(BlockType::Stone));
    world.rebuild_dirty_meshes();

    const auto diagonal_revision_before = world.mesh_revision(diagonal);
    REQUIRE(diagonal_revision_before > 0);

    world.set_block(15, 0, 15, to_block_id(BlockType::Stone));
    world.rebuild_dirty_meshes();

    CHECK(world.mesh_revision(diagonal) > diagonal_revision_before);
}

TEST_CASE("near-player chunk load keeps seam remeshes on the priority path") {
    World world(85, 0);
    world.update_streaming({0.5F, 80.0F, 0.5F});

    const ChunkCoord origin {0, 0};
    const ChunkCoord east {1, 0};
    test::make_chunk_empty(world, origin);
    world.rebuild_dirty_meshes();

    const auto origin_revision_before = world.mesh_revision(origin);
    REQUIRE(origin_revision_before > 0);

    world.ensure_chunk_loaded(east);
    const auto stats = world.process_pending_work({0, 0, 65536});

    CHECK(stats.prioritized_meshed_chunks >= 1);
    CHECK(world.mesh_revision(origin) > origin_revision_before);
}

TEST_CASE("far chunk load defers seam remeshes to the normal mesh budget") {
    World world(86, 0);
    world.update_streaming({0.5F, 80.0F, 0.5F});

    const ChunkCoord far_existing {3, 0};
    const ChunkCoord far_new {4, 0};
    test::make_chunk_empty(world, far_existing);
    world.rebuild_dirty_meshes();

    const auto far_revision_before = world.mesh_revision(far_existing);
    REQUIRE(far_revision_before > 0);

    world.ensure_chunk_loaded(far_new);
    WorldWorkBudget budget {};
    budget.chunk_generation_budget = 0U;
    budget.fluid_cell_budget = 0U;
    budget.mesh_rebuild_budget = 0U;
    budget.light_node_budget = 65536U;
    const auto stats = world.process_pending_work(budget);

    CHECK(stats.prioritized_meshed_chunks == 0);
    CHECK(world.mesh_revision(far_existing) == far_revision_before);
    CHECK(world.pending_mesh_count() >= 1);
}

TEST_CASE("priority seam remeshes can bypass the normal mesh rebuild budget") {
    World world(17, 1);
    test::make_chunk_empty(world, {0, 0});
    test::make_chunk_empty(world, {1, 0});
    world.rebuild_dirty_meshes();

    world.set_block(15, 10, 4, to_block_id(BlockType::Stone));
    world.set_block(16, 10, 4, to_block_id(BlockType::Stone));

    const auto stats = world.process_pending_work({0, 1, 65536});
    CHECK(stats.meshed_chunks == 2);
    CHECK(stats.prioritized_meshed_chunks == 2);
    CHECK(world.pending_mesh_count() == 0);
}

TEST_CASE("overlapping lighting updates coalesce into a single pending job") {
    World world(80, 1);
    test::make_chunk_empty(world, {0, 0});
    test::make_chunk_empty(world, {1, 0});
    world.rebuild_dirty_meshes();

    world.set_block(1, 0, 1, to_block_id(BlockType::Stone));
    world.set_block(16, 0, 1, to_block_id(BlockType::Stone));

    CHECK(world.pending_lighting_count() == 1);
}

TEST_CASE("lighting completion enqueues mesh rebuilds without a global dirty scan") {
    World world(81, 1);
    test::make_chunk_empty(world, {0, 0});
    world.rebuild_dirty_meshes();

    world.set_block(1, 0, 1, to_block_id(BlockType::Stone));
    REQUIRE(world.pending_lighting_count() == 1);

    WorldWorkBudget budget {};
    budget.chunk_generation_budget = 0U;
    budget.fluid_cell_budget = 0U;
    budget.mesh_rebuild_budget = 0U;
    budget.light_node_budget = 65536U;
    const auto stats = world.process_pending_work(budget);
    CHECK(stats.lighting_jobs_completed == 1);
    CHECK(world.pending_mesh_count() >= 1);
}

TEST_CASE("local block lighting only dirties affected mesh sections") {
    World world(194, 1);
    const ChunkCoord origin {0, 0};
    test::make_chunk_empty(world, origin);
    world.set_block(1, 0, 1, to_block_id(BlockType::Stone));
    world.set_block(1, 96, 1, to_block_id(BlockType::Stone));
    test::flush_pending_work(world);

    auto* chunk = world.find_chunk(origin);
    REQUIRE(chunk != nullptr);
    CHECK(chunk->dirty_section_count() == 0);

    world.set_block(1, 1, 1, to_block_id(BlockType::Torch));
    REQUIRE(world.pending_lighting_count() == 1);

    WorldWorkBudget budget {};
    budget.chunk_generation_budget = 0;
    budget.mesh_rebuild_budget = 0;
    budget.light_node_budget = 65536;
    budget.max_generation_ms = 10.0;
    budget.max_lighting_ms = 10.0;
    budget.max_meshing_ms = 0.0;

    const auto stats = world.process_pending_work(budget);
    CHECK(stats.lighting_jobs_completed == 1);

    chunk = world.find_chunk(origin);
    REQUIRE(chunk != nullptr);
    CHECK(chunk->is_section_dirty(0));
    CHECK_FALSE(chunk->is_section_dirty(6));
    CHECK(chunk->dirty_section_count() < kChunkSectionCount);
}

TEST_CASE("sky light changes only remesh the impacted vertical band") {
    World world(195, 1);
    const ChunkCoord origin {0, 0};
    test::make_chunk_empty(world, origin);
    world.set_block(1, 15, 1, to_block_id(BlockType::Stone));
    world.set_block(1, 31, 1, to_block_id(BlockType::Stone));
    test::flush_pending_work(world);

    auto* chunk = world.find_chunk(origin);
    REQUIRE(chunk != nullptr);
    CHECK(chunk->dirty_section_count() == 0);

    world.set_block(1, 31, 1, to_block_id(BlockType::Air));
    REQUIRE(world.pending_lighting_count() == 1);

    WorldWorkBudget budget {};
    budget.chunk_generation_budget = 0;
    budget.mesh_rebuild_budget = 0;
    budget.light_node_budget = 65536;
    budget.max_generation_ms = 10.0;
    budget.max_lighting_ms = 10.0;
    budget.max_meshing_ms = 0.0;

    const auto stats = world.process_pending_work(budget);
    CHECK(stats.lighting_jobs_completed == 1);
    CHECK(world.get_sky_light(1, 30, 1) == 15);
    CHECK(world.get_sky_light(1, 14, 1) == 0);

    chunk = world.find_chunk(origin);
    REQUIRE(chunk != nullptr);
    CHECK(chunk->is_section_dirty(0));
    CHECK(chunk->is_section_dirty(1));
    CHECK(chunk->is_section_dirty(2));
    CHECK_FALSE(chunk->is_section_dirty(6));
    CHECK(chunk->dirty_section_count() < kChunkSectionCount);
}

TEST_CASE("interior sky light changes do not remesh neighboring chunks") {
    World world(197, 1);
    const ChunkCoord origin {0, 0};
    const ChunkCoord east {1, 0};

    test::make_chunk_empty(world, origin);
    test::make_chunk_empty(world, east);
    world.rebuild_dirty_meshes();

    const auto origin_revision_before = world.mesh_revision(origin);
    const auto east_revision_before = world.mesh_revision(east);
    REQUIRE(origin_revision_before > 0);
    REQUIRE(east_revision_before > 0);

    world.set_block(8, 15, 8, to_block_id(BlockType::Stone));
    test::flush_pending_work(world);

    CHECK(world.mesh_revision(origin) > origin_revision_before);
    CHECK(world.mesh_revision(east) == east_revision_before);
}

TEST_CASE("rebuilding a dirty chunk enqueues a GPU upload event") {
    World world(191, 1);
    const ChunkCoord origin {0, 0};
    test::make_chunk_empty(world, origin);

    world.rebuild_dirty_meshes();

    const auto uploads = world.consume_pending_gpu_uploads(8);
    CHECK(std::find(uploads.begin(), uploads.end(), origin) != uploads.end());
    CHECK(world.consume_pending_gpu_uploads(8).empty());
}

TEST_CASE("loaded meshes can be requeued after a renderer reset") {
    World world(193, 1);
    const ChunkCoord origin {0, 0};
    test::make_chunk_empty(world, origin);
    world.rebuild_dirty_meshes();

    const auto initial_uploads = world.consume_pending_gpu_uploads(8);
    REQUIRE(std::find(initial_uploads.begin(), initial_uploads.end(), origin) != initial_uploads.end());
    REQUIRE(world.consume_pending_gpu_uploads(8).empty());
    REQUIRE(world.mesh_revision(origin) > 0);

    world.enqueue_loaded_mesh_uploads();

    const auto restored_uploads = world.consume_pending_gpu_uploads(8);
    CHECK(std::find(restored_uploads.begin(), restored_uploads.end(), origin) != restored_uploads.end());
    CHECK(world.consume_pending_gpu_uploads(8).empty());
}

TEST_CASE("streaming unload enqueues a GPU unload event") {
    World world(192, 0);
    world.update_streaming({0.5F, 70.0F, 0.5F});
    test::flush_pending_work(world);
    const ChunkCoord origin {0, 0};

    CHECK_FALSE(world.consume_pending_gpu_uploads(64).empty());

    world.update_streaming({static_cast<float>(kChunkSizeX * 4) + 0.5F, 70.0F, 0.5F});

    const auto unloads = world.consume_pending_gpu_unloads(8);
    CHECK(std::find(unloads.begin(), unloads.end(), origin) != unloads.end());
}

TEST_CASE("unloading a chunk remeshes diagonal neighbors after the chunk disappears") {
    World world(196, 0);
    const ChunkCoord origin {0, 0};
    const ChunkCoord diagonal {1, 1};

    world.update_streaming({0.5F, 70.0F, 0.5F});
    test::make_chunk_empty(world, origin);
    test::make_chunk_empty(world, diagonal);
    world.set_block(16, 0, 16, to_block_id(BlockType::Stone));
    world.rebuild_dirty_meshes();

    const auto diagonal_revision_before = world.mesh_revision(diagonal);
    REQUIRE(diagonal_revision_before > 0);

    world.update_streaming({
        static_cast<float>(kChunkSizeX * 2) + 0.5F,
        70.0F,
        static_cast<float>(kChunkSizeZ * 2) + 0.5F,
    });
    test::flush_pending_work(world);

    CHECK(world.find_chunk(origin) == nullptr);
    CHECK(world.find_chunk(diagonal) != nullptr);
    CHECK(world.mesh_revision(diagonal) > diagonal_revision_before);
}

TEST_CASE("zero lighting time budget defers pending lighting work") {
    World world(87, 1);
    test::make_chunk_empty(world, {0, 0});
    world.set_block(1, 0, 1, to_block_id(BlockType::Stone));
    world.set_block(1, 1, 1, to_block_id(BlockType::Torch));
    REQUIRE(world.pending_lighting_count() == 1);

    WorldWorkBudget budget {};
    budget.chunk_generation_budget = 0;
    budget.mesh_rebuild_budget = 0;
    budget.light_node_budget = 65536;
    budget.max_generation_ms = 10.0;
    budget.max_lighting_ms = 0.0;
    budget.max_meshing_ms = 10.0;

    const auto stats = world.process_pending_work(budget);

    CHECK(stats.light_nodes_processed == 0);
    CHECK(stats.lighting_jobs_completed == 0);
    CHECK(world.pending_lighting_count() == 1);
}

TEST_CASE("local lighting updates do not remesh unrelated chunks") {
    World world(79, 2);
    const ChunkCoord origin {0, 0};
    const ChunkCoord neighbor {1, 0};
    const ChunkCoord far {2, 0};

    test::make_chunk_empty(world, origin);
    test::make_chunk_empty(world, neighbor);
    test::make_chunk_empty(world, far);
    world.rebuild_dirty_meshes();

    const auto far_revision_before = world.mesh_revision(far);

    world.set_block(1, 0, 1, to_block_id(BlockType::Stone));
    world.set_block(1, 1, 1, to_block_id(BlockType::Torch));
    world.rebuild_dirty_meshes();

    CHECK(world.mesh_revision(origin) > 0);
    CHECK(world.mesh_revision(neighbor) > 0);
    CHECK(world.mesh_revision(far) == far_revision_before);
}

TEST_CASE("lighting keeps diagonal chunks outside the fixed five-slot region but still remeshes sampled diagonals") {
    World world(190, 2);
    const ChunkCoord origin {0, 0};
    const ChunkCoord east {1, 0};
    const ChunkCoord south {0, 1};
    const ChunkCoord diagonal {1, 1};

    test::make_chunk_empty(world, origin);
    test::make_chunk_empty(world, east);
    test::make_chunk_empty(world, south);
    test::make_chunk_empty(world, diagonal);
    world.set_block(16, 0, 16, to_block_id(BlockType::Stone));
    world.rebuild_dirty_meshes();

    const auto diagonal_revision_before = world.mesh_revision(diagonal);
    REQUIRE(diagonal_revision_before > 0);

    world.set_block(15, 0, 15, to_block_id(BlockType::Stone));
    world.set_block(15, 1, 15, to_block_id(BlockType::Torch));
    test::flush_pending_work(world);

    CHECK(world.get_block_light(16, 1, 16) == 0);
    CHECK(world.mesh_revision(diagonal) > diagonal_revision_before);
}

TEST_CASE("placing and removing a torch only remeshes nearby chunks") {
    World world(82, 2);
    const ChunkCoord origin {0, 0};
    const ChunkCoord neighbor {1, 0};
    const ChunkCoord far {2, 0};

    test::make_chunk_empty(world, origin);
    test::make_chunk_empty(world, neighbor);
    test::make_chunk_empty(world, far);
    world.rebuild_dirty_meshes();

    const auto far_revision_before = world.mesh_revision(far);
    world.set_block(1, 0, 1, to_block_id(BlockType::Stone));
    world.set_block(1, 1, 1, to_block_id(BlockType::Torch));
    world.rebuild_dirty_meshes();

    const auto origin_revision_after_place = world.mesh_revision(origin);
    REQUIRE(origin_revision_after_place > 0);
    CHECK(world.mesh_revision(far) == far_revision_before);

    world.set_block(1, 1, 1, to_block_id(BlockType::Air));
    world.rebuild_dirty_meshes();

    CHECK(world.mesh_revision(origin) > origin_revision_after_place);
    CHECK(world.mesh_revision(far) == far_revision_before);
}

TEST_CASE("zero meshing time budget defers queued mesh rebuilds") {
    World world(89, 1);
    test::make_chunk_empty(world, {0, 0});
    world.set_block(1, 1, 1, to_block_id(BlockType::Stone));
    world.rebuild_lighting();
    REQUIRE(world.pending_mesh_count() >= 1);

    WorldWorkBudget budget {};
    budget.chunk_generation_budget = 0;
    budget.mesh_rebuild_budget = 16;
    budget.light_node_budget = 0;
    budget.max_generation_ms = 10.0;
    budget.max_lighting_ms = 10.0;
    budget.max_meshing_ms = 0.0;

    const auto stats = world.process_pending_work(budget);

    CHECK(stats.meshed_chunks == 0);
    CHECK(world.pending_mesh_count() >= 1);
}

TEST_CASE("re-running unchanged lighting does not force another remesh") {
    World world(88, 1);
    const ChunkCoord origin {0, 0};
    test::make_chunk_empty(world, origin);
    world.set_block(1, 0, 1, to_block_id(BlockType::Stone));
    world.set_block(1, 1, 1, to_block_id(BlockType::Torch));
    test::flush_pending_work(world);

    const auto revision_before = world.mesh_revision(origin);
    REQUIRE(revision_before > 0);

    auto* chunk = world.find_chunk(origin);
    REQUIRE(chunk != nullptr);
    chunk->mark_lighting_dirty();

    world.rebuild_dirty_meshes();

    CHECK(world.mesh_revision(origin) == revision_before);
}

TEST_CASE("partial section rebuild preserves untouched section geometry") {
    World world(195, 1);
    const ChunkCoord origin {0, 0};
    test::make_chunk_empty(world, origin);
    world.set_block(2, 4, 2, to_block_id(BlockType::Stone));
    world.set_block(3, 96, 3, to_block_id(BlockType::Stone));
    world.rebuild_dirty_meshes();

    auto* mesh = world.mesh_for(origin);
    REQUIRE(mesh != nullptr);
    CHECK(mesh->face_count == 12);

    const auto revision_before = world.mesh_revision(origin);
    world.set_block(2, 4, 2, to_block_id(BlockType::Air));
    world.rebuild_dirty_meshes();

    CHECK(world.mesh_revision(origin) > revision_before);
    mesh = world.mesh_for(origin);
    REQUIRE(mesh != nullptr);
    CHECK(mesh->face_count == 6);
    CHECK(std::any_of(mesh->vertices.begin(), mesh->vertices.end(), [](const ChunkVertex& vertex) {
        return vertex.y > 90.0F;
    }));
}

TEST_CASE("environment curve is brightest at noon and remains readable at midnight") {
    const auto noon = EnvironmentClock::compute_state(12.0F);
    const auto dusk = EnvironmentClock::compute_state(18.5F);
    const auto midnight = EnvironmentClock::compute_state(0.0F);

    CHECK(noon.daylight_factor > dusk.daylight_factor);
    CHECK(dusk.daylight_factor > midnight.daylight_factor);
    CHECK(midnight.daylight_factor >= 0.18F);
}

TEST_CASE("environment state keeps day crisp dusk warm and night cool with softer night sky contrast") {
    const auto noon = EnvironmentClock::compute_state(12.0F);
    const auto dawn = EnvironmentClock::compute_state(5.5F);
    const auto dusk = EnvironmentClock::compute_state(18.5F);
    const auto midnight = EnvironmentClock::compute_state(0.0F);
    const auto noon_sky_span = glm::length(noon.sky_zenith_color - noon.sky_horizon_color);
    const auto midnight_sky_span = glm::length(midnight.sky_zenith_color - midnight.sky_horizon_color);

    CHECK(noon.star_intensity < 0.05F);
    CHECK(midnight.star_intensity > 0.70F);
    CHECK(dusk.horizon_glow_color.r > noon.horizon_glow_color.r);
    CHECK(noon.exposure > midnight.exposure);
    CHECK(midnight.vignette_strength > noon.vignette_strength);
    CHECK(dusk.glow_strength >= noon.glow_strength);
    CHECK(noon.cloud_intensity > midnight.cloud_intensity);
    CHECK(dusk.cloud_intensity > midnight.cloud_intensity);
    CHECK(dusk.sky_horizon_color.r > dusk.sky_zenith_color.r);
    CHECK(dusk.sky_horizon_color.r > dusk.sky_horizon_color.g);
    CHECK(dusk.sky_horizon_color.g > dusk.sky_horizon_color.b);
    CHECK(dusk.sky_horizon_color.r > dusk.sky_horizon_color.b + 0.32F);
    CHECK(dusk.horizon_glow_color.r > 0.65F);
    CHECK(dusk.sun_disk_color.r > dusk.sun_disk_color.g + 0.20F);
    CHECK(dawn.sky_horizon_color.r > dawn.sky_horizon_color.g);
    CHECK(dawn.sky_horizon_color.g > dawn.sky_horizon_color.b);
    CHECK(dawn.horizon_glow_color.r > 0.65F);
    CHECK(noon.sky_horizon_color.b > noon.sky_horizon_color.r);
    CHECK(midnight.sky_zenith_color.b > midnight.sky_zenith_color.r);
    CHECK(midnight.fog_color.b > midnight.fog_color.r);
    CHECK(noon_sky_span > 0.16F);
    CHECK(midnight_sky_span < noon_sky_span * 0.35F);
    CHECK(midnight.distant_fog_color.b > midnight.night_tint_color.b);
    CHECK(noon.distant_fog_color.r < 0.66F);
    CHECK(noon.distant_fog_color.b < 0.94F);
    CHECK(noon.cloud_shadow_strength > midnight.cloud_shadow_strength);
    CHECK(dusk.atmospheric_scatter_strength > noon.atmospheric_scatter_strength);
    CHECK(midnight.height_fog_density > noon.height_fog_density);
    CHECK(noon.atmospheric_scatter_strength < 0.075F);
    CHECK(noon.height_fog_density < 0.0030F);
    CHECK(noon.post_sharpen_strength > midnight.post_sharpen_strength);
    CHECK(dusk.post_edge_strength >= noon.post_edge_strength);
    CHECK(noon.wind_strength > 0.20F);
}

TEST_CASE("storm weather attenuates cinematic twilight colors without removing them") {
    constexpr std::uint32_t seed = 321987U;
    const auto clear_dusk = EnvironmentClock::compute_state(18.5F, seed, 0.0F);
    EnvironmentState storm_dusk {};
    bool found_storm = false;

    for (int slot = 1; slot < 2048 && !found_storm; ++slot) {
        const auto state = EnvironmentClock::compute_state(18.5F, seed, static_cast<float>(slot) * 240.0F + 120.0F);
        if (state.storm_intensity > 0.30F) {
            storm_dusk = state;
            found_storm = true;
        }
    }

    REQUIRE(found_storm);
    CHECK(clear_dusk.sky_horizon_color.r > clear_dusk.sky_horizon_color.g);
    CHECK(storm_dusk.sky_horizon_color.r > storm_dusk.sky_horizon_color.b);
    CHECK(storm_dusk.overcast_intensity > clear_dusk.overcast_intensity);
    CHECK((storm_dusk.sky_horizon_color.r - storm_dusk.sky_horizon_color.b) <
          (clear_dusk.sky_horizon_color.r - clear_dusk.sky_horizon_color.b));
    CHECK((storm_dusk.horizon_glow_color.r - storm_dusk.horizon_glow_color.b) <
          (clear_dusk.horizon_glow_color.r - clear_dusk.horizon_glow_color.b));
}

TEST_CASE("weather cycle stays mostly fair while still producing rain and storms") {
    constexpr std::uint32_t seed = 424242U;
    constexpr int sampled_slots = 2048;
    int fair_weather_slots = 0;
    int rainy_slots = 0;
    bool saw_overcast = false;
    bool saw_storm = false;
    bool saw_tempest = false;

    for (int slot = 0; slot < sampled_slots; ++slot) {
        const auto state = EnvironmentClock::compute_state(
            12.0F,
            seed,
            static_cast<float>(slot) * 240.0F + 120.0F);
        if (state.weather == WeatherKind::Clear || state.weather == WeatherKind::PartlyCloudy) {
            ++fair_weather_slots;
        }
        if (state.precipitation_intensity > 0.10F) {
            ++rainy_slots;
        }
        saw_overcast = saw_overcast || state.weather == WeatherKind::Overcast;
        saw_storm = saw_storm || state.storm_intensity > 0.30F;
        saw_tempest = saw_tempest || state.weather == WeatherKind::Tempest;
    }

    CHECK(static_cast<float>(fair_weather_slots) / static_cast<float>(sampled_slots) > 0.64F);
    CHECK(rainy_slots > 0);
    CHECK(saw_overcast);
    CHECK(saw_storm);
    CHECK(saw_tempest);
}

TEST_CASE("weather transitions blend cloud cover instead of jumping abruptly") {
    constexpr std::uint32_t seed = 9001U;
    bool checked_transition = false;

    for (int slot = 1; slot < 256 && !checked_transition; ++slot) {
        const auto boundary = static_cast<float>(slot) * 240.0F;
        const auto before = EnvironmentClock::compute_state(12.0F, seed, boundary - 1.0F);
        const auto middle = EnvironmentClock::compute_state(12.0F, seed, boundary + 21.0F);
        const auto after = EnvironmentClock::compute_state(12.0F, seed, boundary + 84.0F);
        const auto min_cloud = std::min(before.cloud_intensity, after.cloud_intensity);
        const auto max_cloud = std::max(before.cloud_intensity, after.cloud_intensity);
        if (max_cloud - min_cloud < 0.25F) {
            continue;
        }

        CHECK(middle.weather_transition_factor == doctest::Approx(0.5F).epsilon(0.04));
        CHECK(middle.cloud_intensity > min_cloud + 0.04F);
        CHECK(middle.cloud_intensity < max_cloud - 0.04F);
        checked_transition = true;
    }

    CHECK(checked_transition);
}

TEST_CASE("rain and storm weather alter lighting wind and precipitation coherently") {
    constexpr std::uint32_t seed = 321987U;
    auto clear_state = EnvironmentClock::compute_state(12.0F, seed, 0.0F);
    EnvironmentState rain_state {};
    EnvironmentState storm_state {};
    bool found_rain = false;
    bool found_storm = false;

    for (int slot = 1; slot < 2048 && (!found_rain || !found_storm); ++slot) {
        const auto state = EnvironmentClock::compute_state(12.0F, seed, static_cast<float>(slot) * 240.0F + 120.0F);
        if (!found_rain && state.precipitation_intensity > 0.30F && state.storm_intensity < 0.05F) {
            rain_state = state;
            found_rain = true;
        }
        if (!found_storm && state.storm_intensity > 0.30F) {
            storm_state = state;
            found_storm = true;
        }
    }

    REQUIRE(found_rain);
    REQUIRE(found_storm);
    CHECK(clear_state.weather == WeatherKind::Clear);
    CHECK(clear_state.cloud_intensity < 0.12F);
    CHECK(rain_state.overcast_intensity > clear_state.overcast_intensity);
    CHECK(rain_state.precipitation_intensity > 0.30F);
    CHECK(rain_state.exposure < clear_state.exposure);
    CHECK(storm_state.storm_intensity > rain_state.storm_intensity);
    CHECK(storm_state.wind_strength > rain_state.wind_strength);
    CHECK(storm_state.cloud_shadow_strength > clear_state.cloud_shadow_strength);
    CHECK(storm_state.saturation_boost < clear_state.saturation_boost);
}

TEST_CASE("weather cycle keeps environment values finite and renderer-safe") {
    constexpr std::array<std::uint32_t, 5> seeds {1U, 1337U, 424242U, 987654U, 0xffffffffU};
    constexpr std::array<float, 7> times_of_day {0.0F, 5.5F, 6.0F, 12.0F, 18.0F, 18.5F, 23.5F};

    for (const auto seed : seeds) {
        for (int slot = 0; slot < 96; ++slot) {
            for (const auto time_of_day : times_of_day) {
                const auto weather_time = static_cast<float>(slot) * 240.0F + 21.0F;
                const auto state = EnvironmentClock::compute_state(time_of_day, seed, weather_time);
                CAPTURE(seed);
                CAPTURE(slot);
                CAPTURE(time_of_day);
                CHECK(state.time_of_day >= 0.0F);
                CHECK(state.time_of_day < 24.0F);
                CHECK(state.weather_time_seconds == doctest::Approx(weather_time));
                CHECK(state.daylight_factor >= 0.18F);
                CHECK(state.daylight_factor <= 1.0F);
                CHECK(state.cloud_intensity >= 0.0F);
                CHECK(state.cloud_intensity <= 1.0F);
                CHECK(state.overcast_intensity >= 0.0F);
                CHECK(state.overcast_intensity <= 1.0F);
                CHECK(state.precipitation_intensity >= 0.0F);
                CHECK(state.precipitation_intensity <= 1.0F);
                CHECK(state.storm_intensity >= 0.0F);
                CHECK(state.storm_intensity <= 1.0F);
                CHECK(state.lightning_intensity >= 0.0F);
                CHECK(state.lightning_intensity <= 1.0F);
                CHECK(state.weather_transition_factor >= 0.0F);
                CHECK(state.weather_transition_factor <= 1.0F);
                CHECK(state.cloud_shadow_strength >= 0.0F);
                CHECK(state.cloud_shadow_strength <= 1.0F);
                CHECK(state.wind_strength >= 0.0F);
                CHECK(state.wind_strength <= 1.0F);
                CHECK(state.height_fog_density >= 0.0F);
                CHECK(state.height_fog_density < 0.012F);
                CHECK(state.atmospheric_scatter_strength >= 0.0F);
                CHECK(state.atmospheric_scatter_strength < 0.16F);
                CHECK(finite_vec3(state.sun_direction));
                CHECK(finite_vec3(state.sun_color));
                CHECK(finite_vec3(state.ambient_color));
                CHECK(finite_vec3(state.fog_color));
                CHECK(finite_vec3(state.sky_zenith_color));
                CHECK(finite_vec3(state.sky_horizon_color));
                CHECK(finite_vec3(state.distant_fog_color));
                CHECK(vec3_components_at_least(state.sun_color, 0.0F));
                CHECK(vec3_components_at_least(state.ambient_color, 0.0F));
                CHECK(vec3_components_at_least(state.fog_color, 0.0F));
                CHECK(vec3_components_at_least(state.sky_zenith_color, 0.0F));
                CHECK(vec3_components_at_least(state.sky_horizon_color, 0.0F));
            }
        }
    }
}

TEST_CASE("environment clock sanitizes non finite time inputs") {
    const auto normalized_nan = EnvironmentClock::normalize_time_of_day(std::numeric_limits<float>::quiet_NaN());
    CHECK(normalized_nan == doctest::Approx(8.0F));

    EnvironmentClock clock(std::numeric_limits<float>::infinity(), false);
    CHECK(clock.time_of_day() == doctest::Approx(8.0F));

    const auto state = EnvironmentClock::compute_state(std::numeric_limits<float>::quiet_NaN(), 1337U, 0.0F);
    CHECK(state.time_of_day == doctest::Approx(8.0F));
    CHECK(finite_vec3(state.sun_direction));
    CHECK(finite_vec3(state.sun_color));
}

TEST_CASE("environment clock respects freeze mode") {
    EnvironmentClock frozen_clock(8.0F, true);
    frozen_clock.update(120.0F);
    CHECK(frozen_clock.time_of_day() == doctest::Approx(8.0F));
    CHECK(frozen_clock.weather_time_seconds() == doctest::Approx(0.0F));

    EnvironmentClock running_clock(8.0F, false);
    running_clock.update(30.0F);
    CHECK(running_clock.time_of_day() == doctest::Approx(9.0F).epsilon(0.01));
    CHECK(running_clock.weather_time_seconds() == doctest::Approx(30.0F));
}

TEST_CASE("performance report formatting includes frame and scheduler counters") {
    PerformanceReportMetadata metadata {};
    metadata.scenario = "baseline";
    metadata.post_process_enabled = false;
    std::vector<FramePerformanceSample> samples {
        {0, 13.0, 1.0, 1.0, 2.0, 3.0, 0.4, 0.1, 0.3, 3, 4, 42, 6, 8, 3, 2, 3, 12, 3, 2, 7, 30, 20, 30, PerformanceStage::Unattributed},
        {1, 20.0, 0.8, 1.5, 2.5, 2.0, 0.2, 0.15, 0.35, 2, 3, 21, 3, 10, 2, 1, 2, 0, 0, 0, 0, 26, 18, 26, PerformanceStage::Unattributed},
    };

    const auto report = format_performance_report(build_performance_report(metadata, samples, false));

    CHECK(report.find("frame_total_ms_avg=") != std::string::npos);
    CHECK(report.find("p95=") != std::string::npos);
    CHECK(report.find("render_flags shadows=on post_process=off") != std::string::npos);
    CHECK(report.find("pending_generation_avg=") != std::string::npos);
    CHECK(report.find("lag_frames_16_7=") != std::string::npos);
    CHECK(report.find("scheduler_stream_changes=") != std::string::npos);
    CHECK(report.find("jobs_total=7") != std::string::npos);
}

} // namespace valcraft
