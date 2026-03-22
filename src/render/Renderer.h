#pragma once

#include "app/DeathScreen.h"
#include "app/Hotbar.h"
#include "app/InventoryMenu.h"
#include "app/PauseMenu.h"
#include "creatures/CreatureTypes.h"
#include "gameplay/ItemDropSystem.h"
#include "gameplay/PlayerController.h"
#include "world/Environment.h"
#include "world/World.h"

#include <glad/gl.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

namespace valcraft {

struct RendererOptions {
    bool shadows_enabled = true;
    int shadow_map_size = 2048;
    bool post_process_enabled = true;
};

struct RendererFrameStats {
    double upload_ms = 0.0;
    double shadow_ms = 0.0;
    double world_ms = 0.0;
    std::size_t uploaded_meshes = 0;
    std::size_t visible_chunks = 0;
    std::size_t shadow_chunks = 0;
    std::size_t world_chunks = 0;
};

class Renderer {
public:
    Renderer() = default;
    ~Renderer();

    Renderer(const Renderer&) = delete;
    auto operator=(const Renderer&) -> Renderer& = delete;

    auto initialize(const RendererOptions& options = {}) -> bool;
    void shutdown();
    void render_frame(const World& world,
                      const PlayerController& player,
                      const HotbarState& hotbar,
                      const InventoryMenuState& inventory_menu,
                      const DeathScreenState& death_screen,
                      const PauseMenuState& pause_menu,
                      std::span<const CreatureRenderInstance> creatures,
                      std::span<const ItemDropRenderInstance> item_drops,
                      const EnvironmentState& environment,
                      int width,
                      int height);
    [[nodiscard]] auto last_frame_stats() const noexcept -> const RendererFrameStats&;

private:
    struct GpuMesh {
        GLuint vao = 0;
        GLuint vbo = 0;
        GLuint ebo = 0;
        GLsizei opaque_index_count = 0;
        GLsizei water_index_count = 0;
        GLsizeiptr water_index_offset_bytes = 0;
        std::uint64_t revision = 0;
        GLsizeiptr vertex_buffer_bytes = 0;
        GLsizeiptr index_buffer_bytes = 0;
    };

    struct WorldUniformLocations {
        GLint view_projection = -1;
        GLint light_view_projection = -1;
        GLint camera_position = -1;
        GLint sun_direction = -1;
        GLint sun_color = -1;
        GLint ambient_color = -1;
        GLint fog_color = -1;
        GLint distant_fog_color = -1;
        GLint night_tint_color = -1;
        GLint daylight_factor = -1;
        GLint sun_visibility = -1;
        GLint time_of_day = -1;
        GLint atlas = -1;
        GLint shadow_map = -1;
        GLint shadows_enabled = -1;
    };

    struct ShadowUniformLocations {
        GLint light_view_projection = -1;
        GLint atlas = -1;
    };

    struct HudUniformLocations {
        GLint atlas = -1;
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
        GLint accent_atlas = -1;
    };

    struct PostProcessUniformLocations {
        GLint scene_texture = -1;
        GLint glow_texture = -1;
        GLint exposure = -1;
        GLint saturation_boost = -1;
        GLint contrast = -1;
        GLint vignette_strength = -1;
        GLint night_tint_color = -1;
        GLint glow_strength = -1;
    };

    struct GlowExtractUniformLocations {
        GLint scene_texture = -1;
        GLint threshold = -1;
    };

    struct GlowBlurUniformLocations {
        GLint source_texture = -1;
        GLint texel_direction = -1;
    };

    struct CreatureUniformLocations {
        GLint view_projection = -1;
        GLint light_view_projection = -1;
        GLint camera_position = -1;
        GLint sun_direction = -1;
        GLint sun_color = -1;
        GLint ambient_color = -1;
        GLint fog_color = -1;
        GLint distant_fog_color = -1;
        GLint night_tint_color = -1;
        GLint daylight_factor = -1;
        GLint sun_visibility = -1;
        GLint atlas = -1;
        GLint shadow_map = -1;
        GLint shadows_enabled = -1;
        GLint time_of_day = -1;
    };

    struct VisibleChunk {
        ChunkCoord coord {};
        const GpuMesh* mesh = nullptr;
        glm::vec3 center {0.0F};
        float distance = 0.0F;
    };

