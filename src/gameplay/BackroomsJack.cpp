#include "gameplay/BackroomsJack.h"
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

constexpr float kFixedStepSeconds = 1.0F / 120.0F;
constexpr float kMaximumAcceptedDeltaSeconds = 0.25F;
constexpr float kJackWanderSpeed = 1.25F;
constexpr float kJackWatchingSuspicionThreshold = 3.0F;
constexpr float kJackChaseSpeedMultiplier = 1.08F;
constexpr float kJackSearchSpeedMultiplier = 0.88F;
constexpr float kJackSearchDelaySeconds = 0.75F;
constexpr float kJackDisappearDelaySeconds = 10.0F;
constexpr float kJackDisappearDistance = 24.0F;
constexpr float kJackCatchDistance = 1.10F;
constexpr float kJackFootprintRadius = 0.42F;
constexpr float kJackCorridorMinimumDistance = 32.0F;
constexpr float kJackCorridorMaximumDistance = 52.0F;
constexpr float kJackRearMinimumDistance = 18.0F;
constexpr float kJackRearMaximumDistance = 30.0F;
constexpr float kJackStuckRecenteringSeconds = 0.55F;
constexpr float kJackStuckShadowStepSeconds = 1.80F;
constexpr float kJackStuckDespawnSeconds = 3.50F;
constexpr float kJackHiddenHuntMinimumDistance = 12.0F;
constexpr float kJackHiddenHuntMaximumDistance = 24.0F;
constexpr float kJackHiddenHuntRepositionMinimumSeconds = 8.0F;
constexpr float kJackHiddenHuntRepositionMaximumSeconds = 12.0F;
constexpr float kJackHiddenHuntRepositionTriggerDistance = 36.0F;
constexpr float kJackHiddenHuntRepositionMinimumDistance = 18.0F;
constexpr float kJackHiddenHuntRepositionMaximumDistance = 28.0F;
constexpr float kJackLevelTransitionGraceMinimumSeconds = 12.0F;
constexpr float kJackLevelTransitionGraceMaximumSeconds = 20.0F;
constexpr float kJackVisibleDeadlineGuardSeconds = 5.0F;
constexpr std::size_t kJackMaximumVisibleCandidateProbes = 64U;
constexpr float kJackCorridorMarginalChaseProbability = 0.25F;
constexpr float kJackRearMarginalChaseProbability = 0.35F;
constexpr float kJackHiddenMarginalChaseProbability = 0.30F;
constexpr float kJackHiddenToCorridorProbability = 0.60F;
constexpr float kJackHiddenToRearProbability = 0.40F;
constexpr float kJackCorridorToHiddenProbability = 0.375F;
constexpr float kJackCorridorToRearProbability = 0.625F;
constexpr float kJackRearToHiddenProbability = 2.0F / 7.0F;
constexpr float kJackRearToCorridorProbability = 5.0F / 7.0F;
constexpr float kJackPreviousChaseGivenHidden =
    (0.40F * kJackCorridorToHiddenProbability *
         kJackCorridorMarginalChaseProbability +
     0.35F * kJackRearToHiddenProbability *
         kJackRearMarginalChaseProbability) /
    0.25F;
constexpr float kJackPreviousChaseGivenCorridor =
    (0.25F * kJackHiddenToCorridorProbability *
         kJackHiddenMarginalChaseProbability +
     0.35F * kJackRearToCorridorProbability *
         kJackRearMarginalChaseProbability) /
    0.40F;
constexpr float kJackPreviousChaseGivenRear =
    (0.25F * kJackHiddenToRearProbability *
         kJackHiddenMarginalChaseProbability +
     0.40F * kJackCorridorToRearProbability *
         kJackCorridorMarginalChaseProbability) /
    0.35F;
constexpr float kJackCorridorUnlockedChaseProbability =
    kJackCorridorMarginalChaseProbability /
    (1.0F - kJackPreviousChaseGivenCorridor);
constexpr float kJackRearUnlockedChaseProbability =
    kJackRearMarginalChaseProbability /
    (1.0F - kJackPreviousChaseGivenRear);
constexpr float kJackHiddenUnlockedChaseProbability =
    kJackHiddenMarginalChaseProbability /
    (1.0F - kJackPreviousChaseGivenHidden);
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

struct JackSpatialQuery {
    const World* world = nullptr;
    const BackroomsJackChunkReadiness* readiness = nullptr;
    int world_y_offset = 0;
};

[[nodiscard]] auto translated_world_y(
    int local_y,
    int world_y_offset) noexcept -> int {
    return static_cast<int>(std::clamp<std::int64_t>(
        static_cast<std::int64_t>(local_y) +
            static_cast<std::int64_t>(world_y_offset),
        static_cast<std::int64_t>(std::numeric_limits<int>::lowest()),
        static_cast<std::int64_t>(std::numeric_limits<int>::max())));
}

[[nodiscard]] auto sample_jack_block(
    const BackroomsGenerator& generator,
    const JackSpatialQuery& spatial,
    int world_x,
    int local_y,
    int world_z) -> BlockId {
    if (spatial.world != nullptr) {
        return spatial.world->peek_block_or_generated(
            world_x,
            translated_world_y(local_y, spatial.world_y_offset),
            world_z);
    }
    return generator.sample_block(world_x, local_y, world_z);
}

