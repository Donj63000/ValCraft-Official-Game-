#include "app/ConfirmDialog.h"
#include "app/DeathScreen.h"
#include "app/GameBranding.h"
#include "app/GameOptions.h"
#include "app/GameLoop.h"
#include "app/Hotbar.h"
#include "app/InputBindings.h"
#include "app/InventoryMenu.h"
#include "app/MainMenu.h"
#include "app/OptionsMenu.h"
#include "app/PauseMenu.h"
#include "app/PerformanceReport.h"
#include "app/SaveGame.h"
#include "app/SaveSlotMenu.h"
#include "render/HotbarLayout.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <string_view>
#include <sstream>
#include <type_traits>
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

TEST_CASE("game option parser rejects non finite time and unsafe streaming radius") {
    const std::vector<std::string_view> nan_time {"--initial-time=nan"};
    const auto parsed_nan_time = parse_game_options(nan_time);
    CHECK_FALSE(parsed_nan_time.ok);

    const std::vector<std::string_view> infinite_time {"--initial-time=inf"};
    const auto parsed_infinite_time = parse_game_options(infinite_time);
    CHECK_FALSE(parsed_infinite_time.ok);

    const std::vector<std::string_view> oversized_stream_radius {"--stream-radius=2147483647"};
    const auto parsed_stream_radius = parse_game_options(oversized_stream_radius);
    CHECK_FALSE(parsed_stream_radius.ok);
}

TEST_CASE("game option parser accepts perf capture flags outside smoke mode") {
    const std::vector<std::string_view> arguments {
        "--perf-report",
        "--perf-json=artifacts/run.json",
        "--perf-trace",
        "--perf-scenario=interactive_session",
    };

    const auto parsed = parse_game_options(arguments);

    REQUIRE(parsed.ok);
    CHECK_FALSE(parsed.options.smoke_test);
    CHECK(parsed.options.performance.report_frame_stats);
    CHECK(parsed.options.performance.perf_json_path == "artifacts/run.json");
    CHECK(parsed.options.performance.perf_trace_enabled);
    CHECK(parsed.options.performance.perf_scenario == "interactive_session");
}

TEST_CASE("gameplay action keys use physical scancodes") {
    SDL_Keysym drop_key {};
    drop_key.sym = SDLK_q;
    drop_key.scancode = SDL_SCANCODE_Q;
    CHECK(is_drop_action_key(drop_key));

    SDL_Keysym super_vision_key {};
    super_vision_key.sym = SDLK_v;
    super_vision_key.scancode = SDL_SCANCODE_V;
    CHECK(is_super_vision_action_key(super_vision_key));

    SDL_Keysym flight_key {};
    flight_key.sym = SDLK_f;
    flight_key.scancode = SDL_SCANCODE_F;
    CHECK(is_flight_action_key(flight_key));

    SDL_Keysym remapped_v_key {};
    remapped_v_key.sym = SDLK_v;
    remapped_v_key.scancode = SDL_SCANCODE_B;
    CHECK_FALSE(is_super_vision_action_key(remapped_v_key));

    SDL_Keysym remapped_f_key {};
    remapped_f_key.sym = SDLK_f;
    remapped_f_key.scancode = SDL_SCANCODE_G;
    CHECK_FALSE(is_flight_action_key(remapped_f_key));
}

TEST_CASE("game option parser enables audit only when audit or perf capture flags are present") {
    const std::vector<std::string_view> default_arguments {
        "--hidden-window",
        "--freeze-time",
    };

    const auto default_parse = parse_game_options(default_arguments);

    REQUIRE(default_parse.ok);
    CHECK_FALSE(default_parse.options.audit.enabled);
    CHECK(default_parse.options.raw_arguments.size() == default_arguments.size());

    const std::vector<std::string_view> audit_arguments {
        "--audit",
        "--audit-mode=forensic",
        "--audit-dir=performancesaudit/custom",
        "--audit-label=manual block break",
        "--audit-trace-frames",
    };

    const auto audit_parse = parse_game_options(audit_arguments);

    REQUIRE(audit_parse.ok);
    CHECK(audit_parse.options.audit.enabled);
    CHECK(audit_parse.options.audit.mode == AuditMode::Forensic);
    CHECK(audit_parse.options.audit.root_directory == std::filesystem::path("performancesaudit/custom"));
    CHECK(audit_parse.options.audit.label == "manual block break");
    CHECK(audit_parse.options.audit.trace_frames);
    CHECK(audit_parse.options.raw_arguments.size() == audit_arguments.size());
}

TEST_CASE("audit helpers sanitize labels and build run paths under performancesaudit runs") {
    AuditOptions options {};
    options.enabled = true;
    options.mode = AuditMode::Forensic;
    options.root_directory = std::filesystem::path("performancesaudit");
    options.label = "manual block/break.v1";

    CHECK(sanitize_audit_label("manual block/break.v1") == "manual-block-break-v1");
    CHECK(sanitize_audit_label("   ") == "interactive");

    const auto started_at = std::chrono::system_clock::from_time_t(1712185200);
    const auto paths = make_audit_run_paths(options, "session-id", started_at);
    const auto run_name = paths.run_directory.filename().string();

    CHECK(paths.root_directory == std::filesystem::absolute(options.root_directory));
    CHECK(paths.run_directory.parent_path() == paths.root_directory / "runs");
    CHECK(run_name.find("-forensic-manual-block-break-v1") != std::string::npos);
    CHECK(paths.manifest_path == paths.run_directory / "manifest.json");
    CHECK(paths.summary_json_path == paths.run_directory / "summary.json");
    CHECK(paths.summary_text_path == paths.run_directory / "summary.txt");
    CHECK(paths.events_path == paths.run_directory / "events.jsonl");
    CHECK(paths.seconds_path == paths.run_directory / "seconds.jsonl");
    CHECK(paths.frames_path == paths.run_directory / "frames.jsonl");
    CHECK(paths.spikes_path == paths.run_directory / "spikes.json");
}

