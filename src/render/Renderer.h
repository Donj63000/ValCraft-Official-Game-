#pragma once

#include "app/CommandConsole.h"
#include "app/ConfirmDialog.h"
#include "app/DeathScreen.h"
#include "app/Hotbar.h"
#include "app/InventoryMenu.h"
#include "app/LoadingScreen.h"
#include "app/MainMenu.h"
#include "app/OptionsMenu.h"
#include "app/PauseMenu.h"
#include "app/SaveSlotMenu.h"
#include "creatures/CreatureGeometry.h"
#include "creatures/CreatureTypes.h"
#include "gameplay/ItemDropSystem.h"
#include "gameplay/OldGuard.h"
#include "gameplay/PlayerController.h"
#include "gameplay/PlayerMusket.h"
#include "gameplay/PlayerProgression.h"
#include "player/PlayerGeometry.h"
#include "render/ItemDropGeometry.h"
#include "render/RendererQuality.h"
#include "render/ShadowCulling.h"
#include "render/StylizedShipMesh.h"
#include "render/VisualEntityPrimitives.h"
#include "render/VisualMaterials.h"
#include "render/VisualMesh.h"
#include "render/VisualPipeline.h"
#include "world/Environment.h"
#include "world/PrecipitationField.h"
#include "world/World.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#if __has_include(<glad/gl.h>)
#include <glad/gl.h>
#else
// Je garde les etats purs du renderer testables sans imposer un contexte ni les en-tetes OpenGL.
using GLenum = unsigned int;
using GLint = int;
using GLsizei = int;
using GLsizeiptr = std::ptrdiff_t;
using GLuint = unsigned int;
#endif

namespace valcraft {

struct ShipRenderState;
struct ShipVoxel;
struct OceanState;

class RendererResourceResetProgress {
public:
    void begin(std::size_t world_mesh_count, bool overlay_pending) noexcept {
        remaining_ = world_mesh_count + (overlay_pending ? 1U : 0U);
        active_ = remaining_ > 0U;
    }

    void consume_one() noexcept {
        if (remaining_ > 0U) {
            --remaining_;
        }
        active_ = remaining_ > 0U;
    }

    void finish() noexcept {
        remaining_ = 0U;
        active_ = false;
    }

    [[nodiscard]] auto remaining() const noexcept -> std::size_t {
        return remaining_;
    }

    [[nodiscard]] auto active() const noexcept -> bool {
        return active_;
    }

    [[nodiscard]] auto complete() const noexcept -> bool {
        return !active_ && remaining_ == 0U;
    }

private:
    std::size_t remaining_ = 0U;
    bool active_ = false;
};

class RendererShipMeshCacheState {
public:
    void remember(
        std::uint64_t geometry_revision,
        std::size_t part_count,
        std::uint8_t visual_variant = 0U) noexcept {
        geometry_revision_ = geometry_revision;
        part_count_ = part_count;
        visual_variant_ = visual_variant;
    }

    void reset() noexcept {
        geometry_revision_ = 0U;
        part_count_ = 0U;
        visual_variant_ = 0U;
    }

    [[nodiscard]] auto matches(
        std::uint64_t geometry_revision,
        std::size_t part_count,
        std::uint8_t visual_variant = 0U) const noexcept -> bool {
        return geometry_revision != 0U && part_count > 0U &&
               geometry_revision_ == geometry_revision &&
               part_count_ == part_count &&
               visual_variant_ == visual_variant;
    }

    [[nodiscard]] auto ready(std::uint64_t geometry_revision,
                             std::size_t part_count,
                             bool renderer_initialized,
                             bool gpu_mesh_ready,
                             std::uint8_t visual_variant = 0U) const noexcept -> bool {
        return renderer_initialized &&
               gpu_mesh_ready &&
               matches(geometry_revision, part_count, visual_variant);
    }

private:
    // Je cle le cache sur la revision immuable du plan, pas sur l'adresse
    // accidentelle d'un span qui pourrait etre reutilisee apres un rechargement.
    std::uint64_t geometry_revision_ = 0U;
    std::size_t part_count_ = 0U;
    std::uint8_t visual_variant_ = 0U;
};

struct RendererOptions {
    bool shadows_enabled = true;
    int shadow_map_size = 1024;
    bool post_process_enabled = true;
    bool collect_detailed_stats = false;
    float viewmodel_fov_degrees = 62.0F;
    RendererQuality quality = RendererQuality::High;
    VisualPipeline visual_pipeline = VisualPipeline::LegacyVoxel;

    auto operator==(const RendererOptions&) const -> bool = default;
};

struct RendererGpuTimings {
    double shadow_ms = 0.0;
    double opaque_ms = 0.0;
    double sky_ms = 0.0;
    double entities_ms = 0.0;
    double water_ms = 0.0;
    double post_process_ms = 0.0;
    double ui_ms = 0.0;
    std::uint64_t source_frame = 0;
    std::uint32_t latency_frames = 0;
    bool valid = false;

