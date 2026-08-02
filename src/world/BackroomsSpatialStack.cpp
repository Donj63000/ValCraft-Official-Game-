#include "world/BackroomsSpatialStack.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace valcraft {

namespace {

constexpr int kConnectorDistrictWorldSize =
    kBackroomsSpatialConnectorDistrictModules *
    kBackroomsModuleSize;
constexpr int kConnectorSearchRadius = 72;
constexpr int kConnectorLandingHalfWidth = 3;
constexpr int kConnectorUpperGalleryHeadroom = 4;
// Je garde assez de tranches pour les croisements devenus plus frequents avec
// les districts de deux modules, sans tronquer silencieusement une volee.
constexpr std::size_t kMaximumConnectorColumnSlices = 32U;
constexpr std::size_t kMaximumConnectorPathPoints =
    static_cast<std::size_t>(kChunkHeight * 2 + 1);

struct PathPoint {
    int x = 0;
    int z = 0;
};

[[nodiscard]] constexpr auto mix64(std::uint64_t value) noexcept
    -> std::uint64_t {
    value += UINT64_C(0x9E3779B97F4A7C15);
    value = (value ^ (value >> 30U)) *
            UINT64_C(0xBF58476D1CE4E5B9);
    value = (value ^ (value >> 27U)) *
            UINT64_C(0x94D049BB133111EB);
    return value ^ (value >> 31U);
}

[[nodiscard]] auto connector_hash(
    int seed,
    int lower_level,
    int district_x,
    int district_z) noexcept -> std::uint32_t {

    auto value = mix64(
        static_cast<std::uint64_t>(
            static_cast<std::int64_t>(seed)) ^
        UINT64_C(0xB4C3A17E5D2906F1));
    value ^= mix64(
        static_cast<std::uint64_t>(
            static_cast<std::int64_t>(lower_level)) +
        UINT64_C(0xC6BC279692B5CC83));
    value ^= mix64(
        static_cast<std::uint64_t>(
            static_cast<std::int64_t>(district_x)) +
        UINT64_C(0xD1B54A32D192ED03));
    value ^= mix64(
        static_cast<std::uint64_t>(
            static_cast<std::int64_t>(district_z)) +
        UINT64_C(0x8CB92BA72F3D8DD7));
    return static_cast<std::uint32_t>(mix64(value) >> 32U);
}

[[nodiscard]] constexpr auto floor_division(
    int value,
    int divisor) noexcept -> int {
    auto quotient = value / divisor;
    if (value % divisor < 0) {
        --quotient;
    }
    return quotient;
}

[[nodiscard]] constexpr auto saturating_add(
    int value,
    int delta) noexcept -> int {
    const auto wide =
        static_cast<std::int64_t>(value) +
        static_cast<std::int64_t>(delta);
    return static_cast<int>(
        std::clamp(
            wide,
            static_cast<std::int64_t>(
                std::numeric_limits<int>::min()),
            static_cast<std::int64_t>(
                std::numeric_limits<int>::max())));
}

[[nodiscard]] constexpr auto between_inclusive(
    int value,
    int first,
    int second) noexcept -> bool {
    return value >= std::min(first, second) &&
           value <= std::max(first, second);
}

[[nodiscard]] auto on_axis_aligned_approach_segment(
    int world_x,
    int world_z,
    PathPoint start,
    PathPoint destination,
    int half_width) noexcept -> bool {
    if (start.z == destination.z) {
        return between_inclusive(
                   world_x,
                   start.x,
                   destination.x) &&
               std::abs(world_z - start.z) <= half_width;
    }
    if (start.x == destination.x) {
        return std::abs(world_x - start.x) <= half_width &&
               between_inclusive(
                   world_z,
                   start.z,
                   destination.z);
    }
    return false;
}

[[nodiscard]] auto on_starter_upper_approach(
    int world_x,
    int world_z,
    PathPoint start,
    PathPoint destination) noexcept -> bool {
    constexpr auto escape_distance = 4;
    constexpr auto half_width = 1;
    const auto escape =
        PathPoint {start.x + escape_distance, start.z};
    const auto corner =
        PathPoint {
            escape.x,
            destination.z,
        };
    return
        on_axis_aligned_approach_segment(
            world_x,
            world_z,
            start,
            escape,
            half_width) ||
        on_axis_aligned_approach_segment(
            world_x,
            world_z,
            escape,
            corner,
            half_width) ||
        on_axis_aligned_approach_segment(
            world_x,
            world_z,
            corner,
            destination,
            half_width);
}

[[nodiscard]] constexpr auto forward_for_orientation(
    int orientation) noexcept -> PathPoint {
    switch (orientation & 3) {
    case 1:
        return {1, 0};
    case 2:
        return {0, -1};
    case 3:
        return {-1, 0};
    case 0:
    default:
        return {0, 1};
    }
}

[[nodiscard]] constexpr auto rotate_local_path_point(
    PathPoint local,
    int orientation) noexcept -> PathPoint {
    switch (orientation & 3) {
    case 1:
        return {local.z, -local.x};
    case 2:
        return {-local.x, -local.z};
    case 3:
        return {-local.z, local.x};
    case 0:
    default:
        return local;
    }
}

[[nodiscard]] auto square_spiral_point(int index) noexcept -> PathPoint {
    PathPoint point {};
    if (index <= 0) {
        return point;
    }

    constexpr std::array<PathPoint, 4U> directions {{
        {0, 1},
        {1, 0},
        {0, -1},
        {-1, 0},
    }};
    auto direction = 0;
    auto leg_length = 1;
    auto leg_progress = 0;
    auto completed_legs_at_length = 0;
    for (int step = 0; step < index; ++step) {
        point.x += directions[static_cast<std::size_t>(direction)].x;
        point.z += directions[static_cast<std::size_t>(direction)].z;
        ++leg_progress;
        if (leg_progress < leg_length) {
            continue;
        }
        leg_progress = 0;
        direction = (direction + 1) & 3;
        ++completed_legs_at_length;
        if (completed_legs_at_length == 2) {
            completed_legs_at_length = 0;
            ++leg_length;
        }
    }
    return point;
}

[[nodiscard]] auto path_point(
    const BackroomsVerticalConnection& connection,
    int index) noexcept -> PathPoint {
    const auto local =
        connection.style ==
                BackroomsVerticalConnectionStyle::SpiralStairs
            ? square_spiral_point(index)
            : PathPoint {0, index};
    const auto rotated =
        rotate_local_path_point(
            local,
            connection.orientation_quarter_turns);
    return {
        connection.lower_landing.x + rotated.x,
        connection.lower_landing.z + rotated.z,
    };
}

[[nodiscard]] auto connector_path_points(
    const BackroomsVerticalConnection& connection) noexcept
    -> std::array<PathPoint, kMaximumConnectorPathPoints> {
    std::array<PathPoint, kMaximumConnectorPathPoints> points {};
    constexpr std::array<PathPoint, 4U> spiral_directions {{
        {0, 1},
        {1, 0},
        {0, -1},
        {-1, 0},
    }};
    auto spiral_point = PathPoint {};
    auto spiral_direction = 0;
    auto spiral_leg_length = 1;
    auto spiral_leg_progress = 0;
    auto spiral_completed_legs = 0;
    const auto count =
        std::min(
            static_cast<std::size_t>(
                std::max(connection.path_length, 0)),
            points.size());

    for (std::size_t index = 0U;
         index < count;
         ++index) {
        const auto local =
            connection.style ==
                    BackroomsVerticalConnectionStyle::SpiralStairs
                ? spiral_point
                : PathPoint {
                      0,
                      static_cast<int>(index),
                  };
        const auto rotated =
            rotate_local_path_point(
                local,
                connection.orientation_quarter_turns);
        points[index] = {
            connection.lower_landing.x + rotated.x,
            connection.lower_landing.z + rotated.z,
        };

        if (connection.style !=
                BackroomsVerticalConnectionStyle::SpiralStairs ||
            index + 1U >= count) {
            continue;
        }
        spiral_point.x +=
            spiral_directions[
                static_cast<std::size_t>(spiral_direction)]
                .x;
        spiral_point.z +=
            spiral_directions[
                static_cast<std::size_t>(spiral_direction)]
                .z;
        ++spiral_leg_progress;
        if (spiral_leg_progress < spiral_leg_length) {
            continue;
        }
        spiral_leg_progress = 0;
        spiral_direction = (spiral_direction + 1) & 3;
        ++spiral_completed_legs;
        if (spiral_completed_legs == 2) {
            spiral_completed_legs = 0;
            ++spiral_leg_length;
        }
    }
    return points;
}

[[nodiscard]] auto width_at_path_index(
    const BackroomsVerticalConnection& connection,
    int index) noexcept -> int {
    if (connection.style !=
            BackroomsVerticalConnectionStyle::ScaleShiftPassage ||
        connection.path_length <= 1) {
        return connection.walkable_width;
    }
    const auto scale_band =
        (index * 4) /
        std::max(connection.path_length - 1, 1);
    return 1 + std::clamp(scale_band, 0, 3) * 2;
}

[[nodiscard]] auto path_surface_y(
    const BackroomsVerticalConnection& connection,
    int index) noexcept -> int {
    const auto rise =
        connection.upper_floor_y -
        connection.lower_floor_y;
    const auto denominator =
        std::max(connection.path_length - 1, 1);
    return connection.lower_floor_y + 1 +
           (index * rise) / denominator;
}

[[nodiscard]] auto chromatic_wall_block(
    int index,
    int path_length) noexcept -> BlockId {
    constexpr std::array<BlockType, 6U> palette {{
        BlockType::BackroomsWallYellow,
        BlockType::BackroomsWallGreen,
        BlockType::BackroomsWallBlue,
        BlockType::BackroomsWallRose,
        BlockType::BackroomsWallOxide,
        BlockType::BackroomsConcrete,
    }};
    const auto palette_index =
        static_cast<std::size_t>(
            std::clamp(
                (index * static_cast<int>(palette.size())) /
                    std::max(path_length, 1),
                0,
                static_cast<int>(palette.size()) - 1));
    return to_block_id(palette[palette_index]);
}

[[nodiscard]] auto ramp_block_for_direction(
    PathPoint direction) noexcept -> BlockId {
    if (direction.x > 0) {
        return to_block_id(BlockType::BackroomsRampPositiveX);
    }
    if (direction.x < 0) {
        return to_block_id(BlockType::BackroomsRampNegativeX);
    }
    if (direction.z < 0) {
        return to_block_id(BlockType::BackroomsRampNegativeZ);
    }
    return to_block_id(BlockType::BackroomsRampPositiveZ);
}

} // namespace

