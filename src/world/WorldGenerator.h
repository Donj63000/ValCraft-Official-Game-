#pragma once

#include "world/Chunk.h"

class FastNoiseLite;

namespace valcraft {

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

private:
    [[nodiscard]] auto choose_surface_block(float biome) const noexcept -> BlockId;
    [[nodiscard]] auto choose_surface_height(float biome, float base_noise, float detail_noise, float ridge_noise) const noexcept -> int;
    [[nodiscard]] auto should_place_tree(int world_x, int world_z, int surface_y, float biome) const noexcept -> bool;
    void place_tree(Chunk& chunk, int local_x, int surface_y, int local_z) const;
    void release() noexcept;

    int seed_ = 1337;
    FastNoiseLite* terrain_noise_ = nullptr;
    FastNoiseLite* detail_noise_ = nullptr;
    FastNoiseLite* biome_noise_ = nullptr;
    FastNoiseLite* ridge_noise_ = nullptr;
    FastNoiseLite* cave_noise_ = nullptr;
};

} // namespace valcraft
