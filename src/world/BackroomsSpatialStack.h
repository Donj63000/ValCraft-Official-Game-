#pragma once

#include "world/BackroomsGenerator.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace valcraft {

inline constexpr std::size_t kBackroomsSpatialLevelCount = 5U;
inline constexpr int kBackroomsSpatialLevelsBelowAnchor = 2;
inline constexpr int kBackroomsSpatialLevelsAboveAnchor = 2;
inline constexpr int kBackroomsSpatialConnectorDistrictModules = 2;
inline constexpr std::array<int, kBackroomsSpatialLevelCount>
    kBackroomsSpatialFloorY {{0, 20, 40, 68, 98}};

enum class BackroomsVerticalConnectionStyle : std::uint8_t {
    ClassicStairs = 0,
    SpiralStairs = 1,
    MonumentalStairs = 2,
    NarrowStairs = 3,
    BrokenStairs = 4,
    InclinedRamp = 5,
    LongDescendingCorridor = 6,
    SlopedTunnel = 7,
    ScaleShiftPassage = 8,
    ChromaticAnomaly = 9,
};

enum class BackroomsSpatialProfile : std::uint8_t {
    LegacyV2 = 0,
    RecessedPoolroomsV3 = 1,
    FloodedPoolroomsV4 = 2,
};

struct BackroomsLevelPlacement {
    int logical_level = 0;
    int base_y = kBackroomsFloorY;
    int floor_y = kBackroomsFloorY;
    int roof_y = kBackroomsRoofY;
    BackroomsTheme theme = BackroomsTheme::Offices;

    auto operator==(const BackroomsLevelPlacement&) const -> bool = default;
};

struct BackroomsVerticalConnection {
    BackroomsVerticalConnectionStyle style =
        BackroomsVerticalConnectionStyle::ClassicStairs;
    int lower_level = -1;
    int upper_level = 0;
    int lower_floor_y = 20;
    int upper_floor_y = 40;
    int district_x = 0;
    int district_z = 0;
    int orientation_quarter_turns = 0;
    int walkable_width = 3;
    int path_length = 20;
    int headroom = 3;
    bool smooth_ramp = false;
    bool upper_approach = false;
    BlockCoord lower_landing {};
    BlockCoord upper_landing {};

    auto operator==(const BackroomsVerticalConnection&) const -> bool = default;
};

struct BackroomsSpatialColumn {
    std::array<BlockId, kChunkHeight> blocks {};
    std::array<WaterState, kChunkHeight> water {};
};

class BackroomsSpatialStack {
public:
    explicit BackroomsSpatialStack(
        int seed = 1337,
        int anchor_level = 0,
        BackroomsSpatialProfile profile =
            BackroomsSpatialProfile::LegacyV2) noexcept;

    [[nodiscard]] auto seed() const noexcept -> int;
    [[nodiscard]] auto anchor_level() const noexcept -> int;
    [[nodiscard]] auto placements() const noexcept
        -> const std::array<BackroomsLevelPlacement,
                            kBackroomsSpatialLevelCount>&;
    [[nodiscard]] auto placement_for_level(int logical_level) const noexcept
        -> std::optional<BackroomsLevelPlacement>;
    [[nodiscard]] auto logical_level_at_y(float world_y) const noexcept -> int;
    [[nodiscard]] auto theme_at_y(float world_y) const noexcept
        -> BackroomsTheme;
    [[nodiscard]] auto spawn_block(int logical_level) const noexcept
        -> BlockCoord;

    [[nodiscard]] auto connection_for_district(
        int lower_level,
        int district_x,
        int district_z) const noexcept
        -> std::optional<BackroomsVerticalConnection>;
    [[nodiscard]] auto sample_block(
        int world_x,
        int world_y,
        int world_z) const noexcept -> BlockId;
    [[nodiscard]] auto sample_water_state(
        int world_x,
        int world_y,
        int world_z) const noexcept -> WaterState;
    [[nodiscard]] auto rasterize_column(
        int world_x,
        int world_z) const noexcept -> BackroomsSpatialColumn;
    [[nodiscard]] auto has_player_clearance(
        int world_x,
        int feet_y,
        int world_z,
        int required_height = 2) const noexcept -> bool;
    [[nodiscard]] auto has_body_clearance(
        float world_x,
        float feet_y,
        float world_z,
        int required_height,
        float half_width) const noexcept -> bool;

private:
    struct ConnectorColumn;

    [[nodiscard]] auto layer_block(
        std::size_t layer_index,
        const BackroomsColumnSample& column,
        int world_y) const noexcept -> BlockId;
    [[nodiscard]] auto connector_column(
        int world_x,
        int world_z) const noexcept -> ConnectorColumn;
    [[nodiscard]] auto connector_override(
        const ConnectorColumn& connector_column,
        int world_y) const noexcept -> std::optional<BlockId>;
    [[nodiscard]] auto needs_recessed_pool_shore(
        std::size_t layer_index,
        const BackroomsColumnSample& column,
        int world_x,
        int world_y,
        int world_z) const noexcept -> bool;
    [[nodiscard]] auto layer_index_at_y(int world_y) const noexcept
        -> std::size_t;

    int seed_ = 1337;
    int anchor_level_ = 0;
    BackroomsSpatialProfile profile_ =
        BackroomsSpatialProfile::LegacyV2;
    std::array<BackroomsLevelPlacement,
               kBackroomsSpatialLevelCount> placements_ {};
    std::array<BackroomsGenerator,
               kBackroomsSpatialLevelCount> generators_ {};
};

[[nodiscard]] auto backrooms_vertical_connection_style_name(
    BackroomsVerticalConnectionStyle style) noexcept -> const char*;

} // namespace valcraft