struct BackroomsSpatialStack::ConnectorColumn {
    struct Slice {
        BackroomsVerticalConnection connection {};
        int path_index = 0;
        int surface_y = 0;
        int lateral_distance = 0;
        int half_width = 0;
        BlockId ramp_block = to_block_id(BlockType::Air);
        bool path = false;
        bool rail = false;
        bool lower_landing = false;
        bool upper_landing = false;
        bool approach = false;
        bool broken = false;
    };

    std::array<Slice, kMaximumConnectorColumnSlices> slices {};
    std::size_t count = 0U;

    void add(const Slice& slice) noexcept {
        if (count >= slices.size()) {
            return;
        }
        slices[count++] = slice;
    }
};

BackroomsSpatialStack::BackroomsSpatialStack(
    int seed,
    int anchor_level,
    BackroomsSpatialProfile profile) noexcept
    : seed_(seed),
      anchor_level_(anchor_level),
      profile_(profile) {

    for (std::size_t index = 0U;
         index < placements_.size();
         ++index) {
        const auto relative_level =
            static_cast<int>(index) -
            kBackroomsSpatialLevelsBelowAnchor;
        const auto logical_level =
            saturating_add(anchor_level_, relative_level);
        const auto base_y =
            kBackroomsSpatialFloorY[index];
        const auto theme =
            logical_level <= -2
                ? BackroomsTheme::Poolrooms
                : BackroomsTheme::Offices;
        // Je réserve un voxel sous le sol nominal en V3 et deux en V4. Les
        // bassins profonds V4 restent ainsi contenus dans leur propre tranche
        // sans toucher au plafond de l'étage inférieur.
        const auto pool_floor_offset =
            theme != BackroomsTheme::Poolrooms
                ? 0
                : profile ==
                          BackroomsSpatialProfile::FloodedPoolroomsV4
                      ? 2
                      : profile ==
                                BackroomsSpatialProfile::RecessedPoolroomsV3
                            ? 1
                            : 0;
        const auto floor_y =
            base_y + pool_floor_offset;
        const auto roof_y =
            index + 1U < placements_.size()
                ? kBackroomsSpatialFloorY[index + 1U] - 1
                : kWorldMaxY;
        placements_[index] = {
            .logical_level = logical_level,
            .base_y = base_y,
            .floor_y = floor_y,
            .roof_y = roof_y,
            .theme = theme,
        };
        const auto pool_geometry_profile =
            profile ==
                    BackroomsSpatialProfile::FloodedPoolroomsV4
                ? BackroomsPoolGeometryProfile::FloodedDistrictsV4
                : profile ==
                          BackroomsSpatialProfile::RecessedPoolroomsV3
                      ? BackroomsPoolGeometryProfile::RecessedOneBlock
                      : BackroomsPoolGeometryProfile::LegacyFlat;
        generators_[index] =
            BackroomsGenerator(
                seed_,
                logical_level,
                kBackroomsSpatialConnectorDistrictModules,
                pool_geometry_profile);
    }
}

