#pragma once

#include "world/BackroomsGenerator.h"
#include "world/Chunk.h"
#include "world/OceanAdventureLayout.h"

#include <array>
#include <cstddef>
#include <memory>

class FastNoiseLite;

namespace valcraft {

enum class WorldGenerationProfile : std::uint8_t {
    Continental = 0,
    OceanAdventure = 1,
    Backrooms = 2,
};

enum class WorldGenerationVersion : std::uint32_t {
    Latest = 0,
    LegacyV1 = 1,
    SparseArchipelagoV2 = 2,
    LivingOceanV3 = 3,
    BackroomsV1 = 4,
};

[[nodiscard]] inline constexpr auto resolve_world_generation_version(
    WorldGenerationProfile profile,
    WorldGenerationVersion requested_version = WorldGenerationVersion::Latest) noexcept -> WorldGenerationVersion {
    if (requested_version != WorldGenerationVersion::Latest) {
        return requested_version;
    }
    switch (profile) {
    case WorldGenerationProfile::OceanAdventure:
        return WorldGenerationVersion::LivingOceanV3;
    case WorldGenerationProfile::Backrooms:
        return WorldGenerationVersion::BackroomsV1;
    case WorldGenerationProfile::Continental:
    default:
        return WorldGenerationVersion::LegacyV1;
    }
}

enum class BiomeType : std::uint8_t {
    Meadow = 0,
    Forest = 1,
    Desert = 2,
    RockyPeaks = 3,
    Taiga = 4,
};

struct TerrainSurfaceSample {
    BiomeType biome = BiomeType::Meadow;
    int surface_height = 0;
    int water_level = kWorldMinY - 1;
    BlockId surface_block = to_block_id(BlockType::Grass);
};

class WorldGenerator {
public:
    struct ChunkGenerationState {
        explicit ChunkGenerationState(ChunkCoord coord, bool should_generate_decorations = true)
            : chunk(coord),
              generate_decorations(should_generate_decorations) {}

        Chunk chunk;
        std::array<BlockId, kChunkVolume> resource_ore_blocks {};
        std::size_t next_column = 0;
        bool finalized = false;
        bool generate_decorations = true;
    };

    explicit WorldGenerator(
        int seed = 1337,
        WorldGenerationProfile profile = WorldGenerationProfile::Continental,
        WorldGenerationVersion generation_version = WorldGenerationVersion::Latest,
        int logical_level = 0);
    ~WorldGenerator();
    WorldGenerator(WorldGenerator&& other) noexcept;
    auto operator=(WorldGenerator&& other) noexcept -> WorldGenerator&;

    WorldGenerator(const WorldGenerator&) = delete;
    auto operator=(const WorldGenerator&) -> WorldGenerator& = delete;

    void generate_chunk(Chunk& chunk) const;
    [[nodiscard]] auto begin_chunk_generation(ChunkCoord coord, bool generate_decorations = true) const -> ChunkGenerationState;
    void advance_chunk_generation(ChunkGenerationState& state, std::size_t column_budget) const;
    [[nodiscard]] auto is_chunk_generation_complete(const ChunkGenerationState& state) const noexcept -> bool;
    [[nodiscard]] auto seed() const noexcept -> int;
    [[nodiscard]] auto profile() const noexcept -> WorldGenerationProfile;
    [[nodiscard]] auto generation_version() const noexcept -> WorldGenerationVersion;
    [[nodiscard]] auto backrooms_level() const noexcept -> int;
    [[nodiscard]] auto biome_at(int world_x, int world_z) const noexcept -> BiomeType;
    [[nodiscard]] auto sample_surface(int world_x, int world_z) const noexcept -> TerrainSurfaceSample;
    [[nodiscard]] auto sample_block(int world_x, int y, int world_z) const noexcept -> BlockId;
    [[nodiscard]] auto sample_water_state(int world_x, int y, int world_z) const noexcept -> WaterState;

private:
    struct TerrainColumnSample {
        BiomeType biome = BiomeType::Meadow;
        float base_noise = 0.0F;
        float detail_noise = 0.0F;
        float ridge_noise = 0.0F;
        float temperature = 0.0F;
        float moisture = 0.0F;
        int surface_height = 0;
        int water_level = kWorldMinY - 1;
        BlockId surface_block = to_block_id(BlockType::Grass);
        BlockId filler_block = to_block_id(BlockType::Dirt);
    };

