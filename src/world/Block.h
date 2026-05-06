#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace valcraft {

using BlockId = std::uint8_t;
using WaterState = std::uint8_t;

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
    TorchWallPositiveX = 21,
    TorchWallNegativeX = 22,
    TorchWallPositiveZ = 23,
    TorchWallNegativeZ = 24,
    Glass = 25,
    Pastron = 26,
    RoundShield = 27,
    Sword = 28,
    Spear = 29,
    Shoes = 30,
    Pants = 31,
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
constexpr int kMaxStreamRadius = 32;
constexpr std::uint8_t kBlockBreakStageCount = 8;

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
    float distance = 0.0F;
};

struct ChunkCoordHash {
    auto operator()(const ChunkCoord& coord) const noexcept -> std::size_t {
        const auto hx = static_cast<std::size_t>(static_cast<std::uint32_t>(coord.x));
        const auto hz = static_cast<std::size_t>(static_cast<std::uint32_t>(coord.z));
        return (hx * static_cast<std::size_t>(73856093U)) ^ (hz * static_cast<std::size_t>(19349663U));
    }
};

struct BlockCoordHash {
    auto operator()(const BlockCoord& coord) const noexcept -> std::size_t {
        const auto hx = static_cast<std::size_t>(static_cast<std::uint32_t>(coord.x));
        const auto hy = static_cast<std::size_t>(static_cast<std::uint32_t>(coord.y));
        const auto hz = static_cast<std::size_t>(static_cast<std::uint32_t>(coord.z));
        return (hx * static_cast<std::size_t>(73856093U)) ^
               (hy * static_cast<std::size_t>(83492791U)) ^
               (hz * static_cast<std::size_t>(19349663U));
    }
};

inline constexpr auto to_block_id(BlockType type) noexcept -> BlockId {
    return static_cast<BlockId>(type);
}

inline constexpr WaterState kWaterSourceBit = static_cast<WaterState>(1U << 7U);
inline constexpr WaterState kWaterLevelMask = static_cast<WaterState>(0x0FU);
inline constexpr std::uint8_t kMaxWaterLevel = 8;

inline constexpr auto water_level_from_state(WaterState state) noexcept -> std::uint8_t {
    const auto raw_level = static_cast<std::uint8_t>(state & kWaterLevelMask);
    return raw_level > kMaxWaterLevel ? kMaxWaterLevel : raw_level;
}

inline constexpr auto water_state_is_source(WaterState state) noexcept -> bool {
    return (state & kWaterSourceBit) != 0;
}

inline constexpr auto make_water_state(std::uint8_t level, bool source = false) noexcept -> WaterState {
    const auto clamped_level = static_cast<WaterState>(std::min<std::uint8_t>(level, kMaxWaterLevel));
    return static_cast<WaterState>(clamped_level | (source ? kWaterSourceBit : 0U));
}

inline constexpr auto water_state_with_level(WaterState state, std::uint8_t level) noexcept -> WaterState {
    return make_water_state(level, water_state_is_source(state));
}

inline constexpr auto is_torch_block(BlockId block_id) noexcept -> bool {
    switch (static_cast<BlockType>(block_id)) {
    case BlockType::Torch:
    case BlockType::TorchWallPositiveX:
    case BlockType::TorchWallNegativeX:
    case BlockType::TorchWallPositiveZ:
    case BlockType::TorchWallNegativeZ:
        return true;
    case BlockType::Air:
    case BlockType::Grass:
    case BlockType::Dirt:
    case BlockType::Stone:
    case BlockType::Sand:
    case BlockType::Wood:
    case BlockType::Leaves:
    case BlockType::Cobblestone:
    case BlockType::Planks:
    case BlockType::Gravel:
    case BlockType::MossyStone:
    case BlockType::Snow:
    case BlockType::PineWood:
    case BlockType::PineLeaves:
    case BlockType::TallGrass:
    case BlockType::RedFlower:
    case BlockType::YellowFlower:
    case BlockType::DeadShrub:
    case BlockType::Cactus:
    case BlockType::Water:
    case BlockType::Glass:
    default:
        return false;
    }
}