auto BackroomsSpatialStack::seed() const noexcept -> int {
    return seed_;
}

auto BackroomsSpatialStack::anchor_level() const noexcept -> int {
    return anchor_level_;
}

auto BackroomsSpatialStack::placements() const noexcept
    -> const std::array<BackroomsLevelPlacement,
                        kBackroomsSpatialLevelCount>& {
    return placements_;
}

auto BackroomsSpatialStack::placement_for_level(
    int logical_level) const noexcept
    -> std::optional<BackroomsLevelPlacement> {
    const auto found =
        std::find_if(
            placements_.begin(),
            placements_.end(),
            [logical_level](const auto& placement) noexcept {
                return placement.logical_level == logical_level;
            });
    if (found == placements_.end()) {
        return std::nullopt;
    }
    return *found;
}

auto BackroomsSpatialStack::logical_level_at_y(
    float world_y) const noexcept -> int {
    if (!std::isfinite(world_y)) {
        return anchor_level_;
    }
    for (std::size_t index = 0U;
         index + 1U < placements_.size();
         ++index) {
        const auto midpoint =
            (static_cast<float>(placements_[index].floor_y) +
             static_cast<float>(placements_[index + 1U].floor_y)) *
            0.5F;
        if (world_y < midpoint) {
            return placements_[index].logical_level;
        }
    }
    return placements_.back().logical_level;
}

auto BackroomsSpatialStack::theme_at_y(
    float world_y) const noexcept -> BackroomsTheme {
    const auto logical_level = logical_level_at_y(world_y);
    return logical_level <= -2
               ? BackroomsTheme::Poolrooms
               : BackroomsTheme::Offices;
}

auto BackroomsSpatialStack::spawn_block(
    int logical_level) const noexcept -> BlockCoord {
    auto index = std::size_t {0U};
    for (; index < placements_.size(); ++index) {
        if (placements_[index].logical_level == logical_level) {
            break;
        }
    }
    if (index == placements_.size()) {
        index = static_cast<std::size_t>(
            kBackroomsSpatialLevelsBelowAnchor);
    }
    const auto local_spawn = generators_[index].spawn_block();
    return {
        local_spawn.x,
        placements_[index].floor_y + 1,
        local_spawn.z,
    };
}