    [[nodiscard]] auto total_ms() const noexcept -> double {
        return shadow_ms + opaque_ms + sky_ms + entities_ms + water_ms + post_process_ms + ui_ms;
    }
};

struct RendererFrameStats {
    double upload_ms = 0.0;
    double shadow_ms = 0.0;
    double world_ms = 0.0;
    std::size_t uploaded_meshes = 0;
    std::size_t visible_chunks = 0;
    std::size_t shadow_chunks = 0;
    std::size_t world_chunks = 0;
    std::size_t precipitation_drops = 0;
    std::size_t precipitation_impacts = 0;
    std::size_t draw_calls = 0;
    std::uint64_t triangles = 0;
    std::uint64_t uploaded_bytes = 0;
    std::uint64_t gpu_buffer_bytes = 0;
    std::uint64_t gpu_texture_bytes = 0;
    RendererQuality resolved_quality = RendererQuality::High;
    VisualPipeline visual_pipeline = VisualPipeline::LegacyVoxel;
    double adaptive_frame_ema_ms = 0.0;
    double adaptive_frame_p95_ms = 0.0;
    RendererGpuTimings gpu {};
};

struct GameplayHudAnnouncementView {
    std::string_view title {};
    std::string_view detail {};
    float normalized_time = 1.0F;
    bool visible = false;
};

struct MaritimeHudView {
    bool visible = false;
    bool on_ship = false;
    bool fishing_active = false;
    bool danger = false;
    bool moored = false;
    bool departing = false;
    float hunger_ratio = 1.0F;
    float thirst_ratio = 1.0F;
    float stamina_ratio = 1.0F;
    float fishing_ratio = 0.0F;
    float ship_distance = 0.0F;
    float ship_speed = 0.0F;
    float departure_seconds_remaining = 0.0F;
    std::uint32_t food_rations = 0U;
    std::uint32_t water_flasks = 0U;
    std::uint32_t fish = 0U;

    // Informations contextuelles du marin place sous le viseur.
    bool crew_focus_visible = false;
    bool crew_moving = false;
    bool crew_blocked = false;
    bool crew_knocked_out = false;
    bool crew_has_progress = false;
    std::string_view crew_role {};
    std::string_view crew_activity {};
    std::string_view crew_cargo {};
    std::string_view crew_destination {};
    float crew_progress_ratio = 0.0F;
    float crew_health_ratio = 1.0F;
    float crew_distance = 0.0F;

    auto operator==(const MaritimeHudView&) const -> bool = default;
};

struct HudVertex {
    float x = 0.0F;
    float y = 0.0F;
    float u = 0.0F;
    float v = 0.0F;
    float r = 1.0F;
    float g = 1.0F;
    float b = 1.0F;
    float a = 1.0F;
    float textured = 0.0F;
};

class Renderer {
public:
    Renderer() = default;
    ~Renderer();

    Renderer(const Renderer&) = delete;
    auto operator=(const Renderer&) -> Renderer& = delete;

    auto initialize(const RendererOptions& options = {}) -> bool;
    void shutdown();
    void render_frame(World& world,
                      const PlayerController& player,
                      const PlayerMusketView& player_musket,
                      const HotbarState& hotbar,
                      const InventoryMenuState& inventory_menu,
                      const DeathScreenState& death_screen,
                      const PauseMenuState& pause_menu,
                      const MainMenuState& main_menu,
                      const SaveSlotMenuState& save_slot_menu,
                      const OptionsMenuState& options_menu,
                      const ConfirmDialogState& confirm_dialog,
                      std::span<const CreatureRenderInstance> creatures,
                      std::span<const ItemDropRenderInstance> item_drops,
                      const ShipRenderState& ship,
                      const PlayerProgressionState& progression,
                      bool super_vision_active,
                      const GameplayHudAnnouncementView& gameplay_announcement,
                      const MaritimeHudView& maritime_hud,
                      const CommandConsoleView& command_console,
                      const EnvironmentState& environment,
                      int width,
                      int height);
    void render_frame(World& world,
                      const PlayerController& player,
                      const PlayerMusketView& player_musket,
                      const HotbarState& hotbar,
                      const InventoryMenuState& inventory_menu,
                      const DeathScreenState& death_screen,
                      const PauseMenuState& pause_menu,
                      const MainMenuState& main_menu,
                      const SaveSlotMenuState& save_slot_menu,
                      const OptionsMenuState& options_menu,
                      const ConfirmDialogState& confirm_dialog,
                      std::span<const CreatureRenderInstance> creatures,
                      std::span<const CrewRenderInstance> crew,
                      std::span<const OldGuardRenderInstance> old_guard,
                      std::span<const OldGuardMuzzleFlashInstance> old_guard_flashes,
                      std::span<const OldGuardSmokeInstance> old_guard_smoke,
                      std::span<const OldGuardMuzzleFlashInstance> player_musket_flashes,
                      std::span<const OldGuardSmokeInstance> player_musket_smoke,
                      std::span<const ItemDropRenderInstance> item_drops,
                      const ShipRenderState& ship,
                      const PlayerProgressionState& progression,
                      bool super_vision_active,
                      const GameplayHudAnnouncementView& gameplay_announcement,
                      const MaritimeHudView& maritime_hud,
                      const CommandConsoleView& command_console,
                      const EnvironmentState& environment,
                      int width,
                      int height);
    void render_loading_screen(const LoadingScreenView& view, int width, int height);
    void render_loading_screen(std::string_view title,
                               std::string_view detail,
                               float progress,
                               int width,
                               int height);
    void drain_pending_world_meshes(World& world, std::size_t max_events, double max_ms);
    void begin_world_resource_reset();
    [[nodiscard]] auto process_world_resource_reset(std::size_t max_events, double max_ms) -> bool;
    [[nodiscard]] auto pending_world_resource_reset_count() const noexcept -> std::size_t;
    void reset_world_resources();
    auto prepare_ship_mesh(const ShipRenderState& ship) -> bool;
    auto upload_prepared_ship_mesh(const ShipRenderState& ship, const ChunkMeshData& mesh) -> bool;
    [[nodiscard]] auto ship_mesh_ready(const ShipRenderState& ship) const noexcept -> bool;
    [[nodiscard]] auto world_mesh_uploaded(const ChunkCoord& coord, std::uint64_t revision) const noexcept -> bool;
    void submit_cpu_frame_time_sample(double active_frame_time_ms) noexcept;
    [[nodiscard]] auto last_frame_stats() const noexcept -> const RendererFrameStats&;
    [[nodiscard]] auto material_pack_version() const noexcept -> std::uint16_t;
    [[nodiscard]] auto material_pack_checksum() const noexcept -> std::uint64_t;
    [[nodiscard]] auto last_initialization_error() const noexcept
        -> std::string_view;

private:
    enum class GpuTimedPass : std::size_t {
        Shadow = 0,
        Opaque,
        Sky,
        Entities,
        Water,
        PostProcess,
        Ui,
        Count,
    };

