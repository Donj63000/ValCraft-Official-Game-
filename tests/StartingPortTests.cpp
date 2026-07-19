#include "gameplay/StartingPort.h"

#include "gameplay/PlayerController.h"
#include "gameplay/SeaAdventure.h"
#include "world/OceanAdventureLayout.h"
#include "world/WorldGenerator.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace valcraft {

namespace {

auto is_masonry(BlockId block_id) noexcept -> bool {
    return block_id == to_block_id(BlockType::Stone) ||
           block_id == to_block_id(BlockType::Cobblestone) ||
           block_id == to_block_id(BlockType::MossyStone);
}

auto save_plan_chunks_equal(const WorldSavePlanChunk& lhs, const WorldSavePlanChunk& rhs) -> bool {
    if (lhs.coord != rhs.coord || lhs.dense_blocks != rhs.dense_blocks ||
        lhs.dense_water_state != rhs.dense_water_state || lhs.sparse_cells.size() != rhs.sparse_cells.size()) {
        return false;
    }
    for (std::size_t index = 0; index < lhs.sparse_cells.size(); ++index) {
        const auto& lhs_cell = lhs.sparse_cells[index];
        const auto& rhs_cell = rhs.sparse_cells[index];
        if (lhs_cell.index != rhs_cell.index || lhs_cell.block != rhs_cell.block ||
            lhs_cell.water_state != rhs_cell.water_state) {
            return false;
        }
    }
    return true;
}

auto save_plans_equal(const WorldSavePlan& lhs, const WorldSavePlan& rhs) -> bool {
    if (lhs.seed != rhs.seed || lhs.generation_profile != rhs.generation_profile ||
        lhs.generation_version != rhs.generation_version || lhs.chunks.size() != rhs.chunks.size()) {
        return false;
    }
    for (std::size_t index = 0; index < lhs.chunks.size(); ++index) {
        if (!save_plan_chunks_equal(lhs.chunks[index], rhs.chunks[index])) {
            return false;
        }
    }
    return true;
}

auto represented_save_cells(const WorldSavePlan& plan) -> std::size_t {
    auto count = std::size_t {0};
    for (const auto& chunk : plan.chunks) {
        count += chunk.dense() ? static_cast<std::size_t>(kChunkVolume) : chunk.sparse_cells.size();
    }
    return count;
}

} // namespace

TEST_CASE("starting port layout is deterministic aligned and clear of the ship sweep") {
    constexpr int kSeed = 424242;
    const auto first = StartingPortGenerator(kSeed).build_layout();
    const auto second = StartingPortGenerator(kSeed).build_layout();
    const auto other_seed = StartingPortGenerator(kSeed + 1).build_layout();
    const auto& ship_bounds = amelie_ship_blueprint().bounds;
    const StartingPortArea expected_quay {
        kStartingPortQuayMinX,
        kStartingPortQuayMaxX,
        kStartingPortQuayMinZ,
        kStartingPortQuayMaxZ,
        kStartingPortSurfaceY,
    };

    CHECK(first == second);
    CHECK(first != other_seed);
    CHECK(first.quay_surface_y == kStartingPortSurfaceY);
    CHECK(first.stone_quay == expected_quay);
    CHECK(first.ship_sweep_min_x == static_cast<int>(std::floor(ship_bounds.min.x)));
    CHECK(first.ship_sweep_max_x == static_cast<int>(std::ceil(ship_bounds.max.x)));
    CHECK(first.gangway.max_x == first.ship_sweep_min_x - 1);
    CHECK(first.gangway.max_x < first.ship_sweep_min_x);
    CHECK(first.wooden_pier.max_x < first.ship_sweep_min_x);
    CHECK(first.breakwater_west.max_x < first.ship_sweep_min_x);
    CHECK(first.breakwater_east.max_x < first.ship_sweep_min_x);
    CHECK(first.gangway.contains(first.gangway.max_x, -8));
    CHECK(first.gangway.surface_y + 1 == 53);

    REQUIRE(first.buildings.size() == 2U);
    CHECK(first.buildings[0].role == StartingPortBuildingRole::HarborMasterOffice);
    CHECK(first.buildings[1].role == StartingPortBuildingRole::Warehouse);
    for (const auto& building : first.buildings) {
        CHECK(building.footprint.max_x + 1 < first.ship_sweep_min_x);
        CHECK(building.door.y == kStartingPortSurfaceY + 1);
        CHECK(building.interior.y == kStartingPortSurfaceY + 1);
    }

    const auto stream_reach = kDefaultStreamRadius * kChunkSizeX;
    CHECK(std::abs(first.min_x) <= stream_reach);
    CHECK(std::abs(first.max_x) <= stream_reach);
    CHECK(std::abs(first.min_z) <= stream_reach);
    CHECK(std::abs(first.max_z) <= stream_reach);
}

