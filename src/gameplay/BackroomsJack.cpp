#include "gameplay/BackroomsJack.h"

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

constexpr float kFixedStepSeconds = 1.0F / 120.0F;
constexpr float kMaximumAcceptedDeltaSeconds = 0.25F;
constexpr float kJackWanderSpeed = 1.25F;
constexpr float kJackWatchingSuspicionThreshold = 3.75F;
constexpr float kJackSearchDelaySeconds = 0.75F;
constexpr float kJackDisappearDelaySeconds = 10.0F;
constexpr float kJackDisappearDistance = 24.0F;
constexpr float kJackCatchDistance = 1.10F;
constexpr float kJackMaximumSightDistance = 48.0F;
constexpr float kJackMaximumPlayerViewDistance = 70.0F;
constexpr float kJackFootprintRadius = 0.42F;
constexpr float kPi = 3.14159265358979323846F;
constexpr std::uint32_t kFallbackRandomState = 0xA341316CU;

template <typename Generator>
[[nodiscard]] auto generator_logical_level(
    const Generator& generator) noexcept -> std::int32_t {
    if constexpr (requires { generator.logical_level(); }) {
        const auto level =
            static_cast<std::int64_t>(
                generator.logical_level());
        return static_cast<std::int32_t>(
            std::clamp<std::int64_t>(
                level,
                kBackroomsMinimumLogicalLevel,
                kBackroomsMaximumLogicalLevel));
    }
    return 0;
}

template <typename Generator>
[[nodiscard]] auto generator_is_poolrooms(
    const Generator& generator) noexcept -> bool {
    if constexpr (requires { generator.is_poolrooms(); }) {
        return generator.is_poolrooms();
    }
    return false;
}

template <typename Generator>
[[nodiscard]] auto generator_has_water_at(
    const Generator& generator,
    int world_x,
    int world_y,
    int world_z) noexcept -> bool {
    if constexpr (requires {
                      generator.sample_water_state(
                          world_x,
                          world_y,
                          world_z);
                  }) {
        return water_level_from_state(
                   generator.sample_water_state(
                       world_x,
                       world_y,
                       world_z)) >
               0U;
    }
    return false;
}

