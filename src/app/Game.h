#pragma once

#include "app/ConfirmDialog.h"
#include "app/DeathScreen.h"
#include "app/Hotbar.h"
#include "app/InventoryMenu.h"
#include "app/Audit.h"
#include "app/GameMusic.h"
#include "app/GameOptions.h"
#include "app/MainMenu.h"
#include "app/OptionsMenu.h"
#include "app/PauseMenu.h"
#include "app/PerformanceReport.h"
#include "app/SaveGame.h"
#include "app/SaveSlotMenu.h"
#include "creatures/CreatureSystem.h"
#include "gameplay/ItemDropSystem.h"
#include "gameplay/PlayerController.h"
#include "gameplay/StartingVillage.h"
#include "render/Renderer.h"
#include "world/Environment.h"

#include <SDL.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

namespace valcraft {

class Game {
public:
    explicit Game(GameOptions options = {});
    ~Game();

    auto run() -> int;

private:
    enum class UiScreen : std::uint8_t {
        MainMenu = 0,
        SaveSlots = 1,
        Options = 2,
        Gameplay = 3,
        Inventory = 4,
        Pause = 5,
        Death = 6,
    };

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

    struct AuditSecondAccumulator {
        std::size_t second_index = 0;
        std::vector<double> frame_ms_values {};
        std::vector<double> fps_values {};
        double streaming_ms_total = 0.0;
        double generation_ms_total = 0.0;
        double lighting_ms_total = 0.0;
        double meshing_ms_total = 0.0;
        double upload_ms_total = 0.0;
        double shadow_ms_total = 0.0;
        double world_ms_total = 0.0;
        std::size_t input_raw_events = 0;
        std::size_t input_action_events = 0;
        std::size_t ui_events = 0;
        std::size_t player_events = 0;
        std::size_t block_breaks = 0;
        std::size_t block_places = 0;
        std::size_t stream_chunk_changes = 0;
        std::size_t generation_enqueued = 0;
        std::size_t generation_pruned = 0;
        std::size_t unloaded_chunks = 0;
        std::size_t generated_chunks = 0;
        std::size_t meshed_chunks = 0;
        std::size_t light_nodes_processed = 0;
        std::size_t lighting_jobs_completed = 0;
        std::size_t uploaded_meshes = 0;
        std::size_t visible_chunks_max = 0;
        std::size_t shadow_chunks_max = 0;
        std::size_t world_chunks_max = 0;
        std::size_t pending_generation_max = 0;
        std::size_t pending_mesh_max = 0;
        std::size_t pending_lighting_max = 0;
        std::size_t creature_spawns = 0;
        std::size_t creature_despawns = 0;
        std::size_t creature_attacks = 0;
        std::size_t active_creatures_max = 0;
        std::size_t item_spawns = 0;
        std::size_t item_merges = 0;
        std::size_t item_pickups = 0;
        std::size_t item_expired = 0;
        std::size_t active_item_drops_max = 0;
        std::size_t spike_frames = 0;

        void reset(std::size_t next_second_index) {
            second_index = next_second_index;
            frame_ms_values.clear();
            fps_values.clear();
            streaming_ms_total = 0.0;
            generation_ms_total = 0.0;
            lighting_ms_total = 0.0;
            meshing_ms_total = 0.0;
            upload_ms_total = 0.0;
            shadow_ms_total = 0.0;
            world_ms_total = 0.0;
            input_raw_events = 0;
            input_action_events = 0;
            ui_events = 0;
            player_events = 0;
            block_breaks = 0;
            block_places = 0;
            stream_chunk_changes = 0;
            generation_enqueued = 0;
            generation_pruned = 0;
            unloaded_chunks = 0;
            generated_chunks = 0;
            meshed_chunks = 0;
            light_nodes_processed = 0;
            lighting_jobs_completed = 0;
            uploaded_meshes = 0;
            visible_chunks_max = 0;
            shadow_chunks_max = 0;
            world_chunks_max = 0;
            pending_generation_max = 0;
            pending_mesh_max = 0;
            pending_lighting_max = 0;
            creature_spawns = 0;
            creature_despawns = 0;
            creature_attacks = 0;
            active_creatures_max = 0;
            item_spawns = 0;
            item_merges = 0;
            item_pickups = 0;
            item_expired = 0;
            active_item_drops_max = 0;
            spike_frames = 0;
        }
    };