TEST_CASE("starting port generator builds a supported explorable working harbor") {
    constexpr int kSeed = 918273;
    StartingPortGenerator generator(kSeed);
    const auto layout = generator.build_layout();
    World world(
        kSeed,
        kDefaultStreamRadius,
        WorldGenerationProfile::OceanAdventure,
        WorldGenerationVersion::SparseArchipelagoV2);
    WorldGenerator terrain(
        kSeed,
        WorldGenerationProfile::OceanAdventure,
        WorldGenerationVersion::SparseArchipelagoV2);

    generator.apply(world, layout);

    for (int z = layout.gangway.min_z; z <= layout.gangway.max_z; ++z) {
        for (int x = layout.gangway.min_x; x <= layout.gangway.max_x; ++x) {
            CHECK(world.get_block(x, layout.gangway.surface_y, z) == to_block_id(BlockType::Planks));
            CHECK(world.get_block(x, layout.gangway.surface_y + 1, z) == to_block_id(BlockType::Air));
            CHECK(world.get_block(x, layout.gangway.surface_y + 2, z) == to_block_id(BlockType::Air));
        }
    }

    const auto pile_x = layout.gangway.min_x;
    const auto pile_z = layout.gangway.min_z;
    const auto generated_basin = terrain.sample_surface(pile_x, pile_z);
    REQUIRE(generated_basin.water_level == kSeaLevel);
    REQUIRE(generated_basin.surface_height < layout.gangway.surface_y - 1);
    for (int y = generated_basin.surface_height + 1; y < layout.gangway.surface_y; ++y) {
        CHECK(world.get_block(pile_x, y, pile_z) == to_block_id(BlockType::PineWood));
        CHECK_FALSE(world.has_water(pile_x, y, pile_z));
    }
    CHECK(world.has_water(layout.gangway.min_x + 1, kSeaLevel, layout.gangway.min_z + 1));

    for (const auto& building : layout.buildings) {
        CAPTURE(static_cast<int>(building.role));
        CHECK(world.get_block(building.door.x, building.door.y, building.door.z) == to_block_id(BlockType::Air));
        CHECK(world.get_block(building.door.x, building.door.y + 1, building.door.z) == to_block_id(BlockType::Air));
        CHECK_FALSE(world.has_water(building.door.x, building.door.y, building.door.z));
        CHECK(world.get_block(building.interior.x, building.interior.y, building.interior.z) == to_block_id(BlockType::Air));
        CHECK(world.get_block(building.interior.x, building.interior.y + 1, building.interior.z) == to_block_id(BlockType::Air));
        CHECK_FALSE(world.has_water(building.interior.x, building.interior.y, building.interior.z));
        CHECK(is_block_collidable(world.get_block(
            building.interior.x,
            building.footprint.surface_y,
            building.interior.z)));
        CHECK(is_masonry(world.get_block(
            building.footprint.min_x,
            building.footprint.surface_y - 1,
            building.footprint.min_z)));
    }

    auto lighthouse_glass = 0;
    auto lighthouse_torches = 0;
    for (int z = layout.lighthouse_base.z - 3; z <= layout.lighthouse_base.z + 3; ++z) {
        for (int x = layout.lighthouse_base.x - 3; x <= layout.lighthouse_base.x + 3; ++x) {
            for (int y = layout.lighthouse_base.y + 1; y <= layout.lighthouse_base.y + 14; ++y) {
                const auto block_id = world.get_block(x, y, z);
                lighthouse_glass += block_id == to_block_id(BlockType::Glass) ? 1 : 0;
                lighthouse_torches += block_id == to_block_id(BlockType::Torch) ? 1 : 0;
            }
        }
    }
    CHECK(lighthouse_glass >= 16);
    CHECK(lighthouse_torches >= 5);

    CHECK(world.get_block(
        layout.crane_base.x,
        layout.crane_base.y + 9,
        layout.crane_base.z) == to_block_id(BlockType::PineWood));
    CHECK(world.get_block(
        layout.gangway.max_x,
        layout.crane_base.y + 4,
        layout.crane_base.z) == to_block_id(BlockType::Cobblestone));
    for (const auto& anchor : layout.cargo_anchors) {
        CHECK(world.get_block(anchor.x, anchor.y, anchor.z) == to_block_id(BlockType::Planks));
    }
    for (const auto& anchor : layout.bollards) {
        CHECK(world.get_block(anchor.x, anchor.y, anchor.z) == to_block_id(BlockType::Wood));
    }
    for (const auto& anchor : layout.lantern_posts) {
        CHECK(world.get_block(anchor.x, anchor.y + 3, anchor.z) == to_block_id(BlockType::Torch));
    }
}

