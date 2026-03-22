#include "render/Renderer.h"
#include "render/HotbarLayout.h"

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace valcraft {

namespace {

constexpr auto kAtlasSize = 64;
constexpr auto kTileSize = 16;
constexpr auto kAtlasTilesPerAxis = 4.0F;
constexpr auto kShadowDistance = 96.0F;
constexpr auto kInitialVertexBufferBytes = static_cast<GLsizeiptr>(sizeof(ChunkVertex) * 256U);
constexpr auto kInitialIndexBufferBytes = static_cast<GLsizeiptr>(sizeof(std::uint32_t) * 384U);
constexpr auto kInitialHudBufferBytes = static_cast<GLsizeiptr>(sizeof(float) * 9U * 6U * 32U);

struct FrustumPlane {
    glm::vec3 normal {0.0F, 1.0F, 0.0F};
    float distance = 0.0F;
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

void set_texel(std::vector<std::uint8_t>& pixels, int x, int y, std::array<std::uint8_t, 3> color) {
    const auto index = static_cast<std::size_t>((y * kAtlasSize + x) * 4);
    pixels[index + 0] = color[0];
    pixels[index + 1] = color[1];
    pixels[index + 2] = color[2];
    pixels[index + 3] = 255;
}

void fill_tile(std::vector<std::uint8_t>& pixels, int tile_x, int tile_y, const auto& color_fn) {
    const auto start_x = tile_x * kTileSize;
    const auto start_y = tile_y * kTileSize;
    for (int y = 0; y < kTileSize; ++y) {
        for (int x = 0; x < kTileSize; ++x) {
            set_texel(pixels, start_x + x, start_y + y, color_fn(x, y));
        }
    }
}

auto make_plane(const glm::vec4& equation) -> FrustumPlane {
    const auto normal = glm::vec3 {equation.x, equation.y, equation.z};
    const auto length = glm::length(normal);
    if (length <= 1.0e-6F) {
        return {};
    }
    return {normal / length, equation.w / length};
}

auto extract_frustum_planes(const glm::mat4& matrix) -> std::array<FrustumPlane, 6> {
    const glm::vec4 row0 {matrix[0][0], matrix[1][0], matrix[2][0], matrix[3][0]};
    const glm::vec4 row1 {matrix[0][1], matrix[1][1], matrix[2][1], matrix[3][1]};
    const glm::vec4 row2 {matrix[0][2], matrix[1][2], matrix[2][2], matrix[3][2]};
    const glm::vec4 row3 {matrix[0][3], matrix[1][3], matrix[2][3], matrix[3][3]};

    return {{
        make_plane(row3 + row0),
        make_plane(row3 - row0),
        make_plane(row3 + row1),
        make_plane(row3 - row1),
        make_plane(row3 + row2),
        make_plane(row3 - row2),
    }};
}

auto intersects_frustum(const std::array<FrustumPlane, 6>& planes, const glm::vec3& min_corner, const glm::vec3& max_corner) -> bool {
    for (const auto& plane : planes) {
        const glm::vec3 positive_vertex {
            plane.normal.x >= 0.0F ? max_corner.x : min_corner.x,
            plane.normal.y >= 0.0F ? max_corner.y : min_corner.y,
            plane.normal.z >= 0.0F ? max_corner.z : min_corner.z,
        };
        if (glm::dot(plane.normal, positive_vertex) + plane.distance < 0.0F) {
            return false;
        }
    }
    return true;
}

auto grow_buffer_capacity(GLsizeiptr current_bytes, GLsizeiptr required_bytes, GLsizeiptr minimum_bytes) -> GLsizeiptr {
    auto capacity = std::max(current_bytes, minimum_bytes);
    while (capacity < required_bytes) {
        capacity = std::max(capacity * 2, required_bytes);
    }
    return capacity;
}

auto pixel_to_ndc_x(float x, float viewport_width) -> float {
    return (x / viewport_width) * 2.0F - 1.0F;
}

auto pixel_to_ndc_y(float y, float viewport_height) -> float {
    return (y / viewport_height) * 2.0F - 1.0F;
}

auto atlas_uv_rect(const HotbarAtlasTile& tile) -> std::array<float, 4> {
    const auto uv_step = 1.0F / kAtlasTilesPerAxis;
    const auto u0 = static_cast<float>(tile.x) * uv_step;
    const auto v0 = static_cast<float>(tile.y) * uv_step;
    return {u0, v0, u0 + uv_step, v0 + uv_step};
}

void append_hud_quad(std::vector<HudVertex>& vertices,
                     float viewport_width,
                     float viewport_height,
                     float x,
                     float y,
                     float width,
                     float height,
                     const std::array<float, 4>& color,
                     const std::array<float, 4>& uv_rect,
                     float textured) {
    const auto left = pixel_to_ndc_x(x, viewport_width);
    const auto right = pixel_to_ndc_x(x + width, viewport_width);
    const auto bottom = pixel_to_ndc_y(y, viewport_height);
    const auto top = pixel_to_ndc_y(y + height, viewport_height);
    const auto u0 = uv_rect[0];
    const auto v0 = uv_rect[1];
    const auto u1 = uv_rect[2];
    const auto v1 = uv_rect[3];

    vertices.insert(vertices.end(), {
        {left, bottom, u0, v0, color[0], color[1], color[2], color[3], textured},
        {right, bottom, u1, v0, color[0], color[1], color[2], color[3], textured},
        {right, top, u1, v1, color[0], color[1], color[2], color[3], textured},
        {left, bottom, u0, v0, color[0], color[1], color[2], color[3], textured},
        {right, top, u1, v1, color[0], color[1], color[2], color[3], textured},
        {left, top, u0, v1, color[0], color[1], color[2], color[3], textured},
    });
}

void append_hud_rect(std::vector<HudVertex>& vertices,
                     float viewport_width,
                     float viewport_height,
                     float x,
                     float y,
                     float width,
                     float height,
                     const std::array<float, 4>& color) {
    append_hud_quad(vertices, viewport_width, viewport_height, x, y, width, height, color, {0.0F, 0.0F, 0.0F, 0.0F}, 0.0F);
}

void append_hud_frame(std::vector<HudVertex>& vertices,
                      float viewport_width,
                      float viewport_height,
                      float x,
                      float y,
                      float width,
                      float height,
                      float border_thickness,
                      const std::array<float, 4>& border_color,
                      const std::array<float, 4>& fill_color) {
    append_hud_rect(vertices, viewport_width, viewport_height, x, y, width, height, border_color);

    const auto inner_x = x + border_thickness;
    const auto inner_y = y + border_thickness;
    const auto inner_width = std::max(0.0F, width - border_thickness * 2.0F);
    const auto inner_height = std::max(0.0F, height - border_thickness * 2.0F);
    if (inner_width > 0.0F && inner_height > 0.0F) {
        append_hud_rect(vertices, viewport_width, viewport_height, inner_x, inner_y, inner_width, inner_height, fill_color);
    }
}

} // namespace

Renderer::~Renderer() {
    shutdown();
}

auto Renderer::initialize(const RendererOptions& options) -> bool {
    if (initialized_ && options_.shadows_enabled == options.shadows_enabled && options_.shadow_map_size == options.shadow_map_size) {
        return true;
    }

    options_ = options;
    if (gl_api_ready_) {
        shutdown();
    }

    gl_api_ready_ = true;
    create_programs();
    create_atlas_texture();
    create_shadow_map();
    create_hud_geometry();
    create_crosshair_geometry();
    initialized_ = true;
    return true;
}

void Renderer::shutdown() {
    if (gl_api_ready_) {
        for (auto& entry : gpu_meshes_) {
            destroy_gpu_mesh(entry.second);
        }

        if (crosshair_vbo_ != 0) {
            glDeleteBuffers(1, &crosshair_vbo_);
        }
        if (crosshair_vao_ != 0) {
            glDeleteVertexArrays(1, &crosshair_vao_);
        }
        if (hud_vbo_ != 0) {
            glDeleteBuffers(1, &hud_vbo_);
        }
        if (hud_vao_ != 0) {
            glDeleteVertexArrays(1, &hud_vao_);
        }
        if (atlas_texture_ != 0) {
            glDeleteTextures(1, &atlas_texture_);
        }
        if (shadow_map_ != 0) {
            glDeleteTextures(1, &shadow_map_);
        }
        if (shadow_framebuffer_ != 0) {
            glDeleteFramebuffers(1, &shadow_framebuffer_);
        }
        if (world_program_ != 0) {
            glDeleteProgram(world_program_);
        }
        if (shadow_program_ != 0) {
            glDeleteProgram(shadow_program_);
        }
        if (hud_program_ != 0) {
            glDeleteProgram(hud_program_);
        }
        if (crosshair_program_ != 0) {
            glDeleteProgram(crosshair_program_);
        }
    }

    gpu_meshes_.clear();
    visible_chunks_cache_.clear();
    crosshair_vbo_ = 0;
    crosshair_vao_ = 0;
    hud_vbo_ = 0;
    hud_vao_ = 0;
    atlas_texture_ = 0;
    shadow_map_ = 0;
    shadow_framebuffer_ = 0;
    world_program_ = 0;
    shadow_program_ = 0;
    hud_program_ = 0;
    crosshair_program_ = 0;
    world_uniforms_ = {};
    shadow_uniforms_ = {};
    hud_uniforms_ = {};
    last_frame_stats_ = {};
    gl_api_ready_ = false;
    initialized_ = false;
}

void Renderer::render_frame(const World& world,
                            const PlayerController& player,
                            const HotbarState& hotbar,
                            const EnvironmentState& environment,
                            int width,
                            int height) {
    if (!initialized_) {
        return;
    }

    using clock = std::chrono::steady_clock;
    RendererFrameStats frame_stats {};

    const auto upload_start = clock::now();
    sync_gpu_meshes(world, frame_stats);
    frame_stats.upload_ms = std::chrono::duration<double, std::milli>(clock::now() - upload_start).count();

    const auto aspect = static_cast<float>(width) / static_cast<float>(std::max(height, 1));
    const auto projection = glm::perspective(glm::radians(75.0F), aspect, 0.1F, 320.0F);
    const auto view_projection = projection * player.view_matrix();
    const auto frustum_planes = extract_frustum_planes(view_projection);
    const auto eye = player.eye_position();
    auto forward = player.look_direction();
    forward.y = 0.0F;
    if (glm::dot(forward, forward) > 1.0e-6F) {
        forward = glm::normalize(forward);
    } else {
        forward = {0.0F, 0.0F, -1.0F};
    }

    const auto draw_distance = static_cast<float>((world.stream_radius() + 2) * kChunkSizeX);
    const auto draw_distance_sq = draw_distance * draw_distance;
    constexpr float kBackCullStartDistance = 20.0F;
    constexpr float kBackCullStartDistanceSq = kBackCullStartDistance * kBackCullStartDistance;
    auto& visible_chunks = visible_chunks_cache_;
    visible_chunks.clear();
    if (visible_chunks.capacity() < gpu_meshes_.size()) {
        visible_chunks.reserve(gpu_meshes_.size());
    }

    for (const auto& [coord, gpu_mesh] : gpu_meshes_) {
        if (gpu_mesh.index_count == 0) {
            continue;
        }

        const auto min_corner = glm::vec3 {
            static_cast<float>(coord.x * kChunkSizeX),
            0.0F,
            static_cast<float>(coord.z * kChunkSizeZ),
        };
        const auto max_corner = glm::vec3 {
            min_corner.x + static_cast<float>(kChunkSizeX),
            static_cast<float>(kChunkHeight),
            min_corner.z + static_cast<float>(kChunkSizeZ),
        };
        if (!intersects_frustum(frustum_planes, min_corner, max_corner)) {
            continue;
        }

        const auto center = (min_corner + max_corner) * 0.5F;
        const auto horizontal = glm::vec3 {center.x - eye.x, 0.0F, center.z - eye.z};
        const auto distance_sq = glm::dot(horizontal, horizontal);
        if (distance_sq > draw_distance_sq) {
            continue;
        }

        if (distance_sq > kBackCullStartDistanceSq) {
            const auto inverse_length = 1.0F / std::sqrt(distance_sq);
            const auto chunk_dir = horizontal * inverse_length;
            if (glm::dot(forward, chunk_dir) < -0.45F) {
                continue;
            }
        }

        visible_chunks.push_back({coord, &gpu_mesh, center, 0.0F});
    }
    frame_stats.visible_chunks = visible_chunks.size();

    const auto sun_visible = environment.sun_direction.y > 0.0F;
    glm::mat4 light_view_projection(1.0F);

    if (options_.shadows_enabled && sun_visible) {
        const auto shadow_start = clock::now();
        const auto shadow_map_size = std::max(options_.shadow_map_size, 1);
        const auto snap = (kShadowDistance * 2.0F) / static_cast<float>(shadow_map_size);
        const auto focus = player.position() + glm::vec3 {0.0F, 18.0F, 0.0F};
        const auto snapped_focus = glm::vec3 {
            std::floor(focus.x / snap) * snap,
            std::floor(focus.y / snap) * snap,
            std::floor(focus.z / snap) * snap,
        };
        const auto light_position = snapped_focus + glm::normalize(environment.sun_direction) * (kShadowDistance * 0.85F);
        const auto up = std::abs(environment.sun_direction.y) > 0.95F ? glm::vec3 {0.0F, 0.0F, 1.0F} : glm::vec3 {0.0F, 1.0F, 0.0F};
        const auto light_view = glm::lookAt(light_position, snapped_focus, up);
        const auto light_projection = glm::ortho(
            -kShadowDistance,
            kShadowDistance,
            -kShadowDistance,
            kShadowDistance,
            1.0F,
            kShadowDistance * 3.0F);
        light_view_projection = light_projection * light_view;

        glViewport(0, 0, shadow_map_size, shadow_map_size);
        glBindFramebuffer(GL_FRAMEBUFFER, shadow_framebuffer_);
        glClear(GL_DEPTH_BUFFER_BIT);
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(2.0F, 4.0F);

        glUseProgram(shadow_program_);
        glUniformMatrix4fv(shadow_uniforms_.light_view_projection, 1, GL_FALSE, glm::value_ptr(light_view_projection));

        const auto max_shadow_distance = kShadowDistance + static_cast<float>(kChunkSizeX);
        const auto max_shadow_distance_sq = max_shadow_distance * max_shadow_distance;
        for (const auto& visible_chunk : visible_chunks) {
            const auto horizontal = glm::vec3 {
                visible_chunk.center.x - focus.x,
                0.0F,
                visible_chunk.center.z - focus.z,
            };
            if (glm::dot(horizontal, horizontal) > max_shadow_distance_sq) {
                continue;
            }

            glBindVertexArray(visible_chunk.mesh->vao);
            glDrawElements(GL_TRIANGLES, visible_chunk.mesh->index_count, GL_UNSIGNED_INT, nullptr);
            ++frame_stats.shadow_chunks;
        }

        glDisable(GL_POLYGON_OFFSET_FILL);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        frame_stats.shadow_ms =
            std::chrono::duration<double, std::milli>(clock::now() - shadow_start).count();
    }

    const auto world_start = clock::now();
    glViewport(0, 0, width, std::max(height, 1));
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glClearColor(environment.sky_color.r, environment.sky_color.g, environment.sky_color.b, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glUseProgram(world_program_);
    glUniformMatrix4fv(world_uniforms_.view_projection, 1, GL_FALSE, glm::value_ptr(view_projection));
    glUniformMatrix4fv(world_uniforms_.light_view_projection, 1, GL_FALSE, glm::value_ptr(light_view_projection));
    glUniform3fv(world_uniforms_.camera_position, 1, glm::value_ptr(eye));
    glUniform3fv(world_uniforms_.sun_direction, 1, glm::value_ptr(environment.sun_direction));
    glUniform3fv(world_uniforms_.sun_color, 1, glm::value_ptr(environment.sun_color));
    glUniform3fv(world_uniforms_.ambient_color, 1, glm::value_ptr(environment.ambient_color));
    glUniform3fv(world_uniforms_.fog_color, 1, glm::value_ptr(environment.fog_color));
    glUniform1f(world_uniforms_.daylight_factor, environment.daylight_factor);
    glUniform1f(world_uniforms_.sun_visibility, sun_visible ? 1.0F : 0.0F);
    glUniform1i(world_uniforms_.atlas, 0);
    glUniform1i(world_uniforms_.shadow_map, 1);
    glUniform1i(world_uniforms_.shadows_enabled, options_.shadows_enabled ? 1 : 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, atlas_texture_);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, shadow_map_);

    for (const auto& visible_chunk : visible_chunks) {
        glBindVertexArray(visible_chunk.mesh->vao);
        glDrawElements(GL_TRIANGLES, visible_chunk.mesh->index_count, GL_UNSIGNED_INT, nullptr);
        ++frame_stats.world_chunks;
    }

    draw_hotbar(hotbar, width, height);
    draw_crosshair();
    frame_stats.world_ms = std::chrono::duration<double, std::milli>(clock::now() - world_start).count();
    last_frame_stats_ = frame_stats;
}

auto Renderer::last_frame_stats() const noexcept -> const RendererFrameStats& {
    return last_frame_stats_;
}

void Renderer::sync_gpu_meshes(const World& world, RendererFrameStats& frame_stats) {
    const auto& records = world.chunk_records();
    if (gpu_meshes_.bucket_count() < records.size()) {
        gpu_meshes_.reserve(records.size());
    }

    for (auto iterator = gpu_meshes_.begin(); iterator != gpu_meshes_.end();) {
        if (!records.contains(iterator->first)) {
            destroy_gpu_mesh(iterator->second);
            iterator = gpu_meshes_.erase(iterator);
        } else {
            ++iterator;
        }
    }

    for (const auto& [coord, record] : records) {
        if (record.mesh_revision == 0) {
            continue;
        }

        const auto gpu_iterator = gpu_meshes_.find(coord);
        if (gpu_iterator == gpu_meshes_.end() || gpu_iterator->second.revision != record.mesh_revision) {
            upload_mesh(coord, record.mesh, record.mesh_revision);
            ++frame_stats.uploaded_meshes;
        }
    }
}

void Renderer::upload_mesh(const ChunkCoord& coord, const ChunkMeshData& mesh, std::uint64_t revision) {
    auto& gpu_mesh = gpu_meshes_[coord];
    gpu_mesh.revision = revision;

    if (mesh.indices.empty()) {
        gpu_mesh.index_count = 0;
        return;
    }

    if (gpu_mesh.vao == 0) {
        glGenVertexArrays(1, &gpu_mesh.vao);
        glGenBuffers(1, &gpu_mesh.vbo);
        glGenBuffers(1, &gpu_mesh.ebo);

        glBindVertexArray(gpu_mesh.vao);
        glBindBuffer(GL_ARRAY_BUFFER, gpu_mesh.vbo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gpu_mesh.ebo);

        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(ChunkVertex), reinterpret_cast<void*>(offsetof(ChunkVertex, x)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(ChunkVertex), reinterpret_cast<void*>(offsetof(ChunkVertex, u)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(ChunkVertex), reinterpret_cast<void*>(offsetof(ChunkVertex, nx)));
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(ChunkVertex), reinterpret_cast<void*>(offsetof(ChunkVertex, face_shade)));
        glEnableVertexAttribArray(4);
        glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof(ChunkVertex), reinterpret_cast<void*>(offsetof(ChunkVertex, ao)));
        glEnableVertexAttribArray(5);
        glVertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE, sizeof(ChunkVertex), reinterpret_cast<void*>(offsetof(ChunkVertex, sky_light)));
        glEnableVertexAttribArray(6);
        glVertexAttribPointer(6, 1, GL_FLOAT, GL_FALSE, sizeof(ChunkVertex), reinterpret_cast<void*>(offsetof(ChunkVertex, block_light)));
    } else {
        glBindVertexArray(gpu_mesh.vao);
        glBindBuffer(GL_ARRAY_BUFFER, gpu_mesh.vbo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gpu_mesh.ebo);
    }

    const auto vertex_bytes = static_cast<GLsizeiptr>(mesh.vertices.size() * sizeof(ChunkVertex));
    const auto index_bytes = static_cast<GLsizeiptr>(mesh.indices.size() * sizeof(std::uint32_t));

    if (gpu_mesh.vertex_buffer_bytes < vertex_bytes) {
        gpu_mesh.vertex_buffer_bytes = grow_buffer_capacity(
            gpu_mesh.vertex_buffer_bytes,
            vertex_bytes,
            kInitialVertexBufferBytes);
        glBufferData(GL_ARRAY_BUFFER, gpu_mesh.vertex_buffer_bytes, nullptr, GL_DYNAMIC_DRAW);
    }
    if (gpu_mesh.index_buffer_bytes < index_bytes) {
        gpu_mesh.index_buffer_bytes = grow_buffer_capacity(
            gpu_mesh.index_buffer_bytes,
            index_bytes,
            kInitialIndexBufferBytes);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, gpu_mesh.index_buffer_bytes, nullptr, GL_DYNAMIC_DRAW);
    }

    glBufferSubData(GL_ARRAY_BUFFER, 0, vertex_bytes, mesh.vertices.data());
    glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, index_bytes, mesh.indices.data());

    gpu_mesh.index_count = static_cast<GLsizei>(mesh.indices.size());
}

