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
        const auto hx = static_cast<std::uint64_t>(static_cast<std::uint32_t>(coord.x));
        const auto hz = static_cast<std::uint64_t>(static_cast<std::uint32_t>(coord.z));
        return static_cast<std::size_t>((hx << 32U) ^ hz ^ 0x9E3779B97F4A7C15ULL);
    }
};

inline constexpr auto to_block_id(BlockType type) noexcept -> BlockId {
    return static_cast<BlockId>(type);
}

inline constexpr auto is_block_solid(BlockId block_id) noexcept -> bool {
    return block_id != to_block_id(BlockType::Air);
}

inline constexpr auto is_world_y_valid(int y) noexcept -> bool {
    return y >= kWorldMinY && y <= kWorldMaxY;
}

} // namespace valcraft
