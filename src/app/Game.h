#pragma once

#include "app/GameOptions.h"
#include "app/PerformanceReport.h"
#include "gameplay/PlayerController.h"
#include "render/Renderer.h"
#include "world/Environment.h"

#include <SDL.h>

#include <cstddef>
#include <vector>

namespace valcraft {

class Game {
public:
    explicit Game(GameOptions options = {});
    ~Game();

    auto run() -> int;

private:
    struct FramePerformanceStats {
        double frame_total_ms = 0.0;
        double streaming_ms = 0.0;
        double generation_ms = 0.0;
        double lighting_ms = 0.0;
        double meshing_ms = 0.0;
        double upload_ms = 0.0;
        double shadow_ms = 0.0;
        double world_ms = 0.0;
        std::size_t generated_chunks = 0;
        std::size_t meshed_chunks = 0;
        std::size_t light_nodes_processed = 0;
        std::size_t uploaded_meshes = 0;
        std::size_t pending_generation = 0;
        std::size_t pending_mesh = 0;
        std::size_t pending_lighting = 0;
        std::size_t stream_chunk_changes = 0;
        std::size_t generation_enqueued = 0;
        std::size_t generation_pruned = 0;
        std::size_t unloaded_chunks = 0;
        std::size_t lighting_jobs_completed = 0;
        std::size_t visible_chunks = 0;
        std::size_t shadow_chunks = 0;
        std::size_t world_chunks = 0;
    };

    auto initialize() -> bool;
    void shutdown();
    void process_events();
    void update(float dt, FramePerformanceStats& frame_stats);
    void set_mouse_capture(bool captured);
    void select_block_from_key(SDL_Keycode keycode);
    void update_smoke_player(float dt);
    void validate_smoke_frame(const WorldWorkBudget& budget, const WorldWorkStats& stats) const;
    void record_frame_stats(const FramePerformanceStats& frame_stats);
    [[nodiscard]] auto should_capture_performance() const noexcept -> bool;
    [[nodiscard]] auto build_performance_report() const -> PerformanceRunReport;
    void write_performance_report(const PerformanceRunReport& report) const;

    SDL_Window* window_ = nullptr;
    SDL_GLContext gl_context_ = nullptr;
    bool running_ = true;
    bool mouse_captured_ = true;
    bool pending_toggle_fly_ = false;
    bool pending_break_block_ = false;
    bool pending_place_block_ = false;
    float pending_look_x_ = 0.0F;
    float pending_look_y_ = 0.0F;
    int window_width_ = 1600;
    int window_height_ = 900;
    int rendered_frames_ = 0;
    float smoke_elapsed_seconds_ = 0.0F;

    EnvironmentClock environment_ {};
    Renderer renderer_ {};
    World world_ {};
    PlayerController player_ {};
    GameOptions options_ {};
    std::vector<FramePerformanceSample> frame_samples_ {};
};

} // namespace valcraft