void Renderer::destroy_gpu_mesh(GpuMesh& mesh) {
    if (mesh.ebo != 0) {
        glDeleteBuffers(1, &mesh.ebo);
        mesh.ebo = 0;
    }
    if (mesh.vbo != 0) {
        glDeleteBuffers(1, &mesh.vbo);
        mesh.vbo = 0;
    }
    if (mesh.vao != 0) {
        glDeleteVertexArrays(1, &mesh.vao);
        mesh.vao = 0;
    }
    mesh.index_count = 0;
    mesh.revision = 0;
    mesh.vertex_buffer_bytes = 0;
    mesh.index_buffer_bytes = 0;
}

auto Renderer::compile_shader(GLenum type, const char* source) -> GLuint {
    const auto shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint success = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (success == GL_TRUE) {
        return shader;
    }

    GLint log_length = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &log_length);
    std::string log(static_cast<std::size_t>(std::max(log_length, 1)), '\0');
    glGetShaderInfoLog(shader, log_length, nullptr, log.data());
    glDeleteShader(shader);
    throw std::runtime_error("Shader compilation failed: " + log);
}

auto Renderer::link_program(GLuint vertex_shader, GLuint fragment_shader) -> GLuint {
    const auto program = glCreateProgram();
    glAttachShader(program, vertex_shader);
    glAttachShader(program, fragment_shader);
    glLinkProgram(program);

    GLint success = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (success == GL_TRUE) {
        glDetachShader(program, vertex_shader);
        glDetachShader(program, fragment_shader);
        glDeleteShader(vertex_shader);
        glDeleteShader(fragment_shader);
        return program;
    }

    GLint log_length = 0;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &log_length);
    std::string log(static_cast<std::size_t>(std::max(log_length, 1)), '\0');
    glGetProgramInfoLog(program, log_length, nullptr, log.data());
    glDeleteProgram(program);
    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);
    throw std::runtime_error("Program link failed: " + log);
}

