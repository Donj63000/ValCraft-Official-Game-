#include "gameplay/BackroomsMarlow.h"
#include "world/World.h"

#include <glm/geometric.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <queue>
#include <utility>
#include <vector>

namespace valcraft {

namespace {

constexpr float kPi = 3.14159265358979323846F;
constexpr float kMaximumAcceptedDeltaSeconds = 0.25F;
constexpr float kMarlowMaximumStepHeight = 1.05F;
constexpr float kMarlowPathNodeTolerance = 0.055F;
constexpr float kMarlowDeepWaterThreshold = 1.45F;
constexpr float kMarlowDrowningMinimumWaterDepth = 1.72F;
constexpr float kMarlowMinimumSafeWaterDepth = 0.25F;
constexpr float kMarlowGrabSeconds = 0.55F;
constexpr float kMarlowDrowningTargetTolerance = 0.45F;
constexpr float kMarlowDrowningVerticalTolerance = 0.12F;
constexpr float kMarlowPursuitRepathSeconds = 0.35F;
constexpr float kMarlowPursuitStuckTimeoutSeconds = 2.25F;
constexpr float kMarlowPursuitMinimumSeconds = 9.0F;
constexpr float kMarlowPursuitMaximumSeconds = 13.0F;
constexpr float kMarlowPursuitBaseSpeed = 2.65F;
constexpr float kMarlowPursuitStopDistance = 1.15F;
constexpr float kMarlowPursuitPressureSpeedBonus = 1.75F;
constexpr float kMarlowWaterAmbushSpeedBonus = 0.55F;
constexpr float kMarlowCornerWallOffset = 0.0F;
constexpr float kMarlowRenderFloorMargin = 0.02F;
constexpr float kMarlowPressureAttackTrigger = 0.55F;
constexpr float kMarlowPressureAttackReset = 0.25F;
constexpr float kMarlowPressureReactionMinimumSeconds = 3.5F;
constexpr float kMarlowPressureReactionMaximumSeconds = 6.0F;
constexpr std::int32_t kMinimumLogicalLevel = -1'000'000;
constexpr std::int32_t kMaximumLogicalLevel = 1'000'000;
constexpr std::uint32_t kFallbackRandomState = 0xD1B54A35U;

struct OpenNode {
    int index = -1;
    float f_cost = 0.0F;
    float h_cost = 0.0F;
};

struct OpenNodeLater {
    auto operator()(const OpenNode& left, const OpenNode& right) const noexcept
        -> bool {
        if (left.f_cost != right.f_cost) {
            return left.f_cost > right.f_cost;
        }
        if (left.h_cost != right.h_cost) {
            return left.h_cost > right.h_cost;
        }
        return left.index > right.index;
    }
};

struct SignalSelection {
    glm::vec3 position {0.0F};
    bool found = false;
};

struct OccluderSelection {
    BackroomsMarlowGridPoint offset {};
    float peek_side = 1.0F;
    bool found = false;
};

[[nodiscard]] auto finite_vector(const glm::vec3& value) noexcept -> bool {
    return std::isfinite(value.x) &&
           std::isfinite(value.y) &&
           std::isfinite(value.z);
}

[[nodiscard]] auto safe_vector(
    const glm::vec3& value,
    const glm::vec3& fallback) noexcept -> glm::vec3 {
    return finite_vector(value) ? value : fallback;
}

[[nodiscard]] auto clamp_finite(
    float value,
    float fallback,
    float minimum,
    float maximum) noexcept -> float {
    if (!std::isfinite(value)) {
        return fallback;
    }
    return std::clamp(value, minimum, maximum);
}

[[nodiscard]] auto safe_horizontal_direction(
    const glm::vec3& value,
    const glm::vec3& fallback) noexcept -> glm::vec3 {
    const auto safe = safe_vector(value, fallback);
    const glm::vec3 horizontal {safe.x, 0.0F, safe.z};
    const auto squared_length = glm::dot(horizontal, horizontal);
    if (!std::isfinite(squared_length) || squared_length <= 0.000001F) {
        return fallback;
    }
    return horizontal / std::sqrt(squared_length);
}

[[nodiscard]] auto horizontal_distance(
    const glm::vec3& first,
    const glm::vec3& second) noexcept -> float {
    const auto delta_x = first.x - second.x;
    const auto delta_z = first.z - second.z;
    return std::sqrt(delta_x * delta_x + delta_z * delta_z);
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

[[nodiscard]] auto safe_floor_to_int(float value) noexcept -> int {
    if (!std::isfinite(value)) {
        return 0;
    }
    const auto floored = std::floor(static_cast<double>(value));
    return static_cast<int>(std::clamp(
        floored,
        static_cast<double>(std::numeric_limits<int>::lowest()),
        static_cast<double>(std::numeric_limits<int>::max())));
}

[[nodiscard]] auto saturating_int(std::int64_t value) noexcept -> int {
    return static_cast<int>(std::clamp(
        value,
        static_cast<std::int64_t>(std::numeric_limits<int>::lowest()),
        static_cast<std::int64_t>(std::numeric_limits<int>::max())));
}

[[nodiscard]] auto translated_world_y(
    int local_y,
    int world_y_offset) noexcept -> int {
    return saturating_int(
        static_cast<std::int64_t>(local_y) +
        static_cast<std::int64_t>(world_y_offset));
}

[[nodiscard]] auto sample_spatial_block(
    const BackroomsGenerator& generator,
    const World* spatial_world,
    int spatial_world_y_offset,
    int world_x,
    int local_y,
    int world_z) -> BlockId {
    if (spatial_world != nullptr) {
        return spatial_world->peek_block_or_generated(
            world_x,
            translated_world_y(local_y, spatial_world_y_offset),
            world_z);
    }
    return generator.sample_block(world_x, local_y, world_z);
}

[[nodiscard]] auto sample_spatial_water_level(
    const BackroomsGenerator& generator,
    const World* spatial_world,
    int spatial_world_y_offset,
    int world_x,
    int local_y,
    int world_z) -> std::uint8_t {
    if (spatial_world != nullptr) {
        return spatial_world->peek_water_level_or_generated(
            world_x,
            translated_world_y(local_y, spatial_world_y_offset),
            world_z);
    }
    return water_level_from_state(
        generator.sample_water_state(world_x, local_y, world_z));
}

[[nodiscard]] constexpr auto local_index(int local_x, int local_z) noexcept
    -> std::size_t {
    return static_cast<std::size_t>(
        local_z * kBackroomsMarlowNavigationSide + local_x);
}

[[nodiscard]] auto navigation_grid_has_valid_shape(
    const BackroomsMarlowNavigationGrid& grid) noexcept -> bool {
    return grid.cells.size() == kBackroomsMarlowNavigationCellCount;
}

[[nodiscard]] auto grid_local_coordinates(
    const BackroomsMarlowNavigationGrid& grid,
    int world_x,
    int world_z,
    int& local_x,
    int& local_z) noexcept -> bool {
    const auto local_x64 =
        static_cast<std::int64_t>(world_x) -
        static_cast<std::int64_t>(grid.origin_world_x);
    const auto local_z64 =
        static_cast<std::int64_t>(world_z) -
        static_cast<std::int64_t>(grid.origin_world_z);
    if (local_x64 < 0 ||
        local_x64 >= kBackroomsMarlowNavigationSide ||
        local_z64 < 0 ||
        local_z64 >= kBackroomsMarlowNavigationSide) {
        return false;
    }
    local_x = static_cast<int>(local_x64);
    local_z = static_cast<int>(local_z64);
    return true;
}

[[nodiscard]] auto point_index(
    const BackroomsMarlowNavigationGrid& grid,
    BackroomsMarlowGridPoint point) noexcept -> int {
    if (!navigation_grid_has_valid_shape(grid)) {
        return -1;
    }
    auto local_x = 0;
    auto local_z = 0;
    if (!grid_local_coordinates(
            grid,
            point.x,
            point.z,
            local_x,
            local_z)) {
        return -1;
    }
    return local_z * kBackroomsMarlowNavigationSide + local_x;
}

[[nodiscard]] auto cell_world_point(
    const BackroomsMarlowNavigationGrid& grid,
    int index) noexcept -> BackroomsMarlowGridPoint {
    const auto local_z = index / kBackroomsMarlowNavigationSide;
    const auto local_x = index % kBackroomsMarlowNavigationSide;
    return {
        saturating_int(
            static_cast<std::int64_t>(grid.origin_world_x) + local_x),
        saturating_int(
            static_cast<std::int64_t>(grid.origin_world_z) + local_z),
    };
}

[[nodiscard]] auto cell_position(
    const BackroomsMarlowNavigationGrid& grid,
    BackroomsMarlowGridPoint point) noexcept -> glm::vec3 {
    const auto* cell = backrooms_marlow_navigation_cell(
        grid,
        point.x,
        point.z);
    const auto floor_y =
        cell != nullptr
            ? cell->floor_y
            : static_cast<float>(kBackroomsFloorY + 1);
    return {
        static_cast<float>(point.x) + 0.5F,
        floor_y + 0.001F,
        static_cast<float>(point.z) + 0.5F,
    };
}

[[nodiscard]] auto next_random(std::uint32_t& state) noexcept
    -> std::uint32_t {
    if (state == 0U) {
        state = kFallbackRandomState;
    }
    state ^= state << 13U;
    state ^= state >> 17U;
    state ^= state << 5U;
    return state;
}

[[nodiscard]] auto random_unit(std::uint32_t& state) noexcept -> float {
    const auto value = next_random(state) >> 8U;
    return static_cast<float>(value) * (1.0F / 16'777'216.0F);
}

[[nodiscard]] auto random_range(
    std::uint32_t& state,
    float minimum,
    float maximum) noexcept -> float {
    return minimum + (maximum - minimum) * random_unit(state);
}

[[nodiscard]] auto wrap_degrees(float degrees) noexcept -> float {
    if (!std::isfinite(degrees)) {
        return 0.0F;
    }
    auto wrapped = std::fmod(degrees + 180.0F, 360.0F);
    if (wrapped < 0.0F) {
        wrapped += 360.0F;
    }
    return wrapped - 180.0F;
}

[[nodiscard]] auto yaw_toward(
    const glm::vec3& from,
    const glm::vec3& to) noexcept -> float {
    const auto direction = safe_horizontal_direction(
        to - from,
        glm::vec3 {0.0F, 0.0F, -1.0F});
    return wrap_degrees(
        std::atan2(direction.x, -direction.z) * 180.0F / kPi);
}

[[nodiscard]] auto valid_mode(
    BackroomsMarlowEncounterMode mode) noexcept -> bool {
    switch (mode) {
    case BackroomsMarlowEncounterMode::CornerPeek:
    case BackroomsMarlowEncounterMode::Blocking:
    case BackroomsMarlowEncounterMode::WaterAmbush:
        return true;
    }
    return false;
}

[[nodiscard]] auto readiness_index(
    const BackroomsMarlowChunkReadiness& readiness,
    ChunkCoord chunk) noexcept -> int {
    const auto delta_x =
        static_cast<std::int64_t>(chunk.x) -
        static_cast<std::int64_t>(readiness.center_chunk.x);
    const auto delta_z =
        static_cast<std::int64_t>(chunk.z) -
        static_cast<std::int64_t>(readiness.center_chunk.z);
    if (delta_x < -kBackroomsMarlowReadinessChunkRadius ||
        delta_x > kBackroomsMarlowReadinessChunkRadius ||
        delta_z < -kBackroomsMarlowReadinessChunkRadius ||
        delta_z > kBackroomsMarlowReadinessChunkRadius) {
        return -1;
    }
    return static_cast<int>(
        (delta_z + kBackroomsMarlowReadinessChunkRadius) *
            kBackroomsMarlowReadinessChunkSide +
        delta_x + kBackroomsMarlowReadinessChunkRadius);
}

[[nodiscard]] auto chunk_ready(
    const BackroomsMarlowChunkReadiness& readiness,
    ChunkCoord chunk) noexcept -> bool {
    const auto index = readiness_index(readiness, chunk);
    return index >= 0 &&
           readiness.ready[static_cast<std::size_t>(index)];
}

[[nodiscard]] auto point_chunk(BackroomsMarlowGridPoint point) noexcept
    -> ChunkCoord {
    return {
        floor_division(point.x, kChunkSizeX),
        floor_division(point.z, kChunkSizeZ),
    };
}

[[nodiscard]] auto point_ready(
    const BackroomsMarlowChunkReadiness& readiness,
    BackroomsMarlowGridPoint point) noexcept -> bool {
    return chunk_ready(readiness, point_chunk(point));
}

[[nodiscard]] auto marlow_body_footprint_clear(
    const BackroomsMarlowNavigationGrid& grid,
    const BackroomsMarlowChunkReadiness* readiness,
    float center_x,
    float center_z,
    float reference_floor_y) noexcept -> bool {
    if (!std::isfinite(center_x) ||
        !std::isfinite(center_z) ||
        !std::isfinite(reference_floor_y) ||
        !navigation_grid_has_valid_shape(grid)) {
        return false;
    }

    const auto minimum_x_exact = std::floor(
        static_cast<double>(center_x) -
        static_cast<double>(kBackroomsMarlowRigVisualRadius));
    const auto maximum_x_exact = std::floor(
        static_cast<double>(center_x) +
        static_cast<double>(kBackroomsMarlowRigVisualRadius));
    const auto minimum_z_exact = std::floor(
        static_cast<double>(center_z) -
        static_cast<double>(kBackroomsMarlowRigVisualRadius));
    const auto maximum_z_exact = std::floor(
        static_cast<double>(center_z) +
        static_cast<double>(kBackroomsMarlowRigVisualRadius));
    const auto grid_minimum_x = static_cast<double>(grid.origin_world_x);
    const auto grid_minimum_z = static_cast<double>(grid.origin_world_z);
    const auto grid_maximum_x = grid_minimum_x +
        static_cast<double>(kBackroomsMarlowNavigationSide - 1);
    const auto grid_maximum_z = grid_minimum_z +
        static_cast<double>(kBackroomsMarlowNavigationSide - 1);
    if (minimum_x_exact < grid_minimum_x ||
        maximum_x_exact > grid_maximum_x ||
        minimum_z_exact < grid_minimum_z ||
        maximum_z_exact > grid_maximum_z) {
        return false;
    }

    const auto minimum_x = safe_floor_to_int(
        center_x - kBackroomsMarlowRigVisualRadius);
    const auto maximum_x = safe_floor_to_int(
        center_x + kBackroomsMarlowRigVisualRadius);
    const auto minimum_z = safe_floor_to_int(
        center_z - kBackroomsMarlowRigVisualRadius);
    const auto maximum_z = safe_floor_to_int(
        center_z + kBackroomsMarlowRigVisualRadius);
    constexpr auto kRadiusSquared =
        kBackroomsMarlowRigVisualRadius *
        kBackroomsMarlowRigVisualRadius;
    for (auto world_z64 = static_cast<std::int64_t>(minimum_z);
         world_z64 <= static_cast<std::int64_t>(maximum_z);
         ++world_z64) {
        const auto world_z = static_cast<int>(world_z64);
        for (auto world_x64 = static_cast<std::int64_t>(minimum_x);
             world_x64 <= static_cast<std::int64_t>(maximum_x);
             ++world_x64) {
            const auto world_x = static_cast<int>(world_x64);
            const auto nearest_x = std::clamp(
                center_x,
                static_cast<float>(world_x),
                static_cast<float>(world_x) + 1.0F);
            const auto nearest_z = std::clamp(
                center_z,
                static_cast<float>(world_z),
                static_cast<float>(world_z) + 1.0F);
            const auto delta_x = center_x - nearest_x;
            const auto delta_z = center_z - nearest_z;
            if (delta_x * delta_x + delta_z * delta_z > kRadiusSquared) {
                continue;
            }

            const BackroomsMarlowGridPoint sample {world_x, world_z};
            const auto* cell = backrooms_marlow_navigation_cell(
                grid,
                sample.x,
                sample.z);
            if (cell == nullptr ||
                !cell->walkable ||
                cell->clearance < kBackroomsMarlowRigStandingHeight ||
                (readiness != nullptr && !point_ready(*readiness, sample)) ||
                std::abs(cell->floor_y - reference_floor_y) >
                    kMarlowMaximumStepHeight) {
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] auto marlow_point_is_navigable(
    const BackroomsMarlowNavigationGrid& grid,
    const BackroomsMarlowChunkReadiness* readiness,
    BackroomsMarlowGridPoint point) noexcept -> bool {
    const auto* cell = backrooms_marlow_navigation_cell(
        grid,
        point.x,
        point.z);
    if (cell == nullptr ||
        !cell->walkable ||
        (readiness != nullptr && !point_ready(*readiness, point))) {
        return false;
    }
    return marlow_body_footprint_clear(
        grid,
        readiness,
        static_cast<float>(point.x) + 0.5F,
        static_cast<float>(point.z) + 0.5F,
        cell->floor_y);
}

[[nodiscard]] auto nearest_navigable_index(
    const BackroomsMarlowNavigationGrid& grid,
    const BackroomsMarlowChunkReadiness* readiness,
    BackroomsMarlowGridPoint point) noexcept -> int {
    const auto exact = point_index(grid, point);
    if (exact >= 0 && marlow_point_is_navigable(grid, readiness, point)) {
        return exact;
    }

    auto best_index = -1;
    auto best_distance = std::numeric_limits<std::int64_t>::max();
    for (std::size_t index = 0U; index < grid.cells.size(); ++index) {
        if (!grid.cells[index].walkable) {
            continue;
        }
        const auto candidate =
            cell_world_point(grid, static_cast<int>(index));
        if (!marlow_point_is_navigable(grid, readiness, candidate)) {
            continue;
        }
        const auto delta_x =
            static_cast<std::int64_t>(candidate.x) - point.x;
        const auto delta_z =
            static_cast<std::int64_t>(candidate.z) - point.z;
        const auto distance = delta_x * delta_x + delta_z * delta_z;
        if (distance < best_distance) {
            best_distance = distance;
            best_index = static_cast<int>(index);
        }
    }
    return best_index;
}

[[nodiscard]] auto marlow_transition_allowed(
    const BackroomsMarlowNavigationGrid& grid,
    const BackroomsMarlowChunkReadiness* readiness,
    BackroomsMarlowGridPoint from,
    BackroomsMarlowGridPoint to) noexcept -> bool {
    if (std::abs(from.x - to.x) + std::abs(from.z - to.z) != 1) {
        return false;
    }
    const auto* from_cell = backrooms_marlow_navigation_cell(
        grid,
        from.x,
        from.z);
    const auto* to_cell = backrooms_marlow_navigation_cell(
        grid,
        to.x,
        to.z);
    if (from_cell == nullptr ||
        to_cell == nullptr ||
        !marlow_point_is_navigable(grid, readiness, from) ||
        !marlow_point_is_navigable(grid, readiness, to) ||
        std::abs(from_cell->floor_y - to_cell->floor_y) >
            kMarlowMaximumStepHeight) {
        return false;
    }

    // Le milieu de l'arête est l'endroit où le centre de Marlow peut encore
    // être dans une cellule alors que son épaule est déjà dans la suivante.
    // Le vérifier supprime les traversées de murs et les couloirs trop étroits.
    const auto midpoint_x =
        (static_cast<float>(from.x + to.x) + 1.0F) * 0.5F;
    const auto midpoint_z =
        (static_cast<float>(from.z + to.z) + 1.0F) * 0.5F;
    const auto midpoint_floor =
        (from_cell->floor_y + to_cell->floor_y) * 0.5F;
    return marlow_body_footprint_clear(
        grid,
        readiness,
        midpoint_x,
        midpoint_z,
        midpoint_floor);
}

[[nodiscard]] auto inner_revisions_equal(
    const BackroomsMarlowChunkReadiness& left,
    const BackroomsMarlowChunkReadiness& right,
    ChunkCoord navigation_center) noexcept -> bool {
    for (auto delta_z = -kBackroomsMarlowNavigationChunkRadius;
         delta_z <= kBackroomsMarlowNavigationChunkRadius;
         ++delta_z) {
        for (auto delta_x = -kBackroomsMarlowNavigationChunkRadius;
             delta_x <= kBackroomsMarlowNavigationChunkRadius;
             ++delta_x) {
            const ChunkCoord chunk {
                navigation_center.x + delta_x,
                navigation_center.z + delta_z,
            };
            const auto left_index = readiness_index(left, chunk);
            const auto right_index = readiness_index(right, chunk);
            if (left_index < 0 || right_index < 0) {
                return false;
            }
            const auto left_offset = static_cast<std::size_t>(left_index);
            const auto right_offset = static_cast<std::size_t>(right_index);
            if (left.ready[left_offset] != right.ready[right_offset] ||
                left.mesh_revisions[left_offset] !=
                    right.mesh_revisions[right_offset]) {
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] auto has_detour_impl(
    const BackroomsMarlowNavigationGrid& grid,
    const BackroomsMarlowChunkReadiness* readiness,
    BackroomsMarlowGridPoint start,
    BackroomsMarlowGridPoint goal,
    BackroomsMarlowGridPoint blocked) noexcept -> bool {
    const auto start_index = point_index(grid, start);
    const auto goal_index = point_index(grid, goal);
    const auto blocked_index = point_index(grid, blocked);
    if (start_index < 0 || goal_index < 0 || blocked_index < 0 ||
        start_index == blocked_index || goal_index == blocked_index ||
        !marlow_point_is_navigable(grid, readiness, start) ||
        !marlow_point_is_navigable(grid, readiness, goal)) {
        return false;
    }
    std::array<bool, kBackroomsMarlowNavigationCellCount> visited {};
    std::array<int, kBackroomsMarlowNavigationCellCount> queue {};
    auto head = 0U;
    auto tail = 0U;
    queue[tail++] = start_index;
    visited[static_cast<std::size_t>(start_index)] = true;
    constexpr std::array<int, 4> delta_x {{1, -1, 0, 0}};
    constexpr std::array<int, 4> delta_z {{0, 0, 1, -1}};
    while (head < tail) {
        const auto current_index = queue[head++];
        if (current_index == goal_index) {
            return true;
        }
        const auto current = cell_world_point(grid, current_index);
        for (std::size_t direction = 0U;
             direction < delta_x.size();
             ++direction) {
            const BackroomsMarlowGridPoint neighbor {
                current.x + delta_x[direction],
                current.z + delta_z[direction],
            };
            const auto neighbor_index = point_index(grid, neighbor);
            if (neighbor_index < 0 ||
                neighbor_index == blocked_index ||
                !marlow_transition_allowed(
                    grid,
                    readiness,
                    current,
                    neighbor)) {
                continue;
            }
            const auto offset = static_cast<std::size_t>(neighbor_index);
            if (visited[offset]) {
                continue;
            }
            visited[offset] = true;
            queue[tail++] = neighbor_index;
        }
    }
    return false;
}

[[nodiscard]] auto movement_cost(
    const BackroomsMarlowNavigationCell& cell) noexcept -> float {
    // Je favorise l'eau et l'obscurite sans rendre le cout nul : l'A* reste
    // monotone et ne peut pas boucler sur une nappe inondee.
    auto cost = 1.0F;
    if (cell.has_water) {
        cost *= 0.72F;
    }
    if (cell.dark) {
        cost *= 0.86F;
    }
    if (cell.deep_water) {
        cost *= 0.90F;
    }
    if (cell.guaranteed_route) {
        cost *= 1.05F;
    }
    return std::max(cost, 0.50F);
}

[[nodiscard]] auto manhattan(
    BackroomsMarlowGridPoint first,
    BackroomsMarlowGridPoint second) noexcept -> float {
    return static_cast<float>(
        std::abs(first.x - second.x) +
        std::abs(first.z - second.z));
}

[[nodiscard]] auto has_adjacent_occluder(
    const BackroomsMarlowNavigationGrid& grid,
    BackroomsMarlowGridPoint point) noexcept -> bool {
    constexpr std::array<BackroomsMarlowGridPoint, 4> offsets {{
        {1, 0},
        {-1, 0},
        {0, 1},
        {0, -1},
    }};
    for (const auto offset : offsets) {
        const auto* neighbor = backrooms_marlow_navigation_cell(
            grid,
            point.x + offset.x,
            point.z + offset.z);
        if (neighbor != nullptr && !neighbor->walkable) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] auto select_adjacent_occluder(
    const BackroomsMarlowNavigationGrid& grid,
    BackroomsMarlowGridPoint point,
    const glm::vec3& toward_player) noexcept -> OccluderSelection {
    constexpr std::array<BackroomsMarlowGridPoint, 4> offsets {{
        {1, 0},
        {-1, 0},
        {0, 1},
        {0, -1},
    }};
    const auto forward = safe_horizontal_direction(
        toward_player,
        glm::vec3 {0.0F, 0.0F, -1.0F});
    const glm::vec3 right {-forward.z, 0.0F, forward.x};
    auto best_alignment = 0.0F;
    OccluderSelection selected {};
    for (const auto offset : offsets) {
        const auto* neighbor = backrooms_marlow_navigation_cell(
            grid,
            point.x + offset.x,
            point.z + offset.z);
        if (neighbor == nullptr || neighbor->walkable) {
            continue;
        }
        const glm::vec3 offset_direction {
            static_cast<float>(offset.x),
            0.0F,
            static_cast<float>(offset.z),
        };
        const auto alignment = glm::dot(right, offset_direction);
        if (std::abs(alignment) <= best_alignment) {
            continue;
        }
        best_alignment = std::abs(alignment);
        selected.offset = offset;
        selected.peek_side = alignment > 0.0F ? -1.0F : 1.0F;
        selected.found = true;
    }
    return selected;
}

[[nodiscard]] auto nearest_water_point(
    const BackroomsMarlowNavigationGrid& grid,
    const BackroomsMarlowChunkReadiness& readiness,
    BackroomsMarlowGridPoint origin,
    float maximum_distance,
    float minimum_water_depth) noexcept -> BackroomsMarlowGridPoint {
    auto best = BackroomsMarlowGridPoint {
        std::numeric_limits<int>::lowest(),
        std::numeric_limits<int>::lowest(),
    };
    auto best_distance = std::numeric_limits<float>::infinity();
    const auto radius = static_cast<int>(std::ceil(maximum_distance));
    for (auto delta_z = -radius; delta_z <= radius; ++delta_z) {
        for (auto delta_x = -radius; delta_x <= radius; ++delta_x) {
            const auto distance = std::sqrt(static_cast<float>(
                delta_x * delta_x + delta_z * delta_z));
            if (distance > maximum_distance || distance >= best_distance) {
                continue;
            }
            const BackroomsMarlowGridPoint candidate {
                origin.x + delta_x,
                origin.z + delta_z,
            };
            const auto* cell = backrooms_marlow_navigation_cell(
                grid,
                candidate.x,
                candidate.z);
            if (cell == nullptr ||
                !cell->walkable ||
                !cell->has_water ||
                cell->water_depth < minimum_water_depth ||
                !point_ready(readiness, candidate)) {
                continue;
            }
            best = candidate;
            best_distance = distance;
        }
    }
    return best;
}

[[nodiscard]] auto valid_grid_point(BackroomsMarlowGridPoint point) noexcept
    -> bool {
    return point.x != std::numeric_limits<int>::lowest() &&
           point.z != std::numeric_limits<int>::lowest();
}

[[nodiscard]] auto signal_position(
    const BackroomsMarlowNavigationGrid& grid,
    const BackroomsMarlowChunkReadiness& readiness,
    const BackroomsMarlowPlayerContext& player,
    std::uint32_t& random_state) noexcept -> SignalSelection {
    const BackroomsMarlowGridPoint player_point {
        safe_floor_to_int(player.feet_position.x),
        safe_floor_to_int(player.feet_position.z),
    };
    const auto look = safe_horizontal_direction(
        player.look_direction,
        glm::vec3 {0.0F, 0.0F, -1.0F});
    auto best_score = -std::numeric_limits<float>::infinity();
    auto best = player_point;
    auto found = false;
    for (std::size_t index = 0U; index < grid.cells.size(); ++index) {
        const auto& cell = grid.cells[index];
        if (!cell.walkable || !cell.has_water) {
            continue;
        }
        const auto point = cell_world_point(grid, static_cast<int>(index));
        if (!point_ready(readiness, point)) {
            continue;
        }
        const auto position = cell_position(grid, point);
        const auto distance = horizontal_distance(
            position,
            player.feet_position);
        if (distance < 8.0F || distance > 24.0F) {
            continue;
        }
        const auto direction = safe_horizontal_direction(
            position - player.feet_position,
            glm::vec3 {0.0F, 0.0F, 1.0F});
        const auto front_dot = glm::dot(look, direction);
        const auto score =
            (cell.dark ? 2.0F : 0.0F) -
            front_dot * 2.5F -
            std::abs(distance - 16.0F) * 0.05F +
            random_unit(random_state) * 0.10F;
        if (score > best_score) {
            best_score = score;
            best = point;
            found = true;
        }
    }
    return {cell_position(grid, best), found};
}

[[nodiscard]] auto pending_manifestation_is_valid(
    const BackroomsMarlowRuntime& runtime,
    const BackroomsMarlowChunkReadiness& readiness,
    const BackroomsMarlowPlayerContext& player) noexcept -> bool {
    const auto& pending = runtime.pending_manifestation;
    if (!pending.found || !finite_vector(pending.position)) {
        return false;
    }
    const BackroomsMarlowGridPoint point {
        safe_floor_to_int(pending.position.x),
        safe_floor_to_int(pending.position.z),
    };
    const auto* cell = backrooms_marlow_navigation_cell(
        runtime.navigation,
        point.x,
        point.z);
    const auto distance = horizontal_distance(
        pending.position,
        player.feet_position);
    const auto corner_peek_clear =
        pending.mode == BackroomsMarlowEncounterMode::CornerPeek &&
        cell != nullptr &&
        cell->walkable &&
        cell->clearance >= kBackroomsMarlowRigStandingHeight &&
        point_ready(readiness, point);
    if (cell == nullptr ||
        (!corner_peek_clear &&
         !marlow_body_footprint_clear(
             runtime.navigation,
             &readiness,
             pending.position.x,
             pending.position.z,
             cell->floor_y)) ||
        !std::isfinite(distance) ||
        distance < 6.0F ||
        distance > 32.0F) {
        return false;
    }
    if (pending.mode == BackroomsMarlowEncounterMode::CornerPeek) {
        return has_adjacent_occluder(runtime.navigation, point);
    }
    if (!finite_vector(pending.buoy_position)) {
        return false;
    }
    const BackroomsMarlowGridPoint buoy_point {
        safe_floor_to_int(pending.buoy_position.x),
        safe_floor_to_int(pending.buoy_position.z),
    };
    const auto* buoy_cell = backrooms_marlow_navigation_cell(
        runtime.navigation,
        buoy_point.x,
        buoy_point.z);
    return buoy_cell != nullptr &&
           buoy_cell->walkable &&
           buoy_cell->has_water &&
           point_ready(readiness, buoy_point);
}

[[nodiscard]] auto next_cue_delay(
    std::uint32_t& random_state,
    float pressure) noexcept -> float {
    const auto safe_pressure = std::clamp(pressure, 0.0F, 1.0F);
    const auto minimum = 8.0F + (3.0F - 8.0F) * safe_pressure;
    const auto maximum = 16.0F + (7.0F - 16.0F) * safe_pressure;
    return random_range(random_state, minimum, maximum);
}

[[nodiscard]] auto next_manifestation_delay(
    std::uint32_t& random_state,
    float pressure) noexcept -> float {
    const auto base = random_range(random_state, 24.0F, 42.0F);
    const auto multiplier = 1.0F - 0.30F * std::clamp(pressure, 0.0F, 1.0F);
    return base * multiplier;
}

void emit_event(
    BackroomsMarlowUpdateResult& result,
    BackroomsMarlowState& state,
    BackroomsMarlowEventKind kind,
    const glm::vec3& position) noexcept {
    if (result.event_count >= result.events.size()) {
        return;
    }
    result.events[result.event_count++] = {
        kind,
        safe_vector(position, glm::vec3 {0.0F}),
        state.next_event_sequence,
    };
    ++state.next_event_sequence;
    if (state.next_event_sequence == 0U) {
        state.next_event_sequence = 1U;
    }
}

void reset_runtime_for_level(
    BackroomsMarlowRuntime& runtime,
    float durable_pressure,
    bool apply_initial_grace) noexcept {
    // Je conserve les buffers deja payes : quitter les Poolrooms ne doit pas
    // provoquer une liberation puis une reallocation de 6 400 cellules.
    auto navigation_cells = std::move(runtime.navigation.cells);
    auto path_nodes = std::move(runtime.path.nodes);
    runtime = {};
    runtime.navigation.cells = std::move(navigation_cells);
    runtime.navigation.cells.clear();
    runtime.path.nodes = std::move(path_nodes);
    runtime.path.clear();
    runtime.grace_seconds = apply_initial_grace
        ? kBackroomsMarlowInitialGraceSeconds
        : 0.0F;
    runtime.pressure_attack_armed =
        durable_pressure < kMarlowPressureAttackTrigger;
    runtime.pressure_hysteresis_initialized = true;
}

void enter_phase(
    BackroomsMarlowRuntime& runtime,
    BackroomsMarlowPhase phase,
    float duration) noexcept {
    runtime.phase = phase;
    runtime.phase_seconds = 0.0F;
    runtime.phase_duration_seconds = std::max(0.0F, duration);
}

void begin_pending_manifestation(
    BackroomsMarlowState& state,
    BackroomsMarlowRuntime& runtime,
    BackroomsMarlowUpdateResult& result) noexcept {
    const auto selection = runtime.pending_manifestation;
    runtime.position = selection.position;
    runtime.buoy_position = selection.buoy_position;
    runtime.body_yaw_degrees = selection.body_yaw_degrees;
    runtime.peek_side = selection.peek_side;
    runtime.waiting_for_threat_slot = false;
    runtime.capture_event_emitted = false;
    runtime.kill_event_emitted = false;
    runtime.capture_transport_blocked = false;
    runtime.pursuit_stuck_seconds = 0.0F;
    runtime.path.clear();
    runtime.has_path_target = false;
    runtime.pursuit_repath_seconds = 0.0F;
    state.last_mode = selection.mode;
    state.has_last_mode = true;

    if (selection.mode == BackroomsMarlowEncounterMode::CornerPeek) {
        runtime.buoy_warning_active = false;
        enter_phase(
            runtime,
            BackroomsMarlowPhase::CornerPeek,
            random_range(state.random_state, 3.0F, 6.0F));
        emit_event(
            result,
            state,
            BackroomsMarlowEventKind::Surfaced,
            runtime.position);
        return;
    }

    runtime.buoy_warning_active = true;
    enter_phase(
        runtime,
        BackroomsMarlowPhase::Signaling,
        random_range(state.random_state, 1.2F, 2.2F));
    emit_event(
        result,
        state,
        BackroomsMarlowEventKind::BuoyAppeared,
        runtime.buoy_position);
}

void finish_manifestation(
    BackroomsMarlowState& state,
    BackroomsMarlowRuntime& runtime,
    BackroomsMarlowUpdateResult& result,
    bool threat_slot_owned) noexcept {
    emit_event(
        result,
        state,
        BackroomsMarlowEventKind::Vanished,
        runtime.position);
    runtime.buoy_warning_active = false;
    runtime.pending_manifestation = {};
    runtime.waiting_for_threat_slot = false;
    state.cooldown_seconds =
        random_range(state.random_state, 12.0F, 17.0F);
    state.manifestation_seconds =
        next_manifestation_delay(state.random_state, state.pressure);
    runtime.path.clear();
    runtime.has_path_target = false;
    runtime.pursuit_repath_seconds = 0.0F;
    runtime.pursuit_stuck_seconds = 0.0F;
    runtime.capture_transport_blocked = false;
    enter_phase(
        runtime,
        BackroomsMarlowPhase::Cooldown,
        state.cooldown_seconds);
    result.releases_threat_slot = threat_slot_owned;
}

[[nodiscard]] auto make_result_views(
    const BackroomsMarlowState& state,
    const BackroomsMarlowRuntime& runtime,
    bool threat_slot_owned) noexcept -> BackroomsMarlowUpdateResult {
    BackroomsMarlowUpdateResult result {};
    const auto phase = runtime.phase;
    const auto visible =
        phase == BackroomsMarlowPhase::CornerPeek ||
        phase == BackroomsMarlowPhase::Emerging ||
        phase == BackroomsMarlowPhase::Blocking ||
        phase == BackroomsMarlowPhase::Submerging ||
        phase == BackroomsMarlowPhase::Dragging ||
        phase == BackroomsMarlowPhase::Drowning ||
        phase == BackroomsMarlowPhase::Screamer;
    const auto duration = std::max(runtime.phase_duration_seconds, 0.001F);
    const auto progress = std::clamp(
        runtime.phase_seconds / duration,
        0.0F,
        1.0F);
    auto reveal = visible ? 1.0F : 0.0F;
    if (phase == BackroomsMarlowPhase::Emerging) {
        // La durée d'émergence ne dépend pas de la durée totale de chasse.
        reveal = std::clamp(runtime.phase_seconds / 1.10F, 0.0F, 1.0F);
    } else if (phase == BackroomsMarlowPhase::Submerging) {
        reveal = 1.0F - progress;
    }
    auto immersion = 0.55F;
    if (phase == BackroomsMarlowPhase::CornerPeek) {
        // Je ne laisse depasser que le haut du crane et les grands yeux : le
        // mur et l'eau cachent le reste du corps pendant l'observation.
        immersion = 0.90F;
    } else if (phase == BackroomsMarlowPhase::Dragging ||
               phase == BackroomsMarlowPhase::Drowning ||
               phase == BackroomsMarlowPhase::Screamer) {
        immersion = 0.20F;
    }

    auto render_position = safe_vector(
        runtime.position,
        glm::vec3 {
            0.5F,
            static_cast<float>(kBackroomsFloorY + 1),
            0.5F,
        });
    auto available_submersion_depth = 0.0F;
    const BackroomsMarlowGridPoint render_point {
        safe_floor_to_int(render_position.x),
        safe_floor_to_int(render_position.z),
    };
    const auto* render_cell = backrooms_marlow_navigation_cell(
        runtime.navigation,
        render_point.x,
        render_point.z);
    if (render_cell != nullptr &&
        std::isfinite(render_cell->floor_y) &&
        std::isfinite(render_cell->water_surface_y)) {
        const auto floor_anchor =
            render_cell->floor_y + kMarlowRenderFloorMargin;
        render_position.y = floor_anchor;
        if (render_cell->has_water &&
            render_cell->water_surface_y > floor_anchor) {
            // L'ancre monte à la surface, mais l'enfoncement disponible reste
            // borné avant le plancher. Même une piscine d'un bloc ne peut donc
            // plus avaler les jambes sous la géométrie solide.
            render_position.y = render_cell->water_surface_y;
            available_submersion_depth = std::max(
                render_position.y -
                    render_cell->floor_y -
                    kMarlowRenderFloorMargin,
                0.0F);
        }
    }
    result.render = {
        render_position,
        runtime.body_yaw_degrees,
        immersion,
        available_submersion_depth,
        reveal,
        runtime.peek_side,
        phase,
        visible,
        phase == BackroomsMarlowPhase::CornerPeek
            ? BackroomsMarlowPresentation::HeadOnlyPeek
            : phase == BackroomsMarlowPhase::Emerging ||
                      phase == BackroomsMarlowPhase::Submerging
                ? BackroomsMarlowPresentation::ProgressiveReveal
                : BackroomsMarlowPresentation::FullBody,
        safe_vector(
            runtime.pending_manifestation.wall_normal,
            glm::vec3 {0.0F}),
    };
    result.buoy = {
        runtime.buoy_position,
        phase == BackroomsMarlowPhase::Signaling
            ? progress
            : runtime.buoy_warning_active ? 1.0F : 0.0F,
        runtime.buoy_warning_active,
    };
    const auto interference_active =
        phase == BackroomsMarlowPhase::Signaling || visible;
    result.interference = {
        render_position,
        interference_active ? 18.0F : 0.0F,
        interference_active
            ? std::clamp(0.35F + state.pressure * 0.65F, 0.0F, 1.0F)
            : 0.0F,
        interference_active,
    };
    const auto capture_active =
        phase == BackroomsMarlowPhase::Dragging ||
        phase == BackroomsMarlowPhase::Drowning ||
        phase == BackroomsMarlowPhase::Screamer;
    result.capture = {
        runtime.capture_target,
        phase == BackroomsMarlowPhase::Dragging ? progress :
            capture_active ? 1.0F : 0.0F,
        phase == BackroomsMarlowPhase::Drowning ? progress :
            phase == BackroomsMarlowPhase::Screamer ? 1.0F : 0.0F,
        capture_active,
        capture_active,
    };
    result.holds_threat_slot =
        threat_slot_owned &&
        phase != BackroomsMarlowPhase::Dormant &&
        phase != BackroomsMarlowPhase::Cooldown;
    return result;
}

} // namespace

void reset_backrooms_marlow_runtime(
    BackroomsMarlowRuntime& runtime,
    float durable_pressure,
    bool apply_initial_grace) noexcept {
    reset_runtime_for_level(
        runtime,
        clamp_finite(
            durable_pressure,
            0.0F,
            0.0F,
            kBackroomsMarlowMaximumPressure),
        apply_initial_grace);
}

auto initialize_backrooms_marlow(
    std::uint32_t seed,
    std::int32_t logical_level) noexcept -> BackroomsMarlowState {
    BackroomsMarlowState state {};
    state.logical_level = static_cast<std::int32_t>(std::clamp<std::int64_t>(
        logical_level,
        kMinimumLogicalLevel,
        kMaximumLogicalLevel));
    state.random_state = seed == 0U ? kFallbackRandomState : seed;
    state.cue_seconds = random_range(state.random_state, 8.0F, 14.0F);
    state.manifestation_seconds =
        random_range(state.random_state, 25.0F, 40.0F);
    state.next_event_sequence = 1U;
    state.initialized = true;
    return state;
}

auto sanitize_backrooms_marlow_state(
    const BackroomsMarlowState& state) noexcept -> BackroomsMarlowState {
    auto sanitized = state;
    sanitized.pressure = clamp_finite(
        sanitized.pressure,
        0.0F,
        0.0F,
        kBackroomsMarlowMaximumPressure);
    sanitized.cue_seconds = clamp_finite(
        sanitized.cue_seconds,
        8.0F,
        0.0F,
        60.0F);
    sanitized.manifestation_seconds = clamp_finite(
        sanitized.manifestation_seconds,
        30.0F,
        0.0F,
        60.0F);
    sanitized.cooldown_seconds = clamp_finite(
        sanitized.cooldown_seconds,
        0.0F,
        0.0F,
        24.0F);
    sanitized.logical_level = static_cast<std::int32_t>(
        std::clamp<std::int64_t>(
            sanitized.logical_level,
            kMinimumLogicalLevel,
            kMaximumLogicalLevel));
    if (!sanitized.has_last_mode || !valid_mode(sanitized.last_mode)) {
        sanitized.last_mode = BackroomsMarlowEncounterMode::CornerPeek;
        if (!valid_mode(state.last_mode)) {
            sanitized.has_last_mode = false;
        }
    }
    if (sanitized.random_state == 0U) {
        sanitized.random_state = kFallbackRandomState;
    }
    if (sanitized.next_event_sequence == 0U) {
        sanitized.next_event_sequence = 1U;
    }
    return sanitized;
}

auto prepare_backrooms_marlow_for_persistence(
    const BackroomsMarlowState& state,
    const BackroomsMarlowRuntime& runtime) noexcept -> BackroomsMarlowState {
    auto prepared = sanitize_backrooms_marlow_state(state);
    const auto manifestation_active =
        runtime.waiting_for_threat_slot ||
        (runtime.phase != BackroomsMarlowPhase::Dormant &&
         runtime.phase != BackroomsMarlowPhase::Cooldown);
    if (!manifestation_active) {
        return prepared;
    }

    // Je transforme toute cinematique en sa sortie durable normale. Le meme
    // etat et le meme RNG produisent donc exactement le meme cooldown au save,
    // sans restaurer une saisie ou une noyade a moitie jouee.
    prepared.cooldown_seconds = random_range(
        prepared.random_state,
        12.0F,
        17.0F);
    prepared.manifestation_seconds = next_manifestation_delay(
        prepared.random_state,
        prepared.pressure);
    return prepared;
}

auto evaluate_backrooms_marlow_pressure(
    float pressure,
    float quiet_seconds,
    const BackroomsMarlowPlayerContext& player,
    bool flashlight_water_started,
    float dt) noexcept -> BackroomsMarlowPressureResult {
    pressure = clamp_finite(pressure, 0.0F, 0.0F, 1.0F);
    quiet_seconds = clamp_finite(
        quiet_seconds,
        0.0F,
        0.0F,
        3'600.0F);
    const auto safe_dt = clamp_finite(
        dt,
        0.0F,
        0.0F,
        kMaximumAcceptedDeltaSeconds);
    const auto distance = clamp_finite(
        player.travelled_horizontal_distance,
        0.0F,
        0.0F,
        4.0F);

    auto gain = 0.0F;
    if (!player.motion_is_forced) {
        if (player.sprinting && !player.in_water) {
            gain += 0.12F * safe_dt;
        }
        if (player.in_water) {
            gain += 0.10F * distance;
            if (player.sprinting) {
                gain += 0.18F * safe_dt;
            }
        }
        if (player.entered_water) {
            gain += 0.22F;
        }
        if (player.jumped) {
            gain += 0.10F;
        }
        if (player.landed_in_water) {
            gain += 0.28F;
        }
    }
    if (flashlight_water_started) {
        gain += 0.08F;
    }
    if (player.flashlight_on_water) {
        gain += 0.16F * safe_dt;
    }

    if (gain > 0.0F) {
        quiet_seconds = 0.0F;
        pressure += gain;
    } else {
        const auto previous_decay_seconds =
            std::max(quiet_seconds - 1.5F, 0.0F);
        quiet_seconds = std::min(quiet_seconds + safe_dt, 3'600.0F);
        const auto current_decay_seconds =
            std::max(quiet_seconds - 1.5F, 0.0F);
        pressure -=
            (current_decay_seconds - previous_decay_seconds) * 0.055F;
    }

    return {
        std::clamp(pressure, 0.0F, 1.0F),
        quiet_seconds,
    };
}

auto select_backrooms_marlow_mode(
    std::uint32_t random_state,
    float pressure,
    bool has_previous_mode,
    BackroomsMarlowEncounterMode previous_mode) noexcept
    -> BackroomsMarlowModeSelection {
    if (random_state == 0U) {
        random_state = kFallbackRandomState;
    }
    pressure = clamp_finite(pressure, 0.0F, 0.0F, 1.0F);
    auto mode = BackroomsMarlowEncounterMode::CornerPeek;
    if (pressure >= 0.35F) {
        const std::array<float, 3> weights =
            pressure < 0.82F
                ? std::array<float, 3> {{0.20F, 0.65F, 0.15F}}
                : std::array<float, 3> {{0.08F, 0.47F, 0.45F}};
        const auto previous_index = static_cast<std::size_t>(previous_mode);
        if (has_previous_mode &&
            valid_mode(previous_mode) &&
            previous_index < weights.size() &&
            weights[previous_index] > 0.0F) {
            // Je fais une transition de Metropolis-Hastings : je reduis les
            // repetitions tout en conservant exactement les poids cibles a
            // long terme, contrairement a un simple reroll biaise.
            std::array<std::size_t, 2> alternatives {};
            std::size_t alternative_count = 0U;
            for (std::size_t index = 0U; index < weights.size(); ++index) {
                if (index != previous_index && weights[index] > 0.0F) {
                    alternatives[alternative_count++] = index;
                }
            }
            if (alternative_count > 0U) {
                const auto choice = std::min(
                    static_cast<std::size_t>(
                        random_unit(random_state) *
                        static_cast<float>(alternative_count)),
                    alternative_count - 1U);
                const auto candidate_index = alternatives[choice];
                const auto acceptance = std::min(
                    1.0F,
                    weights[candidate_index] / weights[previous_index]);
                const auto selected_index =
                    random_unit(random_state) < acceptance
                        ? candidate_index
                        : previous_index;
                mode = static_cast<BackroomsMarlowEncounterMode>(
                    selected_index);
            }
        } else {
            const auto roll = random_unit(random_state);
            mode = roll < weights[0]
                ? BackroomsMarlowEncounterMode::CornerPeek
                : roll < weights[0] + weights[1]
                    ? BackroomsMarlowEncounterMode::Blocking
                    : BackroomsMarlowEncounterMode::WaterAmbush;
        }
    }
    return {mode, random_state};
}

auto backrooms_marlow_chunk_at(
    const glm::vec3& position) noexcept -> ChunkCoord {
    return {
        floor_division(safe_floor_to_int(position.x), kChunkSizeX),
        floor_division(safe_floor_to_int(position.z), kChunkSizeZ),
    };
}

auto backrooms_marlow_navigation_cell(
    const BackroomsMarlowNavigationGrid& grid,
    int world_x,
    int world_z) noexcept -> const BackroomsMarlowNavigationCell* {
    if (!navigation_grid_has_valid_shape(grid)) {
        return nullptr;
    }
    auto local_x = 0;
    auto local_z = 0;
    if (!grid_local_coordinates(
            grid,
            world_x,
            world_z,
            local_x,
            local_z)) {
        return nullptr;
    }
    return &grid.cells[local_index(local_x, local_z)];
}

namespace {

void rebuild_backrooms_marlow_navigation_grid(
    BackroomsMarlowNavigationGrid& grid,
    const BackroomsGenerator& generator,
    const ChunkCoord& center_chunk,
    const World* spatial_world,
    int spatial_world_y_offset) {
    // Je redimensionne le buffer existant : apres un reset, sa capacite de
    // 6 400 cellules est reutilisee sans nouvelle allocation.
    grid.cells.resize(kBackroomsMarlowNavigationCellCount);
    grid.center_chunk = center_chunk;
    grid.logical_level = static_cast<std::int32_t>(std::clamp<std::int64_t>(
        generator.logical_level(),
        kMinimumLogicalLevel,
        kMaximumLogicalLevel));
    grid.origin_world_x = saturating_int(
        (static_cast<std::int64_t>(center_chunk.x) -
         kBackroomsMarlowNavigationChunkRadius) *
        kChunkSizeX);
    grid.origin_world_z = saturating_int(
        (static_cast<std::int64_t>(center_chunk.z) -
         kBackroomsMarlowNavigationChunkRadius) *
        kChunkSizeZ);

    for (auto local_z = 0;
         local_z < kBackroomsMarlowNavigationSide;
         ++local_z) {
        for (auto local_x = 0;
             local_x < kBackroomsMarlowNavigationSide;
             ++local_x) {
            const auto world_x = saturating_int(
                static_cast<std::int64_t>(grid.origin_world_x) + local_x);
            const auto world_z = saturating_int(
                static_cast<std::int64_t>(grid.origin_world_z) + local_z);
            const auto column = generator.sample_column(world_x, world_z);
            const auto floor_y = static_cast<float>(column.floor_y + 1);
            const auto clearance = static_cast<float>(
                column.ceiling_y - column.floor_y - 1);
            auto walkable =
                generator.is_walkable(world_x, world_z) &&
                clearance >= kBackroomsMarlowRigStandingHeight;
            if (walkable && spatial_world != nullptr) {
                walkable = is_block_collidable(sample_spatial_block(
                    generator,
                    spatial_world,
                    spatial_world_y_offset,
                    world_x,
                    column.floor_y,
                    world_z));
                const auto occupied_top_y =
                    column.floor_y +
                    static_cast<int>(
                        std::ceil(kBackroomsMarlowRigStandingHeight));
                for (auto local_y = column.floor_y + 1;
                     walkable && local_y <= occupied_top_y;
                     ++local_y) {
                    walkable = !is_block_collidable(sample_spatial_block(
                        generator,
                        spatial_world,
                        spatial_world_y_offset,
                        world_x,
                        local_y,
                        world_z));
                }
            }

            auto has_water = false;
            auto water_surface_y = floor_y;
            const auto generated_interval_valid =
                column.water_bottom_y >= column.floor_y + 1 &&
                column.water_top_y >= column.water_bottom_y &&
                column.water_top_y < column.ceiling_y;
            const auto water_scan_bottom = generated_interval_valid
                ? column.water_bottom_y
                : column.floor_y + 1;
            const auto water_scan_top = generated_interval_valid
                ? column.water_top_y
                : std::min(column.floor_y + 3, column.ceiling_y - 1);
            for (auto local_y = water_scan_bottom;
                 local_y <= water_scan_top;
                 ++local_y) {
                const auto level = sample_spatial_water_level(
                    generator,
                    spatial_world,
                    spatial_world_y_offset,
                    world_x,
                    local_y,
                    world_z);
                if (level == 0U) {
                    continue;
                }
                has_water = true;
                water_surface_y = std::max(
                    water_surface_y,
                    static_cast<float>(local_y) +
                        static_cast<float>(level) /
                            static_cast<float>(kMaxWaterLevel));
            }
            const auto water_depth =
                std::max(water_surface_y - floor_y, 0.0F);
            const auto descriptor = generator.descriptor_at(world_x, world_z);
            const auto dark =
                descriptor.tension == BackroomsTension::Blackout ||
                column.light_state == BackroomsLightState::None ||
                column.light_state == BackroomsLightState::Failed;
            grid.cells[local_index(local_x, local_z)] = {
                floor_y,
                water_surface_y,
                water_depth,
                clearance,
                walkable,
                has_water,
                water_depth >= kMarlowDeepWaterThreshold,
                column.guaranteed_route,
                dark,
            };
        }
    }
}

} // namespace

auto build_backrooms_marlow_navigation_grid(
    const BackroomsGenerator& generator,
    const ChunkCoord& center_chunk,
    const World* spatial_world,
    int spatial_world_y_offset)
    -> BackroomsMarlowNavigationGrid {
    BackroomsMarlowNavigationGrid grid {};
    rebuild_backrooms_marlow_navigation_grid(
        grid,
        generator,
        center_chunk,
        spatial_world,
        spatial_world_y_offset);
    return grid;
}

namespace {

[[nodiscard]] auto find_backrooms_marlow_path_impl(
    const BackroomsMarlowNavigationGrid& grid,
    const BackroomsMarlowChunkReadiness* readiness,
    BackroomsMarlowGridPoint start,
    BackroomsMarlowGridPoint goal) -> BackroomsMarlowPath {
    BackroomsMarlowPath path {};
    const auto start_index = nearest_navigable_index(
        grid,
        readiness,
        start);
    const auto goal_index = nearest_navigable_index(
        grid,
        readiness,
        goal);
    if (start_index < 0 || goal_index < 0) {
        return path;
    }
    if (start_index == goal_index) {
        path.nodes.push_back(cell_world_point(grid, start_index));
        return path;
    }

    std::array<float, kBackroomsMarlowNavigationCellCount> costs {};
    costs.fill(std::numeric_limits<float>::infinity());
    std::array<int, kBackroomsMarlowNavigationCellCount> parents {};
    parents.fill(-1);
    std::array<bool, kBackroomsMarlowNavigationCellCount> closed {};
    std::priority_queue<
        OpenNode,
        std::vector<OpenNode>,
        OpenNodeLater> open {};
    const auto resolved_goal = cell_world_point(grid, goal_index);
    costs[static_cast<std::size_t>(start_index)] = 0.0F;
    const auto start_h =
        manhattan(cell_world_point(grid, start_index), resolved_goal) * 0.50F;
    open.push({start_index, start_h, start_h});
    constexpr std::array<int, 4> kDeltaX {{1, -1, 0, 0}};
    constexpr std::array<int, 4> kDeltaZ {{0, 0, 1, -1}};

    while (!open.empty()) {
        const auto current = open.top();
        open.pop();
        const auto current_offset = static_cast<std::size_t>(current.index);
        if (closed[current_offset]) {
            continue;
        }
        closed[current_offset] = true;
        if (current.index == goal_index) {
            break;
        }

        const auto current_point = cell_world_point(grid, current.index);
        const auto& current_cell = grid.cells[current_offset];
        for (std::size_t direction = 0U;
             direction < kDeltaX.size();
             ++direction) {
            const BackroomsMarlowGridPoint neighbor_point {
                current_point.x + kDeltaX[direction],
                current_point.z + kDeltaZ[direction],
            };
            const auto neighbor_index = point_index(grid, neighbor_point);
            if (neighbor_index < 0 ||
                !marlow_transition_allowed(
                    grid,
                    readiness,
                    current_point,
                    neighbor_point)) {
                continue;
            }
            const auto neighbor_offset =
                static_cast<std::size_t>(neighbor_index);
            if (closed[neighbor_offset]) {
                continue;
            }

            const auto& neighbor = grid.cells[neighbor_offset];
            const auto vertical_penalty =
                std::abs(neighbor.floor_y - current_cell.floor_y) * 0.35F;
            const auto candidate_cost =
                costs[current_offset] +
                movement_cost(neighbor) +
                vertical_penalty;
            if (candidate_cost >= costs[neighbor_offset]) {
                continue;
            }
            costs[neighbor_offset] = candidate_cost;
            parents[neighbor_offset] = current.index;
            const auto heuristic =
                manhattan(neighbor_point, resolved_goal) * 0.50F;
            open.push({
                neighbor_index,
                candidate_cost + heuristic,
                heuristic,
            });
        }
    }

    if (parents[static_cast<std::size_t>(goal_index)] < 0) {
        return path;
    }
    std::array<int, kBackroomsMarlowNavigationCellCount> reverse {};
    auto reverse_count = 0U;
    auto cursor = goal_index;
    while (cursor >= 0 && reverse_count < reverse.size()) {
        reverse[reverse_count++] = cursor;
        if (cursor == start_index) {
            break;
        }
        cursor = parents[static_cast<std::size_t>(cursor)];
    }
    if (reverse_count == 0U ||
        reverse[reverse_count - 1U] != start_index) {
        return {};
    }

    path.nodes.resize(reverse_count);
    for (std::size_t index = 0U; index < reverse_count; ++index) {
        path.nodes[index] = cell_world_point(
            grid,
            reverse[reverse_count - index - 1U]);
    }
    return path;
}

void invalidate_marlow_pursuit_path(
    BackroomsMarlowRuntime& runtime) noexcept {
    runtime.path.clear();
    runtime.has_path_target = false;
    runtime.pursuit_repath_seconds = 0.0F;
}

void refresh_marlow_pursuit_path(
    BackroomsMarlowRuntime& runtime,
    const BackroomsMarlowChunkReadiness& readiness,
    const glm::vec3& player_position) {
    const BackroomsMarlowGridPoint current {
        safe_floor_to_int(runtime.position.x),
        safe_floor_to_int(runtime.position.z),
    };
    const BackroomsMarlowGridPoint target {
        safe_floor_to_int(player_position.x),
        safe_floor_to_int(player_position.z),
    };
    const auto target_changed =
        !runtime.has_path_target || !(runtime.path_target == target);
    if (!target_changed && runtime.pursuit_repath_seconds > 0.0F) {
        return;
    }

    runtime.path = find_backrooms_marlow_path_impl(
        runtime.navigation,
        &readiness,
        current,
        target);
    // Le premier noeud est le centre de la cellule déjà occupée. Le rejouer à
    // chaque recalcul ferait reculer Marlow vers ce centre toutes les 350 ms.
    if (runtime.path.nodes.size() > 1U) {
        runtime.path.cursor = 1U;
    }
    runtime.path_target = target;
    runtime.has_path_target = true;
    runtime.pursuit_repath_seconds = kMarlowPursuitRepathSeconds;
}

[[nodiscard]] auto approach_marlow_angle(
    float current,
    float target,
    float maximum_delta) noexcept -> float {
    const auto delta = wrap_degrees(target - current);
    return wrap_degrees(
        current + std::clamp(delta, -maximum_delta, maximum_delta));
}

[[nodiscard]] auto marlow_movement_segment_clear(
    const BackroomsMarlowNavigationGrid& grid,
    const BackroomsMarlowChunkReadiness& readiness,
    const glm::vec3& from,
    const glm::vec3& to) noexcept -> bool {
    if (!finite_vector(from) || !finite_vector(to)) {
        return false;
    }

    const glm::vec3 horizontal_delta {
        to.x - from.x,
        0.0F,
        to.z - from.z,
    };
    const auto distance_squared =
        glm::dot(horizontal_delta, horizontal_delta);
    if (!std::isfinite(distance_squared)) {
        return false;
    }
    const BackroomsMarlowGridPoint from_point {
        safe_floor_to_int(from.x),
        safe_floor_to_int(from.z),
    };
    const auto* from_cell = backrooms_marlow_navigation_cell(
        grid,
        from_point.x,
        from_point.z);
    if (from_cell == nullptr ||
        !marlow_body_footprint_clear(
            grid,
            &readiness,
            from.x,
            from.z,
            from_cell->floor_y)) {
        return false;
    }

    constexpr auto kMovementSweepStep = 0.08F;
    constexpr auto kMaximumMovementSweepSteps = 32;
    const auto distance = std::sqrt(std::max(distance_squared, 0.0F));
    const auto step_count = std::clamp(
        static_cast<int>(std::ceil(distance / kMovementSweepStep)),
        1,
        kMaximumMovementSweepSteps);
    auto previous_floor = from_cell->floor_y;
    for (auto step = 1; step <= step_count; ++step) {
        const auto ratio =
            static_cast<float>(step) / static_cast<float>(step_count);
        const auto candidate = from + horizontal_delta * ratio;
        const BackroomsMarlowGridPoint point {
            safe_floor_to_int(candidate.x),
            safe_floor_to_int(candidate.z),
        };
        const auto* cell = backrooms_marlow_navigation_cell(
            grid,
            point.x,
            point.z);
        if (cell == nullptr ||
            std::abs(cell->floor_y - previous_floor) >
                kMarlowMaximumStepHeight ||
            !marlow_body_footprint_clear(
                grid,
                &readiness,
                candidate.x,
                candidate.z,
                cell->floor_y)) {
            return false;
        }
        previous_floor = cell->floor_y;
    }
    return true;
}

[[nodiscard]] auto follow_marlow_pursuit_path(
    BackroomsMarlowRuntime& runtime,
    const BackroomsMarlowChunkReadiness& readiness,
    float speed,
    float dt) noexcept -> float {
    if (runtime.path.empty() ||
        !std::isfinite(speed) ||
        speed <= 0.0F ||
        !std::isfinite(dt) ||
        dt <= 0.0F) {
        return 0.0F;
    }

    auto remaining_distance = speed * dt;
    auto travelled = 0.0F;
    auto safety_iterations = 0U;
    while (!runtime.path.empty() &&
           remaining_distance > 0.0001F &&
           safety_iterations++ < 12U) {
        const auto target_point =
            runtime.path.nodes[runtime.path.cursor];
        const auto* target_cell = backrooms_marlow_navigation_cell(
            runtime.navigation,
            target_point.x,
            target_point.z);
        if (target_cell == nullptr ||
            !marlow_point_is_navigable(
                runtime.navigation,
                &readiness,
                target_point)) {
            invalidate_marlow_pursuit_path(runtime);
            return travelled;
        }

        const glm::vec3 target {
            static_cast<float>(target_point.x) + 0.5F,
            target_cell->floor_y + 0.001F,
            static_cast<float>(target_point.z) + 0.5F,
        };
        const glm::vec3 horizontal_delta {
            target.x - runtime.position.x,
            0.0F,
            target.z - runtime.position.z,
        };
        const auto distance_squared =
            glm::dot(horizontal_delta, horizontal_delta);
        if (!std::isfinite(distance_squared)) {
            invalidate_marlow_pursuit_path(runtime);
            return travelled;
        }
        const auto distance = std::sqrt(std::max(distance_squared, 0.0F));
        if (distance <= kMarlowPathNodeTolerance) {
            runtime.position = target;
            ++runtime.path.cursor;
            continue;
        }

        const auto direction = horizontal_delta / distance;
        const auto travel = std::min(remaining_distance, distance);
        auto candidate = runtime.position + direction * travel;
        const BackroomsMarlowGridPoint candidate_point {
            safe_floor_to_int(candidate.x),
            safe_floor_to_int(candidate.z),
        };
        const auto* candidate_cell = backrooms_marlow_navigation_cell(
            runtime.navigation,
            candidate_point.x,
            candidate_point.z);
        if (candidate_cell == nullptr ||
            !marlow_movement_segment_clear(
                runtime.navigation,
                readiness,
                runtime.position,
                candidate)) {
            // Le balayage complet interdit le tunneling lorsque le framerate
            // chute ou qu'un recalcul de chemin change de direction en cellule.
            invalidate_marlow_pursuit_path(runtime);
            return travelled;
        }

        candidate.y = candidate_cell->floor_y + 0.001F;
        runtime.body_yaw_degrees = approach_marlow_angle(
            runtime.body_yaw_degrees,
            yaw_toward(runtime.position, candidate),
            360.0F * dt);
        runtime.position = candidate;
        travelled += travel;
        remaining_distance -= travel;

        if (travel + kMarlowPathNodeTolerance >= distance) {
            runtime.position = target;
            ++runtime.path.cursor;
        }
    }
    return travelled;
}

[[nodiscard]] auto marlow_runtime_position_is_valid(
    const BackroomsMarlowRuntime& runtime,
    const BackroomsMarlowChunkReadiness& readiness) noexcept -> bool {
    if (!finite_vector(runtime.position)) {
        return false;
    }
    const BackroomsMarlowGridPoint point {
        safe_floor_to_int(runtime.position.x),
        safe_floor_to_int(runtime.position.z),
    };
    const auto* cell = backrooms_marlow_navigation_cell(
        runtime.navigation,
        point.x,
        point.z);
    if (runtime.phase == BackroomsMarlowPhase::CornerPeek) {
        // Je n'impose pas le rayon du corps absent au mode HeadOnlyPeek : la
        // tete doit precisement pouvoir se placer contre son mur d'occlusion.
        return cell != nullptr &&
               cell->walkable &&
               cell->clearance >= kBackroomsMarlowRigStandingHeight &&
               point_ready(readiness, point);
    }
    return cell != nullptr &&
           marlow_body_footprint_clear(
               runtime.navigation,
               &readiness,
               runtime.position.x,
               runtime.position.z,
               cell->floor_y);
}

[[nodiscard]] auto marlow_capture_transition_is_valid(
    const BackroomsMarlowNavigationGrid& grid,
    const BackroomsMarlowChunkReadiness& readiness,
    BackroomsMarlowGridPoint from,
    BackroomsMarlowGridPoint to) noexcept -> bool {
    const auto* from_cell = backrooms_marlow_navigation_cell(
        grid,
        from.x,
        from.z);
    const auto* to_cell = backrooms_marlow_navigation_cell(
        grid,
        to.x,
        to.z);
    return from_cell != nullptr &&
           to_cell != nullptr &&
           from_cell->walkable &&
           to_cell->walkable &&
           point_ready(readiness, from) &&
           point_ready(readiness, to) &&
           std::abs(from_cell->floor_y - to_cell->floor_y) <=
               kMarlowMaximumStepHeight;
}

[[nodiscard]] auto marlow_capture_corridor_is_clear(
    const BackroomsMarlowNavigationGrid& grid,
    const BackroomsMarlowChunkReadiness& readiness,
    BackroomsMarlowGridPoint from,
    BackroomsMarlowGridPoint to) noexcept -> bool {
    if (!marlow_capture_transition_is_valid(
            grid,
            readiness,
            from,
            from) ||
        !marlow_capture_transition_is_valid(
            grid,
            readiness,
            to,
            to)) {
        return false;
    }

    const auto delta_x = to.x - from.x;
    const auto delta_z = to.z - from.z;
    const auto count_x = std::abs(delta_x);
    const auto count_z = std::abs(delta_z);
    const auto step_x = delta_x > 0 ? 1 : delta_x < 0 ? -1 : 0;
    const auto step_z = delta_z > 0 ? 1 : delta_z < 0 ? -1 : 0;
    auto current = from;
    auto traversed_x = 0;
    auto traversed_z = 0;
    while (traversed_x < count_x || traversed_z < count_z) {
        const auto decision_x =
            static_cast<std::int64_t>(1 + 2 * traversed_x) * count_z;
        const auto decision_z =
            static_cast<std::int64_t>(1 + 2 * traversed_z) * count_x;
        if (decision_x == decision_z) {
            const BackroomsMarlowGridPoint side_x {
                current.x + step_x,
                current.z,
            };
            const BackroomsMarlowGridPoint side_z {
                current.x,
                current.z + step_z,
            };
            const BackroomsMarlowGridPoint diagonal {
                current.x + step_x,
                current.z + step_z,
            };
            // Je valide les deux branches d'un coin exact : aucune marche
            // infranchissable ne peut etre masquee par une diagonale.
            if (!marlow_capture_transition_is_valid(
                    grid,
                    readiness,
                    current,
                    side_x) ||
                !marlow_capture_transition_is_valid(
                    grid,
                    readiness,
                    current,
                    side_z) ||
                !marlow_capture_transition_is_valid(
                    grid,
                    readiness,
                    side_x,
                    diagonal) ||
                !marlow_capture_transition_is_valid(
                    grid,
                    readiness,
                    side_z,
                    diagonal)) {
                return false;
            }
            current = diagonal;
            ++traversed_x;
            ++traversed_z;
            continue;
        }

        auto next = current;
        if (decision_x < decision_z) {
            next.x += step_x;
            ++traversed_x;
        } else {
            next.z += step_z;
            ++traversed_z;
        }
        if (!marlow_capture_transition_is_valid(
                grid,
                readiness,
                current,
                next)) {
            return false;
        }
        current = next;
    }
    return true;
}

[[nodiscard]] auto marlow_capture_corridor_is_valid(
    const BackroomsMarlowRuntime& runtime,
    const BackroomsMarlowChunkReadiness& readiness,
    const glm::vec3& player_position) noexcept -> bool {
    if (!finite_vector(runtime.capture_target) ||
        !finite_vector(player_position)) {
        return false;
    }
    const BackroomsMarlowGridPoint player_point {
        safe_floor_to_int(player_position.x),
        safe_floor_to_int(player_position.z),
    };
    const BackroomsMarlowGridPoint target_point {
        safe_floor_to_int(runtime.capture_target.x),
        safe_floor_to_int(runtime.capture_target.z),
    };
    const auto* target_cell = backrooms_marlow_navigation_cell(
        runtime.navigation,
        target_point.x,
        target_point.z);
    return target_cell != nullptr &&
           target_cell->walkable &&
           target_cell->has_water &&
           point_ready(readiness, target_point) &&
           target_cell->deep_water &&
           target_cell->water_depth >=
               kMarlowDrowningMinimumWaterDepth &&
           marlow_capture_corridor_is_clear(
               runtime.navigation,
               readiness,
               player_point,
               target_point);
}

} // namespace

auto find_backrooms_marlow_path(
    const BackroomsMarlowNavigationGrid& grid,
    BackroomsMarlowGridPoint start,
    BackroomsMarlowGridPoint goal) -> BackroomsMarlowPath {
    return find_backrooms_marlow_path_impl(
        grid,
        nullptr,
        start,
        goal);
}

auto backrooms_marlow_has_detour(
    const BackroomsMarlowNavigationGrid& grid,
    BackroomsMarlowGridPoint start,
    BackroomsMarlowGridPoint goal,
    BackroomsMarlowGridPoint blocked) noexcept -> bool {
    return has_detour_impl(grid, nullptr, start, goal, blocked);
}

auto backrooms_marlow_supercover_clear(
    const BackroomsMarlowNavigationGrid& grid,
    const BackroomsMarlowChunkReadiness& readiness,
    BackroomsMarlowGridPoint from,
    BackroomsMarlowGridPoint to) noexcept -> bool {
    const auto check = [&](BackroomsMarlowGridPoint point) noexcept {
        const auto* cell = backrooms_marlow_navigation_cell(
            grid,
            point.x,
            point.z);
        return cell != nullptr &&
               cell->walkable &&
               point_ready(readiness, point);
    };
    if (!check(from) || !check(to)) {
        return false;
    }

    const auto delta_x = to.x - from.x;
    const auto delta_z = to.z - from.z;
    const auto count_x = std::abs(delta_x);
    const auto count_z = std::abs(delta_z);
    const auto step_x = delta_x > 0 ? 1 : delta_x < 0 ? -1 : 0;
    const auto step_z = delta_z > 0 ? 1 : delta_z < 0 ? -1 : 0;
    auto x = from.x;
    auto z = from.z;
    auto traversed_x = 0;
    auto traversed_z = 0;
    while (traversed_x < count_x || traversed_z < count_z) {
        const auto decision_x =
            static_cast<std::int64_t>(1 + 2 * traversed_x) * count_z;
        const auto decision_z =
            static_cast<std::int64_t>(1 + 2 * traversed_z) * count_x;
        if (decision_x == decision_z) {
            // Je controle les deux cellules touchees par un coin exact : une
            // diagonale ne peut plus glisser entre deux obstacles jointifs.
            if (!check({x + step_x, z}) ||
                !check({x, z + step_z})) {
                return false;
            }
            x += step_x;
            z += step_z;
            ++traversed_x;
            ++traversed_z;
        } else if (decision_x < decision_z) {
            x += step_x;
            ++traversed_x;
        } else {
            z += step_z;
            ++traversed_z;
        }
        if (!check({x, z})) {
            return false;
        }
    }
    return true;
}

auto select_backrooms_marlow_manifestation(
    const BackroomsMarlowNavigationGrid& grid,
    const BackroomsMarlowChunkReadiness& readiness,
    const BackroomsMarlowPlayerContext& player,
    BackroomsMarlowEncounterMode mode,
    std::uint32_t random_state)
    -> BackroomsMarlowManifestationSelection {
    BackroomsMarlowManifestationSelection selection {};
    selection.mode = valid_mode(mode)
        ? mode
        : BackroomsMarlowEncounterMode::CornerPeek;
    selection.presentation =
        selection.mode == BackroomsMarlowEncounterMode::CornerPeek
            ? BackroomsMarlowPresentation::HeadOnlyPeek
            : BackroomsMarlowPresentation::FullBody;
    if (random_state == 0U) {
        random_state = kFallbackRandomState;
    }
    const BackroomsMarlowGridPoint player_point {
        safe_floor_to_int(player.feet_position.x),
        safe_floor_to_int(player.feet_position.z),
    };
    const auto* player_cell = backrooms_marlow_navigation_cell(
        grid,
        player_point.x,
        player_point.z);
    if (player_cell == nullptr ||
        !player_cell->walkable ||
        !point_ready(readiness, player_point)) {
        selection.next_random_state = random_state;
        return selection;
    }
    const auto look = safe_horizontal_direction(
        player.look_direction,
        glm::vec3 {0.0F, 0.0F, -1.0F});

    BackroomsMarlowPath direct_path {};
    BackroomsMarlowGridPoint route_goal {};
    if (selection.mode == BackroomsMarlowEncounterMode::Blocking) {
        auto best_goal_score = -std::numeric_limits<float>::infinity();
        for (std::size_t index = 0U; index < grid.cells.size(); ++index) {
            if (!grid.cells[index].walkable) {
                continue;
            }
            const auto point = cell_world_point(grid, static_cast<int>(index));
            if (!marlow_point_is_navigable(
                    grid,
                    &readiness,
                    point)) {
                continue;
            }
            const auto position = cell_position(grid, point);
            const auto distance = horizontal_distance(
                position,
                player.feet_position);
            if (distance < 18.0F || distance > 26.0F) {
                continue;
            }
            const auto direction = safe_horizontal_direction(
                position - player.feet_position,
                glm::vec3 {0.0F, 0.0F, 1.0F});
            const auto front_dot = glm::dot(look, direction);
            const auto score = front_dot * 5.0F -
                std::abs(distance - 22.0F) * 0.08F;
            if (front_dot > 0.55F && score > best_goal_score) {
                best_goal_score = score;
                route_goal = point;
            }
        }
        if (!std::isfinite(best_goal_score)) {
            selection.next_random_state = random_state;
            return selection;
        }
        direct_path = find_backrooms_marlow_path_impl(
            grid,
            &readiness,
            player_point,
            route_goal);
        if (direct_path.empty()) {
            selection.next_random_state = random_state;
            return selection;
        }
        for (std::size_t path_index = 0U;
             path_index < direct_path.nodes.size();
             ++path_index) {
            if (!point_ready(readiness, direct_path.nodes[path_index])) {
                selection.next_random_state = random_state;
                return selection;
            }
        }
    }

    auto best_score = -std::numeric_limits<float>::infinity();
    auto best_point = player_point;
    auto best_buoy = player_point;
    auto best_detour = false;
    const auto consider = [&](BackroomsMarlowGridPoint point,
                              const BackroomsMarlowNavigationCell& cell,
                              bool on_direct_route,
                              BackroomsMarlowGridPoint buoy,
                              bool has_detour,
                              float random_bias,
                              float& score_out) noexcept {
        const auto position = cell_position(grid, point);
        const auto distance = horizontal_distance(
            position,
            player.feet_position);
        const auto direction = safe_horizontal_direction(
            position - player.feet_position,
            glm::vec3 {0.0F, 0.0F, 1.0F});
        const auto front_dot = glm::dot(look, direction);
        if (selection.mode == BackroomsMarlowEncounterMode::CornerPeek) {
            if (distance < 12.0F || distance > 24.0F ||
                front_dot < 0.05F ||
                !has_adjacent_occluder(grid, point) ||
                !backrooms_marlow_supercover_clear(
                    grid,
                    readiness,
                    player_point,
                    point)) {
                return false;
            }
            score_out =
                (cell.dark ? 2.0F : 0.0F) +
                (cell.has_water ? 1.0F : 0.0F) +
                front_dot * 1.25F -
                std::abs(distance - 18.0F) * 0.08F +
                random_bias;
            return true;
        }
        if (selection.mode == BackroomsMarlowEncounterMode::WaterAmbush) {
            if (distance < 8.0F || distance > 16.0F ||
                !cell.has_water ||
                !backrooms_marlow_supercover_clear(
                    grid,
                    readiness,
                    player_point,
                    point)) {
                return false;
            }
            score_out =
                (cell.dark ? 2.0F : 0.0F) +
                (1.0F - std::abs(front_dot)) * 1.5F -
                std::abs(distance - 12.0F) * 0.10F +
                random_bias;
            return true;
        }
        if (!on_direct_route || !has_detour ||
            distance < 10.0F || distance > 18.0F ||
            !valid_grid_point(buoy)) {
            return false;
        }
        score_out =
            (cell.has_water ? 1.4F : 0.0F) +
            (cell.dark ? 1.2F : 0.0F) +
            front_dot * 2.0F -
            std::abs(distance - 14.0F) * 0.10F +
            random_bias;
        return true;
    };

    for (std::size_t index = 0U; index < grid.cells.size(); ++index) {
        const auto& cell = grid.cells[index];
        if (!cell.walkable) {
            continue;
        }
        const auto point = cell_world_point(grid, static_cast<int>(index));
        const auto point_is_clear =
            selection.mode == BackroomsMarlowEncounterMode::CornerPeek
                ? cell.clearance >= kBackroomsMarlowRigStandingHeight &&
                      point_ready(readiness, point)
                : marlow_point_is_navigable(
                      grid,
                      &readiness,
                      point);
        if (!point_is_clear) {
            continue;
        }
        auto on_direct_route = false;
        if (selection.mode == BackroomsMarlowEncounterMode::Blocking) {
            for (std::size_t path_index = 1U;
                 path_index + 1U < direct_path.nodes.size();
                 ++path_index) {
                if (direct_path.nodes[path_index] == point) {
                    on_direct_route = true;
                    break;
                }
            }
            if (!on_direct_route) {
                continue;
            }
        }
        const auto buoy = nearest_water_point(
            grid,
            readiness,
            point,
            selection.mode == BackroomsMarlowEncounterMode::Blocking
                ? 8.0F
                : 2.0F,
            kMarlowMinimumSafeWaterDepth);
        const auto has_detour =
            selection.mode == BackroomsMarlowEncounterMode::Blocking &&
            has_detour_impl(
                grid,
                &readiness,
                player_point,
                route_goal,
                point);
        auto score = 0.0F;
        if (!consider(
                point,
                cell,
                on_direct_route,
                buoy,
                has_detour,
                random_unit(random_state) * 0.10F,
                score)) {
            continue;
        }
        if (score > best_score) {
            best_score = score;
            best_point = point;
            best_buoy = valid_grid_point(buoy) ? buoy : point;
            best_detour = has_detour;
        }
    }

    selection.next_random_state = random_state;
    if (!std::isfinite(best_score)) {
        return selection;
    }
    selection.position = cell_position(grid, best_point);
    const auto* buoy_cell = backrooms_marlow_navigation_cell(
        grid,
        best_buoy.x,
        best_buoy.z);
    selection.buoy_position = cell_position(grid, best_buoy);
    if (buoy_cell != nullptr && buoy_cell->has_water) {
        selection.buoy_position.y = buoy_cell->water_surface_y + 0.06F;
    }
    selection.body_yaw_degrees = yaw_toward(
        selection.position,
        player.feet_position);
    if (selection.mode == BackroomsMarlowEncounterMode::CornerPeek) {
        const auto occluder = select_adjacent_occluder(
            grid,
            best_point,
            player.feet_position - selection.position);
        if (occluder.found) {
            // Le centre reste à plus du rayon corporel de la paroi. Le rig
            // supérieur effectue seul le mouvement de regard autour de l'angle.
            selection.position.x +=
                static_cast<float>(occluder.offset.x) *
                kMarlowCornerWallOffset;
            selection.position.z +=
                static_cast<float>(occluder.offset.z) *
                kMarlowCornerWallOffset;
            selection.peek_side = occluder.peek_side;
            selection.wall_normal = {
                -static_cast<float>(occluder.offset.x),
                0.0F,
                -static_cast<float>(occluder.offset.z),
            };
        }
    }
    selection.found = true;
    selection.has_guaranteed_detour = best_detour;
    return selection;
}

auto evaluate_backrooms_marlow_capture(
    const BackroomsMarlowNavigationGrid& grid,
    const BackroomsMarlowChunkReadiness& readiness,
    const BackroomsMarlowPlayerContext& player,
    const glm::vec3& marlow_position,
    bool buoy_warning_active)
    -> BackroomsMarlowCaptureEvaluation {
    BackroomsMarlowCaptureEvaluation evaluation {};
    const auto safe_player = safe_vector(
        player.feet_position,
        glm::vec3 {0.5F, static_cast<float>(kBackroomsFloorY + 1), 0.5F});
    const auto safe_marlow = safe_vector(marlow_position, safe_player);
    evaluation.distance = horizontal_distance(safe_player, safe_marlow);
    if (evaluation.distance > kBackroomsMarlowCaptureDistance) {
        return evaluation;
    }
    const BackroomsMarlowGridPoint player_point {
        safe_floor_to_int(safe_player.x),
        safe_floor_to_int(safe_player.z),
    };
    const BackroomsMarlowGridPoint marlow_point {
        safe_floor_to_int(safe_marlow.x),
        safe_floor_to_int(safe_marlow.z),
    };
    evaluation.player_reachable = marlow_capture_corridor_is_clear(
        grid,
        readiness,
        marlow_point,
        player_point);
    if (!evaluation.player_reachable) {
        return evaluation;
    }

    const auto maximum_water_distance = player.in_water
        ? 0.75F
        : kBackroomsMarlowConnectedShoreDistance;
    const auto water_point = nearest_water_point(
        grid,
        readiness,
        player_point,
        maximum_water_distance,
        kMarlowDrowningMinimumWaterDepth);
    if (!valid_grid_point(water_point) ||
        !marlow_capture_corridor_is_clear(
            grid,
            readiness,
            player_point,
            water_point)) {
        return evaluation;
    }

    // La route de Marlow et la trajectoire de traction sont deux contraintes
    // distinctes : un A* autour d'un mur n'autorise jamais à tirer le joueur
    // en ligne droite à travers ce même mur.
    const auto path = find_backrooms_marlow_path_impl(
        grid,
        &readiness,
        marlow_point,
        water_point);
    if (path.empty()) {
        return evaluation;
    }
    const auto* water_cell = backrooms_marlow_navigation_cell(
        grid,
        water_point.x,
        water_point.z);
    if (water_cell == nullptr ||
        !water_cell->deep_water ||
        water_cell->water_depth < kMarlowDrowningMinimumWaterDepth) {
        return evaluation;
    }

    evaluation.connected_water_found = true;
    // Je tire les pieds juste au-dessus du fond. La noyade ne sera validee
    // ensuite que si la vraie tete du joueur se trouve sous cette surface.
    evaluation.water_target = {
        static_cast<float>(water_point.x) + 0.5F,
        water_cell->floor_y + 0.05F,
        static_cast<float>(water_point.z) + 0.5F,
    };
    evaluation.allowed = buoy_warning_active;
    return evaluation;
}

auto update_backrooms_marlow(
    BackroomsMarlowState& state,
    BackroomsMarlowRuntime& runtime,
    const BackroomsGenerator& generator,
    const BackroomsMarlowUpdateContext& context,
    float dt) -> BackroomsMarlowUpdateResult {
    state = sanitize_backrooms_marlow_state(state);
    if (!state.initialized) {
        state = initialize_backrooms_marlow(
            state.random_state,
            generator.logical_level());
    }

    const auto logical_level = static_cast<std::int32_t>(
        std::clamp<std::int64_t>(
            generator.logical_level(),
            kMinimumLogicalLevel,
            kMaximumLogicalLevel));
    if (!generator.is_poolrooms() || logical_level > -2) {
        BackroomsMarlowUpdateResult lifecycle {};
        lifecycle.cancels_threat_request =
            runtime.waiting_for_threat_slot;
        lifecycle.releases_threat_slot = context.threat_slot_owned;
        reset_backrooms_marlow_runtime(runtime, state.pressure);
        state.logical_level = logical_level;
        return lifecycle;
    }
    if (state.logical_level != logical_level) {
        BackroomsMarlowUpdateResult lifecycle {};
        lifecycle.cancels_threat_request =
            runtime.waiting_for_threat_slot;
        lifecycle.releases_threat_slot = context.threat_slot_owned;
        state.logical_level = logical_level;
        state.cooldown_seconds = std::max(
            state.cooldown_seconds,
            kBackroomsMarlowInitialGraceSeconds);
        state.manifestation_seconds = std::max(
            state.manifestation_seconds,
            kBackroomsMarlowInitialGraceSeconds);
        reset_backrooms_marlow_runtime(runtime, state.pressure);
        return lifecycle;
    }

    if (!runtime.pressure_hysteresis_initialized) {
        runtime.pressure_attack_armed =
            state.pressure < kMarlowPressureAttackTrigger;
        runtime.pressure_hysteresis_initialized = true;
    }

    const auto safe_dt = clamp_finite(
        dt,
        0.0F,
        0.0F,
        kMaximumAcceptedDeltaSeconds);
    const auto player_position = safe_vector(
        context.player.feet_position,
        glm::vec3 {0.5F, static_cast<float>(kBackroomsFloorY + 1), 0.5F});
    const auto player_chunk = backrooms_marlow_chunk_at(player_position);
    const auto rebuild_navigation =
        !runtime.navigation_valid ||
        !navigation_grid_has_valid_shape(runtime.navigation) ||
        runtime.navigation.logical_level != logical_level ||
        runtime.navigation.center_chunk.x != player_chunk.x ||
        runtime.navigation.center_chunk.z != player_chunk.z ||
        !runtime.navigation_readiness_valid ||
        !inner_revisions_equal(
            runtime.navigation_readiness,
            context.chunk_readiness,
            player_chunk);
    const auto pending_invalidated_by_navigation =
        rebuild_navigation && runtime.waiting_for_threat_slot;
    if (rebuild_navigation) {
        rebuild_backrooms_marlow_navigation_grid(
            runtime.navigation,
            generator,
            player_chunk,
            context.spatial_world,
            context.spatial_world_y_offset);
        runtime.navigation_readiness = context.chunk_readiness;
        runtime.navigation_valid = true;
        runtime.navigation_readiness_valid = true;
        runtime.path.clear();
        runtime.has_path_target = false;
        runtime.pursuit_repath_seconds = 0.0F;
        if (pending_invalidated_by_navigation) {
            runtime.pending_manifestation = {};
            runtime.waiting_for_threat_slot = false;
            runtime.retry_seconds = 1.0F;
            state.manifestation_seconds = 0.0F;
        }
    }

    auto result = make_result_views(
        state,
        runtime,
        context.threat_slot_owned);
    result.cancels_threat_request =
        pending_invalidated_by_navigation;
    result.releases_threat_slot =
        pending_invalidated_by_navigation &&
        context.threat_slot_owned;
    if (context.simulation_frozen || safe_dt <= 0.0F) {
        if (runtime.waiting_for_threat_slot &&
            context.threat_slot_available) {
            result.requests_threat_slot = true;
        }
        return result;
    }

    const auto flashlight_started =
        context.player.flashlight_on_water &&
        !runtime.previous_flashlight_on_water;
    runtime.previous_flashlight_on_water =
        context.player.flashlight_on_water;
    if (context.player_alive) {
        const auto pressure = evaluate_backrooms_marlow_pressure(
            state.pressure,
            runtime.quiet_seconds,
            context.player,
            flashlight_started,
            safe_dt);
        state.pressure = pressure.pressure;
        runtime.quiet_seconds = pressure.quiet_seconds;
    }

    if (state.pressure <= kMarlowPressureAttackReset) {
        runtime.pressure_attack_armed = true;
    }
    if (runtime.pressure_attack_armed &&
        state.pressure >= kMarlowPressureAttackTrigger &&
        context.player_alive &&
        (runtime.phase == BackroomsMarlowPhase::Dormant ||
         runtime.phase == BackroomsMarlowPhase::Cooldown)) {
        const auto reaction_delay = random_range(
            state.random_state,
            kMarlowPressureReactionMinimumSeconds,
            kMarlowPressureReactionMaximumSeconds);
        state.manifestation_seconds = std::min(
            state.manifestation_seconds,
            reaction_delay);
        runtime.pressure_attack_armed = false;
    }

    runtime.grace_seconds = std::max(
        runtime.grace_seconds - safe_dt,
        0.0F);
    state.cooldown_seconds = std::max(
        state.cooldown_seconds - safe_dt,
        0.0F);
    state.cue_seconds -= safe_dt;
    state.manifestation_seconds -= safe_dt;
    runtime.retry_seconds = std::max(runtime.retry_seconds - safe_dt, 0.0F);
    runtime.pursuit_repath_seconds = std::max(
        runtime.pursuit_repath_seconds - safe_dt,
        0.0F);
    runtime.phase_seconds += safe_dt;

    if (state.cue_seconds <= 0.0F &&
        (runtime.phase == BackroomsMarlowPhase::Dormant ||
         runtime.phase == BackroomsMarlowPhase::Cooldown)) {
        auto random_state = state.random_state;
        const auto signal = signal_position(
            runtime.navigation,
            context.chunk_readiness,
            context.player,
            random_state);
        state.random_state = random_state;
        if (signal.found) {
            emit_event(
                result,
                state,
                BackroomsMarlowEventKind::WaterSignal,
                signal.position);
            state.cue_seconds = next_cue_delay(
                state.random_state,
                state.pressure);
        } else {
            // Je garde la fenetre ouverte plutot que de jouer un bruit d'eau
            // au pied du joueur quand aucun chunk aquatique n'est pret.
            state.cue_seconds = 1.0F;
        }
    }

    if (runtime.phase == BackroomsMarlowPhase::Cooldown) {
        if (state.cooldown_seconds <= 0.0F) {
            enter_phase(runtime, BackroomsMarlowPhase::Dormant, 0.0F);
        }
    }

    if (runtime.waiting_for_threat_slot) {
        if (!pending_manifestation_is_valid(
                runtime,
                context.chunk_readiness,
                context.player)) {
            runtime.pending_manifestation = {};
            runtime.waiting_for_threat_slot = false;
            runtime.retry_seconds = 1.0F;
            state.manifestation_seconds = 0.0F;
            result.cancels_threat_request = true;
            result.releases_threat_slot =
                context.threat_slot_owned;
        } else if (context.threat_slot_owned) {
            begin_pending_manifestation(state, runtime, result);
        } else if (context.threat_slot_available) {
            result.requests_threat_slot = true;
        }
    }

    if (runtime.phase == BackroomsMarlowPhase::Dormant &&
        !runtime.waiting_for_threat_slot &&
        state.manifestation_seconds <= 0.0F &&
        state.cooldown_seconds <= 0.0F &&
        runtime.grace_seconds <= 0.0F &&
        runtime.retry_seconds <= 0.0F &&
        context.allow_manifestation &&
        context.player_alive) {
        const auto mode = select_backrooms_marlow_mode(
            state.random_state,
            state.pressure,
            state.has_last_mode,
            state.last_mode);
        state.random_state = mode.next_random_state;
        auto selection = select_backrooms_marlow_manifestation(
            runtime.navigation,
            context.chunk_readiness,
            context.player,
            mode.mode,
            state.random_state);
        state.random_state = selection.next_random_state;
        const auto retry_aggressive_mode =
            [&](BackroomsMarlowEncounterMode retry_mode) {
                selection = select_backrooms_marlow_manifestation(
                    runtime.navigation,
                    context.chunk_readiness,
                    context.player,
                    retry_mode,
                    state.random_state);
                state.random_state = selection.next_random_state;
            };
        if (!selection.found &&
            mode.mode == BackroomsMarlowEncounterMode::Blocking) {
            retry_aggressive_mode(
                BackroomsMarlowEncounterMode::WaterAmbush);
        } else if (
            !selection.found &&
            mode.mode == BackroomsMarlowEncounterMode::WaterAmbush) {
            retry_aggressive_mode(
                BackroomsMarlowEncounterMode::Blocking);
        }
        if (!selection.found &&
            mode.mode != BackroomsMarlowEncounterMode::CornerPeek) {
            // L'observation d'angle reste le dernier repli sûr, uniquement
            // après avoir tenté les deux variantes capables de poursuivre.
            retry_aggressive_mode(
                BackroomsMarlowEncounterMode::CornerPeek);
        }
        if (selection.found) {
            runtime.pending_manifestation = selection;
            runtime.waiting_for_threat_slot = true;
            if (context.threat_slot_owned) {
                begin_pending_manifestation(state, runtime, result);
            } else if (context.threat_slot_available) {
                result.requests_threat_slot = true;
            }
        } else {
            // Je ne consomme pas la fenetre : je retente une seconde plus tard
            // quand les chunks ou un angle valide deviennent disponibles.
            runtime.retry_seconds = 1.0F;
            state.manifestation_seconds = 0.0F;
        }
    }

    const auto physical_phase =
        runtime.phase != BackroomsMarlowPhase::Dormant &&
        runtime.phase != BackroomsMarlowPhase::Cooldown;
    if (physical_phase &&
        !context.threat_slot_owned &&
        !runtime.waiting_for_threat_slot &&
        runtime.phase != BackroomsMarlowPhase::Dragging &&
        runtime.phase != BackroomsMarlowPhase::Drowning &&
        runtime.phase != BackroomsMarlowPhase::Screamer) {
        runtime.buoy_warning_active = false;
        invalidate_marlow_pursuit_path(runtime);
        enter_phase(runtime, BackroomsMarlowPhase::Submerging, 0.65F);
    }

    const auto transport_was_blocked =
        std::exchange(runtime.capture_transport_blocked, false);
    const auto capture_in_progress =
        runtime.phase == BackroomsMarlowPhase::Dragging ||
        runtime.phase == BackroomsMarlowPhase::Drowning;
    if (capture_in_progress &&
        (transport_was_blocked ||
         !context.player_alive ||
         !marlow_capture_corridor_is_valid(
             runtime,
             context.chunk_readiness,
             player_position))) {
        // Une collision réelle ou une modification du décor annule la noyade :
        // on ne téléporte jamais le joueur et on ne tue jamais à travers un mur.
        runtime.buoy_warning_active = false;
        runtime.capture_target = {};
        invalidate_marlow_pursuit_path(runtime);
        enter_phase(runtime, BackroomsMarlowPhase::Submerging, 0.30F);
    }

    const auto body_must_remain_spatially_valid =
        runtime.phase != BackroomsMarlowPhase::Dormant &&
        runtime.phase != BackroomsMarlowPhase::Cooldown &&
        runtime.phase != BackroomsMarlowPhase::Screamer;
    if (body_must_remain_spatially_valid &&
        !marlow_runtime_position_is_valid(
            runtime,
            context.chunk_readiness)) {
        runtime.buoy_warning_active = false;
        runtime.capture_target = {};
        invalidate_marlow_pursuit_path(runtime);
        if (runtime.phase == BackroomsMarlowPhase::Submerging) {
            runtime.phase_duration_seconds = std::min(
                runtime.phase_duration_seconds,
                runtime.phase_seconds + 0.15F);
        } else {
            enter_phase(runtime, BackroomsMarlowPhase::Submerging, 0.20F);
        }
    }

    switch (runtime.phase) {
    case BackroomsMarlowPhase::Dormant:
    case BackroomsMarlowPhase::Cooldown:
        break;
    case BackroomsMarlowPhase::Signaling:
        if (runtime.phase_seconds >= runtime.phase_duration_seconds) {
            const auto next_phase =
                runtime.pending_manifestation.mode ==
                        BackroomsMarlowEncounterMode::WaterAmbush
                    ? BackroomsMarlowPhase::Emerging
                    : BackroomsMarlowPhase::Blocking;
            enter_phase(
                runtime,
                next_phase,
                random_range(
                    state.random_state,
                    kMarlowPursuitMinimumSeconds,
                    kMarlowPursuitMaximumSeconds));
            runtime.pursuit_stuck_seconds = 0.0F;
            invalidate_marlow_pursuit_path(runtime);
            emit_event(
                result,
                state,
                BackroomsMarlowEventKind::Surfaced,
                runtime.position);
        }
        break;
    case BackroomsMarlowPhase::CornerPeek:
        if (runtime.phase_seconds >= runtime.phase_duration_seconds) {
            enter_phase(runtime, BackroomsMarlowPhase::Submerging, 0.65F);
        }
        break;
    case BackroomsMarlowPhase::Emerging:
    case BackroomsMarlowPhase::Blocking: {
        const auto distance_before_move = horizontal_distance(
            runtime.position,
            player_position);
        auto travelled = 0.0F;
        if (context.threat_slot_owned &&
            context.player_alive &&
            distance_before_move > kMarlowPursuitStopDistance) {
            refresh_marlow_pursuit_path(
                runtime,
                context.chunk_readiness,
                player_position);
            const auto desired_speed =
                kMarlowPursuitBaseSpeed +
                std::clamp(state.pressure, 0.0F, 1.0F) *
                    kMarlowPursuitPressureSpeedBonus +
                (runtime.pending_manifestation.mode ==
                         BackroomsMarlowEncounterMode::WaterAmbush
                     ? kMarlowWaterAmbushSpeedBonus
                     : 0.0F);
            const auto maximum_useful_speed =
                (distance_before_move - kMarlowPursuitStopDistance) /
                std::max(safe_dt, 0.001F);
            travelled = follow_marlow_pursuit_path(
                runtime,
                context.chunk_readiness,
                std::min(desired_speed, maximum_useful_speed),
                safe_dt);
        }

        const auto distance_after_move = horizontal_distance(
            runtime.position,
            player_position);
        runtime.body_yaw_degrees = approach_marlow_angle(
            runtime.body_yaw_degrees,
            yaw_toward(runtime.position, player_position),
            360.0F * safe_dt);
        if (travelled > 0.0001F ||
            distance_after_move <= kMarlowPursuitStopDistance) {
            runtime.pursuit_stuck_seconds = 0.0F;
        } else {
            runtime.pursuit_stuck_seconds += safe_dt;
        }

        const auto capture = evaluate_backrooms_marlow_capture(
            runtime.navigation,
            context.chunk_readiness,
            context.player,
            runtime.position,
            runtime.buoy_warning_active);
        if (context.allow_capture &&
            context.threat_slot_owned &&
            context.player_alive &&
            capture.allowed) {
            runtime.capture_target = capture.water_target;
            runtime.capture_transport_blocked = false;
            runtime.pursuit_stuck_seconds = 0.0F;
            invalidate_marlow_pursuit_path(runtime);
            enter_phase(
                runtime,
                BackroomsMarlowPhase::Dragging,
                kMarlowGrabSeconds);
            result.capture_started = true;
            if (!runtime.capture_event_emitted) {
                emit_event(
                    result,
                    state,
                    BackroomsMarlowEventKind::GrabbedPlayer,
                    runtime.position);
                runtime.capture_event_emitted = true;
            }
        } else if (
            runtime.phase_seconds >= runtime.phase_duration_seconds ||
            runtime.pursuit_stuck_seconds >=
                kMarlowPursuitStuckTimeoutSeconds) {
            runtime.buoy_warning_active = false;
            invalidate_marlow_pursuit_path(runtime);
            enter_phase(runtime, BackroomsMarlowPhase::Submerging, 0.65F);
        }
        break;
    }
    case BackroomsMarlowPhase::Submerging:
        if (runtime.phase_seconds >= runtime.phase_duration_seconds) {
            emit_event(
                result,
                state,
                BackroomsMarlowEventKind::Submerged,
                runtime.position);
            finish_manifestation(
                state,
                runtime,
                result,
                context.threat_slot_owned);
        }
        break;
    case BackroomsMarlowPhase::Dragging:
        if (runtime.phase_seconds >= runtime.phase_duration_seconds) {
            enter_phase(
                runtime,
                BackroomsMarlowPhase::Drowning,
                kBackroomsMarlowDrowningSeconds);
        }
        break;
    case BackroomsMarlowPhase::Drowning:
        if (runtime.phase_seconds >= runtime.phase_duration_seconds) {
            const auto target_horizontal_distance = horizontal_distance(
                player_position,
                runtime.capture_target);
            const auto target_vertical_distance = std::abs(
                player_position.y - runtime.capture_target.y);
            const BackroomsMarlowGridPoint target_point {
                safe_floor_to_int(runtime.capture_target.x),
                safe_floor_to_int(runtime.capture_target.z),
            };
            const auto* target_cell = backrooms_marlow_navigation_cell(
                runtime.navigation,
                target_point.x,
                target_point.z);
            const auto target_is_deep_water =
                target_cell != nullptr &&
                target_cell->walkable &&
                target_cell->has_water &&
                target_cell->deep_water &&
                target_cell->water_depth >=
                    kMarlowDrowningMinimumWaterDepth;
            const auto eye_is_below_target_surface =
                target_is_deep_water &&
                finite_vector(context.player.eye_position) &&
                context.player.eye_position.y <=
                    target_cell->water_surface_y - 0.05F;
            if (target_horizontal_distance >
                    kMarlowDrowningTargetTolerance ||
                target_vertical_distance >
                    kMarlowDrowningVerticalTolerance ||
                !finite_vector(context.player.feet_position) ||
                !context.player.head_in_water ||
                !eye_is_below_target_surface) {
                // La durée seule ne suffit pas : la victime doit réellement
                // avoir les pieds au fond et la tête sous la vraie surface.
                // Toute incohérence annule la capture sans jamais tuer.
                runtime.buoy_warning_active = false;
                runtime.capture_target = {};
                enter_phase(
                    runtime,
                    BackroomsMarlowPhase::Submerging,
                    0.30F);
                break;
            }
            if (!runtime.kill_event_emitted) {
                result.kill_player = true;
                runtime.kill_event_emitted = true;
            }
            enter_phase(
                runtime,
                BackroomsMarlowPhase::Screamer,
                kBackroomsMarlowScreamerSeconds);
            emit_event(
                result,
                state,
                BackroomsMarlowEventKind::Screamer,
                runtime.position);
        }
        break;
    case BackroomsMarlowPhase::Screamer:
        if (runtime.phase_seconds >= runtime.phase_duration_seconds) {
            finish_manifestation(
                state,
                runtime,
                result,
                context.threat_slot_owned);
        }
        break;
    }

    auto final_views = make_result_views(
        state,
        runtime,
        context.threat_slot_owned);
    final_views.events = result.events;
    final_views.event_count = result.event_count;
    final_views.requests_threat_slot = result.requests_threat_slot;
    final_views.cancels_threat_request =
        result.cancels_threat_request;
    final_views.releases_threat_slot = result.releases_threat_slot;
    final_views.capture_started = result.capture_started;
    final_views.kill_player = result.kill_player;
    return final_views;
}

} // namespace valcraft
