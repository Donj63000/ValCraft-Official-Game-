#include "gameplay/BackroomsJack.h"

#include <doctest/doctest.h>

#include <glm/geometric.hpp>
#include <glm/vec2.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

namespace valcraft {

namespace {

constexpr float kTestPi = 3.14159265358979323846F;

struct VisiblePair {
    glm::vec3 jack {0.0F};
    glm::vec3 player {0.0F};
    bool found = false;
};

[[nodiscard]] auto test_yaw_toward(
    const glm::vec3& from,
    const glm::vec3& to) noexcept -> float {
    const auto delta = to - from;
    return std::atan2(delta.x, -delta.z) * 180.0F / kTestPi;
}

[[nodiscard]] auto test_horizontal_distance(
    const glm::vec3& first,
    const glm::vec3& second) noexcept -> float {
    return glm::length(
        glm::vec2 {
            first.x - second.x,
            first.z - second.z,
        });
}

[[nodiscard]] auto test_player(
    const glm::vec3& feet,
    const glm::vec3& look_target,
    float sprint_speed = 7.2F) noexcept
    -> BackroomsJackPlayerContext {
    const auto eye = feet + glm::vec3 {0.0F, 1.62F, 0.0F};
    auto direction = look_target - eye;
    if (glm::dot(direction, direction) <= 0.000001F) {
        direction = {0.0F, 0.0F, -1.0F};
    } else {
        direction = glm::normalize(direction);
    }
    return {
        feet,
        eye,
        direction,
        sprint_speed,
    };
}

[[nodiscard]] auto cell_position(
    const BackroomsGenerator& generator,
    int world_x,
    int world_z) noexcept -> glm::vec3 {
    const auto column = generator.sample_column(world_x, world_z);
    return {
        static_cast<float>(world_x) + 0.5F,
        static_cast<float>(column.floor_y + 1) + 0.001F,
        static_cast<float>(world_z) + 0.5F,
    };
}

[[nodiscard]] auto find_visible_pair(
    const BackroomsGenerator& generator,
    float minimum_distance,
    float maximum_distance) noexcept -> VisiblePair {
    const auto spawn = generator.spawn_block();
    constexpr std::array<std::array<int, 2>, 8> directions {{
        {{1, 0}},
        {{-1, 0}},
        {{0, 1}},
        {{0, -1}},
        {{1, 1}},
        {{1, -1}},
        {{-1, 1}},
        {{-1, -1}},
    }};
    const auto minimum_step =
        std::max(1, static_cast<int>(std::ceil(minimum_distance)));
    const auto maximum_step =
        std::max(minimum_step, static_cast<int>(maximum_distance));

    for (auto radius = 0; radius <= 96; ++radius) {
        for (auto offset_z = -radius;
             offset_z <= radius;
             ++offset_z) {
            for (auto offset_x = -radius;
                 offset_x <= radius;
                 ++offset_x) {
                if (radius > 0 &&
                    std::abs(offset_x) != radius &&
                    std::abs(offset_z) != radius) {
                    continue;
                }
                const auto jack_x = spawn.x + offset_x;
                const auto jack_z = spawn.z + offset_z;
                if (!generator.is_walkable(jack_x, jack_z)) {
                    continue;
                }
                const auto jack =
                    cell_position(generator, jack_x, jack_z);
                for (const auto& direction : directions) {
                    for (auto step = minimum_step;
                         step <= maximum_step;
                         ++step) {
                        const auto player_x =
                            jack_x + direction[0] * step;
                        const auto player_z =
                            jack_z + direction[1] * step;
                        if (!generator.is_walkable(
                                player_x,
                                player_z)) {
                            continue;
                        }
                        const auto player =
                            cell_position(
                                generator,
                                player_x,
                                player_z);
                        const auto distance =
                            std::sqrt(
                                static_cast<float>(
                                    direction[0] * direction[0] +
                                    direction[1] * direction[1])) *
                            static_cast<float>(step);
                        if (distance < minimum_distance ||
                            distance > maximum_distance) {
                            continue;
                        }
                        const auto player_context =
                            test_player(player, jack);
                        const auto jack_eye =
                            jack + glm::vec3 {0.0F, 3.42F, 0.0F};
                        if (backrooms_jack_has_line_of_sight(
                                generator,
                                jack_eye,
                                player_context.eye_position)) {
                            return {jack, player, true};
                        }
                    }
                }
            }
        }
    }
    return {};
}

[[nodiscard]] auto find_blocked_pair(
    const BackroomsGenerator& generator) noexcept -> VisiblePair {
    const auto spawn = generator.spawn_block();
    constexpr std::array<std::array<int, 2>, 4> directions {{
        {{1, 0}},
        {{-1, 0}},
        {{0, 1}},
        {{0, -1}},
    }};
    for (auto offset_z = -96; offset_z <= 96; ++offset_z) {
        for (auto offset_x = -96; offset_x <= 96; ++offset_x) {
            const auto jack_x = spawn.x + offset_x;
            const auto jack_z = spawn.z + offset_z;
            if (!generator.is_walkable(jack_x, jack_z)) {
                continue;
            }
            const auto jack =
                cell_position(generator, jack_x, jack_z);
            for (const auto& direction : directions) {
                for (auto step = 4; step <= 14; ++step) {
                    const auto player_x =
                        jack_x + direction[0] * step;
                    const auto player_z =
                        jack_z + direction[1] * step;
                    if (!generator.is_walkable(
                            player_x,
                            player_z)) {
                        continue;
                    }
                    const auto player =
                        cell_position(
                            generator,
                            player_x,
                            player_z);
                    const auto player_context =
                        test_player(player, jack);
                    const auto jack_eye =
                        jack + glm::vec3 {0.0F, 3.42F, 0.0F};
                    if (!backrooms_jack_has_line_of_sight(
                            generator,
                            jack_eye,
                            player_context.eye_position)) {
                        return {jack, player, true};
                    }
                }
            }
        }
    }
    return {};
}

[[nodiscard]] auto ready_neighborhood(
    const ChunkCoord& center,
    bool ready = true) noexcept -> BackroomsJackChunkReadiness {
    BackroomsJackChunkReadiness readiness {};
    readiness.center_chunk = center;
    readiness.ready.fill(ready);
    return readiness;
}

[[nodiscard]] auto count_event(
    const BackroomsJackUpdateResult& result,
    BackroomsJackEventKind kind) noexcept -> std::size_t {
    auto count = std::size_t {0U};
    for (std::size_t index = 0U;
         index < result.event_count;
         ++index) {
        count += result.events[index].kind == kind ? 1U : 0U;
    }
    return count;
}

[[nodiscard]] auto find_clearance_cell(
    const BackroomsGenerator& generator,
    bool standing) noexcept -> std::optional<glm::vec3> {
    for (auto module_z = -16; module_z <= 16; ++module_z) {
        for (auto module_x = -16; module_x <= 16; ++module_x) {
            for (auto local_z = 0;
                 local_z < kBackroomsModuleSize;
                 ++local_z) {
                for (auto local_x = 0;
                     local_x < kBackroomsModuleSize;
                     ++local_x) {
                    const auto world_x =
                        module_x * kBackroomsModuleSize + local_x;
                    const auto world_z =
                        module_z * kBackroomsModuleSize + local_z;
                    const auto column =
                        generator.sample_column(world_x, world_z);
                    if (column.wall) {
                        continue;
                    }
                    const auto clearance = static_cast<float>(
                        column.ceiling_y - (column.floor_y + 1));
                    if (clearance < kBackroomsJackBentHeight + 0.02F) {
                        continue;
                    }
                    const auto allows_standing =
                        clearance >= kBackroomsJackStandingClearance;
                    if (allows_standing == standing) {
                        return cell_position(
                            generator,
                            world_x,
                            world_z);
                    }
                }
            }
        }
    }
    return std::nullopt;
}

[[nodiscard]] auto simulate_chase(
    const BackroomsGenerator& generator,
    const VisiblePair& pair,
    int frames_per_second,
    float duration) -> BackroomsJackState {
    BackroomsJackState state {};
    state.phase = BackroomsJackPhase::Chasing;
    state.active = true;
    state.position = pair.jack;
    state.last_seen_player_position = pair.player;
    state.previous_player_position = pair.player;
    state.body_yaw_degrees =
        test_yaw_toward(pair.jack, pair.player);
    state.chase_event_emitted = true;
    BackroomsJackRuntime runtime {};
    BackroomsJackUpdateContext context {};
    context.player = test_player(pair.player, pair.jack);
    const auto center =
        backrooms_jack_chunk_at(pair.player);
    context.chunk_readiness =
        ready_neighborhood(center);
    context.allow_spawn = false;

    const auto frame_count = static_cast<int>(
        std::round(
            duration *
            static_cast<float>(frames_per_second)));
    const auto dt =
        1.0F / static_cast<float>(frames_per_second);
    for (auto frame = 0; frame < frame_count; ++frame) {
        static_cast<void>(update_backrooms_jack(
            state,
            runtime,
            generator,
            context,
            dt));
    }
    return state;
}

}

TEST_CASE("Jack assainit toutes ses valeurs persistantes") {
    BackroomsJackState state {};
    state.phase = static_cast<BackroomsJackPhase>(255U);
    state.position = {
        std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity(),
    };
    state.last_seen_player_position = state.position;
    state.previous_player_position = state.position;
    state.body_yaw_degrees =
        std::numeric_limits<float>::infinity();
    state.head_yaw_degrees =
        std::numeric_limits<float>::quiet_NaN();
    state.hunch_ratio = 8.0F;
    state.motion_amount = -3.0F;
    state.phase_seconds =
        std::numeric_limits<float>::quiet_NaN();
    state.suspicion =
        std::numeric_limits<float>::infinity();
    state.cooldown_seconds =
        std::numeric_limits<float>::infinity();
    state.evaded_chunk_count = 99U;
    state.random_state = 0U;
    state.next_event_sequence = 0U;
    state.active = true;
    state.logical_level =
        kBackroomsMaximumLogicalLevel + 1;

    const auto sanitized =
        sanitize_backrooms_jack_state(state);

    CHECK(sanitized.phase == BackroomsJackPhase::Dormant);
    CHECK_FALSE(sanitized.active);
    CHECK(std::isfinite(sanitized.position.x));
    CHECK(std::isfinite(sanitized.position.y));
    CHECK(std::isfinite(sanitized.position.z));
    CHECK(std::isfinite(sanitized.body_yaw_degrees));
    CHECK(std::isfinite(sanitized.head_yaw_degrees));
    CHECK(sanitized.hunch_ratio >= 0.0F);
    CHECK(sanitized.hunch_ratio <= 1.0F);
    CHECK(sanitized.motion_amount == doctest::Approx(0.0F));
    CHECK(sanitized.evaded_chunk_count <=
          sanitized.evaded_chunks.size());
    CHECK(sanitized.random_state != 0U);
    CHECK(sanitized.next_event_sequence != 0U);
    CHECK(sanitized.logical_level == 0);
}

TEST_CASE("les coordonnees de chunk restent correctes dans le monde negatif") {
    CHECK(
        backrooms_jack_chunk_at(
            glm::vec3 {-0.01F, 41.0F, -16.01F}) ==
        ChunkCoord {-1, -2});
    CHECK(
        backrooms_jack_chunk_at(
            glm::vec3 {15.99F, 41.0F, 16.0F}) ==
        ChunkCoord {0, 1});
}

TEST_CASE("la premiere apparition reste rare et precedee d une longue grace") {
    for (std::uint32_t seed = 1U; seed <= 128U; ++seed) {
        const auto state = initialize_backrooms_jack(seed);
        CHECK(state.phase == BackroomsJackPhase::Dormant);
        CHECK_FALSE(state.active);
        CHECK(state.spawn_check_seconds >= 120.0F);
        CHECK(state.spawn_check_seconds <= 300.0F);
        CHECK(state.random_state != 0U);
    }
}

TEST_CASE("Jack ne produit aucun rendu ni interference sur un autre niveau logique") {
    auto state =
        initialize_backrooms_jack(7331U, -2);
    state.phase = BackroomsJackPhase::Chasing;
    state.active = true;
    state.motion_amount = 1.0F;

    CHECK(
        make_backrooms_jack_render_view(state, -2)
            .visible);
    CHECK(
        make_backrooms_jack_light_interference_view(
            state,
            -2)
            .active);
    CHECK_FALSE(
        make_backrooms_jack_render_view(state, 0)
            .visible);
    CHECK_FALSE(
        make_backrooms_jack_light_interference_view(
            state,
            0)
            .active);
}

TEST_CASE("la navigation de Jack identifie l'eau Poolrooms et applique le ralentissement leger") {
    const BackroomsGenerator generator {7331, -2};
    CHECK(generator.is_poolrooms());
    auto found_water = false;
    auto found_dry = false;

    for (int chunk_z = -3;
         chunk_z <= 3 &&
         !(found_water && found_dry);
         ++chunk_z) {
        for (int chunk_x = -3;
             chunk_x <= 3 &&
             !(found_water && found_dry);
             ++chunk_x) {
            const auto grid =
                build_backrooms_jack_navigation_grid(
                    generator,
                    {chunk_x, chunk_z});
            CHECK(grid.logical_level == -2);
            for (const auto& cell : grid.cells) {
                if (!cell.walkable) {
                    continue;
                }
                if (cell.in_water) {
                    found_water = true;
                    CHECK(
                        cell.movement_speed_multiplier ==
                        doctest::Approx(
                            kBackroomsJackPoolroomsSpeedMultiplier));
                } else {
                    found_dry = true;
                    CHECK(
                        cell.movement_speed_multiplier ==
                        doctest::Approx(1.0F));
                }
            }
        }
    }

    CHECK(found_water);
    CHECK(found_dry);
}

TEST_CASE("Jack voit uniquement devant lui avec une ligne de vue libre") {
    const BackroomsGenerator generator {1337};
    const auto pair =
        find_visible_pair(generator, 5.0F, 10.0F);
    REQUIRE(pair.found);
    const auto player = test_player(
        pair.player,
        pair.jack + glm::vec3 {0.0F, 3.42F, 0.0F});
    const auto facing_yaw =
        test_yaw_toward(pair.jack, pair.player);

    const auto front = evaluate_backrooms_jack_perception(
        generator,
        player,
        pair.jack,
        facing_yaw,
        0.0F);
    const auto back = evaluate_backrooms_jack_perception(
        generator,
        player,
        pair.jack,
        facing_yaw + 180.0F,
        0.0F);

    CHECK(front.line_of_sight);
    CHECK(front.jack_sees_player);
    CHECK(front.player_faces_jack);
    CHECK_FALSE(back.jack_sees_player);
    CHECK(back.player_sees_jack);
}

TEST_CASE("l observation lointaine augmente la menace beaucoup plus lentement") {
    const BackroomsGenerator generator {4242};
    const auto near_pair =
        find_visible_pair(generator, 6.0F, 10.0F);
    const auto far_pair =
        find_visible_pair(generator, 37.0F, 42.0F);
    REQUIRE(near_pair.found);
    REQUIRE(far_pair.found);

    const auto observe_for_one_second =
        [&](const VisiblePair& pair) {
            BackroomsJackState state {};
            state.phase = BackroomsJackPhase::Watching;
            state.active = true;
            state.position = pair.jack;
            state.body_yaw_degrees =
                test_yaw_toward(pair.jack, pair.player);
            state.notice_event_emitted = true;
            BackroomsJackRuntime runtime {};
            BackroomsJackUpdateContext context {};
            context.player = test_player(
                pair.player,
                pair.jack + glm::vec3 {0.0F, 3.42F, 0.0F});
            context.chunk_readiness = ready_neighborhood(
                backrooms_jack_chunk_at(pair.player));
            context.allow_spawn = false;
            for (auto frame = 0; frame < 60; ++frame) {
                static_cast<void>(update_backrooms_jack(
                    state,
                    runtime,
                    generator,
                    context,
                    1.0F / 60.0F));
            }
            return state;
        };

    const auto near_state =
        observe_for_one_second(near_pair);
    const auto far_state =
        observe_for_one_second(far_pair);
    CHECK(near_state.phase == BackroomsJackPhase::Watching);
    CHECK(far_state.phase == BackroomsJackPhase::Watching);
    CHECK(near_state.suspicion > far_state.suspicion * 2.5F);
    CHECK(far_state.suspicion < 0.5F);
}

TEST_CASE("un mur Backrooms bloque completement la perception de Jack") {
    const BackroomsGenerator generator {1337};
    const auto pair = find_blocked_pair(generator);
    REQUIRE(pair.found);
    const auto player =
        test_player(pair.player, pair.jack);
    const auto perception =
        evaluate_backrooms_jack_perception(
            generator,
            player,
            pair.jack,
            test_yaw_toward(pair.jack, pair.player),
            0.0F);

    CHECK_FALSE(perception.line_of_sight);
    CHECK_FALSE(perception.jack_sees_player);
    CHECK_FALSE(perception.player_sees_jack);
}

TEST_CASE("le spawn reste cache accessible et dans les huit chunks voisins") {
    const BackroomsGenerator generator {7331};
    const auto spawn = generator.spawn_block();
    const glm::vec3 player_position {
        static_cast<float>(spawn.x) + 0.5F,
        static_cast<float>(spawn.y),
        static_cast<float>(spawn.z) + 0.5F,
    };
    const auto player = test_player(
        player_position,
        player_position + glm::vec3 {40.0F, 0.0F, 0.0F});
    const auto center =
        backrooms_jack_chunk_at(player_position);
    const auto grid =
        build_backrooms_jack_navigation_grid(generator, center);
    const auto readiness =
        ready_neighborhood(center);

    const auto first = select_backrooms_jack_spawn(
        generator,
        grid,
        player,
        readiness,
        0x12345678U);
    const auto repeated = select_backrooms_jack_spawn(
        generator,
        grid,
        player,
        readiness,
        0x12345678U);
    REQUIRE(first.found);
    CHECK(repeated.found);
    CHECK(first.position == repeated.position);
    CHECK(first.body_yaw_degrees ==
          repeated.body_yaw_degrees);
    CHECK(first.next_random_state ==
          repeated.next_random_state);

    const auto distance =
        glm::length(
            glm::vec2 {
                first.position.x - player_position.x,
                first.position.z - player_position.z,
            });
    CHECK(distance >= kBackroomsJackMinimumSpawnDistance);
    CHECK(distance <= kBackroomsJackMaximumSpawnDistance);
    const auto candidate_chunk =
        backrooms_jack_chunk_at(first.position);
    CHECK(std::abs(candidate_chunk.x - center.x) <= 1);
    CHECK(std::abs(candidate_chunk.z - center.z) <= 1);
    CHECK_FALSE(candidate_chunk == center);
    const auto cell = backrooms_jack_navigation_cell(
        grid,
        static_cast<int>(std::floor(first.position.x)),
        static_cast<int>(std::floor(first.position.z)));
    REQUIRE(cell != nullptr);
    CHECK(cell->walkable);
    CHECK_FALSE(
        evaluate_backrooms_jack_perception(
            generator,
            player,
            first.position,
            first.body_yaw_degrees,
            first.initial_hunch)
            .player_sees_jack);

    const auto unavailable = select_backrooms_jack_spawn(
        generator,
        grid,
        player,
        ready_neighborhood(center, false),
        0x12345678U);
    CHECK_FALSE(unavailable.found);
    CHECK(unavailable.next_random_state == 0x12345678U);
}

TEST_CASE("le chemin A etoile contourne les murs sans couper les coins") {
    BackroomsJackNavigationGrid grid {};
    grid.origin_world_x = 0;
    grid.origin_world_z = 0;
    for (auto z = 0; z <= 4; ++z) {
        for (auto x = 0; x <= 4; ++x) {
            auto& cell =
                grid.cells[
                    static_cast<std::size_t>(
                        z * kBackroomsJackNavigationSide + x)];
            cell.walkable = true;
            cell.standing_allowed = true;
            cell.clearance = 6.0F;
        }
    }
    for (auto z = 0; z < 4; ++z) {
        grid.cells[
                static_cast<std::size_t>(
                    z * kBackroomsJackNavigationSide + 2)]
            .walkable = false;
    }

    const auto path = find_backrooms_jack_path(
        grid,
        {0, 0},
        {4, 0});
    const auto repeated = find_backrooms_jack_path(
        grid,
        {0, 0},
        {4, 0});
    REQUIRE(path.count > 0U);
    CHECK(path.nodes[path.count - 1U] ==
          BackroomsJackGridPoint {4, 0});
    CHECK(path.count == repeated.count);
    for (std::size_t index = 0U; index < path.count; ++index) {
        CHECK(path.nodes[index] == repeated.nodes[index]);
        const auto crosses_closed_wall =
            path.nodes[index].x == 2 &&
            path.nodes[index].z < 4;
        CHECK_FALSE(crosses_closed_wall);
    }

    BackroomsJackNavigationGrid corner_grid {};
    corner_grid.origin_world_x = 0;
    corner_grid.origin_world_z = 0;
    corner_grid.cells[0].walkable = true;
    corner_grid
        .cells[static_cast<std::size_t>(
            kBackroomsJackNavigationSide + 1)]
        .walkable = true;
    const auto forbidden_diagonal =
        find_backrooms_jack_path(
            corner_grid,
            {0, 0},
            {1, 1});
    CHECK(forbidden_diagonal.empty());
}

TEST_CASE("Jack se penche seulement sous les plafonds trop bas") {
    const BackroomsGenerator generator {1337};
    const auto bent_cell =
        find_clearance_cell(generator, false);
    const auto standing_cell =
        find_clearance_cell(generator, true);
    REQUIRE(bent_cell.has_value());
    REQUIRE(standing_cell.has_value());

    const auto run_posture =
        [&](const glm::vec3& position,
            float initial_hunch) {
            BackroomsJackState state {};
            state.phase = BackroomsJackPhase::Jumpscare;
            state.active = true;
            state.position = position;
            state.hunch_ratio = initial_hunch;
            state.screamer_event_emitted = true;
            BackroomsJackRuntime runtime {};
            BackroomsJackUpdateContext context {};
            context.player = test_player(
                position,
                position + glm::vec3 {0.0F, 0.0F, -1.0F});
            context.chunk_readiness = ready_neighborhood(
                backrooms_jack_chunk_at(position));
            context.allow_spawn = false;
            for (auto frame = 0; frame < 60; ++frame) {
                static_cast<void>(update_backrooms_jack(
                    state,
                    runtime,
                    generator,
                    context,
                    1.0F / 60.0F));
            }
            return state.hunch_ratio;
        };

    CHECK(run_posture(*bent_cell, 0.0F) > 0.95F);
    CHECK(run_posture(*standing_cell, 1.0F) < 0.05F);
}

TEST_CASE("la FSM observe puis poursuit apres plusieurs secondes") {
    const BackroomsGenerator generator {4242};
    const auto pair =
        find_visible_pair(generator, 5.0F, 10.0F);
    REQUIRE(pair.found);
    BackroomsJackState state {};
    state.phase = BackroomsJackPhase::Wandering;
    state.active = true;
    state.position = pair.jack;
    state.body_yaw_degrees =
        test_yaw_toward(pair.jack, pair.player);
    BackroomsJackRuntime runtime {};
    BackroomsJackUpdateContext context {};
    context.player = test_player(pair.player, pair.jack);
    context.chunk_readiness = ready_neighborhood(
        backrooms_jack_chunk_at(pair.player));
    context.allow_spawn = false;

    auto notice_count = std::size_t {0U};
    auto chase_count = std::size_t {0U};
    for (auto frame = 0; frame < 300; ++frame) {
        const auto result = update_backrooms_jack(
            state,
            runtime,
            generator,
            context,
            1.0F / 60.0F);
        notice_count += count_event(
            result,
            BackroomsJackEventKind::Notice);
        chase_count += count_event(
            result,
            BackroomsJackEventKind::Chase);
        if (frame == 60) {
            CHECK(state.phase == BackroomsJackPhase::Watching);
            CHECK(state.suspicion <
                  0.6F * kBackroomsJackStandingHeight);
        }
        if (state.phase == BackroomsJackPhase::Chasing) {
            break;
        }
    }

    CHECK(state.phase == BackroomsJackPhase::Chasing);
    CHECK(notice_count == 1U);
    CHECK(chase_count == 1U);
    CHECK(std::abs(state.head_yaw_degrees) <= 12.1F);
}

TEST_CASE("Jack peut etre seme seulement hors regard apres plusieurs chunks") {
    const BackroomsGenerator generator {7331};
    const auto spawn = generator.spawn_block();
    const glm::vec3 player_position {
        static_cast<float>(spawn.x) + 0.5F,
        static_cast<float>(spawn.y),
        static_cast<float>(spawn.z) + 0.5F,
    };
    const auto player = test_player(
        player_position,
        player_position + glm::vec3 {1.0F, 0.0F, 0.0F});
    const auto center =
        backrooms_jack_chunk_at(player_position);
    const auto grid =
        build_backrooms_jack_navigation_grid(generator, center);
    auto selection = BackroomsJackSpawnSelection {};
    for (std::uint32_t seed = 1U;
         seed < 200U;
         ++seed) {
        const auto candidate = select_backrooms_jack_spawn(
            generator,
            grid,
            player,
            ready_neighborhood(center),
            seed);
        if (candidate.found &&
            glm::length(
                glm::vec2 {
                    candidate.position.x - player_position.x,
                    candidate.position.z - player_position.z,
                }) >= 24.0F) {
            selection = candidate;
            break;
        }
    }
    REQUIRE(selection.found);

    BackroomsJackState state {};
    state.phase = BackroomsJackPhase::Searching;
    state.active = true;
    state.position = selection.position;
    state.body_yaw_degrees =
        test_yaw_toward(selection.position, player_position) +
        180.0F;
    state.last_seen_player_position = selection.position;
    state.lost_sight_seconds = 10.0F;
    state.evaded_chunk_count = 3U;
    state.has_last_evade_chunk = true;
    state.last_evade_chunk = center;
    state.previous_player_position = player_position;
    state.has_previous_player_position = true;
    BackroomsJackRuntime runtime {};
    BackroomsJackUpdateContext context {};
    context.player = player;
    context.chunk_readiness =
        ready_neighborhood(center);
    context.allow_spawn = false;

    const auto result = update_backrooms_jack(
        state,
        runtime,
        generator,
        context,
        1.0F / 60.0F);

    CHECK(state.phase == BackroomsJackPhase::Cooldown);
    CHECK_FALSE(state.active);
    CHECK(state.cooldown_seconds >=
          kBackroomsJackMinimumCooldownSeconds);
    CHECK(state.cooldown_seconds <=
          kBackroomsJackMaximumCooldownSeconds);
    CHECK(count_event(
              result,
              BackroomsJackEventKind::Vanished) == 1U);
}

TEST_CASE("la capture et le screamer arrivent une seule fois en poursuite") {
    const BackroomsGenerator generator {1337};
    const auto pair =
        find_visible_pair(generator, 5.0F, 10.0F);
    REQUIRE(pair.found);
    const auto direction =
        glm::normalize(pair.player - pair.jack);
    const auto close_player =
        pair.jack + direction * 0.75F;
    BackroomsJackUpdateContext context {};
    context.player =
        test_player(close_player, pair.jack);
    context.chunk_readiness = ready_neighborhood(
        backrooms_jack_chunk_at(close_player));
    context.allow_spawn = false;

    BackroomsJackState harmless {};
    harmless.phase = BackroomsJackPhase::Wandering;
    harmless.active = true;
    harmless.position = pair.jack;
    harmless.body_yaw_degrees =
        test_yaw_toward(pair.jack, close_player) + 180.0F;
    BackroomsJackRuntime harmless_runtime {};
    const auto harmless_result =
        update_backrooms_jack(
            harmless,
            harmless_runtime,
            generator,
            context,
            1.0F / 60.0F);
    CHECK_FALSE(harmless_result.caught_player);
    CHECK(count_event(
              harmless_result,
              BackroomsJackEventKind::Screamer) == 0U);

    BackroomsJackState chasing {};
    chasing.phase = BackroomsJackPhase::Chasing;
    chasing.active = true;
    chasing.position = pair.jack;
    chasing.last_seen_player_position = close_player;
    chasing.body_yaw_degrees =
        test_yaw_toward(pair.jack, close_player);
    chasing.chase_event_emitted = true;
    BackroomsJackRuntime chasing_runtime {};
    const auto caught = update_backrooms_jack(
        chasing,
        chasing_runtime,
        generator,
        context,
        1.0F / 60.0F);
    CHECK(caught.caught_player);
    CHECK(chasing.phase == BackroomsJackPhase::Jumpscare);
    CHECK(count_event(
              caught,
              BackroomsJackEventKind::Screamer) == 1U);

    auto repeated_screamers = std::size_t {0U};
    for (auto frame = 0; frame < 120; ++frame) {
        const auto repeated = update_backrooms_jack(
            chasing,
            chasing_runtime,
            generator,
            context,
            1.0F / 60.0F);
        CHECK_FALSE(repeated.caught_player);
        repeated_screamers += count_event(
            repeated,
            BackroomsJackEventKind::Screamer);
    }
    CHECK(repeated_screamers == 0U);
}

TEST_CASE("les pas botte et bois alternent selon la distance parcourue") {
    const BackroomsGenerator generator {9001};
    const auto pair =
        find_visible_pair(generator, 12.0F, 18.0F);
    REQUIRE(pair.found);
    BackroomsJackState state {};
    state.phase = BackroomsJackPhase::Chasing;
    state.active = true;
    state.position = pair.jack;
    state.last_seen_player_position = pair.player;
    state.body_yaw_degrees =
        test_yaw_toward(pair.jack, pair.player);
    state.chase_event_emitted = true;
    BackroomsJackRuntime runtime {};
    BackroomsJackUpdateContext context {};
    context.player = test_player(pair.player, pair.jack);
    context.chunk_readiness = ready_neighborhood(
        backrooms_jack_chunk_at(pair.player));
    context.allow_spawn = false;

    std::vector<BackroomsJackEventKind> footsteps {};
    for (auto frame = 0; frame < 45; ++frame) {
        const auto result = update_backrooms_jack(
            state,
            runtime,
            generator,
            context,
            1.0F / 60.0F);
        for (std::size_t index = 0U;
             index < result.event_count;
             ++index) {
            const auto kind = result.events[index].kind;
            if (kind == BackroomsJackEventKind::BootStep ||
                kind == BackroomsJackEventKind::WoodenLegStep) {
                footsteps.push_back(kind);
            }
        }
    }

    REQUIRE(footsteps.size() >= 3U);
    CHECK(footsteps[0] == BackroomsJackEventKind::BootStep);
    for (std::size_t index = 1U;
         index < footsteps.size();
         ++index) {
        CHECK(footsteps[index] != footsteps[index - 1U]);
    }
}

TEST_CASE("la poursuite est stable a trente soixante et cent quarante quatre Hz") {
    const BackroomsGenerator generator {2222};
    const auto pair =
        find_visible_pair(generator, 12.0F, 18.0F);
    REQUIRE(pair.found);

    const auto at_thirty =
        simulate_chase(generator, pair, 30, 0.5F);
    const auto at_sixty =
        simulate_chase(generator, pair, 60, 0.5F);
    const auto at_one_forty_four =
        simulate_chase(generator, pair, 144, 0.5F);

    CHECK(test_horizontal_distance(
              at_thirty.position,
              at_sixty.position) < 0.02F);
    CHECK(test_horizontal_distance(
              at_sixty.position,
              at_one_forty_four.position) < 0.02F);
    CHECK(at_thirty.phase == at_sixty.phase);
    CHECK(at_sixty.phase == at_one_forty_four.phase);
}

TEST_CASE("les previews smoke sont deterministes et completement bornees") {
    for (auto pose_value = 0U;
         pose_value <=
             static_cast<unsigned int>(
                 BackroomsJackSmokePose::Jumpscare);
         ++pose_value) {
        const auto pose =
            static_cast<BackroomsJackSmokePose>(pose_value);
        const auto first =
            make_backrooms_jack_smoke_preview(
                pose,
                0xBADC0DEU);
        const auto repeated =
            make_backrooms_jack_smoke_preview(
                pose,
                0xBADC0DEU);
        CHECK(first.render.position ==
              repeated.render.position);
        CHECK(first.render.body_yaw_degrees ==
              repeated.render.body_yaw_degrees);
        CHECK(first.render.visible);
        CHECK(first.render.hunch_ratio >= 0.0F);
        CHECK(first.render.hunch_ratio <= 1.0F);
        CHECK(first.render.motion_amount >= 0.0F);
        CHECK(first.render.motion_amount <= 1.0F);
        CHECK(first.render.sky_light == doctest::Approx(0.0F));
        CHECK(first.render.block_light == doctest::Approx(0.0F));
        CHECK(first.light_interference.active);
        CHECK(first.light_interference.intensity >= 0.0F);
        CHECK(first.light_interference.intensity <= 1.0F);
    }
    CHECK(
        make_backrooms_jack_smoke_preview(
            BackroomsJackSmokePose::Jumpscare)
            .render.jumpscare);
    CHECK(
        make_backrooms_jack_smoke_preview(
            BackroomsJackSmokePose::Chasing)
            .render.chasing);
    CHECK(
        make_backrooms_jack_smoke_preview(
            BackroomsJackSmokePose::Standing)
            .render.motion_amount <
        make_backrooms_jack_smoke_preview(
            BackroomsJackSmokePose::Chasing)
            .render.motion_amount);
}

}
