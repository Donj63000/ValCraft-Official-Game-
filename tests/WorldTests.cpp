#include "app/PerformanceReport.h"
#include "gameplay/SeaAdventure.h"
#include "gameplay/StartingVillage.h"
#include "world/World.h"
#include "world/BlockVisuals.h"
#include "world/ChunkMesher.h"
#include "world/Environment.h"
#include "world/OceanSimulation.h"
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
#include <string_view>
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

constexpr std::uint64_t kGenerationGoldenFnvOffset = 14695981039346656037ULL;
constexpr std::uint64_t kGenerationGoldenFnvPrime = 1099511628211ULL;

void hash_generation_golden_byte(std::uint64_t& hash, std::uint8_t value) noexcept {
    hash ^= value;
    hash *= kGenerationGoldenFnvPrime;
}

void hash_generation_golden_u32(std::uint64_t& hash, std::uint32_t value) noexcept {
    for (int shift = 0; shift < 32; shift += 8) {
        hash_generation_golden_byte(
            hash,
            static_cast<std::uint8_t>((value >> static_cast<unsigned>(shift)) & 0xFFU));
    }
}

auto legacy_ocean_generation_golden_checksum() -> std::uint64_t {
    struct GoldenChunkCase {
        int seed = 0;
        ChunkCoord coord {};
    };
    constexpr std::array<GoldenChunkCase, 4> chunk_cases {{
        {424242, {0, 0}},
        {424242, {7, 34}},
        {-9081, {-6, 19}},
        {17, {13, -11}},
    }};
    constexpr std::array<std::pair<int, int>, 6> sample_columns {{
        {0, 0},
        {-73, 41},
        {112, 544},
        {-96, 1199},
        {31, -207},
        {255, 4096},
    }};
    constexpr std::array<int, 9> sample_heights {{0, 7, 19, 31, 42, 51, 52, 63, 91}};

    auto hash = kGenerationGoldenFnvOffset;
    for (const auto& chunk_case : chunk_cases) {
        WorldGenerator generator(
            chunk_case.seed,
            WorldGenerationProfile::OceanAdventure,
            WorldGenerationVersion::LegacyV1);
        hash_generation_golden_u32(hash, static_cast<std::uint32_t>(chunk_case.seed));
        hash_generation_golden_u32(hash, static_cast<std::uint32_t>(chunk_case.coord.x));
        hash_generation_golden_u32(hash, static_cast<std::uint32_t>(chunk_case.coord.z));

        Chunk chunk(chunk_case.coord);
        generator.generate_chunk(chunk);
        for (const auto block : chunk.blocks()) {
            hash_generation_golden_byte(hash, block);
        }
        for (const auto water : chunk.water_state()) {
            hash_generation_golden_byte(hash, water);
        }

        for (const auto& [world_x, world_z] : sample_columns) {
            const auto surface = generator.sample_surface(world_x, world_z);
            hash_generation_golden_byte(hash, static_cast<std::uint8_t>(surface.biome));
            hash_generation_golden_u32(hash, static_cast<std::uint32_t>(surface.surface_height));
            hash_generation_golden_u32(hash, static_cast<std::uint32_t>(surface.water_level));
            hash_generation_golden_byte(hash, surface.surface_block);
            for (const auto y : sample_heights) {
                hash_generation_golden_byte(hash, generator.sample_block(world_x, y, world_z));
                hash_generation_golden_byte(hash, generator.sample_water_state(world_x, y, world_z));
            }
        }
    }
    return hash;
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

auto water_volume_in_box(World& world, int min_x, int max_x, int min_y, int max_y, int min_z, int max_z) -> int {
    auto volume = 0;
    for (int y = min_y; y <= max_y; ++y) {
        for (int z = min_z; z <= max_z; ++z) {
            for (int x = min_x; x <= max_x; ++x) {
                volume += static_cast<int>(world.water_level(x, y, z));
            }
        }
    }
    return volume;
}

auto loaded_water_state_at(const World& world, int x, int y, int z) -> WaterState {
    const auto* chunk = world.find_chunk(world.world_to_chunk(x, z));
    if (chunk == nullptr) {
        return 0;
    }
    const auto local = world.world_to_local(x, y, z);
    return chunk->get_water_state_local(local.x, local.y, local.z);
}

auto infinite_water_cells_in_box(const World& world, int min_x, int max_x, int min_y, int max_y, int min_z, int max_z) -> int {
    auto cells = 0;
    for (int y = min_y; y <= max_y; ++y) {
        for (int z = min_z; z <= max_z; ++z) {
            for (int x = min_x; x <= max_x; ++x) {
                if (water_state_is_infinite(loaded_water_state_at(world, x, y, z))) {
                    ++cells;
                }
            }
        }
    }
    return cells;
}

auto fluid_only_budget(std::size_t fluid_cell_budget, double max_fluid_ms = std::numeric_limits<double>::infinity()) -> WorldWorkBudget {
    WorldWorkBudget budget {};
    budget.chunk_generation_budget = 0U;
    budget.fluid_cell_budget = fluid_cell_budget;
    budget.mesh_rebuild_budget = 0U;
    budget.light_node_budget = 0U;
    budget.max_generation_ms = std::numeric_limits<double>::infinity();
    budget.max_fluid_ms = max_fluid_ms;
    budget.max_lighting_ms = std::numeric_limits<double>::infinity();
    budget.max_meshing_ms = std::numeric_limits<double>::infinity();
    return budget;
}

void place_infinite_test_water(World& world, int x, int y, int z) {
    world.set_block(x, y, z, to_block_id(BlockType::Water));
    auto* chunk = world.find_chunk(world.world_to_chunk(x, z));
    REQUIRE(chunk != nullptr);
    const auto local = world.world_to_local(x, y, z);
    chunk->set_water_state_local(local.x, local.y, local.z, make_water_state(kMaxWaterLevel, true, true));
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

TEST_CASE("ocean adventure profile generates broad water routes with reachable islands") {
    WorldGenerator generator(424242, WorldGenerationProfile::OceanAdventure);
    int ocean_columns = 0;
    int island_columns = 0;
    int beach_columns = 0;

    for (int world_z = -384; world_z <= 384; world_z += 16) {
        for (int world_x = -384; world_x <= 384; world_x += 16) {
            const auto surface = generator.sample_surface(world_x, world_z);
            if (surface.water_level > surface.surface_height) {
                ++ocean_columns;
                CHECK(generator.sample_water_state(world_x, surface.water_level, world_z) != 0);
                continue;
            }

            ++island_columns;
            if (surface.surface_height <= kSeaLevel + 2) {
                ++beach_columns;
                CHECK(surface.surface_block == to_block_id(BlockType::Sand));
            }
            CHECK(surface.surface_block == generator.sample_block(world_x, surface.surface_height, world_z));
        }
    }

    CHECK(generator.profile() == WorldGenerationProfile::OceanAdventure);
    CHECK(ocean_columns > island_columns * 2);
    CHECK(island_columns > 0);
    CHECK(beach_columns > 0);
}

TEST_CASE("world generation versions resolve explicitly and keep legacy ocean terrain available") {
    CHECK(resolve_world_generation_version(WorldGenerationProfile::Continental) ==
          WorldGenerationVersion::LegacyV1);
    CHECK(resolve_world_generation_version(WorldGenerationProfile::OceanAdventure) ==
          WorldGenerationVersion::SparseArchipelagoV2);

    constexpr auto seed = 424242;
    WorldGenerator latest(
        seed,
        WorldGenerationProfile::OceanAdventure,
        WorldGenerationVersion::Latest);
    WorldGenerator legacy(
        seed,
        WorldGenerationProfile::OceanAdventure,
        WorldGenerationVersion::LegacyV1);
    WorldGenerator sparse(
        seed,
        WorldGenerationProfile::OceanAdventure,
        WorldGenerationVersion::SparseArchipelagoV2);

    CHECK(latest.generation_version() == WorldGenerationVersion::SparseArchipelagoV2);
    CHECK(legacy.generation_version() == WorldGenerationVersion::LegacyV1);
    CHECK(sparse.generation_version() == WorldGenerationVersion::SparseArchipelagoV2);

    auto found_distinct_column = false;
    for (int world_z = 480; world_z <= 720 && !found_distinct_column; world_z += 8) {
        for (int world_x = -112; world_x <= 112; world_x += 8) {
            const auto legacy_surface = legacy.sample_surface(world_x, world_z);
            const auto sparse_surface = sparse.sample_surface(world_x, world_z);
            if (legacy_surface.surface_height != sparse_surface.surface_height ||
                legacy_surface.water_level != sparse_surface.water_level ||
                legacy_surface.surface_block != sparse_surface.surface_block) {
                found_distinct_column = true;
                break;
            }
        }
    }
    CHECK(found_distinct_column);

    World ocean_world(seed, 0, WorldGenerationProfile::OceanAdventure);
    CHECK(ocean_world.generation_version() == WorldGenerationVersion::SparseArchipelagoV2);
    CHECK(ocean_world.capture_save_plan().generation_version ==
          WorldGenerationVersion::SparseArchipelagoV2);
    CHECK_THROWS_AS(
        WorldGenerator(
            seed,
            WorldGenerationProfile::Continental,
            WorldGenerationVersion::SparseArchipelagoV2),
        std::invalid_argument);
}

TEST_CASE("legacy ocean generation V1 keeps its exact historical output") {
    const auto checksum = legacy_ocean_generation_golden_checksum();
    // Je fige ici blocs, eau, biomes et decorations de plusieurs zones V1 afin
    // qu'une optimisation future ne transforme jamais les anciennes parties.
    CHECK_MESSAGE(
        checksum == 0xD31C813857218B8BULL,
        "Legacy ocean V1 checksum: ",
        checksum);
}

TEST_CASE("sparse ocean route schedules one island window every five to seven hundred blocks") {
    constexpr std::array<int, 4> seeds {{17, 424242, -9081, 1337}};
    for (const auto seed : seeds) {
        auto previous_start = std::int64_t {0};
        for (int sector = 0; sector < 16; ++sector) {
            const auto start = ocean_route_island_window_start_z(seed, sector);
            const auto sector_min = static_cast<std::int64_t>(sector) * kOceanRouteMacroSectorLength +
                                    kOceanRouteFirstIslandWindowMinZ;
            CAPTURE(seed);
            CAPTURE(sector);
            CAPTURE(start);
            CHECK(start >= sector_min);
            CHECK(start <= sector_min + kOceanRouteIslandWindowJitter);
            if (sector > 0) {
                const auto spacing = start - previous_start;
                CAPTURE(spacing);
                CHECK(spacing >= 500);
                CHECK(spacing <= 700);
            }
            previous_start = start;
        }

        WorldGenerator generator(
            seed,
            WorldGenerationProfile::OceanAdventure,
            WorldGenerationVersion::SparseArchipelagoV2);
        for (int sector = 0; sector < 6; ++sector) {
            const auto window_start = ocean_route_island_window_start_z(seed, sector);
            auto found_land = false;
            for (int dz = 0; dz < kOceanRouteIslandWindowLength && !found_land; dz += 2) {
                for (int world_x = -120; world_x <= 120; world_x += 2) {
                    const auto surface = generator.sample_surface(
                        world_x,
                        static_cast<int>(window_start) + dz);
                    if (surface.water_level <= surface.surface_height) {
                        found_land = true;
                        break;
                    }
                }
            }
            CAPTURE(seed);
            CAPTURE(sector);
            CHECK(found_land);

            const auto next_window_start = ocean_route_island_window_start_z(seed, sector + 1);
            auto unexpected_gap_land = false;
            const auto gap_min_z = static_cast<int>(window_start) + kOceanRouteIslandWindowLength + 16;
            const auto gap_max_z = static_cast<int>(next_window_start) - 16;
            for (int world_z = gap_min_z; world_z <= gap_max_z && !unexpected_gap_land; world_z += 16) {
                for (int world_x = -120; world_x <= 120; world_x += 4) {
                    const auto surface = generator.sample_surface(world_x, world_z);
                    if (surface.water_level <= surface.surface_height) {
                        unexpected_gap_land = true;
                        break;
                    }
                }
            }
            CHECK_FALSE(unexpected_gap_land);
        }
    }
}

TEST_CASE("sparse ocean keeps natural land outside the twenty four block route clearance") {
    constexpr std::array<int, 4> seeds {{17, 424242, -9081, 1337}};
    constexpr std::array<int, 8> route_z_samples {{
        kOceanNavigationCorridorStartZ,
        0,
        499,
        550,
        1150,
        4096,
        8192,
        10000,
    }};

    for (const auto seed : seeds) {
        WorldGenerator generator(
            seed,
            WorldGenerationProfile::OceanAdventure,
            WorldGenerationVersion::SparseArchipelagoV2);
        for (const auto world_z : route_z_samples) {
            for (int world_x = -kOceanNaturalLandExclusionHalfWidth + 1;
                 world_x < kOceanNaturalLandExclusionHalfWidth;
                 ++world_x) {
                const auto surface = generator.sample_surface(world_x, world_z);
                CAPTURE(seed);
                CAPTURE(world_x);
                CAPTURE(world_z);
                CHECK(surface.surface_height < kSeaLevel);
                CHECK(surface.water_level == kSeaLevel);
                if (std::abs(world_x) <= kOceanNavigationCorridorHalfWidth) {
                    CHECK(surface.surface_height <= kOceanNavigationCorridorMaxSeabedY);
                }
            }
        }
    }
}

TEST_CASE("sparse ocean off route archipelago keeps twelve to twenty percent emerged land") {
    constexpr std::array<int, 4> seeds {{17, 424242, -9081, 1337}};
    for (const auto seed : seeds) {
        WorldGenerator generator(
            seed,
            WorldGenerationProfile::OceanAdventure,
            WorldGenerationVersion::SparseArchipelagoV2);
        auto emerged_columns = 0;
        auto sampled_columns = 0;
        for (int world_z = -4096; world_z < 4096; world_z += 8) {
            for (int world_x = 256; world_x < 4352; world_x += 8) {
                const auto surface = generator.sample_surface(world_x, world_z);
                emerged_columns += surface.water_level <= surface.surface_height ? 1 : 0;
                ++sampled_columns;
            }
        }

        const auto emerged_percent = density_percent(emerged_columns, sampled_columns);
        CAPTURE(seed);
        CAPTURE(emerged_percent);
        CHECK(emerged_percent >= 12.0F);
        CHECK(emerged_percent <= 20.0F);
    }
}

TEST_CASE("ocean adventure reserves a deterministic obstacle-free navigation corridor") {
    constexpr std::array<int, 3> seeds {{17, 424242, -9081}};
    constexpr std::array<int, 5> route_z_samples {{
        kOceanNavigationCorridorStartZ,
        -1,
        0,
        511,
        4096,
    }};

    CHECK_FALSE(is_ocean_navigation_corridor_column(
        kOceanNavigationCorridorCenterX,
        kOceanNavigationCorridorStartZ - 1));

    for (const auto seed : seeds) {
        WorldGenerator generator(seed, WorldGenerationProfile::OceanAdventure);
        for (const auto world_z : route_z_samples) {
            for (int world_x = kOceanNavigationCorridorCenterX - kOceanNavigationCorridorHalfWidth;
                 world_x <= kOceanNavigationCorridorCenterX + kOceanNavigationCorridorHalfWidth;
                 ++world_x) {
                CAPTURE(seed);
                CAPTURE(world_x);
                CAPTURE(world_z);
                const auto surface = generator.sample_surface(world_x, world_z);
                CHECK(surface.surface_height <= kOceanNavigationCorridorMaxSeabedY);
                CHECK(surface.water_level == kSeaLevel);

                for (int y = kOceanNavigationCorridorMaxSeabedY + 1; y <= kSeaLevel; ++y) {
                    CHECK(generator.sample_block(world_x, y, world_z) == to_block_id(BlockType::Air));
                    const auto water_state = generator.sample_water_state(world_x, y, world_z);
                    CHECK(water_level_from_state(water_state) == kMaxWaterLevel);
                    CHECK(water_state_is_source(water_state));
                    CHECK(water_state_is_infinite(water_state));
                }
                for (int y = kSeaLevel + 1; y <= kSeaLevel + 24; ++y) {
                    CHECK(generator.sample_block(world_x, y, world_z) == to_block_id(BlockType::Air));
                    CHECK(generator.sample_water_state(world_x, y, world_z) == 0);
                }
            }
        }
    }
}

TEST_CASE("completed ocean chunks remove decorations that overhang the navigation corridor") {
    constexpr std::array<int, 2> seeds {{424242, -7719}};
    constexpr std::array<int, 2> route_chunk_z {{0, 37}};

    for (const auto seed : seeds) {
        WorldGenerator generator(seed, WorldGenerationProfile::OceanAdventure);
        for (const auto chunk_z : route_chunk_z) {
            for (const auto chunk_x : {-1, 0, 1}) {
                Chunk chunk({chunk_x, chunk_z});
                generator.generate_chunk(chunk);

                for (int local_z = 0; local_z < kChunkSizeZ; ++local_z) {
                    for (int local_x = 0; local_x < kChunkSizeX; ++local_x) {
                        const auto world_x = chunk_x * kChunkSizeX + local_x;
                        const auto world_z = chunk_z * kChunkSizeZ + local_z;
                        if (!is_ocean_navigation_corridor_column(world_x, world_z)) {
                            continue;
                        }

                        auto first_obstacle_y = -1;
                        auto first_water_mismatch_y = -1;
                        for (int y = kOceanNavigationCorridorMaxSeabedY + 1; y <= kWorldMaxY; ++y) {
                            if (first_obstacle_y < 0 &&
                                chunk.get_local(local_x, y, local_z) != to_block_id(BlockType::Air)) {
                                first_obstacle_y = y;
                            }
                            const auto expected_water = y <= kSeaLevel
                                                            ? make_water_state(kMaxWaterLevel, true, true)
                                                            : 0;
                            if (first_water_mismatch_y < 0 &&
                                chunk.get_water_state_local(local_x, y, local_z) != expected_water) {
                                first_water_mismatch_y = y;
                            }
                        }
                        CAPTURE(seed);
                        CAPTURE(chunk_x);
                        CAPTURE(chunk_z);
                        CAPTURE(local_x);
                        CAPTURE(local_z);
                        CAPTURE(first_obstacle_y);
                        CAPTURE(first_water_mismatch_y);
                        CHECK(first_obstacle_y == -1);
                        CHECK(first_water_mismatch_y == -1);
                    }
                }
            }
        }
    }
}

TEST_CASE("generated cell restoration preserves natural infinite water without save overrides") {
    World world(88031, 0, WorldGenerationProfile::OceanAdventure);
    constexpr int water_x = kOceanNavigationCorridorCenterX;
    constexpr int water_y = kSeaLevel;
    constexpr int water_z = 0;
    constexpr int marker_y = kSeaLevel + 4;

    world.ensure_chunk_loaded({0, 0});
    const auto* generated_chunk = world.find_chunk({0, 0});
    REQUIRE(generated_chunk != nullptr);
    REQUIRE(water_state_is_infinite(generated_chunk->get_water_state_local(water_x, water_y, water_z)));

    world.set_block(water_x, marker_y, water_z, to_block_id(BlockType::Stone));
    world.set_block(water_x, water_y, water_z, to_block_id(BlockType::Water));
    auto save_plan = world.capture_save_plan();
    REQUIRE(save_plan.chunks.size() == 1U);
    REQUIRE(save_plan.chunks.front().sparse_cells.size() == 2U);

    CHECK(world.restore_generated_cell(water_x, water_y, water_z));
    const auto* restored_chunk = world.find_chunk({0, 0});
    REQUIRE(restored_chunk != nullptr);
    CHECK(restored_chunk->get_local(water_x, water_y, water_z) == to_block_id(BlockType::Air));
    CHECK(restored_chunk->get_water_state_local(water_x, water_y, water_z) ==
          make_water_state(kMaxWaterLevel, true, true));
    save_plan = world.capture_save_plan();
    REQUIRE(save_plan.chunks.size() == 1U);
    CHECK(save_plan.chunks.front().sparse_cells.size() == 1U);

    CHECK(world.restore_generated_cell(water_x, marker_y, water_z));
    CHECK(world.modified_chunk_snapshots().empty());
    CHECK_FALSE(world.restore_generated_cell(water_x, marker_y, water_z));
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

    WorldGenerator ocean_source(1919, WorldGenerationProfile::OceanAdventure);
    const auto ocean_surface = ocean_source.sample_surface(-128, 96);
    WorldGenerator ocean_moved(std::move(ocean_source));
    CHECK(ocean_moved.profile() == WorldGenerationProfile::OceanAdventure);
    CHECK(ocean_moved.sample_surface(-128, 96).surface_height == ocean_surface.surface_height);
}

TEST_CASE("incremental chunk generation stays byte-identical to synchronous generation") {
    WorldGenerator generator(74123, WorldGenerationProfile::OceanAdventure);
    const ChunkCoord coord {-3, 5};
    Chunk synchronous_chunk {coord};
    generator.generate_chunk(synchronous_chunk);

    auto incremental_state = generator.begin_chunk_generation(coord);
    for (std::size_t column = 0; column < static_cast<std::size_t>(kChunkSizeX * kChunkSizeZ); ++column) {
        CAPTURE(column);
        CHECK(generator.is_chunk_generation_complete(incremental_state) ==
              (column == static_cast<std::size_t>(kChunkSizeX * kChunkSizeZ)));
        generator.advance_chunk_generation(incremental_state, 1U);
    }

    REQUIRE(generator.is_chunk_generation_complete(incremental_state));
    CHECK(incremental_state.chunk.blocks() == synchronous_chunk.blocks());
    CHECK(incremental_state.chunk.water_state() == synchronous_chunk.water_state());
}

TEST_CASE("chunk ore rasterization matches deterministic point sampling underground") {
    WorldGenerator generator(49812);
    const ChunkCoord coord {-2, 3};
    Chunk chunk {coord};
    generator.generate_chunk(chunk);

    for (int local_z = 0; local_z < kChunkSizeZ; ++local_z) {
        for (int local_x = 0; local_x < kChunkSizeX; ++local_x) {
            const auto world_x = coord.x * kChunkSizeX + local_x;
            const auto world_z = coord.z * kChunkSizeZ + local_z;
            const auto surface = generator.sample_surface(world_x, world_z);
            for (int y = kWorldMinY; y <= surface.surface_height; ++y) {
                CAPTURE(local_x);
                CAPTURE(y);
                CAPTURE(local_z);
                CHECK(chunk.get_local(local_x, y, local_z) == generator.sample_block(world_x, y, world_z));
            }
        }
    }
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

TEST_CASE("world spatial queries reject non finite inputs before coordinate casts") {
    constexpr auto nan = std::numeric_limits<float>::quiet_NaN();
    constexpr auto infinity = std::numeric_limits<float>::infinity();

    World world(9301, 1);

    CHECK_FALSE(world.raycast({nan, 1.0F, 0.0F}, {1.0F, 0.0F, 0.0F}, 8.0F).hit);
    CHECK_FALSE(world.raycast({0.0F, 1.0F, 0.0F}, {infinity, 0.0F, 0.0F}, 8.0F).hit);
    CHECK_FALSE(world.raycast({0.0F, 1.0F, 0.0F}, {1.0F, 0.0F, 0.0F}, nan).hit);

    const auto streaming_stats = world.update_streaming({nan, 70.0F, infinity});
    CHECK_FALSE(streaming_stats.chunk_changed);
    CHECK_FALSE(world.are_chunks_ready({nan, 70.0F, 0.0F}, 1));
    CHECK_FALSE(world.are_chunks_ready({0.5F, 70.0F, 0.5F}, -1));
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
        CHECK(is_block_breakable(block_id));
        CHECK(block_break_duration_seconds(block_id) > 0.0F);
        CHECK(properties.opaque);
        CHECK(properties.collidable);
        CHECK(properties.surface_support);
        CHECK_FALSE(properties.replaceable);
        CHECK(properties.mesh_type == BlockMeshType::FullCube);
        CHECK(block_visual_material(block_id) == BlockVisualMaterial::Rock);
    }
}

TEST_CASE("invalid block ids behave like empty non placeable data") {
    const auto invalid_block = static_cast<BlockId>(255U);
    const auto properties = block_properties(invalid_block);

    CHECK_FALSE(is_known_block_id(invalid_block));
    CHECK(block_item_id(invalid_block) == to_block_id(BlockType::Air));
    CHECK_FALSE(is_placeable_item(invalid_block));
    CHECK_FALSE(has_block_mesh(invalid_block));
    CHECK_FALSE(is_block_breakable(invalid_block));
    CHECK_FALSE(properties.opaque);
    CHECK_FALSE(properties.collidable);
    CHECK_FALSE(properties.surface_support);
    CHECK(properties.replaceable);
    CHECK(properties.mesh_type == BlockMeshType::FullCube);
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
    CHECK(block_break_duration_seconds(to_block_id(BlockType::Pickaxe)) == doctest::Approx(0.0F));
    CHECK(block_break_duration_seconds(to_block_id(BlockType::Axe)) == doctest::Approx(0.0F));
    CHECK(block_break_duration_seconds(to_block_id(BlockType::Shovel)) == doctest::Approx(0.0F));
    CHECK(is_block_breakable(to_block_id(BlockType::Stone)));
    CHECK_FALSE(is_block_breakable(to_block_id(BlockType::Air)));
    CHECK_FALSE(is_block_breakable(to_block_id(BlockType::Pickaxe)));
}

TEST_CASE("crafted tools accelerate only their matching block families") {
    CHECK(tool_break_speed_multiplier(to_block_id(BlockType::Pickaxe), to_block_id(BlockType::Stone)) == doctest::Approx(1.5F));
    for (const auto ore_type : kResourceOreTypes) {
        CAPTURE(static_cast<int>(ore_type));
        CHECK(tool_break_speed_multiplier(to_block_id(BlockType::Pickaxe), to_block_id(ore_type)) == doctest::Approx(1.5F));
    }
    CHECK(tool_break_speed_multiplier(to_block_id(BlockType::Pickaxe), to_block_id(BlockType::Dirt)) == doctest::Approx(1.0F));

    CHECK(tool_break_speed_multiplier(to_block_id(BlockType::Axe), to_block_id(BlockType::Wood)) == doctest::Approx(2.0F));
    CHECK(tool_break_speed_multiplier(to_block_id(BlockType::Axe), to_block_id(BlockType::PineWood)) == doctest::Approx(2.0F));
    CHECK(tool_break_speed_multiplier(to_block_id(BlockType::Axe), to_block_id(BlockType::Stone)) == doctest::Approx(1.0F));

    CHECK(tool_break_speed_multiplier(to_block_id(BlockType::Shovel), to_block_id(BlockType::Dirt)) == doctest::Approx(3.0F));
    CHECK(tool_break_speed_multiplier(to_block_id(BlockType::Shovel), to_block_id(BlockType::Grass)) == doctest::Approx(3.0F));
    CHECK(tool_break_speed_multiplier(to_block_id(BlockType::Shovel), to_block_id(BlockType::Wood)) == doctest::Approx(1.0F));
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

TEST_CASE("old saved natural sea sources normalize to the current infinite water flag") {
    WorldGenerator generator(18300);
    const ChunkCoord origin {0, 0};
    Chunk generated_chunk(origin);
    generator.generate_chunk(generated_chunk);
    const auto blocks = generated_chunk.blocks();
    auto legacy_water = generated_chunk.water_state();
    auto found_natural_water = false;

    for (auto& water_state : legacy_water) {
        if (water_state_is_infinite(water_state)) {
            water_state = make_water_state(kMaxWaterLevel, true);
            found_natural_water = true;
        }
    }
    REQUIRE(found_natural_water);

    World restored_world(18300, 0);
    restored_world.replace_chunk_snapshots({{origin, blocks, legacy_water}});

    CHECK(restored_world.modified_chunk_snapshots().empty());
}

TEST_CASE("finite reservoirs drain through opened channels without creating infinite water") {
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
    const auto initial_volume = water_volume_in_box(world, min_x, max_x, water_min_y, water_max_y, min_z, max_z);
    CHECK_FALSE(world.has_water(cavity_far_x, water_max_y, cavity_mid_z));

    for (int y = water_min_y; y <= water_max_y; ++y) {
        for (int z = min_z + 1; z < max_z; ++z) {
            world.set_block(separator_x, y, z, to_block_id(BlockType::Air));
        }
    }

    test::flush_pending_work(world);

    const auto final_volume = water_volume_in_box(world, min_x, max_x, water_min_y, water_max_y, min_z, max_z);
    CHECK(final_volume == initial_volume);
    CHECK(world.water_level(separator_x + 1, water_min_y, cavity_mid_z) > 0);
    CHECK(world.water_level(min_x + 1, water_max_y, cavity_mid_z) < kMaxWaterLevel);
}

TEST_CASE("single isolated finite source spreads as conserved water volume") {
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

    CHECK(water_volume_in_box(world, 0, kChunkSizeX * 2 - 1, water_y, water_y, 0, kChunkSizeZ - 1) == kMaxWaterLevel);
    CHECK(world.water_level(2, water_y, 2) > 0);
    CHECK(world.water_level(2, water_y, 2) < kMaxWaterLevel);
    CHECK_FALSE(world.has_water(14, water_y, 2));
}

TEST_CASE("mesh rebuilds do not fast forward active water simulation") {
    World world(18308, 1);
    test::make_chunk_empty(world, {0, 0});

    constexpr int floor_y = 72;
    constexpr int water_y = floor_y + 1;
    const auto stone = to_block_id(BlockType::Stone);

    for (int x = 0; x <= 4; ++x) {
        world.set_block(x, floor_y, 1, stone);
    }

    test::flush_pending_work(world);

    world.set_block(1, water_y, 1, to_block_id(BlockType::Water));
    REQUIRE(world.pending_fluid_count() > 0);
    CHECK_FALSE(world.has_water(2, water_y, 1));

    world.rebuild_dirty_meshes();

    CHECK_FALSE(world.has_water(2, water_y, 1));
    CHECK(world.water_level(1, water_y, 1) == kMaxWaterLevel);
    CHECK(world.pending_fluid_count() > 0);
}

TEST_CASE("finite water drains downward progressively and conserves volume") {
    World world(18309, 1);
    test::make_chunk_empty(world, {0, 0});

    constexpr int source_y = 40;
    constexpr int x = 2;
    constexpr int z = 2;

    test::flush_pending_work(world);

    world.set_block(x, source_y, z, to_block_id(BlockType::Water));

    const auto first_stats = world.process_pending_work(fluid_only_budget(1U));
    CHECK(first_stats.processed_fluid_cells == 1U);
    CHECK(first_stats.fluid_cells_changed == 2U);
    CHECK(world.water_level(x, source_y, z) == 6U);
    CHECK(world.water_level(x, source_y - 1, z) == 2U);
    CHECK(world.water_level(x, source_y - 2, z) == 0U);

    auto budget = fluid_only_budget(32U);
    for (int iteration = 0; iteration < 64; ++iteration) {
        const auto stats = world.process_pending_work(budget);
        CHECK(stats.processed_fluid_cells <= budget.fluid_cell_budget);
    }

    CHECK(water_volume_in_box(world, 0, 4, kWorldMinY, source_y, 0, 4) == kMaxWaterLevel);
    CHECK(water_volume_in_box(world, x, x, kWorldMinY, source_y - 1, z, z) > 0);
    CHECK(infinite_water_cells_in_box(world, 0, 4, kWorldMinY, source_y, 0, 4) == 0);
}

TEST_CASE("flowing water replaces fragile blocks but does not enter solid barriers") {
    World world(18310, 1);
    test::make_chunk_empty(world, {0, 0});

    constexpr int floor_y = 70;
    constexpr int water_y = floor_y + 1;
    constexpr int channel_z = 2;
    const auto stone = to_block_id(BlockType::Stone);

    for (int x = 0; x <= 5; ++x) {
        world.set_block(x, floor_y, channel_z, stone);
        world.set_block(x, water_y, channel_z - 1, stone);
        world.set_block(x, water_y, channel_z + 1, stone);
    }
    world.set_block(2, water_y, channel_z, to_block_id(BlockType::TallGrass));
    world.set_block(3, water_y, channel_z, to_block_id(BlockType::Torch));
    world.set_block(4, water_y, channel_z, stone);

    test::flush_pending_work(world);
    place_infinite_test_water(world, 1, water_y, channel_z);

    auto budget = fluid_only_budget(64U);
    for (int iteration = 0; iteration < 64 && !world.has_water(3, water_y, channel_z); ++iteration) {
        (void)world.process_pending_work(budget);
    }

    CHECK(world.get_block(2, water_y, channel_z) == to_block_id(BlockType::Air));
    CHECK(world.has_water(2, water_y, channel_z));
    CHECK(world.get_block(3, water_y, channel_z) == to_block_id(BlockType::Air));
    CHECK(world.has_water(3, water_y, channel_z));
    CHECK(world.get_block(4, water_y, channel_z) == stone);
    CHECK_FALSE(world.has_water(4, water_y, channel_z));
    CHECK(infinite_water_cells_in_box(world, 2, 3, water_y, water_y, channel_z, channel_z) == 0);
}

TEST_CASE("zero fluid time budget defers active water without advancing the front") {
    World world(18311, 1);
    test::make_chunk_empty(world, {0, 0});

    constexpr int floor_y = 76;
    constexpr int water_y = floor_y + 1;
    constexpr int channel_z = 4;
    const auto stone = to_block_id(BlockType::Stone);

    for (int x = 0; x <= 4; ++x) {
        world.set_block(x, floor_y, channel_z, stone);
        world.set_block(x, water_y, channel_z - 1, stone);
        world.set_block(x, water_y, channel_z + 1, stone);
    }

    test::flush_pending_work(world);
    world.set_block(1, water_y, channel_z, to_block_id(BlockType::Water));

    const auto pending_before = world.pending_fluid_count();
    REQUIRE(pending_before > 0);

    const auto stats = world.process_pending_work(fluid_only_budget(64U, 0.0));

    CHECK(stats.processed_fluid_cells == 0U);
    CHECK(world.pending_fluid_count() == pending_before);
    CHECK_FALSE(world.has_water(2, water_y, channel_z));
    CHECK(world.water_level(1, water_y, channel_z) == kMaxWaterLevel);
}

TEST_CASE("loaded chunk revalidation resumes water across a newly loaded boundary") {
    World world(18312, 2);
    const ChunkCoord origin {0, 0};
    const ChunkCoord east {1, 0};
    test::make_chunk_empty(world, origin);

    constexpr int floor_y = 72;
    constexpr int water_y = floor_y + 1;
    constexpr int channel_z = 3;
    const auto stone = to_block_id(BlockType::Stone);

    for (int x = 13; x <= 15; ++x) {
        world.set_block(x, floor_y, channel_z, stone);
        world.set_block(x, water_y, channel_z - 1, stone);
        world.set_block(x, water_y, channel_z + 1, stone);
    }

    test::flush_pending_work(world);
    place_infinite_test_water(world, 14, water_y, channel_z);

    auto budget = fluid_only_budget(128U);
    for (int iteration = 0; iteration < 64; ++iteration) {
        (void)world.process_pending_work(budget);
    }

    CHECK(world.find_chunk(east) == nullptr);
    CHECK_FALSE(world.has_water(16, water_y, channel_z));

    test::make_chunk_empty(world, east);
    for (int x = 16; x <= 22; ++x) {
        world.set_block(x, floor_y, channel_z, stone);
        world.set_block(x, water_y, channel_z - 1, stone);
        world.set_block(x, water_y, channel_z + 1, stone);
    }

    for (int iteration = 0; iteration < 64 && !world.has_water(16, water_y, channel_z); ++iteration) {
        (void)world.process_pending_work(budget);
    }

    CHECK(world.has_water(16, water_y, channel_z));
    CHECK(world.water_level(16, water_y, channel_z) > 0);
    CHECK(infinite_water_cells_in_box(world, 16, 22, water_y, water_y, channel_z, channel_z) == 0);
}

TEST_CASE("large active flood respects fluid budgets and keeps pending work bounded") {
    World world(18313, 4);
    for (int chunk_x = 0; chunk_x <= 3; ++chunk_x) {
        test::make_chunk_empty(world, {chunk_x, 0});
    }

    constexpr int floor_y = 66;
    constexpr int water_y = floor_y + 1;
    constexpr int channel_z = 6;
    constexpr int far_x = 52;
    const auto stone = to_block_id(BlockType::Stone);

    for (int x = 0; x < kChunkSizeX * 4; ++x) {
        world.set_block(x, floor_y, channel_z, stone);
        world.set_block(x, water_y, channel_z - 1, stone);
        world.set_block(x, water_y, channel_z + 1, stone);
    }

    test::flush_pending_work(world);
    place_infinite_test_water(world, 0, water_y, channel_z);

    auto budget = fluid_only_budget(7U);
    auto max_pending = std::size_t {0U};
    auto progressed_under_budget = false;

    const auto first_stats = world.process_pending_work(budget);
    CHECK(first_stats.processed_fluid_cells <= budget.fluid_cell_budget);
    CHECK(std::isfinite(first_stats.fluid_ms));
    CHECK_FALSE(world.has_water(far_x, water_y, channel_z));

    for (int frame = 0; frame < 80; ++frame) {
        const auto stats = world.process_pending_work(budget);
        CAPTURE(frame);
        CHECK(stats.processed_fluid_cells <= budget.fluid_cell_budget);
        CHECK(std::isfinite(stats.fluid_ms));
        max_pending = std::max(max_pending, world.pending_fluid_count());
        progressed_under_budget = progressed_under_budget || world.has_water(8, water_y, channel_z);
    }

    CHECK(progressed_under_budget);
    CHECK(max_pending <= 4096U);
    CHECK(infinite_water_cells_in_box(world, 1, far_x, water_y, water_y, channel_z, channel_z) == 0);
}

TEST_CASE("infinite sea pressure advances through long channels by flow budget instead of stopping at a short gradient") {
    World world(18303, 3);
    for (int chunk_x = 0; chunk_x <= 3; ++chunk_x) {
        test::make_chunk_empty(world, {chunk_x, 0});
    }

    constexpr int floor_y = 20;
    constexpr int water_y = floor_y + 1;
    constexpr int min_x = 0;
    constexpr int max_x = 55;
    constexpr int channel_z = 1;
    constexpr int far_x = 40;
    const auto stone = to_block_id(BlockType::Stone);

    for (int x = min_x; x <= max_x; ++x) {
        world.set_block(x, floor_y, channel_z, stone);
        world.set_block(x, water_y, channel_z - 1, stone);
        world.set_block(x, water_y, channel_z + 1, stone);
        world.set_block(x, water_y + 1, channel_z, stone);
    }

    test::flush_pending_work(world);
    place_infinite_test_water(world, min_x, water_y, channel_z);

    WorldWorkBudget slow_budget {};
    slow_budget.chunk_generation_budget = 0U;
    slow_budget.fluid_cell_budget = 1U;
    slow_budget.mesh_rebuild_budget = 0U;
    slow_budget.light_node_budget = 0U;
    slow_budget.max_generation_ms = std::numeric_limits<double>::infinity();
    slow_budget.max_fluid_ms = std::numeric_limits<double>::infinity();
    slow_budget.max_lighting_ms = std::numeric_limits<double>::infinity();
    slow_budget.max_meshing_ms = std::numeric_limits<double>::infinity();

    (void)world.process_pending_work(slow_budget);
    CHECK_FALSE(world.has_water(far_x, water_y, channel_z));

    slow_budget.fluid_cell_budget = 128U;
    for (int iteration = 0; iteration < 256 && !world.has_water(far_x, water_y, channel_z); ++iteration) {
        (void)world.process_pending_work(slow_budget);
    }

    CHECK(world.has_water(far_x, water_y, channel_z));
    CHECK(world.water_level(far_x, water_y, channel_z) > 0);
}

TEST_CASE("infinite source surface keeps advancing across same level floodplains") {
    World world(18307, 3);
    for (int chunk_x = 0; chunk_x <= 3; ++chunk_x) {
        test::make_chunk_empty(world, {chunk_x, 0});
    }

    constexpr int floor_y = kSeaLevel;
    constexpr int water_y = kSeaLevel + 1;
    constexpr int min_x = 0;
    constexpr int max_x = 55;
    constexpr int channel_z = 5;
    constexpr int far_x = 44;
    const auto stone = to_block_id(BlockType::Stone);

    for (int x = min_x; x <= max_x; ++x) {
        world.set_block(x, floor_y, channel_z, stone);
        world.set_block(x, water_y, channel_z - 1, stone);
        world.set_block(x, water_y, channel_z + 1, stone);
    }

    test::flush_pending_work(world);
    place_infinite_test_water(world, min_x, water_y, channel_z);

    WorldWorkBudget budget {};
    budget.chunk_generation_budget = 0U;
    budget.fluid_cell_budget = 1U;
    budget.mesh_rebuild_budget = 0U;
    budget.light_node_budget = 0U;
    budget.max_generation_ms = std::numeric_limits<double>::infinity();
    budget.max_fluid_ms = std::numeric_limits<double>::infinity();
    budget.max_lighting_ms = std::numeric_limits<double>::infinity();
    budget.max_meshing_ms = std::numeric_limits<double>::infinity();

    (void)world.process_pending_work(budget);
    CHECK_FALSE(world.has_water(far_x, water_y, channel_z));

    budget.fluid_cell_budget = 128U;
    for (int iteration = 0; iteration < 256 && !world.has_water(far_x, water_y, channel_z); ++iteration) {
        (void)world.process_pending_work(budget);
    }

    CHECK(world.has_water(far_x, water_y, channel_z));
    CHECK(infinite_water_cells_in_box(world, min_x + 1, far_x, water_y, water_y, channel_z, channel_z) == 0);
}

TEST_CASE("infinite sea pressure raises a lower basin progressively") {
    World world(18304, 1);
    test::make_chunk_empty(world, {0, 0});

    constexpr int floor_y = 20;
    constexpr int water_y = floor_y + 1;
    constexpr int basin_min_x = 3;
    constexpr int basin_max_x = 7;
    constexpr int basin_min_z = 1;
    constexpr int basin_max_z = 5;
    constexpr int inlet_z = 3;
    const auto stone = to_block_id(BlockType::Stone);

    for (int z = basin_min_z; z <= basin_max_z; ++z) {
        for (int x = basin_min_x; x <= basin_max_x; ++x) {
            world.set_block(x, floor_y, z, stone);
        }
    }
    for (int y = water_y; y <= water_y + 3; ++y) {
        for (int x = basin_min_x; x <= basin_max_x; ++x) {
            world.set_block(x, y, basin_min_z, stone);
            world.set_block(x, y, basin_max_z, stone);
        }
        for (int z = basin_min_z; z <= basin_max_z; ++z) {
            if (!(y == water_y && z == inlet_z)) {
                world.set_block(basin_min_x, y, z, stone);
            }
            world.set_block(basin_max_x, y, z, stone);
        }
    }
    world.set_block(basin_min_x - 1, floor_y, inlet_z, stone);
    world.set_block(basin_min_x - 1, water_y, inlet_z - 1, stone);
    world.set_block(basin_min_x - 1, water_y, inlet_z + 1, stone);
    world.set_block(basin_min_x - 1, water_y + 1, inlet_z, stone);

    test::flush_pending_work(world);
    place_infinite_test_water(world, basin_min_x - 1, water_y, inlet_z);

    WorldWorkBudget budget {};
    budget.chunk_generation_budget = 0U;
    budget.fluid_cell_budget = 2U;
    budget.mesh_rebuild_budget = 0U;
    budget.light_node_budget = 0U;
    budget.max_generation_ms = std::numeric_limits<double>::infinity();
    budget.max_fluid_ms = std::numeric_limits<double>::infinity();
    budget.max_lighting_ms = std::numeric_limits<double>::infinity();
    budget.max_meshing_ms = std::numeric_limits<double>::infinity();

    for (int iteration = 0; iteration < 4; ++iteration) {
        (void)world.process_pending_work(budget);
    }
    CHECK_FALSE(world.has_water(basin_min_x + 1, water_y + 1, inlet_z));

    budget.fluid_cell_budget = 64U;
    for (int iteration = 0; iteration < 128 && !world.has_water(basin_min_x + 1, water_y + 1, inlet_z); ++iteration) {
        (void)world.process_pending_work(budget);
    }

    CHECK(world.has_water(basin_min_x + 1, water_y + 1, inlet_z));
    CHECK(world.water_level(basin_min_x + 1, water_y + 1, inlet_z) > 0);
}

TEST_CASE("finite pressure transfer conserves reservoir volume while raising a connected basin") {
    World world(18305, 1);
    test::make_chunk_empty(world, {0, 0});

    constexpr int floor_y = 20;
    constexpr int water_min_y = floor_y + 1;
    constexpr int water_max_y = water_min_y + 4;
    constexpr int min_x = 0;
    constexpr int max_x = 9;
    constexpr int channel_z = 2;
    const auto stone = to_block_id(BlockType::Stone);

    for (int x = min_x; x <= max_x; ++x) {
        for (int z = channel_z - 1; z <= channel_z + 1; ++z) {
            world.set_block(x, floor_y, z, stone);
        }
    }
    for (int y = water_min_y; y <= water_max_y; ++y) {
        for (int x = min_x; x <= max_x; ++x) {
            world.set_block(x, y, channel_z - 1, stone);
            world.set_block(x, y, channel_z + 1, stone);
        }
        world.set_block(min_x, y, channel_z, stone);
        world.set_block(max_x, y, channel_z, stone);
    }

    test::flush_pending_work(world);

    for (int y = water_min_y; y <= water_max_y; ++y) {
        for (int x = min_x + 1; x <= min_x + 3; ++x) {
            world.set_block(x, y, channel_z, to_block_id(BlockType::Water));
        }
    }

    const auto initial_volume = water_volume_in_box(world, min_x, max_x, water_min_y, water_max_y, channel_z, channel_z);

    WorldWorkBudget budget {};
    budget.chunk_generation_budget = 0U;
    budget.fluid_cell_budget = 128U;
    budget.mesh_rebuild_budget = 0U;
    budget.light_node_budget = 0U;
    budget.max_generation_ms = std::numeric_limits<double>::infinity();
    budget.max_fluid_ms = std::numeric_limits<double>::infinity();
    budget.max_lighting_ms = std::numeric_limits<double>::infinity();
    budget.max_meshing_ms = std::numeric_limits<double>::infinity();

    auto raised_side_water = false;
    for (int iteration = 0; iteration < 256; ++iteration) {
        (void)world.process_pending_work(budget);
        raised_side_water = raised_side_water || world.has_water(min_x + 4, water_min_y + 1, channel_z);
    }

    const auto final_volume = water_volume_in_box(world, min_x, max_x, water_min_y, water_max_y, channel_z, channel_z);
    CHECK(raised_side_water);
    CHECK(final_volume == initial_volume);
    CHECK(infinite_water_cells_in_box(world, min_x, max_x, water_min_y, water_max_y, channel_z, channel_z) == 0);
}

TEST_CASE("sea floodwater does not become detached infinite sources and can drain after isolation") {
    World world(18306, 1);
    test::make_chunk_empty(world, {0, 0});

    constexpr int floor_y = 20;
    constexpr int water_y = floor_y + 1;
    constexpr int min_x = 0;
    constexpr int max_x = 8;
    constexpr int channel_z = 2;
    const auto stone = to_block_id(BlockType::Stone);

    for (int x = min_x; x <= max_x; ++x) {
        for (int z = channel_z - 1; z <= channel_z + 1; ++z) {
            world.set_block(x, floor_y, z, stone);
        }
    }
    for (int y = water_y; y <= water_y + 2; ++y) {
        for (int x = min_x; x <= max_x; ++x) {
            world.set_block(x, y, channel_z - 1, stone);
            world.set_block(x, y, channel_z + 1, stone);
        }
        world.set_block(min_x, y, channel_z, stone);
        world.set_block(max_x, y, channel_z, stone);
    }

    test::flush_pending_work(world);
    place_infinite_test_water(world, min_x + 1, water_y, channel_z);

    WorldWorkBudget budget {};
    budget.chunk_generation_budget = 0U;
    budget.fluid_cell_budget = 128U;
    budget.mesh_rebuild_budget = 0U;
    budget.light_node_budget = 0U;
    budget.max_generation_ms = std::numeric_limits<double>::infinity();
    budget.max_fluid_ms = std::numeric_limits<double>::infinity();
    budget.max_lighting_ms = std::numeric_limits<double>::infinity();
    budget.max_meshing_ms = std::numeric_limits<double>::infinity();

    for (int iteration = 0; iteration < 256 && world.water_level(max_x - 2, water_y, channel_z) < kMaxWaterLevel; ++iteration) {
        (void)world.process_pending_work(budget);
    }

    REQUIRE(world.water_level(max_x - 2, water_y, channel_z) == kMaxWaterLevel);
    CHECK(infinite_water_cells_in_box(world, min_x + 2, max_x - 1, water_y, water_y, channel_z, channel_z) == 0);

    for (int y = water_y; y <= water_y + 2; ++y) {
        world.set_block(min_x + 2, y, channel_z, stone);
    }
    world.set_block(max_x - 2, floor_y, channel_z, to_block_id(BlockType::Air));

    const auto isolated_volume = water_volume_in_box(world, min_x + 3, max_x - 1, water_y, water_y, channel_z, channel_z);
    for (int iteration = 0; iteration < 256; ++iteration) {
        (void)world.process_pending_work(budget);
    }

    const auto drained_volume = water_volume_in_box(world, min_x + 3, max_x - 1, water_y, water_y, channel_z, channel_z);
    CHECK(drained_volume < isolated_volume);
    CHECK(world.water_level(max_x - 2, water_y, channel_z) < kMaxWaterLevel);
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
    CHECK(mesh.water_vertices.size() == 37);
    CHECK(mesh.water_indices.size() == 78);
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

    REQUIRE(top_vertices.size() == 18);

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
    const auto half_texel_uv = 0.5F / static_cast<float>(kBlockAtlasSize);

    CHECK(shared_near[0].u == doctest::Approx(left_near[0].u + expected_block_delta - half_texel_uv));
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

    REQUIRE(top_vertices.size() == 18);

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
    const auto half_texel_uv = 0.5F / static_cast<float>(kBlockAtlasSize);

    CHECK(left_near[0].u == doctest::Approx(tile_u0 + expected_block_delta * 7.0F));
    CHECK(shared_near[0].u == doctest::Approx(tile_u0 + half_texel_uv));
    CHECK(shared_near[1].u == doctest::Approx(tile_u0 + uv_step - half_texel_uv));
    CHECK(right_near[0].u == doctest::Approx(tile_u0 + expected_block_delta));

    CHECK(shared_mid[0].u == doctest::Approx(tile_u0 + half_texel_uv));
    CHECK(shared_mid[1].u == doctest::Approx(tile_u0 + uv_step - half_texel_uv));
    CHECK(shared_far[0].u == doctest::Approx(tile_u0 + half_texel_uv));
    CHECK(shared_far[1].u == doctest::Approx(tile_u0 + uv_step - half_texel_uv));

    CHECK(shared_near[1].u - left_near[0].u == doctest::Approx(expected_block_delta - half_texel_uv));
    CHECK(right_near[0].u - shared_near[0].u == doctest::Approx(expected_block_delta - half_texel_uv));
}

TEST_CASE("chunk mesher keeps atlas UVs inside half texel safe bounds") {
    World world(286, 1);
    test::make_chunk_empty(world, {0, 0});
    world.set_block(1, 5, 1, to_block_id(BlockType::Stone));
    world.set_block(3, 5, 1, to_block_id(BlockType::Glass));
    world.set_block(5, 5, 1, to_block_id(BlockType::Water));

    ChunkMesher mesher {};
    const auto mesh = mesher.build_mesh(world, {0, 0});
    const auto uv_step = 1.0F / static_cast<float>(kBlockAtlasTilesPerAxis);
    const auto half_texel_uv = 0.5F / static_cast<float>(kBlockAtlasSize);
    const auto coordinate_inside_tile = [&](float value) {
        const auto local = std::fmod(value, uv_step);
        const auto normalized_local = local < 0.0F ? local + uv_step : local;
        return normalized_local + 1.0e-6F >= half_texel_uv &&
               normalized_local <= uv_step - half_texel_uv + 1.0e-6F;
    };

    REQUIRE_FALSE(mesh.vertices.empty());
    REQUIRE_FALSE(mesh.water_vertices.empty());
    for (const auto& vertex : mesh.vertices) {
        CHECK(coordinate_inside_tile(vertex.u));
        CHECK(coordinate_inside_tile(vertex.v));
    }
    for (const auto& vertex : mesh.water_vertices) {
        CHECK(coordinate_inside_tile(vertex.u));
        CHECK(coordinate_inside_tile(vertex.v));
    }
}

TEST_CASE("chunk mesher tags only exposed water surface vertices for wave animation") {
    World world(186, 1);
    test::make_chunk_empty(world, {0, 0});
    world.set_block(
        3,
        kSeaLevel,
        5,
        to_block_id(BlockType::Water));

    ChunkMesher mesher {};
    const auto mesh = mesher.build_mesh(world, {0, 0});

    const auto animated_vertex_count = std::count_if(mesh.water_vertices.begin(), mesh.water_vertices.end(), [](const ChunkVertex& vertex) {
        return vertex.wave_weight > 0.5F;
    });

    CHECK(animated_vertex_count == 21);
    CHECK(std::all_of(mesh.water_vertices.begin(), mesh.water_vertices.end(), [](const ChunkVertex& vertex) {
        return vertex.wave_weight == 0.0F || vertex.wave_weight == 1.0F;
    }));
}

TEST_CASE("water surfaces above sea level stay outside ocean swell") {
    World world(187, 1);
    test::make_chunk_empty(world, {0, 0});
    world.set_block(
        3,
        kSeaLevel,
        5,
        to_block_id(BlockType::Water));
    world.set_block(
        3,
        kSeaLevel + 1,
        5,
        to_block_id(BlockType::Water));

    ChunkMesher mesher {};
    const auto mesh = mesher.build_mesh(world, {0, 0});

    const auto animated_vertex_count = std::count_if(mesh.water_vertices.begin(), mesh.water_vertices.end(), [](const ChunkVertex& vertex) {
        return vertex.wave_weight > 0.5F;
    });
    CHECK(animated_vertex_count == 0);
    CHECK(std::all_of(
        mesh.water_vertices.begin(),
        mesh.water_vertices.end(),
        [](const ChunkVertex& vertex) {
            return vertex.wave_weight == 0.0F;
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

TEST_CASE("update_streaming can expand a preload radius without moving its center") {
    World world(57, 3, WorldGenerationProfile::OceanAdventure);
    const glm::vec3 focus {0.5F, 70.0F, 0.5F};

    const auto preload = world.update_streaming(focus, 1);
    CHECK(preload.chunk_changed);
    CHECK(preload.generation_enqueued == 9U);
    CHECK(world.pending_generation_count() == 9U);

    const auto duplicate = world.update_streaming(focus, 1);
    CHECK_FALSE(duplicate.chunk_changed);
    CHECK(duplicate.generation_enqueued == 0U);

    const auto expanded = world.update_streaming(focus, 3);
    CHECK_FALSE(expanded.chunk_changed);
    CHECK(expanded.generation_enqueued == 40U);
    CHECK(world.pending_generation_count() == 49U);
}

TEST_CASE("sparse save plans restore one cell without generating or loading a chunk") {
    constexpr int seed = 62017;
    constexpr int x = 3;
    constexpr int y = 91;
    constexpr int z = 4;
    World source(seed, 0, WorldGenerationProfile::OceanAdventure);
    const auto generated = source.peek_block_or_generated(x, y, z);
    const auto replacement = generated == to_block_id(BlockType::Stone)
                                 ? to_block_id(BlockType::Air)
                                 : to_block_id(BlockType::Stone);
    source.set_block(x, y, z, replacement);
    auto plan = source.capture_save_plan();
    REQUIRE(plan.chunks.size() == 1U);
    REQUIRE(plan.chunks.front().sparse_cells.size() == 1U);

    World restored(seed, 0, WorldGenerationProfile::OceanAdventure);
    restored.begin_restore_save_plan(std::move(plan));
    REQUIRE(restored.has_pending_save_restore());
    const auto stats = restored.process_save_restore(1U, std::numeric_limits<double>::infinity());

    CHECK(stats.processed_cells == 1U);
    CHECK(stats.completed_chunks == 1U);
    CHECK(stats.pending_cells == 0U);
    CHECK(stats.progress == doctest::Approx(1.0F));
    CHECK_FALSE(restored.has_pending_save_restore());
    CHECK(restored.chunk_records().empty());
    CHECK(restored.pending_generation_count() == 0U);
    CHECK(restored.pending_lighting_count() == 0U);
    CHECK(restored.pending_mesh_count() == 0U);

    restored.ensure_chunk_loaded({0, 0});
    CHECK(restored.get_block(x, y, z) == replacement);
}

TEST_CASE("sparse save plan validation is deferred and bounded by the restore cell budget") {
    constexpr int seed = 62021;
    WorldSavePlan plan {};
    plan.seed = seed;
    WorldSavePlanChunk chunk {};
    chunk.coord = {0, 0};
    chunk.sparse_cells.reserve(2048U);
    for (std::uint16_t index = 0U; index < 2047U; ++index) {
        chunk.sparse_cells.push_back({index, to_block_id(BlockType::Air), WaterState {0}});
    }
    // Je place volontairement l'erreur a la fin pour prouver que begin ne
    // rescane pas synchroniquement tout le payload.
    chunk.sparse_cells.push_back({0U, to_block_id(BlockType::Stone), WaterState {0}});
    plan.chunks.push_back(std::move(chunk));

    World restored(seed, 0);
    CHECK_NOTHROW(restored.begin_restore_save_plan(std::move(plan)));
    REQUIRE(restored.has_pending_save_restore());

    const auto first = restored.process_save_restore(1U, std::numeric_limits<double>::infinity());
    CHECK(first.processed_cells == 1U);
    CHECK(first.completed_chunks == 0U);
    CHECK(restored.has_pending_save_restore());

    const auto middle = restored.process_save_restore(2046U, std::numeric_limits<double>::infinity());
    CHECK(middle.processed_cells == 2046U);
    CHECK_THROWS_AS(
        static_cast<void>(restored.process_save_restore(
            1U,
            std::numeric_limits<double>::infinity())),
        std::invalid_argument);
}

TEST_CASE("dense save plan restoration respects cell slices and completes incrementally") {
    constexpr int seed = 62018;
    WorldSavePlan plan {};
    plan.seed = seed;
    WorldSavePlanChunk chunk {};
    chunk.coord = {0, 0};
    chunk.dense_blocks.assign(kChunkVolume, to_block_id(BlockType::Air));
    chunk.dense_water_state.assign(kChunkVolume, WaterState {0});
    chunk.dense_blocks[37U] = to_block_id(BlockType::DiamondOre);
    plan.chunks.push_back(std::move(chunk));

    World restored(seed, 0);
    restored.begin_restore_save_plan(std::move(plan));
    const auto first = restored.process_save_restore(
        static_cast<std::size_t>(kChunkHeight),
        std::numeric_limits<double>::infinity());
    CHECK(first.processed_cells == static_cast<std::size_t>(kChunkHeight));
    CHECK(first.completed_chunks == 0U);
    CHECK(first.progress > 0.0F);
    CHECK(first.progress < 1.0F);
    CHECK(restored.has_pending_save_restore());
    CHECK(restored.chunk_records().empty());

    auto iterations = std::size_t {1};
    while (restored.has_pending_save_restore() && iterations < 1024U) {
        (void)restored.process_save_restore(
            static_cast<std::size_t>(kChunkHeight),
            std::numeric_limits<double>::infinity());
        ++iterations;
    }
    CHECK(iterations > 1U);
    CHECK_FALSE(restored.has_pending_save_restore());
    CHECK(restored.save_restore_progress() == doctest::Approx(1.0F));
}

TEST_CASE("restoring a generated cell removes an unloaded override without scheduling world work") {
    constexpr int seed = 62019;
    constexpr int x = 5;
    constexpr int y = 88;
    constexpr int z = 7;
    World source(seed, 0, WorldGenerationProfile::OceanAdventure);
    source.set_block(x, y, z, to_block_id(BlockType::GoldOre));
    auto plan = source.capture_save_plan();

    World restored(seed, 0, WorldGenerationProfile::OceanAdventure);
    restored.begin_restore_save_plan(std::move(plan));
    while (restored.has_pending_save_restore()) {
        (void)restored.process_save_restore(8U, std::numeric_limits<double>::infinity());
    }
    REQUIRE(restored.chunk_records().empty());
    REQUIRE_FALSE(restored.capture_save_plan().chunks.empty());

    CHECK(restored.restore_generated_cell(x, y, z));
    CHECK(restored.capture_save_plan().chunks.empty());
    CHECK(restored.chunk_records().empty());
    CHECK(restored.pending_generation_count() == 0U);
    CHECK(restored.pending_fluid_count() == 0U);
    CHECK(restored.pending_lighting_count() == 0U);
    CHECK(restored.pending_mesh_count() == 0U);
}

TEST_CASE("legacy sea ship blueprint keeps its immutable v7 identity") {
    CHECK(legacy_ship_voxel_count() == 2814U);
    CHECK(legacy_ship_blueprint_checksum() != 0U);
    CHECK(legacy_ship_blueprint_checksum() == 0x278956FF051EAC1EULL);
}

TEST_CASE("legacy sea ship migration is sliced and never loads chunks") {
    constexpr int seed = 62020;
    SeaAdventureSaveState legacy_state {};
    legacy_state.active = true;
    legacy_state.ship_position = {0.5F, static_cast<float>(kSeaLevel + 1), 0.5F};
    legacy_state.stamped_ship_x = 0;
    legacy_state.stamped_ship_z = 0;
    legacy_state.has_stamped_ship = true;

    SeaAdventureSystem source_sea;
    source_sea.load_state(legacy_state, seed);
    const auto render_state = source_sea.ship_render_state();
    REQUIRE(render_state.blueprint != nullptr);
    REQUIRE_FALSE(render_state.parts.empty());
    REQUIRE(legacy_ship_voxel_count() == 2814U);

    // Je cible le premier voxel canonique v7 pour verifier que la premiere
    // tranche migre bien l'ancien monde, independamment du nouveau blueprint.
    constexpr BlockCoord legacy_local {-3, 0, -31};
    constexpr auto legacy_block = to_block_id(BlockType::Wood);
    const auto legacy_x = legacy_state.stamped_ship_x + legacy_local.x;
    const auto legacy_y = static_cast<int>(kSeaLevel + 1) + legacy_local.y;
    const auto legacy_z = legacy_state.stamped_ship_z + legacy_local.z;

    World source_world(seed, 0, WorldGenerationProfile::OceanAdventure);
    source_world.set_block(legacy_x, legacy_y, legacy_z, legacy_block);
    auto plan = source_world.capture_save_plan();
    REQUIRE_FALSE(plan.chunks.empty());

    World restored_world(seed, 0, WorldGenerationProfile::OceanAdventure);
    restored_world.begin_restore_save_plan(std::move(plan));
    while (restored_world.has_pending_save_restore()) {
        (void)restored_world.process_save_restore(8U, std::numeric_limits<double>::infinity());
    }

    SeaAdventureSystem restored_sea;
    restored_sea.load_state(legacy_state, seed);
    restored_sea.begin_legacy_ship_migration(restored_world);
    REQUIRE(restored_sea.has_pending_legacy_ship_migration());
    const auto first = restored_sea.migrate_legacy_ship_step(
        restored_world,
        64U,
        std::numeric_limits<double>::infinity());
    CHECK(first.processed_cells == 64U);
    CHECK(first.pending_cells > 0U);
    CHECK(first.progress > 0.0F);
    CHECK(first.progress < 1.0F);
    CHECK(first.restored_cells == 1U);
    CHECK(restored_sea.save_state().has_stamped_ship);
    CHECK(restored_world.chunk_records().empty());

    while (restored_sea.has_pending_legacy_ship_migration()) {
        (void)restored_sea.migrate_legacy_ship_step(
            restored_world,
            64U,
            std::numeric_limits<double>::infinity());
    }
    CHECK_FALSE(restored_sea.save_state().has_stamped_ship);
    CHECK(restored_sea.legacy_ship_migration_progress() == doctest::Approx(1.0F));
    CHECK(restored_world.capture_save_plan().chunks.empty());
    CHECK(restored_world.chunk_records().empty());
}

TEST_CASE("process_pending_work respects chunk generation budget and eventually readies nearby chunks") {
    World world(58, 1);
    world.update_streaming({0.5F, 0.0F, 0.5F});

    WorldWorkBudget first_budget {};
    first_budget.chunk_generation_budget = 1U;
    first_budget.fluid_cell_budget = 16U;
    first_budget.mesh_rebuild_budget = 65536U;
    first_budget.light_node_budget = 65536U;
    first_budget.max_generation_ms = std::numeric_limits<double>::infinity();
    first_budget.max_lighting_ms = std::numeric_limits<double>::infinity();
    first_budget.max_meshing_ms = std::numeric_limits<double>::infinity();
    const auto first_stats = world.process_pending_work(first_budget);
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

TEST_CASE("world memory stats account for loaded chunks meshes and persistent overrides") {
    World world(601, 0);
    const auto empty_stats = world.memory_stats();
    CHECK(empty_stats.loaded_chunks == 0U);
    CHECK(empty_stats.world_cpu_bytes >= sizeof(World));

    world.ensure_chunk_loaded({0, 0});
    const auto loaded_stats = world.memory_stats();
    CHECK(loaded_stats.loaded_chunks == 1U);
    CHECK(loaded_stats.chunk_cpu_bytes >= sizeof(Chunk));
    CHECK(loaded_stats.world_cpu_bytes >= empty_stats.world_cpu_bytes + loaded_stats.chunk_cpu_bytes);

    world.rebuild_dirty_meshes();
    const auto meshed_stats = world.memory_stats();
    CHECK(meshed_stats.mesh_vertex_capacity > 0U);
    CHECK(meshed_stats.mesh_index_capacity > 0U);
    CHECK(meshed_stats.mesh_cpu_bytes > 0U);

    const auto current_block = world.get_block(0, 0, 0);
    const auto replacement = current_block == to_block_id(BlockType::Air)
                                 ? to_block_id(BlockType::Stone)
                                 : to_block_id(BlockType::Air);
    world.set_block(0, 0, 0, replacement);
    const auto modified_stats = world.memory_stats();
    CHECK(modified_stats.override_chunks == 1U);
    CHECK(modified_stats.override_bytes > 0U);
    CHECK(modified_stats.override_bytes < sizeof(WorldChunkSnapshot));
    CHECK(modified_stats.world_cpu_bytes > meshed_stats.world_cpu_bytes);

    const auto save_plan = world.capture_save_plan();
    REQUIRE(save_plan.chunks.size() == 1U);
    CHECK(save_plan.chunks.front().dense_blocks.empty());
    CHECK(save_plan.chunks.front().dense_water_state.empty());
    REQUIRE(save_plan.chunks.front().sparse_cells.size() == 1U);
    CHECK(save_plan.chunks.front().sparse_cells.front().block == replacement);
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

TEST_CASE("mesher hides internal faces between adjacent glass blocks") {
    World world(771, 1);
    const ChunkCoord coord {0, 0};

    test::make_chunk_empty(world, coord);
    world.set_block(0, 10, 0, to_block_id(BlockType::Glass));
    world.rebuild_dirty_meshes();

    const auto* single_block_mesh = world.mesh_for(coord);
    REQUIRE(single_block_mesh != nullptr);
    CHECK(single_block_mesh->face_count == 6);

    world.set_block(1, 10, 0, to_block_id(BlockType::Glass));
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
    WorldWorkBudget budget {};
    budget.chunk_generation_budget = 0U;
    budget.fluid_cell_budget = 0U;
    budget.mesh_rebuild_budget = 65536U;
    budget.light_node_budget = 65536U;
    budget.max_lighting_ms = std::numeric_limits<double>::infinity();
    budget.max_meshing_ms = std::numeric_limits<double>::infinity();
    const auto stats = world.process_pending_work(budget);

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
    budget.max_lighting_ms = std::numeric_limits<double>::infinity();
    const auto stats = world.process_pending_work(budget);

    CHECK(stats.prioritized_meshed_chunks == 0);
    CHECK(world.mesh_revision(far_existing) == far_revision_before);
    CHECK(world.pending_mesh_count() >= 1);
}

TEST_CASE("priority seam remeshes respect the global mesh rebuild budget") {
    World world(17, 1);
    test::make_chunk_empty(world, {0, 0});
    test::make_chunk_empty(world, {1, 0});
    world.rebuild_dirty_meshes();

    world.set_block(15, 10, 4, to_block_id(BlockType::Stone));
    world.set_block(16, 10, 4, to_block_id(BlockType::Stone));

    WorldWorkBudget budget {};
    budget.chunk_generation_budget = 0U;
    budget.fluid_cell_budget = 1U;
    budget.mesh_rebuild_budget = 1U;
    budget.light_node_budget = 65536U;
    budget.max_lighting_ms = std::numeric_limits<double>::infinity();
    budget.max_meshing_ms = std::numeric_limits<double>::infinity();
    const auto first_stats = world.process_pending_work(budget);
    CHECK(first_stats.mesh_sections_processed == 1U);
    CHECK(first_stats.meshed_chunks <= 1U);
    CHECK(world.pending_mesh_count() > 0U);

    auto total_meshed_chunks = first_stats.meshed_chunks;
    auto total_prioritized_chunks = first_stats.prioritized_meshed_chunks;
    for (int iteration = 0; iteration < 32 && world.pending_mesh_count() > 0U; ++iteration) {
        const auto stats = world.process_pending_work(budget);
        CHECK(stats.mesh_sections_processed <= budget.mesh_rebuild_budget);
        total_meshed_chunks += stats.meshed_chunks;
        total_prioritized_chunks += stats.prioritized_meshed_chunks;
    }
    CHECK(total_meshed_chunks == 2U);
    CHECK(total_prioritized_chunks == 2U);
    CHECK(world.pending_mesh_count() == 0);
}

TEST_CASE("dirty chunks near the streaming center overtake a distant mesh backlog") {
    World world(1700, 4);
    const ChunkCoord near_coord {1, 0};
    const ChunkCoord far_coord {4, 4};
    test::make_chunk_empty(world, near_coord);
    test::make_chunk_empty(world, far_coord);
    world.rebuild_dirty_meshes();
    (void)world.update_streaming({0.5F, 80.0F, 0.5F}, 4);

    const auto near_revision = world.mesh_revision(near_coord);
    const auto far_revision = world.mesh_revision(far_coord);
    auto* near_chunk = world.find_chunk(near_coord);
    auto* far_chunk = world.find_chunk(far_coord);
    REQUIRE(near_chunk != nullptr);
    REQUIRE(far_chunk != nullptr);
    // Je salis les deux chunks sans les mettre directement en file pour verifier
    // que le rescan periodique promeut bien celui qui entoure le joueur.
    near_chunk->mark_dirty();
    far_chunk->mark_dirty();

    WorldWorkBudget budget {};
    budget.chunk_generation_budget = 0U;
    budget.fluid_cell_budget = 0U;
    budget.light_node_budget = 0U;
    budget.mesh_rebuild_budget = kChunkSectionCount;
    budget.max_meshing_ms = std::numeric_limits<double>::infinity();
    const auto stats = world.process_pending_work(budget);

    CHECK(stats.prioritized_meshed_chunks == 1U);
    CHECK(world.mesh_revision(near_coord) > near_revision);
    CHECK(world.mesh_revision(far_coord) == far_revision);
    CHECK(world.pending_mesh_count() >= 1U);
}

TEST_CASE("full chunk remeshing advances one section per budget unit and publishes atomically") {
    World world(1701, 0);
    const ChunkCoord origin {0, 0};
    test::make_chunk_empty(world, origin);
    world.rebuild_dirty_meshes();

    const auto revision_before = world.mesh_revision(origin);
    REQUIRE(revision_before > 0U);
    auto* chunk = world.find_chunk(origin);
    REQUIRE(chunk != nullptr);
    chunk->mark_dirty();

    WorldWorkBudget budget {};
    budget.chunk_generation_budget = 0U;
    budget.fluid_cell_budget = 0U;
    budget.mesh_rebuild_budget = 1U;
    budget.light_node_budget = 0U;
    budget.max_meshing_ms = std::numeric_limits<double>::infinity();

    for (std::size_t section = 0; section + 1U < kChunkSectionCount; ++section) {
        const auto stats = world.process_pending_work(budget);
        CAPTURE(section);
        CHECK(stats.mesh_sections_processed == 1U);
        CHECK(stats.meshed_chunks == 0U);
        CHECK(world.mesh_revision(origin) == revision_before);
        CHECK(world.pending_mesh_count() == 1U);
    }

    const auto final_stats = world.process_pending_work(budget);
    CHECK(final_stats.mesh_sections_processed == 1U);
    CHECK(final_stats.meshed_chunks == 1U);
    CHECK(world.mesh_revision(origin) == revision_before + 1U);
    CHECK(world.pending_mesh_count() == 0U);
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

TEST_CASE("lighting setup and finalization consume bounded work units") {
    World world(801, 0);
    test::make_chunk_empty(world, {0, 0});
    REQUIRE(world.pending_lighting_count() == 1U);

    WorldWorkBudget budget {};
    budget.chunk_generation_budget = 0U;
    budget.fluid_cell_budget = 0U;
    budget.mesh_rebuild_budget = 0U;
    budget.light_node_budget = 1U;
    budget.max_lighting_ms = std::numeric_limits<double>::infinity();

    const auto setup_stats = world.process_pending_work(budget);
    CHECK(setup_stats.lighting_work_units_processed == 1U);
    CHECK(setup_stats.lighting_jobs_started == 1U);
    CHECK(setup_stats.lighting_jobs_completed == 0U);
    CHECK(setup_stats.lighting_setup_ms >= 0.0);
    CHECK(world.pending_lighting_count() == 1U);

    const auto finalize_stats = world.process_pending_work(budget);
    CHECK(finalize_stats.lighting_work_units_processed == 1U);
    CHECK(finalize_stats.light_nodes_processed == 0U);
    CHECK(finalize_stats.lighting_jobs_completed == 1U);
    CHECK(finalize_stats.lighting_finalize_ms >= 0.0);
    CHECK(world.pending_lighting_count() == 0U);
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
    budget.max_lighting_ms = std::numeric_limits<double>::infinity();
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
    budget.max_lighting_ms = std::numeric_limits<double>::infinity();
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

    CHECK(world.pending_gpu_upload_count() > 0U);
    const auto uploads = world.consume_pending_gpu_uploads(8);
    CHECK(std::find(uploads.begin(), uploads.end(), origin) != uploads.end());
    CHECK(world.pending_gpu_upload_count() == 0U);
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
    int tempest_slots = 0;
    bool saw_overcast = false;
    bool saw_storm = false;

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
        if (state.weather == WeatherKind::Tempest) {
            ++tempest_slots;
            CHECK(state.violent_storm_intensity ==
                  doctest::Approx(1.0F));
        }
    }

    const auto fair_weather_ratio =
        static_cast<float>(fair_weather_slots) /
        static_cast<float>(sampled_slots);
    const auto tempest_ratio =
        static_cast<float>(tempest_slots) /
        static_cast<float>(sampled_slots);

    // Je verrouille une météo majoritairement praticable tout en conservant
    // des tempêtes majeures rares, mais réellement présentes dans le cycle.
    CHECK(fair_weather_ratio > 0.64F);
    CHECK(fair_weather_ratio < 0.72F);
    CHECK(tempest_ratio > 0.02F);
    CHECK(tempest_ratio < 0.05F);
    CHECK(rainy_slots > 0);
    CHECK(saw_overcast);
    CHECK(saw_storm);
    CHECK(tempest_slots > 0);
}

TEST_CASE("violent storm weather is grey deterministic and produces intermittent lightning") {
    constexpr std::uint32_t seed = 1337U;
    constexpr float lightning_time = 2'685.0979F;

    const auto clear =
        EnvironmentClock::compute_state(
            12.0F,
            seed,
            0.0F);
    const auto tempest =
        EnvironmentClock::compute_state(
            12.0F,
            seed,
            lightning_time);
    const auto replay =
        EnvironmentClock::compute_state(
            12.0F,
            seed,
            lightning_time);
    const auto between_strikes =
        EnvironmentClock::compute_state(
            12.0F,
            seed,
            2'760.0F);

    REQUIRE(clear.weather == WeatherKind::Clear);
    REQUIRE(tempest.weather == WeatherKind::Tempest);
    REQUIRE(between_strikes.weather == WeatherKind::Tempest);
    CHECK(tempest.violent_storm_intensity ==
          doctest::Approx(1.0F));
    CHECK(tempest.overcast_intensity ==
          doctest::Approx(1.0F));
    CHECK(tempest.precipitation_intensity ==
          doctest::Approx(1.0F));
    CHECK(tempest.storm_intensity ==
          doctest::Approx(1.0F));

    const auto tempest_zenith_span =
        std::max({
            tempest.sky_zenith_color.r,
            tempest.sky_zenith_color.g,
            tempest.sky_zenith_color.b,
        }) -
        std::min({
            tempest.sky_zenith_color.r,
            tempest.sky_zenith_color.g,
            tempest.sky_zenith_color.b,
        });
    const auto clear_zenith_span =
        std::max({
            clear.sky_zenith_color.r,
            clear.sky_zenith_color.g,
            clear.sky_zenith_color.b,
        }) -
        std::min({
            clear.sky_zenith_color.r,
            clear.sky_zenith_color.g,
            clear.sky_zenith_color.b,
        });
    const auto tempest_zenith_energy =
        tempest.sky_zenith_color.r +
        tempest.sky_zenith_color.g +
        tempest.sky_zenith_color.b;
    const auto clear_zenith_energy =
        clear.sky_zenith_color.r +
        clear.sky_zenith_color.g +
        clear.sky_zenith_color.b;

    // Je contrôle le résultat visuel : la palette devient sombre et grise
    // même pendant l'éclair, au lieu de garder le bleu saturé du beau temps.
    CHECK(tempest_zenith_span < 0.15F);
    CHECK(tempest_zenith_span <
          clear_zenith_span * 0.25F);
    CHECK(tempest_zenith_energy <
          clear_zenith_energy * 0.55F);
    CHECK(tempest.exposure < clear.exposure);

    CHECK(tempest.lightning_intensity > 0.65F);
    CHECK(tempest.lightning_bolt_intensity > 0.65F);
    CHECK(tempest.lightning_bolt_intensity <=
          tempest.lightning_intensity);
    CHECK(tempest.lightning_direction.x ==
          doctest::Approx(-0.510863F).epsilon(0.001));
    CHECK(tempest.lightning_direction.y ==
          doctest::Approx(0.471381F).epsilon(0.001));
    CHECK(tempest.lightning_direction.z ==
          doctest::Approx(0.718901F).epsilon(0.001));
    CHECK(glm::length(tempest.lightning_direction) ==
          doctest::Approx(1.0F).epsilon(0.0001));
    CHECK(tempest.lightning_shape_seed ==
          doctest::Approx(0.817389F).epsilon(0.001));

    // Je rejoue exactement le même instant pour garantir que l'éclair ne
    // dépend ni du framerate ni d'un générateur aléatoire mutable.
    CHECK(replay.lightning_intensity ==
          tempest.lightning_intensity);
    CHECK(replay.lightning_bolt_intensity ==
          tempest.lightning_bolt_intensity);
    CHECK(replay.lightning_direction.x ==
          tempest.lightning_direction.x);
    CHECK(replay.lightning_direction.y ==
          tempest.lightning_direction.y);
    CHECK(replay.lightning_direction.z ==
          tempest.lightning_direction.z);
    CHECK(replay.lightning_shape_seed ==
          tempest.lightning_shape_seed);

    // Je vérifie que le ciel de tempête ne reste pas artificiellement illuminé
    // entre deux impacts.
    CHECK(between_strikes.lightning_intensity == 0.0F);
    CHECK(between_strikes.lightning_bolt_intensity == 0.0F);
}

TEST_CASE("lightning stays continuous when a tempest transition crosses an event threshold") {
    constexpr std::uint32_t seed = 3U;
    constexpr float after_time = 11'077.0333F;
    constexpr float before_time =
        after_time - 1.0F / 60.0F;

    const auto before =
        EnvironmentClock::compute_state(
            12.0F,
            seed,
            before_time);
    const auto after =
        EnvironmentClock::compute_state(
            12.0F,
            seed,
            after_time);

    // Je verrouille le franchissement réel qui faisait auparavant apparaître
    // un pulse déjà puissant entre deux images consécutives.
    REQUIRE(after.storm_intensity >
            before.storm_intensity);
    CHECK(std::abs(
              after.lightning_intensity -
              before.lightning_intensity) <
          0.01F);
    CHECK(std::abs(
              after.lightning_bolt_intensity -
              before.lightning_bolt_intensity) <
          0.01F);
}

TEST_CASE("lightning fades continuously into the first storm frames") {
    constexpr std::uint32_t seed = 550U;
    constexpr float after_time = 18'242.482F;
    constexpr float before_time =
        after_time - 1.0F / 60.0F;

    const auto before =
        EnvironmentClock::compute_state(
            12.0F,
            seed,
            before_time);
    const auto after =
        EnvironmentClock::compute_state(
            12.0F,
            seed,
            after_time);

    REQUIRE(before.storm_intensity < 0.01F);
    REQUIRE(after.storm_intensity > 0.01F);
    REQUIRE(after.lightning_intensity > 0.0F);
    REQUIRE(after.lightning_bolt_intensity > 0.0F);
    CHECK(std::abs(
              after.lightning_intensity -
              before.lightning_intensity) <
          0.01F);
    CHECK(std::abs(
              after.lightning_bolt_intensity -
              before.lightning_bolt_intensity) <
          0.01F);
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
                CHECK(state.violent_storm_intensity >= 0.0F);
                CHECK(state.violent_storm_intensity <= 1.0F);
                CHECK(state.lightning_intensity >= 0.0F);
                CHECK(state.lightning_intensity <= 1.0F);
                CHECK(state.lightning_bolt_intensity >= 0.0F);
                CHECK(state.lightning_bolt_intensity <=
                      state.lightning_intensity);
                CHECK(state.lightning_shape_seed >= 0.0F);
                CHECK(state.lightning_shape_seed <= 1.0F);
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
                CHECK(finite_vec3(state.lightning_direction));
                CHECK(glm::length(state.lightning_direction) ==
                      doctest::Approx(1.0F).epsilon(0.0001));
                CHECK(state.lightning_direction.y > 0.10F);
                CHECK(state.lightning_direction.y < 0.50F);
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

    EnvironmentClock bounded_clock {};
    bounded_clock.set_weather_time_seconds(1.0e30F);
    CHECK(bounded_clock.weather_time_seconds() ==
          doctest::Approx(kMaximumWeatherTimeSeconds));

    const auto bounded_state =
        EnvironmentClock::compute_state(
            8.0F,
            1337U,
            1.0e30F);
    CHECK(bounded_state.weather_time_seconds ==
          doctest::Approx(kMaximumWeatherTimeSeconds));
    CHECK(std::isfinite(bounded_state.lightning_intensity));
    CHECK(std::isfinite(bounded_state.lightning_bolt_intensity));
    CHECK(finite_vec3(bounded_state.lightning_direction));
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

TEST_CASE("environment clock starts a complete deterministic tempest for arbitrary world seeds") {
    constexpr std::array<std::uint32_t, 6> seeds {
        0U,
        1U,
        1337U,
        424242U,
        987654U,
        0xffffffffU,
    };

    for (const auto seed : seeds) {
        EnvironmentClock clock {12.0F, false, seed};
        clock.set_weather_time_seconds(
            seed == 0xffffffffU
                ? kMaximumWeatherTimeSeconds
                : 17.0F);

        CAPTURE(seed);
        REQUIRE(clock.start_weather_event(WeatherKind::Tempest));
        const auto started = clock.current_state();
        CHECK(started.weather == WeatherKind::Tempest);
        CHECK(started.weather_transition_factor == doctest::Approx(1.0F));
        CHECK(started.storm_intensity == doctest::Approx(1.0F));
        CHECK(started.violent_storm_intensity == doctest::Approx(1.0F));
        CHECK(started.precipitation_intensity == doctest::Approx(1.0F));
        const auto ocean =
            OceanSimulation::evaluate(
                started,
                OceanSurfaceProfile::OpenSea);
        CHECK(ocean.sea_state == OceanSeaState::Tempest);
        CHECK(ocean.tempest_factor == doctest::Approx(1.0F));
        CHECK(ocean.total_amplitude == doctest::Approx(3.33F));

        const auto started_weather_time =
            clock.weather_time_seconds();
        clock.update(1.0F);
        const auto continued = clock.current_state();
        CHECK(clock.weather_time_seconds() ==
              doctest::Approx(started_weather_time + 1.0F));
        CHECK(continued.weather == WeatherKind::Tempest);
        CHECK(continued.violent_storm_intensity == doctest::Approx(1.0F));
    }

    EnvironmentClock invalid_weather_clock {
        12.0F,
        false,
        1337U,
    };
    invalid_weather_clock.set_weather_time_seconds(
        125.0F);
    CHECK_FALSE(
        invalid_weather_clock.start_weather_event(
            static_cast<WeatherKind>(
                0xffU)));
    CHECK(invalid_weather_clock.weather_time_seconds() ==
          doctest::Approx(125.0F));

    EnvironmentClock long_session_clock {
        12.0F,
        false,
        0U,
    };
    long_session_clock.set_weather_time_seconds(
        268'451'040.0F);
    REQUIRE(
        long_session_clock.start_weather_event(
            WeatherKind::Tempest));
    const auto long_session_tempest =
        long_session_clock.current_state();
    CHECK(long_session_tempest.weather ==
          WeatherKind::Tempest);
    CHECK(long_session_tempest.weather_transition_factor ==
          doctest::Approx(1.0F));
    CHECK(long_session_tempest.violent_storm_intensity ==
          doctest::Approx(1.0F));
    const auto long_session_ocean =
        OceanSimulation::evaluate(
            long_session_tempest,
            OceanSurfaceProfile::OpenSea);
    CHECK(long_session_ocean.tempest_factor ==
          doctest::Approx(1.0F));
    CHECK(long_session_ocean.total_amplitude ==
          doctest::Approx(3.33F));
}

TEST_CASE("ocean spectrum maps weather severity to bounded physical states") {
    std::array<EnvironmentState, 5> environments {};
    for (auto& environment : environments) {
        environment.wind_strength = 0.0F;
        environment.storm_intensity = 0.0F;
        environment.precipitation_intensity = 0.0F;
        environment.weather_time_seconds = 123.25F;
    }

    environments[1].storm_intensity = 0.50F;
    environments[2].storm_intensity = 0.70F;
    environments[3].storm_intensity = 0.90F;
    environments[4].wind_strength = 1.0F;
    environments[4].storm_intensity = 1.0F;
    environments[4].precipitation_intensity = 1.0F;

    constexpr std::array<OceanSeaState, 5> expected_states {{
        OceanSeaState::Calm,
        OceanSeaState::Moderate,
        OceanSeaState::Rough,
        OceanSeaState::Storm,
        OceanSeaState::Tempest,
    }};

    auto previous_severity = -1.0F;
    auto previous_amplitude = -1.0F;
    for (std::size_t state_index = 0; state_index < environments.size(); ++state_index) {
        const auto ocean = OceanSimulation::evaluate(
            environments[state_index],
            OceanSurfaceProfile::OpenSea);
        CAPTURE(state_index);

        CHECK(ocean.sea_state == expected_states[state_index]);
        CHECK(std::isfinite(ocean.severity));
        CHECK(std::isfinite(ocean.tempest_factor));
        CHECK(std::isfinite(ocean.total_amplitude));
        CHECK(std::isfinite(ocean.maximum_displacement));
        CHECK(std::isfinite(ocean.foam_threshold));
        CHECK(std::isfinite(ocean.detail_strength));
        CHECK(std::isfinite(ocean.detail_phase));
        CHECK(ocean.severity >= 0.0F);
        CHECK(ocean.severity <= 1.0F);
        CHECK(ocean.tempest_factor >= 0.0F);
        CHECK(ocean.tempest_factor <= 1.0F);
        CHECK(ocean.total_amplitude >= 0.22F);
        CHECK(ocean.total_amplitude <= 3.3301F);
        CHECK(ocean.maximum_displacement >= ocean.total_amplitude);
        CHECK(ocean.foam_threshold >= 0.54F);
        CHECK(ocean.foam_threshold <= 0.84F);
        CHECK(ocean.detail_strength >= 0.0080F);
        CHECK(ocean.detail_strength <= 0.0194F);
        CHECK(ocean.detail_phase >= 0.0F);
        CHECK(ocean.detail_phase < 6.283186F);
        CHECK(ocean.severity > previous_severity);
        CHECK(ocean.total_amplitude > previous_amplitude);

        auto amplitude_sum = 0.0F;
        auto displacement_bound = 0.0F;
        for (const auto& wave : ocean.waves) {
            CHECK(std::isfinite(wave.direction.x));
            CHECK(std::isfinite(wave.direction.y));
            CHECK(std::isfinite(wave.amplitude));
            CHECK(std::isfinite(wave.wave_number));
            CHECK(std::isfinite(wave.phase));
            CHECK(std::isfinite(wave.steepness));
            CHECK(glm::length(wave.direction) == doctest::Approx(1.0F).epsilon(0.0001));
            CHECK(wave.amplitude > 0.0F);
            CHECK(wave.wave_number > 0.0F);
            CHECK(wave.phase >= 0.0F);
            CHECK(wave.phase < 6.283186F);
            CHECK(wave.steepness >= 0.12F);
            CHECK(wave.steepness <= 0.86F);

            amplitude_sum += wave.amplitude;
            displacement_bound += wave.amplitude * (1.0F + 0.14F * wave.steepness);
        }

        CHECK(amplitude_sum == doctest::Approx(ocean.total_amplitude).epsilon(0.0001));
        CHECK(displacement_bound == doctest::Approx(ocean.maximum_displacement).epsilon(0.0001));
        CHECK(ocean.tempest_factor ==
              doctest::Approx(
                  state_index + 1U == environments.size()
                      ? 1.0F
                      : 0.0F));
        previous_severity = ocean.severity;
        previous_amplitude = ocean.total_amplitude;
    }

    CHECK(std::string_view {OceanSimulation::state_label(OceanSeaState::Calm)} == "calme");
    CHECK(std::string_view {OceanSimulation::state_label(OceanSeaState::Moderate)} == "moderee");
    CHECK(std::string_view {OceanSimulation::state_label(OceanSeaState::Rough)} == "agitee");
    CHECK(std::string_view {OceanSimulation::state_label(OceanSeaState::Storm)} == "tempete");
    CHECK(std::string_view {OceanSimulation::state_label(OceanSeaState::Tempest)} == "tempete majeure");
    CHECK(std::string_view {OceanSimulation::state_label(static_cast<OceanSeaState>(255U))} == "calme");
}

TEST_CASE("ocean world profiles and explicit tempest factor stay coherent") {
    CHECK(OceanSimulation::surface_profile_for_world(
              WorldGenerationProfile::Continental) ==
          OceanSurfaceProfile::InlandWater);
    CHECK(OceanSimulation::surface_profile_for_world(
              WorldGenerationProfile::OceanAdventure) ==
          OceanSurfaceProfile::OpenSea);

    EnvironmentState explicit_tempest {};
    explicit_tempest.wind_strength = 0.0F;
    explicit_tempest.storm_intensity = 0.0F;
    explicit_tempest.precipitation_intensity = 0.0F;
    explicit_tempest.violent_storm_intensity = 1.0F;

    const auto open_sea =
        OceanSimulation::evaluate(
            explicit_tempest,
            OceanSurfaceProfile::OpenSea);
    const auto inland =
        OceanSimulation::evaluate(
            explicit_tempest,
            OceanSurfaceProfile::InlandWater);

    // Je traite le facteur Tempest explicite comme l'autorité, même si un mod
    // ou un outil de test ne renseigne pas les anciens champs météo.
    CHECK(open_sea.severity == 0.0F);
    CHECK(open_sea.tempest_factor ==
          doctest::Approx(1.0F));
    CHECK(open_sea.sea_state ==
          OceanSeaState::Tempest);
    CHECK(open_sea.total_amplitude ==
          doctest::Approx(3.33F));
    CHECK(inland.tempest_factor == 0.0F);
    CHECK(inland.sea_state ==
          OceanSeaState::Calm);
    CHECK(inland.total_amplitude ==
          doctest::Approx(0.09F));
}

TEST_CASE("ocean sampling bounds wave counts and rejects non finite inputs") {
    EnvironmentState environment {};
    environment.wind_strength = 0.82F;
    environment.storm_intensity = 0.64F;
    environment.precipitation_intensity = 0.35F;
    environment.weather_time_seconds = 934.5F;

    const auto ocean = OceanSimulation::evaluate(
        environment,
        OceanSurfaceProfile::OpenSea);
    const glm::vec2 point {13.25F, -7.75F};
    const auto full_sample = OceanSimulation::sample(ocean, point);
    const auto oversized_sample = OceanSimulation::sample(ocean, point, kOceanMaxWaveCount + 17U);
    const auto buoyancy_sample = OceanSimulation::sample(ocean, point, kOceanBuoyancyWaveCount);
    const auto empty_sample = OceanSimulation::sample(ocean, point, 0U);

    CHECK(full_sample.height == doctest::Approx(oversized_sample.height));
    CHECK(full_sample.gradient.x == doctest::Approx(oversized_sample.gradient.x));
    CHECK(full_sample.gradient.y == doctest::Approx(oversized_sample.gradient.y));
    CHECK(full_sample.crest == doctest::Approx(oversized_sample.crest));
    CHECK(std::abs(full_sample.height) <= ocean.maximum_displacement + 0.0001F);
    CHECK(full_sample.crest >= 0.0F);
    CHECK(full_sample.crest <= 1.0F);
    const auto detail_waves_change_sample =
        std::abs(full_sample.height - buoyancy_sample.height) > 0.00001F ||
        glm::length(full_sample.gradient - buoyancy_sample.gradient) > 0.00001F;
    CHECK(detail_waves_change_sample);
    CHECK(empty_sample.height == 0.0F);
    CHECK(empty_sample.gradient == glm::vec2 {0.0F});
    CHECK(empty_sample.crest == 0.0F);

    const auto nan = std::numeric_limits<float>::quiet_NaN();
    const auto infinity = std::numeric_limits<float>::infinity();
    const auto nan_position_sample = OceanSimulation::sample(ocean, {nan, 0.0F});
    const auto infinite_position_sample = OceanSimulation::sample(ocean, {0.0F, infinity});
    CHECK(nan_position_sample.height == 0.0F);
    CHECK(nan_position_sample.gradient == glm::vec2 {0.0F});
    CHECK(nan_position_sample.crest == 0.0F);
    CHECK(infinite_position_sample.height == 0.0F);
    CHECK(infinite_position_sample.gradient == glm::vec2 {0.0F});
    CHECK(infinite_position_sample.crest == 0.0F);

    auto malformed_ocean = ocean;
    malformed_ocean.waves[0].direction.x = nan;
    malformed_ocean.waves[1].amplitude = infinity;
    malformed_ocean.waves[2].wave_number = -1.0F;
    const auto malformed_sample = OceanSimulation::sample(malformed_ocean, point, 3U);
    CHECK(malformed_sample.height == 0.0F);
    CHECK(malformed_sample.gradient == glm::vec2 {0.0F});
    CHECK(malformed_sample.crest == 0.0F);
}

TEST_CASE("ocean evaluation sanitizes non finite weather values") {
    EnvironmentState environment {};
    environment.wind_strength = std::numeric_limits<float>::quiet_NaN();
    environment.storm_intensity = std::numeric_limits<float>::infinity();
    environment.precipitation_intensity = -std::numeric_limits<float>::infinity();
    environment.violent_storm_intensity =
        std::numeric_limits<float>::quiet_NaN();
    environment.weather_time_seconds = std::numeric_limits<float>::quiet_NaN();

    const auto ocean = OceanSimulation::evaluate(
        environment,
        OceanSurfaceProfile::OpenSea);
    CHECK(ocean.sea_state == OceanSeaState::Calm);
    CHECK(ocean.severity == 0.0F);
    CHECK(ocean.tempest_factor == 0.0F);
    CHECK(ocean.total_amplitude == doctest::Approx(0.22F));
    CHECK(ocean.detail_phase == 0.0F);

    for (const auto& wave : ocean.waves) {
        CHECK(std::isfinite(wave.direction.x));
        CHECK(std::isfinite(wave.direction.y));
        CHECK(std::isfinite(wave.amplitude));
        CHECK(std::isfinite(wave.wave_number));
        CHECK(std::isfinite(wave.phase));
        CHECK(std::isfinite(wave.steepness));
    }

    environment.wind_strength = -12.0F;
    environment.storm_intensity = 7.0F;
    environment.precipitation_intensity = 4.0F;
    environment.violent_storm_intensity = 7.0F;
    environment.weather_time_seconds = std::numeric_limits<float>::infinity();
    const auto clamped = OceanSimulation::evaluate(
        environment,
        OceanSurfaceProfile::OpenSea);
    CHECK(clamped.severity <= 1.0F);
    CHECK(clamped.tempest_factor == doctest::Approx(1.0F));
    CHECK(clamped.total_amplitude == doctest::Approx(3.33F));
    CHECK(clamped.detail_phase == 0.0F);
}

TEST_CASE("ocean keeps a visible swell and reaches multi metre tempests") {
    EnvironmentState calm {};
    calm.wind_strength = 0.0F;
    calm.storm_intensity = 0.0F;
    calm.precipitation_intensity = 0.0F;

    const auto calm_ocean =
        OceanSimulation::evaluate(
            calm,
            OceanSurfaceProfile::OpenSea);

    CHECK(calm_ocean.sea_state == OceanSeaState::Calm);
    CHECK(calm_ocean.total_amplitude ==
          doctest::Approx(0.22F));
    CHECK(calm_ocean.maximum_displacement >
          calm_ocean.total_amplitude);
    CHECK(calm_ocean.foam_threshold ==
          doctest::Approx(0.84F));
    CHECK(calm_ocean.detail_strength ==
          doctest::Approx(0.0080F));

    const auto inland_calm =
        OceanSimulation::evaluate(
            calm,
            OceanSurfaceProfile::InlandWater);
    CHECK(inland_calm.total_amplitude ==
          doctest::Approx(0.09F));
    CHECK(inland_calm.foam_threshold ==
          doctest::Approx(0.93F));
    CHECK(inland_calm.detail_strength ==
          doctest::Approx(0.0028F));
    CHECK(calm_ocean.total_amplitude >
          inland_calm.total_amplitude * 2.0F);

    auto visual_wave_amplitude = 0.0F;
    for (std::size_t index = kOceanBuoyancyWaveCount;
         index < calm_ocean.waves.size();
         ++index) {
        visual_wave_amplitude +=
            calm_ocean.waves[index].amplitude;
    }
    CHECK(visual_wave_amplitude >=
          calm_ocean.total_amplitude * 0.22F);

    auto minimum_height =
        std::numeric_limits<float>::max();
    auto maximum_height =
        std::numeric_limits<float>::lowest();
    auto maximum_crest = 0.0F;

    // Je balaie une zone assez large pour verrouiller une houle calme
    // reellement visible, pas seulement une amplitude theorique non observee.
    for (int z = -48; z <= 48; ++z) {
        for (int x = -48; x <= 48; ++x) {
            const auto sample =
                OceanSimulation::sample(
                    calm_ocean,
                    {
                        static_cast<float>(x) * 0.5F,
                        static_cast<float>(z) * 0.5F,
                    });
            minimum_height =
                std::min(minimum_height, sample.height);
            maximum_height =
                std::max(maximum_height, sample.height);
            maximum_crest =
                std::max(maximum_crest, sample.crest);
        }
    }

    CHECK(maximum_height - minimum_height > 0.30F);
    CHECK(maximum_crest > 0.95F);

    auto moving_calm = calm;
    moving_calm.weather_time_seconds = 0.5F;
    const auto moving_ocean =
        OceanSimulation::evaluate(
            moving_calm,
            OceanSurfaceProfile::OpenSea);
    const glm::vec2 observation_point {7.25F, -11.5F};
    const auto initial_sample =
        OceanSimulation::sample(
            calm_ocean,
            observation_point);
    const auto moving_sample =
        OceanSimulation::sample(
            moving_ocean,
            observation_point);
    CHECK(std::abs(
              moving_sample.height -
              initial_sample.height) >
          0.001F);

    EnvironmentState tempest {};
    tempest.wind_strength = 1.0F;
    tempest.storm_intensity = 1.0F;
    tempest.precipitation_intensity = 1.0F;
    tempest.violent_storm_intensity = 1.0F;

    const auto tempest_ocean =
        OceanSimulation::evaluate(
            tempest,
            OceanSurfaceProfile::OpenSea);

    CHECK(tempest_ocean.sea_state ==
          OceanSeaState::Tempest);
    CHECK(tempest_ocean.tempest_factor ==
          doctest::Approx(1.0F));
    CHECK(tempest_ocean.total_amplitude ==
          doctest::Approx(3.33F));
}

TEST_CASE("open sea tempest spectrum keeps long waves and produces observed multi metre swell") {
    constexpr std::array<float, kOceanMaxWaveCount>
        expected_open_sea_wavelengths {{
            96.0F,
            64.0F,
            36.0F,
            14.0F,
            7.0F,
            3.5F,
        }};
    constexpr std::array<float, kOceanMaxWaveCount>
        expected_tempest_amplitude_shares {{
            0.480F,
            0.270F,
            0.150F,
            0.055F,
            0.030F,
            0.015F,
        }};
    constexpr float two_pi = 6.28318530717958647692F;
    constexpr float lightning_time = 2'685.0979F;

    const auto calm_environment =
        EnvironmentClock::compute_state(
            12.0F,
            1337U,
            0.0F);
    const auto tempest_environment =
        EnvironmentClock::compute_state(
            12.0F,
            1337U,
            lightning_time);
    REQUIRE(tempest_environment.weather ==
            WeatherKind::Tempest);

    const auto calm_open_sea =
        OceanSimulation::evaluate(
            calm_environment,
            OceanSurfaceProfile::OpenSea);
    const auto tempest_open_sea =
        OceanSimulation::evaluate(
            tempest_environment,
            OceanSurfaceProfile::OpenSea);
    const auto tempest_inland =
        OceanSimulation::evaluate(
            tempest_environment,
            OceanSurfaceProfile::InlandWater);

    REQUIRE(tempest_open_sea.sea_state ==
            OceanSeaState::Tempest);
    CHECK(tempest_open_sea.tempest_factor ==
          doctest::Approx(1.0F));
    CHECK(tempest_open_sea.total_amplitude ==
          doctest::Approx(3.33F));
    CHECK(tempest_open_sea.total_amplitude <= 3.3301F);
    CHECK(tempest_inland.tempest_factor == 0.0F);
    CHECK(tempest_inland.sea_state ==
          OceanSeaState::Storm);
    CHECK(tempest_inland.total_amplitude ==
          doctest::Approx(0.34F));
    CHECK(tempest_inland.total_amplitude <= 0.3401F);

    auto detail_wave_amplitude = 0.0F;
    for (std::size_t index = 0;
         index < kOceanMaxWaveCount;
         ++index) {

        CAPTURE(index);
        const auto expected_wave_number =
            two_pi /
            expected_open_sea_wavelengths[index];

        // Je garde les longueurs d'onde fixes quand la météo change afin
        // d'éviter tout glissement brutal des crêtes pendant une transition.
        CHECK(calm_open_sea.waves[index].wave_number ==
              doctest::Approx(expected_wave_number).epsilon(0.0001));
        CHECK(tempest_open_sea.waves[index].wave_number ==
              doctest::Approx(expected_wave_number).epsilon(0.0001));
        CHECK(tempest_open_sea.waves[index].wave_number ==
              doctest::Approx(
                  calm_open_sea.waves[index].wave_number)
                  .epsilon(0.0001));
        CHECK(
            tempest_open_sea.waves[index].amplitude /
                tempest_open_sea.total_amplitude ==
            doctest::Approx(
                expected_tempest_amplitude_shares[index])
                .epsilon(0.0001));

        if (index >= kOceanBuoyancyWaveCount) {
            detail_wave_amplitude +=
                tempest_open_sea.waves[index].amplitude;
        }
    }

    CHECK(detail_wave_amplitude ==
          doctest::Approx(
              tempest_open_sea.total_amplitude * 0.10F)
              .epsilon(0.0001));
    CHECK(detail_wave_amplitude <= 0.34F);

    auto minimum_height =
        std::numeric_limits<float>::max();
    auto maximum_height =
        std::numeric_limits<float>::lowest();
    auto minimum_buoyancy_height =
        std::numeric_limits<float>::max();
    auto maximum_buoyancy_height =
        std::numeric_limits<float>::lowest();

    // Je mesure la surface rendue sur une large zone, car une simple somme
    // d'amplitudes ne prouverait pas que des vagues de plusieurs mètres sont
    // réellement observables par le joueur et par la flottabilité.
    for (int z = -128; z <= 128; z += 2) {
        for (int x = -128; x <= 128; x += 2) {
            const glm::vec2 point {
                static_cast<float>(x),
                static_cast<float>(z),
            };
            const auto sample =
                OceanSimulation::sample(
                    tempest_open_sea,
                    point);
            const auto buoyancy_sample =
                OceanSimulation::sample(
                    tempest_open_sea,
                    point,
                    kOceanBuoyancyWaveCount);

            minimum_height =
                std::min(
                    minimum_height,
                    sample.height);
            maximum_height =
                std::max(
                    maximum_height,
                    sample.height);
            minimum_buoyancy_height =
                std::min(
                    minimum_buoyancy_height,
                    buoyancy_sample.height);
            maximum_buoyancy_height =
                std::max(
                    maximum_buoyancy_height,
                    buoyancy_sample.height);
        }
    }

    CHECK(maximum_height - minimum_height > 5.0F);
    CHECK(
        maximum_buoyancy_height -
            minimum_buoyancy_height >
        4.5F);
}

TEST_CASE("ocean analytical gradient matches a centered finite difference") {
    EnvironmentState environment {};
    environment.wind_strength = 0.86F;
    environment.storm_intensity = 0.72F;
    environment.precipitation_intensity = 0.41F;
    environment.weather_time_seconds = 1'234.5F;
    const auto ocean = OceanSimulation::evaluate(
        environment,
        OceanSurfaceProfile::OpenSea);

    constexpr float step = 0.005F;
    constexpr std::array<glm::vec2, 5> points {{
        {0.0F, 0.0F},
        {1.25F, -2.75F},
        {-18.5F, 7.125F},
        {83.0F, 41.0F},
        {-127.75F, -96.25F},
    }};

    for (const auto& point : points) {
        const auto sample = OceanSimulation::sample(ocean, point);
        const auto height_x_positive = OceanSimulation::sample(ocean, point + glm::vec2 {step, 0.0F}).height;
        const auto height_x_negative = OceanSimulation::sample(ocean, point - glm::vec2 {step, 0.0F}).height;
        const auto height_z_positive = OceanSimulation::sample(ocean, point + glm::vec2 {0.0F, step}).height;
        const auto height_z_negative = OceanSimulation::sample(ocean, point - glm::vec2 {0.0F, step}).height;
        const auto finite_difference_x = (height_x_positive - height_x_negative) / (2.0F * step);
        const auto finite_difference_z = (height_z_positive - height_z_negative) / (2.0F * step);

        CAPTURE(point.x);
        CAPTURE(point.y);
        CHECK(std::abs(sample.gradient.x - finite_difference_x) < 0.004F);
        CHECK(std::abs(sample.gradient.y - finite_difference_z) < 0.004F);
    }
}

TEST_CASE("ocean samples remain continuous across time and wrapped phases") {
    EnvironmentState environment {};
    environment.wind_strength = 0.68F;
    environment.storm_intensity = 0.52F;
    environment.precipitation_intensity = 0.27F;
    environment.weather_time_seconds = 8'921.25F;

    auto next_environment = environment;
    next_environment.weather_time_seconds += 0.001F;
    const auto ocean = OceanSimulation::evaluate(
        environment,
        OceanSurfaceProfile::OpenSea);
    const auto next_ocean = OceanSimulation::evaluate(
        next_environment,
        OceanSurfaceProfile::OpenSea);
    const glm::vec2 point {37.25F, -19.75F};
    const auto sample = OceanSimulation::sample(ocean, point);
    const auto next_sample = OceanSimulation::sample(next_ocean, point);

    CHECK(next_ocean.severity == doctest::Approx(ocean.severity));
    CHECK(next_ocean.total_amplitude == doctest::Approx(ocean.total_amplitude));
    CHECK(std::abs(next_sample.height - sample.height) < 0.005F);
    CHECK(glm::length(next_sample.gradient - sample.gradient) < 0.005F);

    // Je place volontairement la premiere onde de part et d'autre de son
    // bouclage 2*pi pour verifier la continuite de l'equation echantillonnee.
    EnvironmentState phase_environment = environment;
    phase_environment.weather_time_seconds = 0.0F;
    const auto initial_ocean = OceanSimulation::evaluate(
        phase_environment,
        OceanSurfaceProfile::OpenSea);
    const auto first_angular_frequency = std::sqrt(9.80665F * initial_ocean.waves[0].wave_number);
    const auto first_phase_wrap_time = initial_ocean.waves[0].phase / first_angular_frequency;
    phase_environment.weather_time_seconds = first_phase_wrap_time - 0.0001F;
    const auto before_wrap = OceanSimulation::sample(
        OceanSimulation::evaluate(
            phase_environment,
            OceanSurfaceProfile::OpenSea),
        point);
    phase_environment.weather_time_seconds = first_phase_wrap_time + 0.0001F;
    const auto after_wrap = OceanSimulation::sample(
        OceanSimulation::evaluate(
            phase_environment,
            OceanSurfaceProfile::OpenSea),
        point);
    CHECK(std::abs(after_wrap.height - before_wrap.height) < 0.001F);
    CHECK(glm::length(after_wrap.gradient - before_wrap.gradient) < 0.001F);

    auto nearby_weather = environment;
    auto nearby_ocean = OceanSimulation::evaluate(
        nearby_weather,
        OceanSurfaceProfile::OpenSea);

    // Je cherche le premier changement representable qui modifie reellement
    // le spectre afin que ce controle local ne puisse pas devenir vacu.
    for (std::size_t step = 0U;
         step < 32U &&
         nearby_ocean.total_amplitude == ocean.total_amplitude;
         ++step) {
        nearby_weather.wind_strength =
            std::nextafter(
                nearby_weather.wind_strength,
                1.0F);
        nearby_ocean = OceanSimulation::evaluate(
            nearby_weather,
            OceanSurfaceProfile::OpenSea);
    }

    REQUIRE(nearby_ocean.total_amplitude > ocean.total_amplitude);
    const auto nearby_sample = OceanSimulation::sample(nearby_ocean, point);
    CHECK(std::abs(nearby_sample.height - sample.height) < 0.001F);
    CHECK(glm::length(nearby_sample.gradient - sample.gradient) < 0.001F);

    environment.weather_time_seconds = 1.0e9F;
    const auto long_session_ocean = OceanSimulation::evaluate(
        environment,
        OceanSurfaceProfile::OpenSea);
    const auto long_session_sample = OceanSimulation::sample(long_session_ocean, point);
    CHECK(long_session_ocean.detail_phase >= 0.0F);
    CHECK(long_session_ocean.detail_phase < 6.283186F);
    CHECK(std::isfinite(long_session_sample.height));
    CHECK(std::isfinite(long_session_sample.gradient.x));
    CHECK(std::isfinite(long_session_sample.gradient.y));
    CHECK(std::isfinite(long_session_sample.crest));
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
