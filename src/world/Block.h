#pragma once

#include <cstddef>
#include <cstdint>

namespace valcraft {

using BlockId = std::uint8_t;

enum class BlockType : BlockId {
    Air = 0,
    Grass = 1,
    Dirt = 2,
    Stone = 3,
    Sand = 4,
    Wood = 5,
    Leaves = 6,
    Torch = 7,
    Cobblestone = 8,
    Planks = 9,
    Gravel = 10,
    MossyStone = 11,
    Snow = 12,
    PineWood = 13,
    PineLeaves = 14,
    TallGrass = 15,
    RedFlower = 16,
    YellowFlower = 17,
    DeadShrub = 18,
    Cactus = 19,
    Water = 20,
};

enum class BlockMeshType : std::uint8_t {
    FullCube = 0,
    Torch = 1,
    Cross = 2,
    Water = 3,
};

struct BlockProperties {
    bool opaque = true;
    bool collidable = true;
    bool surface_support = true;
    bool replaceable = false;
    BlockMeshType mesh_type = BlockMeshType::FullCube;
    std::uint8_t emissive_level = 0;
};

constexpr int kChunkSizeX = 16;
constexpr int kChunkSizeZ = 16;
constexpr int kChunkHeight = 128;
constexpr int kWorldMinY = 0;
constexpr int kWorldMaxY = kChunkHeight - 1;
constexpr int kDefaultStreamRadius = 6;

struct ChunkCoord {
    int x = 0;
    int z = 0;

    auto operator==(const ChunkCoord&) const -> bool = default;
};

struct BlockCoord {
    int x = 0;
    int y = 0;
    int z = 0;

    auto operator==(const BlockCoord&) const -> bool = default;
};

struct RaycastHit {
    bool hit = false;
    BlockCoord block {};
    BlockCoord adjacent {};
    BlockId block_id = static_cast<BlockId>(BlockType::Air);
};

struct ChunkCoordHash {
    auto operator()(const ChunkCoord& coord) const noexcept -> std::size_t {
        const auto hx = static_cast<std::size_t>(static_cast<std::uint32_t>(coord.x));
        const auto hz = static_cast<std::size_t>(static_cast<std::uint32_t>(coord.z));
        return (hx * static_cast<std::size_t>(73856093U)) ^ (hz * static_cast<std::size_t>(19349663U));
    }
};

inline constexpr auto to_block_id(BlockType type) noexcept -> BlockId {
    return static_cast<BlockId>(type);
}

inline constexpr auto block_properties(BlockId block_id) noexcept -> BlockProperties {
    switch (static_cast<BlockType>(block_id)) {
    case BlockType::Air:
        return {false, false, false, true, BlockMeshType::FullCube, static_cast<std::uint8_t>(0)};
    case BlockType::Torch:
        return {false, false, false, false, BlockMeshType::Torch, static_cast<std::uint8_t>(14)};
    case BlockType::Leaves:
    case BlockType::PineLeaves:
        return {true, true, false, false, BlockMeshType::FullCube, static_cast<std::uint8_t>(0)};
    case BlockType::TallGrass:
    case BlockType::RedFlower:
    case BlockType::YellowFlower:
    case BlockType::DeadShrub:
        return {false, false, false, true, BlockMeshType::Cross, static_cast<std::uint8_t>(0)};
    case BlockType::Water:
        return {false, false, false, true, BlockMeshType::Water, static_cast<std::uint8_t>(0)};
    case BlockType::Grass:
    case BlockType::Dirt:
    case BlockType::Stone:
    case BlockType::Sand:
    case BlockType::Wood:
    case BlockType::Cobblestone:
    case BlockType::Planks:
    case BlockType::Gravel:
    case BlockType::MossyStone:
    case BlockType::Snow:
    case BlockType::PineWood:
    case BlockType::Cactus:
    default:
        return {true, true, true, false, BlockMeshType::FullCube, static_cast<std::uint8_t>(0)};
    }
}

inline constexpr auto is_block_solid(BlockId block_id) noexcept -> bool {
    return block_id != to_block_id(BlockType::Air);
}

inline constexpr auto is_block_opaque(BlockId block_id) noexcept -> bool {
    return block_properties(block_id).opaque;
}

inline constexpr auto is_block_collidable(BlockId block_id) noexcept -> bool {
    return block_properties(block_id).collidable;
}

inline constexpr auto is_block_surface_support(BlockId block_id) noexcept -> bool {
    return block_properties(block_id).surface_support;
}

inline constexpr auto is_block_replaceable(BlockId block_id) noexcept -> bool {
    return block_properties(block_id).replaceable;
}

inline constexpr auto is_block_liquid(BlockId block_id) noexcept -> bool {
    return block_id == to_block_id(BlockType::Water);
}

inline constexpr auto has_block_mesh(BlockId block_id) noexcept -> bool {
    return block_id != to_block_id(BlockType::Air);
}

inline constexpr auto is_block_targetable(BlockId block_id) noexcept -> bool {
    return has_block_mesh(block_id);
}

inline constexpr auto block_mesh_type(BlockId block_id) noexcept -> BlockMeshType {
    return block_properties(block_id).mesh_type;
}

inline constexpr auto block_emissive_level(BlockId block_id) noexcept -> std::uint8_t {
    return block_properties(block_id).emissive_level;
}

inline constexpr auto is_world_y_valid(int y) noexcept -> bool {
    return y >= kWorldMinY && y <= kWorldMaxY;
}

} // namespace valcraft
