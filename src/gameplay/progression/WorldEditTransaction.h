#pragma once

#include "world/Block.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>

namespace valcraft {

inline constexpr std::size_t kWorldEditMaximumCellCount = 64U;
inline constexpr std::size_t kWorldEditShapeMaximumCellCount = 10U;

struct WorldEditCell {
    BlockCoord coordinate {};
    BlockId block_id = to_block_id(BlockType::Air);

    auto operator==(const WorldEditCell&) const -> bool = default;
};

struct WorldEditCellState {
    BlockCoord coordinate {};
    BlockId block_id = to_block_id(BlockType::Air);
    WaterState water_state = 0U;
    bool player_placed = false;

    auto operator==(const WorldEditCellState&) const -> bool = default;
};

enum class WorldEditTransactionStatus : std::uint8_t {
    InvalidTarget = 0,
    TooManyCells,
    InsufficientMaterials,
    CommitFailed,
    Success,
};

struct WorldEditTransactionResult {
    WorldEditTransactionStatus status =
        WorldEditTransactionStatus::InvalidTarget;
    std::size_t requested_cell_count = 0U;
    std::size_t unique_cell_count = 0U;
    std::size_t changed_cell_count = 0U;
    std::size_t commit_count = 0U;
    std::size_t rollback_count = 0U;

    [[nodiscard]] constexpr auto succeeded() const noexcept -> bool {
        return status == WorldEditTransactionStatus::Success;
    }
};

struct WorldEditTransactionCallbacks {
    std::function<bool(const WorldEditCell&)> validate_cell {};
    std::function<bool(const BlockCoord&)>
        cell_contains_player_or_creature {};
    std::function<std::optional<WorldEditCellState>(const BlockCoord&)>
        read_current {};
    std::function<bool(const WorldEditCell&)> commit_cell {};
    std::function<void(const WorldEditCellState&)> rollback_cell {};
    std::function<bool(BlockId, std::uint32_t)>
        materials_available {};
    std::function<bool(BlockId, std::uint32_t)>
        consume_materials {};
    std::function<void(BlockId, std::uint32_t)>
        refund_materials {};
};

class WorldEditTransaction {
public:
    [[nodiscard]] static auto execute(
        std::span<const WorldEditCell> requested_cells,
        const WorldEditTransactionCallbacks& callbacks)
        -> WorldEditTransactionResult;
};

enum class WorldEditAxis : std::uint8_t {
    X = 0,
    Y = 1,
    Z = 2,
};

enum class WorldEditFace : std::uint8_t {
    NegativeX = 0,
    PositiveX,
    NegativeY,
    PositiveY,
    NegativeZ,
    PositiveZ,
};

enum class WorldEditDirection : std::int8_t {
    Negative = -1,
    Positive = 1,
};

enum class WorldEditShape : std::uint8_t {
    Line = 0,
    Grid = 1,
};

struct WorldEditShapeRequest {
    BlockCoord origin {};
    BlockId block_id = to_block_id(BlockType::Air);
    std::uint8_t rank = 1U;
    WorldEditShape shape = WorldEditShape::Line;
    WorldEditFace support_face = WorldEditFace::PositiveY;
    WorldEditAxis primary_axis = WorldEditAxis::X;
    WorldEditDirection direction = WorldEditDirection::Positive;
    bool mastery_mirror = false;
};

struct WorldEditShapeResult {
    std::array<WorldEditCell, kWorldEditShapeMaximumCellCount> cells {};
    std::size_t cell_count = 0U;
    bool valid = false;

    [[nodiscard]] auto cell_span() const noexcept
        -> std::span<const WorldEditCell> {
        return {
            cells.data(),
            cell_count,
        };
    }
};

[[nodiscard]] auto generate_world_edit_shape(
    const WorldEditShapeRequest& request) noexcept
    -> WorldEditShapeResult;

} // namespace valcraft
