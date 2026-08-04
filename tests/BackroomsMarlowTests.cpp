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
#include <utility>

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
    bool dark = false,
    float water_depth = 1.0F) {
    const auto safe_water_depth = water
        ? std::max(water_depth, 0.0F)
        : 0.0F;
    grid.cells[grid_index(grid, world_x, world_z)] = {
        40.0F,
        40.0F + safe_water_depth,
        safe_water_depth,
        7.0F,
        true,
        water,
        safe_water_depth >= 1.45F,
        false,
        dark,
    };
}

void open_marlow_space_at(
    BackroomsMarlowNavigationGrid& grid,
    int world_x,
    int world_z,
    bool water = false,
    bool dark = false,
    float water_depth = 1.0F) {
    const auto center_x = static_cast<float>(world_x) + 0.5F;
    const auto center_z = static_cast<float>(world_z) + 0.5F;
    const auto radius = static_cast<int>(
        std::ceil(kBackroomsMarlowRigVisualRadius));
    for (auto delta_z = -radius; delta_z <= radius; ++delta_z) {
        for (auto delta_x = -radius; delta_x <= radius; ++delta_x) {
            const auto candidate_x = world_x + delta_x;
            const auto candidate_z = world_z + delta_z;
            const auto nearest_x = std::clamp(
                center_x,
                static_cast<float>(candidate_x),
                static_cast<float>(candidate_x) + 1.0F);
            const auto nearest_z = std::clamp(
                center_z,
                static_cast<float>(candidate_z),
                static_cast<float>(candidate_z) + 1.0F);
            const auto offset_x = center_x - nearest_x;
            const auto offset_z = center_z - nearest_z;
            if (offset_x * offset_x + offset_z * offset_z <=
                kBackroomsMarlowRigVisualRadius *
                    kBackroomsMarlowRigVisualRadius) {
                open_cell(grid, candidate_x, candidate_z);
            }
        }
    }
    open_cell(grid, world_x, world_z, water, dark, water_depth);
}

[[nodiscard]] auto handmade_grid() -> BackroomsMarlowNavigationGrid {
    BackroomsMarlowNavigationGrid grid {};
    grid.cells.resize(kBackroomsMarlowNavigationCellCount);
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

// Je verrouille le contrat d'exception des chemins qui peuvent allouer : une
// penurie memoire doit remonter a l'appelant au lieu de terminer le processus.
static_assert(!noexcept(build_backrooms_marlow_navigation_grid(
    std::declval<const BackroomsGenerator&>(),
    std::declval<const ChunkCoord&>())));
static_assert(!noexcept(select_backrooms_marlow_manifestation(
    std::declval<const BackroomsMarlowNavigationGrid&>(),
    std::declval<const BackroomsMarlowChunkReadiness&>(),
    std::declval<const BackroomsMarlowPlayerContext&>(),
    BackroomsMarlowEncounterMode::CornerPeek,
    1U)));
static_assert(!noexcept(evaluate_backrooms_marlow_capture(
    std::declval<const BackroomsMarlowNavigationGrid&>(),
    std::declval<const BackroomsMarlowChunkReadiness&>(),
    std::declval<const BackroomsMarlowPlayerContext&>(),
    std::declval<const glm::vec3&>(),
    false)));

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

    auto without_history = first;
    without_history.last_mode = BackroomsMarlowEncounterMode::WaterAmbush;
    without_history.has_last_mode = false;
    const auto canonical = sanitize_backrooms_marlow_state(without_history);
    CHECK_FALSE(canonical.has_last_mode);
    CHECK(canonical.last_mode == BackroomsMarlowEncounterMode::CornerPeek);
}

