#include "TestUtils.h"
#include "app/Hotbar.h"
#include "app/InventoryMenu.h"
#include "creatures/CreatureSystem.h"
#include "gameplay/ItemDropSystem.h"
#include "gameplay/PlayerController.h"
#include "render/VisualPipeline.h"
#include "world/Environment.h"
#include "world/World.h"

#include <doctest/doctest.h>

#include <glm/vec3.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <utility>
#include <vector>

namespace valcraft {

namespace {

auto make_differential_world(VisualPipeline pipeline) -> World {
    return World(
        904'221,
        0,
        WorldGenerationProfile::Continental,
        WorldGenerationVersion::Latest,
        pipeline);
}

auto save_plan_digest(const WorldSavePlan& plan) -> std::uint64_t {
    auto digest = UINT64_C(14695981039346656037);
    const auto mix = [&digest](std::uint64_t value) {
        for (unsigned int shift = 0U; shift < 64U; shift += 8U) {
            digest ^= static_cast<std::uint8_t>(value >> shift);
            digest *= UINT64_C(1099511628211);
        }
    };

    mix(static_cast<std::uint32_t>(plan.seed));
    mix(static_cast<std::uint8_t>(plan.generation_profile));
    mix(static_cast<std::uint8_t>(plan.generation_version));
    mix(plan.chunks.size());
    for (const auto& chunk : plan.chunks) {
        mix(static_cast<std::uint32_t>(chunk.coord.x));
        mix(static_cast<std::uint32_t>(chunk.coord.z));
        mix(chunk.sparse_cells.size());
        for (const auto& cell : chunk.sparse_cells) {
            mix(cell.index);
            mix(cell.block);
            mix(cell.water_state);
        }
        mix(chunk.dense_blocks.size());
        for (const auto block : chunk.dense_blocks) {
            mix(block);
        }
        mix(chunk.dense_water_state.size());
        for (const auto water : chunk.dense_water_state) {
            mix(water);
        }
    }
    return digest;
}

void empty_test_neighborhood(World& world) {
    for (int chunk_z = -1; chunk_z <= 1; ++chunk_z) {
        for (int chunk_x = -1; chunk_x <= 1; ++chunk_x) {
            test::make_chunk_empty(world, {chunk_x, chunk_z});
        }
    }
}

void check_same_hit(const RaycastHit& legacy, const RaycastHit& modern) {
    CHECK(legacy.hit == modern.hit);
    CHECK(legacy.block == modern.block);
    CHECK(legacy.adjacent == modern.adjacent);
    CHECK(legacy.block_id == modern.block_id);
    CHECK(legacy.distance == doctest::Approx(modern.distance).epsilon(0.000001F));
}

void check_same_player_state(const PlayerState& legacy, const PlayerState& modern) {
    CHECK(legacy.position.x == doctest::Approx(modern.position.x).epsilon(0.000001F));
    CHECK(legacy.position.y == doctest::Approx(modern.position.y).epsilon(0.000001F));
    CHECK(legacy.position.z == doctest::Approx(modern.position.z).epsilon(0.000001F));
    CHECK(legacy.velocity.x == doctest::Approx(modern.velocity.x).epsilon(0.000001F));
    CHECK(legacy.velocity.y == doctest::Approx(modern.velocity.y).epsilon(0.000001F));
    CHECK(legacy.velocity.z == doctest::Approx(modern.velocity.z).epsilon(0.000001F));
    CHECK(legacy.yaw_degrees == doctest::Approx(modern.yaw_degrees).epsilon(0.000001F));
    CHECK(legacy.pitch_degrees == doctest::Approx(modern.pitch_degrees).epsilon(0.000001F));
    CHECK(legacy.body_yaw_degrees == doctest::Approx(modern.body_yaw_degrees).epsilon(0.000001F));
    CHECK(legacy.animation_time == doctest::Approx(modern.animation_time).epsilon(0.000001F));
    CHECK(legacy.step_phase == doctest::Approx(modern.step_phase).epsilon(0.000001F));
    CHECK(legacy.health == doctest::Approx(modern.health).epsilon(0.000001F));
    CHECK(legacy.air_seconds == doctest::Approx(modern.air_seconds).epsilon(0.000001F));
    CHECK(legacy.on_ground == modern.on_ground);
    CHECK(legacy.fly_mode == modern.fly_mode);
    CHECK(legacy.head_underwater == modern.head_underwater);
    CHECK(legacy.swimming == modern.swimming);
    CHECK(legacy.dead == modern.dead);
    CHECK(legacy.death_cause == modern.death_cause);
}

void check_same_drop(const ItemDrop& legacy, const ItemDrop& modern) {
    CHECK(legacy.position.x == doctest::Approx(modern.position.x).epsilon(0.000001F));
    CHECK(legacy.position.y == doctest::Approx(modern.position.y).epsilon(0.000001F));
    CHECK(legacy.position.z == doctest::Approx(modern.position.z).epsilon(0.000001F));
    CHECK(legacy.velocity.x == doctest::Approx(modern.velocity.x).epsilon(0.000001F));
    CHECK(legacy.velocity.y == doctest::Approx(modern.velocity.y).epsilon(0.000001F));
    CHECK(legacy.velocity.z == doctest::Approx(modern.velocity.z).epsilon(0.000001F));
    CHECK(legacy.stack == modern.stack);
    CHECK(legacy.age_seconds == doctest::Approx(modern.age_seconds).epsilon(0.000001F));
    CHECK(legacy.pickup_cooldown == doctest::Approx(modern.pickup_cooldown).epsilon(0.000001F));
    CHECK(legacy.grounded == modern.grounded);
    CHECK(legacy.sleeping == modern.sleeping);
    CHECK(legacy.sleep_support_valid == modern.sleep_support_valid);
    CHECK(legacy.sleep_support_block == modern.sleep_support_block);
}

void check_same_creature(const CreatureInstance& legacy, const CreatureInstance& modern) {
    CHECK(legacy.anchor == modern.anchor);
    CHECK(legacy.position.x == doctest::Approx(modern.position.x).epsilon(0.000001F));
    CHECK(legacy.position.y == doctest::Approx(modern.position.y).epsilon(0.000001F));
    CHECK(legacy.position.z == doctest::Approx(modern.position.z).epsilon(0.000001F));
    CHECK(legacy.yaw_radians == doctest::Approx(modern.yaw_radians).epsilon(0.000001F));
    CHECK(legacy.behavior_timer == doctest::Approx(modern.behavior_timer).epsilon(0.000001F));
    CHECK(legacy.animation_time == doctest::Approx(modern.animation_time).epsilon(0.000001F));
    CHECK(legacy.wander_heading == doctest::Approx(modern.wander_heading).epsilon(0.000001F));
    CHECK(legacy.behavior_seed == modern.behavior_seed);
    CHECK(legacy.appearance_seed == modern.appearance_seed);
    CHECK(legacy.behavior_state == modern.behavior_state);
    CHECK(legacy.phase == modern.phase);
    CHECK(legacy.morph_factor == doctest::Approx(modern.morph_factor).epsilon(0.000001F));
    CHECK(legacy.motion_amount == doctest::Approx(modern.motion_amount).epsilon(0.000001F));
    CHECK(legacy.health == doctest::Approx(modern.health).epsilon(0.000001F));
}

void check_same_creatures(
    std::span<const CreatureInstance> legacy,
    std::span<const CreatureInstance> modern) {
    REQUIRE(legacy.size() == modern.size());
    for (std::size_t index = 0; index < legacy.size(); ++index) {
        check_same_creature(legacy[index], modern[index]);
    }
}

void restore_plan_fully(World& world, WorldSavePlan plan) {
    world.begin_restore_save_plan(std::move(plan));
    while (world.has_pending_save_restore()) {
        const auto stats = world.process_save_restore(
            4096U,
            std::numeric_limits<double>::infinity());
    REQUIRE((stats.processed_cells > 0U || !world.has_pending_save_restore()));
    }
}

} // namespace

TEST_CASE("legacy and modern pipelines preserve every logical block material at chunk boundaries") {
    auto legacy = make_differential_world(VisualPipeline::LegacyVoxel);
    auto modern = make_differential_world(VisualPipeline::ModernStylized);
    empty_test_neighborhood(legacy);
    empty_test_neighborhood(modern);

    constexpr BlockId last_block = to_block_id(BlockType::Shovel);
    for (BlockId block_id = 1U; block_id <= last_block; ++block_id) {
        const auto ordinal = static_cast<int>(block_id) - 1;
        const BlockCoord coordinate {
            -18 + ordinal,
            12 + ordinal % 3,
            ordinal % 2 == 0 ? -1 : 16,
        };
        legacy.set_block(coordinate.x, coordinate.y, coordinate.z, block_id);
        modern.set_block(coordinate.x, coordinate.y, coordinate.z, block_id);
        CHECK(legacy.get_block(coordinate.x, coordinate.y, coordinate.z) ==
              modern.get_block(coordinate.x, coordinate.y, coordinate.z));
        CHECK(legacy.water_level(coordinate.x, coordinate.y, coordinate.z) ==
              modern.water_level(coordinate.x, coordinate.y, coordinate.z));
        if (block_id == to_block_id(BlockType::Water)) {
            legacy.set_block(
                coordinate.x,
                coordinate.y,
                coordinate.z,
                to_block_id(BlockType::Air));
            modern.set_block(
                coordinate.x,
                coordinate.y,
                coordinate.z,
                to_block_id(BlockType::Air));
        }
    }

    const std::vector<BlockCoord> water_cells {
        {-16, 28, -16},
        {-1, 29, -1},
        {0, 30, 0},
        {15, 31, 15},
        {16, 32, 16},
    };
    for (std::size_t index = 0; index < water_cells.size(); ++index) {
        const auto& coordinate = water_cells[index];
        const std::vector<BlockCoord> container {
            {coordinate.x, coordinate.y - 1, coordinate.z},
            {coordinate.x - 1, coordinate.y, coordinate.z},
            {coordinate.x + 1, coordinate.y, coordinate.z},
            {coordinate.x, coordinate.y, coordinate.z - 1},
            {coordinate.x, coordinate.y, coordinate.z + 1},
        };
        for (const auto& wall : container) {
            legacy.set_block(
                wall.x,
                wall.y,
                wall.z,
                to_block_id(BlockType::Stone));
            modern.set_block(
                wall.x,
                wall.y,
                wall.z,
                to_block_id(BlockType::Stone));
        }
        legacy.set_block(
            coordinate.x,
            coordinate.y,
            coordinate.z,
            to_block_id(BlockType::Water));
        modern.set_block(
            coordinate.x,
            coordinate.y,
            coordinate.z,
            to_block_id(BlockType::Water));
        CHECK(legacy.water_level(coordinate.x, coordinate.y, coordinate.z) ==
              modern.water_level(coordinate.x, coordinate.y, coordinate.z));
    }

    const std::vector<BlockCoord> edit_trace {
        {-17, 20, -1},
        {-16, 20, -1},
        {-1, 20, -1},
        {0, 20, -1},
        {15, 20, 15},
        {16, 20, 16},
    };
    for (const auto& coordinate : edit_trace) {
        legacy.set_block(coordinate.x, coordinate.y, coordinate.z, to_block_id(BlockType::Stone));
        modern.set_block(coordinate.x, coordinate.y, coordinate.z, to_block_id(BlockType::Stone));
        legacy.set_block(coordinate.x, coordinate.y, coordinate.z, to_block_id(BlockType::Air));
        modern.set_block(coordinate.x, coordinate.y, coordinate.z, to_block_id(BlockType::Air));
        legacy.set_block(coordinate.x, coordinate.y, coordinate.z, to_block_id(BlockType::Cobblestone));
        modern.set_block(coordinate.x, coordinate.y, coordinate.z, to_block_id(BlockType::Cobblestone));
    }

    test::flush_pending_work(legacy);
    test::flush_pending_work(modern);

    for (int z = -17; z <= 17; z += 4) {
        for (int x = -17; x <= 17; x += 4) {
            for (int y = 8; y <= 36; y += 4) {
                CHECK(legacy.get_block(x, y, z) == modern.get_block(x, y, z));
                CHECK(legacy.water_level(x, y, z) == modern.water_level(x, y, z));
                CHECK(legacy.has_water(x, y, z) == modern.has_water(x, y, z));
                CHECK(legacy.get_sky_light(x, y, z) == modern.get_sky_light(x, y, z));
                CHECK(legacy.get_block_light(x, y, z) == modern.get_block_light(x, y, z));
            }
        }
    }

    CHECK(legacy.capture_save_plan().seed == modern.capture_save_plan().seed);
    CHECK(legacy.capture_save_plan().generation_profile ==
          modern.capture_save_plan().generation_profile);
    CHECK(legacy.capture_save_plan().generation_version ==
          modern.capture_save_plan().generation_version);
    CHECK(save_plan_digest(legacy.capture_save_plan()) ==
          save_plan_digest(modern.capture_save_plan()));

    auto restored_legacy = make_differential_world(VisualPipeline::LegacyVoxel);
    auto restored_modern = make_differential_world(VisualPipeline::ModernStylized);
    const auto plan = legacy.capture_save_plan();
    restore_plan_fully(restored_legacy, plan);
    restore_plan_fully(restored_modern, plan);
    CHECK(save_plan_digest(restored_legacy.capture_save_plan()) ==
          save_plan_digest(restored_modern.capture_save_plan()));
}

TEST_CASE("legacy and modern pipelines preserve placement breaking raycasts and collisions") {
    auto legacy = make_differential_world(VisualPipeline::LegacyVoxel);
    auto modern = make_differential_world(VisualPipeline::ModernStylized);
    empty_test_neighborhood(legacy);
    empty_test_neighborhood(modern);
    legacy.set_block(0, 4, -1, to_block_id(BlockType::Stone));
    modern.set_block(0, 4, -1, to_block_id(BlockType::Stone));

    PlayerController legacy_builder({0.5F, 8.001F, -0.5F});
    PlayerController modern_builder({0.5F, 8.001F, -0.5F});
    PlayerInput aim_down {};
    aim_down.look_delta_y = 2000.0F;
    legacy_builder.update(aim_down, 0.0F, legacy);
    modern_builder.update(aim_down, 0.0F, modern);
    check_same_hit(
        legacy_builder.current_target(legacy, 8.0F),
        modern_builder.current_target(modern, 8.0F));

    constexpr BlockId last_block = to_block_id(BlockType::Shovel);
    for (BlockId block_id = 1U; block_id <= last_block; ++block_id) {
        if (!is_placeable_item(block_id)) {
            continue;
        }
        legacy_builder.set_selected_block(block_id);
        modern_builder.set_selected_block(block_id);
        const auto legacy_placed = legacy_builder.try_place_block(legacy, 8.0F);
        const auto modern_placed = modern_builder.try_place_block(modern, 8.0F);
        REQUIRE(legacy_placed.has_value() == modern_placed.has_value());
        REQUIRE(legacy_placed.has_value());
        CHECK(legacy_placed->block == modern_placed->block);
        CHECK(legacy_placed->block_id == modern_placed->block_id);
        CHECK(legacy.get_block(
                  legacy_placed->block.x,
                  legacy_placed->block.y,
                  legacy_placed->block.z) ==
              modern.get_block(
                  modern_placed->block.x,
                  modern_placed->block.y,
                  modern_placed->block.z));
        CHECK(legacy.water_level(
                  legacy_placed->block.x,
                  legacy_placed->block.y,
                  legacy_placed->block.z) ==
              modern.water_level(
                  modern_placed->block.x,
                  modern_placed->block.y,
                  modern_placed->block.z));
        legacy.set_block(
            legacy_placed->block.x,
            legacy_placed->block.y,
            legacy_placed->block.z,
            to_block_id(BlockType::Air));
        modern.set_block(
            modern_placed->block.x,
            modern_placed->block.y,
            modern_placed->block.z,
            to_block_id(BlockType::Air));
    }

    PlayerController legacy_breaker({0.5F, 5.001F, -0.5F});
    PlayerController modern_breaker({0.5F, 5.001F, -0.5F});
    for (BlockId block_id = 1U; block_id <= last_block; ++block_id) {
        if (!is_block_breakable(block_id)) {
            continue;
        }
        const BlockCoord coordinate {0, 5, -1};
        legacy.set_block(coordinate.x, coordinate.y, coordinate.z, block_id);
        modern.set_block(coordinate.x, coordinate.y, coordinate.z, block_id);
        const RaycastHit target {
            true,
            coordinate,
            {0, 6, -1},
            block_id,
            1.0F,
        };
        const auto duration = block_break_duration_seconds(block_id);
        REQUIRE(duration > 0.0F);
        CHECK_FALSE(legacy_breaker.update_block_breaking(
            legacy,
            duration * 0.45F,
            true,
            target).has_value());
        CHECK_FALSE(modern_breaker.update_block_breaking(
            modern,
            duration * 0.45F,
            true,
            target).has_value());
        CHECK(legacy_breaker.block_break_progress().duration_seconds ==
              doctest::Approx(modern_breaker.block_break_progress().duration_seconds));
        CHECK(legacy_breaker.block_break_progress().crack_stage ==
              modern_breaker.block_break_progress().crack_stage);
        const auto legacy_broken = legacy_breaker.update_block_breaking(
            legacy,
            duration,
            true,
            target);
        const auto modern_broken = modern_breaker.update_block_breaking(
            modern,
            duration,
            true,
            target);
        REQUIRE(legacy_broken.has_value() == modern_broken.has_value());
        REQUIRE(legacy_broken.has_value());
        CHECK(legacy_broken->block == modern_broken->block);
        CHECK(legacy_broken->block_id == modern_broken->block_id);
        CHECK(legacy.get_block(coordinate.x, coordinate.y, coordinate.z) ==
              modern.get_block(coordinate.x, coordinate.y, coordinate.z));
    }

    test::make_flat_floor(legacy, -8, 8, 3, -8, 8);
    test::make_flat_floor(modern, -8, 8, 3, -8, 8);
    legacy.set_block(2, 4, 0, to_block_id(BlockType::Stone));
    modern.set_block(2, 4, 0, to_block_id(BlockType::Stone));
    PlayerController legacy_player({0.5F, 4.001F, 0.5F});
    PlayerController modern_player({0.5F, 4.001F, 0.5F});
    for (int tick = 0; tick < 240; ++tick) {
        PlayerInput input {};
        input.move_forward = tick < 90 ? 1.0F : -0.35F;
        input.move_right = tick % 80 < 40 ? 0.45F : -0.45F;
        input.sprint = tick >= 20 && tick < 70;
        input.jump = tick == 12 || tick == 118;
        input.look_delta_x = tick % 17 == 0 ? 0.75F : 0.0F;
        input.look_delta_y = tick % 53 == 0 ? -0.25F : 0.0F;
        legacy_player.update(input, 1.0F / 60.0F, legacy);
        modern_player.update(input, 1.0F / 60.0F, modern);
        check_same_player_state(legacy_player.state(), modern_player.state());
        CHECK(legacy_player.collides_at(legacy, legacy_player.position()) ==
              modern_player.collides_at(modern, modern_player.position()));
        check_same_hit(
            legacy_player.current_target(legacy, 8.0F),
            modern_player.current_target(modern, 8.0F));
    }
}

TEST_CASE("legacy and modern pipelines preserve creatures drops and inventory traces") {
    auto legacy = make_differential_world(VisualPipeline::LegacyVoxel);
    auto modern = make_differential_world(VisualPipeline::ModernStylized);
    empty_test_neighborhood(legacy);
    empty_test_neighborhood(modern);
    test::make_flat_floor(legacy, -8, 8, 4, -8, 8);
    test::make_flat_floor(modern, -8, 8, 4, -8, 8);

    CreatureInstance pig {};
    pig.anchor.chunk = {0, 0};
    pig.anchor.ground_block = {2, 4, 2};
    pig.anchor.spawn_position = {2.5F, 5.001F, 2.5F};
    pig.anchor.species = CreatureSpecies::Pig;
    pig.anchor.roam_radius = 5.0F;
    pig.position = pig.anchor.spawn_position;
    pig.behavior_seed = 991U;
    pig.appearance_seed = 313U;
    pig.health = creature_max_health(pig.anchor.species);

    CreatureSystem legacy_creatures {};
    CreatureSystem modern_creatures {};
    const auto environment = EnvironmentClock::compute_state(0.0F);
    const auto cycle = EnvironmentClock::classify_creature_cycle(0.0F);
    legacy_creatures.load_creatures({pig}, environment);
    modern_creatures.load_creatures({pig}, environment);
    for (int tick = 0; tick < 180; ++tick) {
        const glm::vec3 player_position {
            0.5F + static_cast<float>(tick % 30) * 0.01F,
            5.001F,
            0.5F,
        };
        legacy_creatures.update(
            1.0F / 60.0F,
            legacy,
            player_position,
            environment,
            cycle);
        modern_creatures.update(
            1.0F / 60.0F,
            modern,
            player_position,
            environment,
            cycle);
        check_same_creatures(
            legacy_creatures.active_creatures(),
            modern_creatures.active_creatures());
        REQUIRE(legacy_creatures.recent_attacks().size() ==
                modern_creatures.recent_attacks().size());
    }

    ItemDropSystem legacy_drops {};
    ItemDropSystem modern_drops {};
    legacy_drops.spawn_drop(
        inventory_make_slot(to_block_id(BlockType::Stone), 5U),
        {0.5F, 8.0F, 0.5F},
        {0.25F, 0.0F, -0.1F});
    modern_drops.spawn_drop(
        inventory_make_slot(to_block_id(BlockType::Stone), 5U),
        {0.5F, 8.0F, 0.5F},
        {0.25F, 0.0F, -0.1F});
    legacy_drops.spawn_drop(
        inventory_make_slot(to_block_id(BlockType::Torch), 2U),
        {1.5F, 7.0F, 0.5F},
        {-0.1F, 0.1F, 0.2F});
    modern_drops.spawn_drop(
        inventory_make_slot(to_block_id(BlockType::Torch), 2U),
        {1.5F, 7.0F, 0.5F},
        {-0.1F, 0.1F, 0.2F});

    InventoryMenuState legacy_inventory {};
    InventoryMenuState modern_inventory {};
    HotbarState legacy_hotbar {};
    HotbarState modern_hotbar {};
    for (int tick = 0; tick < 240; ++tick) {
        const auto player_position = tick < 180
                                         ? glm::vec3 {20.0F, 5.001F, 20.0F}
                                         : glm::vec3 {0.8F, 5.001F, 0.5F};
        legacy_drops.update(
            1.0F / 60.0F,
            legacy,
            player_position,
            legacy_inventory,
            legacy_hotbar);
        modern_drops.update(
            1.0F / 60.0F,
            modern,
            player_position,
            modern_inventory,
            modern_hotbar);
        REQUIRE(legacy_drops.drops().size() == modern_drops.drops().size());
        for (std::size_t index = 0; index < legacy_drops.drops().size(); ++index) {
            check_same_drop(
                legacy_drops.drops()[index],
                modern_drops.drops()[index]);
        }
        CHECK(legacy_inventory == modern_inventory);
        CHECK(legacy_hotbar == modern_hotbar);
    }

    CHECK(legacy_drops.active_drop_count() == modern_drops.active_drop_count());
    const auto legacy_audit = legacy_drops.consume_audit_stats();
    const auto modern_audit = modern_drops.consume_audit_stats();
    CHECK(legacy_audit.spawned == modern_audit.spawned);
    CHECK(legacy_audit.merged == modern_audit.merged);
    CHECK(legacy_audit.picked_up == modern_audit.picked_up);
    CHECK(legacy_audit.expired == modern_audit.expired);
    CHECK(legacy_audit.active_drops == modern_audit.active_drops);
}

} // namespace valcraft
