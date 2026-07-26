#include "app/TerrainEditStress.h"

#include <doctest/doctest.h>

#include <glm/vec3.hpp>

#include <cstddef>
#include <limits>
#include <vector>

namespace valcraft {

namespace {

[[nodiscard]] auto opposite_action(
    TerrainEditStressAction action) noexcept
    -> TerrainEditStressAction {
    return action == TerrainEditStressAction::Break
               ? TerrainEditStressAction::Place
               : TerrainEditStressAction::Break;
}

} // namespace

TEST_CASE("terrain edit stress is isolated to its exact smoke scenario") {
    GameOptions options {};
    options.performance.perf_scenario =
        "terrain_edit_stress";
    CHECK_FALSE(terrain_edit_stress_enabled(options));

    options.smoke_test = true;
    CHECK(terrain_edit_stress_enabled(options));

    options.performance.perf_scenario =
        "terrain-edit-stress";
    CHECK_FALSE(terrain_edit_stress_enabled(options));

    options.performance.perf_scenario =
        "baseline";
    CHECK_FALSE(terrain_edit_stress_enabled(options));
}

TEST_CASE(
    "terrain edit stress is deterministic reversible and exercises chunk borders") {
    constexpr auto seed = 73'821;
    World first_world(
        seed,
        0,
        WorldGenerationProfile::Continental,
        WorldGenerationVersion::Latest,
        VisualPipeline::ModernStylized);
    World second_world(
        seed,
        0,
        WorldGenerationProfile::Continental,
        WorldGenerationVersion::Latest,
        VisualPipeline::ModernStylized);
    TerrainEditStressScenario first_scenario {};
    TerrainEditStressScenario second_scenario {};
    const glm::vec3 focus {-0.5F, 80.0F, -0.5F};

    std::vector<TerrainEditStressOperation> operations;
    for (std::size_t frame = 0U;
         frame <= 7U * kTerrainEditStressIntervalFrames;
         frame += kTerrainEditStressIntervalFrames) {
        const auto first =
            first_scenario.update(
                first_world,
                focus,
                frame);
        const auto second =
            second_scenario.update(
                second_world,
                focus,
                frame);
        REQUIRE(first.has_value());
        REQUIRE(second.has_value());
        CHECK(*first == *second);
        CHECK(
            first_world.get_block(
                first->block.x,
                first->block.y,
                first->block.z) ==
            first->next_block);
        CHECK(
            second_world.get_block(
                second->block.x,
                second->block.y,
                second->block.z) ==
            second->next_block);
        CHECK_FALSE(
            first_scenario
                .update(first_world, focus, frame)
                .has_value());

        const auto local =
            first_world.world_to_local(
                first->block.x,
                first->block.y,
                first->block.z);
        CHECK((
            local.x == 0 ||
            local.x == kChunkSizeX - 1 ||
            local.z == 0 ||
            local.z == kChunkSizeZ - 1));
        if ((frame / kTerrainEditStressIntervalFrames) % 2U ==
            0U) {
            CHECK_FALSE(
                first_world
                    .capture_save_plan()
                    .chunks.empty());
        } else {
            CHECK(
                first_world
                    .capture_save_plan()
                    .chunks.empty());
        }
        operations.push_back(*first);
    }

    REQUIRE(operations.size() == 8U);
    for (std::size_t pair_index = 0U;
         pair_index < 4U;
         ++pair_index) {
        const auto& edit =
            operations[pair_index * 2U];
        const auto& restore =
            operations[pair_index * 2U + 1U];
        CHECK(edit.pair_index == pair_index);
        CHECK(restore.pair_index == pair_index);
        CHECK(restore.block == edit.block);
        CHECK(
            restore.action ==
            opposite_action(edit.action));
        CHECK(
            restore.next_block ==
            edit.previous_block);
    }

    CHECK_FALSE(first_scenario.has_pending_restore());
    CHECK(first_scenario.completed_pair_count() == 4U);
    CHECK_FALSE(second_scenario.has_pending_restore());
    CHECK(second_scenario.completed_pair_count() == 4U);
    CHECK(first_world.capture_save_plan().chunks.empty());
    CHECK(second_world.capture_save_plan().chunks.empty());
}

TEST_CASE(
    "terrain edit stress honors final-frame cleanup and rejects invalid focuses") {
    World world(
        1'337,
        0,
        WorldGenerationProfile::Continental,
        WorldGenerationVersion::Latest,
        VisualPipeline::ModernStylized);
    TerrainEditStressScenario scenario {};
    const glm::vec3 focus {0.5F, 80.0F, 0.5F};

    CHECK_FALSE(
        scenario
            .update(world, focus, 0U, false)
            .has_value());
    const auto edit =
        scenario.update(
            world,
            focus,
            kTerrainEditStressIntervalFrames);
    REQUIRE(edit.has_value());
    CHECK(scenario.has_pending_restore());

    const auto restore =
        scenario.update(
            world,
            focus,
            2U * kTerrainEditStressIntervalFrames,
            false);
    REQUIRE(restore.has_value());
    CHECK(
        restore->action ==
        opposite_action(edit->action));
    CHECK_FALSE(scenario.has_pending_restore());
    CHECK(world.capture_save_plan().chunks.empty());

    scenario.reset();
    CHECK(scenario.completed_pair_count() == 0U);
    const glm::vec3 invalid_focus {
        std::numeric_limits<float>::quiet_NaN(),
        80.0F,
        0.5F,
    };
    CHECK_FALSE(
        scenario
            .update(world, invalid_focus, 0U)
            .has_value());
    CHECK_FALSE(scenario.has_pending_restore());
    CHECK(world.capture_save_plan().chunks.empty());
}

} // namespace valcraft
