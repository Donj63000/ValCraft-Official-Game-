#include "app/DeathScreen.h"
#include "app/InputBindings.h"
#include "app/Hotbar.h"
#include "app/InventoryMenu.h"
#include "app/PauseMenu.h"
#include "app/GameOptions.h"
#include "app/GameLoop.h"
#include "app/PerformanceReport.h"
#include "render/HotbarLayout.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <string_view>
#include <utility>
#include <vector>

namespace valcraft {

TEST_CASE("game option parser accepts smoke perf flags and values") {
    const std::vector<std::string_view> arguments {
        "--smoke-test",
        "--smoke-frames=120",
        "--hidden-window",
        "--freeze-time",
        "--perf-report",
        "--perf-json=artifacts/run.json",
        "--perf-trace",
        "--perf-scenario=baseline",
        "--disable-shadows",
        "--disable-post-process",
        "--stream-radius=14",
    };

    const auto parsed = parse_game_options(arguments);

    REQUIRE(parsed.ok);
    CHECK(parsed.options.smoke_test);
    CHECK(parsed.options.hidden_window);
    CHECK(parsed.options.freeze_time);
    CHECK(parsed.options.smoke_frames == 120);
    CHECK(parsed.options.performance.report_frame_stats);
    CHECK(parsed.options.performance.perf_json_path == "artifacts/run.json");
    CHECK(parsed.options.performance.perf_trace_enabled);
    CHECK(parsed.options.performance.perf_scenario == "baseline");
    CHECK_FALSE(parsed.options.performance.shadows_enabled);
    CHECK_FALSE(parsed.options.performance.post_process_enabled);
    CHECK(parsed.options.performance.stream_radius == 14);
}

TEST_CASE("game option parser rejects smoke-only perf flags outside smoke mode") {
    const std::vector<std::string_view> arguments {
        "--perf-json=artifacts/run.json",
        "--perf-trace",
    };

    const auto parsed = parse_game_options(arguments);

    CHECK_FALSE(parsed.ok);
    CHECK(parsed.error_message.find("require --smoke-test") != std::string::npos);
}

TEST_CASE("default performance options favor lighter frame pacing defaults") {
    const PerformanceOptions options {};

    CHECK(options.chunk_generation_budget == 2);
    CHECK(options.mesh_rebuild_budget == 4);
    CHECK(options.light_node_budget == 8192);
    CHECK(options.max_generation_ms == doctest::Approx(1.0));
    CHECK(options.max_lighting_ms == doctest::Approx(1.5));
    CHECK(options.max_meshing_ms == doctest::Approx(2.0));
    CHECK(options.stream_radius == 5);
    CHECK(options.shadows_enabled);
    CHECK(options.shadow_map_size == 1024);
    CHECK_FALSE(options.post_process_enabled);
}

TEST_CASE("dominant stage detection chooses the largest measured subsystem") {
    FramePerformanceSample sample {};
    sample.streaming_ms = 1.0;
    sample.generation_ms = 2.5;
    sample.lighting_ms = 0.5;
    sample.meshing_ms = 4.0;
    sample.upload_ms = 3.0;

    CHECK(detect_dominant_stage(sample) == PerformanceStage::Meshing);
}

TEST_CASE("performance report builds lag buckets spike windows and percentiles") {
    PerformanceReportMetadata metadata {};
    metadata.platform = "windows";
    metadata.build_type = "RelWithDebInfo";
    metadata.smoke_frames = 6;
    metadata.stream_radius = 10;
    metadata.shadows_enabled = true;
    metadata.freeze_time = true;
    metadata.scenario = "baseline";

    std::vector<FramePerformanceSample> samples {
        {0, 10.0, 1.0, 1.0, 1.0, 2.0, 0.5, 0.0, 0.5, 1, 1, 10, 1, 0, 0, 0, 0, 0, 0, 0, 1, 40, 0, 40, PerformanceStage::Unattributed},
        {1, 20.0, 1.0, 1.0, 1.0, 9.0, 0.5, 0.0, 0.5, 2, 2, 20, 2, 1, 0, 0, 0, 2, 0, 0, 1, 42, 0, 42, PerformanceStage::Unattributed},
        {2, 25.0, 1.0, 1.0, 6.0, 5.0, 0.5, 0.0, 0.5, 3, 2, 30, 2, 2, 1, 0, 0, 2, 0, 0, 1, 44, 0, 44, PerformanceStage::Unattributed},
        {3, 12.0, 1.0, 1.0, 1.0, 2.0, 0.5, 0.0, 0.5, 1, 1, 10, 1, 0, 0, 0, 0, 0, 0, 0, 1, 40, 0, 40, PerformanceStage::Unattributed},
        {4, 40.0, 1.0, 12.0, 1.0, 4.0, 0.5, 0.0, 0.5, 4, 3, 40, 3, 3, 1, 1, 1, 4, 1, 0, 1, 50, 0, 50, PerformanceStage::Unattributed},
        {5, 55.0, 1.0, 4.0, 1.0, 3.0, 12.0, 0.0, 0.5, 4, 4, 50, 4, 2, 1, 1, 1, 4, 1, 0, 1, 60, 0, 60, PerformanceStage::Unattributed},
    };

    const auto report = build_performance_report(metadata, samples, true, 3);

    CHECK(report.summary.frame_count == 6);
    CHECK(report.summary.frame_total_ms.p50 == doctest::Approx(22.5));
    CHECK(report.summary.frame_total_ms.p95 == doctest::Approx(51.25));
    CHECK(report.summary.lag_buckets.over_16_7_ms == 4);
    CHECK(report.summary.lag_buckets.over_33_3_ms == 2);
    CHECK(report.summary.lag_buckets.over_50_0_ms == 1);
    REQUIRE(report.spike_windows.size() == 2);
    CHECK(report.spike_windows[0].start_frame == 1);
    CHECK(report.spike_windows[0].end_frame == 2);
    CHECK(report.spike_windows[0].peak_frame == 2);
    CHECK(report.spike_windows[0].dominant_stage == PerformanceStage::Lighting);
    CHECK(report.spike_windows[1].start_frame == 4);
    CHECK(report.spike_windows[1].peak_frame == 5);
    CHECK(report.hotspots.worst_frame_stage == PerformanceStage::Upload);
    CHECK(report.worst_frames.size() == 3);
    CHECK(report.worst_frames.front().frame_index == 5);
    CHECK(report.frames.size() == 6);
}

TEST_CASE("performance report JSON includes schema metadata hotspots and optional frames") {
    PerformanceReportMetadata metadata {};
    metadata.platform = "windows";
    metadata.build_type = "RelWithDebInfo";
    metadata.smoke_frames = 2;
    metadata.stream_radius = 10;
    metadata.shadows_enabled = false;
    metadata.post_process_enabled = false;
    metadata.freeze_time = true;
    metadata.scenario = "no_shadows";

    std::vector<FramePerformanceSample> samples {
        {0, 8.0, 0.5, 1.0, 1.0, 2.0, 0.1, 0.0, 0.5, 1, 1, 8, 1, 0, 0, 0, 0, 0, 0, 0, 1, 32, 0, 32, PerformanceStage::Unattributed},
        {1, 18.0, 0.5, 3.0, 1.0, 2.0, 0.1, 0.0, 0.5, 2, 2, 18, 2, 1, 0, 0, 0, 1, 0, 0, 1, 36, 0, 36, PerformanceStage::Unattributed},
    };

    const auto report = build_performance_report(metadata, samples, true);
    const auto json = format_performance_json(report);

    CHECK(json.find("\"schema_version\": 1") != std::string::npos);
    CHECK(json.find("\"scenario\": \"no_shadows\"") != std::string::npos);
    CHECK(json.find("\"post_process_enabled\": false") != std::string::npos);
    CHECK(json.find("\"hotspots\"") != std::string::npos);
    CHECK(json.find("\"worst_frames\"") != std::string::npos);
    CHECK(json.find("\"spike_windows\"") != std::string::npos);
    CHECK(json.find("\"frames\": [") != std::string::npos);
}

TEST_CASE("smoke mode uses a deterministic fixed simulation step") {
    const auto fixed_step = std::chrono::duration<double>(1.0 / 60.0);
    const auto measured = std::chrono::duration<double>(0.001);

    CHECK(resolve_simulation_frame_time(true, measured, fixed_step) == fixed_step);
}

TEST_CASE("normal mode clamps very large frame times without forcing the fixed step") {
    const auto fixed_step = std::chrono::duration<double>(1.0 / 60.0);
    const auto measured = std::chrono::duration<double>(0.5);
    const auto resolved = resolve_simulation_frame_time(false, measured, fixed_step);

    CHECK(resolved.count() == doctest::Approx(0.25));
    CHECK(resolved != fixed_step);
}

TEST_CASE("movement input helper reads the physical strafe cluster consistently") {
    std::array<Uint8, SDL_NUM_SCANCODES> keys {};

    keys[SDL_SCANCODE_D] = 1;
    auto input = read_player_movement_input(keys.data());
    CHECK(input.move_right == doctest::Approx(1.0F));
    CHECK(input.move_forward == doctest::Approx(0.0F));

    keys.fill(0);
    keys[SDL_SCANCODE_A] = 1;
    input = read_player_movement_input(keys.data());
    CHECK(input.move_right == doctest::Approx(-1.0F));

    keys.fill(0);
    keys[SDL_SCANCODE_W] = 1;
    keys[SDL_SCANCODE_SPACE] = 1;
    input = read_player_movement_input(keys.data());
    CHECK(input.move_forward == doctest::Approx(1.0F));
    CHECK(input.jump);
}

TEST_CASE("drop action key follows the physical q position to avoid azerty movement conflicts") {
    SDL_Keysym qwerty_drop {};
    qwerty_drop.sym = SDLK_q;
    qwerty_drop.scancode = SDL_SCANCODE_Q;
    CHECK(is_drop_action_key(qwerty_drop));

    SDL_Keysym azerty_drop {};
    azerty_drop.sym = SDLK_a;
    azerty_drop.scancode = SDL_SCANCODE_Q;
    CHECK(is_drop_action_key(azerty_drop));

    SDL_Keysym azerty_move_left {};
    azerty_move_left.sym = SDLK_q;
    azerty_move_left.scancode = SDL_SCANCODE_A;
    CHECK_FALSE(is_drop_action_key(azerty_move_left));
}

TEST_CASE("default hotbar exposes starter stacks and an empty hand slot") {
    const auto hotbar = make_default_hotbar_state();

    CHECK(hotbar.slots.size() == kHotbarSlotCount);
    CHECK(hotbar.selected_index == 0);
    CHECK(hotbar.slots[0].block_id == to_block_id(BlockType::Grass));
    CHECK(hotbar.slots[0].count == 32);
    CHECK(hotbar.slots[3].block_id == to_block_id(BlockType::Cobblestone));
    CHECK(hotbar.slots[3].count == 32);
    CHECK(hotbar.slots[5].block_id == to_block_id(BlockType::Planks));
    CHECK(hotbar.slots[5].count == 32);
    CHECK(hotbar.slots[6].block_id == to_block_id(BlockType::Torch));
    CHECK(hotbar.slots[6].count == 16);
    CHECK(hotbar.slots[7].block_id == to_block_id(BlockType::Water));
    CHECK(hotbar.slots[7].count == 8);
    CHECK(hotbar.slots[8].block_id == to_block_id(BlockType::Air));
    CHECK(hotbar.slots[8].count == 0);
}

TEST_CASE("hotbar selection supports number keys and mouse wheel wrap") {
    HotbarState hotbar = make_default_hotbar_state();

    CHECK(hotbar_index_from_number_key(1) == 0);
    CHECK(hotbar_index_from_number_key(9) == 8);
    CHECK_FALSE(hotbar_index_from_number_key(0).has_value());

    select_hotbar_index(hotbar, 6);
    CHECK(selected_hotbar_block(hotbar) == to_block_id(BlockType::Torch));

    select_hotbar_index(hotbar, 8);
    cycle_hotbar_selection(hotbar, 1);
    CHECK(hotbar.selected_index == 0);

    cycle_hotbar_selection(hotbar, -1);
    CHECK(hotbar.selected_index == 8);
    CHECK(selected_hotbar_block(hotbar) == to_block_id(BlockType::Air));
}

TEST_CASE("hotbar layout stays centered and anchored to the bottom across resolutions") {
    auto hotbar = make_default_hotbar_state();
    select_hotbar_index(hotbar, 6);

    const std::array<std::pair<int, int>, 3> viewports {{{1600, 900}, {960, 540}, {480, 320}}};
    for (const auto& [width, height] : viewports) {
        const auto layout = build_hotbar_layout(width, height, hotbar);

        CHECK(layout.slots.size() == kHotbarSlotCount);
        CHECK(layout.bar_left + layout.bar_width * 0.5F == doctest::Approx(static_cast<float>(width) * 0.5F));
        CHECK(layout.bar_bottom == doctest::Approx(layout.safe_margin));
        CHECK(layout.bar_bottom < static_cast<float>(height) * 0.5F);
    }

    const auto wide_layout = build_hotbar_layout(1600, 900, hotbar);
    CHECK(wide_layout.slots[6].is_selected);
    CHECK(wide_layout.slots[6].has_icon);
    CHECK(wide_layout.slots[6].icon_tile == HotbarAtlasTile {0, 3});
    CHECK(wide_layout.slots[7].has_icon);
    CHECK(wide_layout.slots[7].icon_tile == HotbarAtlasTile {7, 2});
    CHECK_FALSE(wide_layout.slots[8].has_icon);
}

TEST_CASE("gameplay hud layout keeps the gameplay cluster out of the top half across resolutions") {
    auto hotbar = make_default_hotbar_state();
    select_hotbar_index(hotbar, 6);

    const std::array<std::pair<int, int>, 3> viewports {{{1600, 900}, {960, 540}, {480, 320}}};
    for (const auto& [width, height] : viewports) {
        const auto layout = build_gameplay_hud_layout(width, height, hotbar, 20.0F, 20.0F, 10.0F, 10.0F, false);

        CHECK(layout.hotbar.bar_left + layout.hotbar.bar_width * 0.5F == doctest::Approx(static_cast<float>(width) * 0.5F));
        CHECK(layout.hotbar.bar_bottom >= layout.safe_margin);
        CHECK(layout.hotbar_panel_bottom < static_cast<float>(height) * 0.5F);
        CHECK(layout.cluster_bottom < static_cast<float>(height) * 0.5F);
        CHECK(layout.cluster_top < static_cast<float>(height) * 0.5F);
        CHECK(layout.vitals_bottom > layout.hotbar_top);
        CHECK(layout.label.center_x == doctest::Approx(static_cast<float>(width) * 0.5F));
        CHECK(gameplay_hud_label_top(layout.label) == doctest::Approx(layout.cluster_top));
    }
}

TEST_CASE("gameplay hud layout places hearts left bubbles right and only flags air row when needed") {
    auto hotbar = make_default_hotbar_state();
    select_hotbar_index(hotbar, 0);

    const auto dry_layout = build_gameplay_hud_layout(1600, 900, hotbar, 17.0F, 20.0F, 10.0F, 10.0F, false);
    CHECK_FALSE(dry_layout.air_visible);
    CHECK(dry_layout.hearts.front().bottom > dry_layout.hotbar_top);
    CHECK(dry_layout.hearts.back().x < dry_layout.label.center_x);
    CHECK(dry_layout.slots[0].bottom > dry_layout.slots[1].bottom);
    CHECK(dry_layout.slots[0].count_bottom > dry_layout.slots[0].bottom);
    CHECK_FALSE(dry_layout.slots[8].has_icon);
    CHECK(dry_layout.label.center_x == doctest::Approx(dry_layout.hotbar.bar_left + dry_layout.hotbar.bar_width * 0.5F));
    CHECK(dry_layout.label.bottom > gameplay_hud_vital_top(dry_layout.hearts.front()));

    const auto underwater_layout = build_gameplay_hud_layout(1600, 900, hotbar, 17.0F, 20.0F, 6.5F, 10.0F, true);
    CHECK(underwater_layout.air_visible);
    CHECK(underwater_layout.bubbles.front().bottom == doctest::Approx(underwater_layout.vitals_bottom));
    CHECK(underwater_layout.bubbles.front().x > underwater_layout.label.center_x);
    CHECK(underwater_layout.label.bottom > gameplay_hud_vital_top(underwater_layout.bubbles.front()));
}

TEST_CASE("vital glyph mapping resolves full half and empty states for hearts and bubbles") {
    const auto full_hearts = build_vital_glyph_fills<kHudVitalGlyphCount>(20.0F, 20.0F);
    CHECK(std::all_of(full_hearts.begin(), full_hearts.end(), [](HudGlyphFill fill) { return fill == HudGlyphFill::Full; }));

    const auto wounded_hearts = build_vital_glyph_fills<kHudVitalGlyphCount>(19.0F, 20.0F);
    CHECK(std::count(wounded_hearts.begin(), wounded_hearts.end(), HudGlyphFill::Full) == 9);
    CHECK(std::count(wounded_hearts.begin(), wounded_hearts.end(), HudGlyphFill::Half) == 1);
    CHECK(std::count(wounded_hearts.begin(), wounded_hearts.end(), HudGlyphFill::Empty) == 0);

    const auto mid_air = build_vital_glyph_fills<kHudVitalGlyphCount>(6.5F, 10.0F);
    CHECK(std::count(mid_air.begin(), mid_air.end(), HudGlyphFill::Full) == 6);
    CHECK(std::count(mid_air.begin(), mid_air.end(), HudGlyphFill::Half) == 1);
    CHECK(std::count(mid_air.begin(), mid_air.end(), HudGlyphFill::Empty) == 3);

    const auto empty_row = build_vital_glyph_fills<kHudVitalGlyphCount>(0.0F, 10.0F);
    CHECK(std::all_of(empty_row.begin(), empty_row.end(), [](HudGlyphFill fill) { return fill == HudGlyphFill::Empty; }));
}

TEST_CASE("gameplay hud slot mapping keeps empty slots and stack counters stable") {
    HotbarState hotbar {};
    hotbar.slots[0] = make_item_stack(to_block_id(BlockType::Stone), 1);
    hotbar.slots[1] = make_item_stack(to_block_id(BlockType::Stone), 12);
    hotbar.slots[2] = empty_item_stack();
    select_hotbar_index(hotbar, 1);

    const auto layout = build_gameplay_hud_layout(960, 540, hotbar, 20.0F, 20.0F, 10.0F, 10.0F, false);

    CHECK(layout.slots[0].has_icon);
    CHECK_FALSE(layout.slots[0].show_stack_count);
    CHECK(layout.slots[1].has_icon);
    CHECK(layout.slots[1].show_stack_count);
    CHECK(layout.slots[1].count_bottom > layout.slots[1].bottom);
    CHECK_FALSE(layout.slots[2].has_icon);
    CHECK_FALSE(layout.slots[2].show_stack_count);
    CHECK_FALSE(gameplay_hud_stack_count_visible(hotbar.slots[0]));
    CHECK(gameplay_hud_stack_count_visible(hotbar.slots[1]));
    CHECK_FALSE(gameplay_hud_stack_count_visible(hotbar.slots[2]));
}

TEST_CASE("pause menu layout stays centered and resolves hovered enabled buttons") {
    PauseMenuState state {};
    state.visible = true;
    state.selected_action = PauseMenuAction::Resume;

    const auto base_layout = build_pause_menu_layout(1600, 900, state);
    state.cursor_x = base_layout.buttons[2].x + base_layout.buttons[2].width * 0.5F;
    state.cursor_y = base_layout.buttons[2].y + base_layout.buttons[2].height * 0.5F;

    const auto layout = build_pause_menu_layout(1600, 900, state);

    CHECK(layout.panel_x + layout.panel_width * 0.5F == doctest::Approx(800.0F));
    CHECK(layout.buttons[0].label == "REPRENDRE");
    CHECK_FALSE(layout.buttons[1].enabled);
    CHECK(layout.buttons[2].hovered);
    CHECK(layout.buttons[2].selected);

    const auto action = pause_menu_action_at(layout, state.cursor_x, state.cursor_y);
    REQUIRE(action.has_value());
    CHECK(*action == PauseMenuAction::Quit);
}

TEST_CASE("pause menu keyboard navigation skips disabled options and wraps") {
    CHECK(next_pause_menu_action(PauseMenuAction::Resume, 1) == PauseMenuAction::Quit);
    CHECK(next_pause_menu_action(PauseMenuAction::Quit, 1) == PauseMenuAction::Resume);
    CHECK(next_pause_menu_action(PauseMenuAction::Resume, -1) == PauseMenuAction::Quit);
    CHECK(next_pause_menu_action(PauseMenuAction::Quit, -1) == PauseMenuAction::Resume);
}

TEST_CASE("pause menu layout stays centered on compact viewports") {
    PauseMenuState state {};
    state.visible = true;

    const auto layout = build_pause_menu_layout(280, 220, state);

    CHECK(layout.panel_x + layout.panel_width * 0.5F == doctest::Approx(140.0F));
    CHECK(layout.panel_y + layout.panel_height * 0.5F == doctest::Approx(110.0F));
}

TEST_CASE("death screen layout stays centered and resolves hovered respawn action") {
    DeathScreenState state {};
    state.visible = true;
    state.cause = PlayerDeathCause::Drowning;
    state.selected_action = DeathScreenAction::Respawn;

    const auto base_layout = build_death_screen_layout(1600, 900, state);
    state.cursor_x = base_layout.buttons[0].x + base_layout.buttons[0].width * 0.5F;
    state.cursor_y = base_layout.buttons[0].y + base_layout.buttons[0].height * 0.5F;

    const auto layout = build_death_screen_layout(1600, 900, state);

    CHECK(layout.panel_x + layout.panel_width * 0.5F == doctest::Approx(800.0F));
    CHECK(layout.buttons[0].hovered);
    CHECK(layout.buttons[0].selected);
    CHECK(death_screen_cause_label(state.cause) == "CAUSE NOYADE");

    const auto action = death_screen_action_at(layout, state.cursor_x, state.cursor_y);
    REQUIRE(action.has_value());
    CHECK(*action == DeathScreenAction::Respawn);
}

TEST_CASE("death screen keyboard navigation wraps between actions") {
    CHECK(next_death_screen_action(DeathScreenAction::Respawn, 1) == DeathScreenAction::Quit);
    CHECK(next_death_screen_action(DeathScreenAction::Quit, 1) == DeathScreenAction::Respawn);
    CHECK(next_death_screen_action(DeathScreenAction::Respawn, -1) == DeathScreenAction::Quit);
}

TEST_CASE("default inventory menu exposes a populated storage and an empty carried slot") {
    const auto inventory = make_default_inventory_menu_state();

    CHECK(inventory.storage_slots.size() == kInventoryStorageSlotCount);
    CHECK(inventory_slot_has_item(inventory.storage_slots[0]));
    CHECK(inventory.storage_slots[0].block_id == to_block_id(BlockType::Wood));
    CHECK(inventory.storage_slots[0].count == 16);
    CHECK(inventory_slot_has_item(inventory.storage_slots[13]));
    CHECK(inventory.storage_slots[13].block_id == to_block_id(BlockType::Torch));
    CHECK(inventory.storage_slots[13].count == 16);
    CHECK_FALSE(inventory_slot_has_item(inventory.storage_slots.back()));
    CHECK_FALSE(inventory.carrying_item);
    CHECK_FALSE(inventory_slot_has_item(inventory.carried_slot));
}

TEST_CASE("inventory layout stays centered and resolves hovered storage and hotbar slots") {
    auto hotbar = make_default_hotbar_state();
    auto inventory = make_default_inventory_menu_state();
    inventory.visible = true;

    auto layout = build_inventory_menu_layout(1600, 900, inventory, hotbar);
    inventory.cursor_x = layout.slots[4].x + layout.slots[4].size * 0.5F;
    inventory.cursor_y = layout.slots[4].y + layout.slots[4].size * 0.5F;

    layout = build_inventory_menu_layout(1600, 900, inventory, hotbar);

    CHECK(layout.panel_x + layout.panel_width * 0.5F == doctest::Approx(800.0F));
    CHECK(layout.slots[4].hovered);
    CHECK(layout.slots[kInventoryStorageSlotCount + 7].is_hotbar);
    CHECK(layout.slots[kInventoryStorageSlotCount + hotbar.selected_index].is_selected_hotbar);

    const auto hovered = inventory_slot_at(layout, inventory.cursor_x, inventory.cursor_y);
    REQUIRE(hovered.has_value());
    CHECK(hovered->group == InventorySlotGroup::Storage);
    CHECK(hovered->index == 4);
}

TEST_CASE("inventory layout stays centered on compact viewports") {
    const auto hotbar = make_default_hotbar_state();
    const auto inventory = make_default_inventory_menu_state();

    const auto layout = build_inventory_menu_layout(520, 320, inventory, hotbar);

    CHECK(layout.panel_x + layout.panel_width * 0.5F == doctest::Approx(260.0F));
    CHECK(layout.panel_y + layout.panel_height * 0.5F == doctest::Approx(160.0F));
}

TEST_CASE("inventory primary click can pick and swap full stacks between storage and hotbar") {
    auto hotbar = make_default_hotbar_state();
    auto inventory = make_default_inventory_menu_state();

    inventory_pick_or_swap(inventory, hotbar, {InventorySlotGroup::Storage, 0});
    CHECK(inventory.carrying_item);
    CHECK(inventory.carried_slot.block_id == to_block_id(BlockType::Wood));
    CHECK(inventory.carried_slot.count == 16);
    CHECK_FALSE(inventory_slot_has_item(inventory.storage_slots[0]));

    inventory_pick_or_swap(inventory, hotbar, {InventorySlotGroup::Hotbar, 1});
    CHECK(inventory.carrying_item);
    CHECK(inventory.carried_slot.block_id == to_block_id(BlockType::Dirt));
    CHECK(inventory.carried_slot.count == 32);
    CHECK(hotbar.slots[1].block_id == to_block_id(BlockType::Wood));
    CHECK(hotbar.slots[1].count == 16);
}

TEST_CASE("inventory primary click merges matching stacks up to 64") {
    HotbarState hotbar {};
    hotbar.slots[0] = inventory_make_slot(to_block_id(BlockType::Stone), 60);
    InventoryMenuState inventory {};
    inventory.storage_slots[0] = inventory_make_slot(to_block_id(BlockType::Stone), 8);

    inventory_primary_click(inventory, hotbar, {InventorySlotGroup::Storage, 0});
    REQUIRE(inventory.carrying_item);
    CHECK(inventory.carried_slot.count == 8);

    inventory_primary_click(inventory, hotbar, {InventorySlotGroup::Hotbar, 0});
    CHECK(hotbar.slots[0].count == 64);
    CHECK(inventory.carrying_item);
    CHECK(inventory.carried_slot.block_id == to_block_id(BlockType::Stone));
    CHECK(inventory.carried_slot.count == 4);
}

TEST_CASE("inventory secondary click splits stacks and places single items") {
    HotbarState hotbar {};
    InventoryMenuState inventory {};
    inventory.storage_slots[0] = inventory_make_slot(to_block_id(BlockType::Wood), 9);

    inventory_secondary_click(inventory, hotbar, {InventorySlotGroup::Storage, 0});
    REQUIRE(inventory.carrying_item);
    CHECK(inventory.carried_slot.block_id == to_block_id(BlockType::Wood));
    CHECK(inventory.carried_slot.count == 5);
    CHECK(inventory.storage_slots[0].count == 4);

    inventory_secondary_click(inventory, hotbar, {InventorySlotGroup::Hotbar, 2});
    CHECK(hotbar.slots[2].block_id == to_block_id(BlockType::Wood));
    CHECK(hotbar.slots[2].count == 1);
    CHECK(inventory.carried_slot.count == 4);

    inventory_secondary_click(inventory, hotbar, {InventorySlotGroup::Hotbar, 2});
    CHECK(hotbar.slots[2].count == 2);
    CHECK(inventory.carried_slot.count == 3);
}

TEST_CASE("inventory pickup helper fills matching stacks before using empty slots") {
    HotbarState hotbar {};
    hotbar.slots[0] = inventory_make_slot(to_block_id(BlockType::Stone), 63);
    InventoryMenuState inventory {};

    const auto leftover = inventory_try_store_stack(inventory, hotbar, inventory_make_slot(to_block_id(BlockType::Stone), 4));

    CHECK_FALSE(inventory_slot_has_item(leftover));
    CHECK(hotbar.slots[0].count == 64);
    CHECK(hotbar.slots[1].block_id == to_block_id(BlockType::Stone));
    CHECK(hotbar.slots[1].count == 3);
}

TEST_CASE("inventory number key swap can move a hovered stack into an empty hotbar slot") {
    auto hotbar = make_default_hotbar_state();
    auto inventory = make_default_inventory_menu_state();

    REQUIRE_FALSE(inventory_slot_has_item(hotbar.slots[8]));
    REQUIRE(hotbar.slots[8].block_id == to_block_id(BlockType::Air));

    inventory_swap_with_hotbar(inventory, hotbar, {InventorySlotGroup::Storage, 13}, 8);

    CHECK(hotbar.slots[8].block_id == to_block_id(BlockType::Torch));
    CHECK(hotbar.slots[8].count == 16);
    CHECK_FALSE(inventory_slot_has_item(inventory.storage_slots[13]));
}

} // namespace valcraft