auto BackroomsSpatialStack::connection_for_district(
    int lower_level,
    int district_x,
    int district_z) const noexcept
    -> std::optional<BackroomsVerticalConnection> {
    auto lower_index = placements_.size();
    for (std::size_t index = 0U;
         index + 1U < placements_.size();
         ++index) {
        if (placements_[index].logical_level == lower_level) {
            lower_index = index;
            break;
        }
    }
    if (lower_index + 1U >= placements_.size()) {
        return std::nullopt;
    }

    const auto legacy_anchor =
        generators_[lower_index].connector_in_district(
            BackroomsConnectorDirection::Up,
            district_x,
            district_z);
    const auto hash =
        connector_hash(
            seed_,
            lower_level,
            district_x,
            district_z);
    const auto anchor_index =
        static_cast<std::size_t>(
            kBackroomsSpatialLevelsBelowAnchor);
    const auto anchor_spawn =
        generators_[anchor_index].spawn_block();
    const auto starter_district =
        district_x == floor_division(
                          anchor_spawn.x,
                          kConnectorDistrictWorldSize) &&
        district_z == floor_division(
                          anchor_spawn.z,
                          kConnectorDistrictWorldSize);
    const auto starter_descent =
        starter_district &&
        lower_index + 1U == anchor_index;
    const auto starter_ascent =
        starter_district &&
        lower_index == anchor_index;

    auto style =
        static_cast<BackroomsVerticalConnectionStyle>(hash % 10U);
    // Je reserve deux escaliers compacts autour du hub initial. Le joueur voit
    // ainsi toujours une vraie montee et une vraie descente, quelle que soit la
    // seed, tandis que tous les autres districts conservent leurs variantes.
    if (starter_descent) {
        style = BackroomsVerticalConnectionStyle::SpiralStairs;
    } else if (starter_ascent) {
        style = BackroomsVerticalConnectionStyle::ClassicStairs;
    }
    const auto rise =
        placements_[lower_index + 1U].floor_y -
        placements_[lower_index].floor_y;

    auto width = 3;
    auto path_length = rise + 1;
    auto headroom = 3;
    auto smooth_ramp = false;
    switch (style) {
    case BackroomsVerticalConnectionStyle::SpiralStairs:
        width = 1;
        path_length = rise * 2 + 1;
        break;
    case BackroomsVerticalConnectionStyle::MonumentalStairs:
        width = 9;
        path_length = rise * 2 + 1;
        headroom = 5;
        break;
    case BackroomsVerticalConnectionStyle::NarrowStairs:
        width = 1;
        break;
    case BackroomsVerticalConnectionStyle::BrokenStairs:
        width = 3;
        break;
    case BackroomsVerticalConnectionStyle::InclinedRamp:
        width = 5;
        smooth_ramp = true;
        break;
    case BackroomsVerticalConnectionStyle::LongDescendingCorridor:
        width = 5;
        path_length = rise * 2 + 1;
        headroom = 4;
        break;
    case BackroomsVerticalConnectionStyle::SlopedTunnel:
        width = 3;
        headroom = 3;
        smooth_ramp = true;
        break;
    case BackroomsVerticalConnectionStyle::ScaleShiftPassage:
        width = 7;
        headroom = 4;
        break;
    case BackroomsVerticalConnectionStyle::ChromaticAnomaly:
        width = 5;
        headroom = 4;
        break;
    case BackroomsVerticalConnectionStyle::ClassicStairs:
    default:
        break;
    }

    auto orientation_quarter_turns =
        static_cast<int>(
            legacy_anchor.destination_yaw_degrees / 90.0F) & 3;
    auto lower_landing = BlockCoord {
        legacy_anchor.trigger_block.x,
        placements_[lower_index].floor_y + 1,
        legacy_anchor.trigger_block.z,
    };
    if (starter_descent) {
        orientation_quarter_turns = 1;
        lower_landing = {
            anchor_spawn.x + 3,
            placements_[lower_index].floor_y + 1,
            anchor_spawn.z + 7,
        };
    } else if (starter_ascent) {
        orientation_quarter_turns = 2;
        lower_landing = {
            anchor_spawn.x,
            placements_[lower_index].floor_y + 1,
            anchor_spawn.z - 8,
        };
    }

    BackroomsVerticalConnection connection {
        .style = style,
        .lower_level = lower_level,
        .upper_level = placements_[lower_index + 1U].logical_level,
        .lower_floor_y = placements_[lower_index].floor_y,
        .upper_floor_y = placements_[lower_index + 1U].floor_y,
        .district_x = district_x,
        .district_z = district_z,
        .orientation_quarter_turns =
            orientation_quarter_turns,
        .walkable_width = width,
        .path_length = path_length,
        .headroom = headroom,
        .smooth_ramp = smooth_ramp,
        .upper_approach = starter_ascent,
        .lower_landing = lower_landing,
    };
    const auto upper_point =
        path_point(connection, path_length - 1);
    connection.upper_landing = {
        upper_point.x,
        connection.upper_floor_y + 1,
        upper_point.z,
    };
    return connection;
}

auto BackroomsSpatialStack::layer_index_at_y(
    int world_y) const noexcept -> std::size_t {
    for (std::size_t index = 0U;
         index + 1U < placements_.size();
         ++index) {
        if (world_y < placements_[index + 1U].base_y) {
            return index;
        }
    }
    return placements_.size() - 1U;
}