TEST_CASE("starting port player walks between the gangway and Amelie in both directions") {
    constexpr int kSeed = 918274;
    StartingPortGenerator generator(kSeed);
    const auto layout = generator.build_layout();
    World world(
        kSeed,
        kDefaultStreamRadius,
        WorldGenerationProfile::OceanAdventure,
        WorldGenerationVersion::SparseArchipelagoV2);
    generator.apply(world, layout);

    SeaAdventureSystem sea_adventure {};
    sea_adventure.reset(kSeed);
    const auto& ship = sea_adventure.ship_entity();
    const auto deck_y = ship.world_origin().y + 4.001F;
    PlayerController player({
        static_cast<float>(layout.gangway.max_x) - 1.5F,
        deck_y,
        -8.0F,
    });

    for (int frame = 0; frame < 3; ++frame) {
        player.update(PlayerInput {}, 1.0F / 60.0F, world, &ship);
    }
    REQUIRE(player.state().on_ground);

    const auto walk_until = [&](float right, const auto& reached) {
        PlayerInput input {};
        input.move_right = right;
        for (int frame = 0; frame < 240; ++frame) {
            player.update(input, 1.0F / 60.0F, world, &ship);
            CHECK_FALSE(input.jump);
            CHECK(player.state().on_ground);
            CHECK(player.position().y == doctest::Approx(deck_y).epsilon(0.001F));
            CHECK_FALSE(player.state().head_underwater);
            CHECK_FALSE(player.state().swimming);
            CHECK_FALSE(player.overlaps_dynamic_obstacle(ship));
            if (reached(player.position())) {
                return true;
            }
        }
        return false;
    };

    REQUIRE(walk_until(1.0F, [](const glm::vec3& position) {
        return position.x >= -7.50F;
    }));
    const auto deck_support = ship.support_height(player.position());
    REQUIRE(deck_support.has_value());
    CHECK(*deck_support == doctest::Approx(deck_y - 0.001F).epsilon(0.001F));

    REQUIRE(walk_until(-1.0F, [&](const glm::vec3& position) {
        return position.x <= static_cast<float>(layout.gangway.max_x) - 0.50F;
    }));
    CHECK_FALSE(ship.support_height(player.position()).has_value());
    CHECK(world.get_block(
        static_cast<int>(std::floor(player.position().x)),
        layout.gangway.surface_y,
        static_cast<int>(std::floor(player.position().z))) == to_block_id(BlockType::Planks));
}