struct OpenNode {
    int index = 0;
    int f_cost = 0;
    int h_cost = 0;
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

[[nodiscard]] auto safe_direction(
    const glm::vec3& value,
    const glm::vec3& fallback) noexcept -> glm::vec3 {
    const auto safe = safe_vector(value, fallback);
    const auto squared_length = glm::dot(safe, safe);
    if (!std::isfinite(squared_length) || squared_length <= 0.000001F) {
        return fallback;
    }
    return safe / std::sqrt(squared_length);
}

[[nodiscard]] auto horizontal_distance(
    const glm::vec3& first,
    const glm::vec3& second) noexcept -> float {
    const auto delta_x = first.x - second.x;
    const auto delta_z = first.z - second.z;
    return std::sqrt(delta_x * delta_x + delta_z * delta_z);
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

[[nodiscard]] auto approach(
    float current,
    float target,
    float maximum_delta) noexcept -> float {
    if (current < target) {
        return std::min(current + maximum_delta, target);
    }
    return std::max(current - maximum_delta, target);
}

[[nodiscard]] auto approach_angle(
    float current,
    float target,
    float maximum_delta) noexcept -> float {
    const auto difference = wrap_degrees(target - current);
    return wrap_degrees(
        current +
        std::clamp(difference, -maximum_delta, maximum_delta));
}

[[nodiscard]] auto yaw_from_direction(const glm::vec3& direction) noexcept
    -> float {
    const auto horizontal = safe_horizontal_direction(
        direction,
        glm::vec3 {0.0F, 0.0F, -1.0F});
    return wrap_degrees(
        std::atan2(horizontal.x, -horizontal.z) *
        180.0F / kPi);
}

[[nodiscard]] auto forward_from_yaw(float yaw_degrees) noexcept -> glm::vec3 {
    const auto radians = yaw_degrees * kPi / 180.0F;
    return {
        std::sin(radians),
        0.0F,
        -std::cos(radians),
    };
}

[[nodiscard]] constexpr auto floor_division(
    int value,
    int divisor) noexcept -> int {
    auto quotient = value / divisor;
    const auto remainder = value % divisor;
    if (remainder < 0) {
        --quotient;
    }
    return quotient;
}

[[nodiscard]] auto safe_floor_to_int(float value) noexcept -> int {
    if (!std::isfinite(value)) {
        return 0;
    }
    const auto floored = std::floor(static_cast<double>(value));
    const auto minimum =
        static_cast<double>(std::numeric_limits<int>::lowest());
    const auto maximum =
        static_cast<double>(std::numeric_limits<int>::max());
    return static_cast<int>(std::clamp(floored, minimum, maximum));
}

[[nodiscard]] auto saturating_int(std::int64_t value) noexcept -> int {
    return static_cast<int>(std::clamp(
        value,
        static_cast<std::int64_t>(std::numeric_limits<int>::lowest()),
        static_cast<std::int64_t>(std::numeric_limits<int>::max())));
}

[[nodiscard]] constexpr auto local_index(int local_x, int local_z) noexcept
    -> std::size_t {
    return static_cast<std::size_t>(
        local_z * kBackroomsJackNavigationSide + local_x);
}

[[nodiscard]] auto grid_local_coordinates(
    const BackroomsJackNavigationGrid& grid,
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
        local_x64 >= kBackroomsJackNavigationSide ||
        local_z64 < 0 ||
        local_z64 >= kBackroomsJackNavigationSide) {
        return false;
    }
    local_x = static_cast<int>(local_x64);
    local_z = static_cast<int>(local_z64);
    return true;
}

[[nodiscard]] auto cell_world_point(
    const BackroomsJackNavigationGrid& grid,
    int index) noexcept -> BackroomsJackGridPoint {
    const auto local_z = index / kBackroomsJackNavigationSide;
    const auto local_x = index % kBackroomsJackNavigationSide;
    return {
        saturating_int(
            static_cast<std::int64_t>(grid.origin_world_x) +
            local_x),
        saturating_int(
            static_cast<std::int64_t>(grid.origin_world_z) +
            local_z),
    };
}

[[nodiscard]] auto point_index(
    const BackroomsJackNavigationGrid& grid,
    BackroomsJackGridPoint point) noexcept -> int {
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
    return local_z * kBackroomsJackNavigationSide + local_x;
}

[[nodiscard]] auto nearest_walkable_index(
    const BackroomsJackNavigationGrid& grid,
    BackroomsJackGridPoint point) noexcept -> int {
    const auto exact = point_index(grid, point);
    if (exact >= 0 &&
        grid.cells[static_cast<std::size_t>(exact)].walkable) {
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
        const auto delta_x =
            static_cast<std::int64_t>(candidate.x) -
            static_cast<std::int64_t>(point.x);
        const auto delta_z =
            static_cast<std::int64_t>(candidate.z) -
            static_cast<std::int64_t>(point.z);
        const auto distance =
            delta_x * delta_x + delta_z * delta_z;
        if (distance < best_distance) {
            best_distance = distance;
            best_index = static_cast<int>(index);
        }
    }
    return best_index;
}

[[nodiscard]] auto next_random(std::uint32_t& state) noexcept
    -> std::uint32_t {
    if (state == 0U) {
        state = kFallbackRandomState;
    }
    state ^= state << 13U;
    state ^= state >> 17U;
    state ^= state << 5U;
    if (state == 0U) {
        state = kFallbackRandomState;
    }
    return state;
}

[[nodiscard]] auto random_unit(std::uint32_t& state) noexcept -> float {
    constexpr auto denominator = 4294967296.0;
    return static_cast<float>(
        static_cast<double>(next_random(state)) / denominator);
}

[[nodiscard]] auto random_range(
    std::uint32_t& state,
    float minimum,
    float maximum) noexcept -> float {
    return minimum + (maximum - minimum) * random_unit(state);
}

[[nodiscard]] auto chunks_equal(
    const ChunkCoord& first,
    const ChunkCoord& second) noexcept -> bool {
    return first.x == second.x && first.z == second.z;
}

[[nodiscard]] auto readiness_at(
    const BackroomsJackChunkReadiness& readiness,
    const ChunkCoord& chunk) noexcept -> bool {
    const auto delta_x =
        static_cast<std::int64_t>(chunk.x) -
        static_cast<std::int64_t>(readiness.center_chunk.x);
    const auto delta_z =
        static_cast<std::int64_t>(chunk.z) -
        static_cast<std::int64_t>(readiness.center_chunk.z);
    if (delta_x < -1 || delta_x > 1 ||
        delta_z < -1 || delta_z > 1) {
        return false;
    }
    const auto index =
        static_cast<std::size_t>(
            (delta_z + 1) * 3 + delta_x + 1);
    return readiness.ready[index];
}

[[nodiscard]] auto sanitize_player_context(
    const BackroomsJackPlayerContext& context) noexcept
    -> BackroomsJackPlayerContext {
    BackroomsJackPlayerContext sanitized = context;
    sanitized.feet_position = safe_vector(
        sanitized.feet_position,
        glm::vec3 {
            0.5F,
            static_cast<float>(kBackroomsFloorY + 1),
            0.5F,
        });
    sanitized.eye_position = safe_vector(
        sanitized.eye_position,
        sanitized.feet_position + glm::vec3 {0.0F, 1.62F, 0.0F});
    sanitized.look_direction = safe_direction(
        sanitized.look_direction,
        glm::vec3 {0.0F, 0.0F, -1.0F});
    sanitized.maximum_sprint_speed = clamp_finite(
        sanitized.maximum_sprint_speed,
        7.2F,
        0.5F,
        20.0F);
    return sanitized;
}

[[nodiscard]] auto jack_eye_position(
    const glm::vec3& position,
    float hunch_ratio) noexcept -> glm::vec3 {
    const auto hunch = std::clamp(hunch_ratio, 0.0F, 1.0F);
    const auto eye_height = 3.42F + (2.88F - 3.42F) * hunch;
    return position + glm::vec3 {0.0F, eye_height, 0.0F};
}

[[nodiscard]] auto player_can_see_candidate(
    const BackroomsGenerator& generator,
    const BackroomsJackPlayerContext& player,
    const glm::vec3& candidate,
    float hunch_ratio,
    float half_fov_degrees) noexcept -> bool {
    const auto head = jack_eye_position(candidate, hunch_ratio);
    const auto direction = head - player.eye_position;
    const auto distance = glm::length(direction);
    if (!std::isfinite(distance) ||
        distance <= 0.001F ||
        distance > kJackMaximumPlayerViewDistance) {
        return false;
    }
    const auto normalized = direction / distance;
    const auto threshold =
        std::cos(half_fov_degrees * kPi / 180.0F);
    return glm::dot(player.look_direction, normalized) >= threshold &&
           backrooms_jack_has_line_of_sight(
               generator,
               player.eye_position,
               head);
}

[[nodiscard]] auto walkable_footprint(
    const BackroomsGenerator& generator,
    float center_x,
    float center_z) noexcept -> bool {
    constexpr std::array<std::pair<float, float>, 9> offsets {{
        {0.0F, 0.0F},
        {kJackFootprintRadius, 0.0F},
        {-kJackFootprintRadius, 0.0F},
        {0.0F, kJackFootprintRadius},
        {0.0F, -kJackFootprintRadius},
        {kJackFootprintRadius, kJackFootprintRadius},
        {kJackFootprintRadius, -kJackFootprintRadius},
        {-kJackFootprintRadius, kJackFootprintRadius},
        {-kJackFootprintRadius, -kJackFootprintRadius},
    }};
    // Je contrôle aussi les bords du corps pour ne jamais laisser Jack
    // traverser une cloison lorsque son centre reste encore dans le couloir.
    for (const auto& [offset_x, offset_z] : offsets) {
        const auto world_x = safe_floor_to_int(center_x + offset_x);
        const auto world_z = safe_floor_to_int(center_z + offset_z);
        const auto column = generator.sample_column(world_x, world_z);
        const auto clearance = static_cast<float>(
            column.ceiling_y - (column.floor_y + 1));
        if (column.wall ||
            clearance < kBackroomsJackBentHeight + 0.02F) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] auto path_heuristic(
    int first_index,
    int second_index) noexcept -> int {
    const auto first_x = first_index % kBackroomsJackNavigationSide;
    const auto first_z = first_index / kBackroomsJackNavigationSide;
    const auto second_x = second_index % kBackroomsJackNavigationSide;
    const auto second_z = second_index / kBackroomsJackNavigationSide;
    const auto delta_x = std::abs(first_x - second_x);
    const auto delta_z = std::abs(first_z - second_z);
    const auto diagonal = std::min(delta_x, delta_z);
    const auto straight = std::max(delta_x, delta_z) - diagonal;
    return diagonal * 14 + straight * 10;
}

void emit_event(
    BackroomsJackState& state,
    BackroomsJackUpdateResult& result,
    BackroomsJackEventKind kind) noexcept {
    if (result.event_count >= result.events.size()) {
        return;
    }
    result.events[result.event_count] = {
        kind,
        state.position,
        state.next_event_sequence,
    };
    ++result.event_count;
    if (state.next_event_sequence ==
        std::numeric_limits<std::uint64_t>::max()) {
        state.next_event_sequence = 1U;
    } else {
        ++state.next_event_sequence;
    }
}

void clear_navigation_path(BackroomsJackRuntime& runtime) noexcept {
    runtime.path = {};
    runtime.has_path_target = false;
    runtime.repath_seconds = 0.0F;
    runtime.stuck_seconds = 0.0F;
}

void enter_phase(
    BackroomsJackState& state,
    BackroomsJackRuntime& runtime,
    BackroomsJackPhase phase) noexcept {
    state.phase = phase;
    state.phase_seconds = 0.0F;
    clear_navigation_path(runtime);
}

[[nodiscard]] auto world_point_from_position(
    const glm::vec3& position) noexcept -> BackroomsJackGridPoint {
    return {
        safe_floor_to_int(position.x),
        safe_floor_to_int(position.z),
    };
}

[[nodiscard]] auto choose_wander_target(
    BackroomsJackState& state,
    const BackroomsJackNavigationGrid& grid,
    const glm::vec3& origin,
    float maximum_distance) noexcept -> BackroomsJackGridPoint {
    auto selected = world_point_from_position(origin);
    const auto start_index = nearest_walkable_index(grid, selected);
    if (start_index < 0) {
        return selected;
    }

    // Je limite chaque destination à la composante actuellement accessible.
    // Jack peut ainsi parcourir tous les vrais passages sans viser en boucle
    // une salle décorative fermée par la génération procédurale.
    std::array<bool, kBackroomsJackNavigationCellCount> reachable {};
    std::array<int, kBackroomsJackNavigationCellCount> pending {};
    auto read_cursor = std::size_t {0U};
    auto write_cursor = std::size_t {1U};
    pending[0] = start_index;
    reachable[static_cast<std::size_t>(start_index)] = true;
    constexpr std::array<std::pair<int, int>, 4> directions {{
        {0, -1},
        {-1, 0},
        {1, 0},
        {0, 1},
    }};
    while (read_cursor < write_cursor) {
        const auto current = pending[read_cursor];
        ++read_cursor;
        const auto current_x =
            current % kBackroomsJackNavigationSide;
        const auto current_z =
            current / kBackroomsJackNavigationSide;
        for (const auto& [delta_x, delta_z] : directions) {
            const auto next_x = current_x + delta_x;
            const auto next_z = current_z + delta_z;
            if (next_x < 0 ||
                next_x >= kBackroomsJackNavigationSide ||
                next_z < 0 ||
                next_z >= kBackroomsJackNavigationSide) {
                continue;
            }
            const auto next =
                next_z * kBackroomsJackNavigationSide + next_x;
            const auto index = static_cast<std::size_t>(next);
            if (reachable[index] || !grid.cells[index].walkable) {
                continue;
            }
            reachable[index] = true;
            pending[write_cursor] = next;
            ++write_cursor;
        }
    }

    auto eligible_count = std::uint32_t {0U};
    for (std::size_t index = 0U; index < grid.cells.size(); ++index) {
        if (!reachable[index]) {
            continue;
        }
        const auto candidate =
            cell_world_point(grid, static_cast<int>(index));
        const glm::vec3 candidate_position {
            static_cast<float>(candidate.x) + 0.5F,
            origin.y,
            static_cast<float>(candidate.z) + 0.5F,
        };
        const auto distance =
            horizontal_distance(origin, candidate_position);
        if (distance < 7.0F || distance > maximum_distance) {
            continue;
        }
        ++eligible_count;
        if (next_random(state.random_state) % eligible_count == 0U) {
            selected = candidate;
        }
    }
    return selected;
}

void assign_path(
    BackroomsJackRuntime& runtime,
    const BackroomsJackNavigationGrid& grid,
    BackroomsJackGridPoint start,
    BackroomsJackGridPoint goal,
    float repath_seconds) {
    runtime.path = find_backrooms_jack_path(grid, start, goal);
    runtime.path_target = goal;
    runtime.has_path_target = !runtime.path.empty();
    runtime.repath_seconds = repath_seconds;
    runtime.stuck_seconds = 0.0F;
}

[[nodiscard]] auto target_hunch_for(
    const BackroomsJackState& state,
    const BackroomsJackRuntime& runtime) noexcept -> float {
    const auto current_point = world_point_from_position(state.position);
    const auto* current_cell = backrooms_jack_navigation_cell(
        runtime.navigation,
        current_point.x,
        current_point.z);
    if (current_cell == nullptr || !current_cell->standing_allowed) {
        return 1.0F;
    }
    if (!runtime.path.empty()) {
        const auto next_point = runtime.path.nodes[runtime.path.cursor];
        const auto* next_cell = backrooms_jack_navigation_cell(
            runtime.navigation,
            next_point.x,
            next_point.z);
        if (next_cell != nullptr && !next_cell->standing_allowed) {
            return 1.0F;
        }
    }
    return 0.0F;
}

[[nodiscard]] auto follow_path(
    BackroomsJackState& state,
    BackroomsJackRuntime& runtime,
    const BackroomsGenerator& generator,
    float speed,
    float dt) noexcept -> float {
    if (runtime.path.empty() || speed <= 0.0F) {
        return 0.0F;
    }

    while (!runtime.path.empty()) {
        const auto target_point =
            runtime.path.nodes[runtime.path.cursor];
        const auto* target_cell = backrooms_jack_navigation_cell(
            runtime.navigation,
            target_point.x,
            target_point.z);
        if (target_cell == nullptr || !target_cell->walkable) {
            clear_navigation_path(runtime);
            return 0.0F;
        }

        const glm::vec3 target {
            static_cast<float>(target_point.x) + 0.5F,
            target_cell->floor_y,
            static_cast<float>(target_point.z) + 0.5F,
        };
        const glm::vec3 horizontal_delta {
            target.x - state.position.x,
            0.0F,
            target.z - state.position.z,
        };
        const auto distance = glm::length(horizontal_delta);
        if (distance <= 0.055F) {
            state.position.x = target.x;
            state.position.y = target.y;
            state.position.z = target.z;
            ++runtime.path.cursor;
            continue;
        }

        // Je termine la transition du dos avant d'entrer sous un plafond bas.
        if (!target_cell->standing_allowed &&
            state.hunch_ratio < 0.68F) {
            return 0.0F;
        }

        const auto direction = horizontal_delta / distance;
        state.body_yaw_degrees = approach_angle(
            state.body_yaw_degrees,
            yaw_from_direction(direction),
            (state.phase == BackroomsJackPhase::Chasing
                 ? 300.0F
                 : 78.0F) *
                dt);
        const auto travel = std::min(
            speed *
                std::clamp(
                    target_cell->movement_speed_multiplier,
                    0.1F,
                    1.0F) *
                dt,
            distance);
        const auto candidate =
            state.position + direction * travel;
        if (!walkable_footprint(
                generator,
                candidate.x,
                candidate.z)) {
            clear_navigation_path(runtime);
            return 0.0F;
        }
        state.position.x = candidate.x;
        state.position.z = candidate.z;
        state.position.y = approach(
            state.position.y,
            target.y,
            travel);
        return travel;
    }
    return 0.0F;
}

void emit_footsteps(
    BackroomsJackState& state,
    BackroomsJackUpdateResult& result,
    float traveled) noexcept {
    if (traveled <= 0.0F) {
        return;
    }
    const auto stride =
        state.phase == BackroomsJackPhase::Chasing
            ? 1.22F
            : 1.52F;
    state.footstep_distance += traveled;
    while (state.footstep_distance >= stride) {
        state.footstep_distance -= stride;
        emit_event(
            state,
            result,
            state.next_step_is_wooden
                ? BackroomsJackEventKind::WoodenLegStep
                : BackroomsJackEventKind::BootStep);
        state.next_step_is_wooden = !state.next_step_is_wooden;
    }
}

void reset_evasion_tracking(
    BackroomsJackState& state,
    const BackroomsJackPlayerContext& player) noexcept {
    state.lost_sight_seconds = 0.0F;
    state.unseen_travel_distance = 0.0F;
    state.evaded_chunk_count = 0U;
    state.has_last_evade_chunk = false;
    state.previous_player_position = player.feet_position;
    state.has_previous_player_position = true;
}

void track_evasion(
    BackroomsJackState& state,
    const BackroomsJackPlayerContext& player,
    float dt) noexcept {
    state.lost_sight_seconds += dt;
    if (state.has_previous_player_position) {
        state.unseen_travel_distance += horizontal_distance(
            state.previous_player_position,
            player.feet_position);
    }
    state.previous_player_position = player.feet_position;
    state.has_previous_player_position = true;

    const auto current_chunk =
        backrooms_jack_chunk_at(player.feet_position);
    // Je compte uniquement les nouveaux chunks après la perte de vue. Un
    // aller-retour sur la même frontière ne suffit donc pas à le semer.
    if (!state.has_last_evade_chunk) {
        state.last_evade_chunk = current_chunk;
        state.has_last_evade_chunk = true;
        return;
    }
    if (chunks_equal(current_chunk, state.last_evade_chunk)) {
        return;
    }
    state.last_evade_chunk = current_chunk;

    const auto already_recorded = std::any_of(
        state.evaded_chunks.begin(),
        state.evaded_chunks.begin() +
            static_cast<std::ptrdiff_t>(state.evaded_chunk_count),
        [&](const ChunkCoord& chunk) {
            return chunks_equal(chunk, current_chunk);
        });
    if (!already_recorded &&
        state.evaded_chunk_count < state.evaded_chunks.size()) {
        state.evaded_chunks[state.evaded_chunk_count] = current_chunk;
        ++state.evaded_chunk_count;
    }
}

[[nodiscard]] auto suspicion_rate(float distance) noexcept -> float {
    if (distance <= 18.0F) {
        return 1.25F;
    }
    if (distance <= 36.0F) {
        return 0.65F;
    }
    return 0.35F;
}

[[nodiscard]] auto phase_has_body(BackroomsJackPhase phase) noexcept -> bool {
    return phase == BackroomsJackPhase::Wandering ||
           phase == BackroomsJackPhase::Watching ||
           phase == BackroomsJackPhase::Chasing ||
           phase == BackroomsJackPhase::Searching ||
           phase == BackroomsJackPhase::Jumpscare;
}

void begin_watching(
    BackroomsJackState& state,
    BackroomsJackRuntime& runtime,
    BackroomsJackUpdateResult& result,
    const BackroomsJackPlayerContext& player) noexcept {
    enter_phase(state, runtime, BackroomsJackPhase::Watching);
    state.suspicion = 0.0F;
    state.lost_sight_seconds = 0.0F;
    state.last_seen_player_position = player.feet_position;
    if (!state.notice_event_emitted) {
        emit_event(state, result, BackroomsJackEventKind::Notice);
        state.notice_event_emitted = true;
    }
}

void begin_chasing(
    BackroomsJackState& state,
    BackroomsJackRuntime& runtime,
    BackroomsJackUpdateResult& result,
    const BackroomsJackPlayerContext& player) noexcept {
    enter_phase(state, runtime, BackroomsJackPhase::Chasing);
    state.last_seen_player_position = player.feet_position;
    reset_evasion_tracking(state, player);
    if (!state.chase_event_emitted) {
        emit_event(state, result, BackroomsJackEventKind::Chase);
        state.chase_event_emitted = true;
    }
}

void begin_cooldown(
    BackroomsJackState& state,
    BackroomsJackRuntime& runtime,
    BackroomsJackUpdateResult& result,
    bool emit_vanished) noexcept {
    enter_phase(state, runtime, BackroomsJackPhase::Cooldown);
    state.active = false;
    state.suspicion = 0.0F;
    state.lost_sight_seconds = 0.0F;
    state.unseen_travel_distance = 0.0F;
    state.cooldown_seconds = random_range(
        state.random_state,
        kBackroomsJackMinimumCooldownSeconds,
        kBackroomsJackMaximumCooldownSeconds);
    if (emit_vanished) {
        emit_event(state, result, BackroomsJackEventKind::Vanished);
    }
}

[[nodiscard]] auto update_active_phase(
    BackroomsJackState& state,
    BackroomsJackRuntime& runtime,
    const BackroomsGenerator& generator,
    const BackroomsJackPlayerContext& player,
    float dt,
    BackroomsJackUpdateResult& result) -> float {
    const auto perception = evaluate_backrooms_jack_perception(
        generator,
        player,
        state.position,
        state.body_yaw_degrees,
        state.hunch_ratio);
    auto traveled = 0.0F;

    switch (state.phase) {
    case BackroomsJackPhase::Wandering: {
        state.head_yaw_degrees = approach(
            state.head_yaw_degrees,
            0.0F,
            90.0F * dt);
        if (perception.jack_sees_player) {
            begin_watching(state, runtime, result, player);
            break;
        }
        runtime.repath_seconds =
            std::max(0.0F, runtime.repath_seconds - dt);
        if (runtime.repath_seconds <= 0.0F) {
            const auto target = choose_wander_target(
                state,
                runtime.navigation,
                state.position,
                32.0F);
            assign_path(
                runtime,
                runtime.navigation,
                world_point_from_position(state.position),
                target,
                random_range(state.random_state, 1.0F, 2.2F));
        }
        traveled = follow_path(
            state,
            runtime,
            generator,
            kJackWanderSpeed,
            dt);
        break;
    }
    case BackroomsJackPhase::Watching: {
        const auto to_player =
            player.feet_position - state.position;
        state.body_yaw_degrees = approach_angle(
            state.body_yaw_degrees,
            yaw_from_direction(to_player),
            95.0F * dt);
        state.head_yaw_degrees =
            std::sin(state.phase_seconds * 2.35F) * 12.0F;
        if (perception.jack_sees_player) {
            state.lost_sight_seconds = 0.0F;
            state.last_seen_player_position = player.feet_position;
            state.suspicion = std::min(
                kJackWatchingSuspicionThreshold,
                state.suspicion +
                    suspicion_rate(perception.distance) * dt);
            if (state.suspicion >=
                kJackWatchingSuspicionThreshold) {
                begin_chasing(
                    state,
                    runtime,
                    result,
                    player);
            }
        } else {
            state.lost_sight_seconds += dt;
            state.suspicion = std::max(
                0.0F,
                state.suspicion - 0.80F * dt);
            if (state.lost_sight_seconds >= 1.25F) {
                enter_phase(
                    state,
                    runtime,
                    BackroomsJackPhase::Wandering);
                state.suspicion = 0.0F;
                state.head_yaw_degrees = 0.0F;
            }
        }
        break;
    }
    case BackroomsJackPhase::Chasing: {
        state.head_yaw_degrees = approach(
            state.head_yaw_degrees,
            0.0F,
            180.0F * dt);
        if (perception.jack_sees_player) {
            state.last_seen_player_position = player.feet_position;
            reset_evasion_tracking(state, player);
        } else {
            track_evasion(state, player, dt);
        }

        runtime.repath_seconds =
            std::max(0.0F, runtime.repath_seconds - dt);
        const auto target_position =
            perception.jack_sees_player
                ? player.feet_position
                : state.last_seen_player_position;
        const auto target_point =
            world_point_from_position(target_position);
        if (runtime.path.empty() ||
            runtime.repath_seconds <= 0.0F ||
            !runtime.has_path_target ||
            !(runtime.path_target == target_point)) {
            assign_path(
                runtime,
                runtime.navigation,
                world_point_from_position(state.position),
                target_point,
                0.20F);
        }
        traveled = follow_path(
            state,
            runtime,
            generator,
            player.maximum_sprint_speed,
            dt);

        if (state.lost_sight_seconds >=
            kJackSearchDelaySeconds) {
            enter_phase(
                state,
                runtime,
                BackroomsJackPhase::Searching);
        } else if (
            horizontal_distance(
                state.position,
                player.feet_position) <=
                kJackCatchDistance &&
            std::abs(state.position.y - player.feet_position.y) <=
                2.5F) {
            enter_phase(
                state,
                runtime,
                BackroomsJackPhase::Jumpscare);
            if (!state.screamer_event_emitted) {
                emit_event(
                    state,
                    result,
                    BackroomsJackEventKind::Screamer);
                state.screamer_event_emitted = true;
                result.caught_player = true;
            }
        }
        break;
    }
    case BackroomsJackPhase::Searching: {
        state.head_yaw_degrees =
            std::sin(state.phase_seconds * 1.75F) * 18.0F;
        if (perception.jack_sees_player) {
            begin_chasing(
                state,
                runtime,
                result,
                player);
            break;
        }
        track_evasion(state, player, dt);
        const auto separation =
            horizontal_distance(state.position, player.feet_position);
        if (state.lost_sight_seconds >=
                kJackDisappearDelaySeconds &&
            state.evaded_chunk_count >= 3U &&
            separation >= kJackDisappearDistance &&
            !perception.player_sees_jack) {
            begin_cooldown(state, runtime, result, true);
            break;
        }

        runtime.repath_seconds =
            std::max(0.0F, runtime.repath_seconds - dt);
        if (runtime.repath_seconds <= 0.0F) {
            auto target =
                world_point_from_position(
                    state.last_seen_player_position);
            if (runtime.path.empty() &&
                horizontal_distance(
                    state.position,
                    state.last_seen_player_position) < 1.5F) {
                target = choose_wander_target(
                    state,
                    runtime.navigation,
                    state.last_seen_player_position,
                    12.0F);
            }
            assign_path(
                runtime,
                runtime.navigation,
                world_point_from_position(state.position),
                target,
                0.55F);
        }
        traveled = follow_path(
            state,
            runtime,
            generator,
            player.maximum_sprint_speed * 0.78F,
            dt);
        break;
    }
    case BackroomsJackPhase::Jumpscare:
        state.head_yaw_degrees =
            std::sin(state.phase_seconds * 22.0F) * 3.0F;
        break;
    case BackroomsJackPhase::Dormant:
    case BackroomsJackPhase::Cooldown:
        break;
    }
    return traveled;
}

void update_dormant_phase(
    BackroomsJackState& state,
    BackroomsJackRuntime& runtime,
    const BackroomsGenerator& generator,
    const BackroomsJackUpdateContext& context,
    const BackroomsJackPlayerContext& player,
    float dt) noexcept {
    state.spawn_check_seconds =
        std::max(0.0F, state.spawn_check_seconds - dt);
    if (!context.allow_spawn || state.spawn_check_seconds > 0.0F) {
        return;
    }

    // Je cherche d'abord une position réellement prête et cachée. Aucun tirage
    // de rencontre n'est consommé si le streaming n'offre aucun candidat.
    const auto selection = select_backrooms_jack_spawn(
        generator,
        runtime.navigation,
        player,
        context.chunk_readiness,
        state.random_state);
    if (!selection.found) {
        state.spawn_check_seconds = 1.0F;
        return;
    }

    auto working_random_state = selection.next_random_state;
    const auto should_spawn =
        random_unit(working_random_state) < 0.10F;
    state.random_state = working_random_state;
    if (!should_spawn) {
        state.spawn_check_seconds = random_range(
            state.random_state,
            45.0F,
            90.0F);
        return;
    }

    state.phase = BackroomsJackPhase::Wandering;
    state.position = selection.position;
    state.last_seen_player_position = player.feet_position;
    state.previous_player_position = player.feet_position;
    state.body_yaw_degrees = selection.body_yaw_degrees;
    state.head_yaw_degrees = 0.0F;
    state.hunch_ratio = selection.initial_hunch;
    state.motion_amount = 0.0F;
    state.phase_seconds = 0.0F;
    state.suspicion = 0.0F;
    state.lost_sight_seconds = 0.0F;
    state.unseen_travel_distance = 0.0F;
    state.footstep_distance = 0.0F;
    state.evaded_chunk_count = 0U;
    state.active = true;
    state.has_previous_player_position = true;
    state.has_last_evade_chunk = false;
    state.next_step_is_wooden = false;
    state.notice_event_emitted = false;
    state.chase_event_emitted = false;
    state.screamer_event_emitted = false;
    clear_navigation_path(runtime);
}

void simulate_fixed_step(
    BackroomsJackState& state,
    BackroomsJackRuntime& runtime,
    const BackroomsGenerator& generator,
    const BackroomsJackUpdateContext& context,
    const BackroomsJackPlayerContext& player,
    float dt,
    BackroomsJackUpdateResult& result) {
    if (state.phase == BackroomsJackPhase::Dormant) {
        update_dormant_phase(
            state,
            runtime,
            generator,
            context,
            player,
            dt);
        return;
    }
    if (state.phase == BackroomsJackPhase::Cooldown) {
        state.cooldown_seconds =
            std::max(0.0F, state.cooldown_seconds - dt);
        if (state.cooldown_seconds <= 0.0F) {
            state.phase = BackroomsJackPhase::Dormant;
            state.phase_seconds = 0.0F;
            state.spawn_check_seconds = random_range(
                state.random_state,
                45.0F,
                90.0F);
        }
        return;
    }

    state.active = true;
    state.phase_seconds += dt;
    const auto desired_hunch = target_hunch_for(state, runtime);
    state.hunch_ratio = approach(
        state.hunch_ratio,
        desired_hunch,
        2.4F * dt);

    const auto traveled = update_active_phase(
        state,
        runtime,
        generator,
        player,
        dt,
        result);
    emit_footsteps(state, result, traveled);

    auto motion_target = 0.0F;
    if (traveled > 0.00001F) {
        if (state.phase == BackroomsJackPhase::Chasing) {
            motion_target = 1.0F;
        } else if (state.phase == BackroomsJackPhase::Searching) {
            motion_target = 0.72F;
        } else {
            motion_target = 0.42F;
        }
    }
    state.motion_amount = approach(
        state.motion_amount,
        motion_target,
        6.0F * dt);

    if (traveled <= 0.00001F &&
        !runtime.path.empty() &&
        state.phase != BackroomsJackPhase::Watching &&
        state.phase != BackroomsJackPhase::Jumpscare) {
        runtime.stuck_seconds += dt;
        if (runtime.stuck_seconds >= 1.5F) {
            clear_navigation_path(runtime);
        }
    } else {
        runtime.stuck_seconds = 0.0F;
    }
}

}

auto initialize_backrooms_jack(
    std::uint32_t seed,
    std::int32_t logical_level) noexcept -> BackroomsJackState {
    BackroomsJackState state {};
    state.random_state =
        seed == 0U ? kFallbackRandomState : seed;
    state.spawn_check_seconds = random_range(
        state.random_state,
        120.0F,
        300.0F);
    state.logical_level =
        is_valid_backrooms_logical_level(logical_level)
            ? logical_level
            : 0;
    return state;
}

auto sanitize_backrooms_jack_state(
    const BackroomsJackState& state) noexcept -> BackroomsJackState {
    BackroomsJackState sanitized = state;
    const auto phase_value =
        static_cast<std::uint8_t>(sanitized.phase);
    if (phase_value >
        static_cast<std::uint8_t>(BackroomsJackPhase::Cooldown)) {
        sanitized.phase = BackroomsJackPhase::Dormant;
    }

    const glm::vec3 fallback_position {
        0.5F,
        static_cast<float>(kBackroomsFloorY + 1),
        0.5F,
    };
    sanitized.position = safe_vector(
        sanitized.position,
        fallback_position);
    sanitized.last_seen_player_position = safe_vector(
        sanitized.last_seen_player_position,
        sanitized.position);
    sanitized.previous_player_position = safe_vector(
        sanitized.previous_player_position,
        sanitized.last_seen_player_position);
    sanitized.body_yaw_degrees =
        wrap_degrees(sanitized.body_yaw_degrees);
    sanitized.head_yaw_degrees = clamp_finite(
        sanitized.head_yaw_degrees,
        0.0F,
        -45.0F,
        45.0F);
    sanitized.hunch_ratio = clamp_finite(
        sanitized.hunch_ratio,
        0.0F,
        0.0F,
        1.0F);
    sanitized.motion_amount = clamp_finite(
        sanitized.motion_amount,
        0.0F,
        0.0F,
        1.0F);
    sanitized.phase_seconds = clamp_finite(
        sanitized.phase_seconds,
        0.0F,
        0.0F,
        86400.0F);
    sanitized.suspicion = clamp_finite(
        sanitized.suspicion,
        0.0F,
        0.0F,
        kJackWatchingSuspicionThreshold);
    sanitized.lost_sight_seconds = clamp_finite(
        sanitized.lost_sight_seconds,
        0.0F,
        0.0F,
        86400.0F);
    sanitized.unseen_travel_distance = clamp_finite(
        sanitized.unseen_travel_distance,
        0.0F,
        0.0F,
        100000.0F);
    sanitized.spawn_check_seconds = clamp_finite(
        sanitized.spawn_check_seconds,
        180.0F,
        0.0F,
        3600.0F);
    sanitized.cooldown_seconds = clamp_finite(
        sanitized.cooldown_seconds,
        0.0F,
        0.0F,
        kBackroomsJackMaximumCooldownSeconds);
    sanitized.footstep_distance = clamp_finite(
        sanitized.footstep_distance,
        0.0F,
        0.0F,
        2.0F);
    sanitized.evaded_chunk_count = std::min(
        sanitized.evaded_chunk_count,
        sanitized.evaded_chunks.size());
    if (sanitized.random_state == 0U) {
        sanitized.random_state = kFallbackRandomState;
    }
    if (sanitized.next_event_sequence == 0U) {
        sanitized.next_event_sequence = 1U;
    }
    if (!is_valid_backrooms_logical_level(
            sanitized.logical_level)) {
        sanitized.logical_level = 0;
    }
    sanitized.active = phase_has_body(sanitized.phase);
    if (!sanitized.active) {
        sanitized.motion_amount = 0.0F;
    }
    return sanitized;
}

auto backrooms_jack_chunk_at(
    const glm::vec3& position) noexcept -> ChunkCoord {
    const auto safe = safe_vector(position, glm::vec3 {0.0F});
    return {
        floor_division(safe_floor_to_int(safe.x), kChunkSizeX),
        floor_division(safe_floor_to_int(safe.z), kChunkSizeZ),
    };
}

auto backrooms_jack_navigation_cell(
    const BackroomsJackNavigationGrid& grid,
    int world_x,
    int world_z) noexcept -> const BackroomsJackNavigationCell* {
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

auto build_backrooms_jack_navigation_grid(
    const BackroomsGenerator& generator,
    const ChunkCoord& center_chunk) noexcept
    -> BackroomsJackNavigationGrid {
    BackroomsJackNavigationGrid grid {};
    grid.center_chunk = center_chunk;
    grid.logical_level =
        generator_logical_level(generator);
    grid.origin_world_x = saturating_int(
        static_cast<std::int64_t>(center_chunk.x) * kChunkSizeX -
        kChunkSizeX);
    grid.origin_world_z = saturating_int(
        static_cast<std::int64_t>(center_chunk.z) * kChunkSizeZ -
        kChunkSizeZ);

    for (auto local_z = 0;
         local_z < kBackroomsJackNavigationSide;
         ++local_z) {
        for (auto local_x = 0;
             local_x < kBackroomsJackNavigationSide;
             ++local_x) {
            const auto world_x = saturating_int(
                static_cast<std::int64_t>(grid.origin_world_x) +
                local_x);
            const auto world_z = saturating_int(
                static_cast<std::int64_t>(grid.origin_world_z) +
                local_z);
            const auto column =
                generator.sample_column(world_x, world_z);
            const auto floor_y =
                static_cast<float>(column.floor_y + 1) + 0.001F;
            const auto clearance = static_cast<float>(
                column.ceiling_y - (column.floor_y + 1));
            auto& cell = grid.cells[local_index(local_x, local_z)];
            cell.floor_y = floor_y;
            cell.clearance = std::max(0.0F, clearance);
            cell.walkable =
                !column.wall &&
                clearance >= kBackroomsJackBentHeight + 0.02F;
            cell.standing_allowed =
                cell.walkable &&
                clearance >= kBackroomsJackStandingClearance;
            cell.in_water =
                cell.walkable &&
                generator_is_poolrooms(generator) &&
                generator_has_water_at(
                    generator,
                    world_x,
                    column.water_y,
                    world_z);
            cell.movement_speed_multiplier =
                cell.in_water
                    ? kBackroomsJackPoolroomsSpeedMultiplier
                    : 1.0F;
        }
    }
    return grid;
}

auto find_backrooms_jack_path(
    const BackroomsJackNavigationGrid& grid,
    BackroomsJackGridPoint start,
    BackroomsJackGridPoint goal) -> BackroomsJackPath {
    BackroomsJackPath path {};
    const auto start_index = nearest_walkable_index(grid, start);
    const auto goal_index = nearest_walkable_index(grid, goal);
    if (start_index < 0 || goal_index < 0) {
        return path;
    }

    constexpr auto unreachable = std::numeric_limits<int>::max();
    std::array<int, kBackroomsJackNavigationCellCount> costs {};
    std::array<int, kBackroomsJackNavigationCellCount> parents {};
    std::array<bool, kBackroomsJackNavigationCellCount> closed {};
    costs.fill(unreachable);
    parents.fill(-1);

    std::priority_queue<
        OpenNode,
        std::vector<OpenNode>,
        OpenNodeLater> open {};
    costs[static_cast<std::size_t>(start_index)] = 0;
    const auto first_heuristic =
        path_heuristic(start_index, goal_index);
    open.push({
        start_index,
        first_heuristic,
        first_heuristic,
    });

    constexpr std::array<std::pair<int, int>, 8> directions {{
        {0, -1},
        {-1, 0},
        {1, 0},
        {0, 1},
        {-1, -1},
        {1, -1},
        {-1, 1},
        {1, 1},
    }};

    while (!open.empty()) {
        const auto node = open.top();
        open.pop();
        const auto current_index = node.index;
        if (closed[static_cast<std::size_t>(current_index)]) {
            continue;
        }
        closed[static_cast<std::size_t>(current_index)] = true;
        if (current_index == goal_index) {
            break;
        }

        const auto current_x =
            current_index % kBackroomsJackNavigationSide;
        const auto current_z =
            current_index / kBackroomsJackNavigationSide;
        for (const auto& [delta_x, delta_z] : directions) {
            const auto next_x = current_x + delta_x;
            const auto next_z = current_z + delta_z;
            if (next_x < 0 ||
                next_x >= kBackroomsJackNavigationSide ||
                next_z < 0 ||
                next_z >= kBackroomsJackNavigationSide) {
                continue;
            }
            const auto next_index =
                next_z * kBackroomsJackNavigationSide + next_x;
            if (!grid.cells[static_cast<std::size_t>(next_index)]
                     .walkable) {
                continue;
            }
            // Je refuse une diagonale si l'un des deux passages cardinaux est
            // fermé, ce qui empêche de couper à travers l'angle d'un mur.
            if (delta_x != 0 && delta_z != 0) {
                const auto horizontal_index =
                    current_z * kBackroomsJackNavigationSide +
                    current_x + delta_x;
                const auto vertical_index =
                    (current_z + delta_z) *
                        kBackroomsJackNavigationSide +
                    current_x;
                if (!grid.cells[
                         static_cast<std::size_t>(horizontal_index)]
                         .walkable ||
                    !grid.cells[
                         static_cast<std::size_t>(vertical_index)]
                         .walkable) {
                    continue;
                }
            }

            const auto movement_cost =
                delta_x != 0 && delta_z != 0 ? 14 : 10;
            const auto current_cost =
                costs[static_cast<std::size_t>(current_index)];
            if (current_cost >
                unreachable - movement_cost) {
                continue;
            }
            const auto candidate_cost =
                current_cost + movement_cost;
            auto& next_cost =
                costs[static_cast<std::size_t>(next_index)];
            if (candidate_cost >= next_cost) {
                continue;
            }
            next_cost = candidate_cost;
            parents[static_cast<std::size_t>(next_index)] =
                current_index;
            const auto heuristic =
                path_heuristic(next_index, goal_index);
            open.push({
                next_index,
                candidate_cost + heuristic,
                heuristic,
            });
        }
    }

    if (!closed[static_cast<std::size_t>(goal_index)]) {
        return path;
    }

    std::array<int, kBackroomsJackNavigationCellCount> reverse {};
    auto reverse_count = std::size_t {0U};
    for (auto index = goal_index;
         index >= 0 && reverse_count < reverse.size();
         index = parents[static_cast<std::size_t>(index)]) {
        reverse[reverse_count] = index;
        ++reverse_count;
        if (index == start_index) {
            break;
        }
    }
    if (reverse_count == 0U ||
        reverse[reverse_count - 1U] != start_index) {
        return {};
    }

    path.count = reverse_count;
    for (std::size_t index = 0U;
         index < reverse_count;
         ++index) {
        path.nodes[index] = cell_world_point(
            grid,
            reverse[reverse_count - 1U - index]);
    }
    path.cursor = path.count > 1U ? 1U : path.count;
    return path;
}

auto backrooms_jack_has_line_of_sight(
    const BackroomsGenerator& generator,
    const glm::vec3& from,
    const glm::vec3& to) noexcept -> bool {
    if (!finite_vector(from) || !finite_vector(to)) {
        return false;
    }
    const auto delta = to - from;
    const auto distance = glm::length(delta);
    if (!std::isfinite(distance)) {
        return false;
    }
    if (distance <= 0.001F) {
        return true;
    }
    // Je parcours le générateur lui-même : la vision reste correcte même si
    // le chunk portant le mur n'est pas encore chargé par le monde courant.
    const auto requested_steps = std::ceil(
        static_cast<double>(distance) / 0.16);
    const auto step_count = static_cast<int>(std::clamp(
        requested_steps,
        1.0,
        4096.0));
    for (auto step = 1; step < step_count; ++step) {
        const auto ratio =
            static_cast<float>(step) /
            static_cast<float>(step_count);
        const auto point = from + delta * ratio;
        const auto block = generator.sample_block(
            safe_floor_to_int(point.x),
            safe_floor_to_int(point.y),
            safe_floor_to_int(point.z));
        if (is_block_opaque(block)) {
            return false;
        }
    }
    return true;
}

auto evaluate_backrooms_jack_perception(
    const BackroomsGenerator& generator,
    const BackroomsJackPlayerContext& player,
    const glm::vec3& jack_position,
    float jack_body_yaw_degrees,
    float jack_hunch_ratio) noexcept -> BackroomsJackPerception {
    const auto safe_player = sanitize_player_context(player);
    const auto safe_jack = safe_vector(
        jack_position,
        glm::vec3 {
            0.5F,
            static_cast<float>(kBackroomsFloorY + 1),
            0.5F,
        });
    const auto jack_eye =
        jack_eye_position(safe_jack, jack_hunch_ratio);
    const auto player_chest =
        safe_player.feet_position +
        glm::vec3 {0.0F, 1.05F, 0.0F};
    const auto jack_to_player =
        safe_player.eye_position - jack_eye;
    const auto player_to_jack =
        jack_eye - safe_player.eye_position;
    const auto distance =
        horizontal_distance(safe_jack, safe_player.feet_position);
    const auto jack_direction = safe_horizontal_direction(
        jack_to_player,
        glm::vec3 {0.0F, 0.0F, -1.0F});
    const auto jack_forward =
        forward_from_yaw(wrap_degrees(jack_body_yaw_degrees));
    const auto player_direction = safe_direction(
        player_to_jack,
        safe_player.look_direction);
    const auto jack_front_dot =
        glm::dot(jack_forward, jack_direction);
    const auto player_front_dot =
        glm::dot(safe_player.look_direction, player_direction);
    const auto clear_to_eye = backrooms_jack_has_line_of_sight(
        generator,
        jack_eye,
        safe_player.eye_position);
    const auto clear_to_chest = backrooms_jack_has_line_of_sight(
        generator,
        jack_eye,
        player_chest);
    const auto line_of_sight = clear_to_eye || clear_to_chest;
    const auto jack_fov_threshold =
        std::cos(55.0F * kPi / 180.0F);
    const auto player_fov_threshold =
        std::cos(55.0F * kPi / 180.0F);
    const auto player_face_threshold =
        std::cos(35.0F * kPi / 180.0F);

    return {
        distance,
        jack_front_dot,
        player_front_dot,
        line_of_sight,
        distance <= kJackMaximumSightDistance &&
            jack_front_dot >= jack_fov_threshold &&
            line_of_sight,
        distance <= kJackMaximumPlayerViewDistance &&
            player_front_dot >= player_fov_threshold &&
            line_of_sight,
        distance <= kJackMaximumPlayerViewDistance &&
            player_front_dot >= player_face_threshold &&
            line_of_sight,
    };
}

auto select_backrooms_jack_spawn(
    const BackroomsGenerator& generator,
    const BackroomsJackNavigationGrid& grid,
    const BackroomsJackPlayerContext& player,
    const BackroomsJackChunkReadiness& readiness,
    std::uint32_t random_state) noexcept
    -> BackroomsJackSpawnSelection {
    BackroomsJackSpawnSelection selection {};
    const auto initial_random_state =
        random_state == 0U ? kFallbackRandomState : random_state;
    selection.next_random_state = initial_random_state;
    if (!chunks_equal(grid.center_chunk, readiness.center_chunk)) {
        return selection;
    }

    const auto safe_player = sanitize_player_context(player);
    const auto player_point =
        world_point_from_position(safe_player.feet_position);
    const auto player_index =
        nearest_walkable_index(grid, player_point);
    if (player_index < 0) {
        return selection;
    }

    // Je construis une composante connexe avant le choix aléatoire pour ne
    // jamais faire apparaître Jack dans une poche de salle inaccessible.
    std::array<bool, kBackroomsJackNavigationCellCount> reachable {};
    std::array<int, kBackroomsJackNavigationCellCount> pending {};
    auto read_cursor = std::size_t {0U};
    auto write_cursor = std::size_t {1U};
    pending[0] = player_index;
    reachable[static_cast<std::size_t>(player_index)] = true;
    constexpr std::array<std::pair<int, int>, 4> directions {{
        {0, -1},
        {-1, 0},
        {1, 0},
        {0, 1},
    }};
    while (read_cursor < write_cursor) {
        const auto current = pending[read_cursor];
        ++read_cursor;
        const auto current_x =
            current % kBackroomsJackNavigationSide;
        const auto current_z =
            current / kBackroomsJackNavigationSide;
        for (const auto& [delta_x, delta_z] : directions) {
            const auto next_x = current_x + delta_x;
            const auto next_z = current_z + delta_z;
            if (next_x < 0 ||
                next_x >= kBackroomsJackNavigationSide ||
                next_z < 0 ||
                next_z >= kBackroomsJackNavigationSide) {
                continue;
            }
            const auto next =
                next_z * kBackroomsJackNavigationSide + next_x;
            const auto index = static_cast<std::size_t>(next);
            if (reachable[index] || !grid.cells[index].walkable) {
                continue;
            }
            reachable[index] = true;
            pending[write_cursor] = next;
            ++write_cursor;
        }
    }

    auto working_random_state = initial_random_state;
    auto eligible_count = std::uint32_t {0U};
    auto selected_index = -1;
    for (std::size_t index = 0U; index < grid.cells.size(); ++index) {
        const auto& cell = grid.cells[index];
        if (!reachable[index] || !cell.walkable) {
            continue;
        }
        const auto point =
            cell_world_point(grid, static_cast<int>(index));
        const ChunkCoord chunk {
            floor_division(point.x, kChunkSizeX),
            floor_division(point.z, kChunkSizeZ),
        };
        if (chunks_equal(chunk, grid.center_chunk) ||
            !readiness_at(readiness, chunk)) {
            continue;
        }
        const glm::vec3 candidate {
            static_cast<float>(point.x) + 0.5F,
            cell.floor_y,
            static_cast<float>(point.z) + 0.5F,
        };
        const auto distance =
            horizontal_distance(candidate, safe_player.feet_position);
        if (distance < kBackroomsJackMinimumSpawnDistance ||
            distance > kBackroomsJackMaximumSpawnDistance) {
            continue;
        }
        const auto hunch = cell.standing_allowed ? 0.0F : 1.0F;
        if (player_can_see_candidate(
                generator,
                safe_player,
                candidate,
                hunch,
                80.0F)) {
            continue;
        }

        ++eligible_count;
        if (next_random(working_random_state) % eligible_count == 0U) {
            selected_index = static_cast<int>(index);
        }
    }
    if (selected_index < 0) {
        return selection;
    }

    const auto selected_point =
        cell_world_point(grid, selected_index);
    const auto& selected_cell =
        grid.cells[static_cast<std::size_t>(selected_index)];
    selection.position = {
        static_cast<float>(selected_point.x) + 0.5F,
        selected_cell.floor_y,
        static_cast<float>(selected_point.z) + 0.5F,
    };
    selection.body_yaw_degrees =
        random_range(working_random_state, -180.0F, 180.0F);
    selection.initial_hunch =
        selected_cell.standing_allowed ? 0.0F : 1.0F;
    selection.next_random_state = working_random_state;
    selection.found = true;
    return selection;
}

auto make_backrooms_jack_render_view(
    const BackroomsJackState& state) noexcept -> BackroomsJackRenderView {
    const auto sanitized = sanitize_backrooms_jack_state(state);
    auto motion = BackroomsJackMotion::Idle;
    if (sanitized.motion_amount > 0.02F) {
        motion =
            sanitized.phase == BackroomsJackPhase::Chasing
                ? BackroomsJackMotion::Running
                : BackroomsJackMotion::Walking;
    }
    return {
        .position = sanitized.position,
        .body_yaw_degrees = sanitized.body_yaw_degrees,
        .head_yaw_degrees = sanitized.head_yaw_degrees,
        .hunch_ratio = sanitized.hunch_ratio,
        .motion_amount = sanitized.motion_amount,
        .sky_light = 0.0F,
        .block_light = 0.0F,
        .motion = motion,
        .visible = sanitized.active,
        .chasing = sanitized.phase == BackroomsJackPhase::Chasing ||
            sanitized.phase == BackroomsJackPhase::Searching,
        .jumpscare = sanitized.phase == BackroomsJackPhase::Jumpscare,
        .animation_time_seconds = sanitized.phase_seconds,
    };
}

auto make_backrooms_jack_render_view(
    const BackroomsJackState& state,
    std::int32_t logical_level) noexcept -> BackroomsJackRenderView {
    auto view = make_backrooms_jack_render_view(state);
    if (!is_valid_backrooms_logical_level(logical_level) ||
        state.logical_level != logical_level) {
        view.visible = false;
        view.chasing = false;
        view.jumpscare = false;
    }
    return view;
}

auto make_backrooms_jack_light_interference_view(
    const BackroomsJackState& state) noexcept
    -> BackroomsJackLightInterferenceView {
    const auto sanitized = sanitize_backrooms_jack_state(state);
    if (!sanitized.active) {
        return {};
    }
    auto base_intensity = 0.35F;
    auto radius = 12.0F;
    switch (sanitized.phase) {
    case BackroomsJackPhase::Watching:
        base_intensity = 0.72F;
        radius = 16.0F;
        break;
    case BackroomsJackPhase::Chasing:
        base_intensity = 1.0F;
        radius = 20.0F;
        break;
    case BackroomsJackPhase::Searching:
        base_intensity = 0.68F;
        radius = 17.0F;
        break;
    case BackroomsJackPhase::Jumpscare:
        base_intensity = 1.0F;
        radius = 24.0F;
        break;
    case BackroomsJackPhase::Wandering:
        break;
    case BackroomsJackPhase::Dormant:
    case BackroomsJackPhase::Cooldown:
        return {};
    }
    const auto pulse =
        0.78F +
        std::abs(std::sin(sanitized.phase_seconds * 9.7F)) * 0.22F;
    return {
        sanitized.position,
        radius,
        std::clamp(base_intensity * pulse, 0.0F, 1.0F),
        true,
    };
}

auto make_backrooms_jack_light_interference_view(
    const BackroomsJackState& state,
    std::int32_t logical_level) noexcept
    -> BackroomsJackLightInterferenceView {
    if (!is_valid_backrooms_logical_level(logical_level) ||
        state.logical_level != logical_level) {
        return {};
    }
    return make_backrooms_jack_light_interference_view(
        state);
}

auto update_backrooms_jack(
    BackroomsJackState& state,
    BackroomsJackRuntime& runtime,
    const BackroomsGenerator& generator,
    const BackroomsJackUpdateContext& context,
    float dt) -> BackroomsJackUpdateResult {
    BackroomsJackUpdateResult result {};
    state = sanitize_backrooms_jack_state(state);
    const auto player = sanitize_player_context(context.player);
    const auto logical_level =
        generator_logical_level(generator);

    if (state.logical_level != logical_level) {
        // Je retire le corps de l'ancien étage avant de rattacher Jack au
        // niveau courant. Son délai empêche toute apparition sous les yeux.
        if (state.active) {
            state.phase = BackroomsJackPhase::Cooldown;
            state.active = false;
            state.motion_amount = 0.0F;
            state.suspicion = 0.0F;
            state.lost_sight_seconds = 0.0F;
            state.cooldown_seconds = random_range(
                state.random_state,
                kBackroomsJackMinimumCooldownSeconds,
                kBackroomsJackMaximumCooldownSeconds);
        }
        state.logical_level = logical_level;
        runtime.navigation_valid = false;
        runtime.fixed_step_accumulator = 0.0F;
        clear_navigation_path(runtime);
    }
    if (!std::isfinite(runtime.fixed_step_accumulator) ||
        runtime.fixed_step_accumulator < 0.0F ||
        runtime.fixed_step_accumulator > 1.0F) {
        runtime.fixed_step_accumulator = 0.0F;
    }
    if (!std::isfinite(runtime.repath_seconds) ||
        runtime.repath_seconds < 0.0F) {
        runtime.repath_seconds = 0.0F;
    }
    if (!std::isfinite(runtime.stuck_seconds) ||
        runtime.stuck_seconds < 0.0F) {
        runtime.stuck_seconds = 0.0F;
    }

    const auto player_chunk =
        backrooms_jack_chunk_at(player.feet_position);
    if (!runtime.navigation_valid ||
        !chunks_equal(
            runtime.navigation.center_chunk,
            player_chunk) ||
        runtime.navigation.logical_level !=
            logical_level) {
        runtime.navigation =
            build_backrooms_jack_navigation_grid(
                generator,
                player_chunk);
        runtime.navigation_valid = true;
        clear_navigation_path(runtime);
    }

    if (context.simulation_frozen ||
        !context.player_alive ||
        !std::isfinite(dt) ||
        dt <= 0.0F) {
        result.render = make_backrooms_jack_render_view(
            state,
            logical_level);
        result.light_interference =
            make_backrooms_jack_light_interference_view(
                state,
                logical_level);
        return result;
    }

    // Je simule toujours à 120 Hz afin que la vitesse, la suspicion et les pas
    // restent identiques à 30, 60 ou 144 images par seconde.
    runtime.fixed_step_accumulator +=
        std::min(dt, kMaximumAcceptedDeltaSeconds);
    auto fixed_step_count = 0;
    while (runtime.fixed_step_accumulator + 0.0000001F >=
               kFixedStepSeconds &&
           fixed_step_count < 30) {
        runtime.fixed_step_accumulator -= kFixedStepSeconds;
        simulate_fixed_step(
            state,
            runtime,
            generator,
            context,
            player,
            kFixedStepSeconds,
            result);
        ++fixed_step_count;
    }
    runtime.fixed_step_accumulator = std::clamp(
        runtime.fixed_step_accumulator,
        0.0F,
        kFixedStepSeconds);
    state = sanitize_backrooms_jack_state(state);
    runtime.last_simulated_position = state.position;
    result.render = make_backrooms_jack_render_view(
        state,
        logical_level);
    result.light_interference =
        make_backrooms_jack_light_interference_view(
            state,
            logical_level);
    return result;
}

auto make_backrooms_jack_smoke_preview(
    BackroomsJackSmokePose pose,
    std::uint32_t seed) noexcept
    -> BackroomsJackSmokePreview {
    auto state = initialize_backrooms_jack(seed);
    state.active = true;
    state.position = {
        0.5F,
        static_cast<float>(kBackroomsFloorY + 1) + 0.001F,
        -8.5F,
    };
    state.body_yaw_degrees = 180.0F;
    state.phase_seconds = 1.25F;
    state.spawn_check_seconds = 0.0F;

    switch (pose) {
    case BackroomsJackSmokePose::Standing:
        state.phase = BackroomsJackPhase::Wandering;
        state.hunch_ratio = 0.0F;
        state.motion_amount = 0.58F;
        break;
    case BackroomsJackSmokePose::Bent:
        state.phase = BackroomsJackPhase::Wandering;
        state.hunch_ratio = 1.0F;
        state.motion_amount = 0.58F;
        break;
    case BackroomsJackSmokePose::Watching:
        state.phase = BackroomsJackPhase::Watching;
        state.hunch_ratio = 0.0F;
        state.head_yaw_degrees = 11.0F;
        state.motion_amount = 0.0F;
        break;
    case BackroomsJackSmokePose::Chasing:
        state.phase = BackroomsJackPhase::Chasing;
        state.hunch_ratio = 0.15F;
        state.motion_amount = 1.0F;
        break;
    case BackroomsJackSmokePose::Jumpscare:
        state.phase = BackroomsJackPhase::Jumpscare;
        state.position = {
            0.0F,
            static_cast<float>(kBackroomsFloorY + 1),
            -0.65F,
        };
        state.hunch_ratio = 0.35F;
        state.motion_amount = 0.0F;
        state.screamer_event_emitted = true;
        break;
    }
    state = sanitize_backrooms_jack_state(state);
    return {
        state,
        make_backrooms_jack_render_view(state),
        make_backrooms_jack_light_interference_view(state),
    };
}

}