void Renderer::create_programs() {
    static constexpr auto* world_vertex_shader = R"(#version 330 core
layout(location = 0) in vec3 a_position;
layout(location = 1) in vec2 a_uv;
layout(location = 2) in vec3 a_normal;
layout(location = 3) in float a_face_shade;
layout(location = 4) in float a_ao;
layout(location = 5) in float a_sky_light;
layout(location = 6) in float a_block_light;

uniform mat4 u_view_projection;
uniform mat4 u_light_view_projection;
uniform vec3 u_camera_position;

out vec2 v_uv;
out vec3 v_normal;
out float v_face_shade;
out float v_ao;
out float v_sky_light;
out float v_block_light;
out float v_distance;
out vec4 v_light_position;

void main() {
    vec4 world_position = vec4(a_position, 1.0);
    gl_Position = u_view_projection * world_position;
    v_uv = a_uv;
    v_normal = a_normal;
    v_face_shade = a_face_shade;
    v_ao = a_ao;
    v_sky_light = a_sky_light;
    v_block_light = a_block_light;
    v_distance = distance(world_position.xyz, u_camera_position);
    v_light_position = u_light_view_projection * world_position;
}
)";

    static constexpr auto* world_fragment_shader = R"(#version 330 core
in vec2 v_uv;
in vec3 v_normal;
in float v_face_shade;
in float v_ao;
in float v_sky_light;
in float v_block_light;
in float v_distance;
in vec4 v_light_position;