inline constexpr auto is_wall_torch_block(BlockId block_id) noexcept -> bool {
    switch (static_cast<BlockType>(block_id)) {
    case BlockType::TorchWallPositiveX:
    case BlockType::TorchWallNegativeX:
    case BlockType::TorchWallPositiveZ:
    case BlockType::TorchWallNegativeZ:
        return true;
    case BlockType::Torch:
    case BlockType::Air:
    case BlockType::Grass:
    case BlockType::Dirt:
    case BlockType::Stone:
    case BlockType::Sand:
    case BlockType::Wood:
    case BlockType::Leaves:
    case BlockType::Cobblestone:
    case BlockType::Planks:
    case BlockType::Gravel:
    case BlockType::MossyStone:
    case BlockType::Snow:
    case BlockType::PineWood:
    case BlockType::PineLeaves:
    case BlockType::TallGrass:
    case BlockType::RedFlower:
    case BlockType::YellowFlower:
    case BlockType::DeadShrub:
    case BlockType::Cactus:
    case BlockType::Water:
    case BlockType::Glass:
    default:
        return false;
    }
}

inline constexpr auto block_item_id(BlockId block_id) noexcept -> BlockId {
    return is_torch_block(block_id) ? to_block_id(BlockType::Torch) : block_id;
}

inline constexpr auto is_inventory_only_item(BlockId block_id) noexcept -> bool {
    block_id = block_item_id(block_id);
    switch (static_cast<BlockType>(block_id)) {
    case BlockType::Pastron:
    case BlockType::RoundShield:
    case BlockType::Sword:
    case BlockType::Spear:
    case BlockType::Shoes:
    case BlockType::Pants:
        return true;
    case BlockType::Air:
    case BlockType::Grass:
    case BlockType::Dirt:
    case BlockType::Stone:
    case BlockType::Sand:
    case BlockType::Wood:
    case BlockType::Leaves:
    case BlockType::Torch:
    case BlockType::Cobblestone:
    case BlockType::Planks:
    case BlockType::Gravel:
    case BlockType::MossyStone:
    case BlockType::Snow:
    case BlockType::PineWood:
    case BlockType::PineLeaves:
    case BlockType::TallGrass:
    case BlockType::RedFlower:
    case BlockType::YellowFlower:
    case BlockType::DeadShrub:
    case BlockType::Cactus:
    case BlockType::Water:
    case BlockType::TorchWallPositiveX:
    case BlockType::TorchWallNegativeX:
    case BlockType::TorchWallPositiveZ:
    case BlockType::TorchWallNegativeZ:
    case BlockType::Glass:
    default:
        return false;
    }
}

inline constexpr auto is_weapon_item(BlockId block_id) noexcept -> bool {
    block_id = block_item_id(block_id);
    return block_id == to_block_id(BlockType::Sword) || block_id == to_block_id(BlockType::Spear);
}

inline constexpr auto is_placeable_item(BlockId block_id) noexcept -> bool {
    block_id = block_item_id(block_id);
    return block_id != to_block_id(BlockType::Air) && !is_inventory_only_item(block_id);
}

inline constexpr auto torch_support_offset(BlockId block_id) noexcept -> BlockCoord {
    switch (static_cast<BlockType>(block_id)) {
    case BlockType::Torch:
        return {0, -1, 0};
    case BlockType::TorchWallPositiveX:
        return {1, 0, 0};
    case BlockType::TorchWallNegativeX:
        return {-1, 0, 0};
    case BlockType::TorchWallPositiveZ:
        return {0, 0, 1};
    case BlockType::TorchWallNegativeZ:
        return {0, 0, -1};
    case BlockType::Air:
    case BlockType::Grass:
    case BlockType::Dirt:
    case BlockType::Stone:
    case BlockType::Sand:
    case BlockType::Wood:
    case BlockType::Leaves:
    case BlockType::Cobblestone:
    case BlockType::Planks:
    case BlockType::Gravel:
    case BlockType::MossyStone:
    case BlockType::Snow:
    case BlockType::PineWood:
    case BlockType::PineLeaves:
    case BlockType::TallGrass:
    case BlockType::RedFlower:
    case BlockType::YellowFlower:
    case BlockType::DeadShrub:
    case BlockType::Cactus:
    case BlockType::Water:
    case BlockType::Glass:
    case BlockType::Pastron:
    case BlockType::RoundShield:
    case BlockType::Sword:
    case BlockType::Spear:
    case BlockType::Shoes:
    case BlockType::Pants:
    default:
        return {};
    }
}