TEST_CASE("La persistance transforme toute manifestation en sortie durable deterministe") {
    auto state = initialize_backrooms_marlow(0x50455253U, -2);
    state.pressure = 0.60F;
    state.cooldown_seconds = 0.0F;
    state.manifestation_seconds = 0.0F;
    const auto original_random_state = state.random_state;
    constexpr std::array<BackroomsMarlowPhase, 8> kActivePhases {{
        BackroomsMarlowPhase::Signaling,
        BackroomsMarlowPhase::CornerPeek,
        BackroomsMarlowPhase::Emerging,
        BackroomsMarlowPhase::Blocking,
        BackroomsMarlowPhase::Submerging,
        BackroomsMarlowPhase::Dragging,
        BackroomsMarlowPhase::Drowning,
        BackroomsMarlowPhase::Screamer,
    }};

    for (const auto phase : kActivePhases) {
        BackroomsMarlowRuntime runtime {};
        runtime.phase = phase;
        const auto first = prepare_backrooms_marlow_for_persistence(
            state,
            runtime);
        const auto repeated = prepare_backrooms_marlow_for_persistence(
            state,
            runtime);
        CHECK(first.cooldown_seconds ==
              doctest::Approx(repeated.cooldown_seconds));
        CHECK(first.manifestation_seconds ==
              doctest::Approx(repeated.manifestation_seconds));
        CHECK(first.random_state == repeated.random_state);
        CHECK(first.cooldown_seconds >= 12.0F);
        CHECK(first.cooldown_seconds <= 17.0F);
        CHECK(first.manifestation_seconds >= 16.0F);
        CHECK(first.manifestation_seconds <= 42.0F);
        CHECK(first.random_state != original_random_state);
    }

    BackroomsMarlowRuntime waiting {};
    waiting.waiting_for_threat_slot = true;
    const auto pending = prepare_backrooms_marlow_for_persistence(
        state,
        waiting);
    CHECK(pending.cooldown_seconds >= 12.0F);
    CHECK(pending.manifestation_seconds > 0.0F);

    BackroomsMarlowRuntime dormant {};
    const auto untouched = prepare_backrooms_marlow_for_persistence(
        state,
        dormant);
    CHECK(untouched.cooldown_seconds == doctest::Approx(0.0F));
    CHECK(untouched.manifestation_seconds == doctest::Approx(0.0F));
    CHECK(untouched.random_state == original_random_state);
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

TEST_CASE("Le mouvement force par Marlow ne genere aucune pression") {
    auto player = marlow_player(30.5F, 30.5F);
    player.motion_is_forced = true;
    player.sprinting = true;
    player.in_water = true;
    player.travelled_horizontal_distance = 4.0F;
    player.entered_water = true;
    player.jumped = true;
    player.landed_in_water = true;

    const auto forced = evaluate_backrooms_marlow_pressure(
        0.0F,
        0.0F,
        player,
        false,
        0.25F);
    CHECK(forced.pressure == doctest::Approx(0.0F));
    CHECK(forced.quiet_seconds == doctest::Approx(0.25F));

    player.flashlight_on_water = true;
    const auto flashlight = evaluate_backrooms_marlow_pressure(
        0.0F,
        0.0F,
        player,
        true,
        0.25F);
    CHECK(flashlight.pressure == doctest::Approx(0.12F));
}

TEST_CASE("Le directeur conserve les proportions de modes ciblees") {
    const auto fallback = select_backrooms_marlow_mode(
        0U,
        0.60F,
        true,
        static_cast<BackroomsMarlowEncounterMode>(255U));
    CHECK(fallback.next_random_state != 0U);
    CHECK(static_cast<std::size_t>(fallback.mode) < 3U);

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
    CHECK(medium_counts[0] > 5'500);
    CHECK(medium_counts[0] < 6'700);
    CHECK(medium_counts[1] > 18'700);
    CHECK(medium_counts[1] < 20'100);
    CHECK(medium_counts[2] > 4'000);
    CHECK(medium_counts[2] < 5'100);

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
    CHECK(high_counts[0] > 2'700);
    CHECK(high_counts[0] < 3'700);
    CHECK(high_counts[1] > 18'000);
    CHECK(high_counts[1] < 19'500);
    CHECK(high_counts[2] > 17'300);
    CHECK(high_counts[2] < 18'900);
}

TEST_CASE("La grille lazy et les chemins malformes sont rejetes sans acces hors limites") {
    BackroomsMarlowNavigationGrid grid {};
    CHECK(grid.cells.empty());
    CHECK(backrooms_marlow_navigation_cell(grid, 0, 0) == nullptr);
    CHECK(find_backrooms_marlow_path(grid, {0, 0}, {1, 0}).empty());
    CHECK_FALSE(backrooms_marlow_has_detour(
        grid,
        {0, 0},
        {1, 0},
        {0, 1}));

    const auto invalid_selection = select_backrooms_marlow_manifestation(
        grid,
        full_readiness({0, 0}),
        marlow_player(0.5F, 0.5F),
        static_cast<BackroomsMarlowEncounterMode>(255U),
        0U);
    CHECK_FALSE(invalid_selection.found);
    CHECK(invalid_selection.mode ==
          BackroomsMarlowEncounterMode::CornerPeek);
    CHECK(invalid_selection.next_random_state != 0U);

    grid.cells.resize(kBackroomsMarlowNavigationCellCount - 1U);
    CHECK(backrooms_marlow_navigation_cell(grid, 0, 0) == nullptr);
    CHECK(find_backrooms_marlow_path(grid, {0, 0}, {1, 0}).empty());

    grid.cells.resize(kBackroomsMarlowNavigationCellCount + 1U);
    CHECK(backrooms_marlow_navigation_cell(grid, 0, 0) == nullptr);
    CHECK(find_backrooms_marlow_path(grid, {0, 0}, {1, 0}).empty());

    BackroomsMarlowPath malformed {};
    malformed.nodes.push_back({0, 0});
    malformed.cursor = 2U;
    CHECK(malformed.empty());
    malformed.clear();
    CHECK(malformed.nodes.empty());
    CHECK(malformed.cursor == 0U);

    auto extreme = handmade_grid();
    extreme.origin_world_x =
        std::numeric_limits<int>::max() -
        kBackroomsMarlowNavigationSide + 1;
    extreme.origin_world_z = extreme.origin_world_x;
    for (auto& cell : extreme.cells) {
        cell.walkable = true;
        cell.clearance = 8.0F;
    }
    auto huge_player = marlow_player(
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max());
    const auto readiness = full_readiness(
        backrooms_marlow_chunk_at(huge_player.feet_position));
    const auto huge = select_backrooms_marlow_manifestation(
        extreme,
        readiness,
        huge_player,
        BackroomsMarlowEncounterMode::Blocking,
        1U);
    CHECK_FALSE(huge.found);
}

TEST_CASE("L A star aquatique reste deterministe et le blocage exige un detour") {
    auto grid = handmade_grid();
    for (auto z = 30; z <= 34; ++z) {
        open_marlow_space_at(grid, 30, z, true, true);
    }
    open_marlow_space_at(grid, 31, 31, true, true);
    open_marlow_space_at(grid, 31, 32, true, true);
    open_marlow_space_at(grid, 31, 33, true, true);

    const BackroomsMarlowGridPoint start {30, 30};
    const BackroomsMarlowGridPoint goal {30, 34};
    const BackroomsMarlowGridPoint blocked {30, 32};
    const auto first = find_backrooms_marlow_path(grid, start, goal);
    const auto repeated = find_backrooms_marlow_path(grid, start, goal);
    REQUIRE_FALSE(first.empty());
    CHECK(first.nodes.size() == repeated.nodes.size());
    for (std::size_t index = 0U; index < first.nodes.size(); ++index) {
        CHECK(first.nodes[index] == repeated.nodes[index]);
    }
    CHECK(backrooms_marlow_has_detour(grid, start, goal, blocked));
    CHECK_FALSE(backrooms_marlow_has_detour(
        grid,
        start,
        goal,
        start));
    CHECK_FALSE(backrooms_marlow_has_detour(
        grid,
        start,
        goal,
        goal));

    grid.cells[grid_index(grid, 33, 32)].walkable = false;
    CHECK_FALSE(backrooms_marlow_has_detour(grid, start, goal, blocked));
}

TEST_CASE("Le rayon visuel refuse explicitement un couloir trop etroit") {
    auto grid = handmade_grid();
    for (auto z = 30; z <= 34; ++z) {
        open_cell(grid, 30, z);
    }
    CHECK(find_backrooms_marlow_path(
              grid,
              {30, 30},
              {30, 34})
              .empty());
}

TEST_CASE("L A star refuse une marche plus haute que Marlow") {
    auto grid = handmade_grid();
    open_marlow_space_at(grid, 30, 30);
    open_marlow_space_at(grid, 31, 30);
    grid.cells[grid_index(grid, 31, 30)].floor_y = 42.0F;

    const auto path = find_backrooms_marlow_path(
        grid,
        {30, 30},
        {31, 30});
    CHECK(path.empty());
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
    CHECK(backrooms_marlow_supercover_clear(
        grid,
        readiness,
        {30, 30},
        {30, 30}));
    CHECK(backrooms_marlow_supercover_clear(
        grid,
        readiness,
        {30, 30},
        {31, 30}));
    CHECK(backrooms_marlow_supercover_clear(
        grid,
        readiness,
        {30, 30},
        {30, 31}));
    CHECK(backrooms_marlow_supercover_clear(
        grid,
        readiness,
        {31, 31},
        {30, 30}));

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
        open_marlow_space_at(grid, 30, z, z >= 42, z >= 40);
    }
    for (auto z = 40; z <= 45; ++z) {
        grid.cells[grid_index(grid, 29, z)].walkable = false;
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
    CHECK(selection.presentation ==
          BackroomsMarlowPresentation::HeadOnlyPeek);
    CHECK(std::abs(selection.wall_normal.x) +
              std::abs(selection.wall_normal.z) ==
          doctest::Approx(1.0F));
    // Je garde la racine au centre : seul le rig visuel se penche à l'angle.
    CHECK(selection.position.x == doctest::Approx(30.5F));

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
        open_marlow_space_at(
            grid,
            30,
            z,
            z >= 40 && z <= 48,
            z >= 42);
    }
    for (auto z = 38; z <= 50; ++z) {
        open_marlow_space_at(
            grid,
            31,
            z,
            z >= 40 && z <= 48,
            true);
    }
    // Je pose l'eau en dernier : l'ouverture des footprints voisins ne doit
    // pas remplacer le bassin central par leurs cellules seches de marge.
    for (auto z = 40; z <= 48; ++z) {
        open_cell(grid, 30, z, true, true);
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
    CHECK(grid.cells.size() == kBackroomsMarlowNavigationCellCount);
    const auto* cell = backrooms_marlow_navigation_cell(
        grid,
        wet_x,
        wet_z);
    REQUIRE(cell != nullptr);
    CHECK(cell->walkable);
    CHECK(cell->has_water);
    CHECK(cell->water_depth >= 0.99F);
    CHECK(cell->deep_water == expected_deep);

    const BackroomsMarlowGridPoint wet_point {wet_x, wet_z};
    const auto viable_path = find_backrooms_marlow_path(
        grid,
        wet_point,
        wet_point);
    REQUIRE_FALSE(viable_path.empty());
    const auto viable_point = viable_path.nodes.front();
    const auto* viable_cell = backrooms_marlow_navigation_cell(
        grid,
        viable_point.x,
        viable_point.z);
    REQUIRE(viable_cell != nullptr);
    auto player = marlow_player(
        static_cast<float>(viable_point.x) + 0.5F,
        static_cast<float>(viable_point.z) + 0.5F);
    player.feet_position.y = viable_cell->floor_y + 0.001F;
    player.eye_position.y = player.feet_position.y + 1.62F;
    const auto readiness = full_readiness(grid.center_chunk);
    constexpr std::array<glm::vec3, 4> kLooks {{
        {1.0F, 0.0F, 0.0F},
        {-1.0F, 0.0F, 0.0F},
        {0.0F, 0.0F, 1.0F},
        {0.0F, 0.0F, -1.0F},
    }};
    constexpr std::array<BackroomsMarlowEncounterMode, 3> kModes {{
        BackroomsMarlowEncounterMode::CornerPeek,
        BackroomsMarlowEncounterMode::Blocking,
        BackroomsMarlowEncounterMode::WaterAmbush,
    }};
    auto manifestation_found = false;
    auto seed = UINT32_C(0x56494142);
    for (const auto look : kLooks) {
        player.look_direction = look;
        for (const auto mode : kModes) {
            const auto selection = select_backrooms_marlow_manifestation(
                grid,
                readiness,
                player,
                mode,
                seed++);
            manifestation_found = manifestation_found || selection.found;
        }
    }
    CHECK(manifestation_found);
}

TEST_CASE("La capture exige une bouee une ligne claire et une eau connectee") {
    auto grid = handmade_grid();
    for (auto z = 27; z <= 30; ++z) {
        open_marlow_space_at(grid, 30, z, z == 30, true);
    }
    auto readiness = full_readiness(grid.center_chunk);
    auto player = marlow_player(30.5F, 30.5F, {0.0F, 0.0F, -1.0F});
    player.in_water = true;
    const glm::vec3 marlow {30.5F, 40.001F, 27.5F};

    const auto shallow = evaluate_backrooms_marlow_capture(
        grid,
        readiness,
        player,
        marlow,
        true);
    CHECK_FALSE(shallow.connected_water_found);
    CHECK_FALSE(shallow.allowed);

    open_cell(grid, 30, 30, true, true, 1.71F);
    const auto almost_deep = evaluate_backrooms_marlow_capture(
        grid,
        readiness,
        player,
        marlow,
        true);
    CHECK_FALSE(almost_deep.connected_water_found);
    CHECK_FALSE(almost_deep.allowed);

    open_cell(grid, 30, 30, true, true, 1.72F);

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
    CHECK(warned.water_target.y == doctest::Approx(40.05F));

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

TEST_CASE("La capture ne tire jamais le joueur en diagonale a travers un mur") {
    auto grid = handmade_grid();
    open_cell(grid, 30, 29);
    open_cell(grid, 30, 30);
    open_cell(grid, 30, 31);
    open_cell(grid, 31, 31, true, true, 2.0F);
    const auto readiness = full_readiness(grid.center_chunk);

    auto player = marlow_player(30.5F, 30.5F);
    const auto result = evaluate_backrooms_marlow_capture(
        grid,
        readiness,
        player,
        {30.5F, 40.001F, 29.5F},
        true);

    CHECK(result.player_reachable);
    CHECK_FALSE(result.connected_water_found);
    CHECK_FALSE(result.allowed);
}

TEST_CASE("La capture refuse une transition de sol infranchissable") {
    auto grid = handmade_grid();
    for (auto z = 27; z <= 30; ++z) {
        open_cell(grid, 30, z, z == 30, true, z == 30 ? 2.0F : 0.0F);
    }
    grid.cells[grid_index(grid, 30, 29)].floor_y = 42.0F;
    grid.cells[grid_index(grid, 30, 29)].water_surface_y = 42.0F;
    const auto readiness = full_readiness(grid.center_chunk);

    const auto result = evaluate_backrooms_marlow_capture(
        grid,
        readiness,
        marlow_player(30.5F, 30.5F),
        {30.5F, 40.001F, 27.5F},
        true);

    CHECK_FALSE(result.player_reachable);
    CHECK_FALSE(result.connected_water_found);
    CHECK_FALSE(result.allowed);
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

TEST_CASE("Le reset rend toujours la demande et le slot avant de vider le runtime") {
    const BackroomsGenerator offices {7331, -1};
    auto state = initialize_backrooms_marlow(7331U, -2);
    state.pressure = 0.80F;
    BackroomsMarlowRuntime runtime {};
    runtime.navigation = handmade_grid();
    runtime.navigation.cells.reserve(
        kBackroomsMarlowNavigationCellCount + 64U);
    const auto navigation_capacity = runtime.navigation.cells.capacity();
    runtime.path.nodes.reserve(32U);
    runtime.path.nodes.push_back({30, 30});
    const auto path_capacity = runtime.path.nodes.capacity();
    runtime.waiting_for_threat_slot = true;
    runtime.phase = BackroomsMarlowPhase::Blocking;

    BackroomsMarlowUpdateContext context {};
    context.threat_slot_owned = true;
    const auto outside = update_backrooms_marlow(
        state,
        runtime,
        offices,
        context,
        0.10F);

    CHECK(outside.cancels_threat_request);
    CHECK(outside.releases_threat_slot);
    CHECK_FALSE(runtime.waiting_for_threat_slot);
    CHECK(runtime.phase == BackroomsMarlowPhase::Dormant);
    CHECK(runtime.navigation.cells.empty());
    CHECK(runtime.navigation.cells.capacity() == navigation_capacity);
    CHECK(runtime.path.nodes.empty());
    CHECK(runtime.path.nodes.capacity() == path_capacity);
    CHECK(runtime.pressure_hysteresis_initialized);
    CHECK_FALSE(runtime.pressure_attack_armed);

    reset_backrooms_marlow_runtime(runtime, 0.10F, false);
    CHECK(runtime.grace_seconds == doctest::Approx(0.0F));
    CHECK(runtime.pressure_attack_armed);
    CHECK(runtime.navigation.cells.capacity() == navigation_capacity);
}

TEST_CASE("Une manifestation en attente demande puis acquiert exactement un slot") {
    const BackroomsGenerator poolrooms {
        7331,
        -2,
        kBackroomsConnectorDistrictModules,
        BackroomsPoolGeometryProfile::FloodedDistrictsV4,
    };
    auto grid = handmade_grid();
    open_marlow_space_at(grid, 20, 20, true, true, 2.0F);
    const auto readiness = full_readiness(grid.center_chunk);

    auto state = initialize_backrooms_marlow(0x534C4F54U, -2);
    state.cue_seconds = 0.0F;
    state.manifestation_seconds = 20.0F;
    BackroomsMarlowRuntime runtime {};
    runtime.navigation = grid;
    runtime.navigation_readiness = readiness;
    runtime.navigation_valid = true;
    runtime.navigation_readiness_valid = true;
    runtime.waiting_for_threat_slot = true;
    runtime.pending_manifestation = {
        .position = {20.5F, 40.001F, 20.5F},
        .buoy_position = {20.5F, 42.0F, 20.5F},
        .body_yaw_degrees = 45.0F,
        .peek_side = 1.0F,
        .mode = BackroomsMarlowEncounterMode::WaterAmbush,
        .next_random_state = state.random_state,
        .found = true,
        .has_guaranteed_detour = false,
        .presentation = BackroomsMarlowPresentation::FullBody,
        .wall_normal = {0.0F, 0.0F, 0.0F},
    };

    BackroomsMarlowUpdateContext context {};
    context.player = marlow_player(30.5F, 30.5F);
    context.chunk_readiness = readiness;
    context.threat_slot_available = true;
    context.threat_slot_owned = false;

    // Je publie d'abord la demande sans commencer la manifestation : le
    // directeur attend explicitement la decision unique de l'arbitre.
    const auto requested = update_backrooms_marlow(
        state,
        runtime,
        poolrooms,
        context,
        0.10F);
    CHECK(requested.requests_threat_slot);
    CHECK_FALSE(requested.holds_threat_slot);
    CHECK(runtime.waiting_for_threat_slot);
    CHECK(runtime.phase == BackroomsMarlowPhase::Dormant);
    REQUIRE(requested.event_count == 1U);
    CHECK(requested.events[0].kind ==
          BackroomsMarlowEventKind::WaterSignal);

    // Je ne demarre qu'a la frame ou l'ownership est effectivement attribue.
    context.threat_slot_available = false;
    context.threat_slot_owned = true;
    const auto acquired = update_backrooms_marlow(
        state,
        runtime,
        poolrooms,
        context,
        0.10F);
    CHECK_FALSE(acquired.requests_threat_slot);
    CHECK(acquired.holds_threat_slot);
    CHECK_FALSE(runtime.waiting_for_threat_slot);
    CHECK(runtime.phase == BackroomsMarlowPhase::Signaling);
    CHECK(runtime.buoy_warning_active);
    CHECK(state.has_last_mode);
    CHECK(state.last_mode == BackroomsMarlowEncounterMode::WaterAmbush);
    REQUIRE(acquired.event_count == 1U);
    CHECK(acquired.events[0].kind ==
          BackroomsMarlowEventKind::BuoyAppeared);
}

TEST_CASE("Un changement de Poolrooms restitue la menace sans demarrer le nouveau niveau") {
    const BackroomsGenerator next_poolrooms {
        7331,
        -3,
        kBackroomsConnectorDistrictModules,
        BackroomsPoolGeometryProfile::FloodedDistrictsV4,
    };
    auto state = initialize_backrooms_marlow(7331U, -2);
    BackroomsMarlowRuntime runtime {};
    runtime.navigation = handmade_grid();
    runtime.waiting_for_threat_slot = true;
    runtime.pending_manifestation.found = true;

    BackroomsMarlowUpdateContext context {};
    context.threat_slot_owned = true;
    const auto changed = update_backrooms_marlow(
        state,
        runtime,
        next_poolrooms,
        context,
        0.10F);

    CHECK(changed.cancels_threat_request);
    CHECK(changed.releases_threat_slot);
    CHECK(state.logical_level == -3);
    CHECK(state.cooldown_seconds >= kBackroomsMarlowInitialGraceSeconds);
    CHECK(state.manifestation_seconds >= kBackroomsMarlowInitialGraceSeconds);
    CHECK_FALSE(runtime.navigation_valid);
    CHECK(runtime.navigation.cells.empty());
    CHECK(runtime.phase == BackroomsMarlowPhase::Dormant);
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

TEST_CASE("L ancre de rendu borne toujours l immersion au plancher") {
    const BackroomsGenerator poolrooms {
        7331,
        -2,
        kBackroomsConnectorDistrictModules,
        BackroomsPoolGeometryProfile::FloodedDistrictsV4,
    };
    auto grid = handmade_grid();
    open_cell(grid, 30, 30);
    open_cell(grid, 31, 30, true, true);
    const auto readiness = full_readiness(grid.center_chunk);
    auto state = initialize_backrooms_marlow(0x464C4F52U, -2);
    state.cue_seconds = 20.0F;
    state.manifestation_seconds = 20.0F;

    auto runtime = std::make_unique<BackroomsMarlowRuntime>();
    runtime->navigation = grid;
    runtime->navigation_readiness = readiness;
    runtime->navigation_valid = true;
    runtime->navigation_readiness_valid = true;
    runtime->phase = BackroomsMarlowPhase::Blocking;
    runtime->phase_duration_seconds = 8.0F;
    runtime->position = {30.5F, 40.001F, 30.5F};

    BackroomsMarlowUpdateContext context {};
    context.player = marlow_player(30.5F, 30.5F);
    context.chunk_readiness = readiness;
    context.simulation_frozen = true;

    const auto dry = update_backrooms_marlow(
        state,
        *runtime,
        poolrooms,
        context,
        0.0F);
    REQUIRE(dry.render.visible);
    CHECK(dry.render.position.y == doctest::Approx(40.02F));
    CHECK(dry.render.available_submersion_depth ==
          doctest::Approx(0.0F));

    runtime->position = {31.5F, 40.001F, 30.5F};
    const auto wet = update_backrooms_marlow(
        state,
        *runtime,
        poolrooms,
        context,
        0.0F);
    REQUIRE(wet.render.visible);
    CHECK(wet.render.position.y == doctest::Approx(41.0F));
    CHECK(wet.render.available_submersion_depth ==
          doctest::Approx(0.98F));
    CHECK(wet.render.position.y - wet.render.available_submersion_depth ==
          doctest::Approx(40.02F));
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
    CHECK(peek.render.presentation ==
          BackroomsMarlowPresentation::HeadOnlyPeek);
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
    CHECK(submerged.render.presentation ==
          BackroomsMarlowPresentation::ProgressiveReveal);
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
        open_marlow_space_at(
            grid,
            30,
            z,
            z == 30,
            true,
            z == 30 ? 2.0F : 0.0F);
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

    context.player.feet_position = runtime->capture_target;
    context.player.eye_position = runtime->capture_target +
        glm::vec3 {0.0F, 1.62F, 0.0F};
    context.player.head_in_water = true;
    context.player.motion_is_forced = true;

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

TEST_CASE("La noyade s annule sans tuer si la tete ou les pieds ne sont pas immerges") {
    const BackroomsGenerator poolrooms {
        7331,
        -2,
        kBackroomsConnectorDistrictModules,
        BackroomsPoolGeometryProfile::FloodedDistrictsV4,
    };
    auto grid = handmade_grid();
    open_marlow_space_at(grid, 30, 30, true, true, 2.0F);
    const auto readiness = full_readiness(grid.center_chunk);
    auto state = initialize_backrooms_marlow(0x48454144U, -2);
    state.cue_seconds = 20.0F;
    state.manifestation_seconds = 20.0F;

    const auto make_runtime = [&]() {
        auto runtime = std::make_unique<BackroomsMarlowRuntime>();
        runtime->navigation = grid;
        runtime->navigation_readiness = readiness;
        runtime->navigation_valid = true;
        runtime->navigation_readiness_valid = true;
        runtime->phase = BackroomsMarlowPhase::Drowning;
        runtime->phase_duration_seconds = 0.0F;
        runtime->position = {30.5F, 40.05F, 30.5F};
        runtime->capture_target = {30.5F, 40.05F, 30.5F};
        return runtime;
    };

    BackroomsMarlowUpdateContext context {};
    context.player = marlow_player(30.5F, 30.5F);
    context.player.feet_position = {30.5F, 40.05F, 30.5F};
    context.player.eye_position = {30.5F, 42.01F, 30.5F};
    context.player.in_water = true;
    context.player.head_in_water = true;
    context.player.motion_is_forced = true;
    context.chunk_readiness = readiness;
    context.threat_slot_owned = true;

    auto exposed_head = make_runtime();
    const auto head_result = update_backrooms_marlow(
        state,
        *exposed_head,
        poolrooms,
        context,
        0.01F);
    CHECK_FALSE(head_result.kill_player);
    CHECK_FALSE(head_result.capture.active);
    CHECK(exposed_head->phase == BackroomsMarlowPhase::Submerging);
    CHECK(exposed_head->capture_target == glm::vec3 {0.0F});

    auto misplaced_feet = make_runtime();
    context.player.feet_position.y = 40.18F;
    context.player.eye_position.y = 41.80F;
    const auto feet_result = update_backrooms_marlow(
        state,
        *misplaced_feet,
        poolrooms,
        context,
        0.01F);
    CHECK_FALSE(feet_result.kill_player);
    CHECK(misplaced_feet->phase == BackroomsMarlowPhase::Submerging);

    auto dry_head = make_runtime();
    context.player.feet_position.y = 40.05F;
    context.player.head_in_water = false;
    const auto dry_head_result = update_backrooms_marlow(
        state,
        *dry_head,
        poolrooms,
        context,
        0.01F);
    CHECK_FALSE(dry_head_result.kill_player);
    CHECK(dry_head->phase == BackroomsMarlowPhase::Submerging);

    const auto water_surface_y =
        grid.cells[grid_index(grid, 30, 30)].water_surface_y;
    auto insufficient_head_margin = make_runtime();
    context.player.head_in_water = true;
    context.player.eye_position.y = water_surface_y - 0.049F;
    const auto insufficient_margin_result = update_backrooms_marlow(
        state,
        *insufficient_head_margin,
        poolrooms,
        context,
        0.01F);
    CHECK_FALSE(insufficient_margin_result.kill_player);
    CHECK(insufficient_head_margin->phase ==
          BackroomsMarlowPhase::Submerging);

    auto exact_head_margin = make_runtime();
    context.player.eye_position.y = water_surface_y - 0.05F;
    const auto exact_margin_result = update_backrooms_marlow(
        state,
        *exact_head_margin,
        poolrooms,
        context,
        0.01F);
    CHECK(exact_margin_result.kill_player);
    CHECK(exact_head_margin->phase == BackroomsMarlowPhase::Screamer);
}

TEST_CASE("Une manifestation agressive poursuit puis saisit le joueur") {
    const BackroomsGenerator poolrooms {
        7331,
        -2,
        kBackroomsConnectorDistrictModules,
        BackroomsPoolGeometryProfile::FloodedDistrictsV4,
    };
    auto grid = handmade_grid();
    for (auto z = 20; z <= 30; ++z) {
        open_marlow_space_at(
            grid,
            30,
            z,
            z == 30,
            true,
            z == 30 ? 2.0F : 0.0F);
    }
    const auto readiness = full_readiness(grid.center_chunk);
    auto state = initialize_backrooms_marlow(0x50555253U, -2);
    state.cue_seconds = 20.0F;
    state.manifestation_seconds = 20.0F;

    auto runtime = std::make_unique<BackroomsMarlowRuntime>();
    runtime->navigation = grid;
    runtime->navigation_readiness = readiness;
    runtime->navigation_valid = true;
    runtime->navigation_readiness_valid = true;
    runtime->phase = BackroomsMarlowPhase::Blocking;
    runtime->phase_duration_seconds = 12.0F;
    runtime->position = {30.5F, 40.001F, 20.5F};
    runtime->buoy_position = {30.5F, 41.05F, 30.5F};
    runtime->buoy_warning_active = true;
    runtime->pending_manifestation.mode =
        BackroomsMarlowEncounterMode::Blocking;
    runtime->grace_seconds = 0.0F;

    BackroomsMarlowUpdateContext context {};
    context.player = marlow_player(30.5F, 30.5F);
    context.player.in_water = true;
    context.chunk_readiness = readiness;
    context.threat_slot_available = false;
    context.threat_slot_owned = true;

    auto captured = false;
    for (auto step = 0; step < 80 && !captured; ++step) {
        const auto result = update_backrooms_marlow(
            state,
            *runtime,
            poolrooms,
            context,
            0.10F);
        captured = result.capture_started;
    }

    CHECK(runtime->position.z > 24.0F);
    CHECK(captured);
    CHECK(runtime->phase == BackroomsMarlowPhase::Dragging);
}

TEST_CASE("L hysteresis chargee se derive de la pression durable") {
    const BackroomsGenerator poolrooms {
        7331,
        -2,
        kBackroomsConnectorDistrictModules,
        BackroomsPoolGeometryProfile::FloodedDistrictsV4,
    };
    auto state = initialize_backrooms_marlow(0x4C4F4144U, -2);
    state.pressure = 0.80F;
    state.manifestation_seconds = 30.0F;
    auto runtime = std::make_unique<BackroomsMarlowRuntime>();
    runtime->navigation = handmade_grid();
    runtime->navigation_readiness =
        full_readiness(runtime->navigation.center_chunk);
    runtime->navigation_valid = true;
    runtime->navigation_readiness_valid = true;

    BackroomsMarlowUpdateContext context {};
    context.player = marlow_player(30.5F, 30.5F);
    context.chunk_readiness = runtime->navigation_readiness;
    context.simulation_frozen = true;
    static_cast<void>(update_backrooms_marlow(
        state,
        *runtime,
        poolrooms,
        context,
        0.0F));

    CHECK(runtime->pressure_hysteresis_initialized);
    CHECK_FALSE(runtime->pressure_attack_armed);
    CHECK(state.manifestation_seconds == doctest::Approx(30.0F));
}

TEST_CASE("Un pic de pression programme une attaque avec hysteresis") {
    const BackroomsGenerator poolrooms {
        7331,
        -2,
        kBackroomsConnectorDistrictModules,
        BackroomsPoolGeometryProfile::FloodedDistrictsV4,
    };
    auto grid = handmade_grid();
    for (auto z = 20; z <= 40; ++z) {
        open_cell(grid, 30, z, true, true);
    }
    const auto readiness = full_readiness(grid.center_chunk);
    auto state = initialize_backrooms_marlow(0x50524553U, -2);
    state.pressure = 0.54F;
    state.manifestation_seconds = 30.0F;
    state.cue_seconds = 20.0F;

    auto runtime = std::make_unique<BackroomsMarlowRuntime>();
    runtime->navigation = grid;
    runtime->navigation_readiness = readiness;
    runtime->navigation_valid = true;
    runtime->navigation_readiness_valid = true;
    runtime->grace_seconds = 10.0F;

    BackroomsMarlowUpdateContext context {};
    context.player = marlow_player(30.5F, 30.5F);
    context.player.entered_water = true;
    context.chunk_readiness = readiness;

    static_cast<void>(update_backrooms_marlow(
        state,
        *runtime,
        poolrooms,
        context,
        0.10F));
    CHECK(state.manifestation_seconds >= 3.0F);
    CHECK(state.manifestation_seconds <= 6.0F);
    CHECK_FALSE(runtime->pressure_attack_armed);

    context.player.entered_water = false;
    const auto scheduled = state.manifestation_seconds;
    static_cast<void>(update_backrooms_marlow(
        state,
        *runtime,
        poolrooms,
        context,
        0.10F));
    CHECK(state.manifestation_seconds ==
          doctest::Approx(scheduled - 0.10F));

    state.pressure = 0.20F;
    static_cast<void>(update_backrooms_marlow(
        state,
        *runtime,
        poolrooms,
        context,
        0.10F));
    CHECK(runtime->pressure_attack_armed);
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
        open_marlow_space_at(grid, 30, z, z >= 42, true);
    }
    for (auto z = 40; z <= 45; ++z) {
        grid.cells[grid_index(grid, 29, z)].walkable = false;
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
