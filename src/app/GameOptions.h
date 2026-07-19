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
    std::size_t fluid_cell_budget = 256;
    std::size_t mesh_rebuild_budget = 4;
    std::size_t light_node_budget = 8192;
    double max_generation_ms = 1.0;
    double max_fluid_ms = 1.0;
    double max_lighting_ms = 1.5;
    double max_meshing_ms = 2.0;
    int stream_radius = 5;
    bool shadows_enabled = true;
    int shadow_map_size = 1024;
    bool post_process_enabled = true;
    bool adaptive_quality = true;
    bool report_frame_stats = false;
    std::string perf_json_path {};
    bool perf_trace_enabled = false;
    std::string perf_scenario {};
    std::size_t perf_warmup_frames = 0;

    [[nodiscard]] auto world_budget() const noexcept -> WorldWorkBudget {
        WorldWorkBudget budget {};
        budget.chunk_generation_budget = chunk_generation_budget;
        budget.fluid_cell_budget = fluid_cell_budget;
        budget.mesh_rebuild_budget = mesh_rebuild_budget;
        budget.light_node_budget = light_node_budget;
        budget.max_generation_ms = max_generation_ms;
        budget.max_fluid_ms = max_fluid_ms;
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

enum class SmokeSessionMode {
    Menu = 0,
    SeaNew = 1,
    SeaLegacy = 2,
};

enum class SmokeShipView {
    None = 0,
    Deck = 1,
    Bow = 2,
    Stern = 3,
    Port = 4,
    Starboard = 5,
    Interior = 6,
    CaptainCabin = 7,
    CargoHold = 8,
    CrewDeck = 9,
};

struct GameOptions {
    bool smoke_test = false;
    int smoke_frames = 60;
    SmokeSessionMode smoke_session = SmokeSessionMode::Menu;
    SmokeShipView smoke_ship_view = SmokeShipView::None;
    bool hidden_window = false;
    int window_width = 1600;
    int window_height = 900;
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