auto BackroomsSpatialStack::layer_block(
    std::size_t layer_index,
    const BackroomsColumnSample& column,
    int world_y) const noexcept -> BlockId {
    const auto& placement = placements_[layer_index];
    if (world_y < placement.base_y ||
        world_y > placement.roof_y) {
        return to_block_id(BlockType::Air);
    }

    const auto translate_y =
        [&placement](int local_y) noexcept {
            return placement.floor_y +
                   (local_y - kBackroomsFloorY);
        };
    const auto ceiling_y =
        std::clamp(
            translate_y(column.ceiling_y),
            placement.floor_y + 4,
            placement.roof_y);
    const auto wall_top_y =
        std::clamp(
            translate_y(column.wall_top_y),
            translate_y(column.floor_y),
            ceiling_y);
    const auto overhead_bottom_y =
        translate_y(column.overhead_bottom_y);
    const auto overhead_top_y =
        translate_y(column.overhead_top_y);

    const auto floor_y = translate_y(column.floor_y);
    // Je materialise les fondations entre la base physique de la tranche et
    // le sol propre a la colonne. En V3, cela soutient les rives tandis que le
    // fond humide descend exactement d'un voxel.
    if (world_y < floor_y) {
        return column.foundation_block;
    }
    if (world_y == floor_y) {
        return column.floor_block;
    }
    if (column.wall &&
        world_y > floor_y &&
        world_y <= wall_top_y) {
        return column.wall_block;
    }
    if (world_y >= overhead_bottom_y &&
        world_y <= overhead_top_y &&
        world_y < ceiling_y) {
        return column.overhead_block;
    }
    if (world_y == ceiling_y) {
        return column.ceiling_block;
    }
    if (world_y > ceiling_y) {
        return column.roof_block;
    }
    return to_block_id(BlockType::Air);
}

auto BackroomsSpatialStack::connector_column(
    int world_x,
    int world_z) const noexcept -> ConnectorColumn {
    ConnectorColumn result {};
    const auto center_district_x =
        floor_division(world_x, kConnectorDistrictWorldSize);
    const auto center_district_z =
        floor_division(world_z, kConnectorDistrictWorldSize);

    for (std::size_t lower_index = 0U;
         lower_index + 1U < placements_.size();
         ++lower_index) {
        for (int district_offset_z = -1;
             district_offset_z <= 1;
             ++district_offset_z) {
            for (int district_offset_x = -1;
                 district_offset_x <= 1;
                 ++district_offset_x) {
                const auto connection =
                    connection_for_district(
                        placements_[lower_index].logical_level,
                        center_district_x + district_offset_x,
                        center_district_z + district_offset_z);
                if (!connection.has_value()) {
                    continue;
                }
                const auto trigger_delta_x =
                    world_x - connection->lower_landing.x;
                const auto trigger_delta_z =
                    world_z - connection->lower_landing.z;
                if (std::abs(trigger_delta_x) >
                        kConnectorSearchRadius ||
                    std::abs(trigger_delta_z) >
                        kConnectorSearchRadius) {
                    continue;
                }

                const auto near_lower_landing =
                    std::abs(trigger_delta_x) <=
                        kConnectorLandingHalfWidth &&
                    std::abs(trigger_delta_z) <=
                        kConnectorLandingHalfWidth;
                const auto upper_delta_x =
                    world_x - connection->upper_landing.x;
                const auto upper_delta_z =
                    world_z - connection->upper_landing.z;
                const auto near_upper_landing =
                    std::abs(upper_delta_x) <=
                        kConnectorLandingHalfWidth &&
                    std::abs(upper_delta_z) <=
                        kConnectorLandingHalfWidth;
                auto on_upper_approach = false;
                if (connection->upper_approach) {
                    const auto upper_hub =
                        generators_[lower_index + 1U]
                            .spawn_block();
                    // Je prolonge le palier superieur jusqu'au hub garanti :
                    // la volee visible ne peut donc jamais finir dans une
                    // piece fermee par la generation procedurale.
                    on_upper_approach =
                        on_starter_upper_approach(
                            world_x,
                            world_z,
                            {
                                connection->upper_landing.x,
                                connection->upper_landing.z,
                            },
                            {upper_hub.x, upper_hub.z});
                }
                if (near_lower_landing ||
                    near_upper_landing ||
                    on_upper_approach) {
                    ConnectorColumn::Slice landing {};
                    landing.connection = *connection;
                    landing.lower_landing = near_lower_landing;
                    landing.upper_landing =
                        near_upper_landing ||
                        on_upper_approach;
                    landing.approach = on_upper_approach;
                    result.add(landing);
                }

                const auto path_points =
                    connector_path_points(*connection);
                const auto path_point_count =
                    std::min(
                        static_cast<std::size_t>(
                            std::max(
                                connection->path_length,
                                0)),
                        path_points.size());
                for (std::size_t path_index = 0U;
                     path_index < path_point_count;
                     ++path_index) {
                    const auto point =
                        path_points[path_index];
                    const auto next_point =
                        path_points[
                            std::min(
                                path_index + 1U,
                                path_point_count - 1U)];
                    const auto previous_point =
                        path_points[
                            path_index > 0U
                                ? path_index - 1U
                                : 0U];
                    auto tangent = PathPoint {
                        next_point.x - point.x,
                        next_point.z - point.z,
                    };
                    if (tangent.x == 0 && tangent.z == 0) {
                        tangent = {
                            point.x - previous_point.x,
                            point.z - previous_point.z,
                        };
                    }
                    const auto delta_x = world_x - point.x;
                    const auto delta_z = world_z - point.z;
                    const auto longitudinal =
                        tangent.x != 0
                            ? std::abs(delta_x)
                            : std::abs(delta_z);
                    const auto lateral =
                        tangent.x != 0
                            ? std::abs(delta_z)
                            : std::abs(delta_x);
                    const auto width =
                        width_at_path_index(
                            *connection,
                            static_cast<int>(path_index));
                    const auto half_width = width / 2;
                    const auto on_path =
                        longitudinal == 0 &&
                        lateral <= half_width;
                    const auto on_rail =
                        connection->style !=
                            BackroomsVerticalConnectionStyle::SpiralStairs &&
                        longitudinal == 0 &&
                        lateral == half_width + 1;
                    if (!on_path && !on_rail) {
                        continue;
                    }

                    ConnectorColumn::Slice slice {};
                    slice.connection = *connection;
                    slice.path_index =
                        static_cast<int>(path_index);
                    slice.surface_y =
                        path_surface_y(
                            *connection,
                            static_cast<int>(path_index));
                    slice.lateral_distance = lateral;
                    slice.half_width = half_width;
                    slice.path = on_path;
                    slice.rail = on_rail;
                    slice.broken =
                        connection->style ==
                            BackroomsVerticalConnectionStyle::BrokenStairs &&
                        slice.lateral_distance == 0 &&
                        path_index > 2U &&
                        path_index + 3U <
                            path_point_count &&
                        path_index % 7U == 4U;
                    if (connection->smooth_ramp &&
                        path_index + 1U <
                            path_point_count) {
                        slice.ramp_block =
                            ramp_block_for_direction({
                                next_point.x - point.x,
                                next_point.z - point.z,
                            });
                    }
                    result.add(slice);
                }
            }
        }
    }
    return result;
}

