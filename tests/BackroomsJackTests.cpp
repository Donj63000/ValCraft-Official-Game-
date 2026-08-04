#include "gameplay/BackroomsJack.h"
#include "world/BackroomsSpatialStack.h"
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
                            jack + glm::vec3 {0.0F, 4.08F, 0.0F};
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
                        jack + glm::vec3 {0.0F, 4.08F, 0.0F};
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
    readiness.mesh_revisions.fill(ready ? 1U : 0U);
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

TEST_CASE("la sauvegarde conclut proprement une apparition psychologique") {
    auto state = initialize_backrooms_jack(7331U);
    state.phase = BackroomsJackPhase::Watching;
    state.active = true;
    state.phase_seconds = 1.5F;
    state.suspicion = 2.0F;
    state.motion_amount = 0.4F;

    BackroomsJackRuntime corridor_runtime {};
    corridor_runtime.encounter_mode =
        BackroomsJackEncounterMode::CorridorStare;
    const auto corridor_persistent =
        prepare_backrooms_jack_for_persistence(
            state,
            corridor_runtime);
    CHECK(corridor_persistent.phase == BackroomsJackPhase::Cooldown);
    CHECK_FALSE(corridor_persistent.active);
    CHECK(corridor_persistent.cooldown_seconds >=
          kBackroomsJackPsychologicalCooldownMinimumSeconds);
    CHECK(corridor_persistent.cooldown_seconds <=
          kBackroomsJackPsychologicalCooldownMaximumSeconds);
    CHECK_FALSE(corridor_persistent.chase_event_emitted);
    CHECK(corridor_persistent.phase_seconds == doctest::Approx(0.0F));
    CHECK(corridor_persistent.suspicion == doctest::Approx(0.0F));
    CHECK(corridor_persistent.motion_amount == doctest::Approx(0.0F));

    BackroomsJackRuntime rear_runtime {};
    rear_runtime.encounter_mode = BackroomsJackEncounterMode::RearStare;
    const auto rear_persistent = prepare_backrooms_jack_for_persistence(
        state,
        rear_runtime);
    CHECK(rear_persistent.phase == BackroomsJackPhase::Cooldown);
    CHECK_FALSE(rear_persistent.active);

    const BackroomsJackRuntime ordinary_runtime {};
    const auto ordinary_persistent =
        prepare_backrooms_jack_for_persistence(
            state,
            ordinary_runtime);
    CHECK(ordinary_persistent.phase == BackroomsJackPhase::Watching);
    CHECK(ordinary_persistent.active);
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

TEST_CASE("la premiere apparition conserve une courte grace avant la tension") {
    for (std::uint32_t seed = 1U; seed <= 128U; ++seed) {
        const auto state = initialize_backrooms_jack(seed);
        CHECK(state.phase == BackroomsJackPhase::Dormant);
        CHECK_FALSE(state.active);
        CHECK(state.spawn_check_seconds >=
              kBackroomsJackInitialSpawnDelayMinimumSeconds);
        CHECK(state.spawn_check_seconds <=
              kBackroomsJackInitialSpawnDelayMaximumSeconds);
        CHECK(state.random_state != 0U);
    }
}

TEST_CASE("le plafond visuel respecte le verrou de nouvelle tentative") {
    const BackroomsGenerator generator {7331, -2};
    const auto spawn = generator.spawn_block();
    const auto player_position = cell_position(
        generator,
        spawn.x,
        spawn.z);

    auto state = initialize_backrooms_jack(0x52455452U, -2);
    state.spawn_check_seconds = kBackroomsJackSpawnRetryMaximumSeconds;
    BackroomsJackRuntime runtime {};
    runtime.director_initialized = true;
    runtime.visual_deadline_seconds = 0.0F;
    runtime.distant_cue_seconds = kBackroomsJackCueDeadlineSeconds;
    runtime.spawn_retry_pending = true;

    BackroomsJackUpdateContext context {};
    context.player = test_player(
        player_position,
        player_position + glm::vec3 {0.0F, 0.0F, -1.0F});
    context.chunk_readiness = ready_neighborhood(
        backrooms_jack_chunk_at(player_position));
    // Je rends volontairement toute apparition impossible : si le selecteur
    // etait relance a 120 Hz, son echec rearmerait sans cesse le compteur a 1 s.
    context.maximum_visible_distance = 1.0F;

    for (auto quarter = 0; quarter < 3; ++quarter) {
        static_cast<void>(update_backrooms_jack(
            state,
            runtime,
            generator,
            context,
            0.25F));
    }
    CHECK(runtime.spawn_retry_pending);
    CHECK(state.spawn_check_seconds == doctest::Approx(0.25F).epsilon(0.08));

    static_cast<void>(update_backrooms_jack(
        state,
        runtime,
        generator,
        context,
        0.25F));
    // Je laisse deux pas fixes absorber sans ambiguite l'arrondi du compteur.
    static_cast<void>(update_backrooms_jack(
        state,
        runtime,
        generator,
        context,
        1.0F / 60.0F));
    CHECK(runtime.spawn_retry_pending);
    CHECK(state.spawn_check_seconds == doctest::Approx(1.0F).epsilon(0.08));
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

TEST_CASE("Jack superpose un obstacle World au bon Y des Poolrooms V3") {
    constexpr auto seed = 7331;
    constexpr auto logical_level = -2;
    const BackroomsGenerator generator {
        seed,
        logical_level,
        kBackroomsSpatialConnectorDistrictModules,
        BackroomsPoolGeometryProfile::RecessedOneBlock,
    };
    const BackroomsSpatialStack stack {
        seed,
        logical_level,
        BackroomsSpatialProfile::RecessedPoolroomsV3,
    };
    const auto placement = stack.placement_for_level(logical_level);
    REQUIRE(placement.has_value());
    const auto world_y_offset =
        placement->floor_y - kBackroomsFloorY;
    REQUIRE(world_y_offset == 1);

    World world {
        seed,
        0,
        WorldGenerationProfile::Backrooms,
        WorldGenerationVersion::BackroomsV3,
        VisualPipeline::ModernStylized,
        logical_level,
    };
    const auto spawn = generator.spawn_block();
    const auto local_player =
        cell_position(generator, spawn.x, spawn.z);
    auto state = initialize_backrooms_jack(7331U, logical_level);
    BackroomsJackUpdateContext context {};
    context.player = test_player(
        local_player,
        local_player + glm::vec3 {0.0F, 0.0F, -1.0F});
    context.chunk_readiness = ready_neighborhood(
        backrooms_jack_chunk_at(local_player));
    context.allow_spawn = false;
    context.simulation_frozen = true;
    context.spatial_world = &world;
    context.spatial_world_y_offset = world_y_offset;

    BackroomsJackRuntime clear_runtime {};
    static_cast<void>(update_backrooms_jack(
        state,
        clear_runtime,
        generator,
        context,
        1.0F / 60.0F));
    REQUIRE(clear_runtime.navigation_valid);

    auto obstacle_point = BackroomsJackGridPoint {};
    auto local_body_y = 0;
    auto found_cell = false;
    for (std::size_t index = 0U;
         index < clear_runtime.navigation.cells.size() && !found_cell;
         ++index) {
        const auto& cell = clear_runtime.navigation.cells[index];
        if (!cell.walkable) {
            continue;
        }
        const auto local_z = static_cast<int>(
            index / kBackroomsJackNavigationSide);
        const auto local_x = static_cast<int>(
            index % kBackroomsJackNavigationSide);
        const BackroomsJackGridPoint point {
            clear_runtime.navigation.origin_world_x + local_x,
            clear_runtime.navigation.origin_world_z + local_z,
        };
        const auto candidate_body_y =
            static_cast<int>(std::floor(cell.floor_y));
        if (is_block_collidable(generator.sample_block(
                point.x,
                candidate_body_y,
                point.z)) ||
            is_block_collidable(world.peek_block_or_generated(
                point.x,
                candidate_body_y + world_y_offset,
                point.z))) {
            continue;
        }
        obstacle_point = point;
        local_body_y = candidate_body_y;
        found_cell = true;
    }
    REQUIRE(found_cell);
    const auto* clear_cell = backrooms_jack_navigation_cell(
        clear_runtime.navigation,
        obstacle_point.x,
        obstacle_point.z);
    REQUIRE(clear_cell != nullptr);
    REQUIRE(clear_cell->walkable);

    world.set_block(
        obstacle_point.x,
        local_body_y + world_y_offset,
        obstacle_point.z,
        to_block_id(BlockType::Stone));
    CHECK_FALSE(is_block_collidable(generator.sample_block(
        obstacle_point.x,
        local_body_y,
        obstacle_point.z)));
    CHECK(world.peek_block_or_generated(
              obstacle_point.x,
              local_body_y + world_y_offset,
              obstacle_point.z) ==
          to_block_id(BlockType::Stone));

    BackroomsJackRuntime blocked_runtime {};
    static_cast<void>(update_backrooms_jack(
        state,
        blocked_runtime,
        generator,
        context,
        1.0F / 60.0F));
    const auto* blocked_cell = backrooms_jack_navigation_cell(
        blocked_runtime.navigation,
        obstacle_point.x,
        obstacle_point.z);
    REQUIRE(blocked_cell != nullptr);
    CHECK_FALSE(blocked_cell->walkable);
    CHECK_FALSE(blocked_cell->standing_allowed);
}

TEST_CASE("Jack voit uniquement devant lui avec une ligne de vue libre") {
    const BackroomsGenerator generator {1337};
    const auto pair =
        find_visible_pair(generator, 5.0F, 10.0F);
    REQUIRE(pair.found);
    const auto player = test_player(
        pair.player,
        pair.jack + glm::vec3 {0.0F, 4.08F, 0.0F});
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

TEST_CASE("Jack detecte le joueur au loin sans depasser la brume visible") {
    const BackroomsGenerator generator {7331};
    const auto long_range_pair =
        find_visible_pair(generator, 50.0F, 60.0F);
    const auto beyond_fog_pair =
        find_visible_pair(generator, 65.0F, 72.0F);
    REQUIRE(long_range_pair.found);
    REQUIRE(beyond_fog_pair.found);

    const auto long_range = evaluate_backrooms_jack_perception(
        generator,
        test_player(long_range_pair.player, long_range_pair.jack),
        long_range_pair.jack,
        test_yaw_toward(long_range_pair.jack, long_range_pair.player),
        0.0F);
    const auto beyond_fog = evaluate_backrooms_jack_perception(
        generator,
        test_player(beyond_fog_pair.player, beyond_fog_pair.jack),
        beyond_fog_pair.jack,
        test_yaw_toward(beyond_fog_pair.jack, beyond_fog_pair.player),
        0.0F);

    CHECK(long_range.line_of_sight);
    CHECK(long_range.distance >= 50.0F);
    CHECK(long_range.distance <= 60.0F);
    CHECK(long_range.jack_sees_player);
    // Je traite la brume engagee comme la limite perceptive effective : je
    // ne lance meme pas de rayon supercover dans une zone deja invisible.
    CHECK_FALSE(beyond_fog.line_of_sight);
    CHECK(beyond_fog.distance > 64.0F);
    CHECK_FALSE(beyond_fog.jack_sees_player);
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
                pair.jack + glm::vec3 {0.0F, 4.08F, 0.0F});
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
    CHECK(near_state.suspicion > far_state.suspicion * 2.0F);
    CHECK(far_state.suspicion ==
          doctest::Approx(3.0F / 4.6F).epsilon(0.02));
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

TEST_CASE("la ligne de vue de Jack refuse une origine muree et une traversee hors borne") {
    const BackroomsGenerator generator {7331};
    const auto spawn = generator.spawn_block();
    const auto column = generator.sample_column(spawn.x, spawn.z);
    const glm::vec3 buried_origin {
        static_cast<float>(spawn.x) + 0.5F,
        static_cast<float>(column.floor_y) + 0.5F,
        static_cast<float>(spawn.z) + 0.5F,
    };
    REQUIRE(is_block_opaque(generator.sample_block(
        spawn.x,
        column.floor_y,
        spawn.z)));
    CHECK_FALSE(backrooms_jack_has_line_of_sight(
        generator,
        buried_origin,
        buried_origin + glm::vec3 {0.0F, 3.0F, 0.0F}));

    const glm::vec3 clear_but_excessive_origin {
        static_cast<float>(spawn.x) + 0.5F,
        512.5F,
        static_cast<float>(spawn.z) + 0.5F,
    };
    REQUIRE_FALSE(is_block_opaque(generator.sample_block(
        spawn.x,
        512,
        spawn.z)));
    CHECK_FALSE(backrooms_jack_has_line_of_sight(
        generator,
        clear_but_excessive_origin,
        clear_but_excessive_origin + glm::vec3 {0.0F, 5000.0F, 0.0F}));
}

TEST_CASE("la grille de Jack refuse les chunks dont le domaine deborde") {
    const BackroomsGenerator generator(7331);
    constexpr std::array<ChunkCoord, 2U> extreme_centers {{
        {std::numeric_limits<int>::lowest(),
         std::numeric_limits<int>::lowest()},
        {std::numeric_limits<int>::max(),
         std::numeric_limits<int>::max()},
    }};

    for (const auto center : extreme_centers) {
        CAPTURE(center.x);
        CAPTURE(center.z);
        const auto grid =
            build_backrooms_jack_navigation_grid(generator, center);
        CHECK(grid.center_chunk == center);
        CHECK(std::none_of(
            grid.cells.begin(),
            grid.cells.end(),
            [](const BackroomsJackNavigationCell& cell) {
                return cell.walkable;
            }));
        CHECK(backrooms_jack_navigation_cell(
                  grid,
                  center.x,
                  center.z) == nullptr);
    }
}

TEST_CASE("les APIs de navigation de Jack refusent une grille au domaine forge") {
    BackroomsJackNavigationGrid grid {};
    grid.origin_world_x = std::numeric_limits<int>::max();
    grid.origin_world_z = std::numeric_limits<int>::max();
    grid.cells.front().walkable = true;

    const BackroomsJackGridPoint origin {
        grid.origin_world_x,
        grid.origin_world_z,
    };
    CHECK(backrooms_jack_navigation_cell(
              grid,
              origin.x,
              origin.z) == nullptr);
    const auto path = find_backrooms_jack_path(grid, origin, origin);
    CHECK(path.count == 0U);
    CHECK(path.empty());
}

TEST_CASE("le spawn psychologique reste accessible pret et deterministe") {
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
    CHECK(first.encounter_mode == repeated.encounter_mode);
    CHECK(first.next_random_state ==
          repeated.next_random_state);

    const auto distance =
        glm::length(
            glm::vec2 {
                first.position.x - player_position.x,
                first.position.z - player_position.z,
            });
    CHECK(distance >=
          (first.encounter_mode == BackroomsJackEncounterMode::HiddenHunt
               ? 12.0F
               : kBackroomsJackMinimumSpawnDistance));
    CHECK(distance <= kBackroomsJackMaximumSpawnDistance);
    const auto candidate_chunk =
        backrooms_jack_chunk_at(first.position);
    CHECK(std::abs(candidate_chunk.x - center.x) <=
          kBackroomsJackReadinessChunkRadius);
    CHECK(std::abs(candidate_chunk.z - center.z) <=
          kBackroomsJackReadinessChunkRadius);
    const auto cell = backrooms_jack_navigation_cell(
        grid,
        static_cast<int>(std::floor(first.position.x)),
        static_cast<int>(std::floor(first.position.z)));
    if (first.route_guaranteed) {
        REQUIRE(cell != nullptr);
        CHECK(cell->walkable);
    }
    const auto perception = evaluate_backrooms_jack_perception(
        generator,
        player,
        first.position,
        first.body_yaw_degrees,
        first.initial_hunch);
    CHECK(perception.jack_front_dot > 0.94F);
    switch (first.encounter_mode) {
    case BackroomsJackEncounterMode::HiddenHunt:
        CHECK_FALSE(perception.player_sees_jack);
        break;
    case BackroomsJackEncounterMode::CorridorStare:
        CHECK(distance >= 32.0F);
        CHECK(distance <= 52.0F);
        CHECK(perception.player_faces_jack);
        break;
    case BackroomsJackEncounterMode::RearStare:
        CHECK(distance >= 18.0F);
        CHECK(distance <= 30.0F);
        CHECK(perception.line_of_sight);
        CHECK(perception.player_front_dot < -0.65F);
        CHECK_FALSE(perception.player_sees_jack);
        break;
    }

    const auto unavailable = select_backrooms_jack_spawn(
        generator,
        grid,
        player,
        ready_neighborhood(center, false),
        0x12345678U);
    CHECK_FALSE(unavailable.found);
    CHECK(unavailable.next_random_state == 0x12345678U);

    auto compact_readiness = ready_neighborhood(center, false);
    for (auto delta_z = -1; delta_z <= 1; ++delta_z) {
        for (auto delta_x = -1; delta_x <= 1; ++delta_x) {
            const auto index = static_cast<std::size_t>(
                (delta_z + kBackroomsJackReadinessChunkRadius) *
                    kBackroomsJackReadinessChunkSide +
                delta_x + kBackroomsJackReadinessChunkRadius);
            compact_readiness.ready[index] = true;
            compact_readiness.mesh_revisions[index] = 1U;
        }
    }
    const auto compact_stream_selection = select_backrooms_jack_spawn(
        generator,
        grid,
        player,
        compact_readiness,
        0x12345678U);
    REQUIRE(compact_stream_selection.found);
    const auto compact_chunk =
        backrooms_jack_chunk_at(compact_stream_selection.position);
    CHECK(std::abs(compact_chunk.x - center.x) <= 1);
    CHECK(std::abs(compact_chunk.z - center.z) <= 1);

    auto masked_state = initialize_backrooms_jack(7331U, 0);
    BackroomsJackRuntime masked_runtime {};
    BackroomsJackUpdateContext masked_context {};
    masked_context.player = player;
    masked_context.chunk_readiness = compact_readiness;
    masked_context.allow_spawn = false;
    masked_context.simulation_frozen = true;
    static_cast<void>(update_backrooms_jack(
        masked_state,
        masked_runtime,
        generator,
        masked_context,
        1.0F / 60.0F));
    REQUIRE(masked_runtime.navigation_valid);

    auto masked_cell_count = std::size_t {0U};
    for (auto local_z = 0;
         local_z < kBackroomsJackNavigationSide;
         ++local_z) {
        for (auto local_x = 0;
             local_x < kBackroomsJackNavigationSide;
             ++local_x) {
            const auto world_x =
                masked_runtime.navigation.origin_world_x + local_x;
            const auto world_z =
                masked_runtime.navigation.origin_world_z + local_z;
            const auto chunk = backrooms_jack_chunk_at(glm::vec3 {
                static_cast<float>(world_x) + 0.5F,
                0.0F,
                static_cast<float>(world_z) + 0.5F,
            });
            if (std::abs(chunk.x - center.x) <= 1 &&
                std::abs(chunk.z - center.z) <= 1) {
                continue;
            }
            ++masked_cell_count;
            CHECK_FALSE(
                masked_runtime.navigation.cells[
                    static_cast<std::size_t>(
                        local_z * kBackroomsJackNavigationSide +
                        local_x)]
                    .walkable);
        }
    }
    CHECK(masked_cell_count > 0U);

    const auto compact_path = find_backrooms_jack_path(
        masked_runtime.navigation,
        {
            static_cast<int>(std::floor(player_position.x)),
            static_cast<int>(std::floor(player_position.z)),
        },
        {
            static_cast<int>(
                std::floor(compact_stream_selection.position.x)),
            static_cast<int>(
                std::floor(compact_stream_selection.position.z)),
        });
    CHECK(compact_stream_selection.route_guaranteed ==
          !compact_path.empty());
    if (compact_path.empty()) {
        return;
    }
    for (auto index = std::size_t {0U};
         index < compact_path.count;
         ++index) {
        const auto node_chunk = backrooms_jack_chunk_at(glm::vec3 {
            static_cast<float>(compact_path.nodes[index].x) + 0.5F,
            0.0F,
            static_cast<float>(compact_path.nodes[index].z) + 0.5F,
        });
        CHECK(std::abs(node_chunk.x - center.x) <= 1);
        CHECK(std::abs(node_chunk.z - center.z) <= 1);
    }
}

TEST_CASE("le selecteur produit les trois mises en scene de Jack") {
    const BackroomsGenerator generator {7331};
    const auto corridor_pair =
        find_visible_pair(generator, 32.0F, 36.0F);
    const auto rear_pair =
        find_visible_pair(generator, 18.0F, 28.0F);
    REQUIRE(corridor_pair.found);
    REQUIRE(rear_pair.found);
    const auto find_mode =
        [&](const BackroomsJackPlayerContext& player,
            BackroomsJackEncounterMode expected) {
            const auto center =
                backrooms_jack_chunk_at(player.feet_position);
            const auto grid = build_backrooms_jack_navigation_grid(
                generator,
                center);
            const auto readiness = ready_neighborhood(center);
            auto selected = std::optional<BackroomsJackSpawnSelection> {};
            for (auto seed = std::uint32_t {1U};
                 seed <= 512U;
                 ++seed) {
                const auto candidate = select_backrooms_jack_spawn(
                    generator,
                    grid,
                    player,
                    readiness,
                    seed);
                if (candidate.found &&
                    candidate.encounter_mode == expected) {
                    selected = candidate;
                    break;
                }
            }
            return selected;
        };
    const auto corridor = find_mode(
        test_player(corridor_pair.player, corridor_pair.jack),
        BackroomsJackEncounterMode::CorridorStare);
    REQUIRE(corridor.has_value());
    const auto rear = find_mode(
        test_player(
            rear_pair.player,
            rear_pair.player + (rear_pair.player - rear_pair.jack)),
        BackroomsJackEncounterMode::RearStare);
    const auto spawn = generator.spawn_block();
    const glm::vec3 hidden_player_position {
        static_cast<float>(spawn.x) + 0.5F,
        static_cast<float>(spawn.y),
        static_cast<float>(spawn.z) + 0.5F,
    };
    const auto hidden = find_mode(
        test_player(
            hidden_player_position,
            hidden_player_position + glm::vec3 {40.0F, 0.0F, 0.0F}),
        BackroomsJackEncounterMode::HiddenHunt);

    REQUIRE(rear.has_value());
    REQUIRE(hidden.has_value());
    CHECK(corridor->encounter_mode ==
          BackroomsJackEncounterMode::CorridorStare);
    CHECK(rear->encounter_mode ==
          BackroomsJackEncounterMode::RearStare);
    CHECK(hidden->encounter_mode ==
          BackroomsJackEncounterMode::HiddenHunt);
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

    const auto outside_start = find_backrooms_jack_path(
        grid,
        {-1, 0},
        {4, 0});
    CHECK(outside_start.empty());

    BackroomsJackNavigationGrid projected_goal_grid {};
    projected_goal_grid.origin_world_x = 0;
    projected_goal_grid.origin_world_z = 0;
    const auto set_projected_walkable =
        [&](int x, int z) {
            auto& cell = projected_goal_grid.cells[
                static_cast<std::size_t>(
                    z * kBackroomsJackNavigationSide + x)];
            cell.walkable = true;
            cell.standing_allowed = true;
            cell.clearance = 6.0F;
        };
    set_projected_walkable(1, 4);
    set_projected_walkable(1, 3);
    set_projected_walkable(1, 1);
    const auto projected_path = find_backrooms_jack_path(
        projected_goal_grid,
        {1, 4},
        {1, 2});
    REQUIRE_FALSE(projected_path.empty());
    REQUIRE(projected_path.count >= 2U);
    CHECK(projected_path.nodes[projected_path.count - 1U] ==
          (BackroomsJackGridPoint {1, 3}));
}

TEST_CASE("le watchdog libere Jack lorsqu il demarre dans un obstacle") {
    const BackroomsGenerator generator {7331};
    const auto spawn = generator.spawn_block();
    VisiblePair trapped_pair {};
    constexpr std::array<std::array<int, 2>, 4> directions {{
        {{1, 0}},
        {{-1, 0}},
        {{0, 1}},
        {{0, -1}},
    }};
    for (auto offset_z = -48;
         offset_z <= 48 && !trapped_pair.found;
         ++offset_z) {
        for (auto offset_x = -48;
             offset_x <= 48 && !trapped_pair.found;
             ++offset_x) {
            const auto wall_x = spawn.x + offset_x;
            const auto wall_z = spawn.z + offset_z;
            if (!generator.sample_column(wall_x, wall_z).wall) {
                continue;
            }
            for (const auto& direction : directions) {
                for (auto distance = 6; distance <= 10; ++distance) {
                    const auto player_x =
                        wall_x + direction[0] * distance;
                    const auto player_z =
                        wall_z + direction[1] * distance;
                    if (!generator.is_walkable(player_x, player_z)) {
                        continue;
                    }
                    trapped_pair = {
                        cell_position(generator, wall_x, wall_z),
                        cell_position(generator, player_x, player_z),
                        true,
                    };
                    break;
                }
                if (trapped_pair.found) {
                    break;
                }
            }
        }
    }
    REQUIRE(trapped_pair.found);

    BackroomsJackState state {};
    state.phase = BackroomsJackPhase::Chasing;
    state.active = true;
    state.position = trapped_pair.jack;
    state.last_seen_player_position = trapped_pair.player;
    state.previous_player_position = trapped_pair.player;
    state.chase_event_emitted = true;
    BackroomsJackRuntime runtime {};
    BackroomsJackUpdateContext context {};
    context.player = test_player(
        trapped_pair.player,
        trapped_pair.player +
            (trapped_pair.player - trapped_pair.jack));
    context.chunk_readiness = ready_neighborhood(
        backrooms_jack_chunk_at(trapped_pair.player));
    context.allow_spawn = false;

    const auto initial_position = state.position;
    auto vanished_count = std::size_t {0U};
    for (auto frame = 0; frame < 360; ++frame) {
        const auto result = update_backrooms_jack(
            state,
            runtime,
            generator,
            context,
            1.0F / 60.0F);
        vanished_count += count_event(
            result,
            BackroomsJackEventKind::Vanished);
        if (state.phase == BackroomsJackPhase::Cooldown ||
            state.phase == BackroomsJackPhase::Jumpscare) {
            break;
        }
    }

    const auto escaped_obstacle =
        test_horizontal_distance(initial_position, state.position) > 0.50F;
    const auto recovered =
        escaped_obstacle ||
        state.phase == BackroomsJackPhase::Cooldown ||
        state.phase == BackroomsJackPhase::Jumpscare;
    const auto remains_blocked =
        state.phase == BackroomsJackPhase::Chasing &&
        test_horizontal_distance(initial_position, state.position) <= 0.50F;
    CHECK(recovered);
    CHECK_FALSE(remains_blocked);
    CHECK(vanished_count <= 1U);
}

TEST_CASE("le watchdog fait disparaitre Jack bloque meme sous le regard") {
    const BackroomsGenerator generator {1337};
    const auto pair = find_visible_pair(generator, 6.0F, 10.0F);
    REQUIRE(pair.found);

    BackroomsJackState state {};
    state.phase = BackroomsJackPhase::Chasing;
    state.active = true;
    state.position = pair.jack;
    state.last_seen_player_position = pair.player;
    state.previous_player_position = pair.player;
    state.body_yaw_degrees =
        test_yaw_toward(pair.jack, pair.player);
    state.chase_event_emitted = true;
    BackroomsJackUpdateContext context {};
    context.player = test_player(pair.player, pair.jack);
    context.chunk_readiness = ready_neighborhood(
        backrooms_jack_chunk_at(pair.player));
    context.allow_spawn = false;
    REQUIRE(
        evaluate_backrooms_jack_perception(
            generator,
            context.player,
            state.position,
            state.body_yaw_degrees,
            state.hunch_ratio)
            .player_sees_jack);

    const auto midpoint =
        (pair.jack + pair.player) * 0.5F;
    BackroomsJackRuntime runtime {};
    runtime.navigation = build_backrooms_jack_navigation_grid(
        generator,
        backrooms_jack_chunk_at(midpoint));
    for (auto& cell : runtime.navigation.cells) {
        cell.walkable = false;
        cell.standing_allowed = false;
    }
    runtime.navigation_valid = true;
    runtime.navigation_readiness = context.chunk_readiness;
    runtime.navigation_readiness_valid = true;

    auto vanished_count = std::size_t {0U};
    for (auto frame = 0;
         frame < 240 && state.phase != BackroomsJackPhase::Cooldown;
         ++frame) {
        const auto result = update_backrooms_jack(
            state,
            runtime,
            generator,
            context,
            1.0F / 60.0F);
        vanished_count += count_event(
            result,
            BackroomsJackEventKind::Vanished);
    }

    CHECK(state.phase == BackroomsJackPhase::Cooldown);
    CHECK_FALSE(state.active);
    CHECK(vanished_count == 1U);
}

TEST_CASE("le watchdog retire Jack d une poche sans destination d errance") {
    const BackroomsGenerator generator {4242};
    const auto pair = find_visible_pair(generator, 6.0F, 10.0F);
    REQUIRE(pair.found);
    const auto midpoint = (pair.jack + pair.player) * 0.5F;
    const auto center = backrooms_jack_chunk_at(midpoint);
    auto isolated_navigation =
        build_backrooms_jack_navigation_grid(generator, center);
    const auto jack_x = static_cast<int>(std::floor(pair.jack.x));
    const auto jack_z = static_cast<int>(std::floor(pair.jack.z));
    const auto local_x = jack_x - isolated_navigation.origin_world_x;
    const auto local_z = jack_z - isolated_navigation.origin_world_z;
    REQUIRE(local_x >= 0);
    REQUIRE(local_x < kBackroomsJackNavigationSide);
    REQUIRE(local_z >= 0);
    REQUIRE(local_z < kBackroomsJackNavigationSide);
    const auto isolated_index = static_cast<std::size_t>(
        local_z * kBackroomsJackNavigationSide + local_x);
    const auto isolated_cell =
        isolated_navigation.cells[isolated_index];
    REQUIRE(isolated_cell.walkable);
    for (auto& cell : isolated_navigation.cells) {
        cell.walkable = false;
        cell.standing_allowed = false;
    }
    isolated_navigation.cells[isolated_index] = isolated_cell;

    BackroomsJackState state {};
    state.phase = BackroomsJackPhase::Wandering;
    state.active = true;
    state.position = pair.jack;
    state.body_yaw_degrees =
        test_yaw_toward(pair.jack, pair.player) + 180.0F;
    state.last_seen_player_position = pair.player;
    BackroomsJackUpdateContext context {};
    context.player = test_player(pair.player, pair.jack);
    context.chunk_readiness = ready_neighborhood(
        backrooms_jack_chunk_at(pair.player));
    context.allow_spawn = false;
    BackroomsJackRuntime runtime {};
    runtime.navigation = isolated_navigation;
    runtime.navigation_readiness = context.chunk_readiness;
    runtime.navigation_valid = true;
    runtime.navigation_readiness_valid = true;

    auto vanished_count = std::size_t {0U};
    for (auto frame = 0;
         frame < 240 && state.phase != BackroomsJackPhase::Cooldown;
         ++frame) {
        const auto result = update_backrooms_jack(
            state,
            runtime,
            generator,
            context,
            1.0F / 60.0F);
        vanished_count += count_event(
            result,
            BackroomsJackEventKind::Vanished);
    }

    CHECK(state.phase == BackroomsJackPhase::Cooldown);
    CHECK_FALSE(state.active);
    CHECK(vanished_count == 1U);
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

TEST_CASE("l apparition au bout du couloir observe puis disparait sans bruit") {
    const BackroomsGenerator generator {7331};
    const auto pair = find_visible_pair(generator, 32.0F, 48.0F);
    REQUIRE(pair.found);

    BackroomsJackState state {};
    state.phase = BackroomsJackPhase::Watching;
    state.active = true;
    state.position = pair.jack;
    state.body_yaw_degrees =
        test_yaw_toward(pair.jack, pair.player);
    state.notice_event_emitted = true;
    BackroomsJackRuntime runtime {};
    runtime.encounter_mode =
        BackroomsJackEncounterMode::CorridorStare;
    runtime.encounter_limit_seconds = 0.35F;
    runtime.encounter_chases = false;
    BackroomsJackUpdateContext context {};
    context.player = test_player(pair.player, pair.jack);
    context.chunk_readiness = ready_neighborhood(
        backrooms_jack_chunk_at(pair.player));
    context.allow_spawn = false;

    const auto initial_position = state.position;
    auto vanished_count = std::size_t {0U};
    for (auto frame = 0; frame < 8; ++frame) {
        const auto result = update_backrooms_jack(
            state,
            runtime,
            generator,
            context,
            1.0F / 60.0F);
        vanished_count += count_event(
            result,
            BackroomsJackEventKind::Vanished);
    }
    CHECK(state.phase == BackroomsJackPhase::Watching);
    CHECK(state.position == initial_position);

    for (auto frame = 0;
         frame < 90 && state.phase != BackroomsJackPhase::Cooldown;
         ++frame) {
        const auto result = update_backrooms_jack(
            state,
            runtime,
            generator,
            context,
            1.0F / 60.0F);
        vanished_count += count_event(
            result,
            BackroomsJackEventKind::Vanished);
    }
    CHECK(state.phase == BackroomsJackPhase::Cooldown);
    CHECK_FALSE(state.active);
    CHECK(vanished_count == 1U);
}

TEST_CASE("une apparition observee quitte silencieusement un chunk non pret") {
    const BackroomsGenerator generator {7331};
    const auto pair = find_visible_pair(generator, 32.0F, 48.0F);
    REQUIRE(pair.found);
    const auto player_chunk =
        backrooms_jack_chunk_at(pair.player);
    const auto jack_chunk =
        backrooms_jack_chunk_at(pair.jack);
    REQUIRE_FALSE(jack_chunk == player_chunk);

    BackroomsJackState state {};
    state.phase = BackroomsJackPhase::Watching;
    state.active = true;
    state.position = pair.jack;
    state.body_yaw_degrees =
        test_yaw_toward(pair.jack, pair.player);
    state.notice_event_emitted = true;
    BackroomsJackRuntime runtime {};
    runtime.encounter_mode =
        BackroomsJackEncounterMode::CorridorStare;
    runtime.encounter_limit_seconds = 8.0F;
    BackroomsJackUpdateContext context {};
    context.player = test_player(pair.player, pair.jack);
    context.chunk_readiness =
        ready_neighborhood(player_chunk, false);
    const auto center_readiness_index = static_cast<std::size_t>(
        kBackroomsJackReadinessChunkRadius *
            kBackroomsJackReadinessChunkSide +
        kBackroomsJackReadinessChunkRadius);
    context.chunk_readiness.ready[center_readiness_index] = true;
    context.chunk_readiness.mesh_revisions[
        center_readiness_index] = 1U;
    context.allow_spawn = false;
    context.simulation_frozen = true;

    const auto result = update_backrooms_jack(
        state,
        runtime,
        generator,
        context,
        1.0F / 60.0F);

    CHECK(state.phase == BackroomsJackPhase::Cooldown);
    CHECK_FALSE(state.active);
    CHECK_FALSE(result.render.visible);
    CHECK(result.light_interference.active);
    CHECK(result.light_interference.mode ==
          BackroomsJackLightInterferenceMode::BlackoutPulse);
    CHECK(count_event(result, BackroomsJackEventKind::Vanished) == 0U);
}

TEST_CASE("l apparition derriere attend que le joueur se retourne") {
    const BackroomsGenerator generator {4242};
    const auto pair = find_visible_pair(generator, 18.0F, 28.0F);
    REQUIRE(pair.found);

    BackroomsJackState state {};
    state.phase = BackroomsJackPhase::Watching;
    state.active = true;
    state.position = pair.jack;
    state.body_yaw_degrees =
        test_yaw_toward(pair.jack, pair.player);
    state.notice_event_emitted = true;
    BackroomsJackRuntime runtime {};
    runtime.encounter_mode = BackroomsJackEncounterMode::RearStare;
    runtime.encounter_limit_seconds = 3.0F;
    runtime.encounter_chases = false;
    BackroomsJackUpdateContext context {};
    context.player = test_player(
        pair.player,
        pair.player + (pair.player - pair.jack));
    context.chunk_readiness = ready_neighborhood(
        backrooms_jack_chunk_at(pair.player));
    context.allow_spawn = false;

    const auto initial_position = state.position;
    for (auto frame = 0; frame < 30; ++frame) {
        static_cast<void>(update_backrooms_jack(
            state,
            runtime,
            generator,
            context,
            1.0F / 60.0F));
    }
    CHECK(state.phase == BackroomsJackPhase::Watching);
    CHECK(state.position == initial_position);
    CHECK(runtime.encounter_reaction_seconds == doctest::Approx(0.0F));

    context.player = test_player(pair.player, pair.jack);
    auto vanished_count = std::size_t {0U};
    for (auto frame = 0;
         frame < 90 && state.phase != BackroomsJackPhase::Cooldown;
         ++frame) {
        const auto result = update_backrooms_jack(
            state,
            runtime,
            generator,
            context,
            1.0F / 60.0F);
        vanished_count += count_event(
            result,
            BackroomsJackEventKind::Vanished);
    }
    CHECK(state.phase == BackroomsJackPhase::Cooldown);
    CHECK_FALSE(state.active);
    CHECK(vanished_count == 1U);
}

TEST_CASE("Jack peut etre seme seulement hors regard apres plusieurs chunks") {
    const BackroomsGenerator generator {7331};
    // Je construis directement une séparation visible de 24 m ou plus : ce
    // test porte sur l'évasion, pas sur la distribution du sélecteur de scènes.
    const auto pair = find_visible_pair(generator, 24.0F, 32.0F);
    REQUIRE(pair.found);
    const auto player_position = pair.player;
    const auto player = test_player(
        player_position,
        player_position + (player_position - pair.jack));
    const auto center =
        backrooms_jack_chunk_at(player_position);

    BackroomsJackState state {};
    state.phase = BackroomsJackPhase::Searching;
    state.active = true;
    state.position = pair.jack;
    state.body_yaw_degrees =
        test_yaw_toward(pair.jack, player_position) +
        180.0F;
    state.last_seen_player_position = pair.jack;
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
          kBackroomsJackMinimumCooldownSeconds - 1.0F / 60.0F);
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