uniform sampler2D u_atlas;
uniform sampler2D u_shadow_map;
uniform vec3 u_sun_direction;
uniform vec3 u_sun_color;
uniform vec3 u_ambient_color;
uniform vec3 u_fog_color;
uniform float u_daylight_factor;
uniform float u_sun_visibility;
uniform int u_shadows_enabled;

out vec4 frag_color;

float sample_shadow(vec3 normal) {
    if (u_sun_visibility < 0.5 || u_shadows_enabled == 0) {
        return 1.0;
    }

    vec3 projected = v_light_position.xyz / max(v_light_position.w, 0.0001);
    projected = projected * 0.5 + 0.5;
    if (projected.z > 1.0 || projected.x < 0.0 || projected.x > 1.0 || projected.y < 0.0 || projected.y > 1.0) {
        return 1.0;
    }

    vec2 texel_size = 1.0 / vec2(textureSize(u_shadow_map, 0));
    float ndotl = max(dot(normalize(normal), normalize(u_sun_direction)), 0.0);
    float bias = max(0.00065 * (1.0 - ndotl), 0.00012);
    float shadow = 0.0;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            float sampled_depth = texture(u_shadow_map, projected.xy + vec2(x, y) * texel_size).r;
            shadow += (projected.z - bias) <= sampled_depth ? 1.0 : 0.0;
        }
    }
    return shadow / 9.0;
}