    static constexpr auto kGpuTimedPassCount = static_cast<std::size_t>(GpuTimedPass::Count);
    static constexpr std::size_t kGpuQueryFrameCount = 4U;

    struct GpuQueryFrame {
        std::array<GLuint, kGpuTimedPassCount> queries {};
        std::array<bool, kGpuTimedPassCount> issued {};
        std::uint64_t frame_index = 0;
        bool pending = false;
    };

    struct GpuMesh {
        GLuint vao = 0;
        GLuint vbo = 0;
        GLuint ebo = 0;
        GLsizei opaque_index_count = 0;
        GLuint water_vao = 0;
        GLuint water_vbo = 0;
        GLuint water_ebo = 0;
        GLsizei water_index_count = 0;
        std::uint64_t revision = 0;
        GLsizeiptr vertex_buffer_bytes = 0;
        GLsizeiptr index_buffer_bytes = 0;
        GLsizeiptr water_vertex_buffer_bytes = 0;
        GLsizeiptr water_index_buffer_bytes = 0;
        GLuint terrain_vao = 0;
        GLuint terrain_vbo = 0;
        GLuint terrain_ebo = 0;
        GLsizei terrain_index_count = 0;
        GLsizeiptr terrain_vertex_buffer_bytes = 0;
        GLsizeiptr terrain_index_buffer_bytes = 0;
        GLuint architecture_vao = 0;
        GLuint architecture_vbo = 0;
        GLuint architecture_ebo = 0;
        GLsizei architecture_opaque_index_count = 0;
        GLsizei architecture_transparent_index_count = 0;
        GLsizeiptr architecture_transparent_index_offset_bytes = 0;
        GLsizeiptr architecture_vertex_buffer_bytes = 0;
        GLsizeiptr architecture_index_buffer_bytes = 0;
        ChunkBounds bounds {};
    };

    struct EntityPrimitiveDrawRange {
        std::size_t first_index = 0U;
        GLsizei index_count = 0;
        StylizedPrimitiveType primitive =
            StylizedPrimitiveType::RoundedBox;
        StylizedPrimitiveLod lod = StylizedPrimitiveLod::Medium;
    };

    struct ModernTerrainUniformLocations {
        GLint model = -1;
        GLint view_projection = -1;
        GLint light_view_projection = -1;
        GLint light_view_projection_far = -1;
        GLint camera_position = -1;
        GLint camera_forward = -1;
        GLint sun_direction = -1;
        GLint sun_color = -1;
        GLint ambient_color = -1;
        GLint fog_color = -1;
        GLint distant_fog_color = -1;
        GLint night_tint_color = -1;
        GLint daylight_factor = -1;
        GLint sun_visibility = -1;
        GLint precipitation_intensity = -1;
        GLint storm_intensity = -1;
        GLint lightning_intensity = -1;
        GLint triplanar_sharpness = -1;
        GLint material_detail_scale = -1;
        GLint shadows_enabled = -1;
        GLint material_albedo = -1;
        GLint material_normal_height = -1;
        GLint material_orm_emission = -1;
        GLint shadow_map = -1;
        GLint shadow_map_far = -1;
        GLint shadow_cascade_count = -1;
        GLint shadow_split_distance = -1;
        GLint shadow_transition_width = -1;
    };

    struct ModernTerrainShadowUniformLocations {
        GLint model = -1;
        GLint light_view_projection = -1;
        GLint material_albedo = -1;
    };

    struct WorldUniformLocations {
        GLint model = -1;
        GLint view_projection = -1;
        GLint light_view_projection = -1;
        GLint light_view_projection_far = -1;
        GLint camera_position = -1;
        GLint camera_forward = -1;
        GLint sun_direction = -1;
        GLint sun_color = -1;
        GLint ambient_color = -1;
        GLint fog_color = -1;
        GLint distant_fog_color = -1;
        GLint horizon_glow_color = -1;
        GLint night_tint_color = -1;
        GLint daylight_factor = -1;
        GLint sun_visibility = -1;
        GLint time_of_day = -1;
        GLint cloud_intensity = -1;
        GLint cloud_shadow_strength = -1;
        GLint wind_strength = -1;
        GLint atmospheric_scatter_strength = -1;
        GLint height_fog_density = -1;
        GLint precipitation_intensity = -1;
        GLint storm_intensity = -1;
        GLint lightning_intensity = -1;
        GLint ocean_waves = -1;
        GLint ocean_wave_phases = -1;
        GLint ocean_wave_count = -1;
        GLint ocean_foam_threshold = -1;
        GLint ocean_detail_strength = -1;
        GLint ocean_detail_phase = -1;
        GLint ocean_severity = -1;
        GLint ocean_tempest_factor = -1;
        GLint ocean_open_sea = -1;
        GLint atlas = -1;
        GLint shadow_map = -1;
        GLint shadow_map_far = -1;
        GLint shadow_cascade_count = -1;
        GLint shadow_split_distance = -1;
        GLint shadow_transition_width = -1;
        GLint scene_color = -1;
        GLint scene_depth = -1;
        GLint inverse_view_projection = -1;
        GLint shadows_enabled = -1;
        GLint super_vision_strength = -1;
        GLint ship_protection_enabled = -1;
        GLint ship_inverse_model = -1;
        GLint ship_bounds_min = -1;
        GLint ship_bounds_max = -1;
        GLint ship_profile_longitudinal = -1;
        GLint ship_profile_taper = -1;
        GLint ship_profile_heights = -1;
        GLint ship_profile_widths = -1;
        GLint ship_sheltered_floor = -1;
    };

