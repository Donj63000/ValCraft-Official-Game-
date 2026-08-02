#include "gameplay/BackroomsJack.h"

#include <doctest/doctest.h>

#include <glm/geometric.hpp>

#include <cmath>
#include <cstdint>
#include <optional>
#include <utility>

namespace valcraft {

namespace {

constexpr auto kAuditPi = 3.14159265358979323846F;

struct AuditJackPair {
    glm::vec3 player {0.0F};
    glm::vec3 jack {0.0F};
    ChunkCoord navigation_center {};
    BackroomsJackNavigationGrid navigation {};
    bool found = false;
};

[[nodiscard]] auto audit_cell_position(
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

[[nodiscard]] auto audit_pair(
    const BackroomsGenerator& generator) -> AuditJackPair {
    const auto spawn = generator.spawn_block();
    const auto player = audit_cell_position(generator, spawn.x, spawn.z);
    for (auto delta_z = -28; delta_z <= 28; ++delta_z) {
        for (auto delta_x = -28; delta_x <= 28; ++delta_x) {
            const auto distance = std::sqrt(
                static_cast<float>(
                    delta_x * delta_x + delta_z * delta_z));
            if (distance < 18.0F || distance > 24.0F) {
                continue;
            }
            const auto jack_x = spawn.x + delta_x;
            const auto jack_z = spawn.z + delta_z;
            if (!generator.is_walkable(jack_x, jack_z)) {
                continue;
            }
            const auto jack = audit_cell_position(
                generator,
                jack_x,
                jack_z);
            const glm::vec3 midpoint {
                (player.x + jack.x) * 0.5F,
                player.y,
                (player.z + jack.z) * 0.5F,
            };
            const auto center = backrooms_jack_chunk_at(midpoint);
            auto navigation = build_backrooms_jack_navigation_grid(
                generator,
                center);
            const auto* jack_cell = backrooms_jack_navigation_cell(
                navigation,
                jack_x,
                jack_z);
            if (jack_cell == nullptr || !jack_cell->walkable) {
                continue;
            }
            return {
                .player = player,
                .jack = jack,
                .navigation_center = center,
                .navigation = std::move(navigation),
                .found = true,
            };
        }
    }
    return {};
}

[[nodiscard]] auto audit_readiness(
    const ChunkCoord& center,
    std::uint64_t revision = 1U) noexcept
    -> BackroomsJackChunkReadiness {
    BackroomsJackChunkReadiness readiness {};
    readiness.center_chunk = center;
    readiness.ready.fill(true);
    readiness.mesh_revisions.fill(revision);
    return readiness;
}

[[nodiscard]] auto audit_readiness_index(
    const BackroomsJackChunkReadiness& readiness,
    const ChunkCoord& chunk) noexcept -> std::optional<std::size_t> {
    const auto delta_x = chunk.x - readiness.center_chunk.x;
    const auto delta_z = chunk.z - readiness.center_chunk.z;
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

[[nodiscard]] auto audit_context(
    const AuditJackPair& pair,
    const BackroomsJackChunkReadiness& readiness)
    -> BackroomsJackUpdateContext {
    const auto eye = pair.player + glm::vec3 {0.0F, 1.62F, 0.0F};
    return {
        .player = {
            .feet_position = pair.player,
            .eye_position = eye,
            .look_direction = glm::normalize(pair.player - pair.jack),
            .maximum_sprint_speed = 7.2F,
        },
        .chunk_readiness = readiness,
        .allow_spawn = true,
        .maximum_visible_distance = 64.0F,
    };
}

void prepare_unseen_hidden_hunt(
    BackroomsJackState& state,
    BackroomsJackRuntime& runtime,
    const BackroomsGenerator& generator,
    const AuditJackPair& pair,
    const BackroomsJackChunkReadiness& readiness) {
    state = initialize_backrooms_jack(0x41554449U, generator.logical_level());
    state.phase = BackroomsJackPhase::Wandering;
    state.position = pair.jack;
    state.active = true;
    const auto toward_player = pair.player - pair.jack;
    state.body_yaw_degrees =
        std::atan2(toward_player.x, -toward_player.z) *
            180.0F / kAuditPi +
        180.0F;

    runtime = {};
    runtime.navigation = pair.navigation;
    runtime.navigation_readiness = readiness;
    runtime.navigation_valid = true;
    runtime.navigation_readiness_valid = true;
    runtime.encounter_mode = BackroomsJackEncounterMode::HiddenHunt;
    runtime.encounter_outcome_directed = true;
    runtime.encounter_was_seen = false;
    runtime.encounter_deadline_seconds = 30.0F;
    runtime.visual_deadline_seconds = 37.0F;
    runtime.director_initialized = true;
}

} // namespace

TEST_CASE("Jack ignore les revisions du seul anneau de sighting") {
    const BackroomsGenerator generator {7331, -2};
    const auto spawn = generator.spawn_block();
    const auto player = audit_cell_position(generator, spawn.x, spawn.z);
    auto readiness = audit_readiness(backrooms_jack_chunk_at(player));

    auto state = initialize_backrooms_jack(0x4E415631U, -2);
    BackroomsJackRuntime runtime {};
    BackroomsJackUpdateContext context {};
    context.player.feet_position = player;
    context.player.eye_position =
        player + glm::vec3 {0.0F, 1.62F, 0.0F};
    context.chunk_readiness = readiness;
    context.allow_spawn = false;
    context.simulation_frozen = true;
    static_cast<void>(update_backrooms_jack(
        state,
        runtime,
        generator,
        context,
        0.0F));
    REQUIRE(runtime.navigation_valid);

    runtime.navigation.cells[0].clearance = 12'345.0F;
    const ChunkCoord outer_chunk {
        readiness.center_chunk.x + kBackroomsJackReadinessChunkRadius,
        readiness.center_chunk.z + kBackroomsJackReadinessChunkRadius,
    };
    const auto outer_index = audit_readiness_index(readiness, outer_chunk);
    REQUIRE(outer_index.has_value());
    context.chunk_readiness.mesh_revisions[*outer_index] = 2U;
    static_cast<void>(update_backrooms_jack(
        state,
        runtime,
        generator,
        context,
        0.0F));
    CHECK(runtime.navigation.cells[0].clearance ==
          doctest::Approx(12'345.0F));

    const auto center_index = audit_readiness_index(
        readiness,
        readiness.center_chunk);
    REQUIRE(center_index.has_value());
    context.chunk_readiness.mesh_revisions[*center_index] = 3U;
    static_cast<void>(update_backrooms_jack(
        state,
        runtime,
        generator,
        context,
        0.0F));
    CHECK(runtime.navigation.cells[0].clearance !=
          doctest::Approx(12'345.0F));
}

TEST_CASE("HiddenHunt invisible recycle si son chunk cesse d etre pret") {
    const BackroomsGenerator generator {7331, -2};
    const auto pair = audit_pair(generator);
    REQUIRE(pair.found);
    auto readiness = audit_readiness(
        backrooms_jack_chunk_at(pair.player));
    const auto jack_index = audit_readiness_index(
        readiness,
        backrooms_jack_chunk_at(pair.jack));
    REQUIRE(jack_index.has_value());
    readiness.ready[*jack_index] = false;
    readiness.mesh_revisions[*jack_index] = 0U;

    BackroomsJackState state {};
    BackroomsJackRuntime runtime {};
    prepare_unseen_hidden_hunt(
        state,
        runtime,
        generator,
        pair,
        readiness);
    const auto preserved_deadline = runtime.visual_deadline_seconds;
    auto context = audit_context(pair, readiness);
    context.simulation_frozen = true;
    static_cast<void>(update_backrooms_jack(
        state,
        runtime,
        generator,
        context,
        0.0F));

    CHECK(state.phase == BackroomsJackPhase::Dormant);
    CHECK_FALSE(state.active);
    CHECK(state.cooldown_seconds == doctest::Approx(0.0F));
    CHECK(state.spawn_check_seconds ==
          doctest::Approx(kBackroomsJackSpawnRetryMinimumSeconds));
    CHECK(runtime.visual_deadline_seconds ==
          doctest::Approx(preserved_deadline));
    CHECK(runtime.spawn_retry_pending);
    CHECK(runtime.has_previous_encounter_mode);
    CHECK_FALSE(runtime.previous_encounter_chased);
}

TEST_CASE("un chunk perdu ne donne le repos long qu a une poursuite reelle") {
    const BackroomsGenerator generator {7331, -2};
    const auto pair = audit_pair(generator);
    REQUIRE(pair.found);
    auto readiness = audit_readiness(
        backrooms_jack_chunk_at(pair.player));
    const auto jack_index = audit_readiness_index(
        readiness,
        backrooms_jack_chunk_at(pair.jack));
    REQUIRE(jack_index.has_value());
    readiness.ready[*jack_index] = false;
    readiness.mesh_revisions[*jack_index] = 0U;

    const auto run_case = [&](bool chasing) {
        BackroomsJackState state {};
        BackroomsJackRuntime runtime {};
        prepare_unseen_hidden_hunt(
            state,
            runtime,
            generator,
            pair,
            readiness);
        state.phase = chasing
                          ? BackroomsJackPhase::Chasing
                          : BackroomsJackPhase::Watching;
        state.chase_event_emitted = chasing;
        runtime.encounter_mode =
            BackroomsJackEncounterMode::CorridorStare;
        runtime.encounter_was_seen = true;
        runtime.previous_encounter_chased = !chasing;
        runtime.has_previous_encounter_mode = true;
        auto context = audit_context(pair, readiness);
        context.simulation_frozen = true;
        static_cast<void>(update_backrooms_jack(
            state,
            runtime,
            generator,
            context,
            0.0F));
        CHECK(state.phase == BackroomsJackPhase::Cooldown);
        if (chasing) {
            CHECK(state.cooldown_seconds >=
                  kBackroomsJackMinimumCooldownSeconds);
            CHECK(state.cooldown_seconds <=
                  kBackroomsJackMaximumCooldownSeconds);
            CHECK(runtime.previous_encounter_chased);
            CHECK(runtime.visual_deadline_seconds == doctest::Approx(
                      kBackroomsJackPostChaseVisualDeadlineSeconds));
        } else {
            CHECK(state.cooldown_seconds >=
                  kBackroomsJackPsychologicalCooldownMinimumSeconds);
            CHECK(state.cooldown_seconds <=
                  kBackroomsJackPsychologicalCooldownMaximumSeconds);
            CHECK_FALSE(runtime.previous_encounter_chased);
            CHECK(runtime.visual_deadline_seconds == doctest::Approx(
                      kBackroomsJackVisualDeadlineSeconds));
        }
    };

    run_case(false);
    run_case(true);
}

TEST_CASE("une brume nulle conserve le verrou de poursuite mais recycle la traque invisible") {
    const BackroomsGenerator generator {7331, -2};
    const auto pair = audit_pair(generator);
    REQUIRE(pair.found);
    const auto readiness = audit_readiness(
        backrooms_jack_chunk_at(pair.player));

    auto context = audit_context(pair, readiness);
    context.maximum_visible_distance = 0.0F;
    context.simulation_frozen = true;
    const auto run_case = [&](bool chasing) {
        BackroomsJackState state {};
        BackroomsJackRuntime runtime {};
        prepare_unseen_hidden_hunt(
            state,
            runtime,
            generator,
            pair,
            readiness);
        const auto hidden_deadline = runtime.visual_deadline_seconds;
        if (chasing) {
            state.phase = BackroomsJackPhase::Chasing;
            state.chase_event_emitted = true;
        }
        static_cast<void>(update_backrooms_jack(
            state,
            runtime,
            generator,
            context,
            0.0F));
        if (chasing) {
            CHECK(state.phase == BackroomsJackPhase::Cooldown);
            CHECK(state.cooldown_seconds >=
                  kBackroomsJackMinimumCooldownSeconds);
            CHECK(state.cooldown_seconds <=
                  kBackroomsJackMaximumCooldownSeconds);
            CHECK(runtime.previous_encounter_chased);
            CHECK(runtime.visual_deadline_seconds == doctest::Approx(
                      kBackroomsJackPostChaseVisualDeadlineSeconds));
        } else {
            CHECK(state.phase == BackroomsJackPhase::Dormant);
            CHECK(state.spawn_check_seconds == doctest::Approx(
                      kBackroomsJackSpawnRetryMinimumSeconds));
            CHECK(state.cooldown_seconds == doctest::Approx(0.0F));
            CHECK(runtime.visual_deadline_seconds ==
                  doctest::Approx(hidden_deadline));
        }
    };

    run_case(false);
    run_case(true);
}

TEST_CASE("HiddenHunt invisible recycle aussi apres le watchdog de blocage") {
    const BackroomsGenerator generator {7331, -2};
    const auto pair = audit_pair(generator);
    REQUIRE(pair.found);
    const auto readiness = audit_readiness(
        backrooms_jack_chunk_at(pair.player));

    BackroomsJackState state {};
    BackroomsJackRuntime runtime {};
    prepare_unseen_hidden_hunt(
        state,
        runtime,
        generator,
        pair,
        readiness);
    runtime.repath_seconds = 10.0F;
    runtime.stuck_seconds = 3.499F;
    runtime.movement_blocked = true;
    runtime.last_simulated_position = state.position;
    runtime.has_last_simulated_position = true;
    const auto initial_deadline = runtime.visual_deadline_seconds;
    auto context = audit_context(pair, readiness);
    static_cast<void>(update_backrooms_jack(
        state,
        runtime,
        generator,
        context,
        1.0F / 120.0F));

    CHECK(state.phase == BackroomsJackPhase::Dormant);
    CHECK_FALSE(state.active);
    CHECK(state.cooldown_seconds == doctest::Approx(0.0F));
    CHECK(runtime.visual_deadline_seconds < initial_deadline);
    CHECK(runtime.visual_deadline_seconds > initial_deadline - 0.02F);
    CHECK(runtime.spawn_retry_pending);
    CHECK(runtime.has_previous_encounter_mode);
    CHECK_FALSE(runtime.previous_encounter_chased);
}

} // namespace valcraft
