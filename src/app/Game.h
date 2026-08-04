#pragma once

#include "app/CommandConsole.h"
#include "app/ConfirmDialog.h"
#include "app/ConstructionPlanEditor.h"
#include "app/DeathScreen.h"
#include "app/Hotbar.h"
#include "app/InventoryMenu.h"
#include "app/LoadingScreen.h"
#include "app/Audit.h"
#include "app/GameMusic.h"
#include "app/GameOptions.h"
#include "app/MainMenu.h"
#include "app/OptionsMenu.h"
#include "app/PauseMenu.h"
#include "app/PerformanceReport.h"
#include "app/ProcessMemory.h"
#include "app/ProgressionMenu.h"
#include "app/SaveGame.h"
#include "app/SaveSlotMenu.h"
#include "app/SessionSaveState.h"
#include "app/TerrainEditStress.h"
#include "creatures/CreatureSystem.h"
#include "creatures/bosses/ChainedColossus.h"
#include "creatures/legendary/LegendaryEnemySystem.h"
#include "gameplay/BackroomsFlashlight.h"
#include "gameplay/BackroomsJack.h"
#include "gameplay/BackroomsMarlow.h"
#include "gameplay/BackroomsSimulationTime.h"
#include "gameplay/BackroomsThreatArbiter.h"
#include "gameplay/ItemDropSystem.h"
#include "gameplay/MusketCombat.h"
#include "gameplay/PlayerController.h"
#include "gameplay/PlayerMusket.h"
#include "gameplay/PlayerMusketEffects.h"
#include "gameplay/PlayerProgression.h"
#include "gameplay/SeaAdventure.h"
#include "gameplay/StartingVillage.h"
#include "gameplay/combat/ColossalSweep.h"
#include "gameplay/combat/WorldProtectionRegistry.h"
#include "gameplay/encounters/SeaLeviathanEncounter.h"
#include "gameplay/quests/LegendaryQuestWorldContent.h"
#include "gameplay/quests/LegendaryWeaponQuest.h"
#include "gameplay/progression/AbilitySystem.h"
#include "gameplay/progression/ExperienceAwardService.h"
#include "gameplay/progression/PlayerAbilityEffects.h"
#include "gameplay/progression/SummonedUnitSystem.h"
#include "gameplay/progression/WorldEditTransaction.h"
#include "gameplay/scenarios/IssouArenaScenario.h"
#include "gameplay/scenarios/ScenarioSessionState.h"
#include "gameplay/weapons/ColossalWeaponSystem.h"
#include "gameplay/weapons/LeviathanKnightSynergy.h"
#include "gameplay/weapons/LegendaryWeaponProgression.h"
#include "render/Renderer.h"
#include "render/creatures/ChainedColossusPresentation.h"
#include "render/scenarios/IssouArenaPresentation.h"
#include "render/weapons/LeviathanWeaponPresentation.h"
#include "world/Environment.h"

#include <SDL.h>

#include <chrono>
#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <future>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace valcraft {