inline constexpr auto torch_block_from_support_offset(const BlockCoord& support_offset) noexcept -> BlockId {
    if (support_offset.x == 0 && support_offset.y == -1 && support_offset.z == 0) {
        return to_block_id(BlockType::Torch);
    }
    if (support_offset.x == 1 && support_offset.y == 0 && support_offset.z == 0) {
        return to_block_id(BlockType::TorchWallPositiveX);
    }
    if (support_offset.x == -1 && support_offset.y == 0 && support_offset.z == 0) {
        return to_block_id(BlockType::TorchWallNegativeX);
    }
    if (support_offset.x == 0 && support_offset.y == 0 && support_offset.z == 1) {
        return to_block_id(BlockType::TorchWallPositiveZ);
    }
    if (support_offset.x == 0 && support_offset.y == 0 && support_offset.z == -1) {
        return to_block_id(BlockType::TorchWallNegativeZ);
    }
    return to_block_id(BlockType::Air);
}

inline constexpr auto block_properties(BlockId block_id) noexcept -> BlockProperties {
    if (is_torch_block(block_id)) {
        return {false, false, false, false, BlockMeshType::Torch, static_cast<std::uint8_t>(14)};
    }

    switch (static_cast<BlockType>(block_id)) {
    case BlockType::Air:
        return {false, false, false, true, BlockMeshType::FullCube, static_cast<std::uint8_t>(0)};
    case BlockType::Leaves:
    case BlockType::PineLeaves:
        return {true, true, false, false, BlockMeshType::FullCube, static_cast<std::uint8_t>(0)};
    case BlockType::TallGrass:
    case BlockType::RedFlower:
    case BlockType::YellowFlower:
    case BlockType::DeadShrub:
        return {false, false, false, true, BlockMeshType::Cross, static_cast<std::uint8_t>(0)};
    case BlockType::Glass:
        return {false, true, false, false, BlockMeshType::FullCube, static_cast<std::uint8_t>(0)};
    case BlockType::Water:
        return {false, false, false, true, BlockMeshType::Water, static_cast<std::uint8_t>(0)};
    case BlockType::Pastron:
    case BlockType::RoundShield:
    case BlockType::Sword:
    case BlockType::Spear:
    case BlockType::Shoes:
    case BlockType::Pants:
        return {false, false, false, false, BlockMeshType::FullCube, static_cast<std::uint8_t>(0)};
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
    return block_properties(block_id).collidable;
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
    return block_id != to_block_id(BlockType::Air) && !is_inventory_only_item(block_id);
}

inline constexpr auto is_block_targetable(BlockId block_id) noexcept -> bool {
    return has_block_mesh(block_id);
}

inline constexpr auto block_break_duration_seconds(BlockId block_id) noexcept -> float {
    if (is_torch_block(block_id)) {
        return 0.18F;
    }

    switch (static_cast<BlockType>(block_id)) {
    case BlockType::Air:
        return 0.0F;
    case BlockType::Grass:
        return 0.85F;
    case BlockType::Dirt:
        return 0.80F;
    case BlockType::Stone:
        return 1.30F;
    case BlockType::Sand:
        return 0.72F;
    case BlockType::Wood:
        return 1.18F;
    case BlockType::Leaves:
        return 0.42F;
    case BlockType::Cobblestone:
        return 1.55F;
    case BlockType::Planks:
        return 0.92F;
    case BlockType::Gravel:
        return 0.88F;
    case BlockType::MossyStone:
        return 1.62F;
    case BlockType::Snow:
        return 0.36F;
    case BlockType::PineWood:
        return 1.24F;
    case BlockType::PineLeaves:
        return 0.44F;
    case BlockType::TallGrass:
        return 0.20F;
    case BlockType::RedFlower:
    case BlockType::YellowFlower:
        return 0.16F;
    case BlockType::DeadShrub:
        return 0.24F;
    case BlockType::Cactus:
        return 0.94F;
    case BlockType::Glass:
        return 0.42F;
    case BlockType::Water:
        return 0.65F;
    case BlockType::Pastron:
    case BlockType::RoundShield:
    case BlockType::Sword:
    case BlockType::Spear:
    case BlockType::Shoes:
    case BlockType::Pants:
        return 0.0F;
    default:
        return 0.80F;
    }
}

inline constexpr auto is_block_breakable(BlockId block_id) noexcept -> bool {
    return is_block_targetable(block_id) && block_break_duration_seconds(block_id) > 0.0F;
}

inline constexpr auto is_block_breakable_at(const BlockCoord& block_coord, BlockId block_id) noexcept -> bool {
    return block_coord.y > kWorldMinY && is_block_breakable(block_id);
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
