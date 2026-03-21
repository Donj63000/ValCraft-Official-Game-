#include "render/Renderer.h"

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace valcraft {

namespace {

constexpr auto kAtlasSize = 64;
constexpr auto kTileSize = 16;

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

} // namespace

Renderer::~Renderer() {
    shutdown();
}

auto Renderer::initialize() -> bool {
    if (initialized_) {
        return true;
    }

    if (gl_api_ready_) {
        shutdown();
    }

    gl_api_ready_ = true;
    create_programs();
    create_atlas_texture();
    create_crosshair_geometry();
    initialized_ = true;
    return true;
}

void Renderer::shutdown() {
    if (gl_api_ready_) {
        for (auto& [coord, mesh] : gpu_meshes_) {
            destroy_gpu_mesh(mesh);
        }

        if (crosshair_vbo_ != 0) {
            glDeleteBuffers(1, &crosshair_vbo_);
        }
        if (crosshair_vao_ != 0) {
            glDeleteVertexArrays(1, &crosshair_vao_);
        }
        if (atlas_texture_ != 0) {
            glDeleteTextures(1, &atlas_texture_);
        }
        if (world_program_ != 0) {
            glDeleteProgram(world_program_);
        }
        if (crosshair_program_ != 0) {
            glDeleteProgram(crosshair_program_);
        }
    }

    gpu_meshes_.clear();
    crosshair_vbo_ = 0;
    crosshair_vao_ = 0;
    atlas_texture_ = 0;
    world_program_ = 0;
    crosshair_program_ = 0;
    gl_api_ready_ = false;
    initialized_ = false;
}

void Renderer::render_frame(const World& world, const PlayerController& player, int width, int height) {
    if (!initialized_) {
        return;
    }

    sync_gpu_meshes(world);

    glViewport(0, 0, width, std::max(height, 1));
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glClearColor(0.53F, 0.78F, 0.96F, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    const auto aspect = static_cast<float>(width) / static_cast<float>(std::max(height, 1));
    const auto projection = glm::perspective(glm::radians(75.0F), aspect, 0.1F, 320.0F);
    const auto view_projection = projection * player.view_matrix();
    const auto eye = player.eye_position();
    auto forward = player.look_direction();
    forward.y = 0.0F;
    if (glm::dot(forward, forward) > 1.0e-6F) {
        forward = glm::normalize(forward);
    } else {
        forward = {0.0F, 0.0F, -1.0F};
    }

    glUseProgram(world_program_);
    glUniformMatrix4fv(glGetUniformLocation(world_program_, "u_view_projection"), 1, GL_FALSE, glm::value_ptr(view_projection));
    glUniform3fv(glGetUniformLocation(world_program_, "u_camera_position"), 1, glm::value_ptr(eye));
    glUniform3f(glGetUniformLocation(world_program_, "u_sky_color"), 0.53F, 0.78F, 0.96F);
    glUniform1i(glGetUniformLocation(world_program_, "u_atlas"), 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, atlas_texture_);

    const auto draw_distance = static_cast<float>((world.stream_radius() + 2) * kChunkSizeX);
    for (const auto& [coord, gpu_mesh] : gpu_meshes_) {
        if (gpu_mesh.index_count == 0) {
            continue;
        }

        const auto chunk_center = glm::vec3 {
            static_cast<float>(coord.x * kChunkSizeX) + static_cast<float>(kChunkSizeX) * 0.5F,
            static_cast<float>(kChunkHeight) * 0.5F,
            static_cast<float>(coord.z * kChunkSizeZ) + static_cast<float>(kChunkSizeZ) * 0.5F,
        };
        auto to_chunk = chunk_center - eye;
        const auto horizontal = glm::vec3 {to_chunk.x, 0.0F, to_chunk.z};
        const auto distance = glm::length(horizontal);
        if (distance > draw_distance) {
            continue;
        }

        if (distance > 20.0F && glm::dot(horizontal, horizontal) > 1.0e-6F) {
            const auto chunk_dir = glm::normalize(horizontal);
            if (glm::dot(forward, chunk_dir) < -0.45F) {
                continue;
            }
        }

        glBindVertexArray(gpu_mesh.vao);
        glDrawElements(GL_TRIANGLES, gpu_mesh.index_count, GL_UNSIGNED_INT, nullptr);
    }

    draw_crosshair();
}

void Renderer::sync_gpu_meshes(const World& world) {
    const auto& records = world.chunk_records();

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
        auto gpu_iterator = gpu_meshes_.find(coord);
        if (gpu_iterator == gpu_meshes_.end() || gpu_iterator->second.revision != record.mesh_revision) {
            upload_mesh(coord, record.mesh, record.mesh_revision);
        }
    }
}

void Renderer::upload_mesh(const ChunkCoord& coord, const ChunkMeshData& mesh, std::uint64_t revision) {
    auto& gpu_mesh = gpu_meshes_[coord];
    destroy_gpu_mesh(gpu_mesh);
    gpu_mesh.revision = revision;

    if (mesh.indices.empty()) {
        gpu_mesh.index_count = 0;
        return;
    }

    glGenVertexArrays(1, &gpu_mesh.vao);
    glGenBuffers(1, &gpu_mesh.vbo);
    glGenBuffers(1, &gpu_mesh.ebo);

    glBindVertexArray(gpu_mesh.vao);
    glBindBuffer(GL_ARRAY_BUFFER, gpu_mesh.vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(mesh.vertices.size() * sizeof(ChunkVertex)), mesh.vertices.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gpu_mesh.ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(mesh.indices.size() * sizeof(std::uint32_t)), mesh.indices.data(), GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(ChunkVertex), reinterpret_cast<void*>(offsetof(ChunkVertex, x)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(ChunkVertex), reinterpret_cast<void*>(offsetof(ChunkVertex, u)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(ChunkVertex), reinterpret_cast<void*>(offsetof(ChunkVertex, shade)));

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
layout(location = 2) in float a_shade;

uniform mat4 u_view_projection;
uniform vec3 u_camera_position;

out vec2 v_uv;
out float v_shade;
out float v_distance;

void main() {
    vec4 world_position = vec4(a_position, 1.0);
    gl_Position = u_view_projection * world_position;
    v_uv = a_uv;
    v_shade = a_shade;
    v_distance = distance(world_position.xyz, u_camera_position);
}
)";

    static constexpr auto* world_fragment_shader = R"(#version 330 core
in vec2 v_uv;
in float v_shade;
in float v_distance;

uniform sampler2D u_atlas;
uniform vec3 u_sky_color;

out vec4 frag_color;

void main() {
    vec3 base_color = texture(u_atlas, v_uv).rgb * v_shade;
    float fog = clamp(v_distance / 180.0, 0.0, 1.0);
    fog = fog * fog;
    frag_color = vec4(mix(base_color, u_sky_color, fog), 1.0);
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
    crosshair_program_ = link_program(
        compile_shader(GL_VERTEX_SHADER, crosshair_vertex_shader),
        compile_shader(GL_FRAGMENT_SHADER, crosshair_fragment_shader));
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

    glGenTextures(1, &atlas_texture_);
    glBindTexture(GL_TEXTURE_2D, atlas_texture_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kAtlasSize, kAtlasSize, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
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

void Renderer::draw_crosshair() {
    glDisable(GL_DEPTH_TEST);
    glUseProgram(crosshair_program_);
    glBindVertexArray(crosshair_vao_);
    glDrawArrays(GL_LINES, 0, 4);
    glEnable(GL_DEPTH_TEST);
}

} // namespace valcraft
