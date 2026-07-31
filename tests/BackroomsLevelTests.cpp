#include "app/GameOptions.h"
#include "render/BackroomsFlicker.h"
#include "world/BackroomsGenerator.h"
#include "world/World.h"

#include <doctest/doctest.h>

#include <stdexcept>
#include <string_view>
#include <vector>

namespace valcraft {

TEST_CASE("Backrooms smoke accepts a bounded logical level") {
    const auto parsed =
        parse_game_options(
            std::vector<std::string_view> {
                "--smoke-session=backrooms",
                "--smoke-backrooms-level=-2",
            });
    REQUIRE(parsed.ok);
    CHECK(
        parsed.options.smoke_session ==
        SmokeSessionMode::Backrooms);
    CHECK(parsed.options.smoke_backrooms_level == -2);

    const auto invalid =
        parse_game_options(
            std::vector<std::string_view> {
                "--smoke-backrooms-level=-1000001",
            });
    CHECK_FALSE(invalid.ok);
    CHECK(
        invalid.error_message ==
        "Invalid value for --smoke-backrooms-level");
}

TEST_CASE("World preserves the Backrooms level in its procedural identity") {
    World poolrooms {
        7331,
        0,
        WorldGenerationProfile::Backrooms,
        WorldGenerationVersion::BackroomsV1,
        VisualPipeline::ModernStylized,
        -2,
    };

    CHECK(poolrooms.backrooms_level() == -2);
    const auto save_plan =
        poolrooms.capture_save_plan();
    CHECK(save_plan.backrooms_level == -2);

    World wrong_floor {
        7331,
        0,
        WorldGenerationProfile::Backrooms,
        WorldGenerationVersion::BackroomsV1,
        VisualPipeline::ModernStylized,
        -1,
    };
    CHECK_THROWS_AS(
        wrong_floor.begin_restore_save_plan(
            save_plan),
        std::invalid_argument);
}

TEST_CASE("Flicker timing is deterministic but isolated between levels") {
    const auto office =
        backrooms_flicker_schedule(
            9127,
            16,
            -33,
            0);
    const auto poolrooms =
        backrooms_flicker_schedule(
            9127,
            16,
            -33,
            -2);

    CHECK(
        backrooms_flicker_schedule(
            9127,
            16,
            -33,
            -2) ==
        poolrooms);
    CHECK_FALSE(office == poolrooms);
}

TEST_CASE("Poolrooms procedural water stays shallow and static after chunk seams load") {
    constexpr auto seed = 7331;
    constexpr auto level = -2;
    const BackroomsGenerator generator(seed, level);
    auto wet_cell = BlockCoord {};
    auto found_wet_cell = false;
    for (int z = 0; z < kBackroomsModuleSize && !found_wet_cell; ++z) {
        for (int x = 0; x < kBackroomsModuleSize; ++x) {
            if (generator.sample_water_state(
                    x,
                    kBackroomsFloorY + 1,
                    z) == 0U) {
                continue;
            }
            wet_cell = {x, kBackroomsFloorY + 1, z};
            found_wet_cell = true;
            break;
        }
    }
    REQUIRE(found_wet_cell);

    World poolrooms {
        seed,
        0,
        WorldGenerationProfile::Backrooms,
        WorldGenerationVersion::BackroomsV1,
        VisualPipeline::ModernStylized,
        level,
    };
    const auto wet_chunk =
        poolrooms.world_to_chunk(
            wet_cell.x,
            wet_cell.z);
    poolrooms.ensure_chunk_loaded(wet_chunk);
    poolrooms.ensure_chunk_loaded(
        {wet_chunk.x + 1, wet_chunk.z});

    const auto* loaded_chunk =
        poolrooms.find_chunk(wet_chunk);
    REQUIRE(loaded_chunk != nullptr);
    const auto local =
        poolrooms.world_to_local(
            wet_cell.x,
            wet_cell.y,
            wet_cell.z);
    const auto initial_state =
        loaded_chunk->get_water_state_local(
            local.x,
            local.y,
            local.z);
    CHECK(water_level_from_state(initial_state) == 5U);
    CHECK(water_state_is_source(initial_state));
    CHECK(water_state_is_infinite(initial_state));
    CHECK(poolrooms.pending_fluid_count() == 0U);

    WorldWorkBudget fluid_only {};
    fluid_only.chunk_generation_budget = 0U;
    fluid_only.fluid_cell_budget = 65'536U;
    fluid_only.mesh_rebuild_budget = 0U;
    fluid_only.light_node_budget = 0U;
    fluid_only.max_fluid_ms = 1000.0;
    for (int iteration = 0; iteration < 8; ++iteration) {
        const auto stats =
            poolrooms.process_pending_work(
                fluid_only);
        CHECK(stats.processed_fluid_cells == 0U);
        CHECK(stats.fluid_cells_changed == 0U);
        CHECK(stats.pending_fluid == 0U);
    }

    CHECK(
        loaded_chunk->get_water_state_local(
            local.x,
            local.y,
            local.z) ==
        initial_state);
    CHECK(poolrooms.capture_save_plan().chunks.empty());
}

} // namespace valcraft