void main() {
    vec3 albedo = texture(u_atlas, v_uv).rgb;
    vec3 normal = normalize(v_normal);

    float ambient_strength = mix(0.30, 1.00, clamp(v_sky_light, 0.0, 1.0));
    vec3 ambient = u_ambient_color * ambient_strength * clamp(v_ao, 0.0, 1.0);

    float sun_ndotl = max(dot(normal, normalize(u_sun_direction)), 0.0);
    float shadow = sample_shadow(normal);
    vec3 sunlight = u_sun_color * (sun_ndotl * shadow * u_sun_visibility * clamp(u_daylight_factor, 0.0, 1.0));

    vec3 torch_light = vec3(1.00, 0.74, 0.42) * clamp(v_block_light, 0.0, 1.0) * 1.35;

    vec3 lit_color = albedo * v_face_shade * (ambient + sunlight + torch_light);
    float fog = clamp(v_distance / 180.0, 0.0, 1.0);
    fog = fog * fog;
    frag_color = vec4(mix(lit_color, u_fog_color, fog), 1.0);
}
)";

    static constexpr auto* shadow_vertex_shader = R"(#version 330 core
layout(location = 0) in vec3 a_position;

uniform mat4 u_light_view_projection;

void main() {
    gl_Position = u_light_view_projection * vec4(a_position, 1.0);
}
)";

    static constexpr auto* shadow_fragment_shader = R"(#version 330 core
