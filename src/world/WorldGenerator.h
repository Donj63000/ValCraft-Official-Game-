#pragma once

#include "world/Chunk.h"

class FastNoiseLite;

namespace valcraft {

constexpr int kSeaLevel = 48;

enum class BiomeType : std::uint8_t {
    Meadow = 0,
    Forest = 1,
    Desert = 2,
    RockyPeaks = 3,
    Taiga = 4,
};

class WorldGenerator {
public:
    explicit WorldGenerator(int seed = 1337);
    ~WorldGenerator();
    WorldGenerator(WorldGenerator&& other) noexcept;
    auto operator=(WorldGenerator&& other) noexcept -> WorldGenerator&;

    WorldGenerator(const WorldGenerator&) = delete;
    auto operator=(const WorldGenerator&) -> WorldGenerator& = delete;

    void generate_chunk(Chunk& chunk);
    [[nodiscard]] auto seed() const noexcept -> int;
    [[nodiscard]] auto biome_at(int world_x, int world_z) const noexcept -> BiomeType;

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
    [[nodiscard]] auto classify_biome(float temperature, float moisture, float ridge_noise, float base_noise) const noexcept -> BiomeType;
    [[nodiscard]] auto choose_surface_block(BiomeType biome, int world_x, int world_z, int surface_height) const noexcept -> BlockId;
    [[nodiscard]] auto choose_filler_block(BiomeType biome, int world_x, int world_z) const noexcept -> BlockId;
    [[nodiscard]] auto choose_surface_height(BiomeType biome, float base_noise, float detail_noise, float ridge_noise) const noexcept -> int;
    [[nodiscard]] auto choose_water_level(BiomeType biome, float moisture, int surface_height) const noexcept -> int;
    [[nodiscard]] auto should_place_tree(BiomeType biome, int surface_y, std::uint32_t column_hash) const noexcept -> bool;
    [[nodiscard]] auto should_place_decoration(BiomeType biome, std::uint32_t column_hash) const noexcept -> bool;
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
    void release() noexcept;

    int seed_ = 1337;
    FastNoiseLite* terrain_noise_ = nullptr;
    FastNoiseLite* detail_noise_ = nullptr;
    FastNoiseLite* temperature_noise_ = nullptr;
    FastNoiseLite* moisture_noise_ = nullptr;
    FastNoiseLite* ridge_noise_ = nullptr;
    FastNoiseLite* cave_noise_ = nullptr;
};

} // namespace valcraft