class Game {
public:
    explicit Game(const GameOptions& options = {});
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
        CommandConsole = 7,
        Progression = 8,
    };

    struct FramePerformanceStats {
        std::size_t frame_index = 0;
        double frame_total_ms = 0.0;
        double streaming_ms = 0.0;
        double generation_ms = 0.0;
        double lighting_ms = 0.0;
        double meshing_ms = 0.0;
        double upload_ms = 0.0;
        double shadow_ms = 0.0;
        double world_ms = 0.0;
        double event_processing_ms = 0.0;
        double simulation_ms = 0.0;
        double audio_ms = 0.0;
        double render_preparation_ms = 0.0;
        double fluid_ms = 0.0;
        double render_cpu_ms = 0.0;
        double render_overhead_ms = 0.0;
        double present_ms = 0.0;
        double telemetry_ms = 0.0;
        double residual_ms = 0.0;
        double gpu_shadow_ms = 0.0;
        double gpu_world_ms = 0.0;
        double gpu_sky_ms = 0.0;
        double gpu_water_ms = 0.0;
        double gpu_entities_ms = 0.0;
        double gpu_post_process_ms = 0.0;
        double gpu_hud_ms = 0.0;
        double gpu_frame_ms = 0.0;
        std::size_t gpu_source_frame = 0;
        std::size_t gpu_latency_frames = 0;
        bool gpu_timing_valid = false;
        std::uint8_t resolved_quality = 0;
        double adaptive_frame_ema_ms = 0.0;
        double adaptive_frame_p95_ms = 0.0;
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
        std::size_t processed_fluid_cells = 0;
        std::size_t pending_fluid = 0;
        std::size_t fixed_updates = 0;
        std::size_t dropped_fixed_updates = 0;
        std::size_t draw_calls = 0;
        std::uint64_t triangles = 0;
        std::uint64_t uploaded_bytes = 0;
        std::uint64_t world_cpu_bytes = 0;
        std::uint64_t mesh_cpu_bytes = 0;
        std::uint64_t override_bytes = 0;
        std::uint64_t gpu_buffer_bytes = 0;
        std::uint64_t gpu_texture_bytes = 0;
        // Je conserve gpu_water_ms comme agregat historique et j'ajoute les
        // trois passes modernes a la fin pour ne pas changer son sens.
        double gpu_water_resolve_ms = 0.0;
        double gpu_water_surface_ms = 0.0;
        double gpu_transparent_weather_ms = 0.0;
    };

    struct GameplayAnnouncement {
        std::string title {};
        std::string detail {};
        float elapsed_seconds = 0.0F;
        float duration_seconds = 3.25F;
    };

    struct ScenarioLegendaryRuntimeRestore {
        static constexpr std::size_t
            kSummonCapacity = 8U;

        // Je conserve ici les etats runtime que le format de sauvegarde
        // normalise volontairement. La sortie de /issou peut ainsi restituer
        // la frame de jeu normale sans perdre les vitesses, evenements ou
        // minuteries transitoires.
        EnvironmentClock environment {};
        PlayerController player {};
        PlayerMusketController player_musket {};
        PlayerMusketEffects player_musket_effects {};
        PlayerProgression progression {};
        LegendaryWeaponProgression
            weapon_progression {};
        ExperienceAwardService
            experience_awards {};
        PlayerBuildState player_build {};
        AbilitySystem ability_system {};
        PlayerAbilityEffects ability_effects {};
        CreatureSystem creatures {};
        ItemDropSystem item_drops {};
        SeaAdventureSystem sea_adventure {};
        HotbarState hotbar {};
        InventoryMenuState inventory {};
        StartingVillageLayout starting_village {};
        std::array<
            SummonedUnitSystem,
            kSummonCapacity>
            summoned_footmen {};
        std::array<
            std::optional<glm::vec3>,
            kSummonCapacity>
            summoned_footman_ship_local_positions {};
        std::array<
            float,
            kSummonCapacity>
            summoned_footman_far_seconds {};
        std::array<
            AbilityCastSequence,
            kSummonCapacity>
            summoned_footman_cast_sequences {};
        ColossalWeaponSystem weapon {};
        LeviathanKnightSynergyRuntime
            knight_synergy {};
        ColossalHitLedger hit_ledger {};
        ColossalBladePose previous_blade_pose {};
        WorldProtectionRegistry protections {};
        ColossusBloodTraceBuffer blood_traces {};
        LegendaryEnemySystem enemies {};
        SeaLeviathanEncounter sea_encounter {};
        std::optional<std::size_t>
            bound_musket_hotbar_slot {};
        std::optional<std::size_t>
            pending_ability_slot {};
        glm::vec3 spawn_position {
            0.5F,
            70.0F,
            0.5F,
        };
        GameMode active_game_mode =
            GameMode::ClassicAdventure;
        std::uint64_t wall_impact_sequence = 0U;
        std::uint64_t shockwave_sequence = 0U;
        LegendaryEnemyId quest_guardian_id = 0U;
        LegendaryEnemyId astral_boss_id = 0U;
        AbilityCastSequence
            wind_acceleration_cast_sequence = 0U;
        float melee_attack_cooldown_remaining = 0.0F;
        float wind_acceleration_remaining = 0.0F;
        float wind_movement_bonus = 0.0F;
        float wind_recovery_bonus = 0.0F;
        float wind_dodge_remaining = 0.0F;
        float seconds_since_successful_shield_block = -1.0F;
        float backrooms_elapsed_seconds = 0.0F;
        BackroomsFlashlightState backrooms_flashlight {};
        BackroomsJackState backrooms_jack {};
        BackroomsJackRuntime backrooms_jack_runtime {};
        BackroomsJackUpdateResult
            backrooms_jack_last_result {};
        float backrooms_jack_death_delay_seconds = 0.0F;
        bool backrooms_jack_death_pending = false;
        BackroomsMarlowState backrooms_marlow {};
        BackroomsMarlowRuntime backrooms_marlow_runtime {};
        BackroomsMarlowUpdateResult backrooms_marlow_last_result {};
        BackroomsThreatArbiterRuntime backrooms_threat_arbiter {};
        std::uint64_t backrooms_threat_request_sequence = 0U;
        glm::vec3 backrooms_marlow_previous_player_position {0.0F};
        float backrooms_marlow_death_delay_seconds = 0.0F;
        bool backrooms_marlow_death_pending = false;
        bool backrooms_marlow_previous_in_water = false;
        bool backrooms_marlow_previous_on_ground = true;
        bool backrooms_marlow_previous_jump_input = false;
        bool backrooms_marlow_has_previous_player_position = false;
        bool backrooms_marlow_poolrooms_active = false;
        bool blade_pose_valid = false;
        bool weapon_was_selected = false;
        bool sea_encounter_started = false;
        bool wind_blade_available = false;
        bool super_vision_active = false;
        bool starting_village_enabled = false;
        bool quest_tutorial_spawned = false;
    };

    struct AuditSecondAccumulator {
        std::size_t second_index = 0;
        std::vector<double> frame_ms_values {};
        std::vector<double> fps_values {};
        double event_processing_ms_total = 0.0;
        double simulation_ms_total = 0.0;
        double audio_ms_total = 0.0;
        double render_preparation_ms_total = 0.0;
        double streaming_ms_total = 0.0;
        double generation_ms_total = 0.0;
        double fluid_ms_total = 0.0;
        double lighting_ms_total = 0.0;
        double meshing_ms_total = 0.0;
        double upload_ms_total = 0.0;
        double shadow_ms_total = 0.0;
        double world_ms_total = 0.0;
        double render_cpu_ms_total = 0.0;
        double render_overhead_ms_total = 0.0;
        double present_ms_total = 0.0;
        double telemetry_ms_total = 0.0;
        double residual_ms_total = 0.0;
        double gpu_frame_ms_total = 0.0;
        std::size_t gpu_timing_samples = 0;
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
        std::size_t pending_fluid_max = 0;
        std::uint64_t process_working_set_bytes_max = 0;
        std::uint64_t process_private_bytes_max = 0;
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
            event_processing_ms_total = 0.0;
            simulation_ms_total = 0.0;
            audio_ms_total = 0.0;
            render_preparation_ms_total = 0.0;
            streaming_ms_total = 0.0;
            generation_ms_total = 0.0;
            fluid_ms_total = 0.0;
            lighting_ms_total = 0.0;
            meshing_ms_total = 0.0;
            upload_ms_total = 0.0;
            shadow_ms_total = 0.0;
            world_ms_total = 0.0;
            render_cpu_ms_total = 0.0;
            render_overhead_ms_total = 0.0;
            present_ms_total = 0.0;
            telemetry_ms_total = 0.0;
            residual_ms_total = 0.0;
            gpu_frame_ms_total = 0.0;
            gpu_timing_samples = 0;
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
            pending_fluid_max = 0;
            process_working_set_bytes_max = 0;
            process_private_bytes_max = 0;
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
    void resolve_pending_player_ability(
        bool maritime_session_active);
    void update_summoned_footman(
        float dt,
        bool maritime_session_active);
    void consume_ability_logical_events();
    void rebuild_progression_creature_render_instances(
        const EnvironmentState& environment);
    void grant_creature_kill_rewards(
        const CreatureDamageResult& result,
        std::string_view source);
    void update_world_pipeline(FramePerformanceStats& frame_stats);
    void set_mouse_capture(bool captured);
    [[nodiscard]] auto can_open_command_console() const noexcept -> bool;
    void set_command_console_visible(bool visible);
    void refresh_command_console_text_input_rect() noexcept;
    void handle_command_console_keydown(const SDL_KeyboardEvent& event);
    void submit_command_console();
    void set_death_screen_visible(bool visible, PlayerDeathCause cause = PlayerDeathCause::None);
    void set_paused(bool paused);
    void set_inventory_visible(bool visible);
    void set_progression_menu_visible(bool visible);
    void handle_progression_menu_keydown(
        const SDL_KeyboardEvent& event);
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
    void craft_inventory_tool(BlockId tool_id);
    void assign_hovered_inventory_slot_to_hotbar(std::size_t hotbar_index) noexcept;
    void drop_selected_hotbar_items(bool full_stack) noexcept;
    void drop_hovered_inventory_stack(bool full_stack) noexcept;
    void drop_carried_inventory_stack(bool full_stack) noexcept;
    void spawn_dropped_stack(const HotbarSlot& stack, const glm::vec3& origin, const glm::vec3& initial_velocity) noexcept;
    void award_player_experience(
        const ExperienceAwardEvent& event,
        const BlockCoord& activity_block,
        std::string_view source);
    void toggle_super_vision();
    void queue_gameplay_announcement(std::string title, std::string detail, float duration_seconds = 3.25F);
    void queue_level_up_announcements(std::uint32_t previous_level, std::uint32_t current_level);
    void update_gameplay_announcements(float dt) noexcept;
    [[nodiscard]] auto selected_musket_active() const noexcept -> bool;
    void reset_musket_interaction(bool forget_bound_slot = true) noexcept;
    void resolve_player_musket_shot(
        const PlayerMusketEvents& shot,
        bool maritime_session_active);
    [[nodiscard]] auto current_wind_velocity(
        const EnvironmentState& environment) const noexcept -> glm::vec3;
    [[nodiscard]] auto current_gameplay_announcement_view() const noexcept -> GameplayHudAnnouncementView;
    [[nodiscard]] auto current_maritime_hud_view() const noexcept -> MaritimeHudView;
    void prepare_legendary_presentation(
        float animation_time_seconds);
    [[nodiscard]] auto selected_colossal_weapon_active() const noexcept
        -> bool;
    [[nodiscard]] auto colossal_weapon_drawn() const noexcept -> bool;
    [[nodiscard]] auto intercept_colossal_guard(
        const ColossalGuardRequest& request,
        std::uint8_t allies_behind = 0U) noexcept
        -> ColossalGuardResult;
    void clear_colossal_weapon_input() noexcept;
    void update_colossal_weapon(
        float dt,
        const PlayerInput& movement_input,
        bool gameplay_input_enabled,
        bool maritime_session_active);
    void resolve_colossal_weapon_sweep(
        bool maritime_session_active);
    void update_legendary_encounters(
        float dt,
        bool maritime_session_active);
    void configure_legendary_weapon_quest();
    [[nodiscard]] auto
    apply_legendary_quest_world_content() -> bool;
    void update_legendary_weapon_quest();
    [[nodiscard]] auto
    try_interact_legendary_weapon_quest() -> bool;
    [[nodiscard]] auto
    process_legendary_quest_request(
        const LegendaryQuestRequest& request)
        -> LegendaryQuestProcessResult;
    void record_legendary_quest_tutorial(
        LegendaryQuestAction action,
        std::uint64_t target_id,
        float combat_value);
    void consume_legendary_quest_events();
    void process_colossus_attack_events();
    [[nodiscard]] auto enter_issou_scenario() -> bool;
    [[nodiscard]] auto reset_issou_scenario() -> bool;
    void initialize_issou_run_state(
        const IssouArenaLayout& layout,
        bool rebuild_world);
    [[nodiscard]] auto exit_issou_scenario() -> bool;
    void update_issou_scenario(float dt);
    void consume_issou_scenario_events();
    void restore_scenario_snapshot(
        ScenarioSessionRestore restore);
    void rebuild_colossal_world_protections() noexcept;
    void sync_selected_hotbar_slot() noexcept;
    [[nodiscard]] auto selected_tool_break_speed_multiplier(BlockId target_block_id) const noexcept -> float;
    void select_hotbar_slot(std::size_t index) noexcept;
    void cycle_hotbar_selection(int delta) noexcept;
    void select_hotbar_slot_from_keycode(SDL_Keycode keycode);
    [[nodiscard]] auto find_initial_spawn_position() -> glm::vec3;
    [[nodiscard]] auto find_initial_spawn_position(
        World& world,
        const StartingVillageLayout* starting_village) -> glm::vec3;
    void respawn_player();
    void update_smoke_player(float dt);
    void update_smoke_ship_camera();
    void update_menu_preview_camera(float dt);
    void validate_smoke_frame(const WorldWorkBudget& budget, const WorldWorkStats& stats) const;
    void validate_backrooms_marlow_smoke_completion() const;
    void capture_current_frame_if_requested();
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
    [[nodiscard]] auto make_performance_sample(const FramePerformanceStats& frame_stats) const -> FramePerformanceSample;
    [[nodiscard]] auto make_audit_frame_sample(const FramePerformanceStats& frame_stats) const -> AuditFrameSample;
    [[nodiscard]] auto should_capture_performance() const noexcept -> bool;
    [[nodiscard]] auto build_performance_report() const -> PerformanceRunReport;
    void write_performance_report(const PerformanceRunReport& report) const;
    [[nodiscard]] auto active_ui_screen() const noexcept -> UiScreen;
    [[nodiscard]] auto front_end_visible() const noexcept -> bool;
    [[nodiscard]] auto gameplay_interaction_blocked() const noexcept -> bool;
    [[nodiscard]] auto backrooms_active() const noexcept -> bool;
    [[nodiscard]] auto current_backrooms_level() const noexcept -> int;
    [[nodiscard]] auto session_backrooms_supports_jack() const noexcept
        -> bool;
    [[nodiscard]] auto backrooms_jack_jumpscare_active() const noexcept
        -> bool;
    [[nodiscard]] auto backrooms_marlow_cinematic_active() const noexcept
        -> bool;
    [[nodiscard]] auto backrooms_cinematic_active() const noexcept -> bool;
    [[nodiscard]] auto current_environment_state() const -> EnvironmentState;
    void reset_backrooms_jack_runtime() noexcept;
    void update_backrooms_simulation(float dt);
    [[nodiscard]] auto render_player() const noexcept -> const PlayerController&;
    [[nodiscard]] auto streaming_focus_position() const noexcept -> glm::vec3;
    [[nodiscard]] auto current_renderer_options() const noexcept -> RendererOptions;
    [[nodiscard]] auto resolve_save_root_directory() const -> std::filesystem::path;
    [[nodiscard]] auto make_world_snapshot() const -> SaveGameSnapshot;
    void configure_starting_village(bool enabled, bool apply_layout_to_world);
    [[nodiscard]] auto active_generation_profile() const noexcept -> WorldGenerationProfile;
    void apply_renderer_options();
    void pump_loading_events() noexcept;
    void begin_loading_screen(LoadingScreenTheme theme, std::uint32_t quote_seed) noexcept;
    void record_loading_step(
        std::string_view label,
        std::chrono::steady_clock::time_point started_at) noexcept;
    [[nodiscard]] auto prepare_ship_mesh_during_loading(
        const ShipRenderState& ship,
        std::string_view loading_title,
        bool restoring) -> bool;
    void update_loading_screen(std::string_view title,
                               std::string_view detail,
                               LoadingPhase phase,
                               float local_progress,
                               bool force = false);
    void present_loading_screen(std::string_view title,
                                std::string_view detail,
                                float progress,
                                bool force = false);
    void complete_loading_screen(std::string_view title, std::string_view detail);
    [[nodiscard]] auto initial_preload_targets(const World& world, const glm::vec3& focus) const
        -> std::vector<ChunkCoord>;
    [[nodiscard]] auto preload_readiness(
        const World& world,
        std::span<const ChunkCoord> targets) const -> float;
    [[nodiscard]] auto preload_gpu_readiness(
        const World& world,
        std::span<const ChunkCoord> targets) const -> float;
    void refresh_save_slots();
    auto finish_pending_save(bool wait_for_completion) -> bool;
    [[nodiscard]] auto wait_for_pending_save_during_loading(std::string_view title) -> bool;
    void finish_pending_world_release(bool wait_for_completion);
    [[nodiscard]] auto wait_for_pending_world_release_during_loading(std::string_view title) -> bool;
    void install_prepared_world(World prepared_world);
    [[nodiscard]] auto reset_renderer_world_resources_during_loading(std::string_view title) -> bool;
    void initialize_preview_world();
    void prime_world_around(
        World& world,
        const glm::vec3& focus,
        std::string_view loading_title,
        std::string_view loading_detail);
    void prepare_game_session(
        std::uint64_t musket_shot_sequence = 0U);
    void open_main_menu(bool from_session = false);
    void open_save_slot_menu(SaveSlotMenuMode mode,
                             SaveSlotMenuParent parent,
                             GameMode new_game_mode = GameMode::ClassicAdventure);
    void open_options_menu(OptionsMenuParent parent);
    void close_frontend_menu_to_parent();
    void request_return_to_main_menu();
    void start_new_game_in_slot(std::size_t slot_index, GameMode game_mode);
    auto load_game_from_slot(std::size_t slot_index) -> bool;
    [[nodiscard]] auto load_snapshot_into_session(
        SaveGameSnapshot snapshot,
        std::optional<std::size_t> slot_index) -> bool;
    [[nodiscard]] auto start_smoke_session() -> bool;
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
    bool pending_primary_attack_ = false;
    bool pending_place_block_ = false;
    bool pending_fishing_ = false;
    bool musket_fire_held_ = false;
    bool pending_musket_fire_press_ = false;
    bool musket_aim_held_ = false;
    bool pending_musket_reload_ = false;
    bool colossal_primary_held_ = false;
    bool pending_colossal_primary_press_ = false;
    bool pending_colossal_primary_release_ = false;
    bool colossal_guard_held_ = false;
    bool pending_colossal_guard_press_ = false;
    bool pending_colossal_guard_release_ = false;
    bool colossal_weapon_was_selected_ = false;
    bool colossal_blade_pose_valid_ = false;
    float pending_look_x_ = 0.0F;
    float pending_look_y_ = 0.0F;
    int window_width_ = 1600;
    int window_height_ = 900;
    int rendered_frames_ = 0;
    float smoke_elapsed_seconds_ = 0.0F;
    glm::vec3 backrooms_smoke_camera_position_ {0.0F};
    float backrooms_smoke_camera_yaw_degrees_ = -90.0F;
    bool backrooms_smoke_camera_pose_valid_ = false;
    bool backrooms_marlow_smoke_checkpoint_reached_ = false;
    std::size_t backrooms_marlow_smoke_update_count_ = 0U;
    double audit_elapsed_ms_ = 0.0;
    std::size_t frame_raw_input_events_ = 0;
    std::size_t frame_input_action_events_ = 0;
    ProcessMemorySnapshot last_process_memory_ {};
    WorldMemoryStats last_world_memory_ {};

    EnvironmentClock environment_ {};
    Renderer renderer_ {};
    GameMusic music_ {};
    World world_ {};
    TerrainEditStressScenario terrain_edit_stress_ {};
    PlayerController player_ {};
    PlayerMusketController player_musket_ {};
    PlayerMusketEffects player_musket_effects_ {};
    std::optional<std::size_t> bound_musket_hotbar_slot_ {};
    PlayerController preview_player_ {};
    PlayerProgression progression_ {};
    LegendaryWeaponProgression
        legendary_weapon_progression_ {};
    LegendaryWeaponQuest
        legendary_weapon_quest_ {};
    std::optional<LegendaryQuestWorldContentPlan>
        legendary_quest_world_content_ {};
    std::array<
        bool,
        kLegendaryQuestWorldSceneCount>
        legendary_quest_world_scenes_applied_ {};
    ColossalWeaponSystem colossal_weapon_ {};
    LeviathanKnightSynergyRuntime
        leviathan_knight_synergy_ {};
    ColossalHitLedger colossal_hit_ledger_ {};
    ColossalBladePose previous_colossal_blade_pose_ {};
    std::uint64_t colossal_wall_impact_sequence_ = 0U;
    std::uint64_t colossal_shockwave_sequence_ = 0U;
    std::vector<LeviathanVisualEvent>
        pending_leviathan_visual_events_ {};
    WorldProtectionRegistry colossal_world_protections_ {};
    IssouArenaScenario issou_scenario_ {};
    ScenarioSessionState scenario_session_ {};
    ChainedColossus chained_colossus_ {};
    ColossusBloodTraceBuffer colossus_blood_traces_ {};
    LegendaryEnemySystem legendary_enemies_ {};
    LegendaryEnemyId
        legendary_quest_guardian_id_ = 0U;
    LegendaryEnemyId
        legendary_astral_boss_id_ = 0U;
    bool legendary_quest_tutorial_spawned_ = false;
    SeaLeviathanEncounter sea_leviathan_ {};
    bool sea_leviathan_started_for_session_ = false;
    bool issou_arena_minions_spawned_ = false;
    IssouArenaEventKind latest_issou_event_ =
        IssouArenaEventKind::CrowdMurmur;
    std::optional<ScenarioLegendaryRuntimeRestore>
        scenario_legendary_runtime_restore_ {};
    std::uint32_t issou_run_sequence_ = 0U;
    ExperienceAwardService experience_awards_ {};
    PlayerBuildState player_build_ {};
    AbilitySystem ability_system_ {};
    PlayerAbilityEffects player_ability_effects_ {};
    ProgressionMenu progression_menu_ {};
    ConstructionPlanEditor
        construction_plan_editor_ {};
    static constexpr std::size_t
        kMaximumPlayerSummons = 8U;
    std::array<
        SummonedUnitSystem,
        kMaximumPlayerSummons>
        summoned_footmen_ {};
    std::array<
        std::optional<glm::vec3>,
        kMaximumPlayerSummons>
        summoned_footman_ship_local_positions_ {};
    std::array<
        float,
        kMaximumPlayerSummons>
        summoned_footman_far_seconds_ {};
    std::array<
        AbilityCastSequence,
        kMaximumPlayerSummons>
        summoned_footman_cast_sequences_ {};
    std::vector<CreatureRenderInstance>
        progression_creature_render_instances_ {};
    std::optional<std::size_t>
        pending_ability_slot_ {};
    float melee_attack_cooldown_remaining_ = 0.0F;
    float wind_acceleration_remaining_ = 0.0F;
    float wind_movement_bonus_ = 0.0F;
    float wind_recovery_bonus_ = 0.0F;
    float wind_dodge_remaining_ = 0.0F;
    bool wind_blade_available_ = false;
    AbilityCastSequence
        wind_acceleration_cast_sequence_ = 0U;
    float seconds_since_successful_shield_block_ = -1.0F;
    std::deque<GameplayAnnouncement> gameplay_announcements_ {};
    bool super_vision_active_ = false;
    CreatureSystem creatures_ {};
    ItemDropSystem item_drops_ {};
    SeaAdventureSystem sea_adventure_ {};
    HotbarState hotbar_ = make_default_hotbar_state();
    InventoryMenuState inventory_menu_ = make_default_inventory_menu_state();
    DeathScreenState death_screen_ {};
    PauseMenuState pause_menu_ {};
    MainMenuState main_menu_ {};
    SaveSlotMenuState save_slot_menu_ {};
    OptionsMenuState options_menu_ {};
    ConfirmDialogState confirm_dialog_ {};
    CommandConsoleToggle command_console_toggle_ {};
    CommandConsole command_console_ {};
    glm::vec3 spawn_position_ {0.5F, 70.0F, 0.5F};
    StartingVillageLayout starting_village_ {};
    GameOptions options_ {};
    std::unique_ptr<AuditRecorder> audit_ {};
    std::vector<FramePerformanceSample> frame_samples_ {};
    std::vector<PerformanceEvent> performance_events_ {};
    std::vector<ItemDropRenderInstance> item_drop_render_instances_ {};
    std::filesystem::path save_root_directory_ {};
    std::optional<std::filesystem::path> smoke_temp_root_ {};
    std::optional<std::size_t> active_save_slot_ {};
    std::optional<std::size_t> pending_confirm_slot_ {};
    std::optional<std::size_t> pending_save_slot_ {};
    std::optional<std::size_t> recording_frame_index_ {};
    std::future<void> pending_save_ {};
    std::future<void> pending_world_release_ {};
    LoadingScreenTheme loading_theme_ = LoadingScreenTheme::Standard;
    LoadingProgressTracker loading_progress_ {};
    std::chrono::steady_clock::time_point loading_started_at_ {};
    std::chrono::steady_clock::time_point loading_last_presented_at_ {};
    std::uint32_t loading_quote_seed_ = 0U;
    float loading_last_presented_progress_ = -1.0F;
    std::string loading_last_title_ {};
    std::string loading_last_detail_ {};
    std::string loading_window_title_ {};
    std::size_t loading_update_count_ = 0U;
    double loading_max_step_ms_ = 0.0;
    std::string_view loading_max_step_label_ {};
    bool loading_active_ = false;
    bool loading_completed_ = false;
    GameMode active_game_mode_ = GameMode::ClassicAdventure;
    // Temps indépendant utilisé pour les variations électriques lentes du mode BackRooms.
    float backrooms_elapsed_seconds_ = 0.0F;
    BackroomsFlashlightState backrooms_flashlight_ {};
    BackroomsJackState backrooms_jack_ {};
    BackroomsJackRuntime backrooms_jack_runtime_ {};
    BackroomsJackUpdateResult
        backrooms_jack_last_result_ {};
    float backrooms_jack_death_delay_seconds_ = 0.0F;
    bool backrooms_jack_death_pending_ = false;
    BackroomsMarlowState backrooms_marlow_ {};
    BackroomsMarlowRuntime backrooms_marlow_runtime_ {};
    BackroomsMarlowUpdateResult backrooms_marlow_last_result_ {};
    BackroomsThreatArbiterRuntime backrooms_threat_arbiter_ {};
    std::uint64_t backrooms_threat_request_sequence_ = 0U;
    glm::vec3 backrooms_marlow_previous_player_position_ {0.0F};
    float backrooms_marlow_death_delay_seconds_ = 0.0F;
    bool backrooms_marlow_death_pending_ = false;
    bool backrooms_marlow_previous_in_water_ = false;
    bool backrooms_marlow_previous_on_ground_ = true;
    bool backrooms_marlow_previous_jump_input_ = false;
    bool backrooms_marlow_has_previous_player_position_ = false;
    bool backrooms_marlow_poolrooms_active_ = false;
    BackroomsResumeState backrooms_resume_state_ {};
    float menu_preview_time_of_day_ = 8.25F;
    float preview_orbit_radians_ = 0.0F;
    bool has_active_session_ = false;
    SessionSaveState session_save_state_ {};
    bool starting_village_enabled_ = false;
    bool runtime_shadows_enabled_ = true;
    bool runtime_post_process_enabled_ = true;
    bool software_frame_pacing_enabled_ = false;
    std::string vsync_mode_ = "unknown";
    bool frame_capture_written_ = false;
    UiScreen last_audit_ui_screen_ = UiScreen::Gameplay;
    bool last_audit_mouse_captured_ = true;
    AuditSecondAccumulator audit_second_accumulator_ {};
};

} // namespace valcraft
