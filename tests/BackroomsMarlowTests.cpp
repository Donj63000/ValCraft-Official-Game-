#include "gameplay/BackroomsMarlow.h"
#include "world/BackroomsSpatialStack.h"
#include "world/World.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>

namespace valcraft {

namespace {

[[nodiscard]] auto full_readiness(ChunkCoord center) noexcept
    -> BackroomsMarlowChunkReadiness {
    BackroomsMarlowChunkReadiness readiness {};
    readiness.center_chunk = center;
    readiness.ready.fill(true);
    readiness.mesh_revisions.fill(1U);
    return readiness;
}

[[nodiscard]] auto grid_index(
    const BackroomsMarlowNavigationGrid& grid,
    int world_x,
    int world_z) -> std::size_t {
    const auto local_x = world_x - grid.origin_world_x;
    const auto local_z = world_z - grid.origin_world_z;
    REQUIRE(local_x >= 0);
    REQUIRE(local_z >= 0);
    REQUIRE(local_x < kBackroomsMarlowNavigationSide);
    REQUIRE(local_z < kBackroomsMarlowNavigationSide);
    return static_cast<std::size_t>(
        local_z * kBackroomsMarlowNavigationSide + local_x);
}

void open_cell(
    BackroomsMarlowNavigationGrid& grid,
    int world_x,
    int world_z,
    bool water = false,
    bool dark = false) {
    grid.cells[grid_index(grid, world_x, world_z)] = {
        40.0F,
        water ? 41.0F : 40.0F,
        water ? 1.0F : 0.0F,
        7.0F,
        true,
        water,
        false,
        false,
        dark,
    };
}

[[nodiscard]] auto handmade_grid() -> BackroomsMarlowNavigationGrid {
    BackroomsMarlowNavigationGrid grid {};
    grid.center_chunk = {1, 1};
    grid.logical_level = -2;
    grid.origin_world_x =
        (grid.center_chunk.x - kBackroomsMarlowNavigationChunkRadius) *
        kChunkSizeX;
    grid.origin_world_z =
        (grid.center_chunk.z - kBackroomsMarlowNavigationChunkRadius) *
        kChunkSizeZ;
    return grid;
}

[[nodiscard]] auto marlow_player(
    float x,
    float z,
    glm::vec3 look = {0.0F, 0.0F, 1.0F}) noexcept
    -> BackroomsMarlowPlayerContext {
    return {
        .feet_position = {x, 40.001F, z},
        .eye_position = {x, 41.62F, z},
        .look_direction = look,
    };
}

[[nodiscard]] auto readiness_index(
    const BackroomsMarlowChunkReadiness& readiness,
    ChunkCoord chunk) -> std::size_t {
    const auto delta_x = chunk.x - readiness.center_chunk.x;
    const auto delta_z = chunk.z - readiness.center_chunk.z;
    REQUIRE(delta_x >= -kBackroomsMarlowReadinessChunkRadius);
    REQUIRE(delta_x <= kBackroomsMarlowReadinessChunkRadius);
    REQUIRE(delta_z >= -kBackroomsMarlowReadinessChunkRadius);
    REQUIRE(delta_z <= kBackroomsMarlowReadinessChunkRadius);
    return static_cast<std::size_t>(
        (delta_z + kBackroomsMarlowReadinessChunkRadius) *
            kBackroomsMarlowReadinessChunkSide +
        delta_x + kBackroomsMarlowReadinessChunkRadius);
}

} // namespace

TEST_CASE("Marlow initialise et assainit uniquement son etat durable") {
    const auto first = initialize_backrooms_marlow(0x4D41524CU, -2);
    const auto repeated = initialize_backrooms_marlow(0x4D41524CU, -2);
    CHECK(first.random_state == repeated.random_state);
    CHECK(first.cue_seconds == doctest::Approx(repeated.cue_seconds));
    CHECK(first.cue_seconds >= 8.0F);
    CHECK(first.cue_seconds <= 14.0F);
    CHECK(first.manifestation_seconds >= 25.0F);
    CHECK(first.manifestation_seconds <= 40.0F);
    CHECK(first.logical_level == -2);
    CHECK(first.initialized);

    auto corrupt = first;
    corrupt.pressure = std::numeric_limits<float>::quiet_NaN();
    corrupt.cue_seconds = std::numeric_limits<float>::infinity();
    corrupt.manifestation_seconds = -50.0F;
    corrupt.cooldown_seconds = 900.0F;
    corrupt.random_state = 0U;
    corrupt.next_event_sequence = 0U;
    corrupt.last_mode = static_cast<BackroomsMarlowEncounterMode>(255U);
    corrupt.has_last_mode = true;
    const auto clean = sanitize_backrooms_marlow_state(corrupt);
    CHECK(clean.pressure == 0.0F);
    CHECK(clean.cue_seconds >= 0.0F);
    CHECK(clean.cue_seconds <= 60.0F);
    CHECK(clean.manifestation_seconds == 0.0F);
    CHECK(clean.cooldown_seconds == 24.0F);
    CHECK(clean.random_state != 0U);
    CHECK(clean.next_event_sequence == 1U);
    CHECK_FALSE(clean.has_last_mode);
}

TEST_CASE("La pression de Marlow suit le bruit l eau et la Maglite") {
    auto player = marlow_player(30.5F, 30.5F);
    player.sprinting = true;
    auto result = evaluate_backrooms_marlow_pressure(
        0.0F,
        0.0F,
        player,
        false,
        0.25F);
    CHECK(result.pressure == doctest::Approx(0.03F));

    player.in_water = true;
    player.travelled_horizontal_distance = 0.5F;
    result = evaluate_backrooms_marlow_pressure(
        0.0F,
        0.0F,
        player,
        false,
        0.25F);
    CHECK(result.pressure == doctest::Approx(0.095F));

    player = marlow_player(30.5F, 30.5F);
    player.entered_water = true;
    player.jumped = true;
    player.landed_in_water = true;
    result = evaluate_backrooms_marlow_pressure(
        0.0F,
        0.0F,
        player,
        false,
        0.25F);
    CHECK(result.pressure == doctest::Approx(0.60F));

    player = marlow_player(30.5F, 30.5F);
    player.flashlight_on_water = true;
    result = evaluate_backrooms_marlow_pressure(
        0.0F,
        0.0F,
        player,
        true,
        0.25F);
    CHECK(result.pressure == doctest::Approx(0.12F));

    player = marlow_player(30.5F, 30.5F);
    result = evaluate_backrooms_marlow_pressure(
        0.5F,
        1.5F,
        player,
        false,
        0.25F);
    CHECK(result.pressure == doctest::Approx(0.48625F));
    CHECK(result.quiet_seconds == doctest::Approx(1.75F));
}

TEST_CASE("Le directeur conserve les proportions de modes ciblees") {
    auto random_state = 0x504F4F4CU;
    for (auto index = 0; index < 1'000; ++index) {
        const auto low = select_backrooms_marlow_mode(
            random_state,
            0.20F,
            true,
            BackroomsMarlowEncounterMode::CornerPeek);
        CHECK(low.mode == BackroomsMarlowEncounterMode::CornerPeek);
        random_state = low.next_random_state;
    }

    std::array<int, 3> medium_counts {};
    auto previous = BackroomsMarlowEncounterMode::CornerPeek;
    for (auto index = 0; index < 30'000; ++index) {
        const auto selected = select_backrooms_marlow_mode(
            random_state,
            0.60F,
            index > 0,
            previous);
        previous = selected.mode;
        random_state = selected.next_random_state;
        ++medium_counts[static_cast<std::size_t>(selected.mode)];
    }
    CHECK(medium_counts[0] > 12'900);
    CHECK(medium_counts[0] < 14'100);
    CHECK(medium_counts[1] > 15'900);
    CHECK(medium_counts[1] < 17'100);
    CHECK(medium_counts[2] == 0);

    std::array<int, 3> high_counts {};
    for (auto index = 0; index < 40'000; ++index) {
        const auto selected = select_backrooms_marlow_mode(
            random_state,
            0.95F,
            true,
            previous);
        previous = selected.mode;
        random_state = selected.next_random_state;
        ++high_counts[static_cast<std::size_t>(selected.mode)];
    }
    CHECK(high_counts[0] > 13'200);
    CHECK(high_counts[0] < 14'800);
    CHECK(high_counts[1] > 17'200);
    CHECK(high_counts[1] < 18'800);
    CHECK(high_counts[2] > 7'200);
    CHECK(high_counts[2] < 8'800);
}

TEST_CASE("L A star aquatique reste deterministe et le blocage exige un detour") {
    auto grid = handmade_grid();
    for (auto z = 30; z <= 34; ++z) {
        open_cell(grid, 30, z, true, true);
    }
    open_cell(grid, 31, 31, true, true);
    open_cell(grid, 31, 32, true, true);
    open_cell(grid, 31, 33, true, true);

    const BackroomsMarlowGridPoint start {30, 30};
    const BackroomsMarlowGridPoint goal {30, 34};
    const BackroomsMarlowGridPoint blocked {30, 32};
    const auto first = find_backrooms_marlow_path(grid, start, goal);
    const auto repeated = find_backrooms_marlow_path(grid, start, goal);
    REQUIRE_FALSE(first.empty());
    CHECK(first.count == repeated.count);
    for (std::size_t index = 0U; index < first.count; ++index) {
        CHECK(first.nodes[index] == repeated.nodes[index]);
    }
    CHECK(backrooms_marlow_has_detour(grid, start, goal, blocked));

    grid.cells[grid_index(grid, 31, 32)].walkable = false;
    CHECK_FALSE(backrooms_marlow_has_detour(grid, start, goal, blocked));
}

TEST_CASE("Le supercover refuse les coins et les chunks non publies") {
    auto grid = handmade_grid();
    open_cell(grid, 30, 30);
    open_cell(grid, 31, 30);
    open_cell(grid, 30, 31);
    open_cell(grid, 31, 31);
    auto readiness = full_readiness(grid.center_chunk);
    CHECK(backrooms_marlow_supercover_clear(
        grid,
        readiness,
        {30, 30},
        {31, 31}));

    grid.cells[grid_index(grid, 31, 30)].walkable = false;
    CHECK_FALSE(backrooms_marlow_supercover_clear(
        grid,
        readiness,
        {30, 30},
        {31, 31}));
    grid.cells[grid_index(grid, 31, 30)].walkable = true;

    const ChunkCoord destination_chunk {1, 1};
    readiness.ready[readiness_index(readiness, destination_chunk)] = false;
    CHECK_FALSE(backrooms_marlow_supercover_clear(
        grid,
        readiness,
        {30, 30},
        {31, 31}));
}

TEST_CASE("Marlow choisit un angle visible sans traverser un chunk absent") {
    auto grid = handmade_grid();
    for (auto z = 30; z <= 45; ++z) {
        open_cell(grid, 30, z, z >= 42, z >= 40);
    }
    auto readiness = full_readiness(grid.center_chunk);
    const auto player = marlow_player(30.5F, 30.5F);
    const auto selection = select_backrooms_marlow_manifestation(
        grid,
        readiness,
        player,
        BackroomsMarlowEncounterMode::CornerPeek,
        0x434F524EU);
    REQUIRE(selection.found);
    CHECK(selection.mode == BackroomsMarlowEncounterMode::CornerPeek);
    CHECK(selection.position.z >= 42.0F);
    CHECK(selection.position.z <= 46.0F);
    CHECK(std::abs(selection.peek_side) == doctest::Approx(1.0F));
    CHECK(selection.position.x != doctest::Approx(30.5F));

    const auto selected_chunk = backrooms_marlow_chunk_at(selection.position);
    readiness.ready[readiness_index(readiness, selected_chunk)] = false;
    const auto unavailable = select_backrooms_marlow_manifestation(
        grid,
        readiness,
        player,
        BackroomsMarlowEncounterMode::CornerPeek,
        0x434F524EU);
    CHECK_FALSE(unavailable.found);
}

TEST_CASE("Le mode Blocking ne parait que si un vrai chemin de detour existe") {
    auto grid = handmade_grid();
    for (auto z = 30; z <= 56; ++z) {
        open_cell(grid, 30, z, z >= 40 && z <= 48, z >= 42);
    }
    for (auto z = 38; z <= 50; ++z) {
        open_cell(grid, 31, z, z >= 40 && z <= 48, true);
    }
    auto readiness = full_readiness(grid.center_chunk);
    const auto player = marlow_player(30.5F, 30.5F);
    const auto selection = select_backrooms_marlow_manifestation(
        grid,
        readiness,
        player,
        BackroomsMarlowEncounterMode::Blocking,
        0x424C4F43U);
    REQUIRE(selection.found);
    CHECK(selection.mode == BackroomsMarlowEncounterMode::Blocking);
    CHECK(selection.has_guaranteed_detour);
    CHECK(selection.position.z >= 40.0F);
    CHECK(selection.position.z <= 49.0F);

    for (auto z = 38; z <= 50; ++z) {
        grid.cells[grid_index(grid, 31, z)].walkable = false;
    }
    const auto unsafe = select_backrooms_marlow_manifestation(
        grid,
        readiness,
        player,
        BackroomsMarlowEncounterMode::Blocking,
        0x424C4F43U);
    CHECK_FALSE(unsafe.found);
}

TEST_CASE("La navigation reconnait les nappes V4 et leurs bassins profonds") {
    const BackroomsGenerator poolrooms {
        7331,
        -2,
        kBackroomsConnectorDistrictModules,
        BackroomsPoolGeometryProfile::FloodedDistrictsV4,
    };
    auto wet_x = 0;
    auto wet_z = 0;
    auto found_wet = false;
    auto expected_deep = false;
    for (auto module_z = -8; module_z <= 8 && !found_wet; ++module_z) {
        for (auto module_x = -8; module_x <= 8 && !found_wet; ++module_x) {
            if (!poolrooms.is_flooded_module(module_x, module_z)) {
                continue;
            }
            for (auto local_z = 0; local_z < kBackroomsModuleSize && !found_wet;
                 ++local_z) {
                for (auto local_x = 0; local_x < kBackroomsModuleSize;
                     ++local_x) {
                    const auto world_x =
                        module_x * kBackroomsModuleSize + local_x;
                    const auto world_z =
                        module_z * kBackroomsModuleSize + local_z;
                    const auto column = poolrooms.sample_column(
                        world_x,
                        world_z);
                    if (column.pool_surface != BackroomsPoolSurface::Water ||
                        !poolrooms.is_walkable(world_x, world_z)) {
                        continue;
                    }
                    wet_x = world_x;
                    wet_z = world_z;
                    expected_deep = column.deep_water;
                    found_wet = true;
                    break;
                }
            }
        }
    }
    REQUIRE(found_wet);
    const glm::vec3 wet_position {
        static_cast<float>(wet_x) + 0.5F,
        40.0F,
        static_cast<float>(wet_z) + 0.5F,
    };
    const auto grid = build_backrooms_marlow_navigation_grid(
        poolrooms,
        backrooms_marlow_chunk_at(wet_position));
    const auto* cell = backrooms_marlow_navigation_cell(
        grid,
        wet_x,
        wet_z);
    REQUIRE(cell != nullptr);
    CHECK(cell->walkable);
    CHECK(cell->has_water);
    CHECK(cell->water_depth >= 0.99F);
    CHECK(cell->deep_water == expected_deep);
}

TEST_CASE("La capture exige une bouee une ligne claire et une eau connectee") {
    auto grid = handmade_grid();
    for (auto z = 27; z <= 30; ++z) {
        open_cell(grid, 30, z, z == 30, true);
    }
    auto readiness = full_readiness(grid.center_chunk);
    auto player = marlow_player(30.5F, 30.5F, {0.0F, 0.0F, -1.0F});
    player.in_water = true;
    player.water_depth = 1.0F;
    const glm::vec3 marlow {30.5F, 40.001F, 27.5F};

    const auto unwarned = evaluate_backrooms_marlow_capture(
        grid,
        readiness,
        player,
        marlow,
        false);
    CHECK(unwarned.player_reachable);
    CHECK(unwarned.connected_water_found);
    CHECK_FALSE(unwarned.allowed);

    const auto warned = evaluate_backrooms_marlow_capture(
        grid,
        readiness,
        player,
        marlow,
        true);
    CHECK(warned.allowed);
    CHECK(warned.water_target.y >= 40.0F);

    grid.cells[grid_index(grid, 30, 29)].walkable = false;
    const auto blocked = evaluate_backrooms_marlow_capture(
        grid,
        readiness,
        player,
        marlow,
        true);
    CHECK_FALSE(blocked.player_reachable);
    CHECK_FALSE(blocked.allowed);
}

TEST_CASE("Marlow reste inactif hors des Poolrooms") {
    const BackroomsGenerator offices {7331, -1};
    auto state = initialize_backrooms_marlow(7331U, -1);
    BackroomsMarlowRuntime runtime {};
    BackroomsMarlowUpdateContext context {};
    context.player.sprinting = true;
    context.player.travelled_horizontal_distance = 1.0F;
    const auto result = update_backrooms_marlow(
        state,
        runtime,
        offices,
        context,
        0.25F);
    CHECK(state.pressure == 0.0F);
    CHECK(runtime.phase == BackroomsMarlowPhase::Dormant);
    CHECK(result.event_count == 0U);
    CHECK_FALSE(result.render.visible);
}

TEST_CASE("Un signal sans cellule d eau prete conserve sa fenetre") {
    const BackroomsGenerator poolrooms {
        7331,
        -2,
        kBackroomsConnectorDistrictModules,
        BackroomsPoolGeometryProfile::FloodedDistrictsV4,
    };
    auto grid = handmade_grid();
    for (auto z = 24; z <= 46; ++z) {
        open_cell(grid, 30, z, false, true);
    }
    const auto readiness = full_readiness(grid.center_chunk);
    auto state = initialize_backrooms_marlow(0x43554553U, -2);
    state.cue_seconds = 0.0F;
    state.manifestation_seconds = 20.0F;
    BackroomsMarlowRuntime runtime {};
    runtime.navigation = grid;
    runtime.navigation_readiness = readiness;
    runtime.navigation_valid = true;
    runtime.navigation_readiness_valid = true;
    runtime.grace_seconds = 5.0F;
    BackroomsMarlowUpdateContext context {};
    context.player = marlow_player(30.5F, 30.5F);
    context.chunk_readiness = readiness;
    const auto result = update_backrooms_marlow(
        state,
        runtime,
        poolrooms,
        context,
        0.10F);
    CHECK(result.event_count == 0U);
    CHECK(state.cue_seconds == doctest::Approx(1.0F));
}

TEST_CASE("Marlow ne montre que sa tete pendant le regard d'angle") {
    const BackroomsGenerator poolrooms {
        7331,
        -2,
        kBackroomsConnectorDistrictModules,
        BackroomsPoolGeometryProfile::FloodedDistrictsV4,
    };
    auto state = initialize_backrooms_marlow(0x5045454BU, -2);
    auto runtime = std::make_unique<BackroomsMarlowRuntime>();
    runtime->navigation = handmade_grid();
    runtime->navigation_readiness =
        full_readiness(runtime->navigation.center_chunk);
    runtime->navigation_valid = true;
    runtime->navigation_readiness_valid = true;
    runtime->phase = BackroomsMarlowPhase::CornerPeek;
    runtime->phase_duration_seconds = 4.0F;
    runtime->phase_seconds = 1.0F;

    BackroomsMarlowUpdateContext context {};
    context.player = marlow_player(24.5F, 24.5F);
    context.chunk_readiness = runtime->navigation_readiness;
    context.threat_slot_available = false;
    context.threat_slot_owned = true;
    context.simulation_frozen = true;
    const auto peek = update_backrooms_marlow(
        state,
        *runtime,
        poolrooms,
        context,
        0.0F);
    REQUIRE(peek.render.visible);
    CHECK(peek.render.phase == BackroomsMarlowPhase::CornerPeek);
    CHECK(peek.render.immersion_ratio == doctest::Approx(0.90F));
    CHECK(peek.render.reveal_amount == doctest::Approx(1.0F));

    runtime->phase = BackroomsMarlowPhase::Emerging;
    runtime->phase_seconds = 0.0F;
    runtime->phase_duration_seconds = 1.0F;
    const auto submerged = update_backrooms_marlow(
        state,
        *runtime,
        poolrooms,
        context,
        0.0F);
    REQUIRE(submerged.render.visible);
    CHECK(submerged.render.reveal_amount == doctest::Approx(0.0F));
}

TEST_CASE("La FSM de noyade tue une fois apres 1 virgule 8 seconde") {
    const BackroomsGenerator poolrooms {
        7331,
        -2,
        kBackroomsConnectorDistrictModules,
        BackroomsPoolGeometryProfile::FloodedDistrictsV4,
    };
    auto grid = handmade_grid();
    for (auto z = 27; z <= 30; ++z) {
        open_cell(grid, 30, z, z == 30, true);
    }
    const auto readiness = full_readiness(grid.center_chunk);
    auto state = initialize_backrooms_marlow(0x44524F57U, -2);
    state.cue_seconds = 20.0F;
    state.manifestation_seconds = 20.0F;
    auto runtime = std::make_unique<BackroomsMarlowRuntime>();
    runtime->navigation = grid;
    runtime->navigation_readiness = readiness;
    runtime->navigation_valid = true;
    runtime->navigation_readiness_valid = true;
    runtime->phase = BackroomsMarlowPhase::Emerging;
    runtime->phase_duration_seconds = 8.0F;
    runtime->position = {30.5F, 40.001F, 27.5F};
    runtime->buoy_position = {30.5F, 41.05F, 30.5F};
    runtime->buoy_warning_active = true;
    runtime->grace_seconds = 0.0F;

    BackroomsMarlowUpdateContext context {};
    context.player = marlow_player(
        30.5F,
        30.5F,
        {0.0F, 0.0F, -1.0F});
    context.player.in_water = true;
    context.player.water_depth = 1.0F;
    context.chunk_readiness = readiness;
    context.threat_slot_available = false;
    context.threat_slot_owned = true;

    const auto grabbed = update_backrooms_marlow(
        state,
        *runtime,
        poolrooms,
        context,
        0.10F);
    REQUIRE(grabbed.capture_started);
    CHECK(runtime->phase == BackroomsMarlowPhase::Dragging);
    CHECK(grabbed.capture.lock_player_controls);

    auto kill_count = 0;
    auto screamer_count = 0;
    for (auto step = 0; step < 40; ++step) {
        const auto result = update_backrooms_marlow(
            state,
            *runtime,
            poolrooms,
            context,
            0.10F);
        if (result.kill_player) {
            ++kill_count;
        }
        for (std::size_t event = 0U;
             event < result.event_count;
             ++event) {
            if (result.events[event].kind ==
                BackroomsMarlowEventKind::Screamer) {
                ++screamer_count;
            }
        }
    }
    CHECK(kill_count == 1);
    CHECK(screamer_count == 1);
    CHECK(runtime->phase == BackroomsMarlowPhase::Cooldown);
}

TEST_CASE("Les revisions internes reconstruisent la navigation de Marlow") {
    const BackroomsGenerator poolrooms {
        7331,
        -2,
        kBackroomsConnectorDistrictModules,
        BackroomsPoolGeometryProfile::FloodedDistrictsV4,
    };
    const auto spawn = poolrooms.spawn_block();
    const glm::vec3 feet {
        static_cast<float>(spawn.x) + 0.5F,
        static_cast<float>(spawn.y),
        static_cast<float>(spawn.z) + 0.5F,
    };
    const auto center = backrooms_marlow_chunk_at(feet);
    auto readiness = full_readiness(center);
    auto state = initialize_backrooms_marlow(0x52455653U, -2);
    BackroomsMarlowRuntime runtime {};
    BackroomsMarlowUpdateContext context {};
    context.player = marlow_player(feet.x, feet.z);
    context.chunk_readiness = readiness;
    context.simulation_frozen = true;
    static_cast<void>(update_backrooms_marlow(
        state,
        runtime,
        poolrooms,
        context,
        0.0F));
    REQUIRE(runtime.navigation_valid);
    runtime.navigation.cells[0].clearance = 12'345.0F;

    const ChunkCoord outer {
        center.x + kBackroomsMarlowReadinessChunkRadius,
        center.z + kBackroomsMarlowReadinessChunkRadius,
    };
    readiness.mesh_revisions[readiness_index(readiness, outer)] = 2U;
    context.chunk_readiness = readiness;
    static_cast<void>(update_backrooms_marlow(
        state,
        runtime,
        poolrooms,
        context,
        0.0F));
    CHECK(runtime.navigation.cells[0].clearance ==
          doctest::Approx(12'345.0F));

    readiness.mesh_revisions[readiness_index(readiness, center)] = 3U;
    context.chunk_readiness = readiness;
    static_cast<void>(update_backrooms_marlow(
        state,
        runtime,
        poolrooms,
        context,
        0.0F));
    CHECK(runtime.navigation.cells[0].clearance !=
          doctest::Approx(12'345.0F));
}

TEST_CASE("Une revision annule proprement la demande de menace obsolete") {
    const BackroomsGenerator poolrooms {
        7331,
        -2,
        kBackroomsConnectorDistrictModules,
        BackroomsPoolGeometryProfile::FloodedDistrictsV4,
    };
    auto grid = handmade_grid();
    for (auto z = 30; z <= 45; ++z) {
        open_cell(grid, 30, z, z >= 42, true);
    }
    auto readiness = full_readiness(grid.center_chunk);
    auto state = initialize_backrooms_marlow(0x50454E44U, -2);
    BackroomsMarlowRuntime runtime {};
    runtime.navigation = grid;
    runtime.navigation_readiness = readiness;
    runtime.navigation_valid = true;
    runtime.navigation_readiness_valid = true;
    runtime.pending_manifestation =
        select_backrooms_marlow_manifestation(
            grid,
            readiness,
            marlow_player(30.5F, 30.5F),
            BackroomsMarlowEncounterMode::CornerPeek,
            0x50454E44U);
    REQUIRE(runtime.pending_manifestation.found);
    runtime.waiting_for_threat_slot = true;

    readiness.mesh_revisions[
        readiness_index(readiness, grid.center_chunk)] = 2U;
    BackroomsMarlowUpdateContext context {};
    context.player = marlow_player(30.5F, 30.5F);
    context.chunk_readiness = readiness;
    context.simulation_frozen = true;
    const auto result = update_backrooms_marlow(
        state,
        runtime,
        poolrooms,
        context,
        0.0F);
    CHECK(result.cancels_threat_request);
    CHECK_FALSE(result.requests_threat_slot);
    CHECK_FALSE(runtime.waiting_for_threat_slot);
    CHECK(runtime.retry_seconds == doctest::Approx(1.0F));
    CHECK(state.manifestation_seconds == doctest::Approx(0.0F));
}

TEST_CASE("La grille de Marlow superpose les obstacles reels du World") {
    constexpr auto seed = 7331;
    constexpr auto logical_level = -2;
    const auto generator = std::make_unique<BackroomsGenerator>(
        seed,
        logical_level,
        kBackroomsConnectorDistrictModules,
        BackroomsPoolGeometryProfile::FloodedDistrictsV4);
    const auto stack = std::make_unique<BackroomsSpatialStack>(
        seed,
        logical_level,
        BackroomsSpatialProfile::FloodedPoolroomsV4);
    const auto placement = stack->placement_for_level(logical_level);
    REQUIRE(placement.has_value());
    const auto world_y_offset = placement->floor_y - kBackroomsFloorY;
    auto world = std::make_unique<World>(
        seed,
        0,
        WorldGenerationProfile::Backrooms,
        WorldGenerationVersion::BackroomsV4,
        VisualPipeline::ModernStylized,
        logical_level);
    const auto spawn = generator->spawn_block();
    const glm::vec3 spawn_position {
        static_cast<float>(spawn.x) + 0.5F,
        static_cast<float>(spawn.y),
        static_cast<float>(spawn.z) + 0.5F,
    };
    auto clear_grid = std::make_unique<BackroomsMarlowNavigationGrid>(
        build_backrooms_marlow_navigation_grid(
            *generator,
            backrooms_marlow_chunk_at(spawn_position)));
    auto obstacle = BackroomsMarlowGridPoint {};
    auto local_body_y = 0;
    auto found = false;
    for (std::size_t index = 0U;
         index < clear_grid->cells.size() && !found;
         ++index) {
        if (!clear_grid->cells[index].walkable) {
            continue;
        }
        const auto local_z = static_cast<int>(
            index / kBackroomsMarlowNavigationSide);
        const auto local_x = static_cast<int>(
            index % kBackroomsMarlowNavigationSide);
        obstacle = {
            clear_grid->origin_world_x + local_x,
            clear_grid->origin_world_z + local_z,
        };
        const auto column = generator->sample_column(
            obstacle.x,
            obstacle.z);
        local_body_y = column.floor_y + 1;
        found = !is_block_collidable(world->peek_block_or_generated(
            obstacle.x,
            local_body_y + world_y_offset,
            obstacle.z));
    }
    REQUIRE(found);
    world->set_block(
        obstacle.x,
        local_body_y + world_y_offset,
        obstacle.z,
        to_block_id(BlockType::Stone));

    const auto blocked_grid =
        std::make_unique<BackroomsMarlowNavigationGrid>(
            build_backrooms_marlow_navigation_grid(
            *generator,
            clear_grid->center_chunk,
            world.get(),
            world_y_offset));
    const auto* blocked_cell = backrooms_marlow_navigation_cell(
        *blocked_grid,
        obstacle.x,
        obstacle.z);
    REQUIRE(blocked_cell != nullptr);
    CHECK_FALSE(blocked_cell->walkable);
}

} // namespace valcraft
