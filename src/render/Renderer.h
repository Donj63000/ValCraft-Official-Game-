#pragma once

#include "app/Hotbar.h"
#include "gameplay/PlayerController.h"
#include "world/Environment.h"
#include "world/World.h"

#include <glad/gl.h>

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace valcraft {

struct RendererOptions {
    bool shadows_enabled = true;
    int shadow_map_size = 2048;
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
                      const EnvironmentState& environment,
                      int width,
                      int height);
    [[nodiscard]] auto last_frame_stats() const noexcept -> const RendererFrameStats&;

private:
    struct GpuMesh {
        GLuint vao = 0;
        GLuint vbo = 0;
        GLuint ebo = 0;
        GLsizei index_count = 0;
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
        GLint daylight_factor = -1;
        GLint sun_visibility = -1;
        GLint atlas = -1;
        GLint shadow_map = -1;
        GLint shadows_enabled = -1;
    };

    struct ShadowUniformLocations {
        GLint light_view_projection = -1;
    };

    struct HudUniformLocations {
        GLint atlas = -1;
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
    void create_shadow_map();
    void create_crosshair_geometry();
    void create_hud_geometry();
    void draw_hotbar(const HotbarState& hotbar, int width, int height);
    void draw_crosshair();

    GLuint world_program_ = 0;
    GLuint shadow_program_ = 0;
    GLuint hud_program_ = 0;
    GLuint crosshair_program_ = 0;
    GLuint atlas_texture_ = 0;
    GLuint shadow_map_ = 0;
    GLuint shadow_framebuffer_ = 0;
    GLuint hud_vao_ = 0;
    GLuint hud_vbo_ = 0;
    GLuint crosshair_vao_ = 0;
    GLuint crosshair_vbo_ = 0;
    RendererOptions options_ {};
    WorldUniformLocations world_uniforms_ {};
    ShadowUniformLocations shadow_uniforms_ {};
    HudUniformLocations hud_uniforms_ {};
    std::unordered_map<ChunkCoord, GpuMesh, ChunkCoordHash> gpu_meshes_ {};
    std::vector<VisibleChunk> visible_chunks_cache_ {};
    RendererFrameStats last_frame_stats_ {};
    bool gl_api_ready_ = false;
    bool initialized_ = false;
};

} // namespace valcraft