    struct ShadowUniformLocations {
        GLint model = -1;
        GLint light_view_projection = -1;
        GLint time_of_day = -1;
        GLint wind_strength = -1;
        GLint atlas = -1;
    };

    struct HudUniformLocations {
        GLint atlas = -1;
        GLint font_atlas = -1;
        GLint model_icon_atlas = -1;
    };

    struct SkyUniformLocations {
        GLint inverse_view_projection = -1;
        GLint sun_direction = -1;
        GLint daylight_factor = -1;
        GLint time_of_day = -1;
        GLint sky_zenith_color = -1;
        GLint sky_horizon_color = -1;
        GLint horizon_glow_color = -1;
        GLint sun_disk_color = -1;
        GLint moon_disk_color = -1;
        GLint star_intensity = -1;
        GLint cloud_intensity = -1;
        GLint overcast_intensity = -1;
        GLint precipitation_intensity = -1;
        GLint storm_intensity = -1;
        GLint violent_storm_intensity = -1;
        GLint lightning_intensity = -1;
        GLint lightning_bolt_intensity = -1;
        GLint lightning_direction = -1;
        GLint lightning_shape_seed = -1;
        GLint weather_time = -1;
        GLint cloud_steps = -1;
        GLint cloud_detail = -1;
        GLint accent_atlas = -1;
    };

    struct PostProcessUniformLocations {
        GLint scene_texture = -1;
        GLint glow_texture = -1;
        GLint scene_depth = -1;
        GLint exposure = -1;
        GLint saturation_boost = -1;
        GLint contrast = -1;
        GLint vignette_strength = -1;
        GLint night_tint_color = -1;
        GLint glow_strength = -1;
        GLint sharpen_strength = -1;
        GLint edge_strength = -1;
        GLint fxaa_enabled = -1;
        GLint modern_pipeline = -1;
        GLint resolve_only = -1;
        GLint storm_intensity = -1;
        GLint lightning_intensity = -1;
        GLint weather_exposure = -1;
    };

    struct PrecipitationUniformLocations {
        GLint view_projection = -1;
        GLint camera_position = -1;
        GLint camera_right = -1;
        GLint camera_up = -1;
        GLint fog_color = -1;
        GLint lightning_intensity = -1;
        GLint storm_intensity = -1;
        GLint ship_protection_enabled = -1;
        GLint ship_inverse_model = -1;
        GLint ship_bounds_min = -1;
        GLint ship_bounds_max = -1;
        GLint ship_profile_longitudinal = -1;
        GLint ship_profile_taper = -1;
        GLint ship_profile_heights = -1;
        GLint ship_profile_widths = -1;
        GLint ship_sheltered_floor = -1;
    };

    struct PrecipitationGpuInstance {
        glm::vec4 position_length {0.0F};
        glm::vec4 velocity_width {0.0F};
        glm::vec4 appearance {0.0F};
    };

    struct OldGuardEffectUniformLocations {
        GLint view_projection = -1;
        GLint camera_right = -1;
        GLint camera_up = -1;
    };

    struct OldGuardEffectGpuInstance {
        glm::vec4 position_size {0.0F};
        // x = opacite, y = type (fumee/flash), z = rotation, w = intensite.
        glm::vec4 appearance {0.0F};
    };

    struct GlowExtractUniformLocations {
        GLint scene_texture = -1;
        GLint threshold = -1;
    };

    struct GlowBlurUniformLocations {
        GLint source_texture = -1;
        GLint texel_direction = -1;
    };

    struct MenuBackgroundUniformLocations {
        GLint scene_texture = -1;
        GLint blur_texture = -1;
        GLint blur_mix = -1;
        GLint tint_color = -1;
        GLint vignette_strength = -1;
        GLint exposure = -1;
        GLint modern_pipeline = -1;
    };

    struct CreatureUniformLocations {
        GLint view_projection = -1;
        GLint light_view_projection = -1;
        GLint light_view_projection_far = -1;
        GLint camera_position = -1;
        GLint camera_forward = -1;
        GLint sun_direction = -1;
        GLint sun_color = -1;
        GLint ambient_color = -1;
        GLint fog_color = -1;
        GLint distant_fog_color = -1;
        GLint horizon_glow_color = -1;
        GLint night_tint_color = -1;
        GLint daylight_factor = -1;
        GLint sun_visibility = -1;
        GLint cloud_intensity = -1;
        GLint cloud_shadow_strength = -1;
        GLint atmospheric_scatter_strength = -1;
        GLint height_fog_density = -1;
        GLint precipitation_intensity = -1;
        GLint storm_intensity = -1;
        GLint lightning_intensity = -1;
        GLint atlas = -1;
        GLint shadow_map = -1;
        GLint shadow_map_far = -1;
        GLint shadow_cascade_count = -1;
        GLint shadow_split_distance = -1;
        GLint shadow_transition_width = -1;
        GLint shadows_enabled = -1;
        GLint time_of_day = -1;
        GLint player_light_strength = -1;
        GLint super_vision_strength = -1;
        GLint modern_pipeline = -1;
    };