void main() {
}
)";

    static constexpr auto* hud_vertex_shader = R"(#version 330 core
layout(location = 0) in vec2 a_position;
layout(location = 1) in vec2 a_uv;
layout(location = 2) in vec4 a_color;
layout(location = 3) in float a_textured;

out vec2 v_uv;
out vec4 v_color;
flat out float v_textured;

void main() {
    gl_Position = vec4(a_position, 0.0, 1.0);
    v_uv = a_uv;
    v_color = a_color;
    v_textured = a_textured;
}
)";

    static constexpr auto* hud_fragment_shader = R"(#version 330 core
in vec2 v_uv;
in vec4 v_color;
flat in float v_textured;

uniform sampler2D u_atlas;

out vec4 frag_color;

void main() {
    vec4 color = v_color;
    if (v_textured > 0.5) {
        color *= texture(u_atlas, v_uv);
    }
    frag_color = color;
}
)";

    static constexpr auto* crosshair_vertex_shader = R"(#version 330 core
layout(location = 0) in vec2 a_position;

void main() {
    gl_Position = vec4(a_position, 0.0, 1.0);
}
)";

    static constexpr auto* crosshair_fragment_shader = R"(#version 330 core
out vec4 frag_color;

void main() {
    frag_color = vec4(0.98, 0.98, 0.98, 1.0);
}
)";

    world_program_ = link_program(
        compile_shader(GL_VERTEX_SHADER, world_vertex_shader),
        compile_shader(GL_FRAGMENT_SHADER, world_fragment_shader));
    shadow_program_ = link_program(
        compile_shader(GL_VERTEX_SHADER, shadow_vertex_shader),
        compile_shader(GL_FRAGMENT_SHADER, shadow_fragment_shader));
    hud_program_ = link_program(
        compile_shader(GL_VERTEX_SHADER, hud_vertex_shader),
        compile_shader(GL_FRAGMENT_SHADER, hud_fragment_shader));
    crosshair_program_ = link_program(
        compile_shader(GL_VERTEX_SHADER, crosshair_vertex_shader),
        compile_shader(GL_FRAGMENT_SHADER, crosshair_fragment_shader));

    world_uniforms_.view_projection = glGetUniformLocation(world_program_, "u_view_projection");
    world_uniforms_.light_view_projection = glGetUniformLocation(world_program_, "u_light_view_projection");
    world_uniforms_.camera_position = glGetUniformLocation(world_program_, "u_camera_position");
    world_uniforms_.sun_direction = glGetUniformLocation(world_program_, "u_sun_direction");
    world_uniforms_.sun_color = glGetUniformLocation(world_program_, "u_sun_color");
    world_uniforms_.ambient_color = glGetUniformLocation(world_program_, "u_ambient_color");
    world_uniforms_.fog_color = glGetUniformLocation(world_program_, "u_fog_color");
    world_uniforms_.daylight_factor = glGetUniformLocation(world_program_, "u_daylight_factor");
    world_uniforms_.sun_visibility = glGetUniformLocation(world_program_, "u_sun_visibility");
    world_uniforms_.atlas = glGetUniformLocation(world_program_, "u_atlas");
    world_uniforms_.shadow_map = glGetUniformLocation(world_program_, "u_shadow_map");
    world_uniforms_.shadows_enabled = glGetUniformLocation(world_program_, "u_shadows_enabled");

    shadow_uniforms_.light_view_projection = glGetUniformLocation(shadow_program_, "u_light_view_projection");
    hud_uniforms_.atlas = glGetUniformLocation(hud_program_, "u_atlas");
}

