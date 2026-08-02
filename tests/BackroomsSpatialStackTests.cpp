#include "gameplay/PlayerController.h"
#include "world/BackroomsSpatialStack.h"
#include "world/World.h"
#include "world/WorldGenerator.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numeric>
#include <optional>
#include <string_view>
#include <vector>

namespace valcraft {

namespace {

struct HorizontalPoint {
    int x = 0;
    int z = 0;
};

[[nodiscard]] constexpr auto floor_divide_for_test(
    int value,
    int divisor) noexcept -> int {
    const auto quotient = value / divisor;
    const auto remainder = value % divisor;
    return remainder < 0 ? quotient - 1 : quotient;
}

struct PoolEdgeSample {
    HorizontalPoint water {};
    HorizontalPoint shore {};
    WaterState water_state = 0;
};

[[nodiscard]] auto find_pool_edge(
    const BackroomsSpatialStack& stack,
    const BackroomsLevelPlacement& placement,
    std::uint8_t required_level = 0U) noexcept
    -> std::optional<PoolEdgeSample> {
    constexpr auto basin_size = 32;
    constexpr std::array<HorizontalPoint, 4U> directions {{
        {1, 0},
        {-1, 0},
        {0, 1},
        {0, -1},
    }};

    // Je sonde le centre deterministe de chaque cellule de bassin, puis je
    // suis un rayon cardinal jusqu'a sa margelle. Cela garde le test rapide et
    // evite de dependre d'une coordonnee choisie a la main.
    for (int basin_z = -12; basin_z <= 12; ++basin_z) {
        for (int basin_x = -12; basin_x <= 12; ++basin_x) {
            const HorizontalPoint center {
                basin_x * basin_size + basin_size / 2,
                basin_z * basin_size + basin_size / 2,
            };
            const auto center_water =
                stack.sample_water_state(
                    center.x,
                    placement.floor_y,
                    center.z);
            if (center_water == WaterState {0} ||
                (required_level != 0U &&
                 water_level_from_state(center_water) !=
                     required_level)) {
                continue;
            }

            for (const auto& direction : directions) {
                auto last_water = center;
                for (int distance = 1; distance <= 20; ++distance) {
                    const HorizontalPoint candidate {
                        center.x + direction.x * distance,
                        center.z + direction.z * distance,
                    };
                    const auto candidate_water =
                        stack.sample_water_state(
                            candidate.x,
                            placement.floor_y,
                            candidate.z);
                    if (candidate_water != WaterState {0}) {
                        last_water = candidate;
                        continue;
                    }

                    const auto wet_floor =
                        stack.sample_block(
                            last_water.x,
                            placement.base_y,
                            last_water.z);
                    const auto water_cell =
                        stack.sample_block(
                            last_water.x,
                            placement.floor_y,
                            last_water.z);
                    const auto shore_floor =
                        stack.sample_block(
                            candidate.x,
                            placement.floor_y,
                            candidate.z);
                    if (wet_floor ==
                            to_block_id(
                                BlockType::PoolroomsWetTile) &&
                        water_cell == to_block_id(BlockType::Air) &&
                        is_block_collidable(shore_floor) &&
                        stack.has_player_clearance(
                            candidate.x,
                            placement.floor_y + 1,
                            candidate.z,
                            2)) {
                        return PoolEdgeSample {
                            .water = last_water,
                            .shore = candidate,
                            .water_state = center_water,
                        };
                    }
                    break;
                }
            }
        }
    }
    return std::nullopt;
}

struct TraversalObservation {
    bool reached_upper_landing = false;
    bool returned_to_lower_landing = false;
    bool remained_safe = true;
    bool camera_remained_bounded = true;
    bool descent_camera_step_observed = false;
    bool descent_camera_smoothed = true;
    bool camera_recovered_at_rest = false;
    bool movement_remained_continuous = true;
    float highest_y = -std::numeric_limits<float>::infinity();
    float lowest_return_y = std::numeric_limits<float>::infinity();
    float maximum_horizontal_step = 0.0F;
    float maximum_vertical_step = 0.0F;
    float maximum_airborne_time = 0.0F;
    float maximum_landing_impact = 0.0F;
    float maximum_path_progress =
        -std::numeric_limits<float>::infinity();
    glm::vec3 final_position {};
    std::array<bool, kChunkHeight> visited_height_bands {};
};

[[nodiscard]] constexpr auto style_index(
    BackroomsVerticalConnectionStyle style) noexcept -> std::size_t {
    return static_cast<std::size_t>(style);
}

[[nodiscard]] auto square_spiral_point(int index) noexcept
    -> HorizontalPoint {
    HorizontalPoint point {};
    if (index <= 0) {
        return point;
    }

    constexpr std::array<HorizontalPoint, 4U> directions {{
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

[[nodiscard]] constexpr auto rotate_path_point(
    HorizontalPoint local,
    int orientation) noexcept -> HorizontalPoint {
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

[[nodiscard]] auto connection_path_point(
    const BackroomsVerticalConnection& connection,
    int index) noexcept -> HorizontalPoint {
    const auto local =
        connection.style ==
                BackroomsVerticalConnectionStyle::SpiralStairs
            ? square_spiral_point(index)
            : HorizontalPoint {0, index};
    const auto rotated =
        rotate_path_point(
            local,
            connection.orientation_quarter_turns);
    return {
        connection.lower_landing.x + rotated.x,
        connection.lower_landing.z + rotated.z,
    };
}

[[nodiscard]] auto connection_surface_y(
    const BackroomsVerticalConnection& connection,
    int index) noexcept -> int {
    const auto rise =
        connection.upper_floor_y -
        connection.lower_floor_y;
    return connection.lower_floor_y + 1 +
           (index * rise) /
               std::max(connection.path_length - 1, 1);
}

[[nodiscard]] auto expected_support_block_y(
    const BackroomsVerticalConnection& connection,
    int index) noexcept -> int {
    const auto surface_y =
        connection_surface_y(connection, index);
    if (index + 1 >= connection.path_length) {
        return connection.upper_floor_y;
    }
    return connection.smooth_ramp
               ? surface_y
               : surface_y - 1;
}

[[nodiscard]] auto collect_style_examples(
    const BackroomsSpatialStack& stack,
    int minimum_district,
    int maximum_district) noexcept
    -> std::array<
        std::optional<BackroomsVerticalConnection>,
        10U> {
    std::array<
        std::optional<BackroomsVerticalConnection>,
        10U> examples {};
    auto remaining = examples.size();
    const auto& placements = stack.placements();

    for (std::size_t level_index = 0U;
         level_index + 1U < placements.size() && remaining > 0U;
         ++level_index) {
        for (int district_z = minimum_district;
             district_z <= maximum_district && remaining > 0U;
             ++district_z) {
            for (int district_x = minimum_district;
                 district_x <= maximum_district && remaining > 0U;
                 ++district_x) {
                const auto connection =
                    stack.connection_for_district(
                        placements[level_index].logical_level,
                        district_x,
                        district_z);
                if (!connection.has_value()) {
                    continue;
                }
                auto& slot = examples[style_index(connection->style)];
                if (!slot.has_value()) {
                    slot = connection;
                    --remaining;
                }
            }
        }
    }
    return examples;
}

[[nodiscard]] auto collect_office_connections(
    const BackroomsSpatialStack& stack,
    BackroomsVerticalConnectionStyle style,
    std::size_t maximum_count) noexcept
    -> std::vector<BackroomsVerticalConnection> {
    std::vector<BackroomsVerticalConnection> connections;
    connections.reserve(maximum_count);
    const auto& placements = stack.placements();
    for (std::size_t level_index = 1U;
         level_index + 1U < placements.size() &&
         connections.size() < maximum_count;
         ++level_index) {
        for (int district_z = -8;
             district_z <= 8 &&
             connections.size() < maximum_count;
             ++district_z) {
            for (int district_x = -8;
                 district_x <= 8 &&
                 connections.size() < maximum_count;
                 ++district_x) {
                const auto connection =
                    stack.connection_for_district(
                        placements[level_index].logical_level,
                        district_x,
                        district_z);
                if (connection.has_value() &&
                    connection->style == style) {
                    connections.push_back(*connection);
                }
            }
        }
    }
    return connections;
}

[[nodiscard]] auto first_collidable_in_player_volume(
    const BackroomsSpatialStack& stack,
    const glm::vec3& feet_position) noexcept
    -> std::optional<BlockCoord> {
    constexpr auto half_width = 0.30F;
    constexpr auto player_height = 1.80F;
    constexpr auto epsilon = 0.001F;
    const auto minimum_x =
        static_cast<int>(
            std::floor(feet_position.x - half_width));
    const auto maximum_x =
        static_cast<int>(
            std::floor(
                feet_position.x + half_width - epsilon));
    const auto minimum_y =
        static_cast<int>(std::floor(feet_position.y));
    const auto maximum_y =
        static_cast<int>(
            std::floor(
                feet_position.y + player_height - epsilon));
    const auto minimum_z =
        static_cast<int>(
            std::floor(feet_position.z - half_width));
    const auto maximum_z =
        static_cast<int>(
            std::floor(
                feet_position.z + half_width - epsilon));
    for (int y = minimum_y; y <= maximum_y; ++y) {
        for (int z = minimum_z; z <= maximum_z; ++z) {
            for (int x = minimum_x; x <= maximum_x; ++x) {
                if (is_block_collidable(
                        stack.sample_block(x, y, z))) {
                    return BlockCoord {x, y, z};
                }
            }
        }
    }
    return std::nullopt;
}

[[nodiscard]] auto player_volume_is_clear(
    const BackroomsSpatialStack& stack,
    const glm::vec3& feet_position) noexcept -> bool {
    return !first_collidable_in_player_volume(
                stack,
                feet_position)
                .has_value();
}

[[nodiscard]] constexpr auto forward_for_orientation(
    int orientation) noexcept -> HorizontalPoint {
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

[[nodiscard]] constexpr auto yaw_for_orientation(
    int orientation) noexcept -> float {
    switch (orientation & 3) {
    case 1:
        return 0.0F;
    case 2:
        return -90.0F;
    case 3:
        return 180.0F;
    case 0:
    default:
        return 90.0F;
    }
}

void observe_player_frame(
    TraversalObservation& observation,
    const PlayerController& player,
    const glm::vec3& previous_position,
    float previous_eye_y,
    bool descending_phase) noexcept {
    const auto& state = player.state();
    const auto delta = state.position - previous_position;
    const auto horizontal_step =
        std::sqrt(delta.x * delta.x + delta.z * delta.z);
    observation.maximum_horizontal_step =
        std::max(
            observation.maximum_horizontal_step,
            horizontal_step);
    observation.maximum_vertical_step =
        std::max(
            observation.maximum_vertical_step,
            std::abs(delta.y));
    observation.maximum_airborne_time =
        std::max(
            observation.maximum_airborne_time,
            state.airborne_time);
    observation.maximum_landing_impact =
        std::max(
            observation.maximum_landing_impact,
            state.landing_impact);
    observation.remained_safe =
        observation.remained_safe &&
        !state.dead &&
        state.death_cause == PlayerDeathCause::None &&
        state.health == 20.0F;
    const auto eye_offset =
        player.eye_position().y - state.position.y;
    observation.camera_remained_bounded =
        observation.camera_remained_bounded &&
        std::isfinite(eye_offset) &&
        eye_offset >= 0.519F &&
        eye_offset <= 2.721F;
    if (descending_phase &&
        delta.y < -0.30F) {
        const auto eye_delta =
            player.eye_position().y - previous_eye_y;
        observation.descent_camera_step_observed = true;
        observation.descent_camera_smoothed =
            observation.descent_camera_smoothed &&
            eye_offset > 1.67F &&
            std::abs(eye_delta) + 0.05F <
                std::abs(delta.y);
    }
    observation.movement_remained_continuous =
        observation.movement_remained_continuous &&
        horizontal_step <= 0.20F &&
        std::abs(delta.y) <= 1.06F;
    const auto height_band =
        static_cast<int>(std::floor(state.position.y));
    if (is_world_y_valid(height_band)) {
        observation.visited_height_bands[
            static_cast<std::size_t>(height_band)] = true;
    }
}

[[nodiscard]] auto simulate_round_trip(
    const World& world,
    const BackroomsVerticalConnection& connection)
    -> TraversalObservation {
    constexpr auto dt = 1.0F / 30.0F;
    const auto forward =
        forward_for_orientation(
            connection.orientation_quarter_turns);
    const glm::vec3 start {
        static_cast<float>(connection.lower_landing.x) + 0.5F,
        static_cast<float>(connection.lower_landing.y) + 0.001F,
        static_cast<float>(connection.lower_landing.z) + 0.5F,
    };
    PlayerController player(start);
    auto initial_state = player.state();
    initial_state.position = start;
    initial_state.velocity = {};
    initial_state.yaw_degrees =
        yaw_for_orientation(
            connection.orientation_quarter_turns);
    initial_state.pitch_degrees = 0.0F;
    initial_state.body_yaw_degrees = initial_state.yaw_degrees;
    initial_state.health = 20.0F;
    initial_state.fall_start_y = start.y;
    initial_state.landing_impact = 0.0F;
    initial_state.airborne_time = 0.0F;
    initial_state.on_ground = true;
    initial_state.dead = false;
    initial_state.death_cause = PlayerDeathCause::None;
    player.load_state(initial_state);

    TraversalObservation observation {};
    observation.highest_y = start.y;
    observation.lowest_return_y = start.y;
    observation.visited_height_bands[
        static_cast<std::size_t>(
            std::clamp(
                static_cast<int>(std::floor(start.y)),
                kWorldMinY,
                kWorldMaxY))] = true;

    const auto path_progress =
        [&start, &forward](const glm::vec3& position) noexcept {
            return
                (position.x - start.x) *
                    static_cast<float>(forward.x) +
                (position.z - start.z) *
                    static_cast<float>(forward.z);
        };
    const auto target_progress =
        static_cast<float>(connection.path_length - 1);
    const auto frame_limit =
        connection.path_length * 8 + 120;
    PlayerInput ascending_input {};
    ascending_input.move_forward = 1.0F;

    for (int frame = 0;
         frame < frame_limit &&
         !observation.reached_upper_landing;
         ++frame) {
        const auto previous_position = player.position();
        const auto previous_eye_y = player.eye_position().y;
        player.update(ascending_input, dt, world);
        observe_player_frame(
            observation,
            player,
            previous_position,
            previous_eye_y,
            false);
        observation.highest_y =
            std::max(
                observation.highest_y,
                player.position().y);
        observation.maximum_path_progress =
            std::max(
                observation.maximum_path_progress,
                path_progress(player.position()));
        observation.reached_upper_landing =
            path_progress(player.position()) >=
                target_progress - 0.20F &&
            player.position().y >=
                static_cast<float>(connection.upper_landing.y) -
                    0.10F;
    }

    PlayerInput descending_input {};
    descending_input.move_forward = -1.0F;
    for (int frame = 0;
         frame < frame_limit &&
         observation.reached_upper_landing &&
         !observation.returned_to_lower_landing;
         ++frame) {
        const auto previous_position = player.position();
        const auto previous_eye_y = player.eye_position().y;
        player.update(descending_input, dt, world);
        observe_player_frame(
            observation,
            player,
            previous_position,
            previous_eye_y,
            true);
        observation.lowest_return_y =
            std::min(
                observation.lowest_return_y,
                player.position().y);
        observation.returned_to_lower_landing =
            path_progress(player.position()) <= 0.20F &&
            player.position().y <=
                static_cast<float>(connection.lower_landing.y) +
                    0.75F;
    }

    // Je vérifie que le retard visuel n'introduit aucun état persistant une
    // fois le joueur immobile sur le palier inférieur.
    const PlayerInput resting_input {};
    for (int frame = 0; frame < 30; ++frame) {
        const auto previous_position = player.position();
        const auto previous_eye_y = player.eye_position().y;
        player.update(resting_input, dt, world);
        observe_player_frame(
            observation,
            player,
            previous_position,
            previous_eye_y,
            false);
    }
    observation.camera_recovered_at_rest =
        std::abs(
            (player.eye_position().y - player.position().y) -
            1.62F) <= 0.01F;
    observation.final_position = player.position();
    return observation;
}

} // namespace

TEST_CASE("la pile spatiale Backrooms place cinq niveaux reels autour de son ancre") {
    constexpr auto anchor_level = 7;
    const BackroomsSpatialStack stack(27491, anchor_level);
    constexpr std::array<int, kBackroomsSpatialLevelCount>
        expected_levels {{5, 6, 7, 8, 9}};
    const auto& placements = stack.placements();

    CHECK(stack.seed() == 27491);
    CHECK(stack.anchor_level() == anchor_level);
    REQUIRE(placements.size() == kBackroomsSpatialLevelCount);
    for (std::size_t index = 0U;
         index < placements.size();
         ++index) {
        CAPTURE(index);
        CHECK(placements[index].logical_level == expected_levels[index]);
        CHECK(placements[index].base_y == kBackroomsSpatialFloorY[index]);
        CHECK(placements[index].floor_y == kBackroomsSpatialFloorY[index]);
        CHECK(
            placements[index].roof_y ==
            (index + 1U < placements.size()
                 ? kBackroomsSpatialFloorY[index + 1U] - 1
                 : kWorldMaxY));
        CHECK(
            stack.placement_for_level(expected_levels[index]) ==
            placements[index]);
        CHECK(
            stack.spawn_block(expected_levels[index]).y ==
            placements[index].floor_y + 1);
    }
    CHECK_FALSE(stack.placement_for_level(4).has_value());
    CHECK_FALSE(stack.placement_for_level(10).has_value());
}

TEST_CASE("la pile V3 reserve la base des Poolrooms aux fonds encaisses") {
    constexpr auto seed = 63017;
    constexpr std::array<int, 2U> anchors {{0, -4}};

    for (const auto anchor : anchors) {
        CAPTURE(anchor);
        const BackroomsSpatialStack stack(
            seed,
            anchor,
            BackroomsSpatialProfile::RecessedPoolroomsV3);
        const auto& placements = stack.placements();
        for (std::size_t index = 0U;
             index < placements.size();
             ++index) {
            const auto& placement = placements[index];
            CAPTURE(index);
            CAPTURE(placement.logical_level);
            CHECK(placement.base_y == kBackroomsSpatialFloorY[index]);
            CHECK(
                placement.floor_y ==
                placement.base_y +
                    (placement.theme == BackroomsTheme::Poolrooms
                         ? 1
                         : 0));
            CHECK(
                placement.roof_y ==
                (index + 1U < placements.size()
                     ? kBackroomsSpatialFloorY[index + 1U] - 1
                     : kWorldMaxY));

            if (placement.theme != BackroomsTheme::Poolrooms) {
                continue;
            }
            const auto edge = find_pool_edge(stack, placement);
            REQUIRE(edge.has_value());
            CHECK(
                stack.sample_block(
                    edge->water.x,
                    placement.base_y,
                    edge->water.z) ==
                to_block_id(BlockType::PoolroomsWetTile));
            CHECK(
                stack.sample_block(
                    edge->water.x,
                    placement.floor_y,
                    edge->water.z) ==
                to_block_id(BlockType::Air));
            CHECK(
                stack.sample_water_state(
                    edge->water.x,
                    placement.floor_y,
                    edge->water.z) ==
                edge->water_state);
            CHECK(
                stack.sample_water_state(
                    edge->shore.x,
                    placement.floor_y,
                    edge->shore.z) ==
                WaterState {0});

            const auto spawn =
                stack.spawn_block(placement.logical_level);
            CHECK(spawn.y == placement.floor_y + 1);
            CHECK(
                is_block_collidable(
                    stack.sample_block(
                        spawn.x,
                        placement.floor_y,
                        spawn.z)));
            CHECK(
                is_block_collidable(
                    stack.sample_block(
                        spawn.x,
                        placement.base_y,
                        spawn.z)));
        }
    }

    const BackroomsSpatialStack bottom_stack(
        seed,
        0,
        BackroomsSpatialProfile::RecessedPoolroomsV3);
    CHECK(bottom_stack.placements().front().base_y == kWorldMinY);
    CHECK(bottom_stack.placements().front().floor_y == kWorldMinY + 1);
}

TEST_CASE("la pile V4 contient les nappes continues et les bassins profonds dans leur tranche") {
    constexpr auto seed = 63017;
    constexpr auto anchor = -2;
    const BackroomsSpatialStack stack(
        seed,
        anchor,
        BackroomsSpatialProfile::FloodedPoolroomsV4);
    const auto placement = stack.placement_for_level(anchor);
    REQUIRE(placement.has_value());
    REQUIRE(placement->theme == BackroomsTheme::Poolrooms);
    CHECK(placement->floor_y == placement->base_y + 2);
    CHECK(stack.spawn_block(anchor).y == placement->floor_y + 1);

    const BackroomsGenerator generator(
        seed,
        anchor,
        kBackroomsSpatialConnectorDistrictModules,
        BackroomsPoolGeometryProfile::FloodedDistrictsV4);
    auto shallow = std::optional<HorizontalPoint> {};
    auto deep = std::optional<HorizontalPoint> {};
    for (int module_z = -20;
         module_z <= 20 && !deep.has_value();
         ++module_z) {
        for (int module_x = -20;
             module_x <= 20 && !deep.has_value();
             ++module_x) {
            if (!generator.is_flooded_module(module_x, module_z)) {
                continue;
            }
            for (int local_z = 0;
                 local_z < kBackroomsModuleSize && !deep.has_value();
                 ++local_z) {
                for (int local_x = 0;
                     local_x < kBackroomsModuleSize;
                     ++local_x) {
                    const HorizontalPoint point {
                        module_x * kBackroomsModuleSize + local_x,
                        module_z * kBackroomsModuleSize + local_z,
                    };
                    const auto column =
                        generator.sample_column(point.x, point.z);
                    if (column.water_state == WaterState {0}) {
                        continue;
                    }
                    if (column.deep_water) {
                        deep = point;
                        break;
                    }
                    if (!shallow.has_value()) {
                        shallow = point;
                    }
                }
            }
        }
    }
    REQUIRE(shallow.has_value());
    REQUIRE(deep.has_value());

    const auto shallow_column =
        generator.sample_column(shallow->x, shallow->z);
    const auto shallow_floor_y =
        placement->floor_y +
        (shallow_column.floor_y - kBackroomsFloorY);
    const auto shallow_water_y =
        placement->floor_y +
        (shallow_column.water_top_y - kBackroomsFloorY);
    CHECK(shallow_floor_y == placement->base_y + 1);
    CHECK(shallow_water_y == placement->floor_y);
    CHECK(
        stack.sample_block(
            shallow->x,
            shallow_floor_y,
            shallow->z) ==
        to_block_id(BlockType::PoolroomsWetTile));
    CHECK(
        stack.sample_water_state(
            shallow->x,
            shallow_water_y,
            shallow->z) ==
        shallow_column.water_state);

    const auto deep_column =
        generator.sample_column(deep->x, deep->z);
    const auto deep_floor_y =
        placement->floor_y +
        (deep_column.floor_y - kBackroomsFloorY);
    const auto deep_bottom_y =
        placement->floor_y +
        (deep_column.water_bottom_y - kBackroomsFloorY);
    const auto deep_top_y =
        placement->floor_y +
        (deep_column.water_top_y - kBackroomsFloorY);
    CHECK(deep_floor_y == placement->base_y);
    CHECK(deep_bottom_y == placement->base_y + 1);
    CHECK(deep_top_y == placement->floor_y);
    CHECK(
        stack.sample_block(
            deep->x,
            deep_floor_y,
            deep->z) ==
        to_block_id(BlockType::PoolroomsWetTile));
    for (int world_y = deep_bottom_y;
         world_y <= deep_top_y;
         ++world_y) {
        CHECK(
            stack.sample_block(
                deep->x,
                world_y,
                deep->z) ==
            to_block_id(BlockType::Air));
        CHECK(
            stack.sample_water_state(
                deep->x,
                world_y,
                deep->z) ==
            deep_column.water_state);
    }
    CHECK(
        stack.sample_water_state(
            deep->x,
            deep_floor_y,
            deep->z) == WaterState {0});
    CHECK(
        stack.sample_water_state(
            deep->x,
            deep_top_y + 1,
            deep->z) == WaterState {0});
}

TEST_CASE("le profil LegacyV2 explicite reste identique au constructeur historique") {
    constexpr auto seed = -92741;
    constexpr auto anchor = -3;
    const BackroomsSpatialStack historical(seed, anchor);
    const BackroomsSpatialStack explicit_legacy(
        seed,
        anchor,
        BackroomsSpatialProfile::LegacyV2);

    CHECK(historical.placements() == explicit_legacy.placements());
    constexpr std::array<HorizontalPoint, 4U> coordinates {{
        {0, 0},
        {-1, -1},
        {-257, 129},
        {511, -383},
    }};
    for (const auto& coordinate : coordinates) {
        CHECK(
            historical.rasterize_column(coordinate.x, coordinate.z)
                .blocks ==
            explicit_legacy.rasterize_column(coordinate.x, coordinate.z)
                .blocks);
        CHECK(
            historical.rasterize_column(coordinate.x, coordinate.z)
                .water ==
            explicit_legacy.rasterize_column(coordinate.x, coordinate.z)
                .water);
    }
}

TEST_CASE("les paliers V3 restent secs au niveau nominal des Poolrooms") {
    const BackroomsSpatialStack stack(
        63017,
        -4,
        BackroomsSpatialProfile::RecessedPoolroomsV3);
    const auto& placements = stack.placements();

    for (std::size_t index = 0U;
         index + 1U < placements.size();
         ++index) {
        const auto connection =
            stack.connection_for_district(
                placements[index].logical_level,
                0,
                0);
        REQUIRE(connection.has_value());
        CHECK(connection->lower_floor_y == placements[index].floor_y);
        CHECK(connection->upper_floor_y == placements[index + 1U].floor_y);
        CHECK(
            stack.sample_water_state(
                connection->lower_landing.x,
                connection->lower_floor_y,
                connection->lower_landing.z) ==
            WaterState {0});
        CHECK(
            stack.sample_water_state(
                connection->upper_landing.x,
                connection->upper_floor_y,
                connection->upper_landing.z) ==
            WaterState {0});
        CHECK(
            is_backrooms_connector_step(
                stack.sample_block(
                    connection->lower_landing.x,
                    connection->lower_floor_y,
                    connection->lower_landing.z)));
        CHECK(
            is_backrooms_connector_step(
                stack.sample_block(
                    connection->upper_landing.x,
                    connection->upper_floor_y,
                    connection->upper_landing.z)));
        CHECK(
            stack.has_player_clearance(
                connection->lower_landing.x,
                connection->lower_landing.y,
                connection->lower_landing.z,
                2));
        CHECK(
            stack.has_player_clearance(
                connection->upper_landing.x,
                connection->upper_landing.y,
                connection->upper_landing.z,
                2));
    }
}

TEST_CASE("toutes les ouvertures V3 gardent une rive opaque autour de leur eau") {
    const BackroomsSpatialStack stack(
        63017,
        -4,
        BackroomsSpatialProfile::RecessedPoolroomsV3);
    constexpr std::array<HorizontalPoint, 4U> cardinal_directions {{
        {1, 0},
        {-1, 0},
        {0, 1},
        {0, -1},
    }};
    constexpr std::array<BackroomsVerticalConnectionStyle, 5U>
        critical_styles {{
            BackroomsVerticalConnectionStyle::SpiralStairs,
            BackroomsVerticalConnectionStyle::MonumentalStairs,
            BackroomsVerticalConnectionStyle::InclinedRamp,
            BackroomsVerticalConnectionStyle::LongDescendingCorridor,
            BackroomsVerticalConnectionStyle::SlopedTunnel,
        }};
    constexpr auto inspection_radius = 20;
    constexpr auto connections_per_style = 16;
    std::array<int, 10U> inspected_by_style {};
    std::array<int, 10U> water_edges_by_style {};
    std::array<int, 10U> converted_shores_by_style {};
    auto inspected_water_edges = 0;
    auto raster_parity_checks = 0;

    const auto& placements = stack.placements();
    for (std::size_t level_index = 0U;
         level_index + 1U < placements.size();
         ++level_index) {
        REQUIRE(
            placements[level_index + 1U].theme ==
            BackroomsTheme::Poolrooms);
        for (int district_z = -12;
             district_z <= 12;
             ++district_z) {
            for (int district_x = -12;
                 district_x <= 12;
                 ++district_x) {
                const auto connection =
                    stack.connection_for_district(
                        placements[level_index].logical_level,
                        district_x,
                        district_z);
                if (!connection.has_value()) {
                    continue;
                }
                const auto current_style_index =
                    style_index(connection->style);
                const auto critical_style =
                    std::find(
                        critical_styles.begin(),
                        critical_styles.end(),
                        connection->style) !=
                    critical_styles.end();
                if (!critical_style ||
                    inspected_by_style[current_style_index] >=
                        connections_per_style) {
                    continue;
                }
                ++inspected_by_style[current_style_index];
                const BackroomsGenerator upper_generator(
                    63017,
                    connection->upper_level,
                    kBackroomsSpatialConnectorDistrictModules,
                    BackroomsPoolGeometryProfile::RecessedOneBlock);
                for (int z = connection->upper_landing.z -
                                 inspection_radius;
                     z <= connection->upper_landing.z +
                              inspection_radius;
                     ++z) {
                    for (int x = connection->upper_landing.x -
                                     inspection_radius;
                         x <= connection->upper_landing.x +
                                  inspection_radius;
                         ++x) {
                        const auto water =
                            stack.sample_water_state(
                                x,
                                connection->upper_floor_y,
                                z);
                        if (water == WaterState {0}) {
                            continue;
                        }
                        for (const auto& direction :
                             cardinal_directions) {
                            const auto neighbour_x =
                                x + direction.x;
                            const auto neighbour_z =
                                z + direction.z;
                            if (stack.sample_water_state(
                                    neighbour_x,
                                    connection->upper_floor_y,
                                    neighbour_z) !=
                                WaterState {0}) {
                                continue;
                            }
                            ++inspected_water_edges;
                            ++water_edges_by_style[
                                current_style_index];
                            CAPTURE(level_index);
                            CAPTURE(district_x);
                            CAPTURE(district_z);
                            CAPTURE(x);
                            CAPTURE(z);
                            CAPTURE(neighbour_x);
                            CAPTURE(neighbour_z);
                            const auto neighbour_block =
                                stack.sample_block(
                                    neighbour_x,
                                    connection->upper_floor_y,
                                    neighbour_z);
                            CHECK(
                                is_block_opaque(neighbour_block));

                            const auto source_column =
                                upper_generator.sample_column(
                                    neighbour_x,
                                    neighbour_z);
                            if (source_column.pool_surface !=
                                    BackroomsPoolSurface::Water ||
                                neighbour_block !=
                                    to_block_id(
                                        BlockType::PoolroomsMetal)) {
                                continue;
                            }
                            ++converted_shores_by_style[
                                current_style_index];
                            if (raster_parity_checks >= 64) {
                                continue;
                            }
                            ++raster_parity_checks;
                            const auto rasterized =
                                stack.rasterize_column(
                                    neighbour_x,
                                    neighbour_z);
                            const auto y_index =
                                static_cast<std::size_t>(
                                    connection->upper_floor_y);
                            CHECK(
                                rasterized.blocks[y_index] ==
                                neighbour_block);
                            CHECK(
                                rasterized.water[y_index] ==
                                stack.sample_water_state(
                                    neighbour_x,
                                    connection->upper_floor_y,
                                    neighbour_z));
                        }
                    }
                }
            }
        }
    }

    for (const auto style : critical_styles) {
        CAPTURE(static_cast<int>(style));
        CHECK(
            inspected_by_style[style_index(style)] ==
            connections_per_style);
        CHECK(
            water_edges_by_style[style_index(style)] > 0);
    }
    CHECK(inspected_water_edges > 100);
    CHECK(
        std::accumulate(
            converted_shores_by_style.begin(),
            converted_shores_by_style.end(),
            0) > 0);
    CHECK(raster_parity_checks > 0);
}

TEST_CASE("la rasterisation V3 honore le fond et l'eau traduits de la colonne") {
    const BackroomsSpatialStack stack(
        63017,
        0,
        BackroomsSpatialProfile::RecessedPoolroomsV3);
    const auto& poolrooms = stack.placements().front();
    const auto edge = find_pool_edge(stack, poolrooms);
    REQUIRE(edge.has_value());
    const std::array<HorizontalPoint, 4U> coordinates {{
        edge->water,
        edge->shore,
        {-1, -1},
        {-257, 129},
    }};

    for (const auto& coordinate : coordinates) {
        const auto column =
            stack.rasterize_column(coordinate.x, coordinate.z);
        for (int y = kWorldMinY; y <= kWorldMaxY; ++y) {
            CAPTURE(coordinate.x);
            CAPTURE(y);
            CAPTURE(coordinate.z);
            CHECK(
                column.blocks[static_cast<std::size_t>(y)] ==
                stack.sample_block(coordinate.x, y, coordinate.z));
            CHECK(
                column.water[static_cast<std::size_t>(y)] ==
                stack.sample_water_state(
                    coordinate.x,
                    y,
                    coordinate.z));
        }
    }
}

TEST_CASE("le joueur tombe dans les bassins V3 puis remonte sur leur rive") {
    constexpr auto seed = 63017;
    const BackroomsSpatialStack stack(
        seed,
        0,
        BackroomsSpatialProfile::RecessedPoolroomsV3);
    const auto& poolrooms = stack.placements().front();
    const World world(
        seed,
        0,
        WorldGenerationProfile::Backrooms,
        WorldGenerationVersion::BackroomsV3,
        VisualPipeline::LegacyVoxel,
        0);
    constexpr std::array<std::uint8_t, 2U> levels {{
        5U,
        kMaxWaterLevel,
    }};

    for (const auto level : levels) {
        CAPTURE(level);
        const auto edge = find_pool_edge(stack, poolrooms, level);
        REQUIRE(edge.has_value());
        REQUIRE(
            world.peek_water_level_or_generated(
                edge->water.x,
                poolrooms.floor_y,
                edge->water.z) == level);

        PlayerController player({
            static_cast<float>(edge->water.x) + 0.5F,
            static_cast<float>(poolrooms.floor_y) + 5.0F,
            static_cast<float>(edge->water.z) + 0.5F,
        });
        player.set_water_movement_profile(
            PlayerWaterMovementProfile::Poolrooms);

        auto landed_in_water = false;
        auto was_on_ground = player.state().on_ground;
        for (int frame = 0; frame < 240; ++frame) {
            player.update(PlayerInput {}, 1.0F / 60.0F, world);
            if (!was_on_ground && player.state().on_ground) {
                landed_in_water =
                    player.state().landing_impact <= 0.001F;
                break;
            }
            was_on_ground = player.state().on_ground;
        }
        CHECK(landed_in_water);
        CHECK_FALSE(player.state().dead);
        CHECK(
            player.state().health ==
            doctest::Approx(player.max_health()));
        CHECK(
            player.position().y ==
            doctest::Approx(
                static_cast<float>(poolrooms.base_y) + 1.001F)
                .epsilon(0.003));

        const auto direction_x = edge->shore.x - edge->water.x;
        const auto direction_z = edge->shore.z - edge->water.z;
        auto state = player.state();
        state.yaw_degrees =
            std::atan2(
                static_cast<float>(direction_z),
                static_cast<float>(direction_x)) *
            (180.0F / 3.14159265358979323846F);
        state.pitch_degrees = 0.0F;
        state.body_yaw_degrees = state.yaw_degrees;
        player.load_state(state);

        auto reached_shore = false;
        const auto start = player.position();
        auto maximum_exit_y = start.y;
        for (int frame = 0; frame < 180; ++frame) {
            PlayerInput input {};
            input.move_forward = 1.0F;
            input.jump = frame < 6;
            player.update(input, 1.0F / 60.0F, world);
            maximum_exit_y =
                std::max(maximum_exit_y, player.position().y);
            const auto progress =
                (player.position().x - start.x) *
                    static_cast<float>(direction_x) +
                (player.position().z - start.z) *
                    static_cast<float>(direction_z);
            if (progress >= 0.70F &&
                player.position().y >=
                    static_cast<float>(poolrooms.floor_y) + 0.95F) {
                reached_shore = true;
                break;
            }
        }
        CAPTURE(edge->water.x);
        CAPTURE(edge->water.z);
        CAPTURE(edge->shore.x);
        CAPTURE(edge->shore.z);
        CAPTURE(player.position().x);
        CAPTURE(player.position().y);
        CAPTURE(player.position().z);
        CAPTURE(player.state().velocity.x);
        CAPTURE(player.state().velocity.y);
        CAPTURE(player.state().velocity.z);
        CAPTURE(maximum_exit_y);
        CHECK(reached_shore);
        CHECK_FALSE(player.state().dead);
    }
}

TEST_CASE("le hub Backrooms offre toujours une montee et une descente visibles") {
    constexpr std::array<int, 6U> seeds {{
        0,
        1,
        1337,
        63017,
        -92741,
        1827706392,
    }};
    constexpr std::array<int, 4U> anchors {{-7, -1, 0, 9}};
    constexpr auto district_world_size =
        kBackroomsSpatialConnectorDistrictModules *
        kBackroomsModuleSize;

    for (const auto seed : seeds) {
        for (const auto anchor : anchors) {
            CAPTURE(seed);
            CAPTURE(anchor);
            const BackroomsSpatialStack stack(seed, anchor);
            const auto spawn = stack.spawn_block(anchor);
            const auto district_x =
                spawn.x / district_world_size;
            const auto district_z =
                spawn.z / district_world_size;
            const auto& placements = stack.placements();
            const auto descent =
                stack.connection_for_district(
                    placements[1].logical_level,
                    district_x,
                    district_z);
            const auto ascent =
                stack.connection_for_district(
                    placements[2].logical_level,
                    district_x,
                    district_z);

            REQUIRE(descent.has_value());
            REQUIRE(ascent.has_value());
            CHECK(
                descent->style ==
                BackroomsVerticalConnectionStyle::SpiralStairs);
            CHECK(
                ascent->style ==
                BackroomsVerticalConnectionStyle::ClassicStairs);
            CHECK(ascent->upper_approach);
            CHECK(descent->upper_landing.y == spawn.y);
            CHECK(ascent->lower_landing.y == spawn.y);
            CHECK(descent->upper_landing.x == spawn.x);
            CHECK(descent->upper_landing.z == spawn.z + 8);
            CHECK(ascent->lower_landing.x == spawn.x);
            CHECK(ascent->lower_landing.z == spawn.z - 8);
            CHECK(
                std::abs(descent->upper_landing.x - spawn.x) <= 8);
            CHECK(
                std::abs(descent->upper_landing.z - spawn.z) <= 8);
            CHECK(
                std::abs(ascent->lower_landing.x - spawn.x) <= 8);
            CHECK(
                std::abs(ascent->lower_landing.z - spawn.z) <= 8);
            CHECK(
                stack.has_player_clearance(
                    spawn.x,
                    spawn.y,
                    spawn.z,
                    2));
            CHECK(
                stack.has_player_clearance(
                    descent->upper_landing.x,
                    descent->upper_landing.y,
                    descent->upper_landing.z,
                    2));
            CHECK(
                stack.has_player_clearance(
                    ascent->lower_landing.x,
                    ascent->lower_landing.y,
                    ascent->lower_landing.z,
                    2));
            CHECK(
                is_backrooms_connector_step(
                    stack.sample_block(
                        descent->upper_landing.x,
                        descent->upper_floor_y,
                        descent->upper_landing.z)));
            CHECK(
                is_backrooms_connector_step(
                    stack.sample_block(
                        ascent->lower_landing.x,
                        ascent->lower_floor_y,
                        ascent->lower_landing.z)));

            const auto upper_hub =
                stack.spawn_block(placements[3].logical_level);
            const auto escape_x =
                ascent->upper_landing.x + 4;
            const auto check_approach_cell =
                [&](int x, int z) {
                    CAPTURE(x);
                    CAPTURE(z);
                    CHECK(
                        is_backrooms_connector_step(
                            stack.sample_block(
                                x,
                                ascent->upper_floor_y,
                                z)));
                    CHECK(
                        stack.has_player_clearance(
                            x,
                            ascent->upper_floor_y + 1,
                            z,
                            2));
                };
            for (int x = ascent->upper_landing.x;
                 x <= escape_x;
                 ++x) {
                check_approach_cell(
                    x,
                    ascent->upper_landing.z);
            }
            for (int z = ascent->upper_landing.z;
                 z <= upper_hub.z;
                 ++z) {
                check_approach_cell(escape_x, z);
            }
            for (int x = upper_hub.x;
                 x <= escape_x;
                 ++x) {
                check_approach_cell(x, upper_hub.z);
            }
        }
    }
}

TEST_CASE("les connexions spatiales Backrooms occupent des districts de deux modules") {
    constexpr auto district_world_size =
        kBackroomsSpatialConnectorDistrictModules *
        kBackroomsModuleSize;
    const BackroomsSpatialStack stack(63017, 0);
    const auto& placements = stack.placements();

    for (std::size_t level_index = 0U;
         level_index + 1U < placements.size();
         ++level_index) {
        for (int district_z = -4; district_z <= 4; ++district_z) {
            for (int district_x = -4; district_x <= 4; ++district_x) {
                CAPTURE(level_index);
                CAPTURE(district_x);
                CAPTURE(district_z);
                const auto connection =
                    stack.connection_for_district(
                        placements[level_index].logical_level,
                        district_x,
                        district_z);
                REQUIRE(connection.has_value());
                CHECK(
                    connection->lower_landing.x >=
                    district_x * district_world_size);
                CHECK(
                    connection->lower_landing.x <
                    (district_x + 1) * district_world_size);
                CHECK(
                    connection->lower_landing.z >=
                    district_z * district_world_size);
                CHECK(
                    connection->lower_landing.z <
                    (district_z + 1) * district_world_size);
            }
        }
    }
}

TEST_CASE("le niveau logique et le theme Backrooms proviennent de la hauteur physique") {
    const BackroomsSpatialStack stack(27491, 0);
    const auto& placements = stack.placements();

    for (const auto& placement : placements) {
        CAPTURE(placement.logical_level);
        CAPTURE(placement.floor_y);
        CHECK(
            stack.logical_level_at_y(
                static_cast<float>(placement.floor_y)) ==
            placement.logical_level);
        CHECK(
            stack.theme_at_y(
                static_cast<float>(placement.floor_y)) ==
            placement.theme);
    }

    CHECK(stack.logical_level_at_y(9.999F) == -2);
    CHECK(stack.logical_level_at_y(10.0F) == -1);
    CHECK(stack.theme_at_y(9.999F) == BackroomsTheme::Poolrooms);
    CHECK(stack.theme_at_y(10.0F) == BackroomsTheme::Offices);
    CHECK(stack.logical_level_at_y(29.999F) == -1);
    CHECK(stack.logical_level_at_y(30.0F) == 0);
    CHECK(stack.logical_level_at_y(53.999F) == 0);
    CHECK(stack.logical_level_at_y(54.0F) == 1);
    CHECK(stack.logical_level_at_y(82.999F) == 1);
    CHECK(stack.logical_level_at_y(83.0F) == 2);
}

TEST_CASE("la pile spatiale reste deterministe dans les districts et coordonnees negatifs") {
    constexpr auto seed = -92741;
    const BackroomsSpatialStack first(seed, -3);
    const BackroomsSpatialStack second(seed, -3);
    CHECK(first.placements() == second.placements());

    constexpr std::array<HorizontalPoint, 5U> coordinates {{
        {-1, -1},
        {-64, -127},
        {-255, 31},
        {-256, -256},
        {-513, 257},
    }};
    constexpr std::array<int, 7U> heights {{0, 1, 20, 40, 68, 98, 127}};
    for (const auto& coordinate : coordinates) {
        for (const auto height : heights) {
            CAPTURE(coordinate.x);
            CAPTURE(height);
            CAPTURE(coordinate.z);
            CHECK(
                first.sample_block(
                    coordinate.x,
                    height,
                    coordinate.z) ==
                second.sample_block(
                    coordinate.x,
                    height,
                    coordinate.z));
            CHECK(
                first.sample_water_state(
                    coordinate.x,
                    height,
                    coordinate.z) ==
                second.sample_water_state(
                    coordinate.x,
                    height,
                    coordinate.z));
        }
    }

    constexpr std::array<HorizontalPoint, 5U> districts {{
        {-1, -1},
        {-2, 0},
        {0, -2},
        {-17, -9},
        {13, -21},
    }};
    for (std::size_t level_index = 0U;
         level_index + 1U < first.placements().size();
         ++level_index) {
        const auto lower_level =
            first.placements()[level_index].logical_level;
        for (const auto& district : districts) {
            CAPTURE(lower_level);
            CAPTURE(district.x);
            CAPTURE(district.z);
            const auto first_connection =
                first.connection_for_district(
                    lower_level,
                    district.x,
                    district.z);
            const auto second_connection =
                second.connection_for_district(
                    lower_level,
                    district.x,
                    district.z);
            REQUIRE(first_connection.has_value());
            CHECK(first_connection == second_connection);
            if (district.x < 0) {
                CHECK(first_connection->lower_landing.x < 0);
            }
            if (district.z < 0) {
                CHECK(first_connection->lower_landing.z < 0);
            }
        }
    }
}

TEST_CASE("une connexion superieure ne vide pas les Poolrooms sous sa projection") {
    constexpr auto seed = 63017;
    const BackroomsSpatialStack stack(seed, 0);
    const BackroomsGenerator poolrooms(seed, -2);
    auto preserved_water_found = false;

    // Je parcours uniquement les deux connexions hautes : chacune possède une
    // géométrie réelle dans la colonne, mais aucune ne doit altérer Y=1 tant
    // qu'une cage basse ne retaillait pas elle-même cette cellule.
    for (int lower_level = 0;
         lower_level <= 1 && !preserved_water_found;
         ++lower_level) {
        for (int district_z = -8;
             district_z <= 8 && !preserved_water_found;
             ++district_z) {
            for (int district_x = -8;
                 district_x <= 8 && !preserved_water_found;
                 ++district_x) {
                const auto connection =
                    stack.connection_for_district(
                        lower_level,
                        district_x,
                        district_z);
                REQUIRE(connection.has_value());
                for (int path_index = 0;
                     path_index < connection->path_length;
                     ++path_index) {
                    const auto point =
                        connection_path_point(
                            *connection,
                            path_index);
                    const auto original_water =
                        poolrooms.sample_water_state(
                            point.x,
                            kBackroomsFloorY + 1,
                            point.z);
                    if (original_water == WaterState {0}) {
                        continue;
                    }

                    const auto stacked_water =
                        stack.sample_water_state(
                            point.x,
                            1,
                            point.z);
                    if (stacked_water != original_water) {
                        continue;
                    }
                    const auto column =
                        stack.rasterize_column(
                            point.x,
                            point.z);
                    CHECK(column.water[1U] == original_water);
                    preserved_water_found = true;
                    break;
                }
            }
        }
    }

    CHECK(preserved_water_found);
}

TEST_CASE("les dix styles verticaux sont distribues et relient de vrais deltas Y") {
    const BackroomsSpatialStack stack(63017, 0);
    const auto examples = collect_style_examples(stack, -10, 10);

    for (std::size_t index = 0U;
         index < examples.size();
         ++index) {
        CAPTURE(index);
        REQUIRE(examples[index].has_value());
        const auto& connection = *examples[index];
        CHECK(style_index(connection.style) == index);
        CHECK(connection.upper_level == connection.lower_level + 1);
        CHECK(connection.upper_floor_y > connection.lower_floor_y);
        CHECK(
            connection.upper_landing.y -
                connection.lower_landing.y ==
            connection.upper_floor_y -
                connection.lower_floor_y);
        CHECK(connection.path_length > 1);
        CHECK(connection.walkable_width >= 1);
        CHECK(connection.headroom >= 3);

        const auto final_point =
            connection_path_point(
                connection,
                connection.path_length - 1);
        CHECK(final_point.x == connection.upper_landing.x);
        CHECK(final_point.z == connection.upper_landing.z);
        CHECK(
            connection.lower_landing.y ==
            connection.lower_floor_y + 1);
        CHECK(
            connection.upper_landing.y ==
            connection.upper_floor_y + 1);
        CHECK(
            std::string_view(
                backrooms_vertical_connection_style_name(
                    connection.style)) != "unknown");
    }
}

TEST_CASE("la validation de sauvegarde couvre toute la largeur physique du joueur") {
    const BackroomsSpatialStack stack(
        74'021,
        0,
        BackroomsSpatialProfile::RecessedPoolroomsV3);
    auto found_boundary = false;

    for (const auto& placement : stack.placements()) {
        const auto feet_y = placement.floor_y + 1;
        for (int world_z = -96;
             !found_boundary && world_z <= 96;
             ++world_z) {
            for (int world_x = -96;
                 !found_boundary && world_x <= 96;
                 ++world_x) {
                if (!stack.has_player_clearance(
                        world_x,
                        feet_y,
                        world_z,
                        3) ||
                    stack.has_player_clearance(
                        world_x + 1,
                        feet_y,
                        world_z,
                        3)) {
                    continue;
                }

                const auto center_x =
                    static_cast<float>(world_x) + 0.5F;
                const auto boundary_x =
                    static_cast<float>(world_x) + 0.8F;
                const auto center_z =
                    static_cast<float>(world_z) + 0.5F;
                const auto feet =
                    static_cast<float>(feet_y) + 0.001F;

                // Je prouve le cas limite de migration : la colonne centrale
                // reste libre, mais l'AABB de 0,6 m touche le voxel voisin.
                CHECK(
                    stack.has_body_clearance(
                        center_x,
                        feet,
                        center_z,
                        3,
                        0.30F));
                CHECK_FALSE(
                    stack.has_body_clearance(
                        boundary_x,
                        feet,
                        center_z,
                        3,
                        0.30F));
                found_boundary = true;
            }
        }
    }

    REQUIRE(found_boundary);
}

TEST_CASE("les cages verticales restent seches avec paliers libres et chemin voxel continu") {
    const BackroomsSpatialStack stack(63017, 0);
    const auto examples = collect_style_examples(stack, -10, 10);

    for (std::size_t style = 0U;
         style < examples.size();
         ++style) {
        CAPTURE(style);
        REQUIRE(examples[style].has_value());
        const auto& connection = *examples[style];
        CHECK(
            is_backrooms_connector_step(
                stack.sample_block(
                    connection.lower_landing.x,
                    connection.lower_floor_y,
                    connection.lower_landing.z)));
        CHECK(
            is_backrooms_connector_step(
                stack.sample_block(
                    connection.upper_landing.x,
                    connection.upper_floor_y,
                    connection.upper_landing.z)));
        CHECK(
            stack.has_player_clearance(
                connection.lower_landing.x,
                connection.lower_landing.y,
                connection.lower_landing.z,
                2));
        CHECK(
            stack.has_player_clearance(
                connection.upper_landing.x,
                connection.upper_landing.y,
                connection.upper_landing.z,
                2));

        auto previous_point =
            connection_path_point(connection, 0);
        auto previous_support_y =
            expected_support_block_y(connection, 0);
        auto dry_shaft = true;
        auto continuous_path = true;
        auto two_block_clearance = true;
        auto spiral_path_unblocked = true;
        for (int path_index = 0;
             path_index < connection.path_length;
             ++path_index) {
            const auto point =
                connection_path_point(
                    connection,
                    path_index);
            const auto support_y =
                expected_support_block_y(
                    connection,
                    path_index);
            CAPTURE(path_index);
            CAPTURE(point.x);
            CAPTURE(support_y);
            CAPTURE(point.z);
            const auto column =
                stack.rasterize_column(point.x, point.z);
            for (int y = connection.lower_floor_y;
                 y <= connection.upper_floor_y + 2;
                 ++y) {
                dry_shaft =
                    dry_shaft &&
                    column.water[static_cast<std::size_t>(y)] ==
                        WaterState {0};
            }

            const auto support_block =
                column.blocks[
                    static_cast<std::size_t>(support_y)];
            const auto support_is_ramp =
                is_backrooms_ramp(support_block);
            const auto is_deliberate_gap =
                connection.style ==
                    BackroomsVerticalConnectionStyle::BrokenStairs &&
                path_index > 2 &&
                path_index + 3 < connection.path_length &&
                path_index % 7 == 4;
            if (is_deliberate_gap) {
                CHECK_FALSE(
                    is_backrooms_connector_step(support_block));
                const auto forward =
                    forward_for_orientation(
                        connection.orientation_quarter_turns);
                const HorizontalPoint lateral {
                    -forward.z,
                    forward.x,
                };
                for (const auto side : std::array<int, 2U> {{-1, 1}}) {
                    CAPTURE(side);
                    const auto bypass_x =
                        point.x + lateral.x * side;
                    const auto bypass_z =
                        point.z + lateral.z * side;
                    CHECK(
                        is_backrooms_connector_step(
                            stack.sample_block(
                                bypass_x,
                                support_y,
                                bypass_z)));
                    CHECK(
                        stack.has_player_clearance(
                            bypass_x,
                            support_y + 1,
                            bypass_z,
                            2));
                }
            }
            if (!is_deliberate_gap) {
                continuous_path =
                    continuous_path &&
                    (is_backrooms_connector_step(support_block) ||
                     (connection.smooth_ramp &&
                      is_backrooms_ramp(support_block)));
            }
            const auto center_column_is_clear =
                stack.has_player_clearance(
                    point.x,
                    support_y + 1,
                    point.z,
                    2);
            const auto centered_player_is_clear =
                player_volume_is_clear(
                    stack,
                    {
                        static_cast<float>(point.x) + 0.5F,
                        static_cast<float>(support_y) +
                            (support_is_ramp
                                 ? 0.501F
                                 : 1.001F),
                        static_cast<float>(point.z) + 0.5F,
                    });
            CHECK(center_column_is_clear);
            CHECK(centered_player_is_clear);
            two_block_clearance =
                two_block_clearance &&
                center_column_is_clear &&
                centered_player_is_clear;

            if (connection.style ==
                BackroomsVerticalConnectionStyle::SpiralStairs) {
                spiral_path_unblocked =
                    spiral_path_unblocked &&
                    is_backrooms_connector_step(support_block);
                for (int clearance_y = support_y + 1;
                     clearance_y <= support_y + 2;
                     ++clearance_y) {
                    const auto clearance_block =
                        column.blocks[
                            static_cast<std::size_t>(clearance_y)];
                    spiral_path_unblocked =
                        spiral_path_unblocked &&
                        clearance_block ==
                            to_block_id(BlockType::Air) &&
                        clearance_block !=
                            to_block_id(BlockType::PoolroomsMetal);
                }
            }

            if (path_index > 0) {
                const auto horizontal_delta =
                    std::abs(point.x - previous_point.x) +
                    std::abs(point.z - previous_point.z);
                continuous_path =
                    continuous_path &&
                    horizontal_delta == 1 &&
                    std::abs(support_y - previous_support_y) <= 1;
                if (!is_deliberate_gap) {
                    const auto step_x = point.x - previous_point.x;
                    const auto step_z = point.z - previous_point.z;
                    // Je place l'AABB juste aprÃ¨s l'accroche de la marche
                    // suivante : tout son volume doit dÃ©jÃ  Ãªtre libre.
                    const glm::vec3 approach_position {
                        static_cast<float>(point.x) + 0.5F -
                            static_cast<float>(step_x) * 0.799F,
                        static_cast<float>(support_y) +
                            (support_is_ramp
                                 ? 0.001F
                                 : 1.001F),
                        static_cast<float>(point.z) + 0.5F -
                            static_cast<float>(step_z) * 0.799F,
                    };
                    const auto approach_is_clear =
                        player_volume_is_clear(
                            stack,
                            approach_position);
                    CHECK(approach_is_clear);
                    const glm::vec3 descending_approach_position {
                        static_cast<float>(point.x) + 0.5F -
                            static_cast<float>(step_x) * 0.201F,
                        static_cast<float>(support_y) +
                            (support_is_ramp
                                 ? 0.001F
                                 : 1.001F),
                        static_cast<float>(point.z) + 0.5F -
                            static_cast<float>(step_z) * 0.201F,
                    };
                    const auto descending_approach_is_clear =
                        player_volume_is_clear(
                            stack,
                            descending_approach_position);
                    CHECK(descending_approach_is_clear);
                    two_block_clearance =
                        two_block_clearance &&
                        approach_is_clear &&
                        descending_approach_is_clear;
                }
            }
            previous_point = point;
            previous_support_y = support_y;
        }
        CHECK(dry_shaft);
        CHECK(two_block_clearance);
        if (connection.style !=
            BackroomsVerticalConnectionStyle::BrokenStairs) {
            CHECK(continuous_path);
        }
        if (connection.style ==
            BackroomsVerticalConnectionStyle::SpiralStairs) {
            CHECK(spiral_path_unblocked);
        }
    }
}

TEST_CASE("la rasterisation de colonne reste identique aux echantillons ponctuels") {
    const BackroomsSpatialStack stack(63017, 0);
    const auto examples = collect_style_examples(stack, -10, 10);
    REQUIRE(
        examples[
            style_index(
                BackroomsVerticalConnectionStyle::ClassicStairs)]
            .has_value());
    REQUIRE(
        examples[
            style_index(
                BackroomsVerticalConnectionStyle::InclinedRamp)]
            .has_value());
    const auto& stairs =
        *examples[
            style_index(
                BackroomsVerticalConnectionStyle::ClassicStairs)];
    const auto& ramp =
        *examples[
            style_index(
                BackroomsVerticalConnectionStyle::InclinedRamp)];
    const auto stair_middle =
        connection_path_point(
            stairs,
            stairs.path_length / 2);
    const auto ramp_middle =
        connection_path_point(
            ramp,
            ramp.path_length / 2);
    const std::array<HorizontalPoint, 5U> coordinates {{
        {0, 0},
        {-1, -1},
        {-257, 129},
        stair_middle,
        ramp_middle,
    }};

    for (const auto& coordinate : coordinates) {
        CAPTURE(coordinate.x);
        CAPTURE(coordinate.z);
        const auto column =
            stack.rasterize_column(
                coordinate.x,
                coordinate.z);
        for (int y = kWorldMinY;
             y <= kWorldMaxY;
             ++y) {
            CAPTURE(y);
            CHECK(
                column.blocks[static_cast<std::size_t>(y)] ==
                stack.sample_block(
                    coordinate.x,
                    y,
                    coordinate.z));
            CHECK(
                column.water[static_cast<std::size_t>(y)] ==
                stack.sample_water_state(
                    coordinate.x,
                    y,
                    coordinate.z));
        }
    }
}

TEST_CASE("WorldGenerator Backrooms V2 produit le meme chunk en synchrone et incremental") {
    constexpr auto seed = 918273;
    const WorldGenerator generator(
        seed,
        WorldGenerationProfile::Backrooms,
        WorldGenerationVersion::BackroomsV2,
        0);
    CHECK(generator.seed() == seed);
    CHECK(
        generator.profile() ==
        WorldGenerationProfile::Backrooms);
    CHECK(
        generator.generation_version() ==
        WorldGenerationVersion::BackroomsV2);
    CHECK(generator.backrooms_level_at_y(0.0F) == -2);
    CHECK(generator.backrooms_level_at_y(40.0F) == 0);
    CHECK(
        generator.backrooms_theme_at_y(0.0F) ==
        BackroomsTheme::Poolrooms);
    CHECK(
        generator.backrooms_theme_at_y(40.0F) ==
        BackroomsTheme::Offices);

    constexpr ChunkCoord coordinate {-3, 2};
    Chunk synchronous(coordinate);
    generator.generate_chunk(synchronous);

    auto incremental =
        generator.begin_chunk_generation(coordinate);
    generator.advance_chunk_generation(incremental, 0U);
    CHECK(incremental.next_column == 0U);
    auto iterations = 0;
    while (!generator.is_chunk_generation_complete(incremental) &&
           iterations < 64) {
        generator.advance_chunk_generation(incremental, 13U);
        ++iterations;
    }
    REQUIRE(generator.is_chunk_generation_complete(incremental));
    CHECK(incremental.finalized);
    CHECK(iterations == 20);
    CHECK(synchronous.blocks() == incremental.chunk.blocks());
    CHECK(
        synchronous.water_state() ==
        incremental.chunk.water_state());

    constexpr auto preview_local_x = 7;
    constexpr auto preview_local_z = 11;
    const auto preview_world_x =
        coordinate.x * kChunkSizeX + preview_local_x;
    const auto preview_world_z =
        coordinate.z * kChunkSizeZ + preview_local_z;
    const auto generated_column =
        generator.sample_generated_column(
            preview_world_x,
            preview_world_z);
    const auto spatial_column =
        BackroomsSpatialStack(seed, 0).rasterize_column(
            preview_world_x,
            preview_world_z);
    CHECK(generated_column.blocks == spatial_column.blocks);
    CHECK(generated_column.water_state == spatial_column.water);
    for (int y = kWorldMinY; y <= kWorldMaxY; ++y) {
        const auto index = static_cast<std::size_t>(y);
        CHECK(
            generated_column.blocks[index] ==
            synchronous.get_local(
                preview_local_x,
                y,
                preview_local_z));
        CHECK(
            generated_column.water_state[index] ==
            synchronous.get_water_state_local(
                preview_local_x,
                y,
                preview_local_z));
    }

    constexpr std::array<BlockCoord, 6U> samples {{
        {0, 0, 0},
        {5, 20, 7},
        {8, 40, 9},
        {12, 68, 1},
        {15, 98, 15},
        {3, 127, 11},
    }};
    for (const auto& local : samples) {
        const auto world_x =
            coordinate.x * kChunkSizeX + local.x;
        const auto world_z =
            coordinate.z * kChunkSizeZ + local.z;
        CAPTURE(world_x);
        CAPTURE(local.y);
        CAPTURE(world_z);
        CHECK(
            synchronous.get_local(
                local.x,
                local.y,
                local.z) ==
            generator.sample_block(
                world_x,
                local.y,
                world_z));
        CHECK(
            synchronous.get_water_state_local(
                local.x,
                local.y,
                local.z) ==
            generator.sample_water_state(
                world_x,
                local.y,
                world_z));
    }
}

TEST_CASE("WorldGenerator Backrooms V3 garde un chunk humide de connexion identique en synchrone et incremental") {
    constexpr auto seed = 63017;
    constexpr auto anchor = -4;
    const BackroomsSpatialStack stack(
        seed,
        anchor,
        BackroomsSpatialProfile::RecessedPoolroomsV3);
    const WorldGenerator generator(
        seed,
        WorldGenerationProfile::Backrooms,
        WorldGenerationVersion::BackroomsV3,
        anchor);

    auto selected_chunk = std::optional<ChunkCoord> {};
    const auto& placements = stack.placements();
    for (std::size_t level_index = 0U;
         level_index + 1U < placements.size() &&
         !selected_chunk.has_value();
         ++level_index) {
        for (int district_z = -6;
             district_z <= 6 && !selected_chunk.has_value();
             ++district_z) {
            for (int district_x = -6;
                 district_x <= 6 && !selected_chunk.has_value();
                 ++district_x) {
                const auto connection =
                    stack.connection_for_district(
                        placements[level_index].logical_level,
                        district_x,
                        district_z);
                if (!connection.has_value()) {
                    continue;
                }
                const ChunkCoord candidate {
                    floor_divide_for_test(
                        connection->upper_landing.x,
                        kChunkSizeX),
                    floor_divide_for_test(
                        connection->upper_landing.z,
                        kChunkSizeZ),
                };
                const auto base_x = candidate.x * kChunkSizeX;
                const auto base_z = candidate.z * kChunkSizeZ;
                auto contains_water = false;
                for (int local_z = 0;
                     local_z < kChunkSizeZ && !contains_water;
                     ++local_z) {
                    for (int local_x = 0;
                         local_x < kChunkSizeX;
                         ++local_x) {
                        if (stack.sample_water_state(
                                base_x + local_x,
                                connection->upper_floor_y,
                                base_z + local_z) !=
                            WaterState {0}) {
                            contains_water = true;
                            break;
                        }
                    }
                }
                if (contains_water) {
                    selected_chunk = candidate;
                }
            }
        }
    }
    REQUIRE(selected_chunk.has_value());

    Chunk synchronous(*selected_chunk);
    generator.generate_chunk(synchronous);
    auto incremental =
        generator.begin_chunk_generation(*selected_chunk);
    while (!generator.is_chunk_generation_complete(
        incremental)) {
        generator.advance_chunk_generation(
            incremental,
            11U);
    }

    CHECK(
        synchronous.blocks() ==
        incremental.chunk.blocks());
    CHECK(
        synchronous.water_state() ==
        incremental.chunk.water_state());

    auto water_cells = 0;
    auto connector_cells = 0;
    for (std::size_t index = 0U;
         index < synchronous.blocks().size();
         ++index) {
        water_cells +=
            synchronous.water_state()[index] != WaterState {0}
                ? 1
                : 0;
        connector_cells +=
            is_backrooms_connector_step(
                synchronous.blocks()[index]) ||
                    is_backrooms_ramp(
                        synchronous.blocks()[index])
                ? 1
                : 0;
    }
    CHECK(water_cells > 0);
    CHECK(connector_cells > 0);
}

TEST_CASE("le joueur parcourt physiquement une rampe et un escalier Backrooms V2 dans les deux sens") {
    constexpr auto seed = 73191;
    const BackroomsSpatialStack stack(seed, 0);
    const World world(
        seed,
        0,
        WorldGenerationProfile::Backrooms,
        WorldGenerationVersion::BackroomsV2,
        VisualPipeline::LegacyVoxel,
        0);
    constexpr std::array<BackroomsVerticalConnectionStyle, 2U>
        required_styles {{
            BackroomsVerticalConnectionStyle::ClassicStairs,
            BackroomsVerticalConnectionStyle::InclinedRamp,
        }};
    for (const auto required_style : required_styles) {
        CAPTURE(static_cast<int>(required_style));
        const auto candidates =
            collect_office_connections(
                stack,
                required_style,
                12U);
        REQUIRE_FALSE(candidates.empty());

        auto selected_connection =
            std::optional<BackroomsVerticalConnection> {};
        auto selected_observation =
            std::optional<TraversalObservation> {};
        auto best_observation = TraversalObservation {};
        auto best_connection = BackroomsVerticalConnection {};
        for (const auto& candidate : candidates) {
            const auto candidate_observation =
                simulate_round_trip(world, candidate);
            if (candidate_observation.highest_y >
                best_observation.highest_y) {
                best_observation = candidate_observation;
                best_connection = candidate;
            }
            const auto complete_without_fall =
                candidate_observation.reached_upper_landing &&
                candidate_observation.returned_to_lower_landing &&
                candidate_observation.remained_safe &&
                candidate_observation.camera_remained_bounded &&
                candidate_observation.camera_recovered_at_rest &&
                (required_style !=
                         BackroomsVerticalConnectionStyle::ClassicStairs ||
                 (candidate_observation.descent_camera_step_observed &&
                  candidate_observation.descent_camera_smoothed)) &&
                candidate_observation.movement_remained_continuous &&
                candidate_observation.maximum_airborne_time <= 0.10F &&
                candidate_observation.maximum_landing_impact <= 0.001F;
            if (complete_without_fall) {
                selected_connection = candidate;
                selected_observation = candidate_observation;
                break;
            }
        }
        CAPTURE(best_observation.reached_upper_landing);
        CAPTURE(best_observation.returned_to_lower_landing);
        CAPTURE(best_observation.highest_y);
        CAPTURE(best_observation.maximum_horizontal_step);
        CAPTURE(best_observation.maximum_vertical_step);
        CAPTURE(best_observation.maximum_airborne_time);
        CAPTURE(best_observation.maximum_landing_impact);
        CAPTURE(best_observation.descent_camera_step_observed);
        CAPTURE(best_observation.descent_camera_smoothed);
        CAPTURE(best_observation.camera_recovered_at_rest);
        CAPTURE(best_observation.maximum_path_progress);
        CAPTURE(best_observation.final_position.x);
        CAPTURE(best_observation.final_position.y);
        CAPTURE(best_observation.final_position.z);
        CAPTURE(best_connection.lower_level);
        CAPTURE(best_connection.district_x);
        CAPTURE(best_connection.district_z);
        CAPTURE(best_connection.orientation_quarter_turns);
        CAPTURE(best_connection.path_length);
        CAPTURE(best_connection.lower_landing.x);
        CAPTURE(best_connection.lower_landing.y);
        CAPTURE(best_connection.lower_landing.z);
        const auto best_forward =
            forward_for_orientation(
                best_connection.orientation_quarter_turns);
        auto attempted_exit = best_observation.final_position;
        attempted_exit.x +=
            static_cast<float>(best_forward.x) *
            (5.6F / 30.0F);
        attempted_exit.z +=
            static_cast<float>(best_forward.z) *
            (5.6F / 30.0F);
        attempted_exit.y =
            static_cast<float>(best_connection.upper_landing.y) +
            0.001F;
        const auto exit_obstacle =
            first_collidable_in_player_volume(
                stack,
                attempted_exit);
        CAPTURE(attempted_exit.x);
        CAPTURE(attempted_exit.y);
        CAPTURE(attempted_exit.z);
        CAPTURE(exit_obstacle.has_value());
        const auto obstacle_x =
            exit_obstacle.has_value() ? exit_obstacle->x : 0;
        const auto obstacle_y =
            exit_obstacle.has_value() ? exit_obstacle->y : 0;
        const auto obstacle_z =
            exit_obstacle.has_value() ? exit_obstacle->z : 0;
        const auto obstacle_block =
            exit_obstacle.has_value()
                ? stack.sample_block(
                      obstacle_x,
                      obstacle_y,
                      obstacle_z)
                : to_block_id(BlockType::Air);
        CAPTURE(obstacle_x);
        CAPTURE(obstacle_y);
        CAPTURE(obstacle_z);
        CAPTURE(static_cast<int>(obstacle_block));
        REQUIRE(selected_connection.has_value());
        REQUIRE(selected_observation.has_value());
        const auto& connection = *selected_connection;
        const auto& observation = *selected_observation;
        const auto physical_rise =
            static_cast<float>(
                connection.upper_floor_y -
                connection.lower_floor_y);
        const auto visited_band_count =
            static_cast<int>(
                std::count(
                    observation.visited_height_bands.begin(),
                    observation.visited_height_bands.end(),
                    true));

        CHECK(observation.reached_upper_landing);
        CHECK(observation.returned_to_lower_landing);
        CHECK(
            observation.highest_y >=
            static_cast<float>(connection.upper_landing.y) -
                0.10F);
        CHECK(
            observation.highest_y -
                static_cast<float>(connection.lower_landing.y) >=
            physical_rise - 0.10F);
        CHECK(
            observation.highest_y -
                observation.lowest_return_y >=
            physical_rise - 0.75F);
        CHECK(visited_band_count >= 12);
        CHECK(observation.remained_safe);
        CHECK(observation.camera_remained_bounded);
        CHECK(observation.camera_recovered_at_rest);
        if (required_style ==
            BackroomsVerticalConnectionStyle::ClassicStairs) {
            CHECK(observation.descent_camera_step_observed);
            CHECK(observation.descent_camera_smoothed);
        }
        CHECK(observation.movement_remained_continuous);
        CHECK(observation.maximum_horizontal_step <= 0.20F);
        CHECK(observation.maximum_vertical_step <= 1.06F);
        CHECK(observation.maximum_airborne_time <= 0.10F);
        CHECK(observation.maximum_landing_impact <= 0.001F);
    }
}

TEST_CASE("l'escalier visible du hub se parcourt physiquement dans les deux sens") {
    constexpr auto seed = 1827706392;
    const BackroomsSpatialStack stack(seed, 0);
    const auto& placements = stack.placements();
    const auto ascent =
        stack.connection_for_district(
            placements[2].logical_level,
            0,
            0);
    REQUIRE(ascent.has_value());
    REQUIRE(
        ascent->style ==
        BackroomsVerticalConnectionStyle::ClassicStairs);

    const World world(
        seed,
        0,
        WorldGenerationProfile::Backrooms,
        WorldGenerationVersion::BackroomsV2,
        VisualPipeline::LegacyVoxel,
        0);
    const auto observation =
        simulate_round_trip(world, *ascent);
    CAPTURE(observation.highest_y);
    CAPTURE(observation.lowest_return_y);
    CAPTURE(observation.maximum_airborne_time);
    CAPTURE(observation.maximum_landing_impact);
    CAPTURE(observation.final_position.x);
    CAPTURE(observation.final_position.y);
    CAPTURE(observation.final_position.z);
    CHECK(observation.reached_upper_landing);
    CHECK(observation.returned_to_lower_landing);
    CHECK(observation.remained_safe);
    CHECK(observation.movement_remained_continuous);
    CHECK(observation.maximum_airborne_time <= 0.10F);
    CHECK(observation.maximum_landing_impact <= 0.001F);
    CHECK(observation.camera_remained_bounded);
    CHECK(observation.camera_recovered_at_rest);
}

TEST_CASE("une chute rapide est interceptee par la surface analytique d'une rampe Backrooms") {
    constexpr auto seed = 73191;
    const BackroomsSpatialStack stack(seed, 0);
    const auto ramps =
        collect_office_connections(
            stack,
            BackroomsVerticalConnectionStyle::InclinedRamp,
            1U);
    REQUIRE(ramps.size() == 1U);
    const auto& ramp = ramps.front();
    const auto path_index = ramp.path_length / 2;
    const auto point =
        connection_path_point(
            ramp,
            path_index);
    const auto ramp_block_y =
        expected_support_block_y(
            ramp,
            path_index);
    const auto expected_surface_y =
        static_cast<float>(ramp_block_y) + 0.5F + 0.001F;

    const World world(
        seed,
        0,
        WorldGenerationProfile::Backrooms,
        WorldGenerationVersion::BackroomsV2,
        VisualPipeline::LegacyVoxel,
        0);
    const glm::vec3 start {
        static_cast<float>(point.x) + 0.5F,
        static_cast<float>(ramp_block_y) + 2.0F,
        static_cast<float>(point.z) + 0.5F,
    };
    PlayerController player(start);
    auto state = player.state();
    state.position = start;
    state.velocity = {0.0F, -18.0F, 0.0F};
    state.on_ground = false;
    state.fall_start_y = start.y;
    player.load_state(state);

    // Je force une vitesse qui franchirait plusieurs dixiemes de voxel par
    // sous-pas afin de verrouiller explicitement l'anti-tunneling vertical.
    for (int frame = 0;
         frame < 8 && !player.state().on_ground;
         ++frame) {
        player.update(
            PlayerInput {},
            1.0F / 30.0F,
            world);
    }

    CHECK(player.state().on_ground);
    CHECK_FALSE(player.state().dead);
    CHECK(
        player.position().y ==
        doctest::Approx(expected_surface_y).epsilon(0.002));
    CHECK(player.position().y > static_cast<float>(ramp_block_y));
}

} // namespace valcraft