auto BackroomsSpatialStack::connector_override(
    const ConnectorColumn& connector_column,
    int world_y) const noexcept -> std::optional<BlockId> {
    std::optional<BlockId> chosen;
    auto chosen_priority = -1;
    const auto choose =
        [&](BlockId block, int priority) noexcept {
            if (priority > chosen_priority) {
                chosen = block;
                chosen_priority = priority;
            }
        };

    for (std::size_t index = 0U;
         index < connector_column.count;
         ++index) {
        const auto& slice = connector_column.slices[index];
        const auto& connection = slice.connection;
        const auto lower_floor = connection.lower_floor_y;
        const auto upper_floor = connection.upper_floor_y;

        if (slice.lower_landing) {
            if (world_y == lower_floor) {
                choose(
                    to_block_id(BlockType::BackroomsConnectorStep),
                    4);
            } else if (world_y > lower_floor &&
                       world_y <= lower_floor +
                                      kConnectorUpperGalleryHeadroom) {
                choose(to_block_id(BlockType::Air), 3);
            }
        }
        if (slice.upper_landing) {
            if (world_y == upper_floor) {
                choose(
                    to_block_id(BlockType::BackroomsConnectorStep),
                    4);
            } else if (world_y > upper_floor &&
                       world_y <= upper_floor +
                                      kConnectorUpperGalleryHeadroom) {
                choose(
                    to_block_id(BlockType::Air),
                    slice.approach ? 6 : 3);
            }
        }
        if (!slice.path && !slice.rail) {
            continue;
        }

        // Je prolonge la galerie uniquement au-dessus du niveau supérieur.
        // La dalle Y=upper_floor reste ensuite traitée comme un chevêtre :
        // j'y ouvre les dernières marches au lieu de fermer leur headroom.
        if (world_y > upper_floor &&
                   world_y <= upper_floor +
                                  kConnectorUpperGalleryHeadroom) {
            if (slice.rail &&
                world_y <= upper_floor + 2) {
                choose(
                    connection.style ==
                            BackroomsVerticalConnectionStyle::ChromaticAnomaly
                        ? chromatic_wall_block(
                              slice.path_index,
                              connection.path_length)
                        : to_block_id(BlockType::PoolroomsMetal),
                    5);
            } else {
                choose(to_block_id(BlockType::Air), 3);
            }
        }

        if (slice.path && world_y == upper_floor) {
            const auto step_block_y = slice.surface_y - 1;
            const auto last_path_cell =
                slice.path_index + 1 >=
                connection.path_length;
            const auto final_smooth_ramp =
                connection.smooth_ramp &&
                !last_path_cell &&
                slice.surface_y == upper_floor &&
                slice.ramp_block !=
                    to_block_id(BlockType::Air);
            if (final_smooth_ramp) {
                choose(slice.ramp_block, 7);
            } else if (last_path_cell) {
                choose(
                    to_block_id(BlockType::BackroomsConnectorStep),
                    7);
            } else if (upper_floor <=
                       (connection.smooth_ramp
                            ? slice.surface_y
                            : step_block_y) +
                           connection.headroom) {
                // Je donne la priorité à l'ouverture sur le carré du
                // palier : sans cela ses blocs pleins reboucheraient la volée.
                choose(to_block_id(BlockType::Air), 6);
            }
        }

        if (world_y < lower_floor ||
            world_y >= upper_floor) {
            continue;
        }

        const auto step_block_y = slice.surface_y - 1;
        if (slice.rail) {
            const auto rail_top =
                connection.style ==
                        BackroomsVerticalConnectionStyle::SlopedTunnel
                    ? step_block_y + connection.headroom
                    : step_block_y + 2;
            if (world_y >= lower_floor &&
                world_y <= rail_top) {
                choose(
                    connection.style ==
                            BackroomsVerticalConnectionStyle::ChromaticAnomaly
                        ? chromatic_wall_block(
                              slice.path_index,
                              connection.path_length)
                        : to_block_id(BlockType::PoolroomsMetal),
                    5);
            }
            continue;
        }

        if (connection.smooth_ramp &&
            slice.ramp_block !=
                to_block_id(BlockType::Air)) {
            const auto ramp_base_y = slice.surface_y;
            if (world_y == lower_floor) {
                choose(
                    to_block_id(BlockType::BackroomsConnectorStep),
                    4);
            } else if (world_y > lower_floor &&
                       world_y < ramp_base_y) {
                // Je laisse la sous-face creuse : le prisme incliné reste la
                // seule surface autoritaire et aucun cube ne bloque sa pente.
                choose(to_block_id(BlockType::Air), 3);
            } else if (world_y == ramp_base_y) {
                choose(slice.ramp_block, 5);
            } else if (world_y > ramp_base_y &&
                       world_y <= ramp_base_y +
                                      connection.headroom) {
                choose(to_block_id(BlockType::Air), 3);
            }
            continue;
        }

        const auto broken_floor_limit =
            slice.broken
                ? step_block_y - 1
                : step_block_y;
        if (world_y >= lower_floor &&
            world_y <= broken_floor_limit) {
            choose(
                to_block_id(BlockType::BackroomsConnectorStep),
                4);
        } else if (world_y > broken_floor_limit &&
                   world_y <= step_block_y +
                                  connection.headroom) {
            choose(to_block_id(BlockType::Air), 3);
        }
    }
    return chosen;
}