    void sync_gpu_meshes(const World& world, RendererFrameStats& frame_stats);
    void upload_mesh(const ChunkCoord& coord, const ChunkMeshData& mesh, std::uint64_t revision);
    void destroy_gpu_mesh(GpuMesh& mesh);
    auto compile_shader(GLenum type, const char* source) -> GLuint;
    auto link_program(GLuint vertex_shader, GLuint fragment_shader) -> GLuint;
    void create_programs();
    void create_atlas_texture();
    void create_accent_texture();
    void create_creature_atlas_texture();
    void create_shadow_map();
    void create_creature_geometry();
    void create_item_drop_geometry();
    void create_screen_quad_geometry();
    void create_crosshair_geometry();
    void create_hud_geometry();
    void ensure_post_process_targets(int width, int height);
    void destroy_post_process_targets();
    void draw_sky(const PlayerController& player, const EnvironmentState& environment);
    void run_post_process(const EnvironmentState& environment, int width, int height);
    void draw_item_drops(std::span<const ItemDropRenderInstance> item_drops);
    void draw_creatures(std::span<const CreatureRenderInstance> creatures,
                        const glm::mat4& view_projection,
                        const glm::mat4& light_view_projection,
                        const glm::vec3& camera_position,
                        const EnvironmentState& environment);
    void draw_hotbar(const PlayerController& player, const HotbarState& hotbar, const EnvironmentState& environment, int width, int height);
    void draw_inventory_menu(const InventoryMenuState& inventory_menu, const HotbarState& hotbar, int width, int height);
    void draw_death_screen(const DeathScreenState& death_screen, int width, int height);
    void draw_pause_menu(const PauseMenuState& pause_menu, int width, int height);
    void draw_crosshair();

    GLuint world_program_ = 0;
    GLuint creature_program_ = 0;
    GLuint shadow_program_ = 0;
    GLuint hud_program_ = 0;
    GLuint crosshair_program_ = 0;
    GLuint sky_program_ = 0;
    GLuint post_process_program_ = 0;
    GLuint glow_extract_program_ = 0;
    GLuint glow_blur_program_ = 0;
    GLuint atlas_texture_ = 0;
    GLuint accent_texture_ = 0;
    GLuint creature_atlas_texture_ = 0;
    GLuint shadow_map_ = 0;
    GLuint shadow_framebuffer_ = 0;
    GLuint scene_framebuffer_ = 0;
    GLuint scene_color_texture_ = 0;
    GLuint scene_depth_renderbuffer_ = 0;
    GLuint glow_extract_framebuffer_ = 0;
    GLuint glow_extract_texture_ = 0;
    GLuint glow_ping_framebuffer_ = 0;
    GLuint glow_ping_texture_ = 0;
    GLuint screen_quad_vao_ = 0;
    GLuint creature_vao_ = 0;
    GLuint creature_vbo_ = 0;
    GLuint creature_ebo_ = 0;
    GLuint item_drop_vao_ = 0;
    GLuint item_drop_vbo_ = 0;
    GLuint hud_vao_ = 0;
    GLuint hud_vbo_ = 0;
    GLuint crosshair_vao_ = 0;
    GLuint crosshair_vbo_ = 0;
    RendererOptions options_ {};
    WorldUniformLocations world_uniforms_ {};
    CreatureUniformLocations creature_uniforms_ {};
    ShadowUniformLocations shadow_uniforms_ {};
    HudUniformLocations hud_uniforms_ {};
    SkyUniformLocations sky_uniforms_ {};
    PostProcessUniformLocations post_process_uniforms_ {};
    GlowExtractUniformLocations glow_extract_uniforms_ {};
    GlowBlurUniformLocations glow_blur_uniforms_ {};
    std::unordered_map<ChunkCoord, GpuMesh, ChunkCoordHash> gpu_meshes_ {};
    std::vector<VisibleChunk> visible_chunks_cache_ {};
    GLsizeiptr creature_vertex_buffer_bytes_ = 0;
    GLsizeiptr creature_index_buffer_bytes_ = 0;
    GLsizeiptr item_drop_vertex_buffer_bytes_ = 0;
    RendererFrameStats last_frame_stats_ {};
    int scene_target_width_ = 0;
    int scene_target_height_ = 0;
    int glow_target_width_ = 0;
    int glow_target_height_ = 0;
    bool gl_api_ready_ = false;
    bool initialized_ = false;
};

} // namespace valcraft
