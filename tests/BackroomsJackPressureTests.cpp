#include "gameplay/BackroomsJack.h"
#include "render/Renderer.h"
#include "world/World.h"

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
#include <utility>
#include <vector>

namespace valcraft {

namespace {

constexpr float kPressureTestPi = 3.14159265358979323846F;
constexpr float kDirectorStepSeconds = 0.25F;

struct PressureSpawnFixture {
    BackroomsJackPlayerContext player {};
    BackroomsJackNavigationGrid grid {};
    BackroomsJackChunkReadiness readiness {};
    bool found = false;
};

struct PressureLane {
    glm::vec3 start {0.0F};
    glm::vec3 goal {0.0F};
    bool found = false;
};

struct CornerOcclusion {
    glm::vec3 from {0.0F};
    glm::vec3 to {0.0F};
    bool found = false;
};

struct PressureVisiblePair {
    glm::vec3 jack {0.0F};
    glm::vec3 player {0.0F};
    bool found = false;
};

[[nodiscard]] auto pressure_horizontal_distance(
    const glm::vec3& first,
    const glm::vec3& second) noexcept -> float {
    return glm::length(glm::vec2 {
        first.x - second.x,
        first.z - second.z,
    });
}

[[nodiscard]] auto pressure_yaw_toward(
    const glm::vec3& from,
    const glm::vec3& to) noexcept -> float {
    const auto delta = to - from;
    return std::atan2(delta.x, -delta.z) * 180.0F / kPressureTestPi;
}

[[nodiscard]] auto pressure_player(
    const glm::vec3& feet,
    const glm::vec3& look_target,
    float sprint_speed = 7.2F) noexcept
    -> BackroomsJackPlayerContext {
    const auto eye = feet + glm::vec3 {0.0F, 1.62F, 0.0F};
    auto look = look_target - eye;
    if (glm::dot(look, look) <= 0.000001F) {
        look = {0.0F, 0.0F, -1.0F};
    } else {
        look = glm::normalize(look);
    }
    return {feet, eye, look, sprint_speed};
}

[[nodiscard]] auto pressure_cell_position(
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

[[nodiscard]] auto find_pressure_visible_pair(
    const BackroomsGenerator& generator,
    float minimum_distance,
    float maximum_distance) noexcept -> PressureVisiblePair {
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
        for (auto offset_z = -radius; offset_z <= radius; ++offset_z) {
            for (auto offset_x = -radius; offset_x <= radius; ++offset_x) {
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
                const auto jack = pressure_cell_position(
                    generator,
                    jack_x,
                    jack_z);
                for (const auto& direction : directions) {
                    for (auto step = minimum_step; step <= maximum_step;
                         ++step) {
                        const auto player_x =
                            jack_x + direction[0] * step;
                        const auto player_z =
                            jack_z + direction[1] * step;
                        if (!generator.is_walkable(player_x, player_z)) {
                            continue;
                        }
                        const auto player = pressure_cell_position(
                            generator,
                            player_x,
                            player_z);
                        const auto distance = pressure_horizontal_distance(
                            jack,
                            player);
                        if (distance < minimum_distance ||
                            distance > maximum_distance) {
                            continue;
                        }
                        const auto player_context = pressure_player(
                            player,
                            jack + glm::vec3 {0.0F, 4.08F, 0.0F});
                        if (backrooms_jack_has_line_of_sight(
                                generator,
                                jack + glm::vec3 {0.0F, 4.08F, 0.0F},
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

[[nodiscard]] auto stable_pressure_readiness(
    const ChunkCoord& center,
    bool ready = true,
    std::uint64_t revision = 1U) noexcept
    -> BackroomsJackChunkReadiness {
    BackroomsJackChunkReadiness readiness {};
    readiness.center_chunk = center;
    readiness.ready.fill(ready);
    readiness.mesh_revisions.fill(
        ready ? (revision == 0U ? 1U : revision) : 0U);
    return readiness;
}

[[nodiscard]] auto pressure_readiness_index(
    const BackroomsJackChunkReadiness& readiness,
    const ChunkCoord& chunk) noexcept -> std::optional<std::size_t> {
    const auto delta_x = static_cast<std::int64_t>(chunk.x) -
                         static_cast<std::int64_t>(readiness.center_chunk.x);
    const auto delta_z = static_cast<std::int64_t>(chunk.z) -
                         static_cast<std::int64_t>(readiness.center_chunk.z);
    if (delta_x < -kBackroomsJackReadinessChunkRadius ||
        delta_x > kBackroomsJackReadinessChunkRadius ||
        delta_z < -kBackroomsJackReadinessChunkRadius ||
        delta_z > kBackroomsJackReadinessChunkRadius) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(
        (delta_z + kBackroomsJackReadinessChunkRadius) *
            kBackroomsJackReadinessChunkSide +
        delta_x + kBackroomsJackReadinessChunkRadius);
}

[[nodiscard]] auto pressure_chunk_is_stably_ready(
    const BackroomsJackChunkReadiness& readiness,
    const ChunkCoord& chunk) noexcept -> bool {
    const auto index = pressure_readiness_index(readiness, chunk);
    return index.has_value() && readiness.ready[*index] &&
           readiness.mesh_revisions[*index] != 0U;
}

[[nodiscard]] auto pressure_seed(std::uint32_t index) noexcept
    -> std::uint32_t {
    auto value = index + 0x9E3779B9U;
    value ^= value >> 16U;
    value *= 0x7FEB352DU;
    value ^= value >> 15U;
    value *= 0x846CA68BU;
    value ^= value >> 16U;
    return value == 0U ? 1U : value;
}

[[nodiscard]] auto pressure_mode_index(
    BackroomsJackEncounterMode mode) noexcept -> std::size_t {
    switch (mode) {
    case BackroomsJackEncounterMode::CorridorStare:
        return 0U;
    case BackroomsJackEncounterMode::RearStare:
        return 1U;
    case BackroomsJackEncounterMode::HiddenHunt:
        return 2U;
    }
    return 2U;
}

[[nodiscard]] auto find_pressure_spawn_fixture(
    const BackroomsGenerator& generator) -> PressureSpawnFixture {
    const auto spawn = generator.spawn_block();
    const glm::vec3 feet {
        static_cast<float>(spawn.x) + 0.5F,
        static_cast<float>(spawn.y),
        static_cast<float>(spawn.z) + 0.5F,
    };
    const auto center = backrooms_jack_chunk_at(feet);
    const auto grid = build_backrooms_jack_navigation_grid(generator, center);
    const auto readiness = stable_pressure_readiness(center);
    const std::array<glm::vec3, 12> looks {{
        {1.0F, 0.0F, 0.0F},
        {-1.0F, 0.0F, 0.0F},
        {0.0F, 0.0F, 1.0F},
        {0.0F, 0.0F, -1.0F},
        {1.0F, 0.0F, 1.0F},
        {1.0F, 0.0F, -1.0F},
        {-1.0F, 0.0F, 1.0F},
        {-1.0F, 0.0F, -1.0F},
        {2.0F, 0.0F, 1.0F},
        {2.0F, 0.0F, -1.0F},
        {-2.0F, 0.0F, 1.0F},
        {-2.0F, 0.0F, -1.0F},
    }};

    for (const auto& look : looks) {
        const auto player = pressure_player(feet, feet + look * 40.0F);
        std::array<bool, 3> observed_modes {};
        for (auto sample = std::uint32_t {0U}; sample < 192U; ++sample) {
            const auto selection = select_backrooms_jack_spawn(
                generator,
                grid,
                player,
                readiness,
                pressure_seed(sample));
            if (selection.found) {
                observed_modes[pressure_mode_index(selection.encounter_mode)] =
                    true;
            }
            if (std::all_of(
                    observed_modes.begin(),
                    observed_modes.end(),
                    [](bool observed) { return observed; })) {
                return {player, grid, readiness, true};
            }
        }
    }
    return {};
}

[[nodiscard]] auto cached_poolrooms_pressure_fixture()
    -> const PressureSpawnFixture& {
    static const auto fixture = [] {
        const BackroomsGenerator generator {7331, -2};
        return find_pressure_spawn_fixture(generator);
    }();
    return fixture;
}

[[nodiscard]] auto cached_offices_pressure_fixture()
    -> const PressureSpawnFixture& {
    static const auto fixture = [] {
        const BackroomsGenerator generator {7331, 0};
        return find_pressure_spawn_fixture(generator);
    }();
    return fixture;
}

[[nodiscard]] auto pressure_count_event(
    const BackroomsJackUpdateResult& result,
    BackroomsJackEventKind kind) noexcept -> std::size_t {
    auto count = std::size_t {0U};
    for (auto index = std::size_t {0U};
         index < result.event_count;
         ++index) {
        count += result.events[index].kind == kind ? 1U : 0U;
    }
    return count;
}

[[nodiscard]] auto pressure_is_distant_cue(
    BackroomsJackEventKind kind) noexcept -> bool {
    return kind == BackroomsJackEventKind::DistantBootStep ||
           kind == BackroomsJackEventKind::DistantWoodenLegStep;
}

[[nodiscard]] auto find_pressure_lane(
    const BackroomsGenerator& generator,
    bool require_water,
    int required_length = 12) -> PressureLane {
    const auto spawn = generator.spawn_block();
    const glm::vec3 spawn_position {
        static_cast<float>(spawn.x) + 0.5F,
        static_cast<float>(spawn.y),
        static_cast<float>(spawn.z) + 0.5F,
    };
    const auto center = backrooms_jack_chunk_at(spawn_position);
    const auto grid = build_backrooms_jack_navigation_grid(generator, center);
    constexpr std::array<std::array<int, 2>, 4> directions {{
        {{1, 0}},
        {{-1, 0}},
        {{0, 1}},
        {{0, -1}},
    }};

    for (auto local_z = 2; local_z < kBackroomsJackNavigationSide - 2;
         ++local_z) {
        for (auto local_x = 2; local_x < kBackroomsJackNavigationSide - 2;
             ++local_x) {
            for (const auto& direction : directions) {
                const auto end_x = local_x + direction[0] * required_length;
                const auto end_z = local_z + direction[1] * required_length;
                if (end_x < 1 || end_x >= kBackroomsJackNavigationSide - 1 ||
                    end_z < 1 || end_z >= kBackroomsJackNavigationSide - 1) {
                    continue;
                }
                auto lane_is_valid = true;
                for (auto step = 0; step <= required_length; ++step) {
                    const auto index = static_cast<std::size_t>(
                        (local_z + direction[1] * step) *
                            kBackroomsJackNavigationSide +
                        local_x + direction[0] * step);
                    const auto& cell = grid.cells[index];
                    if (!cell.walkable || !cell.standing_allowed ||
                        cell.in_water != require_water) {
                        lane_is_valid = false;
                        break;
                    }
                }
                if (!lane_is_valid) {
                    continue;
                }
                const auto start_x = grid.origin_world_x + local_x;
                const auto start_z = grid.origin_world_z + local_z;
                const auto goal_x = grid.origin_world_x + end_x;
                const auto goal_z = grid.origin_world_z + end_z;
                return {
                    pressure_cell_position(generator, start_x, start_z),
                    pressure_cell_position(generator, goal_x, goal_z),
                    true,
                };
            }
        }
    }
    return {};
}

[[nodiscard]] auto simulate_pressure_motion(
    const BackroomsGenerator& generator,
    const PressureLane& lane,
    BackroomsJackPhase phase,
    float duration_seconds,
    float sprint_speed = 7.2F) -> float {
    auto state = initialize_backrooms_jack(
        0x504F5354U,
        generator.logical_level());
    state.phase = phase;
    state.active = true;
    state.position = lane.start;
    state.last_seen_player_position = lane.goal;
    state.previous_player_position = lane.goal;
    state.has_previous_player_position = true;
    state.body_yaw_degrees = pressure_yaw_toward(lane.start, lane.goal);
    state.chase_event_emitted = phase == BackroomsJackPhase::Chasing;

    BackroomsJackRuntime runtime {};
    BackroomsJackUpdateContext context {};
    context.player = pressure_player(lane.goal, lane.start, sprint_speed);
    if (phase == BackroomsJackPhase::Searching) {
        // Je place volontairement l'oeil de test au-dessus du plafond pour
        // maintenir la recherche sans rebasculer artificiellement en chasse.
        context.player.eye_position =
            lane.goal + glm::vec3 {0.0F, 1000.0F, 0.0F};
        context.player.look_direction =
            glm::normalize(lane.goal - lane.start);
    }
    const auto anchor = (lane.start + lane.goal) * 0.5F;
    context.chunk_readiness = stable_pressure_readiness(
        backrooms_jack_chunk_at(anchor));
    context.allow_spawn = false;

    const auto initial_position = state.position;
    const auto step_count = static_cast<int>(
        std::lround(duration_seconds * 120.0F));
    for (auto step = 0; step < step_count; ++step) {
        static_cast<void>(update_backrooms_jack(
            state,
            runtime,
            generator,
            context,
            1.0F / 120.0F));
    }
    return pressure_horizontal_distance(initial_position, state.position);
}

[[nodiscard]] auto simulate_moving_pressure_chase(
    const BackroomsGenerator& generator,
    const PressureLane& lane,
    float duration_seconds,
    float sprint_speed = 7.2F) -> std::pair<float, float> {
    auto travel_direction = lane.goal - lane.start;
    travel_direction.y = 0.0F;
    travel_direction = glm::normalize(travel_direction);
    auto player_feet = lane.start + travel_direction * 8.0F;

    auto state = initialize_backrooms_jack(0x4D4F5645U, generator.logical_level());
    state.phase = BackroomsJackPhase::Chasing;
    state.active = true;
    state.position = lane.start;
    state.last_seen_player_position = player_feet;
    state.previous_player_position = player_feet;
    state.has_previous_player_position = true;
    state.body_yaw_degrees = pressure_yaw_toward(lane.start, player_feet);
    state.chase_event_emitted = true;

    BackroomsJackRuntime runtime {};
    BackroomsJackUpdateContext context {};
    context.chunk_readiness = stable_pressure_readiness(
        backrooms_jack_chunk_at((lane.start + lane.goal) * 0.5F));
    context.allow_spawn = false;
    context.maximum_visible_distance =
        kBackroomsJackDefaultMaximumVisibleDistance;

    const auto initial_separation = pressure_horizontal_distance(
        state.position,
        player_feet);
    constexpr auto step_seconds = 1.0F / 120.0F;
    const auto step_count = static_cast<int>(
        std::lround(duration_seconds / step_seconds));
    for (auto step = 0; step < step_count; ++step) {
        context.player = pressure_player(
            player_feet,
            state.position + glm::vec3 {0.0F, 3.0F, 0.0F},
            sprint_speed);
        static_cast<void>(update_backrooms_jack(
            state,
            runtime,
            generator,
            context,
            step_seconds));
        player_feet += travel_direction * sprint_speed * step_seconds;
    }
    return {
        initial_separation,
        pressure_horizontal_distance(state.position, player_feet),
    };
}

[[nodiscard]] auto find_pressure_hidden_cell(
    const PressureSpawnFixture& fixture,
    float minimum_distance,
    float maximum_distance) -> std::optional<glm::vec3> {
    const BackroomsJackGridPoint player_point {
        static_cast<int>(std::floor(fixture.player.feet_position.x)),
        static_cast<int>(std::floor(fixture.player.feet_position.z)),
    };
    const auto outside_view_threshold =
        std::cos(70.0F * kPressureTestPi / 180.0F);
    for (auto local_z = 0; local_z < kBackroomsJackNavigationSide;
         ++local_z) {
        for (auto local_x = 0; local_x < kBackroomsJackNavigationSide;
             ++local_x) {
            const auto index = static_cast<std::size_t>(
                local_z * kBackroomsJackNavigationSide + local_x);
            const auto& cell = fixture.grid.cells[index];
            if (!cell.walkable) {
                continue;
            }
            const glm::vec3 candidate {
                static_cast<float>(fixture.grid.origin_world_x + local_x) +
                    0.5F,
                cell.floor_y,
                static_cast<float>(fixture.grid.origin_world_z + local_z) +
                    0.5F,
            };
            const auto distance = pressure_horizontal_distance(
                candidate,
                fixture.player.feet_position);
            if (distance < minimum_distance || distance > maximum_distance) {
                continue;
            }
            auto view_direction =
                candidate + glm::vec3 {0.0F, 3.0F, 0.0F} -
                fixture.player.eye_position;
            if (glm::dot(view_direction, view_direction) <= 0.000001F) {
                continue;
            }
            view_direction = glm::normalize(view_direction);
            if (glm::dot(
                    fixture.player.look_direction,
                    view_direction) >= outside_view_threshold) {
                continue;
            }
            const BackroomsJackGridPoint candidate_point {
                fixture.grid.origin_world_x + local_x,
                fixture.grid.origin_world_z + local_z,
            };
            if (!find_backrooms_jack_path(
                     fixture.grid,
                     player_point,
                     candidate_point)
                     .empty()) {
                return candidate;
            }
        }
    }
    return std::nullopt;
}

[[nodiscard]] auto find_corner_occlusion(
    const BackroomsGenerator& generator) noexcept -> CornerOcclusion {
    const auto spawn = generator.spawn_block();
    for (auto offset_z = -96; offset_z <= 96; ++offset_z) {
        for (auto offset_x = -96; offset_x <= 96; ++offset_x) {
            const auto world_x = spawn.x + offset_x;
            const auto world_z = spawn.z + offset_z;
            for (auto y = spawn.y; y <= spawn.y + 6; ++y) {
                const auto origin = generator.sample_block(world_x, y, world_z);
                const auto diagonal =
                    generator.sample_block(world_x + 1, y, world_z + 1);
                const auto side_x =
                    generator.sample_block(world_x + 1, y, world_z);
                const auto side_z =
                    generator.sample_block(world_x, y, world_z + 1);
                if (is_block_opaque(origin) || is_block_opaque(diagonal) ||
                    (!is_block_opaque(side_x) && !is_block_opaque(side_z))) {
                    continue;
                }
                return {
                    {
                        static_cast<float>(world_x) + 0.5F,
                        static_cast<float>(y) + 0.5F,
                        static_cast<float>(world_z) + 0.5F,
                    },
                    {
                        static_cast<float>(world_x) + 1.5F,
                        static_cast<float>(y) + 0.5F,
                        static_cast<float>(world_z) + 1.5F,
                    },
                    true,
                };
            }
        }
    }
    return {};
}

[[nodiscard]] auto force_pressure_encounter(
    BackroomsJackState& state,
    BackroomsJackRuntime& runtime,
    const BackroomsGenerator& generator,
    const PressureSpawnFixture& fixture,
    std::uint32_t seed,
    bool previous_encounter_chased = false,
    float maximum_visible_distance = 64.0F,
    float visual_deadline_seconds = 0.0F,
    std::optional<BackroomsJackEncounterMode> previous_encounter_mode =
        std::nullopt) -> bool {
    state = initialize_backrooms_jack(seed, generator.logical_level());
    state.spawn_check_seconds = 0.0F;
    runtime = {};
    runtime.navigation = fixture.grid;
    runtime.navigation_readiness = fixture.readiness;
    runtime.navigation_valid = true;
    runtime.navigation_readiness_valid = true;
    runtime.director_initialized = true;
    runtime.distant_cue_seconds = kBackroomsJackCueDeadlineSeconds;
    runtime.visual_deadline_seconds = visual_deadline_seconds;
    runtime.has_previous_encounter_mode =
        previous_encounter_mode.has_value() || previous_encounter_chased;
    runtime.previous_encounter_mode = previous_encounter_mode.value_or(
        BackroomsJackEncounterMode::HiddenHunt);
    runtime.previous_encounter_chased = previous_encounter_chased;

    BackroomsJackUpdateContext context {};
    context.player = fixture.player;
    context.chunk_readiness = fixture.readiness;
    context.maximum_visible_distance = maximum_visible_distance;
    context.allow_spawn = true;
    for (auto step = 0; step < 8 && !state.active; ++step) {
        static_cast<void>(update_backrooms_jack(
            state,
            runtime,
            generator,
            context,
            kDirectorStepSeconds));
    }
    return state.active;
}

struct PressureDirectorSimulation {
    std::array<float, 32> appearance_times {};
    std::array<bool, 32> encounter_chased {};
    std::size_t appearance_count = 0U;
};

[[nodiscard]] auto simulate_ten_minute_pressure_director(
    const BackroomsGenerator& generator,
    const PressureSpawnFixture& fixture,
    std::uint32_t seed) -> PressureDirectorSimulation {
    auto state = initialize_backrooms_jack(seed, generator.logical_level());
    BackroomsJackRuntime runtime {};
    BackroomsJackUpdateContext context {};
    context.player = fixture.player;
    context.chunk_readiness = fixture.readiness;
    context.maximum_visible_distance =
        kBackroomsJackDefaultMaximumVisibleDistance;
    context.allow_spawn = true;

    PressureDirectorSimulation simulation {};
    constexpr auto duration_seconds = 600.0F;
    auto elapsed_seconds = 0.0F;
    while (elapsed_seconds < duration_seconds) {
        const auto was_active = state.active;
        static_cast<void>(update_backrooms_jack(
            state,
            runtime,
            generator,
            context,
            kDirectorStepSeconds));
        elapsed_seconds += kDirectorStepSeconds;
        if (was_active || !state.active) {
            continue;
        }

        REQUIRE(simulation.appearance_count <
                simulation.appearance_times.size());
        const auto encounter_index = simulation.appearance_count;
        simulation.appearance_times[encounter_index] = elapsed_seconds;
        simulation.encounter_chased[encounter_index] =
            runtime.encounter_chases;
        ++simulation.appearance_count;

        // Je conclus immediatement la mise en scene apres l'avoir observee :
        // je peux ainsi exercer dix minutes du vrai directeur sans laisser
        // une poursuite tuer le joueur fictif ni court-circuiter ses delais.
        runtime.previous_encounter_mode = runtime.encounter_mode;
        runtime.has_previous_encounter_mode = true;
        runtime.previous_encounter_chased = runtime.encounter_chases;
        runtime.pending_vanish = true;
        runtime.pending_vanish_seconds = 0.0F;
        runtime.pending_vanish_uses_short_cooldown =
            !runtime.encounter_chases;
    }
    return simulation;
}

} // namespace

TEST_CASE("le contrat temporel public de Jack reste exactement calibre") {
    CHECK(kBackroomsJackFirstCueMinimumSeconds == doctest::Approx(20.0F));
    CHECK(kBackroomsJackFirstCueMaximumSeconds == doctest::Approx(35.0F));
    CHECK(kBackroomsJackFollowingCueMinimumSeconds == doctest::Approx(25.0F));
    CHECK(kBackroomsJackFollowingCueMaximumSeconds == doctest::Approx(45.0F));
    CHECK(kBackroomsJackCueDeadlineSeconds == doctest::Approx(50.0F));
    CHECK(kBackroomsJackInitialSpawnDelayMinimumSeconds ==
          doctest::Approx(40.0F));
    CHECK(kBackroomsJackInitialSpawnDelayMaximumSeconds ==
          doctest::Approx(60.0F));
    CHECK(kBackroomsJackFollowingSpawnDelayMinimumSeconds ==
          doctest::Approx(45.0F));
    CHECK(kBackroomsJackFollowingSpawnDelayMaximumSeconds ==
          doctest::Approx(65.0F));
    CHECK(kBackroomsJackVisualDeadlineSeconds == doctest::Approx(80.0F));
    CHECK(kBackroomsJackMinimumCooldownSeconds == doctest::Approx(70.0F));
    CHECK(kBackroomsJackMaximumCooldownSeconds == doctest::Approx(90.0F));
    CHECK(kBackroomsJackPostChaseVisualDeadlineSeconds ==
          doctest::Approx(120.0F));
    CHECK(kBackroomsJackHiddenHuntMinimumSeconds == doctest::Approx(25.0F));
    CHECK(kBackroomsJackHiddenHuntMaximumSeconds == doctest::Approx(40.0F));
    CHECK(kBackroomsJackRevealMinimumSeconds == doctest::Approx(0.25F));
    CHECK(kBackroomsJackRevealMaximumSeconds == doctest::Approx(0.40F));
    CHECK(kBackroomsJackInterferenceTailMinimumSeconds ==
          doctest::Approx(0.80F));
    CHECK(kBackroomsJackInterferenceTailMaximumSeconds ==
          doctest::Approx(1.80F));
    CHECK(kBackroomsJackMaximumPersistedSpawnDelaySeconds ==
          doctest::Approx(60.0F));
    CHECK(kBackroomsJackMaximumPersistedCooldownSeconds ==
          doctest::Approx(90.0F));
    CHECK(kBackroomsJackReadinessChunkSide == 7);
    CHECK(kBackroomsJackReadinessCellCount == 49U);
}

TEST_CASE("le directeur de Jack respecte ses premieres fenetres et le plafond de silence") {
    for (auto seed = std::uint32_t {1U}; seed <= 128U; ++seed) {
        const auto state = initialize_backrooms_jack(seed, 0);
        CHECK(state.spawn_check_seconds >=
              kBackroomsJackInitialSpawnDelayMinimumSeconds);
        CHECK(state.spawn_check_seconds <=
              kBackroomsJackInitialSpawnDelayMaximumSeconds);
    }

    const BackroomsGenerator generator {7331, -2};
    const auto& fixture = cached_poolrooms_pressure_fixture();
    REQUIRE(fixture.found);
    auto state = initialize_backrooms_jack(0x43554553U, -2);
    BackroomsJackRuntime runtime {};
    BackroomsJackUpdateContext context {};
    context.player = fixture.player;
    context.chunk_readiness = fixture.readiness;
    // Je ferme seulement la fenetre visuelle : les sources sonores disposent
    // toujours de chunks mailles et valides pour exercer leur vrai plafond.
    context.maximum_visible_distance = 1.0F;
    context.allow_spawn = true;

    std::vector<float> cue_times {};
    std::vector<BackroomsJackEventKind> cue_kinds {};
    constexpr auto cue_observation_step = 1.0F / 30.0F;
    auto elapsed = 0.0F;
    for (auto step = 0;
         step < 6000 && (cue_times.size() < 4U || cue_kinds.size() < 2U);
         ++step) {
        const auto timer_before = runtime.distant_cue_seconds;
        const auto initialized_before = runtime.director_initialized;
        const auto result = update_backrooms_jack(
            state,
            runtime,
            generator,
            context,
            cue_observation_step);
        elapsed += cue_observation_step;
        const auto cue_fired =
            initialized_before &&
            runtime.distant_cue_seconds > timer_before + 1.0F;
        if (!cue_fired) {
            continue;
        }
        auto cue_position = result.light_interference.position;
        auto has_position =
            result.light_interference.active &&
            result.light_interference.mode ==
                BackroomsJackLightInterferenceMode::Flicker;
        for (auto index = std::size_t {0U}; index < result.event_count;
             ++index) {
            if (pressure_is_distant_cue(result.events[index].kind)) {
                cue_position = result.events[index].position;
                has_position = true;
                cue_kinds.push_back(result.events[index].kind);
            }
        }
        REQUIRE(has_position);
        CHECK(pressure_chunk_is_stably_ready(
            context.chunk_readiness,
            backrooms_jack_chunk_at(cue_position)));
        CHECK(generator.is_walkable(
            static_cast<int>(std::floor(cue_position.x)),
            static_cast<int>(std::floor(cue_position.z))));
        const auto cue_distance = pressure_horizontal_distance(
            cue_position,
            context.player.feet_position);
        CHECK(cue_distance >=
              kBackroomsJackDistantCueMinimumDistance - 0.01F);
        CHECK(cue_distance <=
              kBackroomsJackDistantCueMaximumDistance + 0.01F);
        cue_times.push_back(elapsed);
    }

    REQUIRE(cue_times.size() >= 4U);
    REQUIRE(cue_kinds.size() >= 2U);
    CHECK(cue_times.front() >=
          kBackroomsJackFirstCueMinimumSeconds - cue_observation_step);
    CHECK(cue_times.front() <=
          kBackroomsJackFirstCueMaximumSeconds + kDirectorStepSeconds);
    for (auto index = std::size_t {1U}; index < 4U; ++index) {
        const auto interval = cue_times[index] - cue_times[index - 1U];
        CHECK(interval >=
              kBackroomsJackFollowingCueMinimumSeconds -
                  cue_observation_step);
        CHECK(interval <=
              kBackroomsJackFollowingCueMaximumSeconds +
                  kDirectorStepSeconds);
        CHECK(interval <=
              kBackroomsJackCueDeadlineSeconds + kDirectorStepSeconds);
    }
    for (auto index = std::size_t {1U}; index < cue_kinds.size(); ++index) {
        CHECK(cue_kinds[index] != cue_kinds[index - 1U]);
    }
}

TEST_CASE("les leurres repartissent pas neon instable et combinaison sans declencher la FSM") {
    const BackroomsGenerator generator {7331, -2};
    const auto& fixture = cached_poolrooms_pressure_fixture();
    REQUIRE(fixture.found);

    constexpr auto sample_count = std::uint32_t {512U};
    auto footstep_only = std::size_t {0U};
    auto flicker_only = std::size_t {0U};
    auto combined = std::size_t {0U};
    const auto outside_view_threshold =
        std::cos(55.0F * kPressureTestPi / 180.0F);

    for (auto sample = std::uint32_t {0U}; sample < sample_count; ++sample) {
        auto state = initialize_backrooms_jack(
            pressure_seed(sample + 50'000U),
            -2);
        state.spawn_check_seconds = 1000.0F;
        BackroomsJackRuntime runtime {};
        runtime.navigation = fixture.grid;
        runtime.navigation_readiness = fixture.readiness;
        runtime.navigation_valid = true;
        runtime.navigation_readiness_valid = true;
        runtime.director_initialized = true;
        runtime.distant_cue_seconds = 0.0F;
        runtime.visual_deadline_seconds =
            kBackroomsJackVisualDeadlineSeconds;

        BackroomsJackUpdateContext context {};
        context.player = fixture.player;
        context.chunk_readiness = fixture.readiness;
        context.maximum_visible_distance = 16.0F;
        context.allow_spawn = true;
        const auto result = update_backrooms_jack(
            state,
            runtime,
            generator,
            context,
            1.0F / 120.0F);

        const auto boot_count =
            pressure_count_event(result, BackroomsJackEventKind::DistantBootStep) +
            pressure_count_event(
                result,
                BackroomsJackEventKind::DistantWoodenLegStep);
        REQUIRE(boot_count <= 1U);
        const auto has_footstep = boot_count == 1U;
        const auto has_flicker =
            result.light_interference.active &&
            result.light_interference.mode ==
                BackroomsJackLightInterferenceMode::Flicker;
        const auto cue_was_emitted = has_footstep || has_flicker;
        REQUIRE(cue_was_emitted);
        CHECK(pressure_count_event(result, BackroomsJackEventKind::Notice) ==
              0U);
        CHECK(pressure_count_event(result, BackroomsJackEventKind::Chase) ==
              0U);
        CHECK(pressure_count_event(result, BackroomsJackEventKind::Screamer) ==
              0U);
        CHECK_FALSE(state.active);

        auto cue_position = result.light_interference.position;
        if (has_footstep) {
            for (auto index = std::size_t {0U}; index < result.event_count;
                 ++index) {
                if (pressure_is_distant_cue(result.events[index].kind)) {
                    cue_position = result.events[index].position;
                    break;
                }
            }
        }
        const auto cue_distance = pressure_horizontal_distance(
            cue_position,
            fixture.player.feet_position);
        CHECK(cue_distance >=
              kBackroomsJackDistantCueMinimumDistance - 0.01F);
        CHECK(cue_distance <=
              kBackroomsJackDistantCueMaximumDistance + 0.01F);
        CHECK(pressure_chunk_is_stably_ready(
            fixture.readiness,
            backrooms_jack_chunk_at(cue_position)));
        CHECK(generator.is_walkable(
            static_cast<int>(std::floor(cue_position.x)),
            static_cast<int>(std::floor(cue_position.z))));
        auto eye_to_cue = cue_position - fixture.player.eye_position;
        if (glm::dot(eye_to_cue, eye_to_cue) > 0.000001F) {
            eye_to_cue = glm::normalize(eye_to_cue);
        }
        CHECK(glm::dot(fixture.player.look_direction, eye_to_cue) <
              outside_view_threshold);

        if (has_footstep && has_flicker) {
            ++combined;
        } else if (has_footstep) {
            ++footstep_only;
        } else {
            ++flicker_only;
        }
    }

    const auto ratio = [](std::size_t count) {
        return static_cast<float>(count) /
               static_cast<float>(sample_count);
    };
    CHECK(ratio(footstep_only) >= 0.37F);
    CHECK(ratio(footstep_only) <= 0.53F);
    CHECK(ratio(flicker_only) >= 0.22F);
    CHECK(ratio(flicker_only) <= 0.38F);
    CHECK(ratio(combined) >= 0.17F);
    CHECK(ratio(combined) <= 0.33F);
}

TEST_CASE("la premiere apparition arrive dans sa fenetre et dans un chunk stable") {
    const BackroomsGenerator generator {7331, -2};
    const auto& fixture = cached_poolrooms_pressure_fixture();
    REQUIRE(fixture.found);

    auto state = initialize_backrooms_jack(0x41505041U, -2);
    const auto scheduled_delay = state.spawn_check_seconds;
    BackroomsJackRuntime runtime {};
    BackroomsJackUpdateContext context {};
    context.player = fixture.player;
    context.chunk_readiness = fixture.readiness;
    context.maximum_visible_distance = 64.0F;
    context.allow_spawn = true;

    auto elapsed = 0.0F;
    for (auto step = 0; step < 248 && !state.active; ++step) {
        static_cast<void>(update_backrooms_jack(
            state,
            runtime,
            generator,
            context,
            kDirectorStepSeconds));
        elapsed += kDirectorStepSeconds;
    }

    REQUIRE(state.active);
    CHECK(elapsed + kDirectorStepSeconds >= scheduled_delay);
    CHECK(elapsed >=
          kBackroomsJackInitialSpawnDelayMinimumSeconds -
              kDirectorStepSeconds);
    CHECK(elapsed <=
          kBackroomsJackInitialSpawnDelayMaximumSeconds + 1.0F);
    CHECK(pressure_horizontal_distance(
              state.position,
              context.player.feet_position) <= 62.001F);
    CHECK(pressure_chunk_is_stably_ready(
        context.chunk_readiness,
        backrooms_jack_chunk_at(state.position)));
}

TEST_CASE("les transitions runtime respectent reveal observation disparition et residu") {
    const BackroomsGenerator generator {7331, -2};
    const auto& fixture = cached_poolrooms_pressure_fixture();
    REQUIRE(fixture.found);

    constexpr auto sample_count = std::uint32_t {96U};
    std::array<std::size_t, 3> mode_counts {};
    constexpr auto step_seconds = 1.0F / 120.0F;
    for (auto sample = std::uint32_t {0U}; sample < sample_count; ++sample) {
        auto state = initialize_backrooms_jack(
            pressure_seed(sample + 80'000U),
            -2);
        state.spawn_check_seconds = 0.0F;
        BackroomsJackRuntime runtime {};
        runtime.navigation = fixture.grid;
        runtime.navigation_readiness = fixture.readiness;
        runtime.navigation_valid = true;
        runtime.navigation_readiness_valid = true;
        runtime.director_initialized = true;
        runtime.distant_cue_seconds = kBackroomsJackCueDeadlineSeconds;
        runtime.visual_deadline_seconds = kBackroomsJackVisualDeadlineSeconds;

        BackroomsJackUpdateContext context {};
        context.player = fixture.player;
        context.chunk_readiness = fixture.readiness;
        context.maximum_visible_distance =
            kBackroomsJackDefaultMaximumVisibleDistance;
        context.allow_spawn = true;

        const auto scheduled = update_backrooms_jack(
            state,
            runtime,
            generator,
            context,
            step_seconds);
        REQUIRE(runtime.pending_reveal);
        REQUIRE(runtime.pending_spawn.found);
        const auto reveal_seconds = runtime.pending_reveal_seconds;
        CHECK(reveal_seconds >= kBackroomsJackRevealMinimumSeconds);
        CHECK(reveal_seconds <= kBackroomsJackRevealMaximumSeconds);
        CHECK(scheduled.light_interference.active);
        CHECK(scheduled.light_interference.mode ==
              BackroomsJackLightInterferenceMode::BlackoutPulse);
        CHECK_FALSE(scheduled.render.visible);

        auto reveal_elapsed = 0.0F;
        for (auto step = 0; step < 64 && !state.active; ++step) {
            const auto result = update_backrooms_jack(
                state,
                runtime,
                generator,
                context,
                step_seconds);
            reveal_elapsed += step_seconds;
            if (!state.active) {
                CHECK(result.light_interference.active);
                CHECK(result.light_interference.mode ==
                      BackroomsJackLightInterferenceMode::BlackoutPulse);
                CHECK_FALSE(result.render.visible);
            }
        }
        REQUIRE(state.active);
        CHECK(reveal_elapsed >= reveal_seconds - step_seconds);
        CHECK(reveal_elapsed <= reveal_seconds + step_seconds * 2.0F);

        const auto mode = runtime.encounter_mode;
        ++mode_counts[pressure_mode_index(mode)];
        switch (mode) {
        case BackroomsJackEncounterMode::CorridorStare:
            CHECK(runtime.encounter_limit_seconds >= 2.0F);
            CHECK(runtime.encounter_limit_seconds <= 4.0F);
            break;
        case BackroomsJackEncounterMode::RearStare:
            CHECK(runtime.encounter_limit_seconds >= 6.0F);
            CHECK(runtime.encounter_limit_seconds <= 10.0F);
            break;
        case BackroomsJackEncounterMode::HiddenHunt:
            CHECK(runtime.encounter_deadline_seconds >=
                  kBackroomsJackHiddenHuntMinimumSeconds);
            CHECK(runtime.encounter_deadline_seconds <=
                  kBackroomsJackHiddenHuntMaximumSeconds);
            continue;
        }

        auto hard_limit_state = state;
        auto hard_limit_runtime = runtime;
        hard_limit_runtime.encounter_chases = false;
        auto hard_limit_context = context;
        hard_limit_context.allow_spawn = false;
        hard_limit_context.player = pressure_player(
            fixture.player.feet_position,
            fixture.player.feet_position +
                (fixture.player.feet_position - state.position));
        auto hard_limit_elapsed = 0.0F;
        while (!hard_limit_runtime.pending_vanish &&
               hard_limit_elapsed < 10.1F) {
            static_cast<void>(update_backrooms_jack(
                hard_limit_state,
                hard_limit_runtime,
                generator,
                hard_limit_context,
                step_seconds));
            hard_limit_elapsed += step_seconds;
        }
        REQUIRE(hard_limit_runtime.pending_vanish);
        const auto expected_hard_limit =
            mode == BackroomsJackEncounterMode::CorridorStare
                ? 6.0F
                : runtime.encounter_limit_seconds;
        CHECK(hard_limit_elapsed >=
              expected_hard_limit - step_seconds * 2.0F);
        CHECK(hard_limit_elapsed <=
              expected_hard_limit + step_seconds * 2.0F);

        runtime.encounter_chases = false;
        context.allow_spawn = false;
        context.player = pressure_player(
            fixture.player.feet_position,
            state.position + glm::vec3 {0.0F, 3.0F, 0.0F});
        auto observation_elapsed = 0.0F;
        while (!runtime.pending_vanish && observation_elapsed < 6.1F) {
            static_cast<void>(update_backrooms_jack(
                state,
                runtime,
                generator,
                context,
                step_seconds));
            observation_elapsed += step_seconds;
        }
        REQUIRE(runtime.pending_vanish);
        if (mode == BackroomsJackEncounterMode::CorridorStare) {
            CHECK(observation_elapsed >=
                  runtime.encounter_limit_seconds - step_seconds * 2.0F);
            CHECK(observation_elapsed <=
                  runtime.encounter_limit_seconds + step_seconds * 2.0F);
        } else {
            CHECK(observation_elapsed >= 0.60F - step_seconds * 2.0F);
            CHECK(observation_elapsed <= 1.20F + step_seconds * 2.0F);
        }

        const auto vanish_seconds = runtime.pending_vanish_seconds;
        CHECK(vanish_seconds >= kBackroomsJackRevealMinimumSeconds);
        CHECK(vanish_seconds <= kBackroomsJackRevealMaximumSeconds);
        auto vanish_elapsed = 0.0F;
        auto forbidden_audio_events = std::size_t {0U};
        while (state.active && vanish_elapsed < 0.5F) {
            const auto result = update_backrooms_jack(
                state,
                runtime,
                generator,
                context,
                step_seconds);
            vanish_elapsed += step_seconds;
            if (state.active) {
                CHECK(result.light_interference.active);
                CHECK(result.light_interference.mode ==
                      BackroomsJackLightInterferenceMode::BlackoutPulse);
            }
            forbidden_audio_events +=
                pressure_count_event(result, BackroomsJackEventKind::Notice) +
                pressure_count_event(result, BackroomsJackEventKind::Chase) +
                pressure_count_event(result, BackroomsJackEventKind::Screamer);
        }
        REQUIRE_FALSE(state.active);
        CHECK(vanish_elapsed >= vanish_seconds - step_seconds);
        CHECK(vanish_elapsed <= vanish_seconds + step_seconds * 2.0F);
        CHECK(forbidden_audio_events == 0U);
        CHECK(runtime.interference_tail_duration >=
              kBackroomsJackInterferenceTailMinimumSeconds);
        CHECK(runtime.interference_tail_duration <=
              kBackroomsJackInterferenceTailMaximumSeconds);
        CHECK(runtime.interference_tail_seconds > 0.0F);
        CHECK(runtime.interference_tail_seconds <=
              runtime.interference_tail_duration);
    }

    CHECK(mode_counts[0] >= 20U);
    CHECK(mode_counts[1] >= 20U);
    CHECK(mode_counts[2] >= 12U);
}

TEST_CASE("le selecteur ideal suit la repartition quarante trente-cinq vingt-cinq") {
    const BackroomsGenerator generator {7331, -2};
    const auto& fixture = cached_poolrooms_pressure_fixture();
    REQUIRE(fixture.found);

    constexpr auto sample_count = std::uint32_t {512U};
    std::array<std::size_t, 3> counts {};
    std::array<bool, 3> geometry_checked {};
    for (auto sample = std::uint32_t {0U}; sample < sample_count; ++sample) {
        const auto selection = select_backrooms_jack_spawn(
            generator,
            fixture.grid,
            fixture.player,
            fixture.readiness,
            pressure_seed(sample + 10'000U));
        REQUIRE(selection.found);
        const auto mode_index = pressure_mode_index(selection.encounter_mode);
        ++counts[mode_index];
        if (!geometry_checked[mode_index]) {
            const auto distance = pressure_horizontal_distance(
                selection.position,
                fixture.player.feet_position);
            const auto perception = evaluate_backrooms_jack_perception(
                generator,
                fixture.player,
                selection.position,
                selection.body_yaw_degrees,
                selection.initial_hunch);
            CHECK(perception.jack_front_dot > 0.94F);
            switch (selection.encounter_mode) {
            case BackroomsJackEncounterMode::CorridorStare:
                CHECK(distance >= 32.0F);
                CHECK(distance <= 52.0F);
                CHECK(perception.player_faces_jack);
                CHECK(perception.player_sees_jack);
                break;
            case BackroomsJackEncounterMode::RearStare:
                CHECK(distance >= 18.0F);
                CHECK(distance <= 30.0F);
                CHECK(perception.line_of_sight);
                CHECK_FALSE(perception.player_faces_jack);
                CHECK_FALSE(perception.player_sees_jack);
                break;
            case BackroomsJackEncounterMode::HiddenHunt:
                CHECK(distance >= 12.0F);
                CHECK(distance <= 24.0F);
                CHECK_FALSE(perception.player_sees_jack);
                break;
            }
            CHECK(distance <=
                  kBackroomsJackDefaultMaximumVisibleDistance);
            geometry_checked[mode_index] = true;
        }
    }
    CHECK(std::all_of(
        geometry_checked.begin(),
        geometry_checked.end(),
        [](bool checked) { return checked; }));

    const auto ratio = [](std::size_t count) {
        return static_cast<float>(count) /
               static_cast<float>(sample_count);
    };
    CHECK(ratio(counts[0]) >= 0.32F);
    CHECK(ratio(counts[0]) <= 0.48F);
    CHECK(ratio(counts[1]) >= 0.27F);
    CHECK(ratio(counts[1]) <= 0.43F);
    CHECK(ratio(counts[2]) >= 0.17F);
    CHECK(ratio(counts[2]) <= 0.33F);
}

TEST_CASE("la distance visible et la revision du mesh filtrent toute apparition") {
    const BackroomsGenerator generator {7331, -2};
    const auto& fixture = cached_poolrooms_pressure_fixture();
    REQUIRE(fixture.found);

    const BackroomsTerminalFogSnapshot fog_snapshot {
        .valid = true,
        .world_seed = 7331,
        .logical_level = -2,
        .range = {
            .start_distance = 12.0F,
            .end_distance = 24.0F,
        },
    };
    const auto safe_visible_distance =
        fog_snapshot.safe_visible_distance(7331, -2, 2.0F, 64.0F);
    REQUIRE(safe_visible_distance == doctest::Approx(22.0F));
    CHECK(fog_snapshot.safe_visible_distance(7331, 0, 2.0F, 64.0F) ==
          doctest::Approx(0.0F));

    auto state = initialize_backrooms_jack(0x53414645U, -2);
    state.spawn_check_seconds = 0.0F;
    BackroomsJackRuntime runtime {};
    BackroomsJackUpdateContext context {};
    context.player = fixture.player;
    context.chunk_readiness = fixture.readiness;
    context.chunk_readiness.mesh_revisions.fill(0U);
    context.maximum_visible_distance = safe_visible_distance;
    context.allow_spawn = true;

    for (auto step = 0; step < 328; ++step) {
        const auto result = update_backrooms_jack(
            state,
            runtime,
            generator,
            context,
            kDirectorStepSeconds);
        CHECK_FALSE(result.render.visible);
        CHECK_FALSE(runtime.pending_spawn.found);
    }
    CHECK_FALSE(state.active);

    context.chunk_readiness.mesh_revisions.fill(7U);
    runtime.visual_deadline_seconds = 0.0F;
    state.spawn_check_seconds = 0.0F;
    for (auto step = 0; step < 16 && !state.active; ++step) {
        static_cast<void>(update_backrooms_jack(
            state,
            runtime,
            generator,
            context,
            kDirectorStepSeconds));
    }

    REQUIRE(state.active);
    CHECK(pressure_horizontal_distance(
              state.position,
              context.player.feet_position) <=
          safe_visible_distance + 0.001F);
    CHECK(pressure_chunk_is_stably_ready(
        context.chunk_readiness,
        backrooms_jack_chunk_at(state.position)));
}

TEST_CASE("une revision de mesh reconstruit la navigation du meme runtime") {
    constexpr auto seed = 7331;
    const BackroomsGenerator generator {seed, 0};
    World world {
        seed,
        0,
        WorldGenerationProfile::Backrooms,
        WorldGenerationVersion::BackroomsV3,
        VisualPipeline::ModernStylized,
        0,
    };
    const auto& fixture = cached_offices_pressure_fixture();
    REQUIRE(fixture.found);

    auto state = initialize_backrooms_jack(seed, 0);
    BackroomsJackRuntime runtime {};
    BackroomsJackUpdateContext context {};
    context.player = fixture.player;
    context.chunk_readiness = fixture.readiness;
    context.allow_spawn = false;
    context.simulation_frozen = true;
    context.spatial_world = &world;

    static_cast<void>(update_backrooms_jack(
        state,
        runtime,
        generator,
        context,
        1.0F / 60.0F));
    REQUIRE(runtime.navigation_valid);
    REQUIRE(runtime.navigation_readiness_valid);

    auto obstacle_point = BackroomsJackGridPoint {};
    auto obstacle_y = 0;
    auto found_cell = false;
    for (auto local_z = 1;
         local_z < kBackroomsJackNavigationSide - 1 && !found_cell;
         ++local_z) {
        for (auto local_x = 1;
             local_x < kBackroomsJackNavigationSide - 1 && !found_cell;
             ++local_x) {
            const auto index = static_cast<std::size_t>(
                local_z * kBackroomsJackNavigationSide + local_x);
            const auto& cell = runtime.navigation.cells[index];
            if (!cell.walkable) {
                continue;
            }
            const BackroomsJackGridPoint point {
                runtime.navigation.origin_world_x + local_x,
                runtime.navigation.origin_world_z + local_z,
            };
            const auto body_y = static_cast<int>(std::floor(cell.floor_y));
            if (is_block_collidable(generator.sample_block(
                    point.x,
                    body_y,
                    point.z)) ||
                is_block_collidable(world.peek_block_or_generated(
                    point.x,
                    body_y,
                    point.z))) {
                continue;
            }
            obstacle_point = point;
            obstacle_y = body_y;
            found_cell = true;
        }
    }
    REQUIRE(found_cell);

    world.set_block(
        obstacle_point.x,
        obstacle_y,
        obstacle_point.z,
        to_block_id(BlockType::Stone));
    const auto obstacle_chunk = backrooms_jack_chunk_at(glm::vec3 {
        static_cast<float>(obstacle_point.x) + 0.5F,
        0.0F,
        static_cast<float>(obstacle_point.z) + 0.5F,
    });
    const auto readiness_index = pressure_readiness_index(
        context.chunk_readiness,
        obstacle_chunk);
    REQUIRE(readiness_index.has_value());
    context.chunk_readiness.mesh_revisions[*readiness_index] += 1U;

    static_cast<void>(update_backrooms_jack(
        state,
        runtime,
        generator,
        context,
        1.0F / 60.0F));
    const auto* rebuilt_cell = backrooms_jack_navigation_cell(
        runtime.navigation,
        obstacle_point.x,
        obstacle_point.z);
    REQUIRE(rebuilt_cell != nullptr);
    CHECK_FALSE(rebuilt_cell->walkable);
    CHECK_FALSE(rebuilt_cell->standing_allowed);
    CHECK(runtime.navigation_readiness.mesh_revisions[*readiness_index] ==
          context.chunk_readiness.mesh_revisions[*readiness_index]);
}

TEST_CASE("la matrice de brouillard borne chaque apparition a sa distance exacte") {
    const BackroomsGenerator generator {7331, -2};
    const auto& fixture = cached_poolrooms_pressure_fixture();
    REQUIRE(fixture.found);

    constexpr std::array<float, 5> visible_distances {{
        0.0F,
        16.0F,
        32.0F,
        52.0F,
        64.0F,
    }};
    for (auto distance_index = std::size_t {0U};
         distance_index < visible_distances.size();
         ++distance_index) {
        const auto expected_distance = visible_distances[distance_index];
        const BackroomsTerminalFogSnapshot snapshot {
            .valid = true,
            .world_seed = 7331,
            .logical_level = -2,
            .range = {
                .start_distance = 0.0F,
                .end_distance = expected_distance + 2.0F,
            },
        };
        const auto safe_distance = snapshot.safe_visible_distance(
            7331,
            -2,
            2.0F,
            64.0F);
        REQUIRE(safe_distance == doctest::Approx(expected_distance));

        auto successful_spawns = std::size_t {0U};
        for (auto sample = std::uint32_t {0U}; sample < 48U; ++sample) {
            BackroomsJackState state {};
            BackroomsJackRuntime runtime {};
            const auto spawned = force_pressure_encounter(
                state,
                runtime,
                generator,
                fixture,
                pressure_seed(
                    sample + 60'000U +
                    static_cast<std::uint32_t>(distance_index) * 100U),
                false,
                safe_distance,
                kBackroomsJackVisualDeadlineSeconds);
            if (!spawned) {
                continue;
            }
            ++successful_spawns;
            CHECK(pressure_horizontal_distance(
                      state.position,
                      fixture.player.feet_position) <=
                  safe_distance + 0.001F);
        }

        if (expected_distance == 0.0F) {
            CHECK(successful_spawns == 0U);
        } else {
            // Je demande au moins un candidat reel a chaque palier positif ;
            // un test qui ne ferait que constater une absence ne protegerait
            // pas la borne utilisee par le code d'activation.
            CHECK(successful_spawns > 0U);
        }
    }

    const auto exact_selection = select_backrooms_jack_spawn(
        generator,
        fixture.grid,
        fixture.player,
        fixture.readiness,
        pressure_seed(67'777U));
    REQUIRE(exact_selection.found);
    const auto exact_distance = pressure_horizontal_distance(
        exact_selection.position,
        fixture.player.feet_position);
    REQUIRE(exact_distance > 0.0F);

    const auto activate_pending_at = [&](float visible_distance) {
        auto state = initialize_backrooms_jack(67'778U, -2);
        state.spawn_check_seconds = 1000.0F;
        BackroomsJackRuntime runtime {};
        runtime.navigation = fixture.grid;
        runtime.navigation_readiness = fixture.readiness;
        runtime.navigation_valid = true;
        runtime.navigation_readiness_valid = true;
        runtime.director_initialized = true;
        runtime.distant_cue_seconds = kBackroomsJackCueDeadlineSeconds;
        runtime.visual_deadline_seconds =
            kBackroomsJackVisualDeadlineSeconds;
        runtime.pending_spawn = exact_selection;
        runtime.pending_reveal = true;
        runtime.pending_reveal_seconds = 0.0F;

        BackroomsJackUpdateContext context {};
        context.player = fixture.player;
        context.chunk_readiness = fixture.readiness;
        context.maximum_visible_distance = visible_distance;
        context.allow_spawn = false;
        static_cast<void>(update_backrooms_jack(
            state,
            runtime,
            generator,
            context,
            1.0F / 60.0F));
        return state.active;
    };

    CHECK(activate_pending_at(exact_distance));
    CHECK_FALSE(activate_pending_at(
        std::nextafter(exact_distance, 0.0F)));
}

TEST_CASE("un rayon de sighting refuse un chunk intermediaire non pret") {
    const BackroomsGenerator generator {7331, -2};
    const auto& fixture = cached_poolrooms_pressure_fixture();
    REQUIRE(fixture.found);

    auto corridor = std::optional<BackroomsJackSpawnSelection> {};
    const auto player_chunk = backrooms_jack_chunk_at(
        fixture.player.feet_position);
    for (auto sample = std::uint32_t {0U}; sample < 1024U; ++sample) {
        const auto selection = select_backrooms_jack_spawn(
            generator,
            fixture.grid,
            fixture.player,
            fixture.readiness,
            pressure_seed(sample + 90'000U));
        if (!selection.found ||
            selection.encounter_mode !=
                BackroomsJackEncounterMode::CorridorStare) {
            continue;
        }
        const auto candidate_chunk = backrooms_jack_chunk_at(
            selection.position);
        if (std::abs(candidate_chunk.x - player_chunk.x) >= 2 ||
            std::abs(candidate_chunk.z - player_chunk.z) >= 2) {
            corridor = selection;
            break;
        }
    }
    REQUIRE(corridor.has_value());
    const auto candidate_chunk = backrooms_jack_chunk_at(
        corridor->position);

    auto endpoint_readiness = stable_pressure_readiness(
        fixture.readiness.center_chunk,
        false);
    const auto player_index = pressure_readiness_index(
        endpoint_readiness,
        player_chunk);
    const auto candidate_index = pressure_readiness_index(
        endpoint_readiness,
        candidate_chunk);
    REQUIRE(player_index.has_value());
    REQUIRE(candidate_index.has_value());
    endpoint_readiness.ready[*player_index] = true;
    endpoint_readiness.mesh_revisions[*player_index] = 1U;
    endpoint_readiness.ready[*candidate_index] = true;
    endpoint_readiness.mesh_revisions[*candidate_index] = 1U;

    const auto activates_with = [&](const BackroomsJackChunkReadiness& readiness) {
        auto state = initialize_backrooms_jack(0x52415953U, -2);
        state.spawn_check_seconds = 1000.0F;
        BackroomsJackRuntime runtime {};
        runtime.navigation = fixture.grid;
        runtime.navigation_readiness = readiness;
        runtime.navigation_valid = true;
        runtime.navigation_readiness_valid = true;
        runtime.director_initialized = true;
        runtime.distant_cue_seconds = kBackroomsJackCueDeadlineSeconds;
        runtime.visual_deadline_seconds = kBackroomsJackVisualDeadlineSeconds;
        runtime.pending_spawn = *corridor;
        runtime.pending_reveal = true;
        runtime.pending_reveal_seconds = 0.0F;

        BackroomsJackUpdateContext context {};
        context.player = fixture.player;
        context.chunk_readiness = readiness;
        context.maximum_visible_distance =
            kBackroomsJackDefaultMaximumVisibleDistance;
        context.allow_spawn = false;
        static_cast<void>(update_backrooms_jack(
            state,
            runtime,
            generator,
            context,
            1.0F / 60.0F));
        return state.active;
    };

    CHECK_FALSE(activates_with(endpoint_readiness));
    CHECK(activates_with(fixture.readiness));
}

TEST_CASE("un brouillard qui se resserre annule un stare sans lancer de poursuite") {
    const BackroomsGenerator generator {7331, -2};
    const auto& fixture = cached_poolrooms_pressure_fixture();
    REQUIRE(fixture.found);

    auto corridor = std::optional<BackroomsJackSpawnSelection> {};
    for (auto sample = std::uint32_t {0U}; sample < 1024U; ++sample) {
        const auto selection = select_backrooms_jack_spawn(
            generator,
            fixture.grid,
            fixture.player,
            fixture.readiness,
            pressure_seed(sample + 91'000U));
        if (selection.found &&
            selection.encounter_mode ==
                BackroomsJackEncounterMode::CorridorStare) {
            corridor = selection;
            break;
        }
    }
    REQUIRE(corridor.has_value());
    const auto distance = pressure_horizontal_distance(
        corridor->position,
        fixture.player.feet_position);
    REQUIRE(distance > 2.0F);

    auto state = initialize_backrooms_jack(0x464F4753U, -2);
    state.phase = BackroomsJackPhase::Watching;
    state.active = true;
    state.position = corridor->position;
    state.body_yaw_degrees = corridor->body_yaw_degrees;
    state.phase_seconds = 5.99F;
    state.notice_event_emitted = true;
    BackroomsJackRuntime runtime {};
    runtime.navigation = fixture.grid;
    runtime.navigation_readiness = fixture.readiness;
    runtime.navigation_valid = true;
    runtime.navigation_readiness_valid = true;
    runtime.encounter_mode = BackroomsJackEncounterMode::CorridorStare;
    runtime.encounter_outcome_directed = true;
    runtime.encounter_chases = true;
    runtime.encounter_limit_seconds = 2.0F;

    BackroomsJackUpdateContext context {};
    context.player = pressure_player(
        fixture.player.feet_position,
        corridor->position + glm::vec3 {0.0F, 3.0F, 0.0F});
    context.chunk_readiness = fixture.readiness;
    context.maximum_visible_distance = distance - 0.25F;
    context.allow_spawn = false;

    const auto result = update_backrooms_jack(
        state,
        runtime,
        generator,
        context,
        1.0F / 60.0F);
    CHECK(state.phase != BackroomsJackPhase::Chasing);
    CHECK(pressure_count_event(result, BackroomsJackEventKind::Chase) == 0U);
    CHECK_FALSE(result.render.visible);
}

TEST_CASE("la chaine de modes conserve exactement quarante trente-cinq vingt-cinq sans repetition") {
    constexpr auto sample_count = std::uint32_t {100'000U};
    std::array<std::size_t, 3> mode_counts {};
    std::array<std::array<std::size_t, 3>, 3> transitions {};
    auto random_state = pressure_seed(0x4D4F4445U);
    auto previous_mode = BackroomsJackEncounterMode::HiddenHunt;
    auto has_previous_mode = false;

    for (auto sample = std::uint32_t {0U}; sample < sample_count; ++sample) {
        const auto order = backrooms_jack_encounter_mode_order(
            random_state,
            has_previous_mode,
            previous_mode,
            false);
        const auto mode = order.modes[0];
        const auto mode_index = pressure_mode_index(mode);
        ++mode_counts[mode_index];
        if (has_previous_mode) {
            CHECK(mode != previous_mode);
            ++transitions[pressure_mode_index(previous_mode)][mode_index];
        }
        random_state = order.next_random_state;
        previous_mode = mode;
        has_previous_mode = true;
    }

    constexpr std::array<float, 3> expected_modes {{0.40F, 0.35F, 0.25F}};
    for (auto index = std::size_t {0U}; index < expected_modes.size();
         ++index) {
        const auto ratio = static_cast<float>(mode_counts[index]) /
                           static_cast<float>(sample_count);
        CAPTURE(index);
        CAPTURE(mode_counts[index]);
        CHECK(ratio == doctest::Approx(expected_modes[index]).epsilon(0.015));
    }

    // Indices : Corridor=0, Rear=1, Hidden=2.
    const auto transition_ratio = [&](std::size_t from, std::size_t to) {
        const auto total = transitions[from][0] + transitions[from][1] +
                           transitions[from][2];
        REQUIRE(total > 0U);
        return static_cast<float>(transitions[from][to]) /
               static_cast<float>(total);
    };
    CHECK(transition_ratio(2U, 0U) == doctest::Approx(0.60F).epsilon(0.02));
    CHECK(transition_ratio(2U, 1U) == doctest::Approx(0.40F).epsilon(0.02));
    CHECK(transition_ratio(0U, 2U) == doctest::Approx(0.375F).epsilon(0.02));
    CHECK(transition_ratio(0U, 1U) == doctest::Approx(0.625F).epsilon(0.02));
    CHECK(transition_ratio(1U, 2U) ==
          doctest::Approx(2.0F / 7.0F).epsilon(0.02));
    CHECK(transition_ratio(1U, 0U) ==
          doctest::Approx(5.0F / 7.0F).epsilon(0.02));
}

TEST_CASE("une sequence continue conserve les poursuites marginales par mode sans doublon") {
    const BackroomsGenerator generator {7331, -2};
    const auto& fixture = cached_poolrooms_pressure_fixture();
    REQUIRE(fixture.found);

    constexpr auto sample_count = std::uint32_t {512U};
    auto pursuit_count = std::size_t {0U};
    std::array<std::size_t, 3> mode_counts {};
    std::array<std::size_t, 3> mode_pursuit_counts {};
    auto previous_mode = std::optional<BackroomsJackEncounterMode> {};
    auto previous_encounter_chased = false;
    auto state = initialize_backrooms_jack(pressure_seed(20'000U), -2);
    BackroomsJackRuntime runtime {};
    runtime.navigation = fixture.grid;
    runtime.navigation_readiness = fixture.readiness;
    runtime.navigation_valid = true;
    runtime.navigation_readiness_valid = true;
    runtime.director_initialized = true;
    runtime.distant_cue_seconds = kBackroomsJackCueDeadlineSeconds;

    BackroomsJackUpdateContext context {};
    context.player = fixture.player;
    context.chunk_readiness = fixture.readiness;
    context.maximum_visible_distance =
        kBackroomsJackDefaultMaximumVisibleDistance;
    context.allow_spawn = true;

    for (auto sample = std::uint32_t {0U}; sample < sample_count; ++sample) {
        // Je conserve le meme RNG et le meme runtime d'une rencontre a
        // l'autre. Je ne compresse que les compteurs d'attente afin que ce test
        // statistique reste rapide sans reconstruire artificiellement Jack.
        state.phase = BackroomsJackPhase::Dormant;
        state.active = false;
        state.spawn_check_seconds = 0.0F;
        state.cooldown_seconds = 0.0F;
        runtime.visual_deadline_seconds = kBackroomsJackVisualDeadlineSeconds;
        runtime.distant_cue_seconds = kBackroomsJackCueDeadlineSeconds;
        runtime.spawn_retry_pending = false;

        for (auto step = 0; step < 8 && !state.active; ++step) {
            static_cast<void>(update_backrooms_jack(
                state,
                runtime,
                generator,
                context,
                kDirectorStepSeconds));
        }
        REQUIRE(state.active);

        if (previous_mode.has_value()) {
            CHECK(runtime.encounter_mode != *previous_mode);
        }
        const auto consecutive_chase =
            previous_encounter_chased && runtime.encounter_chases;
        CHECK_FALSE(consecutive_chase);
        const auto mode_index = pressure_mode_index(runtime.encounter_mode);
        ++mode_counts[mode_index];
        if (runtime.encounter_chases) {
            ++pursuit_count;
            ++mode_pursuit_counts[mode_index];
        }
        previous_mode = runtime.encounter_mode;
        previous_encounter_chased = runtime.encounter_chases;

        // Je conclus l'apparition par le vrai chemin de disparition afin que
        // le verrou anti-poursuite et le RNG restent ceux du directeur.
        runtime.previous_encounter_mode = runtime.encounter_mode;
        runtime.has_previous_encounter_mode = true;
        runtime.previous_encounter_chased = runtime.encounter_chases;
        runtime.pending_vanish = true;
        runtime.pending_vanish_seconds = 0.0F;
        runtime.pending_vanish_uses_short_cooldown =
            !runtime.encounter_chases;
        static_cast<void>(update_backrooms_jack(
            state,
            runtime,
            generator,
            context,
            kDirectorStepSeconds));
        REQUIRE_FALSE(state.active);
    }
    const auto pursuit_ratio =
        static_cast<float>(pursuit_count) /
        static_cast<float>(sample_count);
    CHECK(pursuit_ratio >= 0.25F);
    CHECK(pursuit_ratio <= 0.35F);

    constexpr std::array<float, 3> expected_ratios {{0.25F, 0.35F, 0.30F}};
    for (auto mode_index = std::size_t {0U};
         mode_index < expected_ratios.size();
         ++mode_index) {
        REQUIRE(mode_counts[mode_index] >= 80U);
        const auto conditional_ratio =
            static_cast<float>(mode_pursuit_counts[mode_index]) /
            static_cast<float>(mode_counts[mode_index]);
        CAPTURE(mode_index);
        CAPTURE(mode_counts[mode_index]);
        CAPTURE(mode_pursuit_counts[mode_index]);
        CHECK(conditional_ratio >= expected_ratios[mode_index] - 0.10F);
        CHECK(conditional_ratio <= expected_ratios[mode_index] + 0.10F);
    }
}

TEST_CASE("dix minutes de directeur bornent les apparitions sur chaque famille de niveau") {
    const auto verify_level = [](
                                  const BackroomsGenerator& generator,
                                  const PressureSpawnFixture& fixture,
                                  std::uint32_t seed) {
        REQUIRE(fixture.found);
        const auto simulation = simulate_ten_minute_pressure_director(
            generator,
            fixture,
            seed);
        REQUIRE(simulation.appearance_count >= 7U);

        CHECK(simulation.appearance_times[0] >=
              kBackroomsJackInitialSpawnDelayMinimumSeconds -
                  kDirectorStepSeconds);
        CHECK(simulation.appearance_times[0] <=
              kBackroomsJackInitialSpawnDelayMaximumSeconds +
                  kDirectorStepSeconds);

        for (auto index = std::size_t {1U};
             index < simulation.appearance_count;
             ++index) {
            const auto previous_chased =
                simulation.encounter_chased[index - 1U];
            const auto interval =
                simulation.appearance_times[index] -
                simulation.appearance_times[index - 1U];
            const auto window_minimum =
                previous_chased
                    ? kBackroomsJackMinimumCooldownSeconds
                    : kBackroomsJackFollowingSpawnDelayMinimumSeconds;
            const auto window_maximum =
                previous_chased
                    ? kBackroomsJackMaximumCooldownSeconds
                    : kBackroomsJackFollowingSpawnDelayMaximumSeconds;
            const auto hard_deadline =
                previous_chased
                    ? kBackroomsJackPostChaseVisualDeadlineSeconds
                    : kBackroomsJackVisualDeadlineSeconds;

            CHECK(interval >= window_minimum - kDirectorStepSeconds);
            CHECK(interval <= window_maximum + kDirectorStepSeconds * 2.0F);
            CHECK(interval <= hard_deadline + kDirectorStepSeconds);
            const auto consecutive_chase =
                previous_chased && simulation.encounter_chased[index];
            CHECK_FALSE(consecutive_chase);
        }
    };

    const BackroomsGenerator offices {7331, 0};
    const BackroomsGenerator poolrooms {7331, -2};
    verify_level(
        offices,
        cached_offices_pressure_fixture(),
        0x4F464649U);
    verify_level(
        poolrooms,
        cached_poolrooms_pressure_fixture(),
        0x504F4F4CU);
}

TEST_CASE("HiddenHunt marche a portee normale puis recentre seulement apres huit secondes") {
    const BackroomsGenerator generator {7331, -2};
    const auto& fixture = cached_poolrooms_pressure_fixture();
    REQUIRE(fixture.found);
    const auto normal_cell = find_pressure_hidden_cell(
        fixture,
        12.0F,
        24.0F);
    const auto far_cell = find_pressure_hidden_cell(
        fixture,
        36.01F,
        kBackroomsJackMaximumSpawnDistance);
    REQUIRE(normal_cell.has_value());
    REQUIRE(far_cell.has_value());

    BackroomsJackState state {};
    BackroomsJackRuntime runtime {};
    auto hidden_started = false;
    for (auto sample = std::uint32_t {0U}; sample < 128U; ++sample) {
        if (!force_pressure_encounter(
                state,
                runtime,
                generator,
                fixture,
                pressure_seed(sample + 40'000U),
                false,
                kBackroomsJackDefaultMaximumVisibleDistance,
                kBackroomsJackVisualDeadlineSeconds)) {
            continue;
        }
        if (runtime.encounter_mode ==
            BackroomsJackEncounterMode::HiddenHunt) {
            hidden_started = true;
            break;
        }
    }
    REQUIRE(hidden_started);
    CHECK(runtime.encounter_deadline_seconds >=
          kBackroomsJackHiddenHuntMinimumSeconds - kDirectorStepSeconds);
    CHECK(runtime.encounter_deadline_seconds <=
          kBackroomsJackHiddenHuntMaximumSeconds);

    BackroomsJackUpdateContext context {};
    context.player = fixture.player;
    context.chunk_readiness = fixture.readiness;
    // Je mets la perception presque a zero sans geler la simulation : seul le
    // directeur HiddenHunt pilote alors le chemin et la garde de huit secondes.
    context.maximum_visible_distance = 1.0F;
    context.allow_spawn = false;

    state.phase = BackroomsJackPhase::Wandering;
    state.active = true;
    state.position = *normal_cell;
    state.body_yaw_degrees = pressure_yaw_toward(
        state.position,
        context.player.feet_position) + 180.0F;
    const auto normal_navigation_anchor =
        (state.position + context.player.feet_position) * 0.5F;
    runtime.navigation = build_backrooms_jack_navigation_grid(
        generator,
        backrooms_jack_chunk_at(normal_navigation_anchor));
    runtime.navigation_readiness = fixture.readiness;
    runtime.navigation_valid = true;
    runtime.navigation_readiness_valid = true;
    runtime.path = {};
    runtime.has_path_target = false;
    runtime.repath_seconds = 0.0F;
    runtime.encounter_outcome_directed = true;
    runtime.encounter_mode = BackroomsJackEncounterMode::HiddenHunt;

    static_cast<void>(update_backrooms_jack(
        state,
        runtime,
        generator,
        context,
        1.0F / 120.0F));
    CHECK(runtime.repath_seconds >= 1.0F - 1.0F / 120.0F);
    CHECK(runtime.repath_seconds <= 1.5F);
    for (auto step = 0; step < 6; ++step) {
        const auto previous_position = state.position;
        static_cast<void>(update_backrooms_jack(
            state,
            runtime,
            generator,
            context,
            kDirectorStepSeconds));
        CHECK(pressure_horizontal_distance(previous_position, state.position) <
              1.0F);
        CHECK(state.active);
    }

    state.phase = BackroomsJackPhase::Wandering;
    state.active = true;
    state.position = *far_cell;
    state.body_yaw_degrees = pressure_yaw_toward(
        state.position,
        context.player.feet_position) + 180.0F;
    const auto far_navigation_anchor =
        (state.position + context.player.feet_position) * 0.5F;
    runtime.navigation = build_backrooms_jack_navigation_grid(
        generator,
        backrooms_jack_chunk_at(far_navigation_anchor));
    runtime.navigation_readiness = fixture.readiness;
    runtime.navigation_valid = true;
    runtime.navigation_readiness_valid = true;
    runtime.path = {};
    runtime.has_path_target = false;
    runtime.repath_seconds = 100.0F;
    runtime.movement_blocked = false;
    runtime.has_last_simulated_position = false;
    const auto far_start = state.position;
    auto elapsed = 0.0F;
    for (auto step = 0; step < 31; ++step) {
        static_cast<void>(update_backrooms_jack(
            state,
            runtime,
            generator,
            context,
            kDirectorStepSeconds));
        elapsed += kDirectorStepSeconds;
        CHECK(pressure_horizontal_distance(far_start, state.position) < 1.0F);
    }
    CHECK(elapsed == doctest::Approx(7.75F));

    auto relocated = false;
    for (auto step = 0; step < 18 && !relocated; ++step) {
        static_cast<void>(update_backrooms_jack(
            state,
            runtime,
            generator,
            context,
            kDirectorStepSeconds));
        elapsed += kDirectorStepSeconds;
        relocated = pressure_horizontal_distance(far_start, state.position) >=
                    6.0F;
    }
    REQUIRE(relocated);
    CHECK(elapsed >= 8.0F);
    CHECK(elapsed <= 12.25F);
    const auto relocated_distance = pressure_horizontal_distance(
        state.position,
        context.player.feet_position);
    CHECK(relocated_distance >= 18.0F);
    CHECK(relocated_distance <= 28.0F);

    // Je recree immediatement une grande separation : le premier recentrage
    // doit avoir rearme une garde complete, pas seulement un compteur global.
    state.phase = BackroomsJackPhase::Wandering;
    state.active = true;
    state.position = *far_cell;
    state.body_yaw_degrees = pressure_yaw_toward(
        state.position,
        context.player.feet_position) + 180.0F;
    runtime.navigation = build_backrooms_jack_navigation_grid(
        generator,
        backrooms_jack_chunk_at(
            (state.position + context.player.feet_position) * 0.5F));
    runtime.navigation_readiness = fixture.readiness;
    runtime.navigation_valid = true;
    runtime.navigation_readiness_valid = true;
    runtime.path = {};
    runtime.has_path_target = false;
    runtime.repath_seconds = 100.0F;
    runtime.encounter_deadline_seconds =
        kBackroomsJackHiddenHuntMaximumSeconds;
    runtime.movement_blocked = false;
    runtime.has_last_simulated_position = false;
    const auto second_far_start = state.position;
    for (auto step = 0; step < 31; ++step) {
        static_cast<void>(update_backrooms_jack(
            state,
            runtime,
            generator,
            context,
            kDirectorStepSeconds));
        CHECK(pressure_horizontal_distance(second_far_start, state.position) <
              1.0F);
    }
    auto second_relocation_elapsed = 7.75F;
    auto second_relocated = false;
    for (auto step = 0; step < 18 && !second_relocated; ++step) {
        static_cast<void>(update_backrooms_jack(
            state,
            runtime,
            generator,
            context,
            kDirectorStepSeconds));
        second_relocation_elapsed += kDirectorStepSeconds;
        second_relocated = pressure_horizontal_distance(
            second_far_start,
            state.position) >= 6.0F;
    }
    REQUIRE(second_relocated);
    CHECK(second_relocation_elapsed >= 8.0F);
    CHECK(second_relocation_elapsed <= 12.25F);
}

TEST_CASE("HiddenHunt expire selon son temps mural meme sans etre decouvert") {
    const BackroomsGenerator generator {7331, -2};
    const auto& fixture = cached_poolrooms_pressure_fixture();
    REQUIRE(fixture.found);

    BackroomsJackState state {};
    BackroomsJackRuntime runtime {};
    auto hidden_started = false;
    for (auto sample = std::uint32_t {0U}; sample < 128U; ++sample) {
        if (!force_pressure_encounter(
                state,
                runtime,
                generator,
                fixture,
                pressure_seed(sample + 70'000U),
                false,
                kBackroomsJackDefaultMaximumVisibleDistance,
                kBackroomsJackVisualDeadlineSeconds)) {
            continue;
        }
        if (runtime.encounter_mode ==
            BackroomsJackEncounterMode::HiddenHunt) {
            hidden_started = true;
            break;
        }
    }
    REQUIRE(hidden_started);
    const auto scheduled_timeout = runtime.encounter_deadline_seconds;
    REQUIRE(scheduled_timeout >=
            kBackroomsJackHiddenHuntMinimumSeconds - kDirectorStepSeconds);
    REQUIRE(scheduled_timeout <= kBackroomsJackHiddenHuntMaximumSeconds);
    const auto visual_deadline_before = runtime.visual_deadline_seconds;

    BackroomsJackUpdateContext context {};
    context.player = fixture.player;
    // Je place l'oeil au-dessus du plafond afin que cette verification mesure
    // le timeout mural et non une detection fortuite pendant la trajectoire.
    context.player.eye_position += glm::vec3 {0.0F, 1000.0F, 0.0F};
    context.chunk_readiness = fixture.readiness;
    context.maximum_visible_distance = 1.0F;
    context.allow_spawn = false;

    auto elapsed = 0.0F;
    while (state.active &&
           elapsed <= kBackroomsJackHiddenHuntMaximumSeconds + 1.0F) {
        static_cast<void>(update_backrooms_jack(
            state,
            runtime,
            generator,
            context,
            kDirectorStepSeconds));
        elapsed += kDirectorStepSeconds;
    }

    CHECK_FALSE(state.active);
    CHECK(elapsed <= scheduled_timeout + kDirectorStepSeconds * 2.0F);
    CHECK(elapsed <=
          kBackroomsJackHiddenHuntMaximumSeconds + kDirectorStepSeconds);
    CHECK(runtime.has_previous_encounter_mode);
    CHECK_FALSE(runtime.previous_encounter_chased);
    const auto expected_remaining_deadline = std::max(
        0.0F,
        visual_deadline_before - elapsed);
    CHECK(runtime.visual_deadline_seconds ==
          doctest::Approx(expected_remaining_deadline).epsilon(0.02));

    context.player = fixture.player;
    context.maximum_visible_distance =
        kBackroomsJackDefaultMaximumVisibleDistance;
    context.allow_spawn = true;
    auto next_appearance_elapsed = 0.0F;
    while (!state.active && next_appearance_elapsed < 3.0F) {
        static_cast<void>(update_backrooms_jack(
            state,
            runtime,
            generator,
            context,
            kDirectorStepSeconds));
        next_appearance_elapsed += kDirectorStepSeconds;
    }
    REQUIRE(state.active);
    CHECK(next_appearance_elapsed <= 2.0F);
    CHECK(runtime.encounter_mode != BackroomsJackEncounterMode::HiddenHunt);
}

TEST_CASE("les delais suivants distinguent observation et poursuite") {
    const BackroomsGenerator generator {7331, -2};
    const auto& fixture = cached_poolrooms_pressure_fixture();
    REQUIRE(fixture.found);

    const auto verify_cooldown = [&](bool chased, std::uint32_t seed) {
        BackroomsJackState state {};
        BackroomsJackRuntime runtime {};
        REQUIRE(force_pressure_encounter(
            state,
            runtime,
            generator,
            fixture,
            seed));
        runtime.pending_vanish = true;
        runtime.pending_vanish_seconds = 0.0F;
        runtime.pending_vanish_uses_short_cooldown = !chased;
        runtime.has_previous_encounter_mode = true;
        runtime.previous_encounter_chased = chased;

        BackroomsJackUpdateContext context {};
        context.player = fixture.player;
        context.chunk_readiness = fixture.readiness;
        context.allow_spawn = false;
        static_cast<void>(update_backrooms_jack(
            state,
            runtime,
            generator,
            context,
            kDirectorStepSeconds));

        REQUIRE(state.phase == BackroomsJackPhase::Cooldown);
        CHECK_FALSE(state.active);
        CHECK(runtime.has_previous_encounter_mode);
        CHECK(runtime.previous_encounter_chased == chased);
        CHECK(runtime.distant_cue_seconds >=
              kBackroomsJackFollowingCueMinimumSeconds -
                  kDirectorStepSeconds);
        CHECK(runtime.distant_cue_seconds <=
              kBackroomsJackFollowingCueMaximumSeconds);
        if (chased) {
            CHECK(state.cooldown_seconds >=
                  kBackroomsJackMinimumCooldownSeconds -
                      kDirectorStepSeconds);
            CHECK(state.cooldown_seconds <=
                  kBackroomsJackMaximumCooldownSeconds);
            CHECK(runtime.visual_deadline_seconds >=
                  kBackroomsJackPostChaseVisualDeadlineSeconds -
                      kDirectorStepSeconds);
            CHECK(runtime.visual_deadline_seconds <=
                  kBackroomsJackPostChaseVisualDeadlineSeconds);
        } else {
            CHECK(state.cooldown_seconds >=
                  kBackroomsJackFollowingSpawnDelayMinimumSeconds -
                      kDirectorStepSeconds);
            CHECK(state.cooldown_seconds <=
                  kBackroomsJackFollowingSpawnDelayMaximumSeconds);
            CHECK(runtime.visual_deadline_seconds >=
                  kBackroomsJackVisualDeadlineSeconds -
                      kDirectorStepSeconds);
            CHECK(runtime.visual_deadline_seconds <=
                  kBackroomsJackVisualDeadlineSeconds);
        }
    };

    verify_cooldown(false, 0x4F425345U);
    verify_cooldown(true, 0x43484153U);

    auto stale = initialize_backrooms_jack(7331U, -2);
    stale.phase = BackroomsJackPhase::Cooldown;
    stale.cooldown_seconds = 1000.0F;
    const auto sanitized = sanitize_backrooms_jack_state(stale);
    CHECK(sanitized.cooldown_seconds ==
          doctest::Approx(kBackroomsJackMaximumPersistedCooldownSeconds));
}

TEST_CASE("le demi FOV de Jack coupe precisement autour de quatre-vingt-cinq degres") {
    const BackroomsGenerator generator {4242, 0};
    const auto pair = find_pressure_visible_pair(generator, 6.0F, 12.0F);
    REQUIRE(pair.found);
    const auto player = pressure_player(
        pair.player,
        pair.jack + glm::vec3 {0.0F, 4.08F, 0.0F});
    const auto facing_yaw = pressure_yaw_toward(pair.jack, pair.player);

    const auto just_inside = evaluate_backrooms_jack_perception(
        generator,
        player,
        pair.jack,
        facing_yaw + 84.9F,
        0.0F);
    const auto just_outside = evaluate_backrooms_jack_perception(
        generator,
        player,
        pair.jack,
        facing_yaw + 85.1F,
        0.0F);

    REQUIRE(just_inside.line_of_sight);
    REQUIRE(just_outside.line_of_sight);
    CHECK(just_inside.jack_sees_player);
    CHECK_FALSE(just_outside.jack_sees_player);
}

TEST_CASE("la suspicion atteint trois aux horizons un-neuf trois et quatre-six") {
    const BackroomsGenerator generator {4242, 0};
    struct SuspicionCase {
        float minimum_distance;
        float maximum_distance;
        float expected_seconds;
    };
    constexpr std::array cases {
        SuspicionCase {8.0F, 16.0F, 1.9F},
        SuspicionCase {24.0F, 32.0F, 3.0F},
        SuspicionCase {40.0F, 48.0F, 4.6F},
    };
    constexpr auto step_seconds = 1.0F / 120.0F;

    for (const auto& test_case : cases) {
        const auto pair = find_pressure_visible_pair(
            generator,
            test_case.minimum_distance,
            test_case.maximum_distance);
        CAPTURE(test_case.expected_seconds);
        REQUIRE(pair.found);

        auto state = initialize_backrooms_jack(0x53555350U, 0);
        state.phase = BackroomsJackPhase::Watching;
        state.active = true;
        state.position = pair.jack;
        state.body_yaw_degrees = pressure_yaw_toward(
            pair.jack,
            pair.player);
        state.notice_event_emitted = true;
        BackroomsJackRuntime runtime {};
        BackroomsJackUpdateContext context {};
        context.player = pressure_player(
            pair.player,
            pair.jack + glm::vec3 {0.0F, 4.08F, 0.0F});
        context.chunk_readiness = stable_pressure_readiness(
            backrooms_jack_chunk_at(pair.player));
        context.maximum_visible_distance =
            kBackroomsJackDefaultMaximumVisibleDistance;
        context.allow_spawn = false;

        auto elapsed = 0.0F;
        while (state.phase != BackroomsJackPhase::Chasing &&
               elapsed < test_case.expected_seconds + 0.25F) {
            static_cast<void>(update_backrooms_jack(
                state,
                runtime,
                generator,
                context,
                step_seconds));
            elapsed += step_seconds;
        }
        REQUIRE(state.phase == BackroomsJackPhase::Chasing);
        CHECK(elapsed >= test_case.expected_seconds - step_seconds * 2.0F);
        CHECK(elapsed <= test_case.expected_seconds + step_seconds * 2.0F);
        CHECK(state.suspicion == doctest::Approx(3.0F));
    }
}

TEST_CASE("la vitesse de poursuite depasse le sprint meme dans l eau des Poolrooms") {
    constexpr auto sprint_speed = 7.2F;
    constexpr auto duration = 0.50F;
    const BackroomsGenerator offices {7331, 0};
    const BackroomsGenerator poolrooms {7331, -2};
    const auto dry_lane = find_pressure_lane(offices, false);
    const auto water_lane = find_pressure_lane(poolrooms, true);
    REQUIRE(dry_lane.found);
    REQUIRE(water_lane.found);

    const auto dry_distance = simulate_pressure_motion(
        offices,
        dry_lane,
        BackroomsJackPhase::Chasing,
        duration,
        sprint_speed);
    const auto water_distance = simulate_pressure_motion(
        poolrooms,
        water_lane,
        BackroomsJackPhase::Chasing,
        duration,
        sprint_speed);
    const auto expected_dry = sprint_speed * 1.08F * duration;
    const auto expected_water =
        sprint_speed * 1.08F * kBackroomsJackPoolroomsSpeedMultiplier *
        duration;

    CHECK(dry_distance == doctest::Approx(expected_dry).epsilon(0.04));
    CHECK(water_distance == doctest::Approx(expected_water).epsilon(0.04));
    CHECK(water_distance / duration >= sprint_speed * 1.05F);
    CHECK(water_distance < dry_distance);
}

TEST_CASE("Jack referme progressivement la distance sur un joueur qui sprinte") {
    constexpr auto sprint_speed = 7.2F;
    constexpr auto duration = 0.50F;
    const BackroomsGenerator offices {7331, 0};
    const BackroomsGenerator poolrooms {7331, -2};
    const auto dry_lane = find_pressure_lane(offices, false, 12);
    const auto water_lane = find_pressure_lane(poolrooms, true, 12);
    REQUIRE(dry_lane.found);
    REQUIRE(water_lane.found);

    const auto dry = simulate_moving_pressure_chase(
        offices,
        dry_lane,
        duration,
        sprint_speed);
    const auto water = simulate_moving_pressure_chase(
        poolrooms,
        water_lane,
        duration,
        sprint_speed);

    CHECK(dry.first == doctest::Approx(8.0F));
    CHECK(water.first == doctest::Approx(8.0F));
    CHECK(dry.second < dry.first - 0.15F);
    CHECK(water.second < water.first - 0.10F);
    CHECK(dry.second < water.second);
    CHECK(dry.second > 1.10F);
    CHECK(water.second > 1.10F);
}

TEST_CASE("la recherche conserve exactement quatre-vingt-huit pour cent du sprint") {
    constexpr auto sprint_speed = 7.2F;
    constexpr auto duration = 0.50F;
    const BackroomsGenerator generator {4242, 0};
    const auto lane = find_pressure_lane(generator, false);
    REQUIRE(lane.found);

    const auto distance = simulate_pressure_motion(
        generator,
        lane,
        BackroomsJackPhase::Searching,
        duration,
        sprint_speed);
    CHECK(distance ==
          doctest::Approx(sprint_speed * 0.88F * duration).epsilon(0.04));
}

TEST_CASE("le DDA supercover bloque les deux cellules touchees au coin") {
    const BackroomsGenerator generator {7331, 0};
    const auto occlusion = find_corner_occlusion(generator);
    REQUIRE(occlusion.found);

    CHECK_FALSE(backrooms_jack_has_line_of_sight(
        generator,
        occlusion.from,
        occlusion.to));
    CHECK_FALSE(backrooms_jack_has_line_of_sight(
        generator,
        occlusion.to,
        occlusion.from));
}

TEST_CASE("un changement de niveau purge toute apparition transitoire de Jack") {
    const BackroomsGenerator poolrooms {7331, -2};
    const auto& fixture = cached_poolrooms_pressure_fixture();
    REQUIRE(fixture.found);

    auto state = initialize_backrooms_jack(7331U, 0);
    state.phase = BackroomsJackPhase::Watching;
    state.active = true;
    state.position = fixture.player.feet_position;
    BackroomsJackRuntime runtime {};
    runtime.pending_spawn.found = true;
    runtime.pending_reveal = true;
    runtime.pending_reveal_seconds = 0.5F;
    runtime.pending_vanish = true;
    runtime.pending_vanish_seconds = 0.5F;
    runtime.interference_tail_seconds = 1.0F;
    runtime.interference_tail_duration = 1.0F;
    runtime.director_initialized = true;
    runtime.has_previous_encounter_mode = true;
    runtime.previous_encounter_chased = true;
    runtime.navigation_valid = true;
    runtime.navigation_readiness_valid = true;

    BackroomsJackUpdateContext context {};
    context.player = fixture.player;
    context.chunk_readiness = fixture.readiness;
    context.allow_spawn = false;
    const auto result = update_backrooms_jack(
        state,
        runtime,
        poolrooms,
        context,
        0.0F);

    CHECK(state.logical_level == -2);
    CHECK_FALSE(state.active);
    const auto phase_is_inactive =
        state.phase == BackroomsJackPhase::Dormant ||
        state.phase == BackroomsJackPhase::Cooldown;
    CHECK(phase_is_inactive);
    CHECK(state.spawn_check_seconds >= 12.0F);
    CHECK(state.spawn_check_seconds <= 20.0F);
    CHECK_FALSE(result.render.visible);
    CHECK_FALSE(result.light_interference.active);
    CHECK_FALSE(runtime.pending_spawn.found);
    CHECK_FALSE(runtime.pending_reveal);
    CHECK_FALSE(runtime.pending_vanish);
    CHECK(runtime.interference_tail_seconds == doctest::Approx(0.0F));
    CHECK(runtime.director_initialized);
    CHECK(runtime.has_previous_encounter_mode);
    CHECK(runtime.previous_encounter_chased);
    CHECK_FALSE(runtime.navigation_valid);
    CHECK_FALSE(runtime.navigation_readiness_valid);
}

TEST_CASE("la grace multi niveaux ne se rearme pas deux fois sur le meme etage") {
    constexpr std::array<int, 5> levels {{-2, 0, -7, 3, -2}};
    auto state = initialize_backrooms_jack(0x4C564C53U, 1);
    state.phase = BackroomsJackPhase::Chasing;
    state.active = true;
    BackroomsJackRuntime runtime {};
    runtime.director_initialized = true;
    runtime.has_previous_encounter_mode = true;
    runtime.previous_encounter_mode =
        BackroomsJackEncounterMode::RearStare;
    runtime.previous_encounter_chased = true;
    runtime.visual_deadline_seconds = 75.0F;

    for (const auto logical_level : levels) {
        const BackroomsGenerator generator {7331, logical_level};
        const auto spawn = generator.spawn_block();
        const auto feet = pressure_cell_position(
            generator,
            spawn.x,
            spawn.z);
        BackroomsJackUpdateContext context {};
        context.player = pressure_player(
            feet,
            feet + glm::vec3 {0.0F, 0.0F, -1.0F});
        context.chunk_readiness = stable_pressure_readiness(
            backrooms_jack_chunk_at(feet));
        context.allow_spawn = false;

        static_cast<void>(update_backrooms_jack(
            state,
            runtime,
            generator,
            context,
            0.0F));
        CAPTURE(logical_level);
        REQUIRE(state.logical_level == logical_level);
        REQUIRE_FALSE(state.active);
        CHECK(state.spawn_check_seconds >= 12.0F);
        CHECK(state.spawn_check_seconds <= 20.0F);
        CHECK(runtime.director_initialized);
        CHECK(runtime.has_previous_encounter_mode);
        CHECK(runtime.previous_encounter_mode ==
              BackroomsJackEncounterMode::RearStare);
        CHECK(runtime.previous_encounter_chased);
        CHECK_FALSE(make_backrooms_jack_render_view(
                        state,
                        logical_level + 1)
                        .visible);

        const auto grace_seconds = state.spawn_check_seconds;
        const auto random_state = state.random_state;
        const auto visual_deadline = runtime.visual_deadline_seconds;
        static_cast<void>(update_backrooms_jack(
            state,
            runtime,
            generator,
            context,
            0.0F));
        CHECK(state.spawn_check_seconds == doctest::Approx(grace_seconds));
        CHECK(state.random_state == random_state);
        CHECK(runtime.visual_deadline_seconds ==
              doctest::Approx(visual_deadline));
    }
}

TEST_CASE("la normalisation persistante borne les deux anciens compteurs") {
    auto stale = initialize_backrooms_jack(7331U, -2);
    stale.spawn_check_seconds = 3'600.0F;
    stale.cooldown_seconds = 3'600.0F;
    const auto sanitized = sanitize_backrooms_jack_state(stale);
    CHECK(sanitized.spawn_check_seconds ==
          doctest::Approx(kBackroomsJackMaximumPersistedSpawnDelaySeconds));
    CHECK(sanitized.cooldown_seconds ==
          doctest::Approx(kBackroomsJackMaximumPersistedCooldownSeconds));
    CHECK(sanitized.logical_level == -2);
}

TEST_CASE("la persistance distingue traque invisible observation et poursuite") {
    auto state = initialize_backrooms_jack(7331U, -2);
    state.phase = BackroomsJackPhase::Wandering;
    state.active = true;
    BackroomsJackRuntime runtime {};
    runtime.encounter_mode = BackroomsJackEncounterMode::HiddenHunt;
    runtime.encounter_outcome_directed = true;
    runtime.encounter_was_seen = false;

    const auto hidden = prepare_backrooms_jack_for_persistence(
        state,
        runtime);
    CHECK(hidden.phase == BackroomsJackPhase::Dormant);
    CHECK_FALSE(hidden.active);
    CHECK(hidden.spawn_check_seconds ==
          doctest::Approx(kBackroomsJackSpawnRetryMinimumSeconds));
    CHECK(hidden.cooldown_seconds == doctest::Approx(0.0F));
    CHECK_FALSE(hidden.chase_event_emitted);

    state.phase = BackroomsJackPhase::Watching;
    state.active = true;
    runtime.encounter_mode = BackroomsJackEncounterMode::CorridorStare;
    runtime.encounter_outcome_directed = true;
    runtime.encounter_was_seen = true;
    runtime.encounter_chases = true;
    const auto observation = prepare_backrooms_jack_for_persistence(
        state,
        runtime);
    CHECK(observation.phase == BackroomsJackPhase::Cooldown);
    CHECK_FALSE(observation.active);
    CHECK(observation.cooldown_seconds == doctest::Approx(
              kBackroomsJackPsychologicalCooldownMinimumSeconds));
    CHECK_FALSE(observation.chase_event_emitted);

    state.phase = BackroomsJackPhase::Chasing;
    state.active = true;
    state.chase_event_emitted = true;
    const auto chase = prepare_backrooms_jack_for_persistence(
        state,
        runtime);
    CHECK(chase.phase == BackroomsJackPhase::Cooldown);
    CHECK_FALSE(chase.active);
    CHECK(chase.cooldown_seconds ==
          doctest::Approx(kBackroomsJackMinimumCooldownSeconds));
    CHECK(chase.chase_event_emitted);
}

TEST_CASE("la reprise restaure le verrou anti poursuite depuis BJCK v1") {
    const BackroomsGenerator generator {7331, -2};
    const auto spawn = generator.spawn_block();
    const auto feet = pressure_cell_position(generator, spawn.x, spawn.z);
    auto state = initialize_backrooms_jack(7331U, -2);
    state.phase = BackroomsJackPhase::Cooldown;
    state.active = false;
    state.motion_amount = 0.0F;
    state.chase_event_emitted = true;
    state.cooldown_seconds = 70.0F;
    BackroomsJackRuntime runtime {};
    BackroomsJackUpdateContext context {};
    context.player = pressure_player(
        feet,
        feet + glm::vec3 {0.0F, 0.0F, -1.0F});
    context.chunk_readiness = stable_pressure_readiness(
        backrooms_jack_chunk_at(feet));
    context.allow_spawn = false;

    static_cast<void>(update_backrooms_jack(
        state,
        runtime,
        generator,
        context,
        0.0F));
    CHECK(runtime.has_previous_encounter_mode);
    CHECK(runtime.previous_encounter_chased);
}

} // namespace valcraft
