#pragma once

#include "world/Block.h"

#include <cstdint>
#include <vector>

namespace valcraft {

constexpr int kBlockAtlasSize = 128;
constexpr int kBlockAtlasTileSize = 16;
constexpr float kBlockAtlasTilesPerAxis = 8.0F;
constexpr int kAccentAtlasSize = 64;
constexpr int kAccentAtlasTileSize = 16;
constexpr float kAccentAtlasTilesPerAxis = 4.0F;

enum class BlockVisualFace : std::uint8_t {
    PositiveX = 0,
    NegativeX = 1,
    PositiveY = 2,
    NegativeY = 3,
    PositiveZ = 4,
    NegativeZ = 5,
    Cross = 6,
};

struct BlockAtlasTile {
    int x = 0;
    int y = 0;

    auto operator==(const BlockAtlasTile&) const -> bool = default;
};

struct AccentAtlasTile {
    int x = 0;
    int y = 0;

    auto operator==(const AccentAtlasTile&) const -> bool = default;
};

enum class AccentAtlasSprite : std::uint8_t {
    Sun = 0,
    Moon = 1,
    Star = 2,
    Cloud = 3,
    Ring = 4,
    Spark = 5,
};

enum class BlockVisualMaterial : std::uint8_t {
    Terrain = 0,
    Rock = 1,
    Sand = 2,
    Wood = 3,
    Foliage = 4,
    Flora = 5,
    Water = 6,
    Emissive = 7,
    Snow = 8,
};

[[nodiscard]] constexpr auto accent_atlas_tile(AccentAtlasSprite sprite) noexcept -> AccentAtlasTile {
    switch (sprite) {
    case AccentAtlasSprite::Sun:
        return {0, 0};
    case AccentAtlasSprite::Moon:
        return {1, 0};
    case AccentAtlasSprite::Star:
        return {2, 0};
    case AccentAtlasSprite::Cloud:
        return {3, 0};
    case AccentAtlasSprite::Ring:
        return {0, 1};
    case AccentAtlasSprite::Spark:
    default:
        return {1, 1};
    }
}

[[nodiscard]] constexpr auto block_atlas_tile(BlockId block_id, BlockVisualFace face) noexcept -> BlockAtlasTile {
    switch (static_cast<BlockType>(block_id)) {
    case BlockType::Grass:
        if (face == BlockVisualFace::PositiveY) {
            return {0, 0};
        }
        if (face == BlockVisualFace::NegativeY) {
            return {2, 0};
        }
        return {1, 0};
    case BlockType::Dirt:
        return {2, 0};
    case BlockType::Stone:
        return {3, 0};
    case BlockType::Sand:
        return {4, 0};
    case BlockType::Cobblestone:
        return {5, 0};
    case BlockType::Gravel:
        return {6, 0};
    case BlockType::MossyStone:
        return {7, 0};
    case BlockType::Wood:
        if (face == BlockVisualFace::PositiveY || face == BlockVisualFace::NegativeY) {
            return {1, 1};
        }
        return {0, 1};
    case BlockType::Planks:
        return {2, 1};
    case BlockType::Leaves:
        return {3, 1};
    case BlockType::PineWood:
        if (face == BlockVisualFace::PositiveY || face == BlockVisualFace::NegativeY) {
            return {5, 1};
        }
        return {4, 1};
    case BlockType::PineLeaves:
        return {6, 1};
    case BlockType::Snow:
        if (face == BlockVisualFace::PositiveY) {
            return {0, 2};
        }
        if (face == BlockVisualFace::NegativeY) {
            return {2, 0};
        }
        return {1, 2};
    case BlockType::Cactus:
        if (face == BlockVisualFace::PositiveY || face == BlockVisualFace::NegativeY) {
            return {6, 2};
        }
        return {5, 2};
    case BlockType::Water:
        if (face == BlockVisualFace::PositiveY || face == BlockVisualFace::NegativeY) {
            return {5, 3};
        }
        return {7, 2};
    case BlockType::Torch:
        return {0, 3};
    case BlockType::TallGrass:
        return {1, 3};
    case BlockType::RedFlower:
        return {2, 3};
    case BlockType::YellowFlower:
        return {3, 3};
    case BlockType::DeadShrub:
        return {4, 3};
    case BlockType::Air:
    default:
        return {0, 0};
    }
}

[[nodiscard]] constexpr auto block_hotbar_tile(BlockId block_id) noexcept -> BlockAtlasTile {
    switch (static_cast<BlockType>(block_id)) {
    case BlockType::Grass:
        return {1, 0};
    case BlockType::Wood:
        return {0, 1};
    case BlockType::Snow:
        return {1, 2};
    case BlockType::Water:
        return {7, 2};
    default:
        return block_atlas_tile(block_id, BlockVisualFace::PositiveX);
    }
}

[[nodiscard]] constexpr auto block_visual_material(BlockId block_id) noexcept -> BlockVisualMaterial {
    switch (static_cast<BlockType>(block_id)) {
    case BlockType::Grass:
    case BlockType::Dirt:
        return BlockVisualMaterial::Terrain;
    case BlockType::Stone:
    case BlockType::Cobblestone:
    case BlockType::Gravel:
    case BlockType::MossyStone:
        return BlockVisualMaterial::Rock;
    case BlockType::Sand:
    case BlockType::Cactus:
        return BlockVisualMaterial::Sand;
    case BlockType::Wood:
    case BlockType::Planks:
    case BlockType::PineWood:
        return BlockVisualMaterial::Wood;
    case BlockType::Leaves:
    case BlockType::PineLeaves:
        return BlockVisualMaterial::Foliage;
    case BlockType::TallGrass:
    case BlockType::RedFlower:
    case BlockType::YellowFlower:
    case BlockType::DeadShrub:
        return BlockVisualMaterial::Flora;
    case BlockType::Water:
        return BlockVisualMaterial::Water;
    case BlockType::Torch:
        return BlockVisualMaterial::Emissive;
    case BlockType::Snow:
        return BlockVisualMaterial::Snow;
    case BlockType::Air:
    default:
        return BlockVisualMaterial::Terrain;
    }
}

[[nodiscard]] constexpr auto block_visual_material_value(BlockVisualMaterial material) noexcept -> float {
    return static_cast<float>(static_cast<std::uint8_t>(material));
}

[[nodiscard]] constexpr auto block_visual_material_value(BlockId block_id) noexcept -> float {
    return block_visual_material_value(block_visual_material(block_id));
}

[[nodiscard]] auto build_block_atlas_pixels() -> std::vector<std::uint8_t>;
[[nodiscard]] auto build_accent_atlas_pixels() -> std::vector<std::uint8_t>;

} // namespace valcraft