    struct VisibleChunk {
        ChunkCoord coord {};
        const GpuMesh* mesh = nullptr;
        glm::vec3 center {0.0F};
        float distance_squared = 0.0F;
    };

    struct ShadowChunk {
        const GpuMesh* mesh = nullptr;
    };

    struct VisibleCreature {
        const CreatureRenderInstance* creature = nullptr;
        float distance_squared = 0.0F;
    };

    struct VisibleCrewMember {
        const CrewRenderInstance* crew = nullptr;
        float distance_squared = 0.0F;
    };

    struct VisibleOldGuardMember {
        const OldGuardRenderInstance* guard = nullptr;
        float distance_squared = 0.0F;
    };

    struct HotbarHudCacheKey {
        HotbarState hotbar {};
        int width = 0;
        int height = 0;
        int health_steps = 0;
        int air_steps = 0;
        int damage_flash_step = 0;
        std::uint32_t player_level = 1U;
        int level_progress_step = 0;
        bool air_visible = false;
        bool underwater = false;

        auto operator==(const HotbarHudCacheKey&) const -> bool = default;
    };

    struct InventoryHudCacheKey {
        InventoryMenuState inventory_menu {};
        HotbarState hotbar {};
        int width = 0;
        int height = 0;

        auto operator==(const InventoryHudCacheKey&) const -> bool = default;
    };

    struct DeathHudCacheKey {
        DeathScreenState death_screen {};
        int width = 0;
        int height = 0;

        auto operator==(const DeathHudCacheKey&) const -> bool = default;
    };

    struct PauseHudCacheKey {
        PauseMenuState pause_menu {};
        int width = 0;
        int height = 0;

        auto operator==(const PauseHudCacheKey&) const -> bool = default;
    };

    struct MainMenuHudCacheKey {
        MainMenuState main_menu {};
        int width = 0;
        int height = 0;

        auto operator==(const MainMenuHudCacheKey&) const -> bool = default;
    };

    struct SaveSlotHudCacheKey {
        SaveSlotMenuState save_slot_menu {};
        int width = 0;
        int height = 0;

        auto operator==(const SaveSlotHudCacheKey&) const -> bool = default;
    };

    struct OptionsHudCacheKey {
        OptionsMenuState options_menu {};
        int width = 0;
        int height = 0;

        auto operator==(const OptionsHudCacheKey&) const -> bool = default;
    };

    struct ConfirmHudCacheKey {
        ConfirmDialogState confirm_dialog {};
        int width = 0;
        int height = 0;

        auto operator==(const ConfirmHudCacheKey&) const -> bool = default;
    };

    struct MaritimeHudCacheKey {
        int width = 0;
        int height = 0;
        bool visible = false;
        bool on_ship = false;
        bool fishing_active = false;
        bool danger = false;
        bool moored = false;
        bool departing = false;
        int hunger_step = 0;
        int thirst_step = 0;
        int stamina_step = 0;
        int fishing_step = 0;
        int ship_distance_step = 0;
        int ship_speed_step = 0;
        int departure_seconds_step = 0;
        std::uint32_t food_rations = 0U;
        std::uint32_t water_flasks = 0U;
        std::uint32_t fish = 0U;
        bool crew_focus_visible = false;
        bool crew_moving = false;
        bool crew_blocked = false;
        bool crew_knocked_out = false;
        bool crew_has_progress = false;
        std::string_view crew_role {};
        std::string_view crew_activity {};
        std::string_view crew_cargo {};
        std::string_view crew_destination {};
        int crew_progress_step = 0;
        int crew_health_step = 0;
        int crew_distance_step = 0;

        auto operator==(const MaritimeHudCacheKey&) const -> bool = default;
    };

    template <typename Key>
    struct HudGeometryCache {
        bool valid = false;
        Key key {};
        std::vector<HudVertex> vertices {};
    };