auto BackroomsSpatialStack::needs_recessed_pool_shore(
    std::size_t layer_index,
    const BackroomsColumnSample& column,
    int world_x,
    int world_y,
    int world_z) const noexcept -> bool {
    if ((profile_ !=
             BackroomsSpatialProfile::RecessedPoolroomsV3 &&
         profile_ !=
             BackroomsSpatialProfile::FloodedPoolroomsV4) ||
        column.water_state == WaterState {0}) {
        return false;
    }
    const auto water_y =
        placements_[layer_index].floor_y +
        (column.water_top_y - kBackroomsFloorY);
    if (world_y != water_y) {
        return false;
    }

    constexpr std::array<PathPoint, 4U> cardinal_directions {{
        {1, 0},
        {-1, 0},
        {0, 1},
        {0, -1},
    }};
    for (const auto& direction : cardinal_directions) {
        const auto neighbour = connector_column(
            world_x + direction.x,
            world_z + direction.z);
        const auto override_block =
            connector_override(neighbour, world_y);
        if (override_block.has_value() &&
            !is_block_opaque(*override_block)) {
            return true;
        }
    }
    return false;
}

auto BackroomsSpatialStack::sample_block(
    int world_x,
    int world_y,
    int world_z) const noexcept -> BlockId {
    if (!is_world_y_valid(world_y)) {
        return to_block_id(BlockType::Air);
    }
    const auto connector =
        connector_column(world_x, world_z);
    if (const auto override_block =
            connector_override(connector, world_y);
        override_block.has_value()) {
        return *override_block;
    }

    const auto layer_index =
        layer_index_at_y(world_y);
    const auto column =
        generators_[layer_index].sample_column(
            world_x,
            world_z);
    if (needs_recessed_pool_shore(
            layer_index,
            column,
            world_x,
            world_y,
            world_z)) {
        return to_block_id(BlockType::PoolroomsMetal);
    }
    return layer_block(
        layer_index,
        column,
        world_y);
}

auto BackroomsSpatialStack::sample_water_state(
    int world_x,
    int world_y,
    int world_z) const noexcept -> WaterState {
    if (!is_world_y_valid(world_y)) {
        return 0;
    }
    const auto connector =
        connector_column(world_x, world_z);
    // Je n'assèche que la cellule réellement retaillée par la connexion.
    // Une galerie placée plusieurs étages plus haut ne doit jamais supprimer
    // l'eau d'un bassin des Poolrooms partageant seulement le même X/Z.
    if (connector_override(connector, world_y).has_value()) {
        return 0;
    }
    const auto layer_index =
        layer_index_at_y(world_y);
    const auto column =
        generators_[layer_index].sample_column(
            world_x,
            world_z);
    const auto water_bottom_y =
        placements_[layer_index].floor_y +
        (column.water_bottom_y - kBackroomsFloorY);
    const auto water_top_y =
        placements_[layer_index].floor_y +
        (column.water_top_y - kBackroomsFloorY);
    if (needs_recessed_pool_shore(
            layer_index,
            column,
            world_x,
            world_y,
            world_z)) {
        return WaterState {0};
    }
    return column.water_state != WaterState {0} &&
                   world_y >= water_bottom_y &&
                   world_y <= water_top_y
               ? column.water_state
               : WaterState {0};
}

