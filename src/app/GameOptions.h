#pragma once

#include "world/World.h"

#include <span>
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
    bool post_process_enabled = false;
    bool report_frame_stats = false;
    std::string perf_json_path {};
    bool perf_trace_enabled = false;
    std::string perf_scenario {};

    [[nodiscard]] auto world_budget() const noexcept -> WorldWorkBudget {
        return {
            chunk_generation_budget,
            mesh_rebuild_budget,
            light_node_budget,
            max_generation_ms,
            max_lighting_ms,
            max_meshing_ms,
        };
    }
};

struct GameOptions {
    bool smoke_test = false;
    int smoke_frames = 60;
    bool hidden_window = false;
    bool freeze_time = false;
    float initial_time_of_day = 8.0F;
    PerformanceOptions performance {};
};

struct GameOptionParseResult {
    bool ok = false;
    GameOptions options {};
    std::string error_message {};
};

[[nodiscard]] auto parse_game_options(std::span<const std::string_view> arguments) -> GameOptionParseResult;
[[nodiscard]] auto parse_game_options(int argc, char** argv) -> GameOptionParseResult;

} // namespace valcraft
