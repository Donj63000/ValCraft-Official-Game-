#include "gameplay/quests/LegendaryQuestWorldContent.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string_view>
#include <utility>
#include <vector>

namespace valcraft {
namespace {

struct BlockCoordinateLess {
    [[nodiscard]] auto operator()(
        const BlockCoord& lhs,
        const BlockCoord& rhs) const noexcept -> bool {
        if (lhs.x != rhs.x) {
            return lhs.x < rhs.x;
        }
        if (lhs.y != rhs.y) {
            return lhs.y < rhs.y;
        }
        return lhs.z < rhs.z;
    }
};

struct ContentTransactionFixture {
    std::map<BlockCoord, BlockId, BlockCoordinateLess> world {};
    std::size_t validation_count = 0U;
    std::size_t commit_attempt_count = 0U;
    std::size_t rollback_count = 0U;
    std::size_t fail_commit_at =
        std::numeric_limits<std::size_t>::max();

    [[nodiscard]] auto callbacks()
        -> WorldEditTransactionCallbacks {
        WorldEditTransactionCallbacks result {};
        result.validate_cell =
            [this](const WorldEditCell& cell) {
                ++validation_count;
                return
                    cell.coordinate.y >= kWorldMinY &&
                    cell.coordinate.y <= kWorldMaxY;
            };
        result.cell_contains_player_or_creature =
            [](const BlockCoord&) {
                return false;
            };
        result.read_current =
            [this](const BlockCoord& coordinate)
                -> std::optional<WorldEditCellState> {
                const auto found = world.find(coordinate);
                return WorldEditCellState {
                    coordinate,
                    found == world.end()
                        ? to_block_id(BlockType::Air)
                        : found->second,
                    0U,
                    false,
                };
            };
        result.commit_cell =
            [this](const WorldEditCell& cell) {
                const auto attempt = commit_attempt_count;
                ++commit_attempt_count;
                if (attempt == fail_commit_at) {
                    return false;
                }
                world[cell.coordinate] = cell.block_id;
                return true;
            };
        result.rollback_cell =
            [this](const WorldEditCellState& state) {
                ++rollback_count;
                if (state.block_id ==
                    to_block_id(BlockType::Air)) {
                    world.erase(state.coordinate);
                } else {
                    world[state.coordinate] =
                        state.block_id;
                }
            };
        result.materials_available =
            [](BlockId, std::uint32_t) {
                return true;
            };
        result.consume_materials =
            [](BlockId, std::uint32_t) {
                return true;
            };
        result.refund_materials =
            [](BlockId, std::uint32_t) {};
        return result;
    }
};

[[nodiscard]] auto contains_cell(
    const WorldProtectionRegion& region,
    const BlockCoord& coordinate) noexcept -> bool {
    return
        coordinate.x >= region.minimum.x &&
        coordinate.x <= region.maximum.x &&
        coordinate.y >= region.minimum.y &&
        coordinate.y <= region.maximum.y &&
        coordinate.z >= region.minimum.z &&
        coordinate.z <= region.maximum.z;
}

[[nodiscard]] auto find_sea_plan_for_site(
    LegendaryQuestForgeSite site)
    -> LegendaryQuestWorldContentPlan {
    for (std::uint64_t seed = 0ULL;
         seed < 512ULL;
         ++seed) {
        const auto candidate =
            generate_legendary_quest_world_content(
                seed,
                GameMode::SeaAdventure);
        if (candidate.has_value() &&
            candidate->forge_site == site) {
            return *candidate;
        }
    }
    FAIL_CHECK("aucune graine maritime ne produit le site attendu");
    return {};
}

}

TEST_CASE("les plans de contenu sont deterministes et distinguent les deux aventures") {
    constexpr std::uint64_t seed = 0x51A7C0FFEEULL;
    const auto classic_first =
        generate_legendary_quest_world_content(
            seed,
            GameMode::ClassicAdventure);
    const auto classic_second =
        generate_legendary_quest_world_content(
            seed,
            GameMode::ClassicAdventure);
    const auto maritime =
        generate_legendary_quest_world_content(
            seed,
            GameMode::SeaAdventure);

    REQUIRE(classic_first.has_value());
    REQUIRE(classic_second.has_value());
    REQUIRE(maritime.has_value());
    CHECK(*classic_first == *classic_second);
    CHECK(*classic_first != *maritime);
    CHECK(
        classic_first->forge_site ==
        LegendaryQuestForgeSite::RemoteMountain);
    const auto maritime_site_is_valid =
        maritime->forge_site ==
            LegendaryQuestForgeSite::VolcanicIsland ||
        maritime->forge_site ==
            LegendaryQuestForgeSite::RuinedIsland;
    CHECK(maritime_site_is_valid);
    CHECK(
        classic_first->block_edit_count ==
        kWorldEditMaximumCellCount);
    CHECK(
        maritime->block_edit_count ==
        kWorldEditMaximumCellCount);
    CHECK(is_valid_legendary_quest_world_content(
        *classic_first));
    CHECK(is_valid_legendary_quest_world_content(
        *maritime));

    CHECK_FALSE(
        generate_legendary_quest_world_content(
            seed,
            static_cast<GameMode>(255U))
            .has_value());

    const auto volcanic = find_sea_plan_for_site(
        LegendaryQuestForgeSite::VolcanicIsland);
    const auto ruined = find_sea_plan_for_site(
        LegendaryQuestForgeSite::RuinedIsland);
    CHECK(volcanic != ruined);
    CHECK(
        volcanic.forge_site ==
        LegendaryQuestForgeSite::VolcanicIsland);
    CHECK(
        ruined.forge_site ==
        LegendaryQuestForgeSite::RuinedIsland);
}

TEST_CASE("toutes les ancres projetees et tous les edits restent dans le monde") {
    for (const auto mode :
         {GameMode::ClassicAdventure,
          GameMode::SeaAdventure}) {
        for (std::uint64_t seed = 0ULL;
             seed < 256ULL;
             ++seed) {
            const auto plan =
                generate_legendary_quest_world_content(
                    seed,
                    mode);
            REQUIRE(plan.has_value());
            CHECK(is_valid_legendary_quest_world_content(
                *plan));
            REQUIRE(
                plan->anchors.size() ==
                kLegendaryQuestWorldAnchorPlacementCount);
            REQUIRE(
                plan->scenes.size() ==
                kLegendaryQuestWorldSceneCount);

            std::set<
                BlockCoord,
                BlockCoordinateLess>
                unique_cells {};
            for (const auto& edit : plan->edits()) {
                CHECK(
                    edit.coordinate.x >=
                    -kLegendaryQuestWorldHorizontalCoordinateLimit);
                CHECK(
                    edit.coordinate.x <=
                    kLegendaryQuestWorldHorizontalCoordinateLimit);
                CHECK(edit.coordinate.y >= kWorldMinY);
                CHECK(edit.coordinate.y <= kWorldMaxY);
                CHECK(
                    edit.coordinate.z >=
                    -kLegendaryQuestWorldHorizontalCoordinateLimit);
                CHECK(
                    edit.coordinate.z <=
                    kLegendaryQuestWorldHorizontalCoordinateLimit);
                CHECK(is_known_block_id(edit.block_id));
                CHECK(unique_cells.insert(
                    edit.coordinate).second);
            }
            CHECK(
                unique_cells.size() ==
                plan->block_edit_count);

            const auto layout =
                generate_legendary_weapon_quest_layout(
                    seed,
                    mode);
            REQUIRE(layout.has_value());
            const std::array expected_ids {
                layout->rumor.id,
                layout->map_clues[0].source.id,
                layout->map_clues[1].source.id,
                layout->map_clues[2].source.id,
                layout->forge.id,
                layout->guardian.id,
                layout->blade.id,
            };
            for (const auto anchor_id : expected_ids) {
                const auto placement =
                    plan->anchor(anchor_id);
                REQUIRE(placement.has_value());
                CHECK(placement->anchor_id == anchor_id);
                CHECK(
                    placement->interaction_position.y >=
                    kWorldMinY);
                CHECK(
                    placement->interaction_position.y <=
                    kWorldMaxY);
            }
        }
    }
}

TEST_CASE("les petites scenes respectent le budget transactionnel annonce") {
    const auto plan =
        generate_legendary_quest_world_content(
            87ULL,
            GameMode::ClassicAdventure);
    REQUIRE(plan.has_value());
    const std::array<std::size_t, 5U>
        expected_counts {4U, 4U, 4U, 4U, 48U};
    std::size_t next_edit = 0U;
    for (std::size_t index = 0U;
         index < plan->scenes.size();
         ++index) {
        const auto& scene = plan->scenes[index];
        CHECK(scene.first_block_edit == next_edit);
        CHECK(
            scene.block_edit_count ==
            expected_counts[index]);
        CHECK(
            plan->scene_edits(index).size() ==
            expected_counts[index]);
        CHECK(
            scene.block_edit_count <=
            kWorldEditMaximumCellCount);
        next_edit += scene.block_edit_count;
    }
    CHECK(next_edit == plan->block_edit_count);
    CHECK(plan->scene_edits(plan->scenes.size()).empty());
}

TEST_CASE("la forge materialise une fois chacun des sept elements du plan") {
    const auto plan =
        generate_legendary_quest_world_content(
            0xFEA70001ULL,
            GameMode::ClassicAdventure);
    REQUIRE(plan.has_value());

    std::size_t feature_edit_count = 0U;
    for (std::size_t index = 0U;
         index < kLegendaryQuestForgeFeatures.size();
         ++index) {
        const auto expected =
            kLegendaryQuestForgeFeatures[index];
        const auto placement =
            plan->forge_feature(expected);
        REQUIRE(placement.has_value());
        CHECK(placement->feature == expected);
        CHECK_FALSE(placement->feature_id.empty());
        CHECK(placement->block_edit_count > 0U);
        CHECK(
            placement->minimum.x <=
            placement->maximum.x);
        CHECK(
            placement->minimum.y <=
            placement->maximum.y);
        CHECK(
            placement->minimum.z <=
            placement->maximum.z);

        const auto begin =
            placement->first_block_edit;
        const auto end =
            begin + placement->block_edit_count;
        REQUIRE(end <= plan->block_edit_count);
        for (std::size_t edit_index = begin;
             edit_index < end;
             ++edit_index) {
            const auto& coordinate =
                plan->block_edits[edit_index].coordinate;
            CHECK(
                coordinate.x >= placement->minimum.x);
            CHECK(
                coordinate.x <= placement->maximum.x);
            CHECK(
                coordinate.y >= placement->minimum.y);
            CHECK(
                coordinate.y <= placement->maximum.y);
            CHECK(
                coordinate.z >= placement->minimum.z);
            CHECK(
                coordinate.z <= placement->maximum.z);
        }
        feature_edit_count +=
            placement->block_edit_count;
    }
    CHECK(feature_edit_count == 44U);
    CHECK_FALSE(
        plan->forge_feature(
            static_cast<LegendaryQuestForgeFeature>(255U))
            .has_value());
}

TEST_CASE("chaque scene fournit un volume de quete enregistrable et couvrant") {
    const auto plan =
        generate_legendary_quest_world_content(
            9ULL,
            GameMode::SeaAdventure);
    REQUIRE(plan.has_value());
    WorldProtectionRegistry registry {};

    for (std::size_t index = 0U;
         index < plan->protection_volumes.size();
         ++index) {
        const auto& volume =
            plan->protection_volumes[index];
        CHECK(
            volume.region.flags ==
            WorldProtectionFlag::QuestStructure);
        const auto registration =
            registry.register_region(volume.region);
        CHECK(registration.registered);
        CHECK(
            registration.error ==
            WorldProtectionRegistrationError::None);

        const auto& scene = plan->scenes[index];
        CHECK(
            scene.protection_volume_index == index);
        CHECK(
            scene.anchor_id ==
            volume.source_anchor_id);
        for (const auto& edit :
             plan->scene_edits(index)) {
            CHECK(contains_cell(
                volume.region,
                edit.coordinate));
            CHECK(world_protection_contains(
                registry.protection_at(
                    {
                        edit.coordinate.x,
                        edit.coordinate.y,
                        edit.coordinate.z,
                    }),
                WorldProtectionFlag::QuestStructure));
        }
    }
    CHECK(
        registry.region_count() ==
        kLegendaryQuestWorldProtectionVolumeCount);
}

TEST_CASE("la pose globale est atomique et restaure le monde si un commit echoue") {
    const auto plan =
        generate_legendary_quest_world_content(
            41ULL,
            GameMode::ClassicAdventure);
    REQUIRE(plan.has_value());
    REQUIRE(is_valid_legendary_quest_world_content(*plan));

    ContentTransactionFixture success {};
    const auto success_result =
        execute_legendary_quest_world_content(
            *plan,
            success.callbacks());
    CHECK(success_result.succeeded());
    CHECK(
        success_result.requested_cell_count ==
        kWorldEditMaximumCellCount);
    CHECK(
        success_result.unique_cell_count ==
        kWorldEditMaximumCellCount);
    CHECK(
        success_result.commit_count ==
        kWorldEditMaximumCellCount);
    CHECK(
        success.world.size() ==
        kWorldEditMaximumCellCount);

    ContentTransactionFixture failure {};
    failure.fail_commit_at = 23U;
    const auto original_world = failure.world;
    const auto failure_result =
        execute_legendary_quest_world_content(
            *plan,
            failure.callbacks());
    CHECK(
        failure_result.status ==
        WorldEditTransactionStatus::CommitFailed);
    CHECK(failure_result.commit_count == 23U);
    CHECK(failure_result.rollback_count == 23U);
    CHECK(failure.rollback_count == 23U);
    CHECK(failure.world == original_world);
}

TEST_CASE("une scene seule reste atomique et une corruption ne declenche aucun callback") {
    const auto generated =
        generate_legendary_quest_world_content(
            104ULL,
            GameMode::SeaAdventure);
    REQUIRE(generated.has_value());

    for (std::size_t scene_index = 0U;
         scene_index < generated->scenes.size();
         ++scene_index) {
        ContentTransactionFixture fixture {};
        const auto result =
            execute_legendary_quest_world_scene(
                *generated,
                scene_index,
                fixture.callbacks());
        CHECK(result.succeeded());
        CHECK(
            result.requested_cell_count ==
            generated->scenes[scene_index]
                .block_edit_count);
        CHECK(
            fixture.world.size() ==
            generated->scenes[scene_index]
                .block_edit_count);
    }

    ContentTransactionFixture invalid_index_fixture {};
    const auto invalid_index_result =
        execute_legendary_quest_world_scene(
            *generated,
            generated->scenes.size(),
            invalid_index_fixture.callbacks());
    CHECK(
        invalid_index_result.status ==
        WorldEditTransactionStatus::InvalidTarget);
    CHECK(
        invalid_index_fixture.validation_count == 0U);

    auto corrupted = *generated;
    corrupted.protection_volumes[0].region.flags =
        WorldProtectionFlag::ImportantStructure;
    CHECK_FALSE(
        is_valid_legendary_quest_world_content(
            corrupted));
    ContentTransactionFixture corrupted_fixture {};
    const auto corrupted_result =
        execute_legendary_quest_world_content(
            corrupted,
            corrupted_fixture.callbacks());
    CHECK(
        corrupted_result.status ==
        WorldEditTransactionStatus::InvalidTarget);
    CHECK(corrupted_fixture.validation_count == 0U);
    CHECK(corrupted_fixture.commit_attempt_count == 0U);

    corrupted = *generated;
    corrupted.block_edits[1].coordinate =
        corrupted.block_edits[0].coordinate;
    CHECK_FALSE(
        is_valid_legendary_quest_world_content(
            corrupted));

    corrupted = *generated;
    corrupted.forge_features[0].block_edit_count = 0U;
    CHECK_FALSE(
        is_valid_legendary_quest_world_content(
            corrupted));
}

TEST_CASE("la proximite 3D et horizontale gere les bornes et les entrees non finies") {
    const LegendaryQuestSpatialPoint origin {
        0.0F,
        10.0F,
        0.0F,
    };
    const LegendaryQuestSpatialPoint boundary {
        3.0F,
        14.0F,
        0.0F,
    };
    CHECK(
        is_legendary_quest_near_3d(
            origin,
            boundary,
            5.0F));
    CHECK(
        evaluate_legendary_quest_proximity(
            {
                origin,
                boundary,
                5.0F,
                LegendaryQuestProximityMetric::
                    ThreeDimensional,
                0.0F,
            })
            .within);
    CHECK_FALSE(
        is_legendary_quest_near_3d(
            origin,
            boundary,
            4.99F));
    CHECK(
        is_legendary_quest_near_horizontal(
            origin,
            boundary,
            3.0F));
    CHECK_FALSE(
        is_legendary_quest_near_horizontal(
            origin,
            boundary,
            3.0F,
            3.99F));
    CHECK(
        is_legendary_quest_near_horizontal(
            origin,
            boundary,
            3.0F,
            4.0F));
    CHECK(
        is_legendary_quest_near_3d(
            origin,
            origin,
            0.0F));

    auto invalid_query = LegendaryQuestProximityQuery {
        origin,
        boundary,
        -1.0F,
        LegendaryQuestProximityMetric::ThreeDimensional,
        std::nullopt,
    };
    CHECK_FALSE(
        evaluate_legendary_quest_proximity(
            invalid_query)
            .valid);
    invalid_query.radius = 5.0F;
    invalid_query.observer.x =
        std::numeric_limits<float>::quiet_NaN();
    CHECK_FALSE(
        evaluate_legendary_quest_proximity(
            invalid_query)
            .valid);
    invalid_query.observer = origin;
    invalid_query.metric =
        static_cast<LegendaryQuestProximityMetric>(255U);
    CHECK_FALSE(
        evaluate_legendary_quest_proximity(
            invalid_query)
            .valid);

    const auto huge =
        std::numeric_limits<float>::max() / 4.0F;
    const auto huge_result =
        evaluate_legendary_quest_proximity(
            {
                {huge, huge, huge},
                {-huge, -huge, -huge},
                huge,
                LegendaryQuestProximityMetric::
                    ThreeDimensional,
                std::nullopt,
            });
    CHECK(huge_result.valid);
    CHECK_FALSE(huge_result.within);
    CHECK(std::isfinite(
        huge_result.distance_squared));
}

}
