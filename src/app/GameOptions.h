#pragma once

#include "app/Audit.h"
#include "world/World.h"

#include <span>
#include <vector>
#include <string>
#include <string_view>

namespace valcraft {

struct PerformanceOptions {
    int spawn_preload_radius = 1;
    std::size_t chunk_generation_budget = 2;
    std::size_t mesh_rebuild_budget = 4;
    std::size_t light_node_budget = 8192;
    double max_generation_ms = 1.0;
    double max_lighting_ms = 1.5;
    double max_meshing_ms = 2.0;
    int stream_radius = 5;
    bool shadows_enabled = true;
    int shadow_map_size = 1024;
    bool post_process_enabled = true;
    bool report_frame_stats = false;
    std::string perf_json_path {};
    bool perf_trace_enabled = false;
    std::string perf_scenario {};

    [[nodiscard]] auto world_budget() const noexcept -> WorldWorkBudget {
        WorldWorkBudget budget {};
        budget.chunk_generation_budget = chunk_generation_budget;
        budget.mesh_rebuild_budget = mesh_rebuild_budget;
        budget.light_node_budget = light_node_budget;
        budget.max_generation_ms = max_generation_ms;
        budget.max_lighting_ms = max_lighting_ms;
        budget.max_meshing_ms = max_meshing_ms;
        return budget;
    }
};

enum class StartupUiOverlay {
    None = 0,
    Inventory = 1,
    Pause = 2,
};

struct GameOptions {
    bool smoke_test = false;
    int smoke_frames = 60;
    bool hidden_window = false;
    bool freeze_time = false;
    float initial_time_of_day = 8.0F;
    StartupUiOverlay startup_ui_overlay = StartupUiOverlay::None;
    std::string frame_capture_path {};
    PerformanceOptions performance {};
    AuditOptions audit {};
    std::vector<std::string> raw_arguments {};
};

struct GameOptionParseResult {
    bool ok = false;
    GameOptions options {};
    std::string error_message {};
};

[[nodiscard]] auto parse_game_options(std::span<const std::string_view> arguments) -> GameOptionParseResult;
[[nodiscard]] auto parse_game_options(int argc, char** argv) -> GameOptionParseResult;

} // namespace valcraft
