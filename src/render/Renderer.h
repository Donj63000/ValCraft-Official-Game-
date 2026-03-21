#pragma once

#include "gameplay/PlayerController.h"
#include "world/World.h"

#include <glad/gl.h>

#include <cstdint>
#include <unordered_map>

namespace valcraft {

class Renderer {
public:
    Renderer() = default;
    ~Renderer();

    Renderer(const Renderer&) = delete;
    auto operator=(const Renderer&) -> Renderer& = delete;

    auto initialize() -> bool;
    void shutdown();
    void render_frame(const World& world, const PlayerController& player, int width, int height);

private:
    struct GpuMesh {
        GLuint vao = 0;
        GLuint vbo = 0;
        GLuint ebo = 0;
        GLsizei index_count = 0;
        std::uint64_t revision = 0;
    };

    void sync_gpu_meshes(const World& world);
    void upload_mesh(const ChunkCoord& coord, const ChunkMeshData& mesh, std::uint64_t revision);
    void destroy_gpu_mesh(GpuMesh& mesh);
    auto compile_shader(GLenum type, const char* source) -> GLuint;
    auto link_program(GLuint vertex_shader, GLuint fragment_shader) -> GLuint;
    void create_programs();
    void create_atlas_texture();
    void create_crosshair_geometry();
    void draw_crosshair();

    GLuint world_program_ = 0;
    GLuint crosshair_program_ = 0;
    GLuint atlas_texture_ = 0;
    GLuint crosshair_vao_ = 0;
    GLuint crosshair_vbo_ = 0;
    std::unordered_map<ChunkCoord, GpuMesh, ChunkCoordHash> gpu_meshes_ {};
    bool gl_api_ready_ = false;
    bool initialized_ = false;
};

} // namespace valcraft