    [[nodiscard]] auto sample_column(int world_x, int world_z) const noexcept -> TerrainColumnSample;
    [[nodiscard]] auto sample_ocean_column(int world_x, int world_z) const noexcept -> TerrainColumnSample;
    [[nodiscard]] auto sample_archipelago_ocean_column(int world_x,
                                                       int world_z,
                                                       bool living_seabed) const noexcept
        -> TerrainColumnSample;
    [[nodiscard]] auto classify_biome(float temperature, float moisture, float ridge_noise, float base_noise) const noexcept -> BiomeType;
    [[nodiscard]] auto choose_surface_block(BiomeType biome, int world_x, int world_z, int surface_height) const noexcept -> BlockId;
    [[nodiscard]] auto choose_filler_block(BiomeType biome, int world_x, int world_z) const noexcept -> BlockId;
    [[nodiscard]] auto choose_surface_height(BiomeType biome, float base_noise, float detail_noise, float ridge_noise) const noexcept -> int;
    [[nodiscard]] auto choose_water_level(int surface_height) const noexcept -> int;
    [[nodiscard]] auto choose_terrain_block(const TerrainColumnSample& column, int world_x, int y, int world_z) const noexcept -> BlockId;
    [[nodiscard]] auto choose_base_terrain_block(const TerrainColumnSample& column,
                                                 int world_x,
                                                 int y,
                                                 int world_z) const noexcept -> BlockId;
    [[nodiscard]] auto choose_resource_ore_block(const TerrainColumnSample& column,
                                                 BlockId base_block,
                                                 int world_x,
                                                 int y,
                                                 int world_z) const noexcept -> BlockId;
    void finalize_ocean_navigation_corridor(Chunk& chunk) const;
    [[nodiscard]] auto should_place_tree(BiomeType biome, int surface_y, std::uint32_t column_hash) const noexcept -> bool;
    [[nodiscard]] auto should_place_decoration(BiomeType biome, std::uint32_t column_hash) const noexcept -> bool;
    void build_resource_ore_map(ChunkGenerationState& state) const;
    void place_tree(Chunk& chunk, int local_x, int surface_y, int local_z, BiomeType biome, std::uint32_t column_hash) const;
    void place_decoration(Chunk& chunk,
                          int local_x,
                          int surface_y,
                          int local_z,
                          BiomeType biome,
                          std::uint32_t column_hash) const;
    void place_oak_tree(Chunk& chunk, int local_x, int surface_y, int local_z, std::uint32_t column_hash) const;
    void place_pine_tree(Chunk& chunk, int local_x, int surface_y, int local_z, std::uint32_t column_hash) const;
    void place_cactus(Chunk& chunk, int local_x, int surface_y, int local_z, std::uint32_t column_hash) const;

    int seed_ = 1337;
    WorldGenerationProfile profile_ = WorldGenerationProfile::Continental;
    WorldGenerationVersion generation_version_ = WorldGenerationVersion::LegacyV1;
    int logical_level_ = 0;
    BackroomsGenerator backrooms_generator_ {};
    std::unique_ptr<FastNoiseLite> terrain_noise_ {};
    std::unique_ptr<FastNoiseLite> detail_noise_ {};
    std::unique_ptr<FastNoiseLite> temperature_noise_ {};
    std::unique_ptr<FastNoiseLite> moisture_noise_ {};
    std::unique_ptr<FastNoiseLite> ridge_noise_ {};
    std::unique_ptr<FastNoiseLite> cave_noise_ {};
};

} // namespace valcraft