[[nodiscard]] auto sample_jack_water_level(
    const BackroomsGenerator& generator,
    const JackSpatialQuery& spatial,
    int world_x,
    int local_y,
    int world_z) -> std::uint8_t {
    if (spatial.world != nullptr) {
        return spatial.world->peek_water_level_or_generated(
            world_x,
            translated_world_y(local_y, spatial.world_y_offset),
            world_z);
    }
    return water_level_from_state(
        generator.sample_water_state(world_x, local_y, world_z));
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
    const auto delta_x =
        static_cast<double>(first.x) - second.x;
    const auto delta_z =
        static_cast<double>(first.z) - second.z;
    const auto distance = std::hypot(delta_x, delta_z);
    return static_cast<float>(std::min(
        distance,
        static_cast<double>(std::numeric_limits<float>::max())));
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

[[nodiscard]] auto sanitize_maximum_visible_distance(
    float value) noexcept -> float {
    return clamp_finite(
        value,
        kBackroomsJackDefaultMaximumVisibleDistance,
        0.0F,
        kBackroomsJackDefaultMaximumVisibleDistance);
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

[[nodiscard]] constexpr auto checked_int(std::int64_t value) noexcept
    -> std::optional<int> {
    if (value < std::numeric_limits<int>::lowest() ||
        value > std::numeric_limits<int>::max()) {
        return std::nullopt;
    }
    return static_cast<int>(value);
}

[[nodiscard]] constexpr auto navigation_grid_domain_is_valid(
    const BackroomsJackNavigationGrid& grid) noexcept -> bool {
    return static_cast<std::int64_t>(grid.origin_world_x) +
                   kBackroomsJackNavigationSide - 1 <=
               std::numeric_limits<int>::max() &&
           static_cast<std::int64_t>(grid.origin_world_z) +
                   kBackroomsJackNavigationSide - 1 <=
               std::numeric_limits<int>::max();
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
        static_cast<int>(
            static_cast<std::int64_t>(grid.origin_world_x) + local_x),
        static_cast<int>(
            static_cast<std::int64_t>(grid.origin_world_z) + local_z),
    };
}

[[nodiscard]] auto point_index(
    const BackroomsJackNavigationGrid& grid,
    BackroomsJackGridPoint point) noexcept -> int {
    if (!navigation_grid_domain_is_valid(grid)) {
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
    return local_z * kBackroomsJackNavigationSide + local_x;
}

[[nodiscard]] auto nearest_walkable_index(
    const BackroomsJackNavigationGrid& grid,
    BackroomsJackGridPoint point) noexcept -> int {
    if (!navigation_grid_domain_is_valid(grid)) {
        return -1;
    }
    const auto exact = point_index(grid, point);
    if (exact >= 0 &&
        grid.cells[static_cast<std::size_t>(exact)].walkable) {
        return exact;
    }

    auto best_index = -1;
    auto best_distance = std::numeric_limits<double>::infinity();
    for (std::size_t index = 0U; index < grid.cells.size(); ++index) {
        if (!grid.cells[index].walkable) {
            continue;
        }
        const auto candidate =
            cell_world_point(grid, static_cast<int>(index));
        const auto delta_x =
            static_cast<double>(candidate.x) - point.x;
        const auto delta_z =
            static_cast<double>(candidate.z) - point.z;
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

[[nodiscard]] auto scramble_random_state(std::uint32_t state) noexcept
    -> std::uint32_t {
    if (state == 0U) {
        state = kFallbackRandomState;
    }
    // Je casse la correlation des petites graines avant un tirage pondere.
    // Un xorshift brut commencerait sinon presque toujours par HiddenHunt
    // pour les anciennes sauvegardes dont l'etat aleatoire est faible.
    state += 0x9E3779B9U;
    state = (state ^ (state >> 16U)) * 0x7FEB352DU;
    state = (state ^ (state >> 15U)) * 0x846CA68BU;
    state ^= state >> 16U;
    return state == 0U ? kFallbackRandomState : state;
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

[[nodiscard]] auto encounter_mode_order_impl(
    std::uint32_t initial_random_state,
    bool has_previous_mode,
    BackroomsJackEncounterMode previous_mode,
    bool force_visible) noexcept -> BackroomsJackEncounterModeOrder {
    auto working_random_state = scramble_random_state(initial_random_state);
    if (static_cast<std::uint8_t>(previous_mode) >
        static_cast<std::uint8_t>(
            BackroomsJackEncounterMode::RearStare)) {
        has_previous_mode = false;
    }
    constexpr std::array<BackroomsJackEncounterMode, 3> modes {{
        BackroomsJackEncounterMode::HiddenHunt,
        BackroomsJackEncounterMode::CorridorStare,
        BackroomsJackEncounterMode::RearStare,
    }};
    std::array<float, 3> weights {{0.25F, 0.40F, 0.35F}};
    if (has_previous_mode) {
        switch (previous_mode) {
        case BackroomsJackEncounterMode::HiddenHunt:
            weights = {{
                0.0F,
                kJackHiddenToCorridorProbability,
                kJackHiddenToRearProbability,
            }};
            break;
        case BackroomsJackEncounterMode::CorridorStare:
            weights = {{
                kJackCorridorToHiddenProbability,
                0.0F,
                kJackCorridorToRearProbability,
            }};
            break;
        case BackroomsJackEncounterMode::RearStare:
            weights = {{
                kJackRearToHiddenProbability,
                kJackRearToCorridorProbability,
                0.0F,
            }};
            break;
        }
    }
    if (force_visible) {
        weights[static_cast<std::size_t>(
            BackroomsJackEncounterMode::HiddenHunt)] = 0.0F;
    }

    BackroomsJackEncounterModeOrder order {};
    std::array<bool, 3> mode_used {};
    for (auto slot = std::size_t {0U}; slot < order.modes.size(); ++slot) {
        auto total_weight = 0.0F;
        for (auto index = std::size_t {0U}; index < modes.size(); ++index) {
            const auto excluded_previous =
                has_previous_mode && slot < 2U &&
                modes[index] == previous_mode;
            if (!mode_used[index] && !excluded_previous) {
                total_weight += weights[index];
            }
        }
        auto roll = random_unit(working_random_state) * total_weight;
        auto selected_mode_index = modes.size();
        for (auto index = std::size_t {0U}; index < modes.size(); ++index) {
            const auto excluded_previous =
                has_previous_mode && slot < 2U &&
                modes[index] == previous_mode;
            if (mode_used[index] || excluded_previous) {
                continue;
            }
            selected_mode_index = index;
            if (roll <= weights[index]) {
                break;
            }
            roll -= weights[index];
        }
        if (selected_mode_index >= modes.size()) {
            for (auto index = std::size_t {0U}; index < modes.size(); ++index) {
                if (!mode_used[index]) {
                    selected_mode_index = index;
                    break;
                }
            }
        }
        mode_used[selected_mode_index] = true;
        order.modes[slot] = modes[selected_mode_index];
    }
    order.next_random_state = working_random_state;
    return order;
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
    if (delta_x < -kBackroomsJackReadinessChunkRadius ||
        delta_x > kBackroomsJackReadinessChunkRadius ||
        delta_z < -kBackroomsJackReadinessChunkRadius ||
        delta_z > kBackroomsJackReadinessChunkRadius) {
        return false;
    }
    const auto index =
        static_cast<std::size_t>(
            (delta_z + kBackroomsJackReadinessChunkRadius) *
                kBackroomsJackReadinessChunkSide +
            delta_x + kBackroomsJackReadinessChunkRadius);
    return readiness.ready[index] &&
           readiness.mesh_revisions[index] != 0U;
}

struct BackroomsJackReadinessRevision {
    std::uint64_t mesh_revision = 0U;
    bool ready = false;
};

[[nodiscard]] auto readiness_revision_at(
    const BackroomsJackChunkReadiness& readiness,
    const ChunkCoord& chunk) noexcept
    -> BackroomsJackReadinessRevision {
    const auto delta_x =
        static_cast<std::int64_t>(chunk.x) -
        static_cast<std::int64_t>(readiness.center_chunk.x);
    const auto delta_z =
        static_cast<std::int64_t>(chunk.z) -
        static_cast<std::int64_t>(readiness.center_chunk.z);
    if (delta_x < -kBackroomsJackReadinessChunkRadius ||
        delta_x > kBackroomsJackReadinessChunkRadius ||
        delta_z < -kBackroomsJackReadinessChunkRadius ||
        delta_z > kBackroomsJackReadinessChunkRadius) {
        return {};
    }
    const auto index =
        static_cast<std::size_t>(
            (delta_z + kBackroomsJackReadinessChunkRadius) *
                kBackroomsJackReadinessChunkSide +
            delta_x + kBackroomsJackReadinessChunkRadius);
    const auto revision = readiness.mesh_revisions[index];
    return {
        .mesh_revision = revision,
        .ready = readiness.ready[index] && revision != 0U,
    };
}

[[nodiscard]] auto navigation_readiness_equal(
    const BackroomsJackChunkReadiness& first,
    const BackroomsJackChunkReadiness& second,
    const ChunkCoord& navigation_center) noexcept -> bool {
    // Je ne reconstruis l'A* que si l'un de ses 25 chunks change. L'anneau
    // externe 7x7 reste suivi pour les sightings, mais sa publication ne doit
    // pas relancer les milliers de sondes de collision de la grille 5x5.
    for (auto delta_z = -kBackroomsJackNavigationChunkRadius;
         delta_z <= kBackroomsJackNavigationChunkRadius;
         ++delta_z) {
        for (auto delta_x = -kBackroomsJackNavigationChunkRadius;
             delta_x <= kBackroomsJackNavigationChunkRadius;
             ++delta_x) {
            const auto chunk_x =
                static_cast<std::int64_t>(navigation_center.x) + delta_x;
            const auto chunk_z =
                static_cast<std::int64_t>(navigation_center.z) + delta_z;
            if (chunk_x < std::numeric_limits<int>::lowest() ||
                chunk_x > std::numeric_limits<int>::max() ||
                chunk_z < std::numeric_limits<int>::lowest() ||
                chunk_z > std::numeric_limits<int>::max()) {
                return false;
            }
            const ChunkCoord chunk {
                static_cast<int>(chunk_x),
                static_cast<int>(chunk_z),
            };
            const auto first_revision =
                readiness_revision_at(first, chunk);
            const auto second_revision =
                readiness_revision_at(second, chunk);
            if (first_revision.ready != second_revision.ready ||
                (first_revision.ready &&
                 first_revision.mesh_revision !=
                     second_revision.mesh_revision)) {
                return false;
            }
        }
    }
    return true;
}

void mask_unready_navigation_cells(
    BackroomsJackNavigationGrid& grid,
    const BackroomsJackChunkReadiness& readiness) noexcept {
    for (std::size_t index = 0U; index < grid.cells.size(); ++index) {
        const auto point =
            cell_world_point(grid, static_cast<int>(index));
        const ChunkCoord chunk {
            floor_division(point.x, kChunkSizeX),
            floor_division(point.z, kChunkSizeZ),
        };
        if (readiness_at(readiness, chunk)) {
            continue;
        }
        // Je ferme aussi le graphe A* sur les chunks non mailles : Jack ne
        // peut ainsi ni les traverser, ni s'y teleporter pour se debloquer.
        auto& cell = grid.cells[index];
        cell.walkable = false;
        cell.standing_allowed = false;
        cell.in_water = false;
        cell.movement_speed_multiplier = 1.0F;
    }
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
    const auto eye_height = 4.08F + (2.88F - 4.08F) * hunch;
    return position + glm::vec3 {0.0F, eye_height, 0.0F};
}

[[nodiscard]] auto backrooms_jack_has_line_of_sight_impl(
    const BackroomsGenerator& generator,
    const JackSpatialQuery& spatial,
    const glm::vec3& from,
    const glm::vec3& to) -> bool;

[[nodiscard]] auto evaluate_backrooms_jack_perception_impl(
    const BackroomsGenerator& generator,
    const JackSpatialQuery& spatial,
    const BackroomsJackPlayerContext& player,
    const glm::vec3& jack_position,
    float jack_body_yaw_degrees,
    float jack_hunch_ratio,
    float maximum_visible_distance) -> BackroomsJackPerception;

[[nodiscard]] auto select_backrooms_jack_spawn_impl(
    const BackroomsGenerator& generator,
    const JackSpatialQuery& spatial,
    const BackroomsJackNavigationGrid& grid,
    const BackroomsJackPlayerContext& player,
    const BackroomsJackChunkReadiness& readiness,
    std::uint32_t random_state,
    float maximum_visible_distance,
    bool has_previous_mode,
    BackroomsJackEncounterMode previous_mode,
    bool force_visible)
    -> BackroomsJackSpawnSelection;

[[nodiscard]] auto build_backrooms_jack_navigation_grid_impl(
    const BackroomsGenerator& generator,
    const JackSpatialQuery& spatial,
    const ChunkCoord& center_chunk)
    -> BackroomsJackNavigationGrid;

[[nodiscard]] auto reposition_hidden_hunt(
    BackroomsJackState& state,
    BackroomsJackRuntime& runtime,
    const BackroomsGenerator& generator,
    const JackSpatialQuery& spatial,
    const BackroomsJackPlayerContext& player) -> bool;

[[nodiscard]] auto spatial_column_allows_jack(
    const BackroomsGenerator& generator,
    const JackSpatialQuery& spatial,
    int world_x,
    int world_z,
    float required_height) -> bool {
    const auto column = generator.sample_column(world_x, world_z);
    const auto clearance = static_cast<float>(
        column.ceiling_y - (column.floor_y + 1));
    if (column.wall || clearance < required_height + 0.02F) {
        return false;
    }
    if (spatial.world == nullptr) {
        return true;
    }

    // Je superpose la geometrie reellement rasterisee au graphe logique. Une
    // marche, un rail ou un override devient ainsi un obstacle planifie au lieu
    // d'etre decouvert trop tard par le corps de Jack.
    if (!is_block_collidable(sample_jack_block(
            generator,
            spatial,
            world_x,
            column.floor_y,
            world_z))) {
        return false;
    }
    const auto occupied_top_y =
        column.floor_y +
        static_cast<int>(std::ceil(required_height));
    for (auto local_y = column.floor_y + 1;
         local_y <= occupied_top_y;
         ++local_y) {
        if (is_block_collidable(sample_jack_block(
                generator,
                spatial,
                world_x,
                local_y,
                world_z))) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] auto player_can_see_candidate(
    const BackroomsGenerator& generator,
    const JackSpatialQuery& spatial,
    const BackroomsJackPlayerContext& player,
    const glm::vec3& candidate,
    float hunch_ratio,
    float half_fov_degrees,
    float maximum_visible_distance) -> bool {
    const auto head = jack_eye_position(candidate, hunch_ratio);
    const auto direction = head - player.eye_position;
    const auto distance = glm::length(direction);
    if (!std::isfinite(distance) ||
        distance <= 0.001F ||
        distance > sanitize_maximum_visible_distance(
                       maximum_visible_distance)) {
        return false;
    }
    const auto normalized = direction / distance;
    const auto threshold =
        std::cos(half_fov_degrees * kPi / 180.0F);
    return glm::dot(player.look_direction, normalized) >= threshold &&
           backrooms_jack_has_line_of_sight_impl(
               generator,
               spatial,
               player.eye_position,
               head);
}

[[nodiscard]] auto walkable_footprint(
    const BackroomsGenerator& generator,
    const JackSpatialQuery& spatial,
    float center_x,
    float center_z) -> bool {
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
        if (!spatial_column_allows_jack(
                generator,
                spatial,
                world_x,
                world_z,
                kBackroomsJackBentHeight)) {
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

void emit_event_at(
    BackroomsJackState& state,
    BackroomsJackUpdateResult& result,
    BackroomsJackEventKind kind,
    const glm::vec3& position) noexcept {
    if (result.event_count >= result.events.size()) {
        return;
    }
    result.events[result.event_count] = {
        kind,
        safe_vector(position, state.position),
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

void invalidate_navigation_path(BackroomsJackRuntime& runtime) noexcept {
    runtime.path = {};
    runtime.has_path_target = false;
    runtime.repath_seconds = 0.0F;
}

void clear_navigation_path(BackroomsJackRuntime& runtime) noexcept {
    invalidate_navigation_path(runtime);
    runtime.stuck_seconds = 0.0F;
    runtime.movement_blocked = false;
}

void enter_phase(
    BackroomsJackState& state,
    BackroomsJackRuntime& runtime,
    BackroomsJackPhase phase) noexcept {
    state.phase = phase;
    state.phase_seconds = 0.0F;
    clear_navigation_path(runtime);
}

void reset_encounter_runtime(BackroomsJackRuntime& runtime) noexcept {
    runtime.encounter_mode = BackroomsJackEncounterMode::HiddenHunt;
    runtime.encounter_limit_seconds = 0.0F;
    runtime.encounter_reaction_seconds = 0.0F;
    runtime.encounter_chases = false;
    runtime.encounter_outcome_directed = false;
    runtime.encounter_was_seen = false;
    runtime.hidden_hunt_timeout_seconds = 0.0F;
    runtime.encounter_deadline_seconds = 0.0F;
    runtime.hidden_hunt_reposition_count = 0U;
    runtime.pending_spawn = {};
    runtime.pending_reveal_seconds = 0.0F;
    runtime.pending_vanish_seconds = 0.0F;
    runtime.pending_reveal = false;
    runtime.pending_vanish = false;
    runtime.pending_vanish_uses_short_cooldown = false;
    runtime.spawn_retry_pending = false;
}

void remember_encounter_result(
    BackroomsJackRuntime& runtime,
    bool chased) noexcept {
    runtime.previous_encounter_mode = runtime.encounter_mode;
    runtime.has_previous_encounter_mode = true;
    runtime.previous_encounter_chased = chased;
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

[[nodiscard]] auto select_hidden_hunt_cell(
    BackroomsJackState& state,
    const BackroomsJackNavigationGrid& grid,
    const BackroomsJackPlayerContext& player,
    const glm::vec3& origin,
    float minimum_translation,
    float minimum_player_distance,
    float maximum_player_distance) noexcept -> int {
    const auto player_index = nearest_walkable_index(
        grid,
        world_point_from_position(player.feet_position));
    if (player_index < 0) {
        return -1;
    }

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
        const auto current = pending[read_cursor++];
        const auto current_x = current % kBackroomsJackNavigationSide;
        const auto current_z = current / kBackroomsJackNavigationSide;
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
            pending[write_cursor++] = next;
        }
    }

    const auto view_threshold = std::cos(85.0F * kPi / 180.0F);
    auto selected_index = -1;
    auto selected_score = std::numeric_limits<float>::max();
    for (std::size_t index = 0U; index < grid.cells.size(); ++index) {
        const auto& cell = grid.cells[index];
        if (!reachable[index] || !cell.walkable) {
            continue;
        }
        const auto point = cell_world_point(grid, static_cast<int>(index));
        const glm::vec3 candidate {
            static_cast<float>(point.x) + 0.5F,
            cell.floor_y,
            static_cast<float>(point.z) + 0.5F,
        };
        const auto player_distance = horizontal_distance(
            candidate,
            player.feet_position);
        const auto translation = horizontal_distance(candidate, origin);
        if (player_distance < minimum_player_distance ||
            player_distance > maximum_player_distance ||
            translation < minimum_translation) {
            continue;
        }
        const auto head = jack_eye_position(
            candidate,
            cell.standing_allowed ? 0.0F : 1.0F);
        const auto view_direction = safe_direction(
            head - player.eye_position,
            player.look_direction);
        const auto view_dot = glm::dot(
            player.look_direction,
            view_direction);
        if (view_dot >= view_threshold) {
            continue;
        }

        // Je vise un anneau a 18 m et ajoute une faible variation deterministe
        // pour que les repositionnements ne choisissent pas toujours la meme
        // case lorsque plusieurs couloirs ont une geometrie equivalente.
        const auto score =
            std::abs(player_distance - 18.0F) +
            std::max(0.0F, view_dot + 0.25F) * 2.0F +
            random_unit(state.random_state) * 0.75F;
        if (score < selected_score) {
            selected_score = score;
            selected_index = static_cast<int>(index);
        }
    }
    return selected_index;
}

[[nodiscard]] auto choose_hidden_hunt_target(
    BackroomsJackState& state,
    const BackroomsJackNavigationGrid& grid,
    const BackroomsJackPlayerContext& player) noexcept
    -> BackroomsJackGridPoint {
    const auto selected = select_hidden_hunt_cell(
        state,
        grid,
        player,
        state.position,
        5.0F,
        kJackHiddenHuntMinimumDistance,
        kJackHiddenHuntMaximumDistance);
    return selected >= 0
        ? cell_world_point(grid, selected)
        : world_point_from_position(state.position);
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
    runtime.movement_blocked =
        runtime.path.empty() && !(start == goal);
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
    const JackSpatialQuery& spatial,
    float speed,
    float dt) -> float {
    if (runtime.path.empty() || speed <= 0.0F) {
        return 0.0F;
    }
    runtime.movement_blocked = false;

    while (!runtime.path.empty()) {
        const auto target_point =
            runtime.path.nodes[runtime.path.cursor];
        const auto* target_cell = backrooms_jack_navigation_cell(
            runtime.navigation,
            target_point.x,
            target_point.z);
        if (target_cell == nullptr || !target_cell->walkable) {
            invalidate_navigation_path(runtime);
            runtime.movement_blocked = true;
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
            state.hunch_ratio <
                kBackroomsJackLowCeilingEntryHunchRatio) {
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
                spatial,
                candidate.x,
                candidate.z)) {
            const auto blocked_index = point_index(
                runtime.navigation,
                target_point);
            if (blocked_index >= 0) {
                auto& blocked_cell = runtime.navigation.cells[
                    static_cast<std::size_t>(blocked_index)];
                // Je retire ce passage du graphe courant : le prochain A*
                // contourne l'obstacle reel au lieu de reprendre la meme voie.
                blocked_cell.walkable = false;
                blocked_cell.standing_allowed = false;
            }
            invalidate_navigation_path(runtime);
            runtime.movement_blocked = true;
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
    float dt) {
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
        return kJackWatchingSuspicionThreshold / 1.9F;
    }
    if (distance <= 36.0F) {
        return kJackWatchingSuspicionThreshold / 3.0F;
    }
    return kJackWatchingSuspicionThreshold / 4.6F;
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
    const auto resumes_existing_chase =
        state.phase == BackroomsJackPhase::Chasing ||
        state.phase == BackroomsJackPhase::Searching;
    if (!resumes_existing_chase) {
        remember_encounter_result(runtime, true);
    }
    enter_phase(state, runtime, BackroomsJackPhase::Chasing);
    reset_encounter_runtime(runtime);
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
    bool emit_vanished,
    bool short_cooldown = false) noexcept {
    runtime.interference_tail_position = state.position;
    runtime.interference_tail_duration = random_range(
        state.random_state,
        kBackroomsJackInterferenceTailMinimumSeconds,
        kBackroomsJackInterferenceTailMaximumSeconds);
    runtime.interference_tail_seconds =
        runtime.interference_tail_duration;
    runtime.interference_tail_mode =
        BackroomsJackLightInterferenceMode::BlackoutPulse;
    enter_phase(state, runtime, BackroomsJackPhase::Cooldown);
    reset_encounter_runtime(runtime);
    state.active = false;
    state.suspicion = 0.0F;
    state.lost_sight_seconds = 0.0F;
    state.unseen_travel_distance = 0.0F;
    state.spawn_check_seconds = 0.0F;
    state.cooldown_seconds = random_range(
        state.random_state,
        short_cooldown
            ? kBackroomsJackPsychologicalCooldownMinimumSeconds
            : kBackroomsJackMinimumCooldownSeconds,
        short_cooldown
            ? kBackroomsJackPsychologicalCooldownMaximumSeconds
            : kBackroomsJackMaximumCooldownSeconds);
    runtime.director_initialized = true;
    runtime.distant_cue_seconds = random_range(
        state.random_state,
        kBackroomsJackFollowingCueMinimumSeconds,
        kBackroomsJackFollowingCueMaximumSeconds);
    runtime.visual_deadline_seconds =
        short_cooldown
            ? kBackroomsJackVisualDeadlineSeconds
            : kBackroomsJackPostChaseVisualDeadlineSeconds;
    if (emit_vanished) {
        emit_event(state, result, BackroomsJackEventKind::Vanished);
    }
}

void request_pending_vanish(
    BackroomsJackState& state,
    BackroomsJackRuntime& runtime,
    bool short_cooldown) noexcept {
    if (runtime.pending_vanish) {
        return;
    }
    remember_encounter_result(runtime, false);
    runtime.pending_vanish = true;
    runtime.pending_vanish_uses_short_cooldown =
        short_cooldown;
    runtime.pending_vanish_seconds = random_range(
        state.random_state,
        kBackroomsJackRevealMinimumSeconds,
        kBackroomsJackRevealMaximumSeconds);
    state.motion_amount = 0.0F;
}

[[nodiscard]] auto is_unseen_hidden_hunt(
    const BackroomsJackRuntime& runtime) noexcept -> bool {
    return runtime.encounter_outcome_directed &&
           runtime.encounter_mode ==
               BackroomsJackEncounterMode::HiddenHunt &&
           !runtime.encounter_was_seen;
}

void recycle_unseen_hidden_hunt(
    BackroomsJackState& state,
    BackroomsJackRuntime& runtime) noexcept {
    // Je ne transforme jamais une traque invisible en long repos de
    // poursuite. Son plafond visuel reste intact et le directeur retente dans
    // une seconde, quel que soit le chemin de sortie (timeout ou streaming).
    state.phase = BackroomsJackPhase::Dormant;
    state.phase_seconds = 0.0F;
    state.spawn_check_seconds = kBackroomsJackSpawnRetryMinimumSeconds;
    state.cooldown_seconds = 0.0F;
    state.active = false;
    state.motion_amount = 0.0F;
    state.suspicion = 0.0F;
    state.lost_sight_seconds = 0.0F;
    state.unseen_travel_distance = 0.0F;
    remember_encounter_result(runtime, false);
    clear_navigation_path(runtime);
    reset_encounter_runtime(runtime);
    runtime.spawn_retry_pending = true;
    runtime.has_last_simulated_position = false;
}

[[nodiscard]] auto update_scripted_stare(
    BackroomsJackState& state,
    BackroomsJackRuntime& runtime,
    const BackroomsJackPlayerContext& player,
    const BackroomsJackPerception& perception,
    float dt,
    BackroomsJackUpdateResult& result) noexcept -> bool {
    if (runtime.encounter_mode ==
        BackroomsJackEncounterMode::HiddenHunt) {
        return false;
    }

    const auto to_player = player.feet_position - state.position;
    state.body_yaw_degrees = approach_angle(
        state.body_yaw_degrees,
        yaw_from_direction(to_player),
        135.0F * dt);
    const auto stare_side =
        runtime.encounter_mode ==
                BackroomsJackEncounterMode::RearStare
            ? -1.0F
            : 1.0F;
    state.head_yaw_degrees =
        std::sin(state.phase_seconds * 0.82F) * 3.5F * stare_side;
    state.motion_amount = approach(
        state.motion_amount,
        0.0F,
        8.0F * dt);
    state.last_seen_player_position = player.feet_position;
    if (perception.player_faces_jack) {
        runtime.encounter_reaction_seconds += dt;
    } else {
        runtime.encounter_reaction_seconds = std::max(
            0.0F,
            runtime.encounter_reaction_seconds - dt * 0.20F);
    }

    const auto rear_reaction_seconds =
        0.60F +
        static_cast<float>((state.random_state >> 8U) & 0xFFU) /
            255.0F *
            0.60F;
    const auto observed_limit =
        runtime.encounter_mode ==
                BackroomsJackEncounterMode::CorridorStare
            ? runtime.encounter_limit_seconds
            : rear_reaction_seconds;
    const auto hard_limit =
        runtime.encounter_mode ==
                BackroomsJackEncounterMode::CorridorStare
            ? 6.0F
            : runtime.encounter_limit_seconds;
    const auto observation_complete =
        perception.player_faces_jack &&
        runtime.encounter_reaction_seconds >= observed_limit;
    const auto patience_exhausted =
        state.phase_seconds >= hard_limit;
    if (!observation_complete && !patience_exhausted) {
        return true;
    }

    if (runtime.encounter_chases) {
        begin_chasing(state, runtime, result, player);
    } else {
        // Je ne joue aucun son de disparition : le doute doit rester entier
        // lorsque le joueur regarde de nouveau le bout du couloir.
        request_pending_vanish(state, runtime, true);
    }
    return true;
}

[[nodiscard]] auto update_hidden_hunt_deadline(
    BackroomsJackState& state,
    BackroomsJackRuntime& runtime,
    float dt) noexcept -> bool {
    const auto hidden_hunt =
        runtime.encounter_outcome_directed &&
        runtime.encounter_mode ==
            BackroomsJackEncounterMode::HiddenHunt;
    if (!hidden_hunt) {
        return false;
    }

    // Je compte la duree murale de la traque, meme lorsque Jack observe le
    // joueur. Une alternance Wandering/Watching ne peut ainsi jamais repousser
    // indefiniment la limite psychologique de 25 a 40 secondes.
    runtime.encounter_deadline_seconds = std::max(
        0.0F,
        runtime.encounter_deadline_seconds - dt);
    if (!runtime.encounter_was_seen) {
        runtime.visual_deadline_seconds = std::max(
            0.0F,
            runtime.visual_deadline_seconds - dt);
    }
    if (runtime.encounter_deadline_seconds > 0.0F &&
        (runtime.encounter_was_seen ||
         runtime.visual_deadline_seconds >
             kJackVisibleDeadlineGuardSeconds)) {
        return false;
    }

    if (runtime.encounter_was_seen) {
        request_pending_vanish(state, runtime, true);
    } else {
        recycle_unseen_hidden_hunt(state, runtime);
    }
    return true;
}

[[nodiscard]] auto update_active_phase(
    BackroomsJackState& state,
    BackroomsJackRuntime& runtime,
    const BackroomsGenerator& generator,
    const JackSpatialQuery& spatial,
    const BackroomsJackPlayerContext& player,
    float maximum_visible_distance,
    float dt,
    BackroomsJackUpdateResult& result) -> float {
    const auto perception = evaluate_backrooms_jack_perception_impl(
        generator,
        spatial,
        player,
        state.position,
        state.body_yaw_degrees,
        state.hunch_ratio,
        maximum_visible_distance);
    if (perception.player_sees_jack) {
        runtime.encounter_was_seen = true;
    }
    const auto outside_visible_distance =
        maximum_visible_distance <= 0.0F ||
        perception.distance > maximum_visible_distance;
    const auto scripted_stare =
        state.phase == BackroomsJackPhase::Watching &&
        runtime.encounter_outcome_directed &&
        runtime.encounter_mode !=
            BackroomsJackEncounterMode::HiddenHunt;
    if (scripted_stare && outside_visible_distance) {
        // Le hard limit d'un stare ne doit jamais emettre Chase apres que le
        // brouillard engage s'est referme devant Jack.
        remember_encounter_result(runtime, false);
        begin_cooldown(state, runtime, result, true, true);
        return 0.0F;
    }
    if ((state.phase == BackroomsJackPhase::Chasing ||
         state.phase == BackroomsJackPhase::Searching) &&
        outside_visible_distance) {
        begin_cooldown(state, runtime, result, true);
        return 0.0F;
    }
    if (update_hidden_hunt_deadline(state, runtime, dt)) {
        return 0.0F;
    }
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
        const auto hidden_hunt =
            runtime.encounter_outcome_directed &&
            runtime.encounter_mode ==
                BackroomsJackEncounterMode::HiddenHunt;
        if (hidden_hunt) {
            const auto separation = horizontal_distance(
                state.position,
                player.feet_position);
            const auto may_reposition =
                separation > kJackHiddenHuntRepositionTriggerDistance &&
                !perception.player_sees_jack;
            if (may_reposition) {
                runtime.hidden_hunt_timeout_seconds = std::max(
                    0.0F,
                    runtime.hidden_hunt_timeout_seconds - dt);
            }
            if (may_reposition &&
                runtime.hidden_hunt_timeout_seconds <= 0.0F) {
                const auto repositioned = reposition_hidden_hunt(
                    state,
                    runtime,
                    generator,
                    spatial,
                    player);
                if (repositioned) {
                    ++runtime.hidden_hunt_reposition_count;
                    runtime.hidden_hunt_timeout_seconds = random_range(
                        state.random_state,
                        kJackHiddenHuntRepositionMinimumSeconds,
                        kJackHiddenHuntRepositionMaximumSeconds);
                } else {
                    runtime.hidden_hunt_timeout_seconds = 1.0F;
                }
                break;
            }
        }
        runtime.repath_seconds =
            std::max(0.0F, runtime.repath_seconds - dt);
        if (runtime.repath_seconds <= 0.0F) {
            const auto start =
                world_point_from_position(state.position);
            const auto target = hidden_hunt
                ? choose_hidden_hunt_target(
                      state,
                      runtime.navigation,
                      player)
                : choose_wander_target(
                      state,
                      runtime.navigation,
                      state.position,
                      32.0F);
            assign_path(
                runtime,
                runtime.navigation,
                start,
                target,
                hidden_hunt
                    ? random_range(state.random_state, 1.0F, 1.5F)
                    : random_range(state.random_state, 1.0F, 2.2F));
            // Je considere une poche sans destination distincte comme un vrai
            // blocage : le watchdog doit alors tenter ses recuperations.
            if (target == start && runtime.path.empty()) {
                runtime.movement_blocked = true;
            }
        }
        traveled = follow_path(
            state,
            runtime,
            generator,
            spatial,
            kJackWanderSpeed,
            dt);
        break;
    }
    case BackroomsJackPhase::Watching: {
        if (update_scripted_stare(
                state,
                runtime,
                player,
                perception,
                dt,
                result)) {
            break;
        }
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
                if (runtime.encounter_outcome_directed &&
                    !runtime.encounter_chases) {
                    request_pending_vanish(state, runtime, true);
                } else {
                    begin_chasing(
                        state,
                        runtime,
                        result,
                        player);
                }
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
        if (runtime.repath_seconds <= 0.0F &&
            (runtime.path.empty() ||
             !runtime.has_path_target ||
             !(runtime.path_target == target_point))) {
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
            spatial,
            player.maximum_sprint_speed *
                kJackChaseSpeedMultiplier,
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
            spatial,
            player.maximum_sprint_speed *
                kJackSearchSpeedMultiplier,
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

[[nodiscard]] auto select_distant_cue_position(
    BackroomsJackState& state,
    const BackroomsJackRuntime& runtime,
    const BackroomsJackPlayerContext& player,
    glm::vec3& selected_position) noexcept -> bool {
    const auto view_threshold = std::cos(85.0F * kPi / 180.0F);
    auto selected_score = std::numeric_limits<float>::max();
    auto found = false;
    for (std::size_t index = 0U;
         index < runtime.navigation.cells.size();
         ++index) {
        const auto& cell = runtime.navigation.cells[index];
        if (!cell.walkable) {
            continue;
        }
        const auto point = cell_world_point(
            runtime.navigation,
            static_cast<int>(index));
        const glm::vec3 candidate {
            static_cast<float>(point.x) + 0.5F,
            cell.floor_y,
            static_cast<float>(point.z) + 0.5F,
        };
        const auto distance = horizontal_distance(
            candidate,
            player.feet_position);
        if (distance < kBackroomsJackDistantCueMinimumDistance ||
            distance > kBackroomsJackDistantCueMaximumDistance) {
            continue;
        }
        const auto view_direction = safe_direction(
            candidate + glm::vec3 {0.0F, 1.0F, 0.0F} -
                player.eye_position,
            player.look_direction);
        if (glm::dot(player.look_direction, view_direction) >=
            view_threshold) {
            continue;
        }
        const auto score =
            std::abs(distance - 23.0F) +
            random_unit(state.random_state) * 1.25F;
        if (score < selected_score) {
            selected_score = score;
            selected_position = candidate;
            found = true;
        }
    }
    return found;
}

void update_director_timers(
    BackroomsJackState& state,
    BackroomsJackRuntime& runtime,
    const BackroomsJackUpdateContext& context,
    const BackroomsJackPlayerContext& player,
    float dt,
    BackroomsJackUpdateResult& result) noexcept {
    if (!context.allow_spawn || phase_has_body(state.phase)) {
        return;
    }
    if (!runtime.director_initialized) {
        runtime.director_initialized = true;
        runtime.distant_cue_seconds = random_range(
            state.random_state,
            runtime.has_previous_encounter_mode
                ? kBackroomsJackFollowingCueMinimumSeconds
                : kBackroomsJackFirstCueMinimumSeconds,
            runtime.has_previous_encounter_mode
                ? kBackroomsJackFollowingCueMaximumSeconds
                : kBackroomsJackFirstCueMaximumSeconds);
        runtime.visual_deadline_seconds =
            runtime.previous_encounter_chased
                ? kBackroomsJackPostChaseVisualDeadlineSeconds
                : kBackroomsJackVisualDeadlineSeconds;
    }

    const auto previous_visual_deadline_seconds =
        runtime.visual_deadline_seconds;
    runtime.visual_deadline_seconds = std::max(
        0.0F,
        runtime.visual_deadline_seconds - dt);
    if (previous_visual_deadline_seconds > 0.0F &&
        runtime.visual_deadline_seconds <= 0.0F) {
        // Je force une seule tentative au franchissement du plafond visuel.
        // Un echec rearme ensuite le verrou normal d'une seconde.
        state.spawn_check_seconds = 0.0F;
        state.cooldown_seconds = 0.0F;
        runtime.spawn_retry_pending = false;
    }
    runtime.distant_cue_seconds = std::max(
        0.0F,
        runtime.distant_cue_seconds - dt);
    if (runtime.distant_cue_seconds <= 0.0F) {
        auto cue_position = player.feet_position;
        if (!select_distant_cue_position(
                state,
                runtime,
                player,
                cue_position)) {
            runtime.distant_cue_seconds = 1.0F;
            return;
        }
        const auto cue_roll = random_unit(state.random_state);
        const auto emits_step = cue_roll < 0.45F || cue_roll >= 0.75F;
        const auto emits_pulse = cue_roll >= 0.45F;
        if (emits_step) {
            emit_event_at(
                state,
                result,
                runtime.distant_cue_next_wooden
                    ? BackroomsJackEventKind::DistantWoodenLegStep
                    : BackroomsJackEventKind::DistantBootStep,
                cue_position);
            runtime.distant_cue_next_wooden =
                !runtime.distant_cue_next_wooden;
        }
        if (emits_pulse) {
            runtime.interference_tail_position = cue_position;
            runtime.interference_tail_duration = random_range(
                state.random_state,
                kBackroomsJackRevealMinimumSeconds,
                kBackroomsJackRevealMaximumSeconds);
            runtime.interference_tail_seconds =
                runtime.interference_tail_duration;
            // Je reserve la panne noire aux apparitions et disparitions. Le
            // leurre ordinaire ne fait qu'imiter un ballast instable.
            runtime.interference_tail_mode =
                BackroomsJackLightInterferenceMode::Flicker;
        }
        runtime.distant_cue_seconds = std::min(
            kBackroomsJackCueDeadlineSeconds,
            random_range(
                state.random_state,
                kBackroomsJackFollowingCueMinimumSeconds,
                kBackroomsJackFollowingCueMaximumSeconds));
    }

}

[[nodiscard]] auto pending_spawn_is_valid(
    const BackroomsJackSpawnSelection& selection,
    const BackroomsGenerator& generator,
    const JackSpatialQuery& spatial,
    const BackroomsJackChunkReadiness& readiness,
    const BackroomsJackPlayerContext& player,
    float maximum_visible_distance) -> bool {
    const auto visible_distance = sanitize_maximum_visible_distance(
        maximum_visible_distance);
    const auto current_distance = horizontal_distance(
        selection.position,
        player.feet_position);
    if (!selection.found || !finite_vector(selection.position) ||
        visible_distance <= 0.0F ||
        current_distance > visible_distance ||
        !readiness_at(
            readiness,
            backrooms_jack_chunk_at(selection.position))) {
        return false;
    }
    if (selection.encounter_mode ==
            BackroomsJackEncounterMode::HiddenHunt &&
        (current_distance < kJackHiddenHuntMinimumDistance ||
         current_distance > kJackHiddenHuntMaximumDistance)) {
        return false;
    }
    if (selection.encounter_mode ==
            BackroomsJackEncounterMode::CorridorStare &&
        (current_distance < kJackCorridorMinimumDistance ||
         current_distance > kJackCorridorMaximumDistance)) {
        return false;
    }
    if (selection.encounter_mode ==
            BackroomsJackEncounterMode::RearStare &&
        (current_distance < kJackRearMinimumDistance ||
         current_distance > kJackRearMaximumDistance)) {
        return false;
    }
    if (selection.encounter_mode !=
            BackroomsJackEncounterMode::HiddenHunt &&
        !backrooms_jack_has_line_of_sight_impl(
            generator,
            spatial,
            player.eye_position,
            jack_eye_position(
                selection.position,
                selection.initial_hunch))) {
        // Je refais le rayon supercover au dernier instant : une revision de
        // chunk pendant la rampe de reveal ne peut pas placer Jack derriere un
        // obstacle nouvellement publie.
        return false;
    }
    return walkable_footprint(
        generator,
        spatial,
        selection.position.x,
        selection.position.z);
}

void activate_pending_spawn(
    BackroomsJackState& state,
    BackroomsJackRuntime& runtime,
    const BackroomsJackPlayerContext& player) noexcept {
    const auto selection = runtime.pending_spawn;
    state.phase =
        selection.encounter_mode ==
                BackroomsJackEncounterMode::HiddenHunt
            ? BackroomsJackPhase::Wandering
            : BackroomsJackPhase::Watching;
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
    state.notice_event_emitted =
        selection.encounter_mode !=
        BackroomsJackEncounterMode::HiddenHunt;
    state.chase_event_emitted = false;
    state.screamer_event_emitted = false;
    clear_navigation_path(runtime);
    runtime.encounter_mode = selection.encounter_mode;
    runtime.encounter_reaction_seconds = 0.0F;
    runtime.encounter_limit_seconds =
        selection.encounter_mode ==
                BackroomsJackEncounterMode::CorridorStare
            ? random_range(state.random_state, 2.0F, 4.0F)
            : random_range(state.random_state, 6.0F, 10.0F);
    runtime.encounter_deadline_seconds = random_range(
        state.random_state,
        kBackroomsJackHiddenHuntMinimumSeconds,
        kBackroomsJackHiddenHuntMaximumSeconds);
    runtime.hidden_hunt_timeout_seconds = random_range(
        state.random_state,
        kJackHiddenHuntRepositionMinimumSeconds,
        kJackHiddenHuntRepositionMaximumSeconds);
    // Je compense le tour obligatoirement non lethal qui suit chaque poursuite.
    // Les probabilites conditionnelles ci-dessous rendent ainsi les taux
    // marginaux 25/35/30 %, tout en interdisant deux poursuites consecutives.
    const auto unlocked_chase_probability =
        selection.encounter_mode ==
                BackroomsJackEncounterMode::CorridorStare
            ? kJackCorridorUnlockedChaseProbability
            : (selection.encounter_mode ==
                       BackroomsJackEncounterMode::RearStare
                   ? kJackRearUnlockedChaseProbability
                   : kJackHiddenUnlockedChaseProbability);
    runtime.encounter_chases =
        selection.route_guaranteed &&
        !runtime.previous_encounter_chased &&
        random_unit(state.random_state) < unlocked_chase_probability;
    runtime.encounter_outcome_directed = true;
    runtime.encounter_was_seen =
        selection.encounter_mode ==
        BackroomsJackEncounterMode::CorridorStare;
    runtime.pending_spawn = {};
    runtime.pending_reveal = false;
    runtime.pending_reveal_seconds = 0.0F;
    runtime.hidden_hunt_reposition_count = 0U;
    runtime.has_last_simulated_position = false;
}

[[nodiscard]] auto schedule_pending_spawn(
    BackroomsJackState& state,
    BackroomsJackRuntime& runtime,
    const BackroomsGenerator& generator,
    const BackroomsJackUpdateContext& context,
    const BackroomsJackPlayerContext& player,
    float planned_reveal_seconds) -> bool {
    const JackSpatialQuery spatial {
        .world = context.spatial_world,
        .readiness = &context.chunk_readiness,
        .world_y_offset = context.spatial_world_y_offset,
    };
    const auto selection = select_backrooms_jack_spawn_impl(
        generator,
        spatial,
        runtime.navigation,
        player,
        context.chunk_readiness,
        state.random_state,
        context.maximum_visible_distance,
        runtime.has_previous_encounter_mode,
        runtime.previous_encounter_mode,
        runtime.visual_deadline_seconds <=
            kJackVisibleDeadlineGuardSeconds);
    state.random_state = selection.next_random_state;
    if (!selection.found) {
        state.spawn_check_seconds = random_range(
            state.random_state,
            kBackroomsJackSpawnRetryMinimumSeconds,
            kBackroomsJackSpawnRetryMaximumSeconds);
        runtime.spawn_retry_pending = true;
        return false;
    }

    runtime.pending_spawn = selection;
    runtime.pending_reveal = true;
    runtime.spawn_retry_pending = false;
    runtime.pending_reveal_seconds =
        planned_reveal_seconds >=
                kBackroomsJackRevealMinimumSeconds &&
            planned_reveal_seconds <=
                kBackroomsJackRevealMaximumSeconds
        ? planned_reveal_seconds
        : random_range(
              state.random_state,
              kBackroomsJackRevealMinimumSeconds,
              kBackroomsJackRevealMaximumSeconds);
    return true;
}

void update_inactive_phase(
    BackroomsJackState& state,
    BackroomsJackRuntime& runtime,
    const BackroomsGenerator& generator,
    const BackroomsJackUpdateContext& context,
    const BackroomsJackPlayerContext& player,
    float dt,
    BackroomsJackUpdateResult& result) {
    update_director_timers(
        state,
        runtime,
        context,
        player,
        dt,
        result);

    if (state.phase == BackroomsJackPhase::Cooldown) {
        state.cooldown_seconds = std::max(
            0.0F,
            state.cooldown_seconds - dt);
    }
    state.spawn_check_seconds = std::max(
        0.0F,
        state.spawn_check_seconds - dt);
    if (runtime.pending_reveal) {
        runtime.pending_reveal_seconds = std::max(
            0.0F,
            runtime.pending_reveal_seconds - dt);
    }

    const auto inactive_countdown =
        state.phase == BackroomsJackPhase::Cooldown
            ? state.cooldown_seconds
            : state.spawn_check_seconds;
    const auto deadline_reveal_window =
        runtime.visual_deadline_seconds <=
        kBackroomsJackRevealMaximumSeconds;
    const auto countdown_reveal_window =
        inactive_countdown <= kBackroomsJackRevealMaximumSeconds;
    const auto retry_ready =
        !runtime.spawn_retry_pending ||
        state.spawn_check_seconds <= 0.0F;
    if (context.allow_spawn &&
        !runtime.pending_reveal &&
        retry_ready &&
        (deadline_reveal_window ||
         countdown_reveal_window)) {
        static_cast<void>(schedule_pending_spawn(
            state,
            runtime,
            generator,
            context,
            player,
            deadline_reveal_window
                ? runtime.visual_deadline_seconds
                : inactive_countdown));
    }

    if (state.phase == BackroomsJackPhase::Cooldown &&
        state.cooldown_seconds > 0.0F) {
        return;
    }
    if (state.phase == BackroomsJackPhase::Cooldown) {
        state.phase = BackroomsJackPhase::Dormant;
        state.phase_seconds = 0.0F;
    }

    if (!runtime.pending_reveal ||
        runtime.pending_reveal_seconds > 0.0F) {
        return;
    }
    const JackSpatialQuery spatial {
        .world = context.spatial_world,
        .readiness = &context.chunk_readiness,
        .world_y_offset = context.spatial_world_y_offset,
    };
    if (!pending_spawn_is_valid(
            runtime.pending_spawn,
            generator,
            spatial,
            context.chunk_readiness,
            player,
            context.maximum_visible_distance)) {
        runtime.pending_spawn = {};
        runtime.pending_reveal = false;
        runtime.pending_reveal_seconds = 0.0F;
        state.spawn_check_seconds = random_range(
            state.random_state,
            kBackroomsJackSpawnRetryMinimumSeconds,
            kBackroomsJackSpawnRetryMaximumSeconds);
        runtime.spawn_retry_pending = true;
        return;
    }
    activate_pending_spawn(state, runtime, player);
}

[[nodiscard]] auto place_jack_on_navigation_cell(
    BackroomsJackState& state,
    BackroomsJackRuntime& runtime,
    const BackroomsGenerator& generator,
    const JackSpatialQuery& spatial,
    int cell_index,
    float maximum_translation) -> bool {
    if (cell_index < 0 ||
        cell_index >= static_cast<int>(runtime.navigation.cells.size())) {
        return false;
    }
    const auto& cell =
        runtime.navigation.cells[static_cast<std::size_t>(cell_index)];
    if (!cell.walkable) {
        return false;
    }
    const auto point = cell_world_point(runtime.navigation, cell_index);
    const glm::vec3 candidate {
        static_cast<float>(point.x) + 0.5F,
        cell.floor_y,
        static_cast<float>(point.z) + 0.5F,
    };
    const auto translation = glm::length(candidate - state.position);
    if (translation <= 0.01F ||
        horizontal_distance(state.position, candidate) >
            maximum_translation ||
        !walkable_footprint(
            generator,
            spatial,
            candidate.x,
            candidate.z)) {
        return false;
    }

    state.position = candidate;
    if (!cell.standing_allowed) {
        state.hunch_ratio = 1.0F;
    }
    invalidate_navigation_path(runtime);
    runtime.movement_blocked = false;
    return true;
}

[[nodiscard]] auto recenter_stuck_jack(
    BackroomsJackState& state,
    BackroomsJackRuntime& runtime,
    const BackroomsGenerator& generator,
    const JackSpatialQuery& spatial) -> bool {
    const auto nearest = nearest_walkable_index(
        runtime.navigation,
        world_point_from_position(state.position));
    return place_jack_on_navigation_cell(
        state,
        runtime,
        generator,
        spatial,
        nearest,
        1.25F);
}

[[nodiscard]] auto reposition_hidden_hunt(
    BackroomsJackState& state,
    BackroomsJackRuntime& runtime,
    const BackroomsGenerator& generator,
    const JackSpatialQuery& spatial,
    const BackroomsJackPlayerContext& player) -> bool {
    const auto selected = select_hidden_hunt_cell(
        state,
        runtime.navigation,
        player,
        state.position,
        6.0F,
        kJackHiddenHuntRepositionMinimumDistance,
        kJackHiddenHuntRepositionMaximumDistance);
    if (!place_jack_on_navigation_cell(
            state,
            runtime,
            generator,
            spatial,
            selected,
            kBackroomsJackDefaultMaximumVisibleDistance)) {
        return false;
    }
    state.body_yaw_degrees = wrap_degrees(
        yaw_from_direction(player.feet_position - state.position) +
        random_range(state.random_state, -18.0F, 18.0F));
    state.head_yaw_degrees = 0.0F;
    state.motion_amount = 0.0F;
    return true;
}

[[nodiscard]] auto shadow_step_stuck_jack(
    BackroomsJackState& state,
    BackroomsJackRuntime& runtime,
    const BackroomsGenerator& generator,
    const JackSpatialQuery& spatial,
    const BackroomsJackPlayerContext& player) -> bool {
    const auto target_index = nearest_walkable_index(
        runtime.navigation,
        world_point_from_position(state.last_seen_player_position));
    if (target_index < 0) {
        return false;
    }

    // Je limite le saut d'ombre a la composante du joueur. Jack ne traverse
    // ainsi jamais une cloison pour se retrouver dans une poche condamnee.
    std::array<bool, kBackroomsJackNavigationCellCount> reachable {};
    std::array<int, kBackroomsJackNavigationCellCount> pending {};
    auto read_cursor = std::size_t {0U};
    auto write_cursor = std::size_t {1U};
    pending[0] = target_index;
    reachable[static_cast<std::size_t>(target_index)] = true;
    constexpr std::array<std::pair<int, int>, 4> directions {{
        {0, -1},
        {-1, 0},
        {1, 0},
        {0, 1},
    }};
    while (read_cursor < write_cursor) {
        const auto current = pending[read_cursor];
        ++read_cursor;
        const auto current_x = current % kBackroomsJackNavigationSide;
        const auto current_z = current / kBackroomsJackNavigationSide;
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
            const auto next_index = static_cast<std::size_t>(next);
            if (reachable[next_index] ||
                !runtime.navigation.cells[next_index].walkable) {
                continue;
            }
            reachable[next_index] = true;
            pending[write_cursor] = next;
            ++write_cursor;
        }
    }

    auto selected_index = -1;
    auto selected_score = std::numeric_limits<float>::max();
    for (std::size_t index = 0U;
         index < runtime.navigation.cells.size();
         ++index) {
        const auto& cell = runtime.navigation.cells[index];
        if (!reachable[index] || !cell.walkable) {
            continue;
        }
        const auto point = cell_world_point(
            runtime.navigation,
            static_cast<int>(index));
        const glm::vec3 candidate {
            static_cast<float>(point.x) + 0.5F,
            cell.floor_y,
            static_cast<float>(point.z) + 0.5F,
        };
        const auto translation =
            horizontal_distance(state.position, candidate);
        const auto player_separation =
            horizontal_distance(player.feet_position, candidate);
        if (translation < 2.0F ||
            translation > 9.0F ||
            player_separation < 6.0F ||
            player_can_see_candidate(
                generator,
                spatial,
                player,
                candidate,
                cell.standing_allowed ? 0.0F : 1.0F,
                85.0F,
                kBackroomsJackDefaultMaximumVisibleDistance)) {
            continue;
        }
        const auto score = horizontal_distance(
            candidate,
            state.last_seen_player_position);
        if (score < selected_score) {
            selected_score = score;
            selected_index = static_cast<int>(index);
        }
    }
    return place_jack_on_navigation_cell(
        state,
        runtime,
        generator,
        spatial,
        selected_index,
        9.0F);
}

void simulate_fixed_step(
    BackroomsJackState& state,
    BackroomsJackRuntime& runtime,
    const BackroomsGenerator& generator,
    const BackroomsJackUpdateContext& context,
    const BackroomsJackPlayerContext& player,
    float dt,
    BackroomsJackUpdateResult& result) {
    runtime.interference_tail_seconds = std::max(
        0.0F,
        runtime.interference_tail_seconds - dt);
    if (state.phase == BackroomsJackPhase::Dormant ||
        state.phase == BackroomsJackPhase::Cooldown) {
        update_inactive_phase(
            state,
            runtime,
            generator,
            context,
            player,
            dt,
            result);
        return;
    }

    state.active = true;
    state.phase_seconds += dt;
    const JackSpatialQuery spatial {
        .world = context.spatial_world,
        .readiness = &context.chunk_readiness,
        .world_y_offset = context.spatial_world_y_offset,
    };
    if (runtime.pending_vanish) {
        runtime.pending_vanish_seconds = std::max(
            0.0F,
            runtime.pending_vanish_seconds - dt);
        state.motion_amount = approach(
            state.motion_amount,
            0.0F,
            8.0F * dt);
        if (runtime.pending_vanish_seconds <= 0.0F) {
            begin_cooldown(
                state,
                runtime,
                result,
                true,
                runtime.pending_vanish_uses_short_cooldown);
            runtime.has_last_simulated_position = false;
        }
        return;
    }
    const auto desired_hunch = target_hunch_for(state, runtime);
    state.hunch_ratio = approach(
        state.hunch_ratio,
        desired_hunch,
        2.4F * dt);

    if (!runtime.has_last_simulated_position) {
        runtime.last_simulated_position = state.position;
        runtime.has_last_simulated_position = true;
    }
    const auto position_before_simulation =
        runtime.last_simulated_position;
    const auto traveled = update_active_phase(
        state,
        runtime,
        generator,
        spatial,
        player,
        sanitize_maximum_visible_distance(
            context.maximum_visible_distance),
        dt,
        result);
    emit_footsteps(state, result, traveled);
    if (!phase_has_body(state.phase)) {
        runtime.has_last_simulated_position = false;
        return;
    }

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

    const auto measured_progress = horizontal_distance(
        position_before_simulation,
        state.position);
    const auto made_progress =
        std::max(traveled, measured_progress) > 0.0001F;
    const auto movement_phase =
        state.phase == BackroomsJackPhase::Wandering ||
        state.phase == BackroomsJackPhase::Chasing ||
        state.phase == BackroomsJackPhase::Searching;
    const auto movement_expected =
        movement_phase &&
        (runtime.movement_blocked ||
         !runtime.path.empty() ||
         state.phase != BackroomsJackPhase::Wandering);
    if (made_progress) {
        runtime.stuck_seconds = 0.0F;
        runtime.movement_blocked = false;
    } else if (movement_expected) {
        const auto previous_stuck_seconds = runtime.stuck_seconds;
        runtime.stuck_seconds += dt;
        if (previous_stuck_seconds < kJackStuckRecenteringSeconds &&
            runtime.stuck_seconds >= kJackStuckRecenteringSeconds) {
            static_cast<void>(recenter_stuck_jack(
                state,
                runtime,
                generator,
                spatial));
        }
        if (previous_stuck_seconds < kJackStuckShadowStepSeconds &&
            runtime.stuck_seconds >= kJackStuckShadowStepSeconds) {
            static_cast<void>(shadow_step_stuck_jack(
                state,
                runtime,
                generator,
                spatial,
                player));
        }
        if (runtime.stuck_seconds >= kJackStuckDespawnSeconds) {
            // Meme sous le regard, une disparition franche reste preferable a
            // une silhouette immobile coincee contre le decor. Le saut d'ombre
            // choisit deja une destination hors champ lorsque c'est possible.
            if (is_unseen_hidden_hunt(runtime)) {
                recycle_unseen_hidden_hunt(state, runtime);
                return;
            }
            begin_cooldown(state, runtime, result, true);
            runtime.has_last_simulated_position = false;
        }
    } else {
        runtime.stuck_seconds = 0.0F;
    }
    runtime.last_simulated_position = state.position;
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
        kBackroomsJackInitialSpawnDelayMinimumSeconds,
        kBackroomsJackInitialSpawnDelayMaximumSeconds);
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
        kBackroomsJackInitialSpawnDelayMaximumSeconds,
        0.0F,
        kBackroomsJackMaximumPersistedSpawnDelaySeconds);
    sanitized.cooldown_seconds = clamp_finite(
        sanitized.cooldown_seconds,
        0.0F,
        0.0F,
        kBackroomsJackMaximumPersistedCooldownSeconds);
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

auto prepare_backrooms_jack_for_persistence(
    const BackroomsJackState& state,
    const BackroomsJackRuntime& runtime) noexcept -> BackroomsJackState {
    auto persistent = sanitize_backrooms_jack_state(state);
    const auto scripted_watching =
        persistent.phase == BackroomsJackPhase::Watching &&
        (runtime.encounter_mode ==
             BackroomsJackEncounterMode::CorridorStare ||
         runtime.encounter_mode ==
             BackroomsJackEncounterMode::RearStare);
    const auto directed_hidden_hunt =
        runtime.encounter_outcome_directed &&
        runtime.encounter_mode ==
            BackroomsJackEncounterMode::HiddenHunt &&
        (persistent.phase == BackroomsJackPhase::Wandering ||
         persistent.phase == BackroomsJackPhase::Watching);
    const auto chase_in_progress =
        persistent.phase == BackroomsJackPhase::Chasing ||
        persistent.phase == BackroomsJackPhase::Searching;
    if (!scripted_watching &&
        !directed_hidden_hunt &&
        !chase_in_progress) {
        return persistent;
    }

    if (directed_hidden_hunt &&
        !runtime.encounter_was_seen &&
        !chase_in_progress) {
        // Je ne transforme pas une traque jamais apercue en repos de
        // poursuite lors d'une sauvegarde. BJCK reste identique et la reprise
        // retente simplement une apparition une seconde plus tard.
        persistent.phase = BackroomsJackPhase::Dormant;
        persistent.active = false;
        persistent.phase_seconds = 0.0F;
        persistent.suspicion = 0.0F;
        persistent.lost_sight_seconds = 0.0F;
        persistent.unseen_travel_distance = 0.0F;
        persistent.motion_amount = 0.0F;
        persistent.spawn_check_seconds =
            kBackroomsJackSpawnRetryMinimumSeconds;
        persistent.cooldown_seconds = 0.0F;
        persistent.chase_event_emitted = false;
        return sanitize_backrooms_jack_state(persistent);
    }

    // Je conclus toute rencontre dont le timer ou la cible vit dans le runtime
    // non sauvegarde. Le marqueur Chase deja present dans BJCK v1 transporte le
    // verrou post-poursuite sans ajouter un seul octet au format.
    persistent.phase = BackroomsJackPhase::Cooldown;
    persistent.active = false;
    persistent.phase_seconds = 0.0F;
    persistent.suspicion = 0.0F;
    persistent.lost_sight_seconds = 0.0F;
    persistent.motion_amount = 0.0F;
    persistent.cooldown_seconds = std::max(
        persistent.cooldown_seconds,
        chase_in_progress
            ? kBackroomsJackMinimumCooldownSeconds
            : kBackroomsJackPsychologicalCooldownMinimumSeconds);
    if (chase_in_progress) {
        persistent.chase_event_emitted = true;
    } else {
        persistent.chase_event_emitted = false;
    }
    return sanitize_backrooms_jack_state(persistent);
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
    if (!navigation_grid_domain_is_valid(grid)) {
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

auto build_backrooms_jack_navigation_grid_impl(
    const BackroomsGenerator& generator,
    const JackSpatialQuery& spatial,
    const ChunkCoord& center_chunk)
    -> BackroomsJackNavigationGrid {
    BackroomsJackNavigationGrid grid {};
    grid.center_chunk = center_chunk;
    grid.logical_level =
        generator_logical_level(generator);
    const auto origin_world_x = checked_int(
        static_cast<std::int64_t>(center_chunk.x) * kChunkSizeX -
        static_cast<std::int64_t>(
            kBackroomsJackNavigationChunkRadius) *
        kChunkSizeX);
    const auto origin_world_z = checked_int(
        static_cast<std::int64_t>(center_chunk.z) * kChunkSizeZ -
        static_cast<std::int64_t>(
            kBackroomsJackNavigationChunkRadius) *
        kChunkSizeZ);
    if (!origin_world_x.has_value() || !origin_world_z.has_value() ||
        static_cast<std::int64_t>(*origin_world_x) +
                kBackroomsJackNavigationSide - 1 >
            std::numeric_limits<int>::max() ||
        static_cast<std::int64_t>(*origin_world_z) +
                kBackroomsJackNavigationSide - 1 >
            std::numeric_limits<int>::max()) {
        // Je rends une grille vide quand son domaine ne tient pas dans int :
        // aucune cellule extreme ne peut etre repliee sur une autre.
        return grid;
    }
    grid.origin_world_x = *origin_world_x;
    grid.origin_world_z = *origin_world_z;

    for (auto local_z = 0;
         local_z < kBackroomsJackNavigationSide;
         ++local_z) {
        for (auto local_x = 0;
             local_x < kBackroomsJackNavigationSide;
             ++local_x) {
            const auto world_x = static_cast<int>(
                static_cast<std::int64_t>(grid.origin_world_x) +
                local_x);
            const auto world_z = static_cast<int>(
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
            const auto logical_walkable =
                !column.wall &&
                clearance >= kBackroomsJackBentHeight + 0.02F;
            const auto logical_standing =
                logical_walkable &&
                clearance >= kBackroomsJackStandingClearance;
            auto bent_voxels_clear = true;
            auto standing_voxels_clear = true;
            if (spatial.world != nullptr) {
                const auto supported = is_block_collidable(
                    sample_jack_block(
                        generator,
                        spatial,
                        world_x,
                        column.floor_y,
                        world_z));
                bent_voxels_clear = supported;
                standing_voxels_clear = supported;
                const auto bent_top =
                    column.floor_y +
                    static_cast<int>(
                        std::ceil(kBackroomsJackBentHeight));
                const auto standing_top =
                    column.floor_y +
                    static_cast<int>(
                        std::ceil(kBackroomsJackStandingHeight));
                for (auto local_y = column.floor_y + 1;
                     local_y <= standing_top;
                     ++local_y) {
                    if (!is_block_collidable(sample_jack_block(
                            generator,
                            spatial,
                            world_x,
                            local_y,
                            world_z))) {
                        continue;
                    }
                    standing_voxels_clear = false;
                    if (local_y <= bent_top) {
                        bent_voxels_clear = false;
                    }
                }
            }
            cell.walkable = logical_walkable && bent_voxels_clear;
            cell.standing_allowed =
                logical_standing && standing_voxels_clear;
            cell.in_water =
                cell.walkable &&
                generator_is_poolrooms(generator) &&
                sample_jack_water_level(
                    generator,
                    spatial,
                    world_x,
                    column.water_y,
                    world_z) > 0U;
            cell.movement_speed_multiplier =
                cell.in_water
                    ? kBackroomsJackPoolroomsSpeedMultiplier
                    : 1.0F;
        }
    }
    return grid;
}

} // namespace

auto build_backrooms_jack_navigation_grid(
    const BackroomsGenerator& generator,
    const ChunkCoord& center_chunk) noexcept
    -> BackroomsJackNavigationGrid {
    return build_backrooms_jack_navigation_grid_impl(
        generator,
        {},
        center_chunk);
}

auto find_backrooms_jack_path(
    const BackroomsJackNavigationGrid& grid,
    BackroomsJackGridPoint start,
    BackroomsJackGridPoint goal) -> BackroomsJackPath {
    BackroomsJackPath path {};
    if (!navigation_grid_domain_is_valid(grid)) {
        return path;
    }
    const auto start_index = point_index(grid, start);
    if (start_index < 0 ||
        !grid.cells[static_cast<std::size_t>(start_index)].walkable) {
        return path;
    }

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

    // Je projette l'objectif uniquement dans la composante du depart. Une
    // cellule proche mais situee derriere un mur ne doit jamais condamner un
    // chemin pourtant disponible du bon cote de l'obstacle.
    std::array<bool, kBackroomsJackNavigationCellCount> reachable {};
    std::array<int, kBackroomsJackNavigationCellCount> pending {};
    auto read_cursor = std::size_t {0U};
    auto write_cursor = std::size_t {1U};
    pending[0] = start_index;
    reachable[static_cast<std::size_t>(start_index)] = true;
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
            const auto next_index = static_cast<std::size_t>(next);
            if (reachable[next_index] ||
                !grid.cells[next_index].walkable) {
                continue;
            }
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
            reachable[next_index] = true;
            pending[write_cursor] = next;
            ++write_cursor;
        }
    }

    auto goal_index = -1;
    auto best_goal_distance =
        std::numeric_limits<long double>::infinity();
    for (std::size_t index = 0U; index < reachable.size(); ++index) {
        if (!reachable[index]) {
            continue;
        }
        const auto candidate =
            cell_world_point(grid, static_cast<int>(index));
        const auto delta_x =
            static_cast<long double>(candidate.x) -
            static_cast<long double>(goal.x);
        const auto delta_z =
            static_cast<long double>(candidate.z) -
            static_cast<long double>(goal.z);
        const auto distance =
            delta_x * delta_x + delta_z * delta_z;
        if (distance < best_goal_distance) {
            best_goal_distance = distance;
            goal_index = static_cast<int>(index);
        }
    }
    if (goal_index < 0) {
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

namespace {

auto backrooms_jack_has_line_of_sight_impl(
    const BackroomsGenerator& generator,
    const JackSpatialQuery& spatial,
    const glm::vec3& from,
    const glm::vec3& to) -> bool {
    if (!finite_vector(from) || !finite_vector(to)) {
        return false;
    }
    const auto traversable_coordinate = [](float value) noexcept {
        return static_cast<double>(value) >=
                   static_cast<double>(
                       std::numeric_limits<int>::lowest()) +
                       2.0 &&
               static_cast<double>(value) <=
                   static_cast<double>(
                       std::numeric_limits<int>::max()) -
                       2.0;
    };
    if (!traversable_coordinate(from.x) ||
        !traversable_coordinate(from.y) ||
        !traversable_coordinate(from.z) ||
        !traversable_coordinate(to.x) ||
        !traversable_coordinate(to.y) ||
        !traversable_coordinate(to.z)) {
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
    // Je traverse chaque voxel une seule fois avec un DDA. La ligne reste
    // exacte, mais une apparition ne paie plus six lectures du meme bloc.
    auto voxel_x = safe_floor_to_int(from.x);
    auto voxel_y = safe_floor_to_int(from.y);
    auto voxel_z = safe_floor_to_int(from.z);
    const auto end_x = safe_floor_to_int(to.x);
    const auto end_y = safe_floor_to_int(to.y);
    const auto end_z = safe_floor_to_int(to.z);
    const auto voxel_is_clear =
        [&](int x, int y, int z, bool endpoint) {
            if (spatial.readiness != nullptr) {
                const ChunkCoord chunk {
                    floor_division(x, kChunkSizeX),
                    floor_division(z, kChunkSizeZ),
                };
                if (!readiness_at(*spatial.readiness, chunk)) {
                    return false;
                }
            }
            return endpoint ||
                   !is_block_opaque(sample_jack_block(
                       generator,
                       spatial,
                       x,
                       y,
                       z));
        };
    // Je refuse une vision qui partirait d'un obstacle ou d'un chunk dont le
    // maillage n'est pas encore publie.
    if (!voxel_is_clear(voxel_x, voxel_y, voxel_z, false) ||
        !voxel_is_clear(end_x, end_y, end_z, true)) {
        return false;
    }
    const auto step_x = delta.x > 0.0F ? 1 : (delta.x < 0.0F ? -1 : 0);
    const auto step_y = delta.y > 0.0F ? 1 : (delta.y < 0.0F ? -1 : 0);
    const auto step_z = delta.z > 0.0F ? 1 : (delta.z < 0.0F ? -1 : 0);
    const auto infinity = std::numeric_limits<float>::infinity();
    const auto delta_t_x = step_x == 0 ? infinity : 1.0F / std::abs(delta.x);
    const auto delta_t_y = step_y == 0 ? infinity : 1.0F / std::abs(delta.y);
    const auto delta_t_z = step_z == 0 ? infinity : 1.0F / std::abs(delta.z);
    auto next_t_x = step_x > 0
        ? (static_cast<float>(voxel_x + 1) - from.x) / delta.x
        : (step_x < 0
               ? (from.x - static_cast<float>(voxel_x)) / -delta.x
               : infinity);
    auto next_t_y = step_y > 0
        ? (static_cast<float>(voxel_y + 1) - from.y) / delta.y
        : (step_y < 0
               ? (from.y - static_cast<float>(voxel_y)) / -delta.y
               : infinity);
    auto next_t_z = step_z > 0
        ? (static_cast<float>(voxel_z + 1) - from.z) / delta.z
        : (step_z < 0
               ? (from.z - static_cast<float>(voxel_z)) / -delta.z
               : infinity);

    for (auto traversal = 0; traversal < 4096; ++traversal) {
        const auto next_t = std::min({next_t_x, next_t_y, next_t_z});
        if (!std::isfinite(next_t) || next_t >= 1.0F) {
            return true;
        }
        constexpr auto tie_epsilon = 0.000001F;
        const auto crosses_x = next_t_x <= next_t + tie_epsilon;
        const auto crosses_y = next_t_y <= next_t + tie_epsilon;
        const auto crosses_z = next_t_z <= next_t + tie_epsilon;
        const auto crossing_mask =
            (crosses_x ? 1U : 0U) |
            (crosses_y ? 2U : 0U) |
            (crosses_z ? 4U : 0U);

        // En cas d'egalite, je teste toutes les cellules touchees par l'arete
        // ou le coin. Le rayon ne peut donc plus glisser entre deux voxels
        // opaques places en diagonale.
        for (auto subset = crossing_mask;
             subset != 0U;
             subset = (subset - 1U) & crossing_mask) {
            const auto candidate_x =
                voxel_x + ((subset & 1U) != 0U ? step_x : 0);
            const auto candidate_y =
                voxel_y + ((subset & 2U) != 0U ? step_y : 0);
            const auto candidate_z =
                voxel_z + ((subset & 4U) != 0U ? step_z : 0);
            const auto endpoint =
                candidate_x == end_x &&
                candidate_y == end_y &&
                candidate_z == end_z;
            if (!voxel_is_clear(
                    candidate_x,
                    candidate_y,
                    candidate_z,
                    endpoint)) {
                return false;
            }
        }

        if (crosses_x) {
            voxel_x += step_x;
            next_t_x += delta_t_x;
        }
        if (crosses_y) {
            voxel_y += step_y;
            next_t_y += delta_t_y;
        }
        if (crosses_z) {
            voxel_z += step_z;
            next_t_z += delta_t_z;
        }
        if (voxel_x == end_x &&
            voxel_y == end_y &&
            voxel_z == end_z) {
            return true;
        }
    }
    // Je traite une ligne anormalement longue comme bloquee : la borne de
    // securite ne doit jamais devenir un moyen de voir au-dela des obstacles.
    return false;
}

auto evaluate_backrooms_jack_perception_impl(
    const BackroomsGenerator& generator,
    const JackSpatialQuery& spatial,
    const BackroomsJackPlayerContext& player,
    const glm::vec3& jack_position,
    float jack_body_yaw_degrees,
    float jack_hunch_ratio,
    float maximum_visible_distance) -> BackroomsJackPerception {
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
    const auto visible_distance = sanitize_maximum_visible_distance(
        maximum_visible_distance);
    const auto within_visible_distance =
        visible_distance > 0.0F && distance <= visible_distance;
    auto clear_to_eye = false;
    auto clear_to_chest = false;
    if (within_visible_distance) {
        // Je borne avant le supercover : aucun rayon potentiellement long ne
        // tourne a 120 Hz dans une zone que le brouillard GPU a deja fermee.
        clear_to_eye = backrooms_jack_has_line_of_sight_impl(
            generator,
            spatial,
            jack_eye,
            safe_player.eye_position);
        const auto eye_height =
            safe_player.eye_position.y - safe_player.feet_position.y;
        clear_to_chest =
            eye_height >= 0.5F && eye_height <= 3.0F &&
            backrooms_jack_has_line_of_sight_impl(
                generator,
                spatial,
                jack_eye,
                player_chest);
    }
    const auto line_of_sight = clear_to_eye || clear_to_chest;
    const auto jack_fov_threshold =
        std::cos(85.0F * kPi / 180.0F);
    const auto player_fov_threshold =
        std::cos(55.0F * kPi / 180.0F);
    const auto player_face_threshold =
        std::cos(35.0F * kPi / 180.0F);

    return {
        distance,
        jack_front_dot,
        player_front_dot,
        line_of_sight,
        within_visible_distance &&
            jack_front_dot >= jack_fov_threshold &&
            line_of_sight,
        within_visible_distance &&
            player_front_dot >= player_fov_threshold &&
            line_of_sight,
        within_visible_distance &&
            player_front_dot >= player_face_threshold &&
            line_of_sight,
    };
}

auto select_backrooms_jack_spawn_impl(
    const BackroomsGenerator& generator,
    const JackSpatialQuery& spatial,
    const BackroomsJackNavigationGrid& grid,
    const BackroomsJackPlayerContext& player,
    const BackroomsJackChunkReadiness& readiness,
    std::uint32_t random_state,
    float maximum_visible_distance,
    bool has_previous_mode,
    BackroomsJackEncounterMode previous_mode,
    bool force_visible)
    -> BackroomsJackSpawnSelection {
    BackroomsJackSpawnSelection selection {};
    const auto initial_random_state =
        random_state == 0U ? kFallbackRandomState : random_state;
    selection.next_random_state = initial_random_state;
    const auto visible_distance = sanitize_maximum_visible_distance(
        maximum_visible_distance);
    if (visible_distance <= 0.0F ||
        !chunks_equal(grid.center_chunk, readiness.center_chunk)) {
        return selection;
    }

    const auto safe_player = sanitize_player_context(player);
    const auto player_point =
        world_point_from_position(safe_player.feet_position);
    const ChunkCoord player_chunk {
        floor_division(player_point.x, kChunkSizeX),
        floor_division(player_point.z, kChunkSizeZ),
    };
    if (!readiness_at(readiness, player_chunk)) {
        return selection;
    }
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
            const auto point = cell_world_point(grid, next);
            const ChunkCoord chunk {
                floor_division(point.x, kChunkSizeX),
                floor_division(point.z, kChunkSizeZ),
            };
            if (!readiness_at(readiness, chunk)) {
                continue;
            }
            reachable[index] = true;
            pending[write_cursor] = next;
            ++write_cursor;
        }
    }

    const auto planned_modes = encounter_mode_order_impl(
        initial_random_state,
        has_previous_mode,
        previous_mode,
        force_visible);
    auto working_random_state = planned_modes.next_random_state;
    const auto& mode_order = planned_modes.modes;

    struct CandidateReservoir {
        int index = -1;
        std::uint32_t count = 0U;
    };
    CandidateReservoir hidden_candidates {};
    const auto consider_hidden =
        [&working_random_state](
            CandidateReservoir& reservoir,
            int candidate_index) noexcept {
            ++reservoir.count;
            if (next_random(working_random_state) % reservoir.count == 0U) {
                reservoir.index = candidate_index;
            }
        };
    struct VisibleProbe {
        glm::vec3 position {0.0F};
        float hunch = 0.0F;
        float score = std::numeric_limits<float>::max();
        bool route_guaranteed = false;
    };
    constexpr auto kProbesPerVisibleMode =
        kJackMaximumVisibleCandidateProbes / 2U;
    struct VisibleProbePool {
        std::array<VisibleProbe, kProbesPerVisibleMode> probes {};
        std::size_t count = 0U;
    };
    VisibleProbePool corridor_probes {};
    VisibleProbePool rear_probes {};
    const auto consider_visible = [](
        VisibleProbePool& pool,
        const VisibleProbe& candidate) noexcept {
        if (pool.count < pool.probes.size()) {
            pool.probes[pool.count++] = candidate;
            return;
        }
        auto worst = std::size_t {0U};
        for (auto index = std::size_t {1U}; index < pool.count; ++index) {
            if (pool.probes[index].score > pool.probes[worst].score) {
                worst = index;
            }
        }
        if (candidate.score < pool.probes[worst].score) {
            pool.probes[worst] = candidate;
        }
    };
    const auto player_horizontal_look = safe_horizontal_direction(
        safe_player.look_direction,
        glm::vec3 {0.0F, 0.0F, -1.0F});
    const auto corridor_threshold =
        std::cos(18.0F * kPi / 180.0F);
    const auto rear_threshold =
        std::cos(135.0F * kPi / 180.0F);
    const auto normal_view_threshold =
        std::cos(55.0F * kPi / 180.0F);
    const auto extended_view_threshold =
        std::cos(85.0F * kPi / 180.0F);

    // La traque cachee reste dans la composante A* compacte et vise l'anneau
    // psychologique 12-24 m, toujours hors du champ etendu du joueur.
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
        if (!readiness_at(readiness, chunk)) {
            continue;
        }
        const glm::vec3 candidate {
            static_cast<float>(point.x) + 0.5F,
            cell.floor_y,
            static_cast<float>(point.z) + 0.5F,
        };
        const auto distance =
            horizontal_distance(candidate, safe_player.feet_position);
        if (distance < kJackHiddenHuntMinimumDistance ||
            distance > kJackHiddenHuntMaximumDistance ||
            distance > visible_distance) {
            continue;
        }
        const auto hunch = cell.standing_allowed ? 0.0F : 1.0F;
        const auto head = jack_eye_position(candidate, hunch);
        const auto player_to_head = head - safe_player.eye_position;
        const auto view_direction = safe_direction(
            player_to_head,
            safe_player.look_direction);
        const auto view_dot =
            glm::dot(safe_player.look_direction, view_direction);
        if (view_dot < extended_view_threshold) {
            consider_hidden(
                hidden_candidates,
                static_cast<int>(index));
        }
    }

    // Les deux mises en scene visibles utilisent l'anneau readiness 7x7. Les
    // 64 meilleurs probes perceptuels sont conserves, puis seulement eux sont
    // testes en supercover afin de borner strictement le cout des raycasts.
    for (auto chunk_delta_z = -kBackroomsJackReadinessChunkRadius;
         chunk_delta_z <= kBackroomsJackReadinessChunkRadius;
         ++chunk_delta_z) {
        for (auto chunk_delta_x = -kBackroomsJackReadinessChunkRadius;
             chunk_delta_x <= kBackroomsJackReadinessChunkRadius;
             ++chunk_delta_x) {
            const auto chunk_x = checked_int(
                static_cast<std::int64_t>(readiness.center_chunk.x) +
                chunk_delta_x);
            const auto chunk_z = checked_int(
                static_cast<std::int64_t>(readiness.center_chunk.z) +
                chunk_delta_z);
            if (!chunk_x.has_value() || !chunk_z.has_value()) {
                continue;
            }
            const ChunkCoord chunk {*chunk_x, *chunk_z};
            if (!readiness_at(readiness, chunk)) {
                continue;
            }
            const auto origin_x = checked_int(
                static_cast<std::int64_t>(chunk.x) * kChunkSizeX);
            const auto origin_z = checked_int(
                static_cast<std::int64_t>(chunk.z) * kChunkSizeZ);
            if (!origin_x.has_value() || !origin_z.has_value() ||
                static_cast<std::int64_t>(*origin_x) +
                        kChunkSizeX - 1 >
                    std::numeric_limits<int>::max() ||
                static_cast<std::int64_t>(*origin_z) +
                        kChunkSizeZ - 1 >
                    std::numeric_limits<int>::max()) {
                continue;
            }
            for (auto local_z = 0; local_z < kChunkSizeZ; ++local_z) {
                for (auto local_x = 0; local_x < kChunkSizeX; ++local_x) {
                    const auto world_x = static_cast<int>(
                        static_cast<std::int64_t>(*origin_x) + local_x);
                    const auto world_z = static_cast<int>(
                        static_cast<std::int64_t>(*origin_z) + local_z);
                    const auto column = generator.sample_column(
                        world_x,
                        world_z);
                    const auto clearance = static_cast<float>(
                        column.ceiling_y - (column.floor_y + 1));
                    if (!spatial_column_allows_jack(
                            generator,
                            spatial,
                            world_x,
                            world_z,
                            kBackroomsJackBentHeight)) {
                        continue;
                    }
                    const auto standing =
                        clearance >= kBackroomsJackStandingClearance;
                    const auto hunch = standing ? 0.0F : 1.0F;
                    const auto navigation_index = point_index(
                        grid,
                        {world_x, world_z});
                    const auto route_guaranteed =
                        navigation_index >= 0 &&
                        reachable[static_cast<std::size_t>(
                            navigation_index)];
                    const glm::vec3 candidate {
                        static_cast<float>(world_x) + 0.5F,
                        static_cast<float>(column.floor_y + 1) + 0.001F,
                        static_cast<float>(world_z) + 0.5F,
                    };
                    const auto distance = horizontal_distance(
                        candidate,
                        safe_player.feet_position);
                    if (distance > visible_distance) {
                        continue;
                    }
                    const auto head = jack_eye_position(candidate, hunch);
                    const auto view_direction = safe_direction(
                        head - safe_player.eye_position,
                        safe_player.look_direction);
                    const auto view_dot = glm::dot(
                        safe_player.look_direction,
                        view_direction);
                    const auto horizontal_direction =
                        safe_horizontal_direction(
                            candidate - safe_player.feet_position,
                            player_horizontal_look);
                    const auto front_dot = glm::dot(
                        player_horizontal_look,
                        horizontal_direction);
                    auto noise =
                        static_cast<std::uint32_t>(world_x) * 0x9E3779B9U ^
                        static_cast<std::uint32_t>(world_z) * 0x85EBCA6BU ^
                        initial_random_state;
                    noise ^= noise >> 16U;
                    noise *= 0x7FEB352DU;
                    noise ^= noise >> 15U;
                    noise *= 0x846CA68BU;
                    noise ^= noise >> 16U;
                    const auto jitter =
                        static_cast<float>(noise & 0xFFFFU) / 65535.0F;
                    // Je garde le couloir disponible sous un plafond bas :
                    // Jack s'y montre alors voûté au lieu de supprimer toute
                    // apparition frontale dans les bureaux historiques.
                    if (distance >= kJackCorridorMinimumDistance &&
                        distance <= kJackCorridorMaximumDistance &&
                        front_dot >= corridor_threshold &&
                        view_dot >= normal_view_threshold) {
                        consider_visible(
                            corridor_probes,
                            {
                                candidate,
                                hunch,
                                std::abs(distance - 42.0F) * 0.08F +
                                    (1.0F - front_dot) * 8.0F +
                                    (1.0F - view_dot) * 2.0F +
                                    hunch * 0.35F +
                                    jitter * 0.15F +
                                    (route_guaranteed ? 0.0F : 3.0F),
                                route_guaranteed,
                            });
                    } else if (
                        distance >= kJackRearMinimumDistance &&
                        distance <= kJackRearMaximumDistance &&
                        front_dot <= rear_threshold &&
                        view_dot < extended_view_threshold) {
                        consider_visible(
                            rear_probes,
                            {
                                candidate,
                                hunch,
                                std::abs(distance - 24.0F) * 0.10F +
                                    (front_dot + 1.0F) * 4.0F +
                                    jitter * 0.15F +
                                    (route_guaranteed ? 0.0F : 3.0F),
                                route_guaranteed,
                            });
                    }
                }
            }
        }
    }

    auto ray_spatial = spatial;
    ray_spatial.readiness = &readiness;
    auto raycast_count = std::size_t {0U};
    const auto select_visible =
        [&](const VisibleProbePool& pool,
            BackroomsJackEncounterMode mode) -> bool {
            auto best_probe = pool.count;
            auto best_score = std::numeric_limits<float>::max();
            for (auto index = std::size_t {0U};
                 index < pool.count &&
                 raycast_count < kJackMaximumVisibleCandidateProbes;
                 ++index) {
                ++raycast_count;
                const auto& probe = pool.probes[index];
                if (probe.score >= best_score ||
                    !backrooms_jack_has_line_of_sight_impl(
                        generator,
                        ray_spatial,
                        safe_player.eye_position,
                        jack_eye_position(probe.position, probe.hunch))) {
                    continue;
                }
                best_score = probe.score;
                best_probe = index;
            }
            if (best_probe >= pool.count) {
                return false;
            }
            const auto& probe = pool.probes[best_probe];
            selection.position = probe.position;
            selection.initial_hunch = probe.hunch;
            selection.encounter_mode = mode;
            selection.route_guaranteed =
                probe.route_guaranteed;
            selection.body_yaw_degrees = yaw_from_direction(
                safe_player.feet_position - selection.position);
            selection.found = true;
            return true;
        };

    for (const auto mode : mode_order) {
        if (force_visible &&
            mode == BackroomsJackEncounterMode::HiddenHunt) {
            continue;
        }
        if (mode == BackroomsJackEncounterMode::CorridorStare &&
            select_visible(corridor_probes, mode)) {
            break;
        }
        if (mode == BackroomsJackEncounterMode::RearStare &&
            select_visible(rear_probes, mode)) {
            break;
        }
        if (mode == BackroomsJackEncounterMode::HiddenHunt &&
            hidden_candidates.index >= 0) {
            const auto selected_point = cell_world_point(
                grid,
                hidden_candidates.index);
            const auto& selected_cell = grid.cells[
                static_cast<std::size_t>(hidden_candidates.index)];
            selection.position = {
                static_cast<float>(selected_point.x) + 0.5F,
                selected_cell.floor_y,
                static_cast<float>(selected_point.z) + 0.5F,
            };
            selection.initial_hunch =
                selected_cell.standing_allowed ? 0.0F : 1.0F;
            selection.encounter_mode = mode;
            selection.route_guaranteed = true;
            selection.body_yaw_degrees = wrap_degrees(
                yaw_from_direction(
                    safe_player.feet_position - selection.position) +
                random_range(working_random_state, -12.0F, 12.0F));
            selection.found = true;
            break;
        }
    }
    selection.next_random_state = working_random_state;
    return selection;
}

} // namespace

auto backrooms_jack_encounter_mode_order(
    std::uint32_t random_state,
    bool has_previous_mode,
    BackroomsJackEncounterMode previous_mode,
    bool force_visible) noexcept -> BackroomsJackEncounterModeOrder {
    return encounter_mode_order_impl(
        random_state,
        has_previous_mode,
        previous_mode,
        force_visible);
}

auto backrooms_jack_has_line_of_sight(
    const BackroomsGenerator& generator,
    const glm::vec3& from,
    const glm::vec3& to) noexcept -> bool {
    return backrooms_jack_has_line_of_sight_impl(
        generator,
        {},
        from,
        to);
}

auto evaluate_backrooms_jack_perception(
    const BackroomsGenerator& generator,
    const BackroomsJackPlayerContext& player,
    const glm::vec3& jack_position,
    float jack_body_yaw_degrees,
    float jack_hunch_ratio) noexcept -> BackroomsJackPerception {
    return evaluate_backrooms_jack_perception_impl(
        generator,
        {},
        player,
        jack_position,
        jack_body_yaw_degrees,
        jack_hunch_ratio,
        kBackroomsJackDefaultMaximumVisibleDistance);
}

auto select_backrooms_jack_spawn(
    const BackroomsGenerator& generator,
    const BackroomsJackNavigationGrid& grid,
    const BackroomsJackPlayerContext& player,
    const BackroomsJackChunkReadiness& readiness,
    std::uint32_t random_state) noexcept
    -> BackroomsJackSpawnSelection {
    return select_backrooms_jack_spawn_impl(
        generator,
        {},
        grid,
        player,
        readiness,
        random_state,
        kBackroomsJackDefaultMaximumVisibleDistance,
        false,
        BackroomsJackEncounterMode::HiddenHunt,
        false);
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
    auto mode = BackroomsJackLightInterferenceMode::Flicker;
    switch (sanitized.phase) {
    case BackroomsJackPhase::Watching:
        base_intensity = 0.72F;
        radius = 16.0F;
        break;
    case BackroomsJackPhase::Chasing:
        base_intensity = 1.0F;
        radius = 20.0F;
        mode = BackroomsJackLightInterferenceMode::BlackoutPulse;
        break;
    case BackroomsJackPhase::Searching:
        base_intensity = 0.68F;
        radius = 17.0F;
        mode = BackroomsJackLightInterferenceMode::BlackoutPulse;
        break;
    case BackroomsJackPhase::Jumpscare:
        base_intensity = 1.0F;
        radius = 24.0F;
        mode = BackroomsJackLightInterferenceMode::BlackoutPulse;
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
        .position = sanitized.position,
        .radius = radius,
        .intensity = std::clamp(
            base_intensity * pulse,
            0.0F,
            1.0F),
        .active = true,
        .mode = mode,
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

namespace {

[[nodiscard]] auto make_runtime_light_interference_view(
    const BackroomsJackState& state,
    const BackroomsJackRuntime& runtime,
    std::int32_t logical_level) noexcept
    -> BackroomsJackLightInterferenceView {
    if (!is_valid_backrooms_logical_level(logical_level) ||
        state.logical_level != logical_level) {
        return {};
    }
    if (runtime.pending_vanish) {
        return {
            .position = state.position,
            .radius = 24.0F,
            .intensity = 1.0F,
            .active = true,
            .mode = BackroomsJackLightInterferenceMode::BlackoutPulse,
        };
    }
    if (runtime.pending_reveal && runtime.pending_spawn.found) {
        return {
            .position = runtime.pending_spawn.position,
            .radius = 22.0F,
            .intensity = 1.0F,
            .active = true,
            .mode = BackroomsJackLightInterferenceMode::BlackoutPulse,
        };
    }
    const auto body_view =
        make_backrooms_jack_light_interference_view(state);
    if (body_view.active) {
        return body_view;
    }
    if (runtime.interference_tail_seconds <= 0.0F ||
        runtime.interference_tail_duration <= 0.0F) {
        return {};
    }
    const auto remaining_ratio = std::clamp(
        runtime.interference_tail_seconds /
            runtime.interference_tail_duration,
        0.0F,
        1.0F);
    return {
        .position = runtime.interference_tail_position,
        .radius = 20.0F,
        .intensity = std::clamp(
            remaining_ratio,
            0.05F,
            1.0F),
        .active = true,
        .mode = runtime.interference_tail_mode,
    };
}

} // namespace

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
    const JackSpatialQuery spatial {
        .world = context.spatial_world,
        .readiness = &context.chunk_readiness,
        .world_y_offset = context.spatial_world_y_offset,
    };

    if (state.logical_level != logical_level) {
        // Je purge tout artefact visuel de l'ancien étage, mais je conserve le
        // directeur et son historique pour ne pas contourner l'anti-poursuite.
        const auto grace_seconds = random_range(
            state.random_state,
            kJackLevelTransitionGraceMinimumSeconds,
            kJackLevelTransitionGraceMaximumSeconds);
        state.phase = BackroomsJackPhase::Dormant;
        state.active = false;
        state.phase_seconds = 0.0F;
        state.motion_amount = 0.0F;
        state.suspicion = 0.0F;
        state.lost_sight_seconds = 0.0F;
        state.cooldown_seconds = 0.0F;
        state.spawn_check_seconds = grace_seconds;
        state.logical_level = logical_level;
        runtime.navigation_valid = false;
        runtime.navigation_readiness_valid = false;
        runtime.fixed_step_accumulator = 0.0F;
        clear_navigation_path(runtime);
        reset_encounter_runtime(runtime);
        runtime.interference_tail_position = {};
        runtime.interference_tail_seconds = 0.0F;
        runtime.interference_tail_duration = 0.0F;
        runtime.interference_tail_mode =
            BackroomsJackLightInterferenceMode::Flicker;
        runtime.visual_deadline_seconds = std::max(
            clamp_finite(
                runtime.visual_deadline_seconds,
                0.0F,
                0.0F,
                kBackroomsJackPostChaseVisualDeadlineSeconds),
            grace_seconds);
        runtime.has_last_simulated_position = false;
        return result;
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
    runtime.encounter_limit_seconds = clamp_finite(
        runtime.encounter_limit_seconds,
        0.0F,
        0.0F,
        30.0F);
    runtime.encounter_reaction_seconds = clamp_finite(
        runtime.encounter_reaction_seconds,
        0.0F,
        0.0F,
        30.0F);
    runtime.pending_reveal_seconds = clamp_finite(
        runtime.pending_reveal_seconds,
        0.0F,
        0.0F,
        kBackroomsJackRevealMaximumSeconds);
    runtime.pending_vanish_seconds = clamp_finite(
        runtime.pending_vanish_seconds,
        0.0F,
        0.0F,
        kBackroomsJackRevealMaximumSeconds);
    runtime.distant_cue_seconds = clamp_finite(
        runtime.distant_cue_seconds,
        0.0F,
        0.0F,
        kBackroomsJackCueDeadlineSeconds);
    runtime.visual_deadline_seconds = clamp_finite(
        runtime.visual_deadline_seconds,
        0.0F,
        0.0F,
        kBackroomsJackPostChaseVisualDeadlineSeconds);
    runtime.hidden_hunt_timeout_seconds = clamp_finite(
        runtime.hidden_hunt_timeout_seconds,
        0.0F,
        0.0F,
        kBackroomsJackHiddenHuntMaximumSeconds);
    runtime.encounter_deadline_seconds = clamp_finite(
        runtime.encounter_deadline_seconds,
        0.0F,
        0.0F,
        kBackroomsJackHiddenHuntMaximumSeconds);
    runtime.interference_tail_duration = clamp_finite(
        runtime.interference_tail_duration,
        0.0F,
        0.0F,
        kBackroomsJackInterferenceTailMaximumSeconds);
    runtime.interference_tail_seconds = clamp_finite(
        runtime.interference_tail_seconds,
        0.0F,
        0.0F,
        runtime.interference_tail_duration);
    if (static_cast<std::uint8_t>(runtime.interference_tail_mode) >
        static_cast<std::uint8_t>(
            BackroomsJackLightInterferenceMode::BlackoutPulse)) {
        runtime.interference_tail_mode =
            BackroomsJackLightInterferenceMode::Flicker;
    }
    if (static_cast<std::uint8_t>(runtime.encounter_mode) >
        static_cast<std::uint8_t>(
            BackroomsJackEncounterMode::RearStare)) {
        reset_encounter_runtime(runtime);
    }
    if (static_cast<std::uint8_t>(runtime.previous_encounter_mode) >
        static_cast<std::uint8_t>(
            BackroomsJackEncounterMode::RearStare)) {
        runtime.previous_encounter_mode =
            BackroomsJackEncounterMode::HiddenHunt;
        runtime.has_previous_encounter_mode = false;
        runtime.previous_encounter_chased = false;
    }
    if (state.chase_event_emitted) {
        // BJCK v1 sauvegarde deja ce booléen. Tant que la rencontre non létale
        // suivante n'a pas ete activee (elle le remet a false), il restaure le
        // verrou de poursuite perdu avec le runtime volontairement transitoire.
        runtime.previous_encounter_chased = true;
        runtime.has_previous_encounter_mode = true;
    }
    if (!finite_vector(runtime.interference_tail_position)) {
        runtime.interference_tail_position = state.position;
        runtime.interference_tail_seconds = 0.0F;
        runtime.interference_tail_duration = 0.0F;
        runtime.interference_tail_mode =
            BackroomsJackLightInterferenceMode::Flicker;
    }
    if (!finite_vector(runtime.last_simulated_position)) {
        runtime.last_simulated_position = state.position;
        runtime.has_last_simulated_position = false;
    }

    const auto maximum_visible_distance =
        sanitize_maximum_visible_distance(
            context.maximum_visible_distance);
    if (maximum_visible_distance <= 0.0F) {
        if (phase_has_body(state.phase)) {
            const auto pursuit_in_progress =
                state.phase == BackroomsJackPhase::Chasing ||
                state.phase == BackroomsJackPhase::Searching ||
                state.phase == BackroomsJackPhase::Jumpscare ||
                state.chase_event_emitted;
            if (!pursuit_in_progress &&
                is_unseen_hidden_hunt(runtime)) {
                recycle_unseen_hidden_hunt(state, runtime);
            } else {
                if (!pursuit_in_progress) {
                    remember_encounter_result(runtime, false);
                }
                // Je respecte le repos acquis meme si le snapshot GPU est
                // momentanement nul : une brume indisponible ne permet pas de
                // contourner le verrou post-poursuite.
                begin_cooldown(
                    state,
                    runtime,
                    result,
                    false,
                    !pursuit_in_progress);
            }
            runtime.has_last_simulated_position = false;
        } else if (runtime.pending_reveal) {
            reset_encounter_runtime(runtime);
        }
        runtime.interference_tail_seconds = 0.0F;
        runtime.interference_tail_duration = 0.0F;
        runtime.interference_tail_mode =
            BackroomsJackLightInterferenceMode::Flicker;
    }

    auto navigation_anchor = player.feet_position;
    if (phase_has_body(state.phase)) {
        navigation_anchor = {
            static_cast<float>(
                (static_cast<double>(player.feet_position.x) +
                 static_cast<double>(state.position.x)) *
                0.5),
            player.feet_position.y,
            static_cast<float>(
                (static_cast<double>(player.feet_position.z) +
                 static_cast<double>(state.position.z)) *
                0.5),
        };
    }
    const auto navigation_center =
        backrooms_jack_chunk_at(navigation_anchor);
    const auto navigation_readiness_changed =
        !runtime.navigation_readiness_valid ||
        !navigation_readiness_equal(
            runtime.navigation_readiness,
            context.chunk_readiness,
            navigation_center);
    if (!runtime.navigation_valid ||
        !chunks_equal(
            runtime.navigation.center_chunk,
            navigation_center) ||
        runtime.navigation.logical_level !=
            logical_level ||
        navigation_readiness_changed) {
        runtime.navigation =
            build_backrooms_jack_navigation_grid_impl(
                generator,
                spatial,
                navigation_center);
        mask_unready_navigation_cells(
            runtime.navigation,
            context.chunk_readiness);
        runtime.navigation_readiness =
            context.chunk_readiness;
        runtime.navigation_valid = true;
        runtime.navigation_readiness_valid = true;
        if (phase_has_body(state.phase)) {
            invalidate_navigation_path(runtime);
        } else {
            clear_navigation_path(runtime);
        }
    }

    if (phase_has_body(state.phase) &&
        !readiness_at(
            context.chunk_readiness,
            backrooms_jack_chunk_at(state.position))) {
        // Je retire silencieusement Jack avant tout rendu si son chunk sort
        // de la fenetre prete. Une apparition ne flotte jamais dans le vide.
        const auto pursuit_in_progress =
            state.phase == BackroomsJackPhase::Chasing ||
            state.phase == BackroomsJackPhase::Searching ||
            state.phase == BackroomsJackPhase::Jumpscare ||
            state.chase_event_emitted;
        if (!pursuit_in_progress &&
            is_unseen_hidden_hunt(runtime)) {
            recycle_unseen_hidden_hunt(state, runtime);
        } else {
            if (!pursuit_in_progress) {
                remember_encounter_result(runtime, false);
            }
            begin_cooldown(
                state,
                runtime,
                result,
                false,
                !pursuit_in_progress);
            runtime.has_last_simulated_position = false;
        }
    }

    if (context.simulation_frozen ||
        !context.player_alive ||
        !std::isfinite(dt) ||
        dt <= 0.0F) {
        result.render = make_backrooms_jack_render_view(
            state,
            logical_level);
        result.light_interference =
            make_runtime_light_interference_view(
                state,
                runtime,
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
        make_runtime_light_interference_view(
            state,
            runtime,
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
