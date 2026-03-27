#pragma once

#include "app/DeathScreen.h"
#include "app/Hotbar.h"
#include "app/InventoryMenu.h"
#include "app/PauseMenu.h"
#include "app/GameOptions.h"
#include "app/PerformanceReport.h"
#include "creatures/CreatureSystem.h"
#include "gameplay/ItemDropSystem.h"
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
    void update_simulation(float dt, FramePerformanceStats& frame_stats);
    void update_world_pipeline(FramePerformanceStats& frame_stats);
    void set_mouse_capture(bool captured);
    void set_death_screen_visible(bool visible, PlayerDeathCause cause = PlayerDeathCause::None);
    void set_paused(bool paused);
    void set_inventory_visible(bool visible);
    void activate_death_screen_action(DeathScreenAction action);
    void activate_pause_menu_action(PauseMenuAction action);
    void refresh_death_screen_hover() noexcept;
    void refresh_pause_menu_hover() noexcept;
    void refresh_inventory_hover() noexcept;
    void click_inventory_slot(bool secondary);
    void assign_hovered_inventory_slot_to_hotbar(std::size_t hotbar_index) noexcept;
    void drop_selected_hotbar_items(bool full_stack) noexcept;
    void drop_hovered_inventory_stack(bool full_stack) noexcept;
    void drop_carried_inventory_stack(bool full_stack) noexcept;
    void spawn_dropped_stack(const HotbarSlot& stack, const glm::vec3& origin, const glm::vec3& initial_velocity) noexcept;
    void sync_selected_hotbar_slot() noexcept;
    void select_hotbar_slot(std::size_t index) noexcept;
    void cycle_hotbar_selection(int delta) noexcept;
    void select_hotbar_slot_from_keycode(SDL_Keycode keycode);
    [[nodiscard]] auto find_initial_spawn_position() -> glm::vec3;
    void respawn_player();
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
    bool death_screen_visible_ = false;
    bool paused_ = false;
    bool inventory_visible_ = false;
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
    CreatureSystem creatures_ {};
    ItemDropSystem item_drops_ {};
    HotbarState hotbar_ = make_default_hotbar_state();
    InventoryMenuState inventory_menu_ = make_default_inventory_menu_state();
    DeathScreenState death_screen_ {};
    PauseMenuState pause_menu_ {};
    glm::vec3 spawn_position_ {0.5F, 70.0F, 0.5F};
    GameOptions options_ {};
    std::vector<FramePerformanceSample> frame_samples_ {};
    std::vector<ItemDropRenderInstance> item_drop_render_instances_ {};
};

} // namespace valcraft