void Renderer::create_atlas_texture() {
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(kAtlasSize * kAtlasSize * 4), 255);

    fill_tile(pixels, 0, 0, [](int x, int y) {
        const auto tint = static_cast<std::uint8_t>(140 + ((x + y) % 4) * 8);
        return std::array<std::uint8_t, 3> {40, tint, 45};
    });
    fill_tile(pixels, 1, 0, [](int x, int y) {
        if (y < 4) {
            return std::array<std::uint8_t, 3> {50, static_cast<std::uint8_t>(150 + (x % 3) * 8), 55};
        }
        return std::array<std::uint8_t, 3> {120, static_cast<std::uint8_t>(88 + ((x + y) % 3) * 5), 52};
    });
    fill_tile(pixels, 2, 0, [](int x, int y) {
        const auto shade = static_cast<std::uint8_t>(94 + ((x * 3 + y) % 5) * 5);
        return std::array<std::uint8_t, 3> {125, shade, 68};
    });
    fill_tile(pixels, 3, 0, [](int x, int y) {
        const auto shade = static_cast<std::uint8_t>(115 + ((x + y * 2) % 6) * 8);
        return std::array<std::uint8_t, 3> {shade, shade, static_cast<std::uint8_t>(shade + 4)};
    });
    fill_tile(pixels, 0, 1, [](int x, int y) {
        const auto shade = static_cast<std::uint8_t>(180 + ((x + y) % 4) * 6);
        return std::array<std::uint8_t, 3> {shade, static_cast<std::uint8_t>(shade - 8), 102};
    });
    fill_tile(pixels, 1, 1, [](int x, int) {
        const auto stripe = ((x / 4) % 2) == 0 ? 0 : 18;
        return std::array<std::uint8_t, 3> {124, static_cast<std::uint8_t>(84 + stripe), 46};
    });
    fill_tile(pixels, 2, 1, [](int x, int y) {
        const auto cx = x - 7;
        const auto cy = y - 7;
        const auto ring = (cx * cx + cy * cy) / 16;
        return std::array<std::uint8_t, 3> {136, static_cast<std::uint8_t>(90 + (ring % 3) * 9), 52};
    });
    fill_tile(pixels, 3, 1, [](int x, int y) {
        const auto shade = ((x + y) % 5 == 0) ? 40 : 0;
        return std::array<std::uint8_t, 3> {45, static_cast<std::uint8_t>(130 - shade), 55};
    });
    fill_tile(pixels, 0, 2, [](int x, int y) {
        const auto glow = (x > 5 && x < 10 && y < 6) ? 48 : 0;
        return std::array<std::uint8_t, 3> {
            static_cast<std::uint8_t>(154 + glow),
            static_cast<std::uint8_t>(96 + glow / 2),
            42,
        };
    });

    glGenTextures(1, &atlas_texture_);
    glBindTexture(GL_TEXTURE_2D, atlas_texture_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kAtlasSize, kAtlasSize, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

void Renderer::create_shadow_map() {
    glGenTextures(1, &shadow_map_);
    glBindTexture(GL_TEXTURE_2D, shadow_map_);

    if (!options_.shadows_enabled) {
        const float depth_value = 1.0F;
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, 1, 1, 0, GL_DEPTH_COMPONENT, GL_FLOAT, &depth_value);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        return;
    }

    const auto shadow_map_size = std::max(options_.shadow_map_size, 1);
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_DEPTH_COMPONENT24,
        shadow_map_size,
        shadow_map_size,
        0,
        GL_DEPTH_COMPONENT,
        GL_FLOAT,
        nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    const std::array<float, 4> border_color {{1.0F, 1.0F, 1.0F, 1.0F}};
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border_color.data());

    glGenFramebuffers(1, &shadow_framebuffer_);
    glBindFramebuffer(GL_FRAMEBUFFER, shadow_framebuffer_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, shadow_map_, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        throw std::runtime_error("Shadow framebuffer is incomplete");
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Renderer::create_hud_geometry() {
    glGenVertexArrays(1, &hud_vao_);
    glGenBuffers(1, &hud_vbo_);
    glBindVertexArray(hud_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, hud_vbo_);
    glBufferData(GL_ARRAY_BUFFER, kInitialHudBufferBytes, nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(HudVertex), reinterpret_cast<void*>(offsetof(HudVertex, x)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(HudVertex), reinterpret_cast<void*>(offsetof(HudVertex, u)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(HudVertex), reinterpret_cast<void*>(offsetof(HudVertex, r)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(HudVertex), reinterpret_cast<void*>(offsetof(HudVertex, textured)));
}

void Renderer::create_crosshair_geometry() {
    static constexpr std::array<float, 8> kCrosshairVertices {{
        -0.015F, 0.0F,
        0.015F, 0.0F,
        0.0F, -0.02F,
        0.0F, 0.02F,
    }};

    glGenVertexArrays(1, &crosshair_vao_);
    glGenBuffers(1, &crosshair_vbo_);
    glBindVertexArray(crosshair_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, crosshair_vbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(kCrosshairVertices.size() * sizeof(float)), kCrosshairVertices.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 2, nullptr);
}

void Renderer::draw_hotbar(const HotbarState& hotbar, int width, int height) {
    if (width <= 0 || height <= 0 || hud_program_ == 0 || hud_vao_ == 0 || hud_vbo_ == 0) {
        return;
    }

    const auto layout = build_hotbar_layout(width, height, hotbar);
    std::vector<HudVertex> vertices;
    vertices.reserve(6U * (3U + static_cast<std::size_t>(kHotbarSlotCount) * 4U));

    const auto viewport_width = static_cast<float>(width);
    const auto viewport_height = static_cast<float>(height);
    const auto bar_border_thickness = std::max(2.0F, layout.slot_size * 0.06F);
    append_hud_frame(
        vertices,
        viewport_width,
        viewport_height,
        layout.bar_left,
        layout.bar_bottom,
        layout.bar_width,
        layout.bar_height,
        bar_border_thickness,
        {0.02F, 0.03F, 0.04F, 0.90F},
        {0.07F, 0.08F, 0.10F, 0.78F});

    const auto slot_border_thickness = std::max(2.0F, layout.slot_size * 0.07F);
    const auto glow_padding = std::max(2.0F, layout.slot_size * 0.05F);
    for (const auto& slot : layout.slots) {
        if (slot.is_selected) {
            append_hud_rect(
                vertices,
                viewport_width,
                viewport_height,
                slot.x - glow_padding,
                slot.y - glow_padding,
                slot.size + glow_padding * 2.0F,
                slot.size + glow_padding * 2.0F,
                {1.00F, 0.84F, 0.42F, 0.18F});
        }

        const auto border_color = slot.is_selected
                                      ? std::array<float, 4> {0.97F, 0.88F, 0.52F, 1.0F}
                                      : std::array<float, 4> {0.28F, 0.31F, 0.35F, 0.92F};
        const auto fill_color = slot.is_selected
                                    ? std::array<float, 4> {0.20F, 0.22F, 0.25F, 0.95F}
                                    : (slot.slot.is_empty_utility
                                           ? std::array<float, 4> {0.09F, 0.10F, 0.12F, 0.66F}
                                           : std::array<float, 4> {0.12F, 0.13F, 0.15F, 0.84F});
        append_hud_frame(
            vertices,
            viewport_width,
            viewport_height,
            slot.x,
            slot.y,
            slot.size,
            slot.size,
            slot_border_thickness,
            border_color,
            fill_color);

        if (!slot.has_icon) {
            continue;
        }

        const auto icon_size = std::max(8.0F, slot.size - layout.icon_inset * 2.0F);
        const auto icon_offset = (slot.size - icon_size) * 0.5F;
        append_hud_quad(
            vertices,
            viewport_width,
            viewport_height,
            slot.x + icon_offset,
            slot.y + icon_offset,
            icon_size,
            icon_size,
            {1.0F, 1.0F, 1.0F, slot.is_selected ? 1.0F : 0.95F},
            atlas_uv_rect(slot.icon_tile),
            1.0F);
    }

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glUseProgram(hud_program_);
    glUniform1i(hud_uniforms_.atlas, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, atlas_texture_);

    glBindVertexArray(hud_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, hud_vbo_);
    glBufferData(
        GL_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(vertices.size() * sizeof(HudVertex)),
        vertices.data(),
        GL_DYNAMIC_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size()));

    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
}

void Renderer::draw_crosshair() {
    glDisable(GL_DEPTH_TEST);
    glUseProgram(crosshair_program_);
    glBindVertexArray(crosshair_vao_);
    glDrawArrays(GL_LINES, 0, 4);
    glEnable(GL_DEPTH_TEST);
}

} // namespace valcraft