    auto initialize() -> bool;
    void shutdown();
    void initialize_audit();
    void finalize_audit(const PerformanceRunReport& report, AuditRunStatus status);
    void process_events();
    void update_simulation(float dt, FramePerformanceStats& frame_stats);
    void update_world_pipeline(FramePerformanceStats& frame_stats);
    void set_mouse_capture(bool captured);
    void set_death_screen_visible(bool visible, PlayerDeathCause cause = PlayerDeathCause::None);
    void set_paused(bool paused);
    void set_inventory_visible(bool visible);
    void set_confirm_dialog_visible(bool visible,
                                    ConfirmDialogIntent intent = ConfirmDialogIntent::None,
                                    std::optional<std::size_t> slot_index = std::nullopt);
    void activate_death_screen_action(DeathScreenAction action);
    void activate_pause_menu_action(PauseMenuAction action);
    void activate_main_menu_action(MainMenuAction action);
    void activate_save_slot_selection(std::size_t slot_index);
    void activate_options_menu_action(OptionsMenuAction action);
    void activate_confirm_dialog_choice(ConfirmDialogChoice choice);
    void refresh_death_screen_hover() noexcept;
    void refresh_pause_menu_hover() noexcept;
    void refresh_inventory_hover() noexcept;
    void refresh_main_menu_hover() noexcept;
    void refresh_save_slot_menu_hover() noexcept;
    void refresh_options_menu_hover() noexcept;
    void refresh_confirm_dialog_hover() noexcept;
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
    void update_menu_preview_camera(float dt);
    void validate_smoke_frame(const WorldWorkBudget& budget, const WorldWorkStats& stats) const;
    void record_frame_stats(const FramePerformanceStats& frame_stats);
    void record_performance_event(PerformanceEventKind kind, const BlockCoord& block, std::string_view label);
    void record_audit_event(AuditEventCategory category,
                            std::string_view kind,
                            AuditSeverity severity,
                            std::string payload_json,
                            AuditPriority priority = AuditPriority::Normal);
    void record_raw_input_event(const SDL_Event& event);
    void note_audit_event(AuditEventCategory category, std::string_view kind);
    void note_frame_for_audit(const FramePerformanceStats& frame_stats);
    void flush_audit_second_sample(bool force);
    [[nodiscard]] auto make_audit_frame_sample(const FramePerformanceStats& frame_stats) const -> AuditFrameSample;
    [[nodiscard]] auto should_capture_performance() const noexcept -> bool;
    [[nodiscard]] auto build_performance_report() const -> PerformanceRunReport;
    void write_performance_report(const PerformanceRunReport& report) const;
    [[nodiscard]] auto active_ui_screen() const noexcept -> UiScreen;
    [[nodiscard]] auto front_end_visible() const noexcept -> bool;
    [[nodiscard]] auto gameplay_interaction_blocked() const noexcept -> bool;
    [[nodiscard]] auto render_player() const noexcept -> const PlayerController&;
    [[nodiscard]] auto streaming_focus_position() const noexcept -> glm::vec3;
    [[nodiscard]] auto current_renderer_options() const noexcept -> RendererOptions;
    [[nodiscard]] auto resolve_save_root_directory() const -> std::filesystem::path;
    [[nodiscard]] auto make_world_snapshot() const -> SaveGameSnapshot;
    void configure_starting_village(bool enabled, bool apply_layout_to_world);
    void apply_renderer_options();
    void pump_loading_events() noexcept;
    void present_loading_screen(std::string_view title, std::string_view detail, float progress);
    [[nodiscard]] auto preload_readiness(const glm::vec3& focus, int radius) const -> float;
    void refresh_save_slots();
    void initialize_preview_world();
    void prime_world_around(const glm::vec3& focus, std::string_view loading_title, std::string_view loading_detail);
    void prepare_game_session();
    void open_main_menu(bool from_session = false);
    void open_save_slot_menu(SaveSlotMenuMode mode, SaveSlotMenuParent parent);
    void open_options_menu(OptionsMenuParent parent);
    void close_frontend_menu_to_parent();
    void request_return_to_main_menu();
    void start_new_game_in_slot(std::size_t slot_index);
    auto load_game_from_slot(std::size_t slot_index) -> bool;
    void load_snapshot_into_session(const SaveGameSnapshot& snapshot, std::optional<std::size_t> slot_index);
    void save_game_to_slot(std::size_t slot_index);
    void mark_session_dirty() noexcept;
    void sync_menu_preview_environment() noexcept;

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
    double audit_elapsed_ms_ = 0.0;
    std::size_t frame_raw_input_events_ = 0;
    std::size_t frame_input_action_events_ = 0;

    EnvironmentClock environment_ {};
    Renderer renderer_ {};
    GameMusic music_ {};
    World world_ {};
    PlayerController player_ {};
    PlayerController preview_player_ {};
    CreatureSystem creatures_ {};
    ItemDropSystem item_drops_ {};
    HotbarState hotbar_ = make_default_hotbar_state();
    InventoryMenuState inventory_menu_ = make_default_inventory_menu_state();
    DeathScreenState death_screen_ {};
    PauseMenuState pause_menu_ {};
    MainMenuState main_menu_ {};
    SaveSlotMenuState save_slot_menu_ {};
    OptionsMenuState options_menu_ {};
    ConfirmDialogState confirm_dialog_ {};
    glm::vec3 spawn_position_ {0.5F, 70.0F, 0.5F};
    StartingVillageLayout starting_village_ {};
    GameOptions options_ {};
    std::unique_ptr<AuditRecorder> audit_ {};
    std::vector<FramePerformanceSample> frame_samples_ {};
    std::vector<PerformanceEvent> performance_events_ {};
    std::vector<ItemDropRenderInstance> item_drop_render_instances_ {};
    std::filesystem::path save_root_directory_ {};
    std::optional<std::size_t> active_save_slot_ {};
    std::optional<std::size_t> pending_confirm_slot_ {};
    float menu_preview_time_of_day_ = 8.25F;
    float preview_orbit_radians_ = 0.0F;
    bool has_active_session_ = false;
    bool session_dirty_ = false;
    bool starting_village_enabled_ = false;
    bool runtime_shadows_enabled_ = true;
    bool runtime_post_process_enabled_ = false;
    UiScreen last_audit_ui_screen_ = UiScreen::Gameplay;
    bool last_audit_mouse_captured_ = true;
    AuditSecondAccumulator audit_second_accumulator_ {};
};

} // namespace valcraft
