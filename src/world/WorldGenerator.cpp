#include "world/WorldGenerator.h"

#include <FastNoiseLite.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>

namespace valcraft {

namespace {

constexpr auto kBaseStoneHeight = 42;

auto hash_column(int x, int z, int seed) noexcept -> std::uint32_t {
    auto value = static_cast<std::uint32_t>(x) * 374761393U;
    value ^= static_cast<std::uint32_t>(z) * 668265263U;
    value ^= static_cast<std::uint32_t>(seed) * 362437U;
    value = (value ^ (value >> 13U)) * 1274126177U;
    return value ^ (value >> 16U);
}

auto make_noise(int seed, FastNoiseLite::NoiseType type, float frequency) -> FastNoiseLite* {
    auto* noise = new FastNoiseLite(seed);
    noise->SetNoiseType(type);
    noise->SetFrequency(frequency);
    return noise;
}

} // namespace

WorldGenerator::WorldGenerator(int seed)
    : seed_(seed),
      terrain_noise_(make_noise(seed, FastNoiseLite::NoiseType_OpenSimplex2, 0.0065F)),
      detail_noise_(make_noise(seed + 101, FastNoiseLite::NoiseType_Perlin, 0.018F)),
      temperature_noise_(make_noise(seed + 202, FastNoiseLite::NoiseType_OpenSimplex2, 0.0021F)),
      moisture_noise_(make_noise(seed + 257, FastNoiseLite::NoiseType_OpenSimplex2S, 0.0025F)),
      ridge_noise_(make_noise(seed + 303, FastNoiseLite::NoiseType_OpenSimplex2S, 0.009F)),
      cave_noise_(make_noise(seed + 404, FastNoiseLite::NoiseType_OpenSimplex2, 0.038F)) {
    ridge_noise_->SetFractalType(FastNoiseLite::FractalType_FBm);
    ridge_noise_->SetFractalOctaves(3);
    moisture_noise_->SetFractalType(FastNoiseLite::FractalType_FBm);
    moisture_noise_->SetFractalOctaves(2);
}

WorldGenerator::~WorldGenerator() {
    release();
}

WorldGenerator::WorldGenerator(WorldGenerator&& other) noexcept
    : seed_(other.seed_),
      terrain_noise_(std::exchange(other.terrain_noise_, nullptr)),
      detail_noise_(std::exchange(other.detail_noise_, nullptr)),
      temperature_noise_(std::exchange(other.temperature_noise_, nullptr)),
      moisture_noise_(std::exchange(other.moisture_noise_, nullptr)),
      ridge_noise_(std::exchange(other.ridge_noise_, nullptr)),
      cave_noise_(std::exchange(other.cave_noise_, nullptr)) {
}

auto WorldGenerator::operator=(WorldGenerator&& other) noexcept -> WorldGenerator& {
    if (this == &other) {
        return *this;
    }

    release();
    seed_ = other.seed_;
    terrain_noise_ = std::exchange(other.terrain_noise_, nullptr);
    detail_noise_ = std::exchange(other.detail_noise_, nullptr);
    temperature_noise_ = std::exchange(other.temperature_noise_, nullptr);
    moisture_noise_ = std::exchange(other.moisture_noise_, nullptr);
    ridge_noise_ = std::exchange(other.ridge_noise_, nullptr);
    cave_noise_ = std::exchange(other.cave_noise_, nullptr);
    return *this;
}

void WorldGenerator::generate_chunk(Chunk& chunk) {
    chunk.fill(to_block_id(BlockType::Air));

    const auto coord = chunk.coord();
    const auto base_world_x = coord.x * kChunkSizeX;
    const auto base_world_z = coord.z * kChunkSizeZ;

    for (int local_z = 0; local_z < kChunkSizeZ; ++local_z) {
        for (int local_x = 0; local_x < kChunkSizeX; ++local_x) {
            const auto world_x = base_world_x + local_x;
            const auto world_z = base_world_z + local_z;
            const auto column = sample_column(world_x, world_z);

            for (int y = 0; y <= column.surface_height; ++y) {
                auto block = to_block_id(BlockType::Stone);
                if (column.biome == BiomeType::RockyPeaks && y >= column.surface_height - 7 && y < column.surface_height - 2) {
                    const auto layer_hash = hash_column(world_x, world_z + y, seed_);
                    switch (layer_hash % 5U) {
                    case 0U:
                        block = to_block_id(BlockType::Gravel);
                        break;
                    case 1U:
                        block = to_block_id(BlockType::Cobblestone);
                        break;
                    case 2U:
                        block = to_block_id(BlockType::MossyStone);
                        break;
                    default:
                        block = to_block_id(BlockType::Stone);
                        break;
                    }
                }
                if (y == column.surface_height) {
                    block = column.surface_block;
                } else if (y >= column.surface_height - 3) {
                    block = column.filler_block;
                }

                const auto cave_noise = cave_noise_->GetNoise(static_cast<float>(world_x), static_cast<float>(y), static_cast<float>(world_z));
                if (y > 6 && y < column.surface_height - 4 && cave_noise > 0.58F) {
                    continue;
                }

                chunk.set_local(local_x, y, local_z, block);
            }

            if (column.water_level > column.surface_height) {
                for (int y = column.surface_height + 1; y <= column.water_level && y < kChunkHeight; ++y) {
                    if (chunk.get_local(local_x, y, local_z) == to_block_id(BlockType::Air)) {
                        chunk.set_local(local_x, y, local_z, to_block_id(BlockType::Water));
                    }
                }
            }

            const auto column_hash = hash_column(world_x, world_z, seed_);
            if (column.water_level > column.surface_height) {
                continue;
            }

            if (should_place_tree(column.biome, column.surface_height, column_hash)) {
                place_tree(chunk, local_x, column.surface_height, local_z, column.biome, column_hash);
            } else if (should_place_decoration(column.biome, column_hash)) {
                place_decoration(chunk, local_x, column.surface_height, local_z, column.biome, column_hash);
            }
        }
    }

    chunk.clear_dirty();
    chunk.mark_dirty();
}

auto WorldGenerator::seed() const noexcept -> int {
    return seed_;
}

auto WorldGenerator::biome_at(int world_x, int world_z) const noexcept -> BiomeType {
    return sample_column(world_x, world_z).biome;
}

auto WorldGenerator::sample_column(int world_x, int world_z) const noexcept -> TerrainColumnSample {
    const auto base = terrain_noise_->GetNoise(static_cast<float>(world_x), static_cast<float>(world_z));
    const auto detail = detail_noise_->GetNoise(static_cast<float>(world_x), static_cast<float>(world_z));
    const auto temperature = temperature_noise_->GetNoise(static_cast<float>(world_x), static_cast<float>(world_z));
    const auto moisture = moisture_noise_->GetNoise(static_cast<float>(world_x), static_cast<float>(world_z));
    const auto ridge = std::abs(ridge_noise_->GetNoise(static_cast<float>(world_x), static_cast<float>(world_z)));
    const auto biome = classify_biome(temperature, moisture, ridge, base);

    TerrainColumnSample sample {};
    sample.biome = biome;
    sample.base_noise = base;
    sample.detail_noise = detail;
    sample.ridge_noise = ridge;
    sample.temperature = temperature;
    sample.moisture = moisture;
    sample.surface_height = choose_surface_height(biome, base, detail, ridge);
    sample.surface_block = choose_surface_block(biome, world_x, world_z, sample.surface_height);
    sample.filler_block = choose_filler_block(biome, world_x, world_z);
    sample.water_level = choose_water_level(biome, moisture, sample.surface_height);
    return sample;
}

auto WorldGenerator::classify_biome(float temperature, float moisture, float ridge_noise, float base_noise) const noexcept -> BiomeType {
    if (ridge_noise > 0.66F && base_noise > 0.02F) {
        return BiomeType::RockyPeaks;
    }
    if (temperature < -0.24F) {
        return BiomeType::Taiga;
    }
    if (temperature > 0.34F && moisture < -0.06F) {
        return BiomeType::Desert;
    }
    if (moisture > 0.20F) {
        return BiomeType::Forest;
    }
    return BiomeType::Meadow;
}

auto WorldGenerator::choose_surface_block(BiomeType biome, int world_x, int world_z, int surface_height) const noexcept -> BlockId {
    const auto column_hash = hash_column(world_x, world_z, seed_);
    switch (biome) {
    case BiomeType::Desert:
        return to_block_id(BlockType::Sand);
    case BiomeType::RockyPeaks:
        if (surface_height > 78 && (column_hash % 3U) != 0U) {
            return to_block_id(BlockType::Snow);
        }
        switch (column_hash % 4U) {
        case 0U:
            return to_block_id(BlockType::Stone);
        case 1U:
            return to_block_id(BlockType::Cobblestone);
        case 2U:
            return to_block_id(BlockType::Gravel);
        default:
            return to_block_id(BlockType::MossyStone);
        }
    case BiomeType::Taiga:
        return surface_height > 60 ? to_block_id(BlockType::Snow) : to_block_id(BlockType::Grass);
    case BiomeType::Forest:
        return surface_height <= kSeaLevel + 1 ? to_block_id(BlockType::Sand) : to_block_id(BlockType::Grass);
    case BiomeType::Meadow:
        return surface_height <= kSeaLevel + 1 ? to_block_id(BlockType::Sand) : to_block_id(BlockType::Grass);
    default:
        return to_block_id(BlockType::Grass);
    }
}

auto WorldGenerator::choose_filler_block(BiomeType biome, int world_x, int world_z) const noexcept -> BlockId {
    const auto column_hash = hash_column(world_x, world_z, seed_ + 61);
    switch (biome) {
    case BiomeType::Desert:
        return to_block_id(BlockType::Sand);
    case BiomeType::RockyPeaks:
        return (column_hash % 3U) == 0U ? to_block_id(BlockType::Gravel) : to_block_id(BlockType::Stone);
    case BiomeType::Taiga:
    case BiomeType::Forest:
    case BiomeType::Meadow:
    default:
        return to_block_id(BlockType::Dirt);
    }
}

auto WorldGenerator::choose_surface_height(BiomeType biome, float base_noise, float detail_noise, float ridge_noise) const noexcept -> int {
    float height = 50.0F + base_noise * 9.0F + detail_noise * 3.5F;
    switch (biome) {
    case BiomeType::Meadow:
        height = 50.0F + base_noise * 8.0F + detail_noise * 4.0F;
        break;
    case BiomeType::Forest:
        height = 53.0F + base_noise * 8.5F + detail_noise * 5.0F;
        break;
    case BiomeType::Desert:
        height = 46.0F + base_noise * 4.0F + detail_noise * 2.0F + ridge_noise * 3.0F;
        break;
    case BiomeType::RockyPeaks:
        height = 61.0F + base_noise * 14.0F + ridge_noise * 18.0F + detail_noise * 3.0F;
        break;
    case BiomeType::Taiga:
        height = 55.0F + base_noise * 9.0F + detail_noise * 4.5F + ridge_noise * 5.0F;
        break;
    }
    const auto rounded = static_cast<int>(std::round(height));
    return std::clamp(rounded, kBaseStoneHeight, kWorldMaxY - 6);
}

auto WorldGenerator::choose_water_level(BiomeType biome, float moisture, int surface_height) const noexcept -> int {
    switch (biome) {
    case BiomeType::RockyPeaks:
        return kWorldMinY - 1;
    case BiomeType::Desert:
        return surface_height < kSeaLevel - 3 && moisture > 0.25F ? surface_height + 1 : kWorldMinY - 1;
    case BiomeType::Taiga:
        return surface_height < kSeaLevel - 1 ? kSeaLevel - 1 : kWorldMinY - 1;
    case BiomeType::Forest:
    case BiomeType::Meadow:
    default:
        return surface_height < kSeaLevel ? kSeaLevel : kWorldMinY - 1;
    }
}

auto WorldGenerator::should_place_tree(BiomeType biome, int surface_y, std::uint32_t column_hash) const noexcept -> bool {
    if (surface_y < 48 || surface_y > kWorldMaxY - 10) {
        return false;
    }
    switch (biome) {
    case BiomeType::Forest:
        return (column_hash % 14U) == 0U;
    case BiomeType::Meadow:
        return (column_hash % 31U) == 0U;
    case BiomeType::Taiga:
        return (column_hash % 18U) == 0U;
    case BiomeType::Desert:
    case BiomeType::RockyPeaks:
    default:
        return false;
    }
}

auto WorldGenerator::should_place_decoration(BiomeType biome, std::uint32_t column_hash) const noexcept -> bool {
    switch (biome) {
    case BiomeType::Meadow:
        return (column_hash % 3U) == 0U;
    case BiomeType::Forest:
        return (column_hash % 5U) == 0U;
    case BiomeType::Desert:
        return (column_hash % 9U) == 0U;
    case BiomeType::Taiga:
        return (column_hash % 11U) == 0U;
    case BiomeType::RockyPeaks:
        return (column_hash % 19U) == 0U;
    default:
        return false;
    }
}

void WorldGenerator::place_tree(Chunk& chunk, int local_x, int surface_y, int local_z, BiomeType biome, std::uint32_t column_hash) const {
    switch (biome) {
    case BiomeType::Taiga:
        place_pine_tree(chunk, local_x, surface_y, local_z, column_hash);
        break;
    case BiomeType::Forest:
    case BiomeType::Meadow:
        place_oak_tree(chunk, local_x, surface_y, local_z, column_hash);
        break;
    case BiomeType::Desert:
    case BiomeType::RockyPeaks:
    default:
        break;
    }
}

void WorldGenerator::place_decoration(Chunk& chunk,
                                      int local_x,
                                      int surface_y,
                                      int local_z,
                                      BiomeType biome,
                                      std::uint32_t column_hash) const {
    if (!chunk.in_bounds_local(local_x, surface_y + 1, local_z)) {
        return;
    }
    if (chunk.get_local(local_x, surface_y + 1, local_z) != to_block_id(BlockType::Air)) {
        return;
    }

    BlockId decoration = to_block_id(BlockType::Air);
    switch (biome) {
    case BiomeType::Meadow:
        if ((column_hash % 23U) == 0U) {
            decoration = to_block_id(BlockType::RedFlower);
        } else if ((column_hash % 29U) == 0U) {
            decoration = to_block_id(BlockType::YellowFlower);
        } else {
            decoration = to_block_id(BlockType::TallGrass);
        }
        break;
    case BiomeType::Forest:
        if ((column_hash % 37U) == 0U) {
            decoration = to_block_id(BlockType::YellowFlower);
        } else {
            decoration = to_block_id(BlockType::TallGrass);
        }
        break;
    case BiomeType::Desert:
        if ((column_hash % 31U) == 0U) {
            place_cactus(chunk, local_x, surface_y, local_z, column_hash);
            return;
        }
        decoration = to_block_id(BlockType::DeadShrub);
        break;
    case BiomeType::Taiga:
        decoration = (column_hash % 4U) == 0U ? to_block_id(BlockType::TallGrass) : to_block_id(BlockType::DeadShrub);
        break;
    case BiomeType::RockyPeaks:
        if ((column_hash % 3U) == 0U) {
            decoration = to_block_id(BlockType::DeadShrub);
        }
        break;
    default:
        break;
    }

    if (decoration != to_block_id(BlockType::Air)) {
        chunk.set_local(local_x, surface_y + 1, local_z, decoration);
    }
}

void WorldGenerator::place_oak_tree(Chunk& chunk, int local_x, int surface_y, int local_z, std::uint32_t column_hash) const {
    if (local_x < 2 || local_x > kChunkSizeX - 3 || local_z < 2 || local_z > kChunkSizeZ - 3) {
        return;
    }

    const auto trunk_base = surface_y + 1;
    const auto trunk_height = 4 + static_cast<int>(column_hash % 2U);
    if (trunk_base + trunk_height + 3 >= kChunkHeight) {
        return;
    }

    for (int y = 0; y < trunk_height; ++y) {
        chunk.set_local(local_x, trunk_base + y, local_z, to_block_id(BlockType::Wood));
    }

    const auto canopy_center_y = trunk_base + trunk_height - 1;
    for (int dz = -2; dz <= 2; ++dz) {
        for (int dx = -2; dx <= 2; ++dx) {
            for (int dy = -1; dy <= 2; ++dy) {
                const auto distance = std::abs(dx) + std::abs(dz) + std::abs(dy);
                if (distance > 4 || (std::abs(dx) == 2 && std::abs(dz) == 2 && dy > 0)) {
                    continue;
                }

                const auto x = local_x + dx;
                const auto y = canopy_center_y + dy;
                const auto z = local_z + dz;
                if (!chunk.in_bounds_local(x, y, z)) {
                    continue;
                }
                if (chunk.get_local(x, y, z) == to_block_id(BlockType::Air)) {
                    chunk.set_local(x, y, z, to_block_id(BlockType::Leaves));
                }
            }
        }
    }
}

void WorldGenerator::place_pine_tree(Chunk& chunk, int local_x, int surface_y, int local_z, std::uint32_t column_hash) const {
    if (local_x < 3 || local_x > kChunkSizeX - 4 || local_z < 3 || local_z > kChunkSizeZ - 4) {
        return;
    }

    const auto trunk_base = surface_y + 1;
    const auto trunk_height = 5 + static_cast<int>(column_hash % 3U);
    if (trunk_base + trunk_height + 3 >= kChunkHeight) {
        return;
    }

    for (int y = 0; y < trunk_height; ++y) {
        chunk.set_local(local_x, trunk_base + y, local_z, to_block_id(BlockType::PineWood));
    }

    for (int level = 0; level < 4; ++level) {
        const auto radius = 2 - level / 2;
        const auto canopy_y = trunk_base + trunk_height - 1 - level;
        for (int dz = -radius; dz <= radius; ++dz) {
            for (int dx = -radius; dx <= radius; ++dx) {
                if (std::abs(dx) + std::abs(dz) > radius + 1) {
                    continue;
                }

                const auto x = local_x + dx;
                const auto z = local_z + dz;
                if (!chunk.in_bounds_local(x, canopy_y, z)) {
                    continue;
                }
                if (chunk.get_local(x, canopy_y, z) == to_block_id(BlockType::Air)) {
                    chunk.set_local(x, canopy_y, z, to_block_id(BlockType::PineLeaves));
                }
            }
        }
    }

    if (chunk.in_bounds_local(local_x, trunk_base + trunk_height, local_z)) {
        chunk.set_local(local_x, trunk_base + trunk_height, local_z, to_block_id(BlockType::PineLeaves));
    }
}

void WorldGenerator::place_cactus(Chunk& chunk, int local_x, int surface_y, int local_z, std::uint32_t column_hash) const {
    if (local_x < 1 || local_x > kChunkSizeX - 2 || local_z < 1 || local_z > kChunkSizeZ - 2) {
        return;
    }
    const auto height = 2 + static_cast<int>(column_hash % 2U);
    if (surface_y + height >= kChunkHeight) {
        return;
    }
    for (int y = 1; y <= height; ++y) {
        if (chunk.get_local(local_x, surface_y + y, local_z) != to_block_id(BlockType::Air)) {
            return;
        }
    }
    for (int y = 1; y <= height; ++y) {
        chunk.set_local(local_x, surface_y + y, local_z, to_block_id(BlockType::Cactus));
    }
}

void WorldGenerator::release() noexcept {
    delete terrain_noise_;
    delete detail_noise_;
    delete temperature_noise_;
    delete moisture_noise_;
    delete ridge_noise_;
    delete cave_noise_;
    terrain_noise_ = nullptr;
    detail_noise_ = nullptr;
    temperature_noise_ = nullptr;
    moisture_noise_ = nullptr;
    ridge_noise_ = nullptr;
    cave_noise_ = nullptr;
}

} // namespace valcraft
