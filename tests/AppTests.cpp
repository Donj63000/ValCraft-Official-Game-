#include "app/Hotbar.h"
#include "app/GameOptions.h"
#include "app/GameLoop.h"
#include "app/PerformanceReport.h"
#include "render/HotbarLayout.h"

#include <doctest/doctest.h>

#include <string_view>
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

TEST_CASE("default hotbar contains nine slots with torch and utility empties") {
    const auto hotbar = make_default_hotbar_state();

    CHECK(hotbar.slots.size() == kHotbarSlotCount);
    CHECK(hotbar.selected_index == 0);
    CHECK(hotbar.slots[0].block_id == to_block_id(BlockType::Grass));
    CHECK(hotbar.slots[6].block_id == to_block_id(BlockType::Torch));
    CHECK_FALSE(hotbar.slots[6].is_empty_utility);
    CHECK(hotbar.slots[7].block_id == to_block_id(BlockType::Air));
    CHECK(hotbar.slots[7].is_empty_utility);
    CHECK(hotbar.slots[8].block_id == to_block_id(BlockType::Air));
    CHECK(hotbar.slots[8].is_empty_utility);
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

TEST_CASE("hotbar layout stays centered and keeps torch and utility visuals distinct") {
    auto hotbar = make_default_hotbar_state();
    select_hotbar_index(hotbar, 6);

    const auto layout = build_hotbar_layout(1600, 900, hotbar);

    CHECK(layout.slots.size() == kHotbarSlotCount);
    CHECK(layout.bar_left + layout.bar_width * 0.5F == doctest::Approx(800.0F));
    CHECK(layout.bar_bottom >= layout.safe_margin);
    CHECK(layout.slots[6].is_selected);
    CHECK(layout.slots[6].has_icon);
    CHECK(layout.slots[6].icon_tile == HotbarAtlasTile {0, 2});
    CHECK_FALSE(layout.slots[7].has_icon);
    CHECK_FALSE(layout.slots[8].has_icon);
}

} // namespace valcraft