TEST_CASE("starting port save plan restores every harbor landmark without rebuilding") {
    constexpr int kSeed = 86420;
    StartingPortGenerator generator(kSeed);
    const auto layout = generator.build_layout();
    REQUIRE(layout.buildings.size() == 2U);

    World source(
        kSeed,
        kDefaultStreamRadius,
        WorldGenerationProfile::OceanAdventure,
        WorldGenerationVersion::SparseArchipelagoV2);
    generator.apply(source, layout);
    const auto save_plan = source.capture_save_plan();
    REQUIRE(save_plan.generation_version == WorldGenerationVersion::SparseArchipelagoV2);

    std::vector<BlockCoord> markers {
        {layout.stone_quay.min_x, layout.stone_quay.surface_y, layout.stone_quay.min_z},
        {layout.gangway.max_x, layout.gangway.surface_y, -8},
        {layout.buildings[0].footprint.min_x,
         layout.buildings[0].footprint.surface_y + 5,
         layout.buildings[0].footprint.min_z},
        {layout.buildings[1].footprint.min_x,
         layout.buildings[1].footprint.surface_y + 5,
         layout.buildings[1].footprint.min_z},
        {layout.lighthouse_base.x, layout.lighthouse_base.y + 12, layout.lighthouse_base.z},
        {layout.gangway.max_x, layout.crane_base.y + 4, layout.crane_base.z},
    };
    std::vector<BlockId> expected_blocks {};
    std::vector<std::uint8_t> expected_water_levels {};
    expected_blocks.reserve(markers.size());
    expected_water_levels.reserve(markers.size());
    for (const auto& marker : markers) {
        expected_blocks.push_back(source.get_block(marker.x, marker.y, marker.z));
        expected_water_levels.push_back(source.water_level(marker.x, marker.y, marker.z));
    }

    World restored(
        kSeed,
        kDefaultStreamRadius,
        WorldGenerationProfile::OceanAdventure,
        WorldGenerationVersion::SparseArchipelagoV2);
    restored.begin_restore_save_plan(save_plan);
    while (restored.has_pending_save_restore()) {
        static_cast<void>(restored.process_save_restore(
            kChunkVolume,
            std::numeric_limits<double>::infinity()));
    }

    CHECK(restored.generation_version() == WorldGenerationVersion::SparseArchipelagoV2);
    for (std::size_t index = 0; index < markers.size(); ++index) {
        CAPTURE(index);
        CHECK(restored.get_block(markers[index].x, markers[index].y, markers[index].z) == expected_blocks[index]);
        CHECK(restored.water_level(markers[index].x, markers[index].y, markers[index].z) ==
              expected_water_levels[index]);
    }
}

TEST_CASE("starting port application is idempotent and keeps a bounded save plan") {
    constexpr int kSeed = 13579;
    StartingPortGenerator generator(kSeed);
    const auto layout = generator.build_layout();
    World world(
        kSeed,
        kDefaultStreamRadius,
        WorldGenerationProfile::OceanAdventure,
        WorldGenerationVersion::SparseArchipelagoV2);

    generator.apply(world, layout);
    const auto first_plan = world.capture_save_plan();
    generator.apply(world, layout);
    const auto second_plan = world.capture_save_plan();

    CHECK(save_plans_equal(first_plan, second_plan));
    CHECK_FALSE(first_plan.chunks.empty());
    CHECK(first_plan.chunks.size() <= 32U);
    CHECK(represented_save_cells(first_plan) <= 24'000U);
    CHECK(std::none_of(first_plan.chunks.begin(), first_plan.chunks.end(), [](const WorldSavePlanChunk& chunk) {
        return chunk.dense();
    }));

    for (const auto& chunk : first_plan.chunks) {
        REQUIRE_FALSE(chunk.dense());
        for (const auto& cell : chunk.sparse_cells) {
            const auto linear_index = static_cast<std::size_t>(cell.index);
            const auto local_x = static_cast<int>(linear_index % static_cast<std::size_t>(kChunkSizeX));
            const auto world_x = chunk.coord.x * kChunkSizeX + local_x;
            CAPTURE(chunk.coord.x);
            CAPTURE(chunk.coord.z);
            CAPTURE(cell.index);
            CHECK(world_x < layout.ship_sweep_min_x);
        }
    }
}

} // namespace valcraft