    void sync_gpu_meshes(World& world, RendererFrameStats& frame_stats, std::size_t max_events, double max_ms);
    void upload_mesh(const ChunkCoord& coord,
                     const ChunkMeshData& mesh,
                     const OrganicTerrainMesh* terrain_mesh,
                     const ArchitecturalMesh* architectural_mesh,
                     std::uint64_t revision);
    void upload_mesh_data(GpuMesh& gpu_mesh, const ChunkMeshData& mesh, std::uint64_t revision, const ChunkBounds& bounds);
    void upload_terrain_mesh_data(GpuMesh& gpu_mesh, const OrganicTerrainMesh& mesh);
    void upload_architectural_mesh_data(
        GpuMesh& gpu_mesh,
        const ArchitecturalMesh& mesh);
    void ensure_ship_mesh(
        const ShipRenderState& ship,
        StylizedShipLod lod = StylizedShipLod::Near);
    [[nodiscard]] auto ship_mesh_ready(
        const ShipRenderState& ship,
        StylizedShipLod lod) const noexcept -> bool;
    void destroy_gpu_mesh(GpuMesh& mesh);
    auto compile_shader(GLenum type, const char* source) -> GLuint;
    auto link_program(GLuint vertex_shader, GLuint fragment_shader) -> GLuint;
    void create_programs();
    void create_atlas_texture();
    auto create_msdf_font_texture() -> bool;
    void destroy_msdf_font_texture();
    auto create_model_icon_texture() -> bool;
    void destroy_model_icon_texture();
    void bind_hud_textures();
    [[nodiscard]] auto hud_item_texture_mode(
        BlockId block_id) const noexcept -> float;
    auto create_modern_material_textures() -> bool;
    void destroy_modern_material_textures();
    void create_accent_texture();
    void create_creature_atlas_texture();
    void create_player_atlas_texture();
    void create_shadow_map();
    void destroy_shadow_map();
    void create_scene_sampler_fallback_textures();
    void create_creature_geometry();
    void create_item_drop_geometry();
    void create_precipitation_geometry();
    void create_old_guard_effect_geometry();
    void create_screen_quad_geometry();
    void create_crosshair_geometry();
    void upload_block_break_overlay_mesh(
        const World& world,
        const BlockBreakProgress& break_progress);
    void create_hud_geometry();
    void ensure_post_process_targets(int width, int height, bool require_glow_targets);
    void destroy_post_process_targets();
    void destroy_scene_targets();
    void destroy_glow_targets();
    void ensure_water_scene_targets(int width, int height);
    void destroy_water_scene_targets();
    void draw_sky(const glm::mat4& inverse_view_projection,
                  const EnvironmentState& environment,
                  const RendererQualitySettings& quality_settings);
    void run_post_process(const EnvironmentState& environment,
                          float weather_exposure,
                          int width,
                          int height,
                          bool optional_effects_enabled);
    void run_menu_background_pass(
        int width,
        int height,
        float exposure);
    void draw_item_drops(std::span<const ItemDropRenderInstance> item_drops,
                         const glm::mat4& view_projection,
                         const glm::mat4& light_view_projection,
                         const glm::mat4& light_view_projection_far,
                         int shadow_cascade_count,
                         float shadow_split_distance,
                         float shadow_transition_width,
                         const glm::mat4& inverse_view_projection,
                         const glm::vec3& camera_position,
                         const glm::vec3& camera_forward,
                         const EnvironmentState& environment,
                         bool sun_visible);
    void draw_creatures(std::span<const CreatureRenderInstance> creatures,
                        std::span<const CrewRenderInstance> crew,
                        std::span<const OldGuardRenderInstance> old_guard,
                        const glm::mat4& view_projection,
                        const glm::mat4& light_view_projection,
                        const glm::mat4& light_view_projection_far,
                        int shadow_cascade_count,
                        float shadow_split_distance,
                        float shadow_transition_width,
                        const glm::vec3& camera_position,
                        const glm::vec3& camera_forward,
                        const EnvironmentState& environment,
                        bool player_light_active,
                        float super_vision_strength);
    void draw_precipitation(const glm::mat4& view_projection,
                            const glm::mat4& inverse_view,
                            const glm::vec3& camera_position,
                            const EnvironmentState& environment,
                            const OceanState& ocean,
                            const ShipRenderState& ship,
                            const RendererQualitySettings& quality_settings,
                            RendererFrameStats& frame_stats);
    void draw_old_guard_effects(
        std::span<const OldGuardMuzzleFlashInstance> flashes,
        std::span<const OldGuardSmokeInstance> smoke,
        const glm::mat4& view_projection,
        const glm::mat4& inverse_view,
        const glm::vec3& camera_position,
        bool viewmodel_overlay = false,
        const PlayerViewModelPose* viewmodel_pose = nullptr);
    void upload_world_ship_protection(const ShipRenderState& ship);
    void upload_precipitation_ship_protection(const ShipRenderState& ship);
    void draw_creature_shadows(std::span<const CreatureRenderInstance> creatures,
                               std::span<const CrewRenderInstance> crew,
                               std::span<const OldGuardRenderInstance> old_guard,
                               const glm::mat4& light_view_projection,
                               const glm::vec3& shadow_focus);
    void prepare_visual_entity_batches(
        std::span<const CreaturePartInstance> parts,
        VisualEntityContext context,
        const glm::vec3& focus,
        bool simplified_shadow,
        bool viewmodel);
    void draw_visual_entity_batches(
        GLuint instance_vbo,
        GLsizeiptr& instance_buffer_bytes);
    [[nodiscard]] auto collect_visible_creature_parts(std::span<const CreatureRenderInstance> creatures,
                                                      std::span<const CrewRenderInstance> crew,
                                                      std::span<const OldGuardRenderInstance> old_guard,
                                                      const glm::vec3& focus,
                                                      float creature_draw_distance,
                                                      float crew_draw_distance,
                                                      float old_guard_draw_distance)
        -> std::span<const CreaturePartInstance>;
    [[nodiscard]] auto draw_player_viewmodel(
        const PlayerController& player,
        BlockId held_item,
        const PlayerMusketView& player_musket,
        const glm::mat4& view_projection,
        const glm::mat4& light_view_projection,
        const glm::vec3& camera_position,
        const EnvironmentState& environment)
        -> PlayerViewModelPose;
    void draw_block_break_overlay(
        const World& world,
        const PlayerController& player);
    void draw_hotbar(const PlayerController& player,
                     const HotbarState& hotbar,
                     const PlayerProgressionState& progression,
                     const EnvironmentState& environment,
                     int width,
                     int height);
    void draw_maritime_hud(const MaritimeHudView& maritime_hud, int width, int height);
    void draw_gameplay_announcement(const GameplayHudAnnouncementView& announcement, int width, int height);
    void draw_command_console(const CommandConsoleView& command_console, int width, int height);
    void draw_inventory_menu(const InventoryMenuState& inventory_menu, const HotbarState& hotbar, int width, int height);
    void draw_death_screen(const DeathScreenState& death_screen, int width, int height);
    void draw_pause_menu(const PauseMenuState& pause_menu, int width, int height);
    void draw_main_menu(const MainMenuState& main_menu, int width, int height);
    void draw_save_slot_menu(const SaveSlotMenuState& save_slot_menu, int width, int height);
    void draw_options_menu(const OptionsMenuState& options_menu, int width, int height);
    void draw_confirm_dialog(const ConfirmDialogState& confirm_dialog, int width, int height);
    void draw_crosshair();
    void draw_musket_hud(
        const PlayerMusketView& musket,
        int width,
        int height);
    void ensure_hud_buffer_capacity(std::size_t vertex_count);
    void upload_hud_vertices(std::span<const HudVertex> vertices);
    void create_gpu_timers();
    void destroy_gpu_timers();
    void begin_gpu_frame(RendererFrameStats& frame_stats);
    void end_gpu_frame();
    void begin_gpu_pass(GpuTimedPass pass);
    void end_gpu_pass(GpuTimedPass pass);
    void record_triangle_draw(GLsizei index_or_vertex_count, GLsizei instance_count = 1) noexcept;
    void record_draw_call() noexcept;
    [[nodiscard]] auto estimate_gpu_buffer_bytes() const noexcept -> std::uint64_t;
    [[nodiscard]] auto estimate_gpu_texture_bytes() const noexcept -> std::uint64_t;