auto BackroomsSpatialStack::rasterize_column(
    int world_x,
    int world_z) const noexcept -> BackroomsSpatialColumn {
    BackroomsSpatialColumn result {};
    result.blocks.fill(to_block_id(BlockType::Air));
    result.water.fill(WaterState {0});

    std::array<BackroomsColumnSample,
               kBackroomsSpatialLevelCount> columns {};
    for (std::size_t index = 0U;
         index < columns.size();
         ++index) {
        columns[index] =
            generators_[index].sample_column(
                world_x,
                world_z);
    }
    const auto connector =
        connector_column(world_x, world_z);
    std::array<bool, kBackroomsSpatialLevelCount>
        recessed_pool_shores {};
    const auto has_recessed_water =
        (profile_ ==
             BackroomsSpatialProfile::RecessedPoolroomsV3 ||
         profile_ ==
             BackroomsSpatialProfile::FloodedPoolroomsV4) &&
        std::any_of(
            columns.begin(),
            columns.end(),
            [](const auto& column) noexcept {
                return column.water_state != WaterState {0};
            });
    if (has_recessed_water) {
        constexpr std::array<PathPoint, 4U>
            cardinal_directions {{
                {1, 0},
                {-1, 0},
                {0, 1},
                {0, -1},
            }};
        std::array<ConnectorColumn,
                   cardinal_directions.size()> neighbours {};
        for (std::size_t direction_index = 0U;
             direction_index < cardinal_directions.size();
             ++direction_index) {
            neighbours[direction_index] = connector_column(
                world_x + cardinal_directions[direction_index].x,
                world_z + cardinal_directions[direction_index].z);
        }
        for (std::size_t layer_index = 0U;
             layer_index < columns.size();
             ++layer_index) {
            if (columns[layer_index].water_state ==
                WaterState {0}) {
                continue;
            }
            const auto water_y =
                placements_[layer_index].floor_y +
                (columns[layer_index].water_top_y -
                 kBackroomsFloorY);
            recessed_pool_shores[layer_index] =
                std::any_of(
                    neighbours.begin(),
                    neighbours.end(),
                    [this, water_y](
                        const auto& neighbour) noexcept {
                        const auto override_block =
                            connector_override(
                                neighbour,
                                water_y);
                        return override_block.has_value() &&
                               !is_block_opaque(
                                   *override_block);
                    });
        }
    }
    for (int world_y = kWorldMinY;
         world_y <= kWorldMaxY;
         ++world_y) {
        const auto layer_index =
            layer_index_at_y(world_y);
        auto block =
            layer_block(
                layer_index,
                columns[layer_index],
                world_y);
        const auto override_block =
            connector_override(
                connector,
                world_y);
        const auto connector_owns_cell =
            override_block.has_value();
        if (connector_owns_cell) {
            block = *override_block;
        } else if (recessed_pool_shores[layer_index]) {
            const auto water_y =
                placements_[layer_index].floor_y +
                (columns[layer_index].water_top_y -
                 kBackroomsFloorY);
            if (world_y == water_y) {
                block = to_block_id(BlockType::PoolroomsMetal);
            }
        }
        result.blocks[static_cast<std::size_t>(world_y)] =
            block;

        if (connector_owns_cell ||
            block != to_block_id(BlockType::Air)) {
            continue;
        }
        const auto water_bottom_y =
            placements_[layer_index].floor_y +
            (columns[layer_index].water_bottom_y -
             kBackroomsFloorY);
        const auto water_top_y =
            placements_[layer_index].floor_y +
            (columns[layer_index].water_top_y -
             kBackroomsFloorY);
        if (columns[layer_index].water_state != WaterState {0} &&
            world_y >= water_bottom_y &&
            world_y <= water_top_y) {
            result.water[static_cast<std::size_t>(world_y)] =
                columns[layer_index].water_state;
        }
    }
    return result;
}

auto BackroomsSpatialStack::has_player_clearance(
    int world_x,
    int feet_y,
    int world_z,
    int required_height) const noexcept -> bool {
    const auto safe_height = std::max(required_height, 1);
    for (int offset = 0;
         offset < safe_height;
         ++offset) {
        if (is_block_collidable(
                sample_block(
                    world_x,
                    feet_y + offset,
                    world_z))) {
            return false;
        }
    }
    return true;
}

auto BackroomsSpatialStack::has_body_clearance(
    float world_x,
    float feet_y,
    float world_z,
    int required_height,
    float half_width) const noexcept -> bool {
    constexpr auto collision_epsilon = 0.001F;
    const auto safe_half_width = std::max(half_width, 0.0F);
    const auto min_x = static_cast<int>(std::floor(
        world_x - safe_half_width));
    const auto max_x = static_cast<int>(std::floor(
        world_x + safe_half_width - collision_epsilon));
    const auto min_z = static_cast<int>(std::floor(
        world_z - safe_half_width));
    const auto max_z = static_cast<int>(std::floor(
        world_z + safe_half_width - collision_epsilon));
    const auto block_y = static_cast<int>(std::floor(feet_y));
    if (!is_world_y_valid(block_y)) {
        return false;
    }

    // Je valide chaque colonne traversée par l'AABB, avec la même marge
    // que la collision du joueur. Cette primitive sert aussi aux migrations.
    for (auto sample_z = min_z; sample_z <= max_z; ++sample_z) {
        for (auto sample_x = min_x; sample_x <= max_x; ++sample_x) {
            if (!has_player_clearance(
                    sample_x,
                    block_y,
                    sample_z,
                    required_height)) {
                return false;
            }
        }
    }
    return true;
}

auto backrooms_vertical_connection_style_name(
    BackroomsVerticalConnectionStyle style) noexcept -> const char* {
    switch (style) {
    case BackroomsVerticalConnectionStyle::ClassicStairs:
        return "classic_stairs";
    case BackroomsVerticalConnectionStyle::SpiralStairs:
        return "spiral_stairs";
    case BackroomsVerticalConnectionStyle::MonumentalStairs:
        return "monumental_stairs";
    case BackroomsVerticalConnectionStyle::NarrowStairs:
        return "narrow_stairs";
    case BackroomsVerticalConnectionStyle::BrokenStairs:
        return "broken_stairs";
    case BackroomsVerticalConnectionStyle::InclinedRamp:
        return "inclined_ramp";
    case BackroomsVerticalConnectionStyle::LongDescendingCorridor:
        return "long_descending_corridor";
    case BackroomsVerticalConnectionStyle::SlopedTunnel:
        return "sloped_tunnel";
    case BackroomsVerticalConnectionStyle::ScaleShiftPassage:
        return "scale_shift_passage";
    case BackroomsVerticalConnectionStyle::ChromaticAnomaly:
        return "chromatic_anomaly";
    default:
        return "unknown";
    }
}

} // namespace valcraft