TEST_CASE("audit recorder stays opt in and writes no files when disabled") {
    const auto unique_suffix = std::to_string(static_cast<unsigned long long>(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto audit_root = std::filesystem::temp_directory_path() / ("valcraft-audit-disabled-" + unique_suffix);
    std::filesystem::remove_all(audit_root);

    AuditOptions options {};
    options.enabled = false;
    options.root_directory = audit_root;

    AuditStartContext start_context {};
    start_context.working_directory = audit_root;

    {
        AuditRecorder recorder(options, start_context);
        CHECK_FALSE(recorder.enabled());
        recorder.finalize(AuditFinalizeContext {});
    }

    CHECK_FALSE(std::filesystem::exists(audit_root));
}

TEST_CASE("window icon path resolution prefers working directory then executable directory then build root") {
    const auto unique_suffix = std::to_string(static_cast<unsigned long long>(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto root = std::filesystem::temp_directory_path() / ("valcraft-window-icon-tests-" + unique_suffix);
    const auto working_directory = root / "workspace";
    const auto build_directory = root / "build";
    const auto executable_directory = build_directory / "bin";
    const auto icon_name = std::filesystem::path(kGameWindowIconRelativePath).filename();
    const auto parent_icon_path = build_directory / "Images" / icon_name;
    const auto executable_icon_path = executable_directory / "Images" / icon_name;
    const auto working_icon_path = working_directory / "Images" / icon_name;

    std::filesystem::remove_all(root);
    std::filesystem::create_directories(parent_icon_path.parent_path());
    std::filesystem::create_directories(executable_icon_path.parent_path());
    std::filesystem::create_directories(working_icon_path.parent_path());

    {
        std::ofstream icon_stream(parent_icon_path, std::ios::binary);
        icon_stream.put('\0');
    }
    CHECK(resolve_window_icon_path(working_directory, executable_directory) == parent_icon_path);

    {
        std::ofstream icon_stream(executable_icon_path, std::ios::binary);
        icon_stream.put('\0');
    }
    CHECK(resolve_window_icon_path(working_directory, executable_directory) == executable_icon_path);

    {
        std::ofstream icon_stream(working_icon_path, std::ios::binary);
        icon_stream.put('\0');
    }
    CHECK(resolve_window_icon_path(working_directory, executable_directory) == working_icon_path);

    std::filesystem::remove_all(root);
}

TEST_CASE("audit recorder writes manifest summary and compatibility report when enabled") {
    const auto unique_suffix = std::to_string(static_cast<unsigned long long>(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto audit_root = std::filesystem::temp_directory_path() / ("valcraft-audit-enabled-" + unique_suffix);
    std::filesystem::remove_all(audit_root);

    AuditOptions options {};
    options.enabled = true;
    options.mode = AuditMode::Measure;
    options.root_directory = audit_root;
    options.label = "manual block break";
    options.trace_frames = true;
    options.compatibility_json_path = (audit_root / "compatibility.json").string();

    AuditStartContext start_context {};
    start_context.platform = "windows";
    start_context.build_type = "Debug";
    start_context.working_directory = audit_root;
    start_context.arguments = {"--audit", "--audit-mode=measure", "--audit-label=manual block break"};

    PerformanceRunReport report {};
    report.metadata.platform = "windows";
    report.metadata.build_type = "Debug";
    report.metadata.capture_mode = "measure";
    report.metadata.scenario = "manual block break";
    report.summary.frame_count = 1;
    report.summary.frame_total_ms.average = 16.0;
    report.summary.frame_total_ms.maximum = 16.0;
    report.summary.frame_total_ms.p50 = 16.0;
    report.summary.frame_total_ms.p95 = 16.0;
    report.summary.frame_total_ms.p99 = 16.0;

    std::filesystem::path manifest_path;
    std::filesystem::path summary_json_path;
    std::filesystem::path summary_text_path;
    std::filesystem::path events_path;
    std::filesystem::path seconds_path;
    std::filesystem::path frames_path;
    std::filesystem::path spikes_path;

    {
        AuditRecorder recorder(options, start_context);
        REQUIRE(recorder.enabled());

        AuditEvent event {};
        event.frame_index = 0;
        event.second_index = 0;
        event.category = AuditEventCategory::Player;
        event.kind = "block_break";
        event.severity = AuditSeverity::Info;
        event.payload_json = audit_json_object({
            {"world_x", audit_json_number(12)},
            {"world_y", audit_json_number(64)},
            {"world_z", audit_json_number(5)},
        });
        recorder.record_event(event, AuditPriority::High);

        AuditFrameSample frame {};
        frame.frame_index = 0;
        frame.second_index = 0;
        frame.fps = 62.5;
        frame.ui_screen = "gameplay";
        frame.mouse_captured = true;
        frame.input_raw_events = 2;
        frame.input_action_events = 1;
        frame.active_creatures = 3;
        frame.active_item_drops = 1;
        frame.performance.frame_index = 0;
        frame.performance.frame_total_ms = 16.0;
        frame.performance.world_ms = 5.0;
        recorder.record_frame(frame, AuditPriority::Low);

        AuditSecondSample second {};
        second.second_index = 0;
        second.frame_count = 1;
        second.fps_avg = 62.5;
        second.fps_min = 62.5;
        second.fps_max = 62.5;
        second.frame_ms_avg = 16.0;
        second.frame_ms_p95 = 16.0;
        second.frame_ms_max = 16.0;
        second.player_events = 1;
        second.block_breaks = 1;
        recorder.record_second(second, AuditPriority::High);

        AuditFinalizeContext finalize_context {};
        finalize_context.status = AuditRunStatus::Completed;
        finalize_context.performance_report = report;
        recorder.finalize(finalize_context);

        manifest_path = recorder.paths().manifest_path;
        summary_json_path = recorder.paths().summary_json_path;
        summary_text_path = recorder.paths().summary_text_path;
        events_path = recorder.paths().events_path;
        seconds_path = recorder.paths().seconds_path;
        frames_path = recorder.paths().frames_path;
        spikes_path = recorder.paths().spikes_path;
    }

    const auto read_file = [](const std::filesystem::path& path) {
        std::ifstream input(path, std::ios::binary);
        std::ostringstream buffer;
        buffer << input.rdbuf();
        return buffer.str();
    };

    CHECK(std::filesystem::exists(manifest_path));
    CHECK(std::filesystem::exists(summary_json_path));
    CHECK(std::filesystem::exists(summary_text_path));
    CHECK(std::filesystem::exists(events_path));
    CHECK(std::filesystem::exists(seconds_path));
    CHECK(std::filesystem::exists(frames_path));
    CHECK(std::filesystem::exists(spikes_path));
    CHECK(std::filesystem::exists(audit_root / "compatibility.json"));

    const auto manifest_json = read_file(manifest_path);
    const auto summary_json = read_file(summary_json_path);
    const auto summary_text = read_file(summary_text_path);
    const auto events_jsonl = read_file(events_path);

    CHECK(manifest_json.find("\"status\": \"completed\"") != std::string::npos);
    CHECK(summary_json.find("\"performance_report\":") != std::string::npos);
    CHECK(summary_json.find("\"events_written\": 1") != std::string::npos);
    CHECK(summary_text.find("ValCraft audit summary") != std::string::npos);
    CHECK(events_jsonl.find("\"kind\": \"block_break\"") != std::string::npos);

    std::filesystem::remove_all(audit_root);
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
    CHECK(options.post_process_enabled);
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
    metadata.capture_mode = "interactive";
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

    const std::vector<PerformanceEvent> events {
        {1, PerformanceEventKind::BlockBreak, "STONE", 12, 64, 5, 0, 0, 0, 3, 1},
    };

    const auto report = build_performance_report(metadata, samples, true, 10, events);
    const auto json = format_performance_json(report);

    CHECK(json.find("\"schema_version\": 1") != std::string::npos);
    CHECK(json.find("\"capture_mode\": \"interactive\"") != std::string::npos);
    CHECK(json.find("\"scenario\": \"no_shadows\"") != std::string::npos);
    CHECK(json.find("\"post_process_enabled\": false") != std::string::npos);
    CHECK(json.find("\"hotspots\"") != std::string::npos);
    CHECK(json.find("\"event_summary\": {") != std::string::npos);
    CHECK(json.find("\"block_breaks\": 1") != std::string::npos);
    CHECK(json.find("\"events\": [") != std::string::npos);
    CHECK(json.find("\"kind\": \"block_break\"") != std::string::npos);
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
    CHECK(input.move_up == doctest::Approx(1.0F));
    CHECK(input.jump);

    keys.fill(0);
    keys[SDL_SCANCODE_W] = 1;
    keys[SDL_SCANCODE_LSHIFT] = 1;
    input = read_player_movement_input(keys.data());
    CHECK(input.move_forward == doctest::Approx(1.0F));
    CHECK(input.sprint);

    keys.fill(0);
    keys[SDL_SCANCODE_LCTRL] = 1;
    input = read_player_movement_input(keys.data());
    CHECK(input.move_up == doctest::Approx(-1.0F));
    CHECK_FALSE(input.sprint);
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

TEST_CASE("selected torch slot exposes local handheld light state") {
    auto hotbar = make_default_hotbar_state();

    CHECK_FALSE(selected_hotbar_emits_local_light(hotbar));

    select_hotbar_index(hotbar, 6);
    CHECK(selected_hotbar_emits_local_light(hotbar));

    select_hotbar_index(hotbar, 7);
    CHECK_FALSE(selected_hotbar_emits_local_light(hotbar));

    hotbar.slots[6] = empty_item_stack();
    select_hotbar_index(hotbar, 6);
    CHECK_FALSE(selected_hotbar_emits_local_light(hotbar));
}

TEST_CASE("hotbar layout stays centered and anchored to the bottom across resolutions") {
    auto hotbar = make_default_hotbar_state();
    select_hotbar_index(hotbar, 6);

    const std::array<std::pair<int, int>, 3> viewports {{{1600, 900}, {960, 540}, {480, 320}}};
    for (const auto& [width, height] : viewports) {
        const auto layout = build_hotbar_layout(width, height, hotbar);

        CHECK(layout.slots.size() == kHotbarSlotCount);
        CHECK(layout.bar_left + layout.bar_width * 0.5F == doctest::Approx(static_cast<float>(width) * 0.5F));
        CHECK(layout.bar_bottom >= layout.safe_margin);
        CHECK(layout.bar_bottom - layout.safe_margin <= std::max(2.0F, layout.slot_size * 0.07F));
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

TEST_CASE("gameplay hud level badge stays top right and exposes stable progress geometry") {
    auto hotbar = make_default_hotbar_state();

    const auto desktop = build_gameplay_hud_layout(1600, 900, hotbar, 20.0F, 20.0F, 10.0F, 10.0F, false, 0.42F);
    CHECK(desktop.level.x >= desktop.safe_margin);
    CHECK(desktop.level.y >= desktop.safe_margin);
    CHECK(desktop.level.x + desktop.level.width <= 1600.0F - desktop.safe_margin + 0.01F);
    CHECK(desktop.level.progress_fill_width == doctest::Approx(desktop.level.progress_width * 0.42F));

    const auto mobile = build_gameplay_hud_layout(480, 320, hotbar, 20.0F, 20.0F, 10.0F, 10.0F, false, 1.0F);
    CHECK(mobile.level.x + mobile.level.width <= 480.0F - mobile.safe_margin + 0.01F);
    CHECK(mobile.level.y + mobile.level.height < mobile.cluster_top);
    CHECK(mobile.level.progress_fill_width == doctest::Approx(mobile.level.progress_width));
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

TEST_CASE("main menu layout stays centered and resolves hovered buttons") {
    MainMenuState state {};
    state.visible = true;
    state.selected_action = MainMenuAction::Play;

    const auto base_layout = build_main_menu_layout(1600, 900, state);
    state.cursor_x = base_layout.buttons[1].x + base_layout.buttons[1].width * 0.5F;
    state.cursor_y = base_layout.buttons[1].y + base_layout.buttons[1].height * 0.5F;

    const auto layout = build_main_menu_layout(1600, 900, state);

    CHECK(layout.hero_center_x == doctest::Approx(800.0F));
    CHECK(layout.tagline_center_x == doctest::Approx(800.0F));
    CHECK(layout.buttons[0].label == "JOUER");
    CHECK(layout.buttons[1].label == "CHARGER");
    CHECK(layout.buttons[2].label == "OPTIONS");
    CHECK(layout.buttons[1].hovered);
    CHECK(layout.buttons[1].selected);

    const auto action = main_menu_action_at(layout, state.cursor_x, state.cursor_y);
    REQUIRE(action.has_value());
    CHECK(*action == MainMenuAction::Load);
}

TEST_CASE("main menu keyboard navigation wraps across all actions") {
    CHECK(next_main_menu_action(MainMenuAction::Play, 1) == MainMenuAction::Load);
    CHECK(next_main_menu_action(MainMenuAction::Load, 1) == MainMenuAction::Options);
    CHECK(next_main_menu_action(MainMenuAction::Options, 1) == MainMenuAction::Play);
    CHECK(next_main_menu_action(MainMenuAction::Play, -1) == MainMenuAction::Options);
}

TEST_CASE("main menu layout keeps the action stack centered on compact viewports") {
    MainMenuState state {};
    state.visible = true;

    const auto layout = build_main_menu_layout(360, 240, state);

    CHECK(layout.button_stack_x + layout.button_stack_width * 0.5F == doctest::Approx(180.0F));
    CHECK(layout.button_stack_y >= 0.0F);
    CHECK(layout.button_stack_y + layout.button_stack_height < 240.0F);
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
    CHECK(layout.buttons[1].label == "SAUVEGARDER");
    CHECK(layout.buttons[2].hovered);
    CHECK(layout.buttons[2].selected);
    CHECK(layout.buttons[4].label == "MENU PRINCIPAL");

    const auto action = pause_menu_action_at(layout, state.cursor_x, state.cursor_y);
    REQUIRE(action.has_value());
    CHECK(*action == PauseMenuAction::Load);
}

TEST_CASE("pause menu keyboard navigation wraps across the full action list") {
    CHECK(next_pause_menu_action(PauseMenuAction::Resume, 1) == PauseMenuAction::Save);
    CHECK(next_pause_menu_action(PauseMenuAction::Save, 1) == PauseMenuAction::Load);
    CHECK(next_pause_menu_action(PauseMenuAction::Load, 1) == PauseMenuAction::Options);
    CHECK(next_pause_menu_action(PauseMenuAction::Options, 1) == PauseMenuAction::ReturnToMainMenu);
    CHECK(next_pause_menu_action(PauseMenuAction::ReturnToMainMenu, 1) == PauseMenuAction::Resume);
    CHECK(next_pause_menu_action(PauseMenuAction::Resume, -1) == PauseMenuAction::ReturnToMainMenu);
}

TEST_CASE("pause menu layout stays centered on compact viewports") {
    PauseMenuState state {};
    state.visible = true;

    const auto layout = build_pause_menu_layout(280, 220, state);

    CHECK(layout.panel_x + layout.panel_width * 0.5F == doctest::Approx(140.0F));
    CHECK(layout.panel_y + layout.panel_height * 0.5F == doctest::Approx(110.0F));
}

TEST_CASE("save slot menu disables empty load slots and keeps the active slot highlighted") {
    SaveSlotMenuState state {};
    state.visible = true;
    state.mode = SaveSlotMenuMode::LoadGame;
    state.parent = SaveSlotMenuParent::MainMenu;
    state.selected_index = 1;
    state.slots[1].exists = true;
    state.slots[1].seed = 4242;
    state.slots[1].time_of_day = 14.5F;
    state.slots[3].exists = true;
    state.active_slot = 3;

    const auto base_layout = build_save_slot_menu_layout(1600, 900, state);
    state.cursor_x = base_layout.cards[1].x + base_layout.cards[1].width * 0.5F;
    state.cursor_y = base_layout.cards[1].y + base_layout.cards[1].height * 0.5F;

    const auto layout = build_save_slot_menu_layout(1600, 900, state);

    CHECK(layout.panel_x + layout.panel_width * 0.5F == doctest::Approx(800.0F));
    CHECK_FALSE(layout.cards[0].enabled);
    CHECK(layout.cards[1].enabled);
    CHECK(layout.cards[1].hovered);
    CHECK(layout.cards[1].selected);
    CHECK(layout.cards[1].occupied);
    CHECK(layout.cards[3].active_slot);
    CHECK(save_slot_menu_mode_title(state.mode) == "CHARGER UNE PARTIE");
    CHECK(save_slot_menu_mode_subtitle(state.mode) == "SEULS LES SLOTS EXISTANTS SONT ACTIFS");
    CHECK(first_save_slot_menu_index(state) == 1);
    CHECK(next_save_slot_menu_index(state, 1) == 3);
    CHECK(next_save_slot_menu_index(state, -1) == kSaveSlotCount);

    const auto slot = save_slot_card_at(layout, state.cursor_x, state.cursor_y);
    REQUIRE(slot.has_value());
    CHECK(*slot == 1);
}

TEST_CASE("save slot menu primary action opens occupied play slots and protects dirty loads") {
    SaveSlotMenuState state {};
    state.mode = SaveSlotMenuMode::NewGame;
    state.slots[2].exists = true;

    CHECK(save_slot_menu_mode_subtitle(state.mode) == "VIDE = NOUVELLE PARTIE, OCCUPE = OUVRIR");
    CHECK(resolve_save_slot_primary_action(state, 0, false) == SaveSlotPrimaryAction::StartNewGame);
    CHECK(resolve_save_slot_primary_action(state, 2, false) == SaveSlotPrimaryAction::LoadGame);

    state.mode = SaveSlotMenuMode::SaveGame;
    CHECK(resolve_save_slot_primary_action(state, 2, false) == SaveSlotPrimaryAction::ConfirmOverwrite);
    CHECK(resolve_save_slot_primary_action(state, 0, false) == SaveSlotPrimaryAction::SaveGame);

    state.mode = SaveSlotMenuMode::LoadGame;
    state.parent = SaveSlotMenuParent::PauseMenu;
    CHECK(resolve_save_slot_primary_action(state, 2, true) == SaveSlotPrimaryAction::ConfirmLoad);
    CHECK(resolve_save_slot_primary_action(state, 2, false) == SaveSlotPrimaryAction::LoadGame);
}

TEST_CASE("save slot menu delete button has its own hit zone") {
    SaveSlotMenuState state {};
    state.visible = true;
    state.mode = SaveSlotMenuMode::NewGame;
    state.selected_index = 1;
    state.slots[1].exists = true;

    const auto base_layout = build_save_slot_menu_layout(1600, 900, state);
    const auto& delete_button = base_layout.cards[1];
    state.cursor_x = delete_button.delete_x + delete_button.delete_size * 0.5F;
    state.cursor_y = delete_button.delete_y + delete_button.delete_size * 0.5F;

    const auto layout = build_save_slot_menu_layout(1600, 900, state);

    CHECK(layout.cards[0].delete_visible == false);
    CHECK(layout.cards[1].delete_visible);
    CHECK(layout.cards[1].delete_hovered);
    CHECK(layout.cards[1].selected);
    CHECK_FALSE(layout.cards[1].hovered);

    const auto delete_slot = save_slot_delete_at(layout, state.cursor_x, state.cursor_y);
    REQUIRE(delete_slot.has_value());
    CHECK(*delete_slot == 1);
    CHECK_FALSE(save_slot_card_at(layout, state.cursor_x, state.cursor_y).has_value());
}

TEST_CASE("save slot menu exposes a back button in the navigation loop") {
    SaveSlotMenuState state {};
    state.visible = true;
    state.mode = SaveSlotMenuMode::NewGame;
    state.selected_index = kSaveSlotCount;

    const auto base_layout = build_save_slot_menu_layout(960, 540, state);
    state.cursor_x = base_layout.back_button.x + base_layout.back_button.width * 0.5F;
    state.cursor_y = base_layout.back_button.y + base_layout.back_button.height * 0.5F;

    const auto layout = build_save_slot_menu_layout(960, 540, state);

    CHECK(layout.back_button.hovered);
    CHECK(layout.back_button.selected);
    CHECK(save_slot_back_hovered(layout, state.cursor_x, state.cursor_y));
    CHECK(next_save_slot_menu_index(state, 1) == 0);
    CHECK(next_save_slot_menu_index(state, -1) == 7);
}

TEST_CASE("options menu layout reflects toggle labels and hovered action") {
    OptionsMenuState state {};
    state.visible = true;
    state.parent = OptionsMenuParent::MainMenu;
    state.shadows_enabled = false;
    state.post_process_enabled = true;
    state.selected_action = OptionsMenuAction::ToggleShadows;

    const auto base_layout = build_options_menu_layout(1600, 900, state);
    state.cursor_x = base_layout.buttons[1].x + base_layout.buttons[1].width * 0.5F;
    state.cursor_y = base_layout.buttons[1].y + base_layout.buttons[1].height * 0.5F;

    const auto layout = build_options_menu_layout(1600, 900, state);

    CHECK(layout.panel_x + layout.panel_width * 0.5F == doctest::Approx(800.0F));
    CHECK(layout.buttons[0].label == "OMBRES  OFF");
    CHECK(layout.buttons[1].label == "POST PROCESS  ON");
    CHECK(layout.buttons[2].label == "RETOUR");
    CHECK(options_menu_subtitle(state.parent) == "REGLAGES GENERAUX");
    CHECK(layout.buttons[1].hovered);
    CHECK(layout.buttons[1].selected);

    const auto action = options_menu_action_at(layout, state.cursor_x, state.cursor_y);
    REQUIRE(action.has_value());
    CHECK(*action == OptionsMenuAction::TogglePostProcess);
}

TEST_CASE("options menu keyboard navigation wraps across toggles and back") {
    CHECK(next_options_menu_action(OptionsMenuAction::ToggleShadows, 1) == OptionsMenuAction::TogglePostProcess);
    CHECK(next_options_menu_action(OptionsMenuAction::TogglePostProcess, 1) == OptionsMenuAction::Back);
    CHECK(next_options_menu_action(OptionsMenuAction::Back, 1) == OptionsMenuAction::ToggleShadows);
    CHECK(next_options_menu_action(OptionsMenuAction::ToggleShadows, -1) == OptionsMenuAction::Back);
}

TEST_CASE("confirm dialog stays centered and resolves the hovered cancel choice") {
    ConfirmDialogState state {};
    state.visible = true;
    state.intent = ConfirmDialogIntent::LoadSlot;
    state.selected_choice = ConfirmDialogChoice::Confirm;

    const auto base_layout = build_confirm_dialog_layout(1600, 900, state);
    state.cursor_x = base_layout.buttons[1].x + base_layout.buttons[1].width * 0.5F;
    state.cursor_y = base_layout.buttons[1].y + base_layout.buttons[1].height * 0.5F;

    const auto layout = build_confirm_dialog_layout(1600, 900, state);

    CHECK(layout.panel_x + layout.panel_width * 0.5F == doctest::Approx(800.0F));
    CHECK(confirm_dialog_title(state.intent) == "CHARGER CETTE PARTIE");
    CHECK(confirm_dialog_subtitle(state.intent) == "LA PROGRESSION NON SAUVEGARDEE SERA PERDUE");
    CHECK(layout.buttons[1].hovered);
    CHECK(layout.buttons[1].selected);
    CHECK(next_confirm_dialog_choice(ConfirmDialogChoice::Confirm, 1) == ConfirmDialogChoice::Cancel);

    const auto choice = confirm_dialog_choice_at(layout, state.cursor_x, state.cursor_y);
    REQUIRE(choice.has_value());
    CHECK(*choice == ConfirmDialogChoice::Cancel);
}

TEST_CASE("confirm dialog delete intent uses yes no wording") {
    ConfirmDialogState state {};
    state.visible = true;
    state.intent = ConfirmDialogIntent::DeleteSlot;
    state.selected_choice = ConfirmDialogChoice::Confirm;

    const auto layout = build_confirm_dialog_layout(1600, 900, state);

    CHECK(confirm_dialog_title(state.intent) == "VOULEZ VOUS SUPPRIMER CETTE PARTIE ?");
    CHECK(confirm_dialog_subtitle(state.intent) == "CETTE ACTION EST DEFINITIVE");
    CHECK(layout.buttons[0].label == "OUI");
    CHECK(layout.buttons[1].label == "NON");
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
    CHECK(inventory.storage_slots[18].block_id == to_block_id(BlockType::Pastron));
    CHECK(inventory.storage_slots[19].block_id == to_block_id(BlockType::RoundShield));
    CHECK(inventory.storage_slots[20].block_id == to_block_id(BlockType::Sword));
    CHECK(inventory.storage_slots[21].block_id == to_block_id(BlockType::Spear));
    CHECK(inventory.storage_slots[22].block_id == to_block_id(BlockType::Shoes));
    CHECK(inventory.storage_slots[23].block_id == to_block_id(BlockType::Pants));
    CHECK_FALSE(inventory_slot_has_item(inventory.storage_slots.back()));
    CHECK(std::all_of(inventory.equipment_slots.begin(), inventory.equipment_slots.end(), [](const HotbarSlot& slot) {
        return !inventory_slot_has_item(slot);
    }));
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
    const auto equipment_layout_index = kInventoryStorageSlotCount + kHotbarSlotCount + equipment_slot_index(EquipmentSlot::Weapon);
    CHECK(layout.slots[equipment_layout_index].is_equipment);
    CHECK(layout.slots[equipment_layout_index].ref.group == InventorySlotGroup::Equipment);

    const auto hovered = inventory_slot_at(layout, inventory.cursor_x, inventory.cursor_y);
    REQUIRE(hovered.has_value());
    CHECK(hovered->group == InventorySlotGroup::Storage);
    CHECK(hovered->index == 4);
}

TEST_CASE("inventory layout stays horizontally centered and visible on compact viewports") {
    const auto hotbar = make_default_hotbar_state();
    const auto inventory = make_default_inventory_menu_state();

    const auto layout = build_inventory_menu_layout(520, 320, inventory, hotbar);

    CHECK(layout.panel_x + layout.panel_width * 0.5F == doctest::Approx(260.0F));
    CHECK(layout.panel_y >= 0.0F);
    CHECK(layout.panel_y <= 8.0F);
    CHECK(layout.panel_y + layout.panel_height > 160.0F);
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

TEST_CASE("inventory equipment slots accept matching gear and expose combat stats") {
    HotbarState hotbar {};
    InventoryMenuState inventory {};
    inventory.storage_slots[0] = inventory_make_slot(to_block_id(BlockType::Pastron), 1);
    inventory.storage_slots[1] = inventory_make_slot(to_block_id(BlockType::RoundShield), 1);
    inventory.storage_slots[2] = inventory_make_slot(to_block_id(BlockType::Sword), 1);
    inventory.storage_slots[3] = inventory_make_slot(to_block_id(BlockType::Stone), 8);

    inventory_primary_click(inventory, hotbar, {InventorySlotGroup::Storage, 0});
    inventory_primary_click(inventory, hotbar, {InventorySlotGroup::Equipment, equipment_slot_index(EquipmentSlot::Chest)});
    CHECK_FALSE(inventory.carrying_item);
    CHECK(inventory.equipment_slots[equipment_slot_index(EquipmentSlot::Chest)].block_id == to_block_id(BlockType::Pastron));

    inventory_primary_click(inventory, hotbar, {InventorySlotGroup::Storage, 1});
    inventory_primary_click(inventory, hotbar, {InventorySlotGroup::Equipment, equipment_slot_index(EquipmentSlot::Shield)});
    CHECK(inventory_equipment_resistance_percent(inventory) == doctest::Approx(30.0F));

    inventory_primary_click(inventory, hotbar, {InventorySlotGroup::Storage, 2});
    inventory_primary_click(inventory, hotbar, {InventorySlotGroup::Equipment, equipment_slot_index(EquipmentSlot::Weapon)});
    const auto equipped_weapon = inventory_active_weapon_stats(inventory, hotbar);
    REQUIRE(equipped_weapon.has_value());
    CHECK(equipped_weapon->damage == doctest::Approx(6.0F));
    CHECK(equipped_weapon->range == doctest::Approx(3.1F));

    inventory_primary_click(inventory, hotbar, {InventorySlotGroup::Storage, 3});
    inventory_primary_click(inventory, hotbar, {InventorySlotGroup::Equipment, equipment_slot_index(EquipmentSlot::Feet)});
    CHECK(inventory.carrying_item);
    CHECK(inventory.carried_slot.block_id == to_block_id(BlockType::Stone));
    CHECK_FALSE(inventory_slot_has_item(inventory.equipment_slots[equipment_slot_index(EquipmentSlot::Feet)]));
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

TEST_CASE("save game scanning and loading preserve slot metadata and payloads") {
    const auto unique_suffix =
        std::to_string(static_cast<long long>(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto save_root = std::filesystem::temp_directory_path() / ("valcraft-save-tests-" + unique_suffix);
    std::filesystem::remove_all(save_root);

    SaveGameSnapshot snapshot {};
    snapshot.metadata.saved_at_unix_seconds = 1712185200;
    snapshot.metadata.seed = 987654;
    snapshot.metadata.time_of_day = 18.25F;
    snapshot.metadata.weather_time_seconds = 735.5F;
    snapshot.metadata.has_starting_village = true;
    snapshot.spawn_position = {4.5F, 82.0F, -6.5F};
    snapshot.player_state.position = {11.0F, 65.0F, 3.0F};
    snapshot.player_state.velocity = {0.5F, -1.0F, 2.5F};
    snapshot.player_state.fly_mode = true;
    snapshot.player_state.health = 13.5F;
    snapshot.player_state.death_cause = PlayerDeathCause::Zombie;
    snapshot.progression = {12U, 3456ULL};
    snapshot.hotbar.slots[0] = make_item_stack(to_block_id(BlockType::Stone), 12);
    snapshot.hotbar.slots[4] = make_item_stack(to_block_id(BlockType::Torch), 16);
    snapshot.hotbar.selected_index = 4;
    snapshot.inventory.storage_slots[0] = make_item_stack(to_block_id(BlockType::Wood), 8);
    snapshot.inventory.storage_slots[7] = make_item_stack(to_block_id(BlockType::Water), 2);
    snapshot.inventory.equipment_slots[equipment_slot_index(EquipmentSlot::Chest)] =
        make_item_stack(to_block_id(BlockType::Pastron), 1);
    snapshot.inventory.equipment_slots[equipment_slot_index(EquipmentSlot::Weapon)] =
        make_item_stack(to_block_id(BlockType::Spear), 1);
    snapshot.inventory.carried_slot = make_item_stack(to_block_id(BlockType::Dirt), 5);
    snapshot.inventory.carrying_item = true;

    CreatureInstance creature {};
    creature.anchor.chunk = {2, -1};
    creature.anchor.ground_block = {33, 64, -14};
    creature.anchor.spawn_position = {33.5F, 65.0F, -13.5F};
    creature.anchor.species = CreatureSpecies::Cow;
    creature.position = {34.0F, 65.0F, -12.0F};
    creature.yaw_radians = 1.25F;
    creature.behavior_seed = 42;
    creature.appearance_seed = 7;
    creature.behavior_state = CreatureBehaviorState::Wander;
    creature.health = 4.5F;
    snapshot.creatures.push_back(creature);

    ItemDrop drop {};
    drop.position = {9.0F, 66.0F, 2.0F};
    drop.velocity = {0.0F, 1.0F, 0.0F};
    drop.stack = make_item_stack(to_block_id(BlockType::Grass), 3);
    drop.age_seconds = 1.5F;
    drop.pickup_cooldown = 0.75F;
    drop.grounded = true;
    snapshot.item_drops.push_back(drop);

    WorldChunkSnapshot chunk_snapshot {};
    chunk_snapshot.coord = {1, 2};
    chunk_snapshot.blocks.fill(to_block_id(BlockType::Air));
    chunk_snapshot.blocks[0] = to_block_id(BlockType::Stone);
    chunk_snapshot.blocks[1] = to_block_id(BlockType::Torch);
    snapshot.chunk_snapshots.push_back(chunk_snapshot);

    write_save_slot(save_root, 2, snapshot);

    const auto scanned = scan_save_slots(save_root);
    CHECK_FALSE(scanned[0].exists);
    CHECK(scanned[2].exists);
    CHECK(scanned[2].saved_at_unix_seconds == snapshot.metadata.saved_at_unix_seconds);
    CHECK(scanned[2].seed == snapshot.metadata.seed);
    CHECK(scanned[2].time_of_day == doctest::Approx(snapshot.metadata.time_of_day));
    CHECK(scanned[2].weather_time_seconds == doctest::Approx(snapshot.metadata.weather_time_seconds));
    CHECK(scanned[2].modified_chunk_count == 1);
    CHECK(scanned[2].has_starting_village);

    const auto loaded = load_save_slot(save_root, 2);
    REQUIRE(loaded.has_value());
    CHECK(loaded->metadata.exists);
    CHECK(loaded->metadata.seed == snapshot.metadata.seed);
    CHECK(loaded->metadata.weather_time_seconds == doctest::Approx(snapshot.metadata.weather_time_seconds));
    CHECK(loaded->metadata.has_starting_village);
    CHECK(loaded->spawn_position.x == doctest::Approx(snapshot.spawn_position.x));
    CHECK(loaded->spawn_position.y == doctest::Approx(snapshot.spawn_position.y));
    CHECK(loaded->spawn_position.z == doctest::Approx(snapshot.spawn_position.z));
    CHECK(loaded->player_state.position.x == doctest::Approx(snapshot.player_state.position.x));
    CHECK(loaded->player_state.position.y == doctest::Approx(snapshot.player_state.position.y));
    CHECK(loaded->player_state.position.z == doctest::Approx(snapshot.player_state.position.z));
    CHECK(loaded->player_state.velocity.x == doctest::Approx(snapshot.player_state.velocity.x));
    CHECK(loaded->player_state.velocity.y == doctest::Approx(snapshot.player_state.velocity.y));
    CHECK(loaded->player_state.velocity.z == doctest::Approx(snapshot.player_state.velocity.z));
    CHECK(loaded->player_state.fly_mode == snapshot.player_state.fly_mode);
    CHECK(loaded->player_state.health == doctest::Approx(snapshot.player_state.health));
    CHECK(loaded->player_state.death_cause == snapshot.player_state.death_cause);
    CHECK(loaded->progression.level == 12U);
    CHECK(loaded->progression.experience == 3456ULL);
    CHECK(loaded->hotbar.selected_index == 4);
    CHECK(loaded->hotbar.slots[4].block_id == to_block_id(BlockType::Torch));
    CHECK(loaded->inventory.storage_slots[7].block_id == to_block_id(BlockType::Water));
    CHECK(loaded->inventory.equipment_slots[equipment_slot_index(EquipmentSlot::Chest)].block_id ==
          to_block_id(BlockType::Pastron));
    CHECK(loaded->inventory.equipment_slots[equipment_slot_index(EquipmentSlot::Weapon)].block_id ==
          to_block_id(BlockType::Spear));
    CHECK(inventory_equipment_resistance_percent(loaded->inventory) == doctest::Approx(18.0F));
    CHECK(loaded->inventory.carrying_item);
    REQUIRE(loaded->creatures.size() == 1);
    CHECK(loaded->creatures[0].anchor.species == CreatureSpecies::Cow);
    CHECK(loaded->creatures[0].behavior_state == CreatureBehaviorState::Wander);
    CHECK(loaded->creatures[0].health == doctest::Approx(4.5F));
    REQUIRE(loaded->item_drops.size() == 1);
    CHECK(loaded->item_drops[0].grounded);
    REQUIRE(loaded->chunk_snapshots.size() == 1);
    CHECK(loaded->chunk_snapshots[0].coord == chunk_snapshot.coord);
    CHECK(loaded->chunk_snapshots[0].blocks[1] == to_block_id(BlockType::Torch));

    std::filesystem::remove_all(save_root);
}

TEST_CASE("save game loader preserves backward compatibility with version 1 files") {
    const auto unique_suffix =
        std::to_string(static_cast<long long>(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto save_root = std::filesystem::temp_directory_path() / ("valcraft-save-v1-tests-" + unique_suffix);
    std::filesystem::remove_all(save_root);
    std::filesystem::create_directories(save_root);

    const auto file_path = save_slot_file_path(save_root, 0);
    std::ofstream output(file_path, std::ios::binary | std::ios::trunc);
    REQUIRE(output.good());

    const auto write_bytes = [&](const void* data, std::size_t size) {
        output.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
    };
    const auto write_value = [&](const auto& value) {
        output.write(reinterpret_cast<const char*>(&value), static_cast<std::streamsize>(sizeof(value)));
    };
    const auto write_bool = [&](bool value) {
        const std::uint8_t raw = value ? 1U : 0U;
        write_value(raw);
    };
    const auto write_vec3 = [&](const glm::vec3& value) {
        write_value(value.x);
        write_value(value.y);
        write_value(value.z);
    };
    const auto write_hotbar_slot = [&](const HotbarSlot& slot) {
        write_value(slot.block_id);
        write_value(slot.count);
    };
    const auto write_player_state = [&](const PlayerState& state) {
        write_vec3(state.position);
        write_vec3(state.velocity);
        write_value(state.yaw_degrees);
        write_value(state.pitch_degrees);
        write_value(state.body_yaw_degrees);
        write_value(state.animation_time);
        write_value(state.step_phase);
        write_value(state.health);
        write_value(state.air_seconds);
        write_value(state.hurt_timer);
        write_value(state.damage_cooldown);
        write_value(state.regen_delay);
        write_value(state.regen_tick_timer);
        write_value(state.drowning_tick_timer);
        write_value(state.fall_start_y);
        write_value(state.primary_action_progress);
        write_value(state.secondary_action_progress);
        write_value(state.landing_impact);
        write_value(state.airborne_time);
        write_value(state.look_sway_yaw);
        write_value(state.look_sway_pitch);
        write_bool(state.on_ground);
        write_bool(state.fly_mode);
        write_bool(state.head_underwater);
        write_bool(state.swimming);
        write_bool(state.primary_action_active);
        write_bool(state.secondary_action_active);
        write_bool(state.dead);
        const auto death_cause = static_cast<std::underlying_type_t<PlayerDeathCause>>(state.death_cause);
        write_value(death_cause);
    };

    const std::array<char, 8> magic {{'V', 'A', 'L', 'S', 'L', 'O', 'T', '1'}};
    const std::uint32_t version = 1;
    const std::uint64_t saved_at = 1712185200;
    const int seed = 13579;
    const float time_of_day = 9.5F;
    const std::uint32_t modified_chunk_count = 0;
    const glm::vec3 spawn_position {8.5F, 72.0F, -4.5F};
    auto hotbar = make_default_hotbar_state();
    auto inventory = make_default_inventory_menu_state();
    PlayerState player_state {};
    player_state.position = spawn_position;
    player_state.velocity = {0.25F, 0.0F, -0.5F};
    player_state.fly_mode = true;
    player_state.health = 18.0F;

    write_bytes(magic.data(), magic.size());
    write_value(version);
    write_value(saved_at);
    write_value(seed);
    write_value(time_of_day);
    write_value(modified_chunk_count);
    write_vec3(spawn_position);
    write_player_state(player_state);
    for (const auto& slot : hotbar.slots) {
        write_hotbar_slot(slot);
    }
    write_value(static_cast<std::uint32_t>(hotbar.selected_index));
    for (const auto& slot : inventory.storage_slots) {
        write_hotbar_slot(slot);
    }
    write_hotbar_slot(inventory.carried_slot);
    write_bool(inventory.carrying_item);
    write_value(std::uint32_t {0});
    write_value(std::uint32_t {0});
    write_value(std::uint32_t {0});
    output.close();

    const auto scanned = scan_save_slots(save_root);
    REQUIRE(scanned[0].exists);
    CHECK(scanned[0].saved_at_unix_seconds == saved_at);
    CHECK(scanned[0].seed == seed);
    CHECK(scanned[0].time_of_day == doctest::Approx(time_of_day));
    CHECK(scanned[0].weather_time_seconds == doctest::Approx(0.0F));
    CHECK_FALSE(scanned[0].has_starting_village);

    const auto loaded = load_save_slot(save_root, 0);
    REQUIRE(loaded.has_value());
    CHECK(loaded->metadata.exists);
    CHECK(loaded->metadata.seed == seed);
    CHECK(loaded->metadata.time_of_day == doctest::Approx(time_of_day));
    CHECK(loaded->metadata.weather_time_seconds == doctest::Approx(0.0F));
    CHECK_FALSE(loaded->metadata.has_starting_village);
    CHECK(loaded->spawn_position.x == doctest::Approx(spawn_position.x));
    CHECK(loaded->spawn_position.y == doctest::Approx(spawn_position.y));
    CHECK(loaded->spawn_position.z == doctest::Approx(spawn_position.z));
    CHECK(loaded->player_state.fly_mode);
    CHECK(loaded->player_state.health == doctest::Approx(player_state.health));
    CHECK(loaded->progression.level == 1U);
    CHECK(loaded->progression.experience == 0ULL);
    CHECK(std::all_of(loaded->inventory.equipment_slots.begin(), loaded->inventory.equipment_slots.end(), [](const HotbarSlot& slot) {
        return !inventory_slot_has_item(slot);
    }));
    CHECK(loaded->creatures.empty());
    CHECK(loaded->item_drops.empty());
    CHECK(loaded->chunk_snapshots.empty());

    std::filesystem::remove_all(save_root);
}

TEST_CASE("save game loader rejects corrupt oversized payload counts before allocation") {
    const auto unique_suffix =
        std::to_string(static_cast<long long>(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto save_root = std::filesystem::temp_directory_path() / ("valcraft-save-corrupt-tests-" + unique_suffix);
    std::filesystem::remove_all(save_root);
    std::filesystem::create_directories(save_root);

    const auto file_path = save_slot_file_path(save_root, 0);
    std::ofstream output(file_path, std::ios::binary | std::ios::trunc);
    REQUIRE(output.good());

    const auto write_bytes = [&](const void* data, std::size_t size) {
        output.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
    };
    const auto write_value = [&](const auto& value) {
        output.write(reinterpret_cast<const char*>(&value), static_cast<std::streamsize>(sizeof(value)));
    };
    const auto write_bool = [&](bool value) {
        const std::uint8_t raw = value ? 1U : 0U;
        write_value(raw);
    };
    const auto write_vec3 = [&](const glm::vec3& value) {
        write_value(value.x);
        write_value(value.y);
        write_value(value.z);
    };
    const auto write_hotbar_slot = [&](const HotbarSlot& slot) {
        write_value(slot.block_id);
        write_value(slot.count);
    };
    const auto write_player_state = [&](const PlayerState& state) {
        write_vec3(state.position);
        write_vec3(state.velocity);
        write_value(state.yaw_degrees);
        write_value(state.pitch_degrees);
        write_value(state.body_yaw_degrees);
        write_value(state.animation_time);
        write_value(state.step_phase);
        write_value(state.health);
        write_value(state.air_seconds);
        write_value(state.hurt_timer);
        write_value(state.damage_cooldown);
        write_value(state.regen_delay);
        write_value(state.regen_tick_timer);
        write_value(state.drowning_tick_timer);
        write_value(state.fall_start_y);
        write_value(state.primary_action_progress);
        write_value(state.secondary_action_progress);
        write_value(state.landing_impact);
        write_value(state.airborne_time);
        write_value(state.look_sway_yaw);
        write_value(state.look_sway_pitch);
        write_bool(state.on_ground);
        write_bool(state.fly_mode);
        write_bool(state.head_underwater);
        write_bool(state.swimming);
        write_bool(state.primary_action_active);
        write_bool(state.secondary_action_active);
        write_bool(state.dead);
        const auto death_cause = static_cast<std::underlying_type_t<PlayerDeathCause>>(state.death_cause);
        write_value(death_cause);
    };

    const std::array<char, 8> magic {{'V', 'A', 'L', 'S', 'L', 'O', 'T', '1'}};
    const std::uint32_t version = 5;
    const std::uint64_t saved_at = 1712185200;
    const int seed = 24680;
    const float time_of_day = 12.0F;
    const std::uint32_t modified_chunk_count = 0;
    const float weather_time_seconds = 0.0F;
    const glm::vec3 spawn_position {0.5F, 72.0F, 0.5F};
    const auto hotbar = make_default_hotbar_state();
    const auto inventory = make_default_inventory_menu_state();
    PlayerState player_state {};
    player_state.position = spawn_position;

    write_bytes(magic.data(), magic.size());
    write_value(version);
    write_value(saved_at);
    write_value(seed);
    write_value(time_of_day);
    write_value(modified_chunk_count);
    write_bool(false);
    write_value(weather_time_seconds);
    write_vec3(spawn_position);
    write_player_state(player_state);
    for (const auto& slot : hotbar.slots) {
        write_hotbar_slot(slot);
    }
    write_value(static_cast<std::uint32_t>(hotbar.selected_index));
    for (const auto& slot : inventory.storage_slots) {
        write_hotbar_slot(slot);
    }
    write_hotbar_slot(inventory.carried_slot);
    write_bool(inventory.carrying_item);
    for (const auto& slot : inventory.equipment_slots) {
        write_hotbar_slot(slot);
    }
    write_value(std::numeric_limits<std::uint32_t>::max());
    output.close();

    CHECK_FALSE(load_save_slot(save_root, 0).has_value());

    std::filesystem::remove_all(save_root);
}

TEST_CASE("save game writer rejects snapshots that exceed the loader payload limits") {
    const auto unique_suffix =
        std::to_string(static_cast<long long>(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto save_root = std::filesystem::temp_directory_path() / ("valcraft-save-oversized-tests-" + unique_suffix);
    std::filesystem::remove_all(save_root);

    SaveGameSnapshot snapshot {};
    snapshot.item_drops.resize(129);

    CHECK_THROWS_AS(write_save_slot(save_root, 0, snapshot), std::runtime_error);
    CHECK_FALSE(std::filesystem::exists(save_slot_file_path(save_root, 0)));

    std::filesystem::remove_all(save_root);
}

TEST_CASE("save game writer keeps an existing slot intact when a replacement is rejected before finalize") {
    const auto unique_suffix =
        std::to_string(static_cast<long long>(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto save_root = std::filesystem::temp_directory_path() / ("valcraft-save-atomic-tests-" + unique_suffix);
    std::filesystem::remove_all(save_root);

    SaveGameSnapshot original {};
    original.metadata.saved_at_unix_seconds = 1712185200;
    original.metadata.seed = 1111;
    original.metadata.time_of_day = 7.25F;
    original.metadata.weather_time_seconds = 12.5F;
    original.spawn_position = {2.5F, 74.0F, -3.5F};
    original.player_state.position = original.spawn_position;
    original.hotbar = make_default_hotbar_state();
    original.inventory = make_default_inventory_menu_state();
    original.hotbar.selected_index = 2;

    write_save_slot(save_root, 3, original);
    const auto before = load_save_slot(save_root, 3);
    REQUIRE(before.has_value());
    REQUIRE(before->metadata.seed == original.metadata.seed);

    auto rejected_replacement = original;
    rejected_replacement.metadata.saved_at_unix_seconds = 1812185200;
    rejected_replacement.metadata.seed = 2222;
    rejected_replacement.metadata.time_of_day = 19.0F;
    rejected_replacement.item_drops.resize(129);

    CHECK_THROWS_AS(write_save_slot(save_root, 3, rejected_replacement), std::runtime_error);

    const auto after = load_save_slot(save_root, 3);
    REQUIRE(after.has_value());
    CHECK(after->metadata.saved_at_unix_seconds == before->metadata.saved_at_unix_seconds);
    CHECK(after->metadata.seed == before->metadata.seed);
    CHECK(after->metadata.time_of_day == doctest::Approx(before->metadata.time_of_day));
    CHECK(after->spawn_position.x == doctest::Approx(before->spawn_position.x));
    CHECK(after->spawn_position.y == doctest::Approx(before->spawn_position.y));
    CHECK(after->spawn_position.z == doctest::Approx(before->spawn_position.z));
    CHECK_FALSE(std::filesystem::exists(std::filesystem::path(save_slot_file_path(save_root, 3).string() + ".tmp")));

    std::filesystem::remove_all(save_root);
}

TEST_CASE("save game deletion removes slot metadata and payloads") {
    const auto unique_suffix = std::to_string(static_cast<unsigned long long>(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto save_root = std::filesystem::temp_directory_path() / ("valcraft-save-delete-tests-" + unique_suffix);
    std::filesystem::remove_all(save_root);

    SaveGameSnapshot snapshot {};
    snapshot.metadata.saved_at_unix_seconds = 1712185200;
    snapshot.metadata.exists = true;
    snapshot.metadata.seed = 9001;
    snapshot.metadata.time_of_day = 6.75F;
    snapshot.spawn_position = {4.5F, 72.0F, -2.0F};
    snapshot.player_state.position = snapshot.spawn_position;

    write_save_slot(save_root, 1, snapshot);

    const auto before_delete = scan_save_slots(save_root);
    REQUIRE(before_delete[1].exists);
    CHECK(load_save_slot(save_root, 1).has_value());

    CHECK(remove_save_slot(save_root, 1));

    const auto after_delete = scan_save_slots(save_root);
    CHECK_FALSE(after_delete[1].exists);
    CHECK_FALSE(load_save_slot(save_root, 1).has_value());

    std::filesystem::remove_all(save_root);
}

} // namespace valcraft