    GLuint world_program_ = 0;
    GLuint modern_terrain_program_ = 0;
    GLuint modern_architecture_program_ = 0;
    GLuint modern_terrain_shadow_program_ = 0;
    GLuint creature_program_ = 0;
    GLuint creature_shadow_program_ = 0;
    GLuint item_drop_program_ = 0;
    GLuint precipitation_program_ = 0;
    GLuint old_guard_effect_program_ = 0;
    GLuint shadow_program_ = 0;
    GLuint hud_program_ = 0;
    GLuint crosshair_program_ = 0;
    GLuint sky_program_ = 0;
    GLuint post_process_program_ = 0;
    GLuint glow_extract_program_ = 0;
    GLuint glow_blur_program_ = 0;
    GLuint menu_background_program_ = 0;
    GLuint atlas_texture_ = 0;
    GLuint msdf_font_texture_ = 0;
    GLuint model_icon_texture_ = 0;
    GLuint modern_material_albedo_texture_ = 0;
    GLuint modern_material_normal_height_texture_ = 0;
    GLuint modern_material_orm_emission_texture_ = 0;
    GLuint accent_texture_ = 0;
    GLuint creature_atlas_texture_ = 0;
    GLuint player_atlas_texture_ = 0;
    GLuint shadow_map_ = 0;
    GLuint shadow_framebuffer_ = 0;
    GLuint shadow_map_far_ = 0;
    GLuint shadow_framebuffer_far_ = 0;
    GLuint scene_fallback_color_texture_ = 0;
    GLuint scene_fallback_depth_texture_ = 0;
    GLuint water_scene_framebuffer_ = 0;
    GLuint water_scene_color_texture_ = 0;
    GLuint water_scene_depth_texture_ = 0;
    GLuint scene_framebuffer_ = 0;
    GLuint scene_color_texture_ = 0;
    GLuint scene_depth_texture_ = 0;
    GLuint glow_extract_framebuffer_ = 0;
    GLuint glow_extract_texture_ = 0;
    GLuint glow_ping_framebuffer_ = 0;
    GLuint glow_ping_texture_ = 0;
    GLuint screen_quad_vao_ = 0;
    GLuint creature_vao_ = 0;
    GLuint creature_vbo_ = 0;
    GLuint creature_ebo_ = 0;
    GLuint creature_instance_vbo_ = 0;
    GLuint viewmodel_vao_ = 0;
    GLuint viewmodel_instance_vbo_ = 0;
    GpuMesh block_break_overlay_mesh_ {};
    GLuint item_drop_vao_ = 0;
    GLuint item_drop_vbo_ = 0;
    GLuint item_drop_ebo_ = 0;
    GLuint item_drop_instance_vbo_ = 0;
    GLuint precipitation_vao_ = 0;
    GLuint precipitation_vbo_ = 0;
    GLuint precipitation_instance_vbo_ = 0;
    GLuint old_guard_effect_vao_ = 0;
    GLuint old_guard_effect_vbo_ = 0;
    GLuint old_guard_effect_instance_vbo_ = 0;
    GLuint hud_vao_ = 0;
    GLuint hud_vbo_ = 0;
    GLuint crosshair_vao_ = 0;
    GLuint crosshair_vbo_ = 0;
    GpuMesh ship_gpu_mesh_ {};
    RendererOptions options_ {};
    RendererQualitySettings active_quality_settings_ {};
    RendererAdaptiveQualityController adaptive_quality_controller_ {};
    WorldUniformLocations world_uniforms_ {};
    ModernTerrainUniformLocations modern_terrain_uniforms_ {};
    ModernTerrainUniformLocations modern_architecture_uniforms_ {};
    ModernTerrainShadowUniformLocations modern_terrain_shadow_uniforms_ {};
    CreatureUniformLocations creature_uniforms_ {};
    GLint creature_shadow_light_view_projection_ = -1;
    WorldUniformLocations item_drop_uniforms_ {};
    ShadowUniformLocations shadow_uniforms_ {};
    HudUniformLocations hud_uniforms_ {};
    SkyUniformLocations sky_uniforms_ {};
    PostProcessUniformLocations post_process_uniforms_ {};
    PrecipitationUniformLocations precipitation_uniforms_ {};
    OldGuardEffectUniformLocations old_guard_effect_uniforms_ {};
    GlowExtractUniformLocations glow_extract_uniforms_ {};
    GlowBlurUniformLocations glow_blur_uniforms_ {};
    MenuBackgroundUniformLocations menu_background_uniforms_ {};
    std::unordered_map<ChunkCoord, GpuMesh, ChunkCoordHash> gpu_meshes_ {};
    std::vector<GpuMesh> world_resource_reset_queue_ {};
    std::vector<VisibleChunk> visible_chunks_cache_ {};
    std::vector<ShadowChunk> shadow_chunks_cache_ {};
    std::vector<VisibleCreature> visible_creatures_cache_ {};
    std::vector<VisibleCrewMember> visible_crew_cache_ {};
    std::vector<VisibleOldGuardMember> visible_old_guard_cache_ {};
    GLsizeiptr creature_instance_buffer_bytes_ = 0;
    GLsizeiptr viewmodel_instance_buffer_bytes_ = 0;
    GLsizeiptr item_drop_instance_buffer_bytes_ = 0;
    GLsizeiptr creature_template_vertex_buffer_bytes_ = 0;
    GLsizeiptr creature_template_index_buffer_bytes_ = 0;
    GLsizeiptr item_drop_template_vertex_buffer_bytes_ = 0;
    GLsizeiptr item_drop_template_index_buffer_bytes_ = 0;
    GLsizei creature_template_index_count_ = 0;
    GLsizei item_drop_template_index_count_ = 0;
    GLsizeiptr precipitation_instance_buffer_bytes_ = 0;
    GLsizeiptr old_guard_effect_instance_buffer_bytes_ = 0;
    GLsizeiptr hud_vertex_buffer_bytes_ = 0;
    RendererFrameStats last_frame_stats_ {};
    std::vector<ItemDropGpuInstance> item_drop_instances_scratch_ {};
    PrecipitationField precipitation_field_ {};
    std::vector<PrecipitationGpuInstance> precipitation_instances_scratch_ {};
    std::vector<OldGuardEffectGpuInstance> old_guard_effect_instances_scratch_ {};
    std::vector<CreaturePartInstance> creature_parts_scratch_ {};
    std::array<
        std::vector<CreaturePartInstance>,
        kVisualEntityPrimitiveTypeCount * kVisualEntityPrimitiveLodCount>
        visual_entity_batches_ {};
    std::array<
        EntityPrimitiveDrawRange,
        kVisualEntityPrimitiveTypeCount * kVisualEntityPrimitiveLodCount>
        visual_entity_draw_ranges_ {};
    ChunkMeshData chunk_upload_scratch_ {};
    OrganicTerrainMesh terrain_upload_scratch_ {};
    ArchitecturalMesh architecture_upload_scratch_ {};
    std::vector<std::uint32_t> architecture_indices_scratch_ {};
    ChunkMeshData block_break_overlay_scratch_ {};
    std::vector<HudVertex> loading_vertices_scratch_ {};
    std::vector<HudVertex> gameplay_announcement_vertices_scratch_ {};
    std::vector<HudVertex> command_console_vertices_scratch_ {};
    HudGeometryCache<HotbarHudCacheKey> hotbar_cache_ {};
    HudGeometryCache<InventoryHudCacheKey> inventory_cache_ {};
    HudGeometryCache<DeathHudCacheKey> death_cache_ {};
    HudGeometryCache<PauseHudCacheKey> pause_cache_ {};
    HudGeometryCache<MainMenuHudCacheKey> main_menu_cache_ {};
    HudGeometryCache<SaveSlotHudCacheKey> save_slot_cache_ {};
    HudGeometryCache<OptionsHudCacheKey> options_cache_ {};
    HudGeometryCache<ConfirmHudCacheKey> confirm_cache_ {};
    HudGeometryCache<MaritimeHudCacheKey> maritime_cache_ {};
    int water_scene_target_width_ = 0;
    int water_scene_target_height_ = 0;
    int scene_target_width_ = 0;
    int scene_target_height_ = 0;
    int glow_target_width_ = 0;
    int glow_target_height_ = 0;
    GLint water_scene_color_internal_format_ = 0;
    GLint scene_color_internal_format_ = 0;
    GLint glow_color_internal_format_ = 0;
    std::array<GpuQueryFrame, kGpuQueryFrameCount> gpu_query_frames_ {};
    RendererGpuTimings last_gpu_timings_ {};
    std::uint64_t gpu_frame_index_ = 0;
    std::uint64_t adaptive_last_gpu_source_frame_ = 0;
    double pending_cpu_frame_time_ms_ = 0.0;
    std::uint64_t material_pack_checksum_ = 0;
    std::uint16_t material_pack_version_ = 0;
    std::uint16_t material_pack_width_ = 0;
    std::uint16_t material_pack_height_ = 0;
    std::uint16_t material_pack_layers_ = 0;
    std::uint16_t material_pack_mips_ = 0;
    std::uint32_t msdf_font_width_ = 0;
    std::uint32_t msdf_font_height_ = 0;
    std::uint32_t msdf_font_mips_ = 0;
    std::uint16_t model_icon_width_ = 0;
    std::uint16_t model_icon_height_ = 0;
    std::uint16_t model_icon_layers_ = 0;
    std::uint16_t model_icon_mips_ = 0;
    std::array<std::uint16_t, 256> model_icon_layer_by_block_ {};
    std::string last_initialization_error_ {};
    std::size_t frame_draw_calls_ = 0;
    std::uint64_t frame_triangles_ = 0;
    std::uint64_t frame_uploaded_bytes_ = 0;
    RendererResourceResetProgress world_resource_reset_progress_ {};
    RendererShipMeshCacheState ship_mesh_cache_ {};
    StylizedShipLod active_ship_lod_ = StylizedShipLod::Near;
    int active_gpu_query_frame_ = -1;
    int active_gpu_pass_ = -1;
    bool adaptive_gpu_sample_consumed_ = false;
    bool pending_cpu_frame_time_valid_ = false;
    bool gpu_timers_supported_ = false;
    bool gl_api_ready_ = false;
    bool initialized_ = false;
};

} // namespace valcraft
