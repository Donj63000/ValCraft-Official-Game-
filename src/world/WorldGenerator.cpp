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
      biome_noise_(make_noise(seed + 202, FastNoiseLite::NoiseType_OpenSimplex2, 0.0023F)),
      ridge_noise_(make_noise(seed + 303, FastNoiseLite::NoiseType_OpenSimplex2S, 0.009F)),
      cave_noise_(make_noise(seed + 404, FastNoiseLite::NoiseType_OpenSimplex2, 0.038F)) {
    ridge_noise_->SetFractalType(FastNoiseLite::FractalType_FBm);
    ridge_noise_->SetFractalOctaves(3);
}

WorldGenerator::~WorldGenerator() {
    release();
}

WorldGenerator::WorldGenerator(WorldGenerator&& other) noexcept
    : seed_(other.seed_),
      terrain_noise_(std::exchange(other.terrain_noise_, nullptr)),
      detail_noise_(std::exchange(other.detail_noise_, nullptr)),
      biome_noise_(std::exchange(other.biome_noise_, nullptr)),
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
    biome_noise_ = std::exchange(other.biome_noise_, nullptr);
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

            const auto biome = biome_noise_->GetNoise(static_cast<float>(world_x), static_cast<float>(world_z));
            const auto base = terrain_noise_->GetNoise(static_cast<float>(world_x), static_cast<float>(world_z));
            const auto detail = detail_noise_->GetNoise(static_cast<float>(world_x), static_cast<float>(world_z));
            const auto ridge = std::abs(ridge_noise_->GetNoise(static_cast<float>(world_x), static_cast<float>(world_z)));
            const auto surface_height = choose_surface_height(biome, base, detail, ridge);
            const auto top_block = choose_surface_block(biome);

            for (int y = 0; y <= surface_height; ++y) {
                auto block = to_block_id(BlockType::Stone);
                if (y == surface_height) {
                    block = top_block;
                } else if (y >= surface_height - 3) {
                    block = top_block == to_block_id(BlockType::Sand)
                                ? to_block_id(BlockType::Sand)
                                : to_block_id(BlockType::Dirt);
                }

                const auto cave_noise = cave_noise_->GetNoise(static_cast<float>(world_x), static_cast<float>(y), static_cast<float>(world_z));
                if (y > 6 && y < surface_height - 4 && cave_noise > 0.58F) {
                    continue;
                }

                chunk.set_local(local_x, y, local_z, block);
            }

            if (should_place_tree(world_x, world_z, surface_height, biome)) {
                place_tree(chunk, local_x, surface_height, local_z);
            }
        }
    }

    chunk.clear_dirty();
    chunk.mark_dirty();
}

auto WorldGenerator::seed() const noexcept -> int {
    return seed_;
}

auto WorldGenerator::choose_surface_block(float biome) const noexcept -> BlockId {
    if (biome < -0.28F) {
        return to_block_id(BlockType::Sand);
    }
    if (biome > 0.42F) {
        return to_block_id(BlockType::Stone);
    }
    return to_block_id(BlockType::Grass);
}

auto WorldGenerator::choose_surface_height(float biome, float base_noise, float detail_noise, float ridge_noise) const noexcept -> int {
    float height = 50.0F + base_noise * 9.0F + detail_noise * 3.5F;
    if (biome < -0.28F) {
        height = 44.0F + base_noise * 4.0F + detail_noise * 2.0F;
    } else if (biome > 0.42F) {
        height = 57.0F + base_noise * 14.0F + ridge_noise * 11.0F;
    }

    const auto rounded = static_cast<int>(std::round(height));
    return std::clamp(rounded, kBaseStoneHeight, kWorldMaxY - 6);
}

auto WorldGenerator::should_place_tree(int world_x, int world_z, int surface_y, float biome) const noexcept -> bool {
    if (biome < -0.10F || biome > 0.30F) {
        return false;
    }
    if (surface_y < 48 || surface_y > kWorldMaxY - 10) {
        return false;
    }

    const auto hash = hash_column(world_x, world_z, seed_);
    return (hash % 23U) == 0U;
}

void WorldGenerator::place_tree(Chunk& chunk, int local_x, int surface_y, int local_z) const {
    if (local_x < 2 || local_x > kChunkSizeX - 3 || local_z < 2 || local_z > kChunkSizeZ - 3) {
        return;
    }

    const auto trunk_base = surface_y + 1;
    const auto trunk_height = 4;
    if (trunk_base + trunk_height + 2 >= kChunkHeight) {
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
                if (distance > 4) {
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

void WorldGenerator::release() noexcept {
    delete terrain_noise_;
    delete detail_noise_;
    delete biome_noise_;
    delete ridge_noise_;
    delete cave_noise_;
    terrain_noise_ = nullptr;
    detail_noise_ = nullptr;
    biome_noise_ = nullptr;
    ridge_noise_ = nullptr;
    cave_noise_ = nullptr;
}

} // namespace valcraft
