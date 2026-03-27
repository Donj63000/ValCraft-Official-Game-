#include "app/GameBranding.h"
#include "render/Renderer.h"
#include "render/ShadowCulling.h"
#include "creatures/CreatureGeometry.h"
#include "render/HotbarLayout.h"
#include "world/BlockVisuals.h"

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace valcraft {

namespace {

constexpr auto kShadowDistance = 96.0F;
constexpr auto kInitialVertexBufferBytes = static_cast<GLsizeiptr>(sizeof(ChunkVertex) * 256U);
constexpr auto kInitialIndexBufferBytes = static_cast<GLsizeiptr>(sizeof(std::uint32_t) * 384U);
constexpr std::size_t kCreatureVerticesPerBox = 24U;
constexpr std::size_t kCreatureIndicesPerBox = 36U;
constexpr std::size_t kCreatureDayBoxBudget = 30U;
constexpr std::size_t kCreatureNightBoxBudget = 40U;
constexpr std::size_t kCreatureMaxBoxBudget = kCreatureDayBoxBudget > kCreatureNightBoxBudget ? kCreatureDayBoxBudget : kCreatureNightBoxBudget;
constexpr std::size_t kCreatureMaxRenderedCount = 12U;
constexpr auto kInitialCreatureVertexBufferBytes =
    static_cast<GLsizeiptr>(sizeof(CreatureVertex) * kCreatureVerticesPerBox * kCreatureMaxBoxBudget * kCreatureMaxRenderedCount);
constexpr auto kInitialCreatureIndexBufferBytes =
    static_cast<GLsizeiptr>(sizeof(std::uint32_t) * kCreatureIndicesPerBox * kCreatureMaxBoxBudget * kCreatureMaxRenderedCount);
constexpr auto kInitialItemDropVertexBufferBytes = static_cast<GLsizeiptr>(sizeof(ChunkVertex) * 768U);
constexpr auto kInitialHudBufferBytes = static_cast<GLsizeiptr>(sizeof(float) * 9U * 6U * 32U);
constexpr std::size_t kMaxGpuMeshEventsPerFrame = 8;
constexpr double kMaxGpuMeshSyncMsPerFrame = 1.0;

auto grow_buffer_capacity(GLsizeiptr current_bytes, GLsizeiptr required_bytes, GLsizeiptr minimum_bytes) -> GLsizeiptr {
    auto capacity = std::max(current_bytes, minimum_bytes);
    while (capacity < required_bytes) {
        capacity = std::max(capacity * 2, required_bytes);
    }
    return capacity;
}

auto quantize_hud_value(float value, float steps_per_unit) -> int {
    return static_cast<int>(std::lround(value * steps_per_unit));
}

auto pixel_to_ndc_x(float x, float viewport_width) -> float {
    return (x / viewport_width) * 2.0F - 1.0F;
}

auto pixel_to_ndc_y(float y, float viewport_height) -> float {
    return (y / viewport_height) * 2.0F - 1.0F;
}

auto atlas_uv_rect(const HotbarAtlasTile& tile) -> std::array<float, 4> {
    const auto uv_step = 1.0F / kBlockAtlasTilesPerAxis;
    const auto u0 = static_cast<float>(tile.x) * uv_step;
    const auto v0 = static_cast<float>(tile.y) * uv_step;
    return {u0, v0, u0 + uv_step, v0 + uv_step};
}

[[maybe_unused]] auto accent_uv_rect(const AccentAtlasTile& tile) -> std::array<float, 4> {
    const auto uv_step = 1.0F / kAccentAtlasTilesPerAxis;
    const auto u0 = static_cast<float>(tile.x) * uv_step;
    const auto v0 = static_cast<float>(tile.y) * uv_step;
    return {u0, v0, u0 + uv_step, v0 + uv_step};
}

void append_item_drop_quad(std::vector<ChunkVertex>& vertices,
                           const glm::vec3& bottom_center,
                           const glm::vec3& right,
                           const glm::vec3& up,
                           const std::array<float, 4>& uv_rect,
                           float sky_light,
                           float block_light,
                           float material_class) {
    const auto bottom_left = bottom_center - right;
    const auto bottom_right = bottom_center + right;
    const auto top_right = bottom_right + up;
    const auto top_left = bottom_left + up;
    auto normal = glm::cross(bottom_right - bottom_left, top_left - bottom_left);
    if (glm::dot(normal, normal) <= 1.0e-6F) {
        normal = glm::vec3 {0.0F, 0.0F, 1.0F};
    } else {
        normal = glm::normalize(normal);
    }

    const auto u0 = uv_rect[0];
    const auto v0 = uv_rect[1];
    const auto u1 = uv_rect[2];
    const auto v1 = uv_rect[3];

    vertices.insert(vertices.end(), {
        {bottom_left.x, bottom_left.y, bottom_left.z, u0, v1, normal.x, normal.y, normal.z, 1.0F, 1.0F, sky_light, block_light, material_class},
        {bottom_right.x, bottom_right.y, bottom_right.z, u1, v1, normal.x, normal.y, normal.z, 1.0F, 1.0F, sky_light, block_light, material_class},
        {top_right.x, top_right.y, top_right.z, u1, v0, normal.x, normal.y, normal.z, 1.0F, 1.0F, sky_light, block_light, material_class},
        {bottom_left.x, bottom_left.y, bottom_left.z, u0, v1, normal.x, normal.y, normal.z, 1.0F, 1.0F, sky_light, block_light, material_class},
        {top_right.x, top_right.y, top_right.z, u1, v0, normal.x, normal.y, normal.z, 1.0F, 1.0F, sky_light, block_light, material_class},
        {top_left.x, top_left.y, top_left.z, u0, v0, normal.x, normal.y, normal.z, 1.0F, 1.0F, sky_light, block_light, material_class},
    });
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

auto bottom_to_top_left_y(float viewport_height, float bottom, float height) -> float {
    return viewport_height - bottom - height;
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

void append_hud_beveled_panel(std::vector<HudVertex>& vertices,
                              float viewport_width,
                              float viewport_height,
                              float x,
                              float y,
                              float width,
                              float height,
                              float border_thickness,
                              const std::array<float, 4>& border_color,
                              const std::array<float, 4>& fill_color,
                              const std::array<float, 4>& highlight_color,
                              const std::array<float, 4>& shadow_color) {
    append_hud_frame(vertices, viewport_width, viewport_height, x, y, width, height, border_thickness, border_color, fill_color);

    const auto inner_x = x + border_thickness;
    const auto inner_y = y + border_thickness;
    const auto inner_width = std::max(0.0F, width - border_thickness * 2.0F);
    const auto inner_height = std::max(0.0F, height - border_thickness * 2.0F);
    if (inner_width <= 2.0F || inner_height <= 2.0F) {
        return;
    }

    const auto bevel = std::max(1.0F, static_cast<float>(std::floor(border_thickness * 0.55F)));
    append_hud_rect(
        vertices,
        viewport_width,
        viewport_height,
        inner_x,
        inner_y + std::max(0.0F, inner_height - bevel),
        inner_width,
        bevel,
        highlight_color);
    append_hud_rect(vertices, viewport_width, viewport_height, inner_x, inner_y, bevel, inner_height, highlight_color);
    append_hud_rect(vertices, viewport_width, viewport_height, inner_x, inner_y, inner_width, bevel, shadow_color);
    append_hud_rect(
        vertices,
        viewport_width,
        viewport_height,
        inner_x + std::max(0.0F, inner_width - bevel),
        inner_y,
        bevel,
        inner_height,
        shadow_color);
}

void append_hud_quad_top_left(std::vector<HudVertex>& vertices,
                              float viewport_width,
                              float viewport_height,
                              float x,
                              float y,
                              float width,
                              float height,
                              const std::array<float, 4>& color,
                              const std::array<float, 4>& uv_rect,
                              float textured) {
    append_hud_quad(vertices, viewport_width, viewport_height, x, viewport_height - y - height, width, height, color, uv_rect, textured);
}

void append_hud_rect_top_left(std::vector<HudVertex>& vertices,
                              float viewport_width,
                              float viewport_height,
                              float x,
                              float y,
                              float width,
                              float height,
                              const std::array<float, 4>& color) {
    append_hud_quad_top_left(vertices, viewport_width, viewport_height, x, y, width, height, color, {0.0F, 0.0F, 0.0F, 0.0F}, 0.0F);
}

void append_hud_frame_top_left(std::vector<HudVertex>& vertices,
                               float viewport_width,
                               float viewport_height,
                               float x,
                               float y,
                               float width,
                               float height,
                               float border_thickness,
                               const std::array<float, 4>& border_color,
                               const std::array<float, 4>& fill_color) {
    append_hud_rect_top_left(vertices, viewport_width, viewport_height, x, y, width, height, border_color);

    const auto inner_x = x + border_thickness;
    const auto inner_y = y + border_thickness;
    const auto inner_width = std::max(0.0F, width - border_thickness * 2.0F);
    const auto inner_height = std::max(0.0F, height - border_thickness * 2.0F);
    if (inner_width > 0.0F && inner_height > 0.0F) {
        append_hud_rect_top_left(vertices, viewport_width, viewport_height, inner_x, inner_y, inner_width, inner_height, fill_color);
    }
}

void append_hud_beveled_panel_top_left(std::vector<HudVertex>& vertices,
                                       float viewport_width,
                                       float viewport_height,
                                       float x,
                                       float y,
                                       float width,
                                       float height,
                                       float border_thickness,
                                       const std::array<float, 4>& border_color,
                                       const std::array<float, 4>& fill_color,
                                       const std::array<float, 4>& highlight_color,
                                       const std::array<float, 4>& shadow_color) {
    append_hud_frame_top_left(vertices, viewport_width, viewport_height, x, y, width, height, border_thickness, border_color, fill_color);

    const auto inner_x = x + border_thickness;
    const auto inner_y = y + border_thickness;
    const auto inner_width = std::max(0.0F, width - border_thickness * 2.0F);
    const auto inner_height = std::max(0.0F, height - border_thickness * 2.0F);
    if (inner_width <= 2.0F || inner_height <= 2.0F) {
        return;
    }

    const auto bevel = std::max(1.0F, static_cast<float>(std::floor(border_thickness * 0.55F)));
    append_hud_rect_top_left(vertices, viewport_width, viewport_height, inner_x, inner_y, inner_width, bevel, highlight_color);
    append_hud_rect_top_left(vertices, viewport_width, viewport_height, inner_x, inner_y, bevel, inner_height, highlight_color);
    append_hud_rect_top_left(
        vertices,
        viewport_width,
        viewport_height,
        inner_x,
        inner_y + std::max(0.0F, inner_height - bevel),
        inner_width,
        bevel,
        shadow_color);
    append_hud_rect_top_left(
        vertices,
        viewport_width,
        viewport_height,
        inner_x + std::max(0.0F, inner_width - bevel),
        inner_y,
        bevel,
        inner_height,
        shadow_color);
}

[[maybe_unused]] void append_segmented_meter_top_left(std::vector<HudVertex>& vertices,
                                                      float viewport_width,
                                                      float viewport_height,
                                                      float x,
                                                      float y,
                                                      float width,
                                                      float height,
                                                      std::size_t segments,
                                                      float fill_ratio,
                                                      const std::array<float, 4>& border_color,
                                                      const std::array<float, 4>& background_color,
                                                      const std::array<float, 4>& empty_segment_color,
                                                      const std::array<float, 4>& fill_segment_color) {
    append_hud_beveled_panel_top_left(
        vertices,
        viewport_width,
        viewport_height,
        x,
        y,
        width,
        height,
        3.0F,
        border_color,
        background_color,
        {1.0F, 1.0F, 1.0F, 0.10F},
        {0.0F, 0.0F, 0.0F, 0.34F});

    const auto inner_x = x + 6.0F;
    const auto inner_y = y + 5.0F;
    const auto inner_width = std::max(0.0F, width - 12.0F);
    const auto inner_height = std::max(0.0F, height - 10.0F);
    const auto gap = std::max(2.0F, inner_height * 0.18F);
    const auto segment_width =
        (inner_width - gap * static_cast<float>(segments > 0 ? segments - 1 : 0)) / static_cast<float>(std::max<std::size_t>(segments, 1));
    const auto filled_segments = glm::clamp(fill_ratio, 0.0F, 1.0F) * static_cast<float>(segments);

    for (std::size_t index = 0; index < segments; ++index) {
        const auto segment_x = inner_x + static_cast<float>(index) * (segment_width + gap);
        append_hud_rect_top_left(
            vertices,
            viewport_width,
            viewport_height,
            segment_x,
            inner_y,
            segment_width,
            inner_height,
            empty_segment_color);

        const auto segment_fill = glm::clamp(filled_segments - static_cast<float>(index), 0.0F, 1.0F);
        if (segment_fill <= 0.0F) {
            continue;
        }

        append_hud_rect_top_left(
            vertices,
            viewport_width,
            viewport_height,
            segment_x,
            inner_y,
            segment_width * segment_fill,
            inner_height,
            fill_segment_color);
        append_hud_rect_top_left(
            vertices,
            viewport_width,
            viewport_height,
            segment_x,
            inner_y,
            segment_width * segment_fill,
            std::max(1.0F, inner_height * 0.18F),
            {1.0F, 1.0F, 1.0F, fill_segment_color[3] * 0.18F});
    }
}

auto glyph_rows(char character) -> std::array<std::uint8_t, 7> {
    switch (static_cast<char>(std::toupper(static_cast<unsigned char>(character)))) {
    case '0': return {{0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}};
    case '1': return {{0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}};
    case '2': return {{0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}};
    case '3': return {{0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E}};
    case '4': return {{0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}};
    case '5': return {{0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E}};
    case '6': return {{0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}};
    case '7': return {{0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}};
    case '8': return {{0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}};
    case '9': return {{0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}};
    case 'A': return {{0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}};
    case 'B': return {{0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}};
    case 'C': return {{0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}};
    case 'D': return {{0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E}};
    case 'E': return {{0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}};
    case 'F': return {{0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}};
    case 'G': return {{0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0E}};
    case 'H': return {{0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}};
    case 'I': return {{0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1F}};
    case 'J': return {{0x1F, 0x02, 0x02, 0x02, 0x12, 0x12, 0x0C}};
    case 'K': return {{0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}};
    case 'L': return {{0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}};
    case 'M': return {{0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}};
    case 'N': return {{0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11}};
    case 'O': return {{0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}};
    case 'P': return {{0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}};
    case 'Q': return {{0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}};
    case 'R': return {{0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}};
    case 'S': return {{0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}};
    case 'T': return {{0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}};
    case 'U': return {{0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}};
    case 'V': return {{0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04}};
    case 'W': return {{0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A}};
    case 'X': return {{0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11}};
    case 'Y': return {{0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04}};
    case 'Z': return {{0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}};
    case '(': return {{0x03, 0x06, 0x0C, 0x0C, 0x0C, 0x06, 0x03}};
    case ')': return {{0x18, 0x0C, 0x06, 0x06, 0x06, 0x0C, 0x18}};
    case '+': return {{0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00}};
    case '.': return {{0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x06}};
    case '-': return {{0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00}};
    default: return {{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}};
    }
}

auto measure_pixel_text(std::string_view text, float pixel_size) -> float {
    float width = 0.0F;
    bool first = true;
    for (const auto character : text) {
        if (!first) {
            width += pixel_size;
        }
        width += (character == ' ' ? 3.0F : 5.0F) * pixel_size;
        first = false;
    }
    return width;
}

void append_pixel_text(std::vector<HudVertex>& vertices,
                       float viewport_width,
                       float viewport_height,
                       float x,
                       float y,
                       float pixel_size,
                       std::string_view text,
                       const std::array<float, 4>& color,
                       bool centered = false) {
    auto cursor_x = x;
    if (centered) {
        cursor_x -= measure_pixel_text(text, pixel_size) * 0.5F;
    }

    for (const auto character : text) {
        if (character == ' ') {
            cursor_x += pixel_size * 4.0F;
            continue;
        }

        const auto rows = glyph_rows(character);
        for (std::size_t row = 0; row < rows.size(); ++row) {
            for (int column = 0; column < 5; ++column) {
                const auto bit = static_cast<std::uint8_t>(1U << (4 - column));
                if ((rows[row] & bit) == 0U) {
                    continue;
                }
                append_hud_rect_top_left(
                    vertices,
                    viewport_width,
                    viewport_height,
                    cursor_x + static_cast<float>(column) * pixel_size,
                    y + static_cast<float>(row) * pixel_size,
                    pixel_size,
                    pixel_size,
                    color);
            }
        }

        cursor_x += pixel_size * 6.0F;
    }
}

void append_pixel_text_bottom_left(std::vector<HudVertex>& vertices,
                                   float viewport_width,
                                   float viewport_height,
                                   float x,
                                   float bottom,
                                   float pixel_size,
                                   std::string_view text,
                                   const std::array<float, 4>& color,
                                   bool centered = false) {
    const auto text_height = pixel_size * 7.0F;
    append_pixel_text(
        vertices,
        viewport_width,
        viewport_height,
        x,
        bottom_to_top_left_y(viewport_height, bottom, text_height),
        pixel_size,
        text,
        color,
        centered);
}

void append_stack_count(std::vector<HudVertex>& vertices,
                        float viewport_width,
                        float viewport_height,
                        float right_x,
                        float bottom_y,
                        float pixel_size,
                        std::uint8_t count) {
    if (count <= 1) {
        return;
    }

    const auto count_text = std::to_string(count);
    const auto text_width = measure_pixel_text(count_text, pixel_size);
    const auto text_x = right_x - text_width;
    const auto text_y = bottom_y - pixel_size * 7.0F;

    append_pixel_text(
        vertices,
        viewport_width,
        viewport_height,
        text_x + pixel_size,
        text_y + pixel_size,
        pixel_size,
        count_text,
        {0.0F, 0.0F, 0.0F, 0.62F});
    append_pixel_text(
        vertices,
        viewport_width,
        viewport_height,
        text_x,
        text_y,
        pixel_size,
        count_text,
        {0.98F, 0.98F, 0.98F, 0.98F});
}

void append_stack_count_bottom_left(std::vector<HudVertex>& vertices,
                                    float viewport_width,
                                    float viewport_height,
                                    float right_x,
                                    float bottom,
                                    float pixel_size,
                                    std::uint8_t count) {
    if (count <= 1) {
        return;
    }

    const auto count_text = std::to_string(count);
    const auto text_width = measure_pixel_text(count_text, pixel_size);
    const auto text_x = right_x - text_width;

    append_pixel_text_bottom_left(
        vertices,
        viewport_width,
        viewport_height,
        text_x + pixel_size,
        bottom - pixel_size,
        pixel_size,
        count_text,
        {0.0F, 0.0F, 0.0F, 0.62F});
    append_pixel_text_bottom_left(
        vertices,
        viewport_width,
        viewport_height,
        text_x,
        bottom,
        pixel_size,
        count_text,
        {0.98F, 0.98F, 0.98F, 0.98F});
}

template <std::size_t RowCount>
void append_pixel_mask_bottom_left(std::vector<HudVertex>& vertices,
                                   float viewport_width,
                                   float viewport_height,
                                   float x,
                                   float bottom,
                                   float pixel_size,
                                   const std::array<std::uint8_t, RowCount>& rows,
                                   int columns,
                                   const std::array<float, 4>& color,
                                   int max_fill_columns = -1) {
    const auto mask_height = pixel_size * static_cast<float>(rows.size());
    const auto top_left_y = bottom_to_top_left_y(viewport_height, bottom, mask_height);
    for (std::size_t row = 0; row < rows.size(); ++row) {
        for (int column = 0; column < columns; ++column) {
            if (max_fill_columns >= 0 && column >= max_fill_columns) {
                continue;
            }

            const auto bit = static_cast<std::uint8_t>(1U << (columns - 1 - column));
            if ((rows[row] & bit) == 0U) {
                continue;
            }

            append_hud_rect_top_left(
                vertices,
                viewport_width,
                viewport_height,
                x + static_cast<float>(column) * pixel_size,
                top_left_y + static_cast<float>(row) * pixel_size,
                pixel_size,
                pixel_size,
                color);
        }
    }
}

void append_vital_glyph_bottom_left(std::vector<HudVertex>& vertices,
                                    float viewport_width,
                                    float viewport_height,
                                    float x,
                                    float bottom,
                                    float size,
                                    HudGlyphFill fill,
                                    const std::array<std::uint8_t, 8>& rows,
                                    const std::array<float, 4>& empty_color,
                                    const std::array<float, 4>& fill_color,
                                    const std::array<float, 4>& shine_color) {
    constexpr int kGlyphColumns = 8;
    const auto pixel_size = size / static_cast<float>(kGlyphColumns);
    const auto shadow_offset = std::max(1.0F, pixel_size * 0.55F);

    append_pixel_mask_bottom_left(
        vertices,
        viewport_width,
        viewport_height,
        x + shadow_offset,
        bottom - shadow_offset,
        pixel_size,
        rows,
        kGlyphColumns,
        {0.0F, 0.0F, 0.0F, 0.46F});
    append_pixel_mask_bottom_left(vertices, viewport_width, viewport_height, x, bottom, pixel_size, rows, kGlyphColumns, empty_color);

    const auto fill_columns = fill == HudGlyphFill::Full ? kGlyphColumns : (fill == HudGlyphFill::Half ? kGlyphColumns / 2 : 0);
    if (fill_columns > 0) {
        append_pixel_mask_bottom_left(vertices, viewport_width, viewport_height, x, bottom, pixel_size, rows, kGlyphColumns, fill_color, fill_columns);
        append_pixel_mask_bottom_left(
            vertices,
            viewport_width,
            viewport_height,
            x,
            bottom,
            pixel_size,
            rows,
            kGlyphColumns,
            shine_color,
            std::min(fill_columns, kGlyphColumns / 2));
    }
}

void append_heart_glyph_bottom_left(std::vector<HudVertex>& vertices,
                                    float viewport_width,
                                    float viewport_height,
                                    const VitalGlyphLayout& glyph) {
    constexpr std::array<std::uint8_t, 8> kHeartRows {
        0b01100110,
        0b11111111,
        0b11111111,
        0b11111111,
        0b01111110,
        0b00111100,
        0b00011000,
        0b00000000,
    };

    append_vital_glyph_bottom_left(
        vertices,
        viewport_width,
        viewport_height,
        glyph.x,
        glyph.bottom,
        glyph.size,
        glyph.fill,
        kHeartRows,
        {0.24F, 0.08F, 0.10F, 0.80F},
        {0.86F, 0.18F, 0.24F, 0.98F},
        {1.0F, 0.56F, 0.60F, 0.34F});
}

void append_bubble_glyph_bottom_left(std::vector<HudVertex>& vertices,
                                     float viewport_width,
                                     float viewport_height,
                                     const VitalGlyphLayout& glyph) {
    constexpr std::array<std::uint8_t, 8> kBubbleRows {
        0b00111100,
        0b01111110,
        0b11100111,
        0b11111111,
        0b11111111,
        0b01111110,
        0b00111100,
        0b00010000,
    };

    append_vital_glyph_bottom_left(
        vertices,
        viewport_width,
        viewport_height,
        glyph.x,
        glyph.bottom,
        glyph.size,
        glyph.fill,
        kBubbleRows,
        {0.08F, 0.17F, 0.26F, 0.74F},
        {0.42F, 0.80F, 0.98F, 0.96F},
        {0.92F, 0.98F, 1.0F, 0.28F});
}

auto item_stack_display_label(const HotbarSlot& slot) -> std::string {
    if (!inventory_slot_has_item(slot)) {
        return "MAINS VIDES";
    }

    std::string label(inventory_item_label(slot.block_id));
    if (slot.count > 1) {
        label += " X";
        label += std::to_string(slot.count);
    }
    return label;
}

} // namespace

Renderer::~Renderer() {
    shutdown();
}

auto Renderer::initialize(const RendererOptions& options) -> bool {
    if (initialized_ &&
        options_.shadows_enabled == options.shadows_enabled &&
        options_.shadow_map_size == options.shadow_map_size &&
        options_.post_process_enabled == options.post_process_enabled) {
        return true;
    }

    options_ = options;
    if (gl_api_ready_) {
        shutdown();
    }

    gl_api_ready_ = true;
    create_programs();
    create_atlas_texture();
    create_accent_texture();
    create_creature_atlas_texture();
    create_player_atlas_texture();
    create_shadow_map();
    create_creature_geometry();
    create_item_drop_geometry();
    create_hud_geometry();
    create_screen_quad_geometry();
    create_crosshair_geometry();
    initialized_ = true;
    return true;
}

void Renderer::shutdown() {
    if (gl_api_ready_) {
        for (auto& entry : gpu_meshes_) {
            destroy_gpu_mesh(entry.second);
        }

        destroy_post_process_targets();

        if (screen_quad_vao_ != 0) {
            glDeleteVertexArrays(1, &screen_quad_vao_);
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
        if (accent_texture_ != 0) {
            glDeleteTextures(1, &accent_texture_);
        }
        if (creature_atlas_texture_ != 0) {
            glDeleteTextures(1, &creature_atlas_texture_);
        }
        if (player_atlas_texture_ != 0) {
            glDeleteTextures(1, &player_atlas_texture_);
        }
        if (shadow_map_ != 0) {
            glDeleteTextures(1, &shadow_map_);
        }
        if (shadow_framebuffer_ != 0) {
            glDeleteFramebuffers(1, &shadow_framebuffer_);
        }
        if (creature_ebo_ != 0) {
            glDeleteBuffers(1, &creature_ebo_);
        }
        if (creature_vbo_ != 0) {
            glDeleteBuffers(1, &creature_vbo_);
        }
        if (creature_vao_ != 0) {
            glDeleteVertexArrays(1, &creature_vao_);
        }
        if (item_drop_vbo_ != 0) {
            glDeleteBuffers(1, &item_drop_vbo_);
        }
        if (item_drop_vao_ != 0) {
            glDeleteVertexArrays(1, &item_drop_vao_);
        }
        if (world_program_ != 0) {
            glDeleteProgram(world_program_);
        }
        if (creature_program_ != 0) {
            glDeleteProgram(creature_program_);
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
        if (sky_program_ != 0) {
            glDeleteProgram(sky_program_);
        }
        if (post_process_program_ != 0) {
            glDeleteProgram(post_process_program_);
        }
        if (glow_extract_program_ != 0) {
            glDeleteProgram(glow_extract_program_);
        }
        if (glow_blur_program_ != 0) {
            glDeleteProgram(glow_blur_program_);
        }
    }

    gpu_meshes_.clear();
    visible_chunks_cache_.clear();
    screen_quad_vao_ = 0;
    crosshair_vbo_ = 0;
    crosshair_vao_ = 0;
    hud_vbo_ = 0;
    hud_vao_ = 0;
    atlas_texture_ = 0;
    accent_texture_ = 0;
    creature_atlas_texture_ = 0;
    player_atlas_texture_ = 0;
    shadow_map_ = 0;
    shadow_framebuffer_ = 0;
    creature_vao_ = 0;
    creature_vbo_ = 0;
    creature_ebo_ = 0;
    item_drop_vao_ = 0;
    item_drop_vbo_ = 0;
    world_program_ = 0;
    creature_program_ = 0;
    shadow_program_ = 0;
    hud_program_ = 0;
    crosshair_program_ = 0;
    sky_program_ = 0;
    post_process_program_ = 0;
    glow_extract_program_ = 0;
    glow_blur_program_ = 0;
    world_uniforms_ = {};
    creature_uniforms_ = {};
    shadow_uniforms_ = {};
    hud_uniforms_ = {};
    sky_uniforms_ = {};
    post_process_uniforms_ = {};
    glow_extract_uniforms_ = {};
    glow_blur_uniforms_ = {};
    creature_vertex_buffer_bytes_ = 0;
    creature_index_buffer_bytes_ = 0;
    item_drop_vertex_buffer_bytes_ = 0;
    last_frame_stats_ = {};
    scene_target_width_ = 0;
    scene_target_height_ = 0;
    glow_target_width_ = 0;
    glow_target_height_ = 0;
    gl_api_ready_ = false;
    initialized_ = false;
}

void Renderer::render_frame(World& world,
                            const PlayerController& player,
                            const HotbarState& hotbar,
                            const InventoryMenuState& inventory_menu,
                            const DeathScreenState& death_screen,
                            const PauseMenuState& pause_menu,
                            std::span<const CreatureRenderInstance> creatures,
                            std::span<const ItemDropRenderInstance> item_drops,
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
        if (gpu_mesh.opaque_index_count == 0 && gpu_mesh.water_index_count == 0) {
            continue;
        }

        const auto bounds = make_chunk_bounds(coord);
        if (!should_render_chunk_in_camera_pass(
                bounds,
                frustum_planes,
                eye,
                forward,
                draw_distance_sq,
                kBackCullStartDistanceSq)) {
            continue;
        }

        visible_chunks.push_back({
            coord,
            &gpu_mesh,
            bounds.center,
            chunk_horizontal_distance_sq(bounds.center, eye),
        });
    }
    std::sort(visible_chunks.begin(), visible_chunks.end(), [](const VisibleChunk& lhs, const VisibleChunk& rhs) {
        return lhs.distance_squared < rhs.distance_squared;
    });
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
        const auto light_frustum_planes = extract_frustum_planes(light_view_projection);

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
        glUniform1i(shadow_uniforms_.atlas, 0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, atlas_texture_);

        const auto max_shadow_distance = kShadowDistance + static_cast<float>(kChunkSizeX);
        const auto max_shadow_distance_sq = max_shadow_distance * max_shadow_distance;
        for (const auto& [coord, gpu_mesh] : gpu_meshes_) {
            if (gpu_mesh.opaque_index_count == 0) {
                continue;
            }

            const auto bounds = make_chunk_bounds(coord);
            if (!should_render_chunk_in_shadow_pass(bounds, light_frustum_planes, focus, max_shadow_distance_sq)) {
                continue;
            }

            glBindVertexArray(gpu_mesh.vao);
            glDrawElements(GL_TRIANGLES, gpu_mesh.opaque_index_count, GL_UNSIGNED_INT, nullptr);
            ++frame_stats.shadow_chunks;
        }

        glDisable(GL_POLYGON_OFFSET_FILL);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glActiveTexture(GL_TEXTURE0);
        frame_stats.shadow_ms =
            std::chrono::duration<double, std::milli>(clock::now() - shadow_start).count();
    }

    const auto world_start = clock::now();
    const auto render_height = std::max(height, 1);
    const auto use_post_process = options_.post_process_enabled && width > 0 && height > 0;
    if (use_post_process) {
        ensure_post_process_targets(width, render_height);
        glBindFramebuffer(GL_FRAMEBUFFER, scene_framebuffer_);
    } else {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    glViewport(0, 0, width, render_height);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glClearColor(environment.sky_zenith_color.r, environment.sky_zenith_color.g, environment.sky_zenith_color.b, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    draw_sky(player, environment);

    glUseProgram(world_program_);
    glUniformMatrix4fv(world_uniforms_.view_projection, 1, GL_FALSE, glm::value_ptr(view_projection));
    glUniformMatrix4fv(world_uniforms_.light_view_projection, 1, GL_FALSE, glm::value_ptr(light_view_projection));
    glUniform3fv(world_uniforms_.camera_position, 1, glm::value_ptr(eye));
    glUniform3fv(world_uniforms_.sun_direction, 1, glm::value_ptr(environment.sun_direction));
    glUniform3fv(world_uniforms_.sun_color, 1, glm::value_ptr(environment.sun_color));
    glUniform3fv(world_uniforms_.ambient_color, 1, glm::value_ptr(environment.ambient_color));
    glUniform3fv(world_uniforms_.fog_color, 1, glm::value_ptr(environment.fog_color));
    glUniform3fv(world_uniforms_.distant_fog_color, 1, glm::value_ptr(environment.distant_fog_color));
    glUniform3fv(world_uniforms_.night_tint_color, 1, glm::value_ptr(environment.night_tint_color));
    glUniform1f(world_uniforms_.daylight_factor, environment.daylight_factor);
    glUniform1f(world_uniforms_.sun_visibility, sun_visible ? 1.0F : 0.0F);
    glUniform1f(world_uniforms_.time_of_day, environment.time_of_day);
    glUniform1i(world_uniforms_.atlas, 0);
    glUniform1i(world_uniforms_.shadow_map, 1);
    glUniform1i(world_uniforms_.shadows_enabled, options_.shadows_enabled ? 1 : 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, atlas_texture_);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, shadow_map_);

    for (const auto& visible_chunk : visible_chunks) {
        if (visible_chunk.mesh->opaque_index_count == 0) {
            continue;
        }
        glBindVertexArray(visible_chunk.mesh->vao);
        glDrawElements(GL_TRIANGLES, visible_chunk.mesh->opaque_index_count, GL_UNSIGNED_INT, nullptr);
        ++frame_stats.world_chunks;
    }

    draw_item_drops(item_drops);
    draw_creatures(creatures, view_projection, light_view_projection, eye, environment);
    draw_player_avatar(player, view_projection, light_view_projection, eye, environment);
    glUseProgram(world_program_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, atlas_texture_);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, shadow_map_);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    for (auto it = visible_chunks.rbegin(); it != visible_chunks.rend(); ++it) {
        if (it->mesh->water_index_count == 0) {
            continue;
        }
        glBindVertexArray(it->mesh->vao);
        glDrawElements(
            GL_TRIANGLES,
            it->mesh->water_index_count,
            GL_UNSIGNED_INT,
            reinterpret_cast<const void*>(static_cast<std::uintptr_t>(it->mesh->water_index_offset_bytes)));
    }
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);

    if (use_post_process) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        run_post_process(environment, width, render_height);
    } else {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    if (death_screen.visible) {
        draw_death_screen(death_screen, width, height);
    } else if (pause_menu.visible) {
        draw_pause_menu(pause_menu, width, height);
    } else if (inventory_menu.visible) {
        draw_inventory_menu(inventory_menu, hotbar, width, height);
    } else {
        draw_hotbar(player, hotbar, environment, width, height);
        draw_crosshair();
    }
    frame_stats.world_ms = std::chrono::duration<double, std::milli>(clock::now() - world_start).count();
    last_frame_stats_ = frame_stats;
}

auto Renderer::last_frame_stats() const noexcept -> const RendererFrameStats& {
    return last_frame_stats_;
}

void Renderer::sync_gpu_meshes(World& world, RendererFrameStats& frame_stats) {
    using clock = std::chrono::steady_clock;

    const auto deadline = clock::now() + std::chrono::duration<double, std::milli>(kMaxGpuMeshSyncMsPerFrame);
    std::size_t processed_events = 0;
    while (processed_events < kMaxGpuMeshEventsPerFrame && clock::now() < deadline) {
        const auto unloads = world.consume_pending_gpu_unloads(1);
        if (!unloads.empty()) {
            const auto iterator = gpu_meshes_.find(unloads.front());
            if (iterator != gpu_meshes_.end()) {
                destroy_gpu_mesh(iterator->second);
                gpu_meshes_.erase(iterator);
            }
            ++processed_events;
            continue;
        }

        const auto uploads = world.consume_pending_gpu_uploads(1);
        if (uploads.empty()) {
            break;
        }

        const auto& records = world.chunk_records();
        const auto record_iterator = records.find(uploads.front());
        if (record_iterator == records.end() || record_iterator->second.mesh_revision == 0) {
            ++processed_events;
            continue;
        }

        upload_mesh(record_iterator->first, record_iterator->second.mesh, record_iterator->second.mesh_revision);
        ++frame_stats.uploaded_meshes;
        ++processed_events;
    }
}

void Renderer::upload_mesh(const ChunkCoord& coord, const ChunkMeshData& mesh, std::uint64_t revision) {
    auto& gpu_mesh = gpu_meshes_[coord];
    gpu_mesh.revision = revision;
    gpu_mesh.opaque_index_count = 0;
    gpu_mesh.water_index_count = 0;
    gpu_mesh.water_index_offset_bytes = 0;

    if (mesh.total_index_count() == 0 || mesh.total_vertex_count() == 0) {
        return;
    }

    std::vector<ChunkVertex> combined_vertices;
    combined_vertices.reserve(mesh.total_vertex_count());
    combined_vertices.insert(combined_vertices.end(), mesh.vertices.begin(), mesh.vertices.end());
    combined_vertices.insert(combined_vertices.end(), mesh.water_vertices.begin(), mesh.water_vertices.end());

    std::vector<std::uint32_t> combined_indices;
    combined_indices.reserve(mesh.total_index_count());
    combined_indices.insert(combined_indices.end(), mesh.indices.begin(), mesh.indices.end());
    const auto water_vertex_offset = static_cast<std::uint32_t>(mesh.vertices.size());
    for (const auto index : mesh.water_indices) {
        combined_indices.push_back(index + water_vertex_offset);
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
        glEnableVertexAttribArray(7);
        glVertexAttribPointer(7, 1, GL_FLOAT, GL_FALSE, sizeof(ChunkVertex), reinterpret_cast<void*>(offsetof(ChunkVertex, material_class)));
    } else {
        glBindVertexArray(gpu_mesh.vao);
        glBindBuffer(GL_ARRAY_BUFFER, gpu_mesh.vbo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gpu_mesh.ebo);
    }

    const auto vertex_bytes = static_cast<GLsizeiptr>(combined_vertices.size() * sizeof(ChunkVertex));
    const auto index_bytes = static_cast<GLsizeiptr>(combined_indices.size() * sizeof(std::uint32_t));

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

    glBufferSubData(GL_ARRAY_BUFFER, 0, vertex_bytes, combined_vertices.data());
    glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, index_bytes, combined_indices.data());

    gpu_mesh.opaque_index_count = static_cast<GLsizei>(mesh.indices.size());
    gpu_mesh.water_index_count = static_cast<GLsizei>(mesh.water_indices.size());
    gpu_mesh.water_index_offset_bytes = static_cast<GLsizeiptr>(mesh.indices.size() * sizeof(std::uint32_t));
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
    mesh.opaque_index_count = 0;
    mesh.water_index_count = 0;
    mesh.water_index_offset_bytes = 0;
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
layout(location = 7) in float a_material_class;

uniform mat4 u_view_projection;
uniform mat4 u_light_view_projection;
uniform vec3 u_camera_position;

out vec2 v_uv;
out vec3 v_normal;
out float v_face_shade;
out float v_ao;
out float v_sky_light;
out float v_block_light;
out float v_material_class;
out float v_distance;
out vec3 v_world_position;
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
    v_material_class = a_material_class;
    v_distance = distance(world_position.xyz, u_camera_position);
    v_world_position = world_position.xyz;
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
in float v_material_class;
in float v_distance;
in vec3 v_world_position;
in vec4 v_light_position;

uniform sampler2D u_atlas;
uniform sampler2D u_shadow_map;
uniform vec3 u_camera_position;
uniform vec3 u_sun_direction;
uniform vec3 u_sun_color;
uniform vec3 u_ambient_color;
uniform vec3 u_fog_color;
uniform vec3 u_distant_fog_color;
uniform vec3 u_night_tint_color;
uniform float u_daylight_factor;
uniform float u_sun_visibility;
uniform float u_time_of_day;
uniform int u_shadows_enabled;

out vec4 frag_color;

float material_mask(float material, float expected) {
    return 1.0 - step(0.25, abs(material - expected));
}

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
    float visibility = 0.0;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            float sampled_depth = texture(u_shadow_map, projected.xy + vec2(x, y) * texel_size).r;
            visibility += (projected.z - bias) <= sampled_depth ? 1.0 : 0.0;
        }
    }
    return visibility / 9.0;
}

void main() {
    float water_mask = material_mask(v_material_class, 6.0);
    vec2 animated_uv = v_uv;
    if (water_mask > 0.5) {
        float wave_a = sin(v_world_position.x * 0.33 + v_world_position.z * 0.19 + u_time_of_day * 2.2);
        float wave_b = cos(v_world_position.z * 0.41 - v_world_position.x * 0.21 + u_time_of_day * 1.7);
        animated_uv += vec2(wave_a, wave_b) * 0.0038;
    }

    vec4 sampled = texture(u_atlas, animated_uv);
    if (sampled.a < 0.1) {
        discard;
    }

    vec3 albedo = sampled.rgb;
    vec3 normal = normalize(v_normal);
    vec3 view_direction = normalize(u_camera_position - v_world_position);
    vec3 sun_direction = normalize(u_sun_direction);
    float daylight = clamp(u_daylight_factor, 0.0, 1.0);
    float sky_light = clamp(v_sky_light, 0.0, 1.0);
    float block_light = clamp(v_block_light, 0.0, 1.0);
    float shadow = sample_shadow(normal);

    float rock_mask = material_mask(v_material_class, 1.0);
    float sand_mask = material_mask(v_material_class, 2.0);
    float wood_mask = material_mask(v_material_class, 3.0);
    float foliage_mask = material_mask(v_material_class, 4.0);
    float flora_mask = material_mask(v_material_class, 5.0);
    float emissive_mask = material_mask(v_material_class, 7.0);
    float snow_mask = material_mask(v_material_class, 8.0);

    float face_light = mix(0.82, 1.10, clamp(v_face_shade, 0.0, 1.0)) * mix(0.78, 1.00, clamp(v_ao, 0.0, 1.0));
    vec3 ambient = u_ambient_color * mix(0.40, 1.12, sky_light);
    ambient *= mix(0.88, 1.08, smoothstep(-0.25, 1.0, normal.y));

    float direct = max(dot(normal, sun_direction), 0.0);
    direct = mix(direct, direct * direct, 0.45);
    vec3 sunlight = u_sun_color * direct * shadow * u_sun_visibility * daylight * (0.72 + 0.28 * sky_light);

    float bounce_factor = smoothstep(-0.35, 1.0, normal.y) * sky_light;
    vec3 bounce_light = mix(u_fog_color, u_distant_fog_color, 0.42) * bounce_factor * (0.12 + 0.12 * daylight);
    vec3 torch_light = vec3(1.14, 0.70, 0.32) * block_light * (1.18 + emissive_mask * 0.55);

    float rim = pow(1.0 - max(dot(view_direction, normal), 0.0), mix(3.0, 1.7, water_mask + foliage_mask * 0.35 + flora_mask * 0.45));
    vec3 rim_color = mix(u_fog_color, u_sun_color, 0.55) * rim * (0.02 + 0.08 * daylight + 0.04 * foliage_mask + 0.05 * flora_mask);

    vec3 reflected = reflect(-sun_direction, normal);
    float specular_power = mix(11.0, 34.0, rock_mask + snow_mask * 0.3 + sand_mask * 0.1);
    specular_power = mix(specular_power, 18.0, wood_mask);
    float specular = pow(max(dot(reflected, view_direction), 0.0), specular_power);
    vec3 specular_color = u_sun_color * specular * shadow * (0.12 * rock_mask + 0.08 * wood_mask + 0.05 * snow_mask);

    vec3 material_tint = vec3(1.0);
    material_tint = mix(material_tint, vec3(1.03, 0.99, 0.92), sand_mask);
    material_tint = mix(material_tint, vec3(0.94, 0.98, 1.06), snow_mask);
    material_tint = mix(material_tint, vec3(0.96, 1.03, 0.97), foliage_mask * 0.65 + flora_mask * 0.45);
    material_tint = mix(material_tint, vec3(1.02, 0.98, 0.94), wood_mask * 0.45);

    vec3 lit_color = albedo * material_tint * face_light * (ambient + bounce_light + sunlight + torch_light);
    lit_color += rim_color + specular_color;
    lit_color += u_night_tint_color * (0.05 + 0.05 * sky_light) * (1.0 - daylight);

    float output_alpha = 1.0;
    if (water_mask > 0.5) {
        float fresnel = pow(1.0 - max(dot(view_direction, normal), 0.0), 4.0);
        float sparkle = pow(max(dot(reflected, view_direction), 0.0), 56.0);
        float shimmer = 0.5 + 0.5 * sin(v_world_position.x * 0.27 + v_world_position.z * 0.31 + u_time_of_day * 2.0);
        vec3 water_tint = mix(vec3(0.05, 0.20, 0.30), vec3(0.16, 0.60, 0.72), clamp(daylight * 0.72 + sky_light * 0.28, 0.0, 1.0));
        vec3 water_light = ambient * 0.76 + bounce_light * 0.90 + sunlight * 0.48 + torch_light * 0.24;
        vec3 water_specular =
            mix(u_distant_fog_color, u_sun_color, 0.52) * (fresnel * (0.14 + 0.12 * daylight) + sparkle * (0.16 + 0.12 * daylight));
        lit_color = mix(albedo, water_tint, 0.74) * water_light;
        lit_color += water_tint * shimmer * (0.04 + 0.04 * daylight);
        lit_color += water_specular;
        output_alpha = sampled.a;
    }

    lit_color += vec3(1.24, 0.68, 0.24) * emissive_mask * (0.32 + 0.90 * block_light);

    float fog = clamp(v_distance / 170.0, 0.0, 1.0);
    fog = fog * fog;
    float ground_haze = clamp((32.0 - v_world_position.y) / 28.0, 0.0, 1.0);
    ground_haze *= clamp(v_distance / 84.0, 0.0, 1.0) * (0.14 + 0.22 * (1.0 - daylight));
    fog = clamp(fog + ground_haze, 0.0, 1.0);
    vec3 fog_color = mix(u_fog_color, u_distant_fog_color, sqrt(fog));
    frag_color = vec4(mix(lit_color, fog_color, fog), output_alpha);
}
)";

    static constexpr auto* creature_vertex_shader = R"(#version 330 core
layout(location = 0) in vec3 a_position;
layout(location = 1) in vec2 a_uv;
layout(location = 2) in vec3 a_normal;
layout(location = 3) in float a_nightmare_factor;
layout(location = 4) in float a_tension;
layout(location = 5) in float a_material_class;
layout(location = 6) in float a_cavity_mask;
layout(location = 7) in float a_emissive_strength;

uniform mat4 u_view_projection;
uniform mat4 u_light_view_projection;
uniform vec3 u_camera_position;

out vec2 v_uv;
out vec3 v_normal;
out vec3 v_world_position;
out float v_distance;
out float v_nightmare_factor;
out float v_tension;
out float v_material_class;
out float v_cavity_mask;
out float v_emissive_strength;
out vec4 v_light_position;

void main() {
    vec4 world_position = vec4(a_position, 1.0);
    gl_Position = u_view_projection * world_position;
    v_uv = a_uv;
    v_normal = a_normal;
    v_world_position = world_position.xyz;
    v_distance = distance(world_position.xyz, u_camera_position);
    v_nightmare_factor = a_nightmare_factor;
    v_tension = a_tension;
    v_material_class = a_material_class;
    v_cavity_mask = a_cavity_mask;
    v_emissive_strength = a_emissive_strength;
    v_light_position = u_light_view_projection * world_position;
}
)";

    static constexpr auto* creature_fragment_shader = R"(#version 330 core
in vec2 v_uv;
in vec3 v_normal;
in vec3 v_world_position;
in float v_distance;
in float v_nightmare_factor;
in float v_tension;
in float v_material_class;
in float v_cavity_mask;
in float v_emissive_strength;
in vec4 v_light_position;

uniform sampler2D u_atlas;
uniform sampler2D u_shadow_map;
uniform vec3 u_camera_position;
uniform vec3 u_sun_direction;
uniform vec3 u_sun_color;
uniform vec3 u_ambient_color;
uniform vec3 u_fog_color;
uniform vec3 u_distant_fog_color;
uniform vec3 u_night_tint_color;
uniform float u_daylight_factor;
uniform float u_sun_visibility;
uniform float u_time_of_day;
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
    float visibility = 0.0;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            float sampled_depth = texture(u_shadow_map, projected.xy + vec2(x, y) * texel_size).r;
            visibility += (projected.z - bias) <= sampled_depth ? 1.0 : 0.0;
        }
    }
    return visibility / 9.0;
}

void main() {
    vec4 sampled = texture(u_atlas, v_uv);
    vec3 albedo = sampled.rgb;
    float emissive_mask = sampled.a;
    vec3 normal = normalize(v_normal);
    vec3 view_direction = normalize(u_camera_position - v_world_position);
    vec3 sun_direction = normalize(u_sun_direction);

    float shadow = sample_shadow(normal);
    float sky_mix = clamp(u_daylight_factor, 0.0, 1.0);
    float cavity = clamp(v_cavity_mask, 0.0, 1.0);
    float hard_material = smoothstep(0.44, 0.90, v_material_class);
    float soft_fiber = 1.0 - smoothstep(0.28, 0.58, v_material_class);
    float thin_surface = 1.0 - smoothstep(0.22, 0.50, v_material_class);

    float cavity_occlusion = mix(1.0, 0.54, cavity * (0.62 + 0.14 * v_nightmare_factor));
    float ambient_strength = mix(0.36, 1.02, sky_mix) * mix(1.08, 0.84, hard_material) * cavity_occlusion;
    vec3 ambient = u_ambient_color * ambient_strength;

    float wrap = mix(0.34, 0.10, hard_material);
    float sun_wrap = clamp((dot(normal, sun_direction) + wrap) / (1.0 + wrap), 0.0, 1.0);
    vec3 sunlight = u_sun_color * (sun_wrap * sky_mix * shadow * u_sun_visibility);

    float backlight = pow(max(dot(normal, -sun_direction), 0.0), 1.8);
    vec3 translucency = u_sun_color * backlight * thin_surface * sky_mix * u_sun_visibility * (0.04 + 0.10 * soft_fiber);

    float rim = pow(1.0 - max(dot(view_direction, normal), 0.0), 2.45);
    vec3 rim_light = mix(vec3(0.12, 0.10, 0.08), vec3(0.34, 0.50, 0.60), 1.0 - sky_mix);
    rim_light *= rim * mix(0.05, 0.11, 1.0 - hard_material) * mix(0.75, 1.0, v_nightmare_factor);

    vec3 reflected = reflect(-sun_direction, normal);
    float specular = pow(max(dot(reflected, view_direction), 0.0), mix(42.0, 16.0, hard_material));
    float hard_specular = specular * smoothstep(0.52, 0.90, v_material_class);
    vec3 specular_color = u_sun_color * hard_specular * shadow * sky_mix * u_sun_visibility * (0.03 + 0.18 * v_nightmare_factor);

    float pulse = 0.84 + 0.16 * sin(u_time_of_day * 1.7 + v_tension * 7.0 + v_world_position.y * 2.2);
    vec3 nightmare_glow =
        vec3(1.00, 0.18, 0.12) * emissive_mask * v_emissive_strength * v_nightmare_factor * (0.24 + v_tension * 0.30) * pulse;

    vec3 lit_color = albedo * (ambient + sunlight + translucency);
    lit_color *= cavity_occlusion;
    lit_color += rim_light + specular_color;
    lit_color += u_night_tint_color * (0.07 + 0.07 * v_nightmare_factor) * (1.0 - sky_mix);
    float fog = clamp(v_distance / 160.0, 0.0, 1.0);
    fog = fog * fog;
    vec3 fog_color = mix(u_fog_color, u_distant_fog_color, sqrt(fog));
    vec3 fogged_color = mix(lit_color, fog_color, fog);
    vec3 fogged_glow = nightmare_glow * (1.0 - fog * 0.72);
    frag_color = vec4(fogged_color + fogged_glow, 1.0);
}
)";

    static constexpr auto* shadow_vertex_shader = R"(#version 330 core
layout(location = 0) in vec3 a_position;
layout(location = 1) in vec2 a_uv;

uniform mat4 u_light_view_projection;

out vec2 v_uv;

void main() {
    gl_Position = u_light_view_projection * vec4(a_position, 1.0);
    v_uv = a_uv;
}
)";

    static constexpr auto* shadow_fragment_shader = R"(#version 330 core
in vec2 v_uv;

uniform sampler2D u_atlas;

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

    static constexpr auto* screen_vertex_shader = R"(#version 330 core
out vec2 v_uv;

void main() {
    vec2 positions[3] = vec2[](vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0));
    vec2 clip = positions[gl_VertexID];
    gl_Position = vec4(clip, 0.0, 1.0);
    v_uv = clip * 0.5 + 0.5;
}
)";

    static constexpr auto* sky_fragment_shader = R"(#version 330 core
in vec2 v_uv;

uniform mat4 u_inverse_view_projection;
uniform vec3 u_sun_direction;
uniform float u_daylight_factor;
uniform float u_time_of_day;
uniform vec3 u_sky_zenith_color;
uniform vec3 u_sky_horizon_color;
uniform vec3 u_horizon_glow_color;
uniform vec3 u_sun_disk_color;
uniform vec3 u_moon_disk_color;
uniform float u_star_intensity;
uniform float u_cloud_intensity;
uniform sampler2D u_accent_atlas;

out vec4 frag_color;

const float kPi = 3.14159265358979323846;

vec2 project_to_billboard(vec3 direction, vec3 center_direction, float angular_scale) {
    vec3 center = normalize(center_direction);
    vec3 helper = abs(center.y) > 0.95 ? vec3(0.0, 0.0, 1.0) : vec3(0.0, 1.0, 0.0);
    vec3 right = normalize(cross(helper, center));
    vec3 up = normalize(cross(center, right));
    float depth = max(dot(direction, center), 0.0001);
    vec2 plane = vec2(dot(direction, right), dot(direction, up)) / depth;
    return plane / angular_scale * 0.5 + 0.5;
}

vec2 tile_uv(vec2 tile, vec2 local_uv) {
    float step_uv = 0.25;
    vec2 padding = vec2(0.012);
    return (tile + mix(padding, vec2(1.0) - padding, clamp(local_uv, 0.0, 1.0))) * step_uv;
}

float hash12(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123);
}

void main() {
    vec4 far_point = u_inverse_view_projection * vec4(v_uv * 2.0 - 1.0, 1.0, 1.0);
    vec3 direction = normalize(far_point.xyz / max(far_point.w, 0.0001));
    float up = clamp(direction.y * 0.5 + 0.5, 0.0, 1.0);
    float horizon_band = exp(-abs(direction.y) * 8.0);
    vec3 color = mix(u_sky_horizon_color, u_sky_zenith_color, pow(up, 0.62));
    color += u_horizon_glow_color * horizon_band * (0.12 + 0.32 * (1.0 - clamp(u_daylight_factor, 0.0, 1.0)));

    vec2 sun_uv = project_to_billboard(direction, normalize(u_sun_direction), 0.08);
    vec4 sun_sprite = texture(u_accent_atlas, tile_uv(vec2(0.0, 0.0), sun_uv));
    vec4 ring_sprite = texture(u_accent_atlas, tile_uv(vec2(0.0, 1.0), project_to_billboard(direction, normalize(u_sun_direction), 0.15)));
    color += u_sun_disk_color * sun_sprite.a * (0.85 + 0.25 * clamp(u_daylight_factor, 0.0, 1.0));
    color += u_horizon_glow_color * ring_sprite.a * (0.08 + 0.12 * clamp(u_daylight_factor, 0.0, 1.0));

    vec2 moon_uv = project_to_billboard(direction, -normalize(u_sun_direction), 0.072);
    vec4 moon_sprite = texture(u_accent_atlas, tile_uv(vec2(1.0, 0.0), moon_uv));
    color += u_moon_disk_color * moon_sprite.a * (0.20 + 0.80 * u_star_intensity);

    vec2 cloud_flow = vec2(u_time_of_day * 0.0035, u_time_of_day * 0.0018);
    vec2 cloud_a = fract(direction.xz * 1.55 + cloud_flow);
    vec2 cloud_b = fract(direction.zx * 1.10 - cloud_flow * 0.7);
    float cloud_mask =
        texture(u_accent_atlas, tile_uv(vec2(3.0, 0.0), cloud_a)).a * 0.65 +
        texture(u_accent_atlas, tile_uv(vec2(3.0, 0.0), cloud_b)).a * 0.45;
    cloud_mask *= smoothstep(-0.22, 0.32, direction.y) * (1.0 - smoothstep(0.34, 0.82, direction.y));
    color = mix(color, color + vec3(0.18, 0.16, 0.14), cloud_mask * u_cloud_intensity * 0.22);

    vec2 star_grid = vec2(atan(direction.z, direction.x) / (2.0 * kPi) + 0.5, asin(clamp(direction.y, -1.0, 1.0)) / kPi + 0.5);
    vec2 star_cell = floor(star_grid * vec2(38.0, 18.0));
    vec2 star_local = fract(star_grid * vec2(38.0, 18.0));
    float star_seed = hash12(star_cell);
    float star_gate = step(0.92, star_seed);
    vec4 star_sprite = texture(u_accent_atlas, tile_uv(vec2(2.0, 0.0), star_local));
    float twinkle = 0.65 + 0.35 * sin(u_time_of_day * 6.0 + star_seed * 20.0);
    color += vec3(0.78, 0.90, 1.00) * star_sprite.a * star_gate * twinkle * u_star_intensity;

    frag_color = vec4(color, 1.0);
}
)";

    static constexpr auto* glow_extract_fragment_shader = R"(#version 330 core
in vec2 v_uv;

uniform sampler2D u_scene_texture;
uniform float u_threshold;

out vec4 frag_color;

void main() {
    vec3 color = texture(u_scene_texture, v_uv).rgb;
    float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));
    float bloom = max(luminance - u_threshold, 0.0);
    frag_color = vec4(color * bloom / max(luminance, 0.0001), 1.0);
}
)";

    static constexpr auto* glow_blur_fragment_shader = R"(#version 330 core
in vec2 v_uv;

uniform sampler2D u_source_texture;
uniform vec2 u_texel_direction;

out vec4 frag_color;

void main() {
    vec3 color = texture(u_source_texture, v_uv).rgb * 0.227027;
    color += texture(u_source_texture, v_uv + u_texel_direction * 1.384615).rgb * 0.316216;
    color += texture(u_source_texture, v_uv - u_texel_direction * 1.384615).rgb * 0.316216;
    color += texture(u_source_texture, v_uv + u_texel_direction * 3.230769).rgb * 0.070270;
    color += texture(u_source_texture, v_uv - u_texel_direction * 3.230769).rgb * 0.070270;
    frag_color = vec4(color, 1.0);
}
)";

    static constexpr auto* post_process_fragment_shader = R"(#version 330 core
in vec2 v_uv;

uniform sampler2D u_scene_texture;
uniform sampler2D u_glow_texture;
uniform float u_exposure;
uniform float u_saturation_boost;
uniform float u_contrast;
uniform float u_vignette_strength;
uniform vec3 u_night_tint_color;
uniform float u_glow_strength;

out vec4 frag_color;

vec3 apply_saturation(vec3 color, float saturation) {
    float luma = dot(color, vec3(0.2126, 0.7152, 0.0722));
    return mix(vec3(luma), color, saturation);
}

void main() {
    vec3 scene = texture(u_scene_texture, v_uv).rgb;
    vec3 glow = texture(u_glow_texture, v_uv).rgb * u_glow_strength;
    vec3 color = scene + glow;
    color = vec3(1.0) - exp(-color * max(u_exposure, 0.001));
    color = apply_saturation(color, u_saturation_boost);
    color = (color - 0.5) * u_contrast + 0.5;
    float vignette = smoothstep(0.92, 0.22, distance(v_uv, vec2(0.5)));
    color *= mix(1.0 - u_vignette_strength, 1.0, vignette);
    color = mix(color, color + u_night_tint_color * 0.28, clamp(length(u_night_tint_color) * 2.0, 0.0, 1.0));
    color = pow(clamp(color, 0.0, 16.0), vec3(1.0 / 2.2));
    frag_color = vec4(clamp(color, 0.0, 1.0), 1.0);
}
)";

    world_program_ = link_program(
        compile_shader(GL_VERTEX_SHADER, world_vertex_shader),
        compile_shader(GL_FRAGMENT_SHADER, world_fragment_shader));
    creature_program_ = link_program(
        compile_shader(GL_VERTEX_SHADER, creature_vertex_shader),
        compile_shader(GL_FRAGMENT_SHADER, creature_fragment_shader));
    shadow_program_ = link_program(
        compile_shader(GL_VERTEX_SHADER, shadow_vertex_shader),
        compile_shader(GL_FRAGMENT_SHADER, shadow_fragment_shader));
    hud_program_ = link_program(
        compile_shader(GL_VERTEX_SHADER, hud_vertex_shader),
        compile_shader(GL_FRAGMENT_SHADER, hud_fragment_shader));
    crosshair_program_ = link_program(
        compile_shader(GL_VERTEX_SHADER, crosshair_vertex_shader),
        compile_shader(GL_FRAGMENT_SHADER, crosshair_fragment_shader));
    sky_program_ = link_program(
        compile_shader(GL_VERTEX_SHADER, screen_vertex_shader),
        compile_shader(GL_FRAGMENT_SHADER, sky_fragment_shader));
    glow_extract_program_ = link_program(
        compile_shader(GL_VERTEX_SHADER, screen_vertex_shader),
        compile_shader(GL_FRAGMENT_SHADER, glow_extract_fragment_shader));
    glow_blur_program_ = link_program(
        compile_shader(GL_VERTEX_SHADER, screen_vertex_shader),
        compile_shader(GL_FRAGMENT_SHADER, glow_blur_fragment_shader));
    post_process_program_ = link_program(
        compile_shader(GL_VERTEX_SHADER, screen_vertex_shader),
        compile_shader(GL_FRAGMENT_SHADER, post_process_fragment_shader));

    world_uniforms_.view_projection = glGetUniformLocation(world_program_, "u_view_projection");
    world_uniforms_.light_view_projection = glGetUniformLocation(world_program_, "u_light_view_projection");
    world_uniforms_.camera_position = glGetUniformLocation(world_program_, "u_camera_position");
    world_uniforms_.sun_direction = glGetUniformLocation(world_program_, "u_sun_direction");
    world_uniforms_.sun_color = glGetUniformLocation(world_program_, "u_sun_color");
    world_uniforms_.ambient_color = glGetUniformLocation(world_program_, "u_ambient_color");
    world_uniforms_.fog_color = glGetUniformLocation(world_program_, "u_fog_color");
    world_uniforms_.distant_fog_color = glGetUniformLocation(world_program_, "u_distant_fog_color");
    world_uniforms_.night_tint_color = glGetUniformLocation(world_program_, "u_night_tint_color");
    world_uniforms_.daylight_factor = glGetUniformLocation(world_program_, "u_daylight_factor");
    world_uniforms_.sun_visibility = glGetUniformLocation(world_program_, "u_sun_visibility");
    world_uniforms_.time_of_day = glGetUniformLocation(world_program_, "u_time_of_day");
    world_uniforms_.atlas = glGetUniformLocation(world_program_, "u_atlas");
    world_uniforms_.shadow_map = glGetUniformLocation(world_program_, "u_shadow_map");
    world_uniforms_.shadows_enabled = glGetUniformLocation(world_program_, "u_shadows_enabled");

    creature_uniforms_.view_projection = glGetUniformLocation(creature_program_, "u_view_projection");
    creature_uniforms_.light_view_projection = glGetUniformLocation(creature_program_, "u_light_view_projection");
    creature_uniforms_.camera_position = glGetUniformLocation(creature_program_, "u_camera_position");
    creature_uniforms_.sun_direction = glGetUniformLocation(creature_program_, "u_sun_direction");
    creature_uniforms_.sun_color = glGetUniformLocation(creature_program_, "u_sun_color");
    creature_uniforms_.ambient_color = glGetUniformLocation(creature_program_, "u_ambient_color");
    creature_uniforms_.fog_color = glGetUniformLocation(creature_program_, "u_fog_color");
    creature_uniforms_.distant_fog_color = glGetUniformLocation(creature_program_, "u_distant_fog_color");
    creature_uniforms_.night_tint_color = glGetUniformLocation(creature_program_, "u_night_tint_color");
    creature_uniforms_.daylight_factor = glGetUniformLocation(creature_program_, "u_daylight_factor");
    creature_uniforms_.sun_visibility = glGetUniformLocation(creature_program_, "u_sun_visibility");
    creature_uniforms_.atlas = glGetUniformLocation(creature_program_, "u_atlas");
    creature_uniforms_.shadow_map = glGetUniformLocation(creature_program_, "u_shadow_map");
    creature_uniforms_.shadows_enabled = glGetUniformLocation(creature_program_, "u_shadows_enabled");
    creature_uniforms_.time_of_day = glGetUniformLocation(creature_program_, "u_time_of_day");

    shadow_uniforms_.light_view_projection = glGetUniformLocation(shadow_program_, "u_light_view_projection");
    shadow_uniforms_.atlas = glGetUniformLocation(shadow_program_, "u_atlas");
    hud_uniforms_.atlas = glGetUniformLocation(hud_program_, "u_atlas");
    sky_uniforms_.inverse_view_projection = glGetUniformLocation(sky_program_, "u_inverse_view_projection");
    sky_uniforms_.sun_direction = glGetUniformLocation(sky_program_, "u_sun_direction");
    sky_uniforms_.daylight_factor = glGetUniformLocation(sky_program_, "u_daylight_factor");
    sky_uniforms_.time_of_day = glGetUniformLocation(sky_program_, "u_time_of_day");
    sky_uniforms_.sky_zenith_color = glGetUniformLocation(sky_program_, "u_sky_zenith_color");
    sky_uniforms_.sky_horizon_color = glGetUniformLocation(sky_program_, "u_sky_horizon_color");
    sky_uniforms_.horizon_glow_color = glGetUniformLocation(sky_program_, "u_horizon_glow_color");
    sky_uniforms_.sun_disk_color = glGetUniformLocation(sky_program_, "u_sun_disk_color");
    sky_uniforms_.moon_disk_color = glGetUniformLocation(sky_program_, "u_moon_disk_color");
    sky_uniforms_.star_intensity = glGetUniformLocation(sky_program_, "u_star_intensity");
    sky_uniforms_.cloud_intensity = glGetUniformLocation(sky_program_, "u_cloud_intensity");
    sky_uniforms_.accent_atlas = glGetUniformLocation(sky_program_, "u_accent_atlas");
    glow_extract_uniforms_.scene_texture = glGetUniformLocation(glow_extract_program_, "u_scene_texture");
    glow_extract_uniforms_.threshold = glGetUniformLocation(glow_extract_program_, "u_threshold");
    glow_blur_uniforms_.source_texture = glGetUniformLocation(glow_blur_program_, "u_source_texture");
    glow_blur_uniforms_.texel_direction = glGetUniformLocation(glow_blur_program_, "u_texel_direction");
    post_process_uniforms_.scene_texture = glGetUniformLocation(post_process_program_, "u_scene_texture");
    post_process_uniforms_.glow_texture = glGetUniformLocation(post_process_program_, "u_glow_texture");
    post_process_uniforms_.exposure = glGetUniformLocation(post_process_program_, "u_exposure");
    post_process_uniforms_.saturation_boost = glGetUniformLocation(post_process_program_, "u_saturation_boost");
    post_process_uniforms_.contrast = glGetUniformLocation(post_process_program_, "u_contrast");
    post_process_uniforms_.vignette_strength = glGetUniformLocation(post_process_program_, "u_vignette_strength");
    post_process_uniforms_.night_tint_color = glGetUniformLocation(post_process_program_, "u_night_tint_color");
    post_process_uniforms_.glow_strength = glGetUniformLocation(post_process_program_, "u_glow_strength");
}

void Renderer::create_atlas_texture() {
    const auto pixels = build_block_atlas_pixels();

    glGenTextures(1, &atlas_texture_);
    glBindTexture(GL_TEXTURE_2D, atlas_texture_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kBlockAtlasSize, kBlockAtlasSize, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

void Renderer::create_accent_texture() {
    const auto pixels = build_accent_atlas_pixels();

    glGenTextures(1, &accent_texture_);
    glBindTexture(GL_TEXTURE_2D, accent_texture_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kAccentAtlasSize, kAccentAtlasSize, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

void Renderer::create_creature_atlas_texture() {
    const auto pixels = build_creature_atlas_pixels();

    glGenTextures(1, &creature_atlas_texture_);
    glBindTexture(GL_TEXTURE_2D, creature_atlas_texture_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA8,
        kCreatureAtlasSize,
        kCreatureAtlasSize,
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        pixels.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

void Renderer::create_player_atlas_texture() {
    const auto pixels = build_player_atlas_pixels();

    glGenTextures(1, &player_atlas_texture_);
    glBindTexture(GL_TEXTURE_2D, player_atlas_texture_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA8,
        kPlayerAtlasSize,
        kPlayerAtlasSize,
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        pixels.data());
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

void Renderer::create_creature_geometry() {
    glGenVertexArrays(1, &creature_vao_);
    glGenBuffers(1, &creature_vbo_);
    glGenBuffers(1, &creature_ebo_);
    glBindVertexArray(creature_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, creature_vbo_);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, creature_ebo_);
    glBufferData(GL_ARRAY_BUFFER, kInitialCreatureVertexBufferBytes, nullptr, GL_DYNAMIC_DRAW);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, kInitialCreatureIndexBufferBytes, nullptr, GL_DYNAMIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(CreatureVertex), reinterpret_cast<void*>(offsetof(CreatureVertex, x)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(CreatureVertex), reinterpret_cast<void*>(offsetof(CreatureVertex, u)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(CreatureVertex), reinterpret_cast<void*>(offsetof(CreatureVertex, nx)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(CreatureVertex), reinterpret_cast<void*>(offsetof(CreatureVertex, nightmare_factor)));
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof(CreatureVertex), reinterpret_cast<void*>(offsetof(CreatureVertex, tension)));
    glEnableVertexAttribArray(5);
    glVertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE, sizeof(CreatureVertex), reinterpret_cast<void*>(offsetof(CreatureVertex, material_class)));
    glEnableVertexAttribArray(6);
    glVertexAttribPointer(6, 1, GL_FLOAT, GL_FALSE, sizeof(CreatureVertex), reinterpret_cast<void*>(offsetof(CreatureVertex, cavity_mask)));
    glEnableVertexAttribArray(7);
    glVertexAttribPointer(7, 1, GL_FLOAT, GL_FALSE, sizeof(CreatureVertex), reinterpret_cast<void*>(offsetof(CreatureVertex, emissive_strength)));

    creature_vertex_buffer_bytes_ = kInitialCreatureVertexBufferBytes;
    creature_index_buffer_bytes_ = kInitialCreatureIndexBufferBytes;
}

void Renderer::create_item_drop_geometry() {
    glGenVertexArrays(1, &item_drop_vao_);
    glGenBuffers(1, &item_drop_vbo_);
    glBindVertexArray(item_drop_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, item_drop_vbo_);
    glBufferData(GL_ARRAY_BUFFER, kInitialItemDropVertexBufferBytes, nullptr, GL_DYNAMIC_DRAW);

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
    glEnableVertexAttribArray(7);
    glVertexAttribPointer(7, 1, GL_FLOAT, GL_FALSE, sizeof(ChunkVertex), reinterpret_cast<void*>(offsetof(ChunkVertex, material_class)));

    item_drop_vertex_buffer_bytes_ = kInitialItemDropVertexBufferBytes;
}

void Renderer::create_screen_quad_geometry() {
    glGenVertexArrays(1, &screen_quad_vao_);
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
    hud_vertex_buffer_bytes_ = kInitialHudBufferBytes;
}

void Renderer::ensure_hud_buffer_capacity(std::size_t vertex_count) {
    const auto required_bytes = static_cast<GLsizeiptr>(vertex_count * sizeof(HudVertex));
    if (hud_vertex_buffer_bytes_ >= required_bytes) {
        return;
    }

    hud_vertex_buffer_bytes_ = grow_buffer_capacity(
        hud_vertex_buffer_bytes_,
        required_bytes,
        kInitialHudBufferBytes);
    glBindBuffer(GL_ARRAY_BUFFER, hud_vbo_);
    glBufferData(GL_ARRAY_BUFFER, hud_vertex_buffer_bytes_, nullptr, GL_DYNAMIC_DRAW);
}

void Renderer::upload_hud_vertices(std::span<const HudVertex> vertices) {
    glBindVertexArray(hud_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, hud_vbo_);
    ensure_hud_buffer_capacity(vertices.size());
    glBufferSubData(
        GL_ARRAY_BUFFER,
        0,
        static_cast<GLsizeiptr>(vertices.size() * sizeof(HudVertex)),
        vertices.data());
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

void Renderer::ensure_post_process_targets(int width, int height) {
    const auto target_width = std::max(width, 1);
    const auto target_height = std::max(height, 1);
    const auto glow_width = std::max(target_width / 2, 1);
    const auto glow_height = std::max(target_height / 2, 1);

    const auto scene_matches = scene_framebuffer_ != 0 && scene_target_width_ == target_width && scene_target_height_ == target_height;
    const auto glow_matches = glow_extract_framebuffer_ != 0 && glow_ping_framebuffer_ != 0 && glow_target_width_ == glow_width &&
                              glow_target_height_ == glow_height;
    if (scene_matches && glow_matches) {
        return;
    }

    destroy_post_process_targets();

    glGenFramebuffers(1, &scene_framebuffer_);
    glBindFramebuffer(GL_FRAMEBUFFER, scene_framebuffer_);

    glGenTextures(1, &scene_color_texture_);
    glBindTexture(GL_TEXTURE_2D, scene_color_texture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, target_width, target_height, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, scene_color_texture_, 0);

    glGenRenderbuffers(1, &scene_depth_renderbuffer_);
    glBindRenderbuffer(GL_RENDERBUFFER, scene_depth_renderbuffer_);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, target_width, target_height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, scene_depth_renderbuffer_);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        throw std::runtime_error("Scene framebuffer is incomplete");
    }

    glGenFramebuffers(1, &glow_extract_framebuffer_);
    glBindFramebuffer(GL_FRAMEBUFFER, glow_extract_framebuffer_);
    glGenTextures(1, &glow_extract_texture_);
    glBindTexture(GL_TEXTURE_2D, glow_extract_texture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, glow_width, glow_height, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, glow_extract_texture_, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        throw std::runtime_error("Glow extract framebuffer is incomplete");
    }

    glGenFramebuffers(1, &glow_ping_framebuffer_);
    glBindFramebuffer(GL_FRAMEBUFFER, glow_ping_framebuffer_);
    glGenTextures(1, &glow_ping_texture_);
    glBindTexture(GL_TEXTURE_2D, glow_ping_texture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, glow_width, glow_height, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, glow_ping_texture_, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        throw std::runtime_error("Glow blur framebuffer is incomplete");
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    scene_target_width_ = target_width;
    scene_target_height_ = target_height;
    glow_target_width_ = glow_width;
    glow_target_height_ = glow_height;
}

void Renderer::destroy_post_process_targets() {
    if (scene_depth_renderbuffer_ != 0) {
        glDeleteRenderbuffers(1, &scene_depth_renderbuffer_);
        scene_depth_renderbuffer_ = 0;
    }
    if (scene_color_texture_ != 0) {
        glDeleteTextures(1, &scene_color_texture_);
        scene_color_texture_ = 0;
    }
    if (scene_framebuffer_ != 0) {
        glDeleteFramebuffers(1, &scene_framebuffer_);
        scene_framebuffer_ = 0;
    }
    if (glow_extract_texture_ != 0) {
        glDeleteTextures(1, &glow_extract_texture_);
        glow_extract_texture_ = 0;
    }
    if (glow_extract_framebuffer_ != 0) {
        glDeleteFramebuffers(1, &glow_extract_framebuffer_);
        glow_extract_framebuffer_ = 0;
    }
    if (glow_ping_texture_ != 0) {
        glDeleteTextures(1, &glow_ping_texture_);
        glow_ping_texture_ = 0;
    }
    if (glow_ping_framebuffer_ != 0) {
        glDeleteFramebuffers(1, &glow_ping_framebuffer_);
        glow_ping_framebuffer_ = 0;
    }
    scene_target_width_ = 0;
    scene_target_height_ = 0;
    glow_target_width_ = 0;
    glow_target_height_ = 0;
}

void Renderer::draw_sky(const PlayerController& player, const EnvironmentState& environment) {
    if (sky_program_ == 0 || screen_quad_vao_ == 0 || accent_texture_ == 0) {
        return;
    }

    std::array<GLint, 4> viewport {};
    glGetIntegerv(GL_VIEWPORT, viewport.data());
    const auto viewport_width = std::max(viewport[2], 1);
    const auto viewport_height = std::max(viewport[3], 1);
    const auto aspect = static_cast<float>(viewport_width) / static_cast<float>(viewport_height);
    const auto projection = glm::perspective(glm::radians(75.0F), aspect, 0.1F, 320.0F);
    auto view = player.view_matrix();
    view[3] = glm::vec4 {0.0F, 0.0F, 0.0F, 1.0F};
    const auto inverse_view_projection = glm::inverse(projection * view);

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    glUseProgram(sky_program_);
    glUniformMatrix4fv(sky_uniforms_.inverse_view_projection, 1, GL_FALSE, glm::value_ptr(inverse_view_projection));
    glUniform3fv(sky_uniforms_.sun_direction, 1, glm::value_ptr(environment.sun_direction));
    glUniform1f(sky_uniforms_.daylight_factor, environment.daylight_factor);
    glUniform1f(sky_uniforms_.time_of_day, environment.time_of_day);
    glUniform3fv(sky_uniforms_.sky_zenith_color, 1, glm::value_ptr(environment.sky_zenith_color));
    glUniform3fv(sky_uniforms_.sky_horizon_color, 1, glm::value_ptr(environment.sky_horizon_color));
    glUniform3fv(sky_uniforms_.horizon_glow_color, 1, glm::value_ptr(environment.horizon_glow_color));
    glUniform3fv(sky_uniforms_.sun_disk_color, 1, glm::value_ptr(environment.sun_disk_color));
    glUniform3fv(sky_uniforms_.moon_disk_color, 1, glm::value_ptr(environment.moon_disk_color));
    glUniform1f(sky_uniforms_.star_intensity, environment.star_intensity);
    glUniform1f(sky_uniforms_.cloud_intensity, environment.cloud_intensity);
    glUniform1i(sky_uniforms_.accent_atlas, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, accent_texture_);
    glBindVertexArray(screen_quad_vao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
}

void Renderer::run_post_process(const EnvironmentState& environment, int width, int height) {
    if (post_process_program_ == 0 || glow_extract_program_ == 0 || glow_blur_program_ == 0 || screen_quad_vao_ == 0 ||
        scene_color_texture_ == 0 || glow_extract_texture_ == 0 || glow_ping_texture_ == 0) {
        return;
    }

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glBindVertexArray(screen_quad_vao_);

    glViewport(0, 0, glow_target_width_, glow_target_height_);
    glBindFramebuffer(GL_FRAMEBUFFER, glow_extract_framebuffer_);
    glClearColor(0.0F, 0.0F, 0.0F, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(glow_extract_program_);
    glUniform1i(glow_extract_uniforms_.scene_texture, 0);
    glUniform1f(glow_extract_uniforms_.threshold, environment.glow_threshold);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, scene_color_texture_);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    glBindFramebuffer(GL_FRAMEBUFFER, glow_ping_framebuffer_);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(glow_blur_program_);
    glUniform1i(glow_blur_uniforms_.source_texture, 0);
    glUniform2f(glow_blur_uniforms_.texel_direction, 1.0F / static_cast<float>(glow_target_width_), 0.0F);
    glBindTexture(GL_TEXTURE_2D, glow_extract_texture_);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    glBindFramebuffer(GL_FRAMEBUFFER, glow_extract_framebuffer_);
    glClear(GL_COLOR_BUFFER_BIT);
    glUniform2f(glow_blur_uniforms_.texel_direction, 0.0F, 1.0F / static_cast<float>(glow_target_height_));
    glBindTexture(GL_TEXTURE_2D, glow_ping_texture_);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, width, std::max(height, 1));
    glUseProgram(post_process_program_);
    glUniform1i(post_process_uniforms_.scene_texture, 0);
    glUniform1i(post_process_uniforms_.glow_texture, 1);
    glUniform1f(post_process_uniforms_.exposure, environment.exposure);
    glUniform1f(post_process_uniforms_.saturation_boost, environment.saturation_boost);
    glUniform1f(post_process_uniforms_.contrast, environment.contrast);
    glUniform1f(post_process_uniforms_.vignette_strength, environment.vignette_strength);
    glUniform3fv(post_process_uniforms_.night_tint_color, 1, glm::value_ptr(environment.night_tint_color));
    glUniform1f(post_process_uniforms_.glow_strength, environment.glow_strength);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, scene_color_texture_);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, glow_extract_texture_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glActiveTexture(GL_TEXTURE0);
}

void Renderer::draw_item_drops(std::span<const ItemDropRenderInstance> item_drops) {
    if (item_drops.empty() || world_program_ == 0 || item_drop_vao_ == 0 || item_drop_vbo_ == 0) {
        return;
    }

    auto& vertices = item_drop_vertices_scratch_;
    vertices.clear();
    vertices.reserve(item_drops.size() * 36U);

    for (const auto& drop : item_drops) {
        if (drop.block_id == to_block_id(BlockType::Air) || drop.count == 0) {
            continue;
        }

        const auto uv_rect = atlas_uv_rect(block_hotbar_tile(drop.block_id));
        const auto material_class = block_visual_material_value(drop.block_id);
        const auto bob_offset = std::sin(drop.age_seconds * 3.2F) * 0.06F + 0.12F;
        const auto size = drop.count >= 32 ? 0.42F : (drop.count >= 2 ? 0.39F : 0.35F);
        const auto half_width = glm::vec3 {size * 0.5F, 0.0F, 0.0F};
        const auto up = glm::vec3 {0.0F, size, 0.0F};
        const auto rotation = drop.spin_radians;
        const auto cos_rotation = std::cos(rotation);
        const auto sin_rotation = std::sin(rotation);
        const auto basis_a = glm::vec3 {cos_rotation * half_width.x, 0.0F, sin_rotation * half_width.x};
        const auto basis_b = glm::vec3 {-sin_rotation * half_width.x, 0.0F, cos_rotation * half_width.x};
        const auto layer_count = drop.count >= 32 ? 3 : (drop.count >= 2 ? 2 : 1);

        for (int layer = 0; layer < layer_count; ++layer) {
            const auto layer_offset = static_cast<float>(layer) * 0.035F;
            const auto bottom_center = drop.position + glm::vec3 {0.0F, bob_offset + layer_offset, 0.0F};
            append_item_drop_quad(vertices, bottom_center, basis_a, up, uv_rect, drop.sky_light, drop.block_light, material_class);
            append_item_drop_quad(vertices, bottom_center, basis_b, up, uv_rect, drop.sky_light, drop.block_light, material_class);
        }
    }

    if (vertices.empty()) {
        return;
    }

    const auto vertex_bytes = static_cast<GLsizeiptr>(vertices.size() * sizeof(ChunkVertex));
    glBindVertexArray(item_drop_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, item_drop_vbo_);
    if (item_drop_vertex_buffer_bytes_ < vertex_bytes) {
        item_drop_vertex_buffer_bytes_ = grow_buffer_capacity(
            item_drop_vertex_buffer_bytes_,
            vertex_bytes,
            kInitialItemDropVertexBufferBytes);
        glBufferData(GL_ARRAY_BUFFER, item_drop_vertex_buffer_bytes_, nullptr, GL_DYNAMIC_DRAW);
    }
    glBufferSubData(GL_ARRAY_BUFFER, 0, vertex_bytes, vertices.data());

    glUseProgram(world_program_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, atlas_texture_);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, shadow_map_);

    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size()));
    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
}

void Renderer::draw_creatures(std::span<const CreatureRenderInstance> creatures,
                              const glm::mat4& view_projection,
                              const glm::mat4& light_view_projection,
                              const glm::vec3& camera_position,
                              const EnvironmentState& environment) {
    if (creatures.empty() || creature_program_ == 0 || creature_vao_ == 0 || creature_vbo_ == 0 || creature_ebo_ == 0) {
        return;
    }

    constexpr float kCreatureDrawDistance = 64.0F;
    constexpr std::size_t kMaxRenderedCreatures = kCreatureMaxRenderedCount;
    const auto draw_distance_sq = kCreatureDrawDistance * kCreatureDrawDistance;

    struct VisibleCreature {
        const CreatureRenderInstance* creature = nullptr;
        float distance_squared = 0.0F;
    };

    std::vector<VisibleCreature> visible_creatures;
    visible_creatures.reserve(std::min(creatures.size(), kMaxRenderedCreatures));

    for (const auto& creature : creatures) {
        const auto dx = creature.position.x - camera_position.x;
        const auto dz = creature.position.z - camera_position.z;
        const auto distance_squared = dx * dx + dz * dz;
        if (distance_squared > draw_distance_sq) {
            continue;
        }

        visible_creatures.push_back({&creature, distance_squared});
    }

    if (visible_creatures.empty()) {
        return;
    }

    std::sort(visible_creatures.begin(), visible_creatures.end(), [](const VisibleCreature& lhs, const VisibleCreature& rhs) {
        return lhs.distance_squared < rhs.distance_squared;
    });
    if (visible_creatures.size() > kMaxRenderedCreatures) {
        visible_creatures.resize(kMaxRenderedCreatures);
    }

    auto& vertices = creature_vertices_scratch_;
    auto& indices = creature_indices_scratch_;
    vertices.clear();
    indices.clear();
    vertices.reserve(visible_creatures.size() * kCreatureVerticesPerBox * kCreatureMaxBoxBudget);
    indices.reserve(visible_creatures.size() * kCreatureIndicesPerBox * kCreatureMaxBoxBudget);

    for (const auto& visible_creature : visible_creatures) {
        const auto mesh = build_creature_mesh(*visible_creature.creature);
        if (mesh.empty()) {
            continue;
        }

        const auto base_index = static_cast<std::uint32_t>(vertices.size());
        vertices.insert(vertices.end(), mesh.vertices.begin(), mesh.vertices.end());
        indices.reserve(indices.size() + mesh.indices.size());
        for (const auto index : mesh.indices) {
            indices.push_back(base_index + index);
        }
    }

    if (indices.empty()) {
        return;
    }

    glBindVertexArray(creature_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, creature_vbo_);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, creature_ebo_);

    const auto vertex_bytes = static_cast<GLsizeiptr>(vertices.size() * sizeof(CreatureVertex));
    const auto index_bytes = static_cast<GLsizeiptr>(indices.size() * sizeof(std::uint32_t));
    if (creature_vertex_buffer_bytes_ < vertex_bytes) {
        creature_vertex_buffer_bytes_ = grow_buffer_capacity(
            creature_vertex_buffer_bytes_,
            vertex_bytes,
            kInitialCreatureVertexBufferBytes);
        glBufferData(GL_ARRAY_BUFFER, creature_vertex_buffer_bytes_, nullptr, GL_DYNAMIC_DRAW);
    }
    if (creature_index_buffer_bytes_ < index_bytes) {
        creature_index_buffer_bytes_ = grow_buffer_capacity(
            creature_index_buffer_bytes_,
            index_bytes,
            kInitialCreatureIndexBufferBytes);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, creature_index_buffer_bytes_, nullptr, GL_DYNAMIC_DRAW);
    }

    glBufferSubData(GL_ARRAY_BUFFER, 0, vertex_bytes, vertices.data());
    glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, index_bytes, indices.data());

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glUseProgram(creature_program_);
    glUniformMatrix4fv(creature_uniforms_.view_projection, 1, GL_FALSE, glm::value_ptr(view_projection));
    glUniformMatrix4fv(creature_uniforms_.light_view_projection, 1, GL_FALSE, glm::value_ptr(light_view_projection));
    glUniform3fv(creature_uniforms_.camera_position, 1, glm::value_ptr(camera_position));
    glUniform3fv(creature_uniforms_.sun_direction, 1, glm::value_ptr(environment.sun_direction));
    glUniform3fv(creature_uniforms_.sun_color, 1, glm::value_ptr(environment.sun_color));
    glUniform3fv(creature_uniforms_.ambient_color, 1, glm::value_ptr(environment.ambient_color));
    glUniform3fv(creature_uniforms_.fog_color, 1, glm::value_ptr(environment.fog_color));
    glUniform3fv(creature_uniforms_.distant_fog_color, 1, glm::value_ptr(environment.distant_fog_color));
    glUniform3fv(creature_uniforms_.night_tint_color, 1, glm::value_ptr(environment.night_tint_color));
    glUniform1f(creature_uniforms_.daylight_factor, environment.daylight_factor);
    glUniform1f(creature_uniforms_.sun_visibility, environment.sun_direction.y > 0.0F ? 1.0F : 0.0F);
    glUniform1i(creature_uniforms_.atlas, 0);
    glUniform1i(creature_uniforms_.shadow_map, 1);
    glUniform1i(creature_uniforms_.shadows_enabled, options_.shadows_enabled ? 1 : 0);
    glUniform1f(creature_uniforms_.time_of_day, environment.time_of_day);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, creature_atlas_texture_);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, shadow_map_);
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(indices.size()), GL_UNSIGNED_INT, nullptr);
    glActiveTexture(GL_TEXTURE0);
}

void Renderer::draw_player_avatar(const PlayerController& player,
                                  const glm::mat4& view_projection,
                                  const glm::mat4& light_view_projection,
                                  const glm::vec3& camera_position,
                                  const EnvironmentState& environment) {
    if (player.is_dead() || creature_program_ == 0 || creature_vao_ == 0 || creature_vbo_ == 0 || creature_ebo_ == 0 || player_atlas_texture_ == 0) {
        return;
    }

    const auto mesh = build_player_mesh(player);
    if (mesh.empty()) {
        return;
    }

    auto& vertices = creature_vertices_scratch_;
    auto& indices = creature_indices_scratch_;
    vertices.assign(mesh.vertices.begin(), mesh.vertices.end());
    indices.assign(mesh.indices.begin(), mesh.indices.end());

    glBindVertexArray(creature_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, creature_vbo_);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, creature_ebo_);

    const auto vertex_bytes = static_cast<GLsizeiptr>(vertices.size() * sizeof(CreatureVertex));
    const auto index_bytes = static_cast<GLsizeiptr>(indices.size() * sizeof(std::uint32_t));
    if (creature_vertex_buffer_bytes_ < vertex_bytes) {
        creature_vertex_buffer_bytes_ = grow_buffer_capacity(
            creature_vertex_buffer_bytes_,
            vertex_bytes,
            kInitialCreatureVertexBufferBytes);
        glBufferData(GL_ARRAY_BUFFER, creature_vertex_buffer_bytes_, nullptr, GL_DYNAMIC_DRAW);
    }
    if (creature_index_buffer_bytes_ < index_bytes) {
        creature_index_buffer_bytes_ = grow_buffer_capacity(
            creature_index_buffer_bytes_,
            index_bytes,
            kInitialCreatureIndexBufferBytes);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, creature_index_buffer_bytes_, nullptr, GL_DYNAMIC_DRAW);
    }

    glBufferSubData(GL_ARRAY_BUFFER, 0, vertex_bytes, vertices.data());
    glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, index_bytes, indices.data());

    glEnable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glUseProgram(creature_program_);
    glUniformMatrix4fv(creature_uniforms_.view_projection, 1, GL_FALSE, glm::value_ptr(view_projection));
    glUniformMatrix4fv(creature_uniforms_.light_view_projection, 1, GL_FALSE, glm::value_ptr(light_view_projection));
    glUniform3fv(creature_uniforms_.camera_position, 1, glm::value_ptr(camera_position));
    glUniform3fv(creature_uniforms_.sun_direction, 1, glm::value_ptr(environment.sun_direction));
    glUniform3fv(creature_uniforms_.sun_color, 1, glm::value_ptr(environment.sun_color));
    glUniform3fv(creature_uniforms_.ambient_color, 1, glm::value_ptr(environment.ambient_color));
    glUniform3fv(creature_uniforms_.fog_color, 1, glm::value_ptr(environment.fog_color));
    glUniform3fv(creature_uniforms_.distant_fog_color, 1, glm::value_ptr(environment.distant_fog_color));
    glUniform3fv(creature_uniforms_.night_tint_color, 1, glm::value_ptr(environment.night_tint_color));
    glUniform1f(creature_uniforms_.daylight_factor, environment.daylight_factor);
    glUniform1f(creature_uniforms_.sun_visibility, environment.sun_direction.y > 0.0F ? 1.0F : 0.0F);
    glUniform1i(creature_uniforms_.atlas, 0);
    glUniform1i(creature_uniforms_.shadow_map, 1);
    glUniform1i(creature_uniforms_.shadows_enabled, options_.shadows_enabled ? 1 : 0);
    glUniform1f(creature_uniforms_.time_of_day, environment.time_of_day);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, player_atlas_texture_);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, shadow_map_);
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(indices.size()), GL_UNSIGNED_INT, nullptr);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glActiveTexture(GL_TEXTURE0);
}

void Renderer::draw_hotbar(const PlayerController& player, const HotbarState& hotbar, const EnvironmentState& /*environment*/, int width, int height) {
    if (width <= 0 || height <= 0 || hud_program_ == 0 || hud_vao_ == 0 || hud_vbo_ == 0) {
        return;
    }

    const auto& player_state = player.state();
    const auto max_health = std::max(player.max_health(), 0.001F);
    const auto max_air = std::max(player.max_air_seconds(), 0.001F);
    const auto damage_flash =
        std::max(
            glm::clamp(player_state.hurt_timer / 0.35F, 0.0F, 1.0F) * 0.32F,
            glm::clamp((max_health - player_state.health) / max_health, 0.0F, 1.0F) * 0.18F);
    const auto air_visible = player_state.head_underwater || player_state.air_seconds < max_air - 0.05F;
    const auto hud_layout =
        build_gameplay_hud_layout(width, height, hotbar, player_state.health, max_health, player_state.air_seconds, max_air, air_visible);

    HotbarHudCacheKey cache_key {};
    cache_key.hotbar = hotbar;
    cache_key.width = width;
    cache_key.height = height;
    cache_key.health_steps = quantize_hud_value(player_state.health, 16.0F);
    cache_key.air_steps = quantize_hud_value(player_state.air_seconds, 64.0F);
    cache_key.damage_flash_step = quantize_hud_value(damage_flash, 128.0F);
    cache_key.air_visible = air_visible;

    auto& cache = hotbar_cache_;
    auto& vertices = cache.vertices;
    const auto needs_rebuild = !cache.valid || cache.key != cache_key;
    if (needs_rebuild) {
        cache.valid = true;
        cache.key = cache_key;
        vertices.clear();
        vertices.reserve(16384U);

        const auto viewport_width = static_cast<float>(width);
        const auto viewport_height = static_cast<float>(height);

        if (damage_flash > 0.0F) {
            const auto edge_size = std::clamp(std::min(viewport_width, viewport_height) * 0.09F, 28.0F, 72.0F);
            append_hud_rect_top_left(vertices, viewport_width, viewport_height, 0.0F, 0.0F, viewport_width, edge_size, {0.48F, 0.04F, 0.05F, damage_flash});
            append_hud_rect_top_left(
                vertices,
                viewport_width,
                viewport_height,
                0.0F,
                viewport_height - edge_size,
                viewport_width,
                edge_size,
                {0.48F, 0.04F, 0.05F, damage_flash});
            append_hud_rect_top_left(vertices, viewport_width, viewport_height, 0.0F, 0.0F, edge_size, viewport_height, {0.48F, 0.04F, 0.05F, damage_flash * 0.9F});
            append_hud_rect_top_left(
                vertices,
                viewport_width,
                viewport_height,
                viewport_width - edge_size,
                0.0F,
                edge_size,
                viewport_height,
                {0.48F, 0.04F, 0.05F, damage_flash * 0.9F});
        }

        append_hud_beveled_panel(
            vertices,
            viewport_width,
            viewport_height,
            hud_layout.hotbar_panel_x,
            hud_layout.hotbar_panel_bottom,
            hud_layout.hotbar_panel_width,
            hud_layout.hotbar_panel_height,
            4.0F,
            {0.04F, 0.04F, 0.05F, 0.94F},
            {0.10F, 0.10F, 0.11F, 0.88F},
            {0.68F, 0.68F, 0.70F, 0.10F},
            {0.01F, 0.01F, 0.01F, 0.70F});
        append_hud_rect(
            vertices,
            viewport_width,
            viewport_height,
            hud_layout.hotbar_panel_x + 6.0F,
            hud_layout.hotbar_panel_bottom + hud_layout.hotbar_panel_height - 6.0F,
            std::max(0.0F, hud_layout.hotbar_panel_width - 12.0F),
            2.0F,
            {1.0F, 1.0F, 1.0F, 0.06F});

        for (const auto& heart : hud_layout.hearts) {
            append_heart_glyph_bottom_left(vertices, viewport_width, viewport_height, heart);
        }

        if (hud_layout.air_visible) {
            for (const auto& bubble : hud_layout.bubbles) {
                append_bubble_glyph_bottom_left(vertices, viewport_width, viewport_height, bubble);
            }
        }

        const auto slot_border_thickness = std::max(2.0F, hud_layout.hotbar.slot_size * 0.07F);
        const auto glow_padding = std::max(2.0F, hud_layout.hotbar.slot_size * 0.05F);
        for (const auto& slot : hud_layout.slots) {
            if (slot.is_selected) {
                append_hud_rect(
                    vertices,
                    viewport_width,
                    viewport_height,
                    slot.x - glow_padding,
                    slot.bottom - glow_padding,
                    slot.size + glow_padding * 2.0F,
                    slot.size + glow_padding * 2.0F,
                    {1.00F, 0.94F, 0.68F, 0.16F});
            }

            const auto border_color = slot.is_selected
                                          ? std::array<float, 4> {0.96F, 0.90F, 0.66F, 1.0F}
                                          : std::array<float, 4> {0.22F, 0.22F, 0.24F, 0.98F};
            const auto fill_color = slot.is_selected
                                        ? std::array<float, 4> {0.26F, 0.26F, 0.28F, 0.96F}
                                        : (slot.has_icon
                                               ? std::array<float, 4> {0.18F, 0.18F, 0.19F, 0.90F}
                                               : std::array<float, 4> {0.13F, 0.13F, 0.14F, 0.82F});
            append_hud_beveled_panel(
                vertices,
                viewport_width,
                viewport_height,
                slot.x,
                slot.bottom,
                slot.size,
                slot.size,
                slot_border_thickness,
                border_color,
                fill_color,
                {1.0F, 1.0F, 1.0F, slot.is_selected ? 0.14F : 0.06F},
                {0.0F, 0.0F, 0.0F, 0.42F});

            if (slot.is_selected) {
                const auto selected_highlight_height = std::max(1.0F, slot.size * 0.06F);
                append_hud_rect(
                    vertices,
                    viewport_width,
                    viewport_height,
                    slot.x + slot_border_thickness + 1.0F,
                    slot.bottom + slot.size - slot_border_thickness - 1.0F - selected_highlight_height,
                    std::max(0.0F, slot.size - slot_border_thickness * 2.0F - 2.0F),
                    selected_highlight_height,
                    {1.0F, 0.98F, 0.90F, 0.10F});
            }

            if (!slot.has_icon) {
                continue;
            }

            append_hud_quad(
                vertices,
                viewport_width,
                viewport_height,
                slot.icon_x,
                slot.icon_bottom,
                slot.icon_size,
                slot.icon_size,
                {1.0F, 1.0F, 1.0F, slot.is_selected ? 1.0F : 0.96F},
                atlas_uv_rect(slot.icon_tile),
                1.0F);
            if (slot.show_stack_count) {
                append_stack_count_bottom_left(
                    vertices,
                    viewport_width,
                    viewport_height,
                    slot.count_right_x,
                    slot.count_bottom,
                    2.0F,
                    slot.slot.count);
            }
        }

        const auto selected_label = item_stack_display_label(hotbar.selected_slot());
        if (!selected_label.empty()) {
            append_pixel_text_bottom_left(
                vertices,
                viewport_width,
                viewport_height,
                hud_layout.label.center_x + hud_layout.label.pixel_size,
                hud_layout.label.bottom - hud_layout.label.pixel_size,
                hud_layout.label.pixel_size,
                selected_label,
                {0.0F, 0.0F, 0.0F, 0.55F},
                true);
            append_pixel_text_bottom_left(
                vertices,
                viewport_width,
                viewport_height,
                hud_layout.label.center_x,
                hud_layout.label.bottom,
                hud_layout.label.pixel_size,
                selected_label,
                {0.98F, 0.98F, 0.98F, 0.98F},
                true);
        }
    }

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glUseProgram(hud_program_);
    glUniform1i(hud_uniforms_.atlas, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, atlas_texture_);

    upload_hud_vertices(vertices);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size()));

    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
}

void Renderer::draw_inventory_menu(const InventoryMenuState& inventory_menu, const HotbarState& hotbar, int width, int height) {
    if (width <= 0 || height <= 0 || hud_program_ == 0 || hud_vao_ == 0 || hud_vbo_ == 0) {
        return;
    }

    const auto layout = build_inventory_menu_layout(width, height, inventory_menu, hotbar);
    const auto viewport_width = static_cast<float>(width);
    const auto viewport_height = static_cast<float>(height);
    InventoryHudCacheKey cache_key {};
    cache_key.inventory_menu = inventory_menu;
    cache_key.hotbar = hotbar;
    cache_key.width = width;
    cache_key.height = height;

    auto& cache = inventory_cache_;
    auto& vertices = cache.vertices;
    const auto needs_rebuild = !cache.valid || cache.key != cache_key;
    if (needs_rebuild) {
        cache.valid = true;
        cache.key = cache_key;
        vertices.clear();
        vertices.reserve(16384U);

    append_hud_rect_top_left(
        vertices,
        viewport_width,
        viewport_height,
        0.0F,
        0.0F,
        viewport_width,
        viewport_height,
        {0.02F, 0.03F, 0.04F, 0.62F});

    append_hud_beveled_panel_top_left(
        vertices,
        viewport_width,
        viewport_height,
        layout.panel_x,
        layout.panel_y,
        layout.panel_width,
        layout.panel_height,
        8.0F,
        {0.05F, 0.05F, 0.06F, 0.96F},
        {0.27F, 0.28F, 0.31F, 0.94F},
        {0.55F, 0.57F, 0.61F, 0.34F},
        {0.03F, 0.03F, 0.04F, 0.78F});

    append_hud_beveled_panel_top_left(
        vertices,
        viewport_width,
        viewport_height,
        layout.preview_x,
        layout.preview_y,
        layout.preview_width,
        layout.preview_height,
        5.0F,
        {0.07F, 0.07F, 0.08F, 0.95F},
        {0.17F, 0.18F, 0.20F, 0.92F},
        {0.38F, 0.40F, 0.44F, 0.22F},
        {0.04F, 0.04F, 0.05F, 0.64F});

    append_hud_beveled_panel_top_left(
        vertices,
        viewport_width,
        viewport_height,
        layout.grid_x - 12.0F,
        layout.grid_y - 14.0F,
        layout.grid_width + 24.0F,
        layout.grid_height + 28.0F,
        5.0F,
        {0.07F, 0.07F, 0.08F, 0.90F},
        {0.15F, 0.16F, 0.18F, 0.72F},
        {0.35F, 0.36F, 0.40F, 0.18F},
        {0.03F, 0.03F, 0.04F, 0.56F});

    const auto title_pixel_size = static_cast<float>(std::floor(std::clamp(viewport_width * 0.0035F, 4.0F, 6.0F)));
    const auto subtitle_pixel_size = static_cast<float>(std::floor(std::clamp(viewport_width * 0.0019F, 2.0F, 3.0F)));
    const auto label_pixel_size = static_cast<float>(std::floor(std::clamp(layout.slot_size / 18.0F, 2.0F, 3.0F)));
    const auto brand_pixel_size = subtitle_pixel_size;

    append_pixel_text(
        vertices,
        viewport_width,
        viewport_height,
        layout.title_center_x,
        layout.panel_y + 10.0F,
        brand_pixel_size,
        kGameDisplayNamePixel,
        {0.72F, 0.74F, 0.78F, 0.90F},
        true);

    append_pixel_text(
        vertices,
        viewport_width,
        viewport_height,
        layout.title_center_x + title_pixel_size,
        layout.title_y + title_pixel_size,
        title_pixel_size,
        "INVENTAIRE",
        {0.0F, 0.0F, 0.0F, 0.55F},
        true);
    append_pixel_text(
        vertices,
        viewport_width,
        viewport_height,
        layout.title_center_x,
        layout.title_y,
        title_pixel_size,
        "INVENTAIRE",
        {0.96F, 0.97F, 0.99F, 1.0F},
        true);
    append_pixel_text(
        vertices,
        viewport_width,
        viewport_height,
        layout.subtitle_center_x,
        layout.subtitle_y,
        subtitle_pixel_size,
        "EQUIPEMENT ET SAC",
        {0.78F, 0.80F, 0.84F, 0.92F},
        true);
    append_pixel_text(
        vertices,
        viewport_width,
        viewport_height,
        layout.preview_x + layout.preview_width * 0.5F,
        layout.preview_y + 14.0F,
        label_pixel_size,
        "AVATAR",
        {0.86F, 0.88F, 0.92F, 0.92F},
        true);
    append_pixel_text(
        vertices,
        viewport_width,
        viewport_height,
        layout.storage_label_x,
        layout.storage_label_y,
        label_pixel_size,
        "SAC",
        {0.86F, 0.88F, 0.92F, 0.92F});
    append_pixel_text(
        vertices,
        viewport_width,
        viewport_height,
        layout.hotbar_label_x,
        layout.hotbar_label_y,
        label_pixel_size,
        "BARRE RAPIDE",
        {0.86F, 0.88F, 0.92F, 0.92F});
    append_pixel_text(
        vertices,
        viewport_width,
        viewport_height,
        layout.footer_center_x,
        layout.footer_y,
        subtitle_pixel_size,
        "E OU ECHAP POUR FERMER",
        {0.70F, 0.72F, 0.76F, 0.92F},
        true);

    const auto scale = layout.silhouette_scale;
    const auto center_x = layout.preview_center_x;
    const auto base_y = layout.preview_base_y;
    append_hud_rect_top_left(
        vertices,
        viewport_width,
        viewport_height,
        center_x - scale * 0.55F,
        base_y - scale * 6.70F,
        scale * 1.10F,
        scale * 1.10F,
        {0.92F, 0.78F, 0.60F, 1.0F});
    append_hud_rect_top_left(
        vertices,
        viewport_width,
        viewport_height,
        center_x - scale * 0.80F,
        base_y - scale * 5.55F,
        scale * 1.60F,
        scale * 2.10F,
        {0.36F, 0.58F, 0.92F, 1.0F});
    append_hud_rect_top_left(
        vertices,
        viewport_width,
        viewport_height,
        center_x - scale * 1.55F,
        base_y - scale * 5.35F,
        scale * 0.65F,
        scale * 1.85F,
        {0.92F, 0.78F, 0.60F, 1.0F});
    append_hud_rect_top_left(
        vertices,
        viewport_width,
        viewport_height,
        center_x + scale * 0.90F,
        base_y - scale * 5.35F,
        scale * 0.65F,
        scale * 1.85F,
        {0.92F, 0.78F, 0.60F, 1.0F});
    append_hud_rect_top_left(
        vertices,
        viewport_width,
        viewport_height,
        center_x - scale * 0.75F,
        base_y - scale * 3.30F,
        scale * 0.65F,
        scale * 2.35F,
        {0.20F, 0.24F, 0.40F, 1.0F});
    append_hud_rect_top_left(
        vertices,
        viewport_width,
        viewport_height,
        center_x + scale * 0.10F,
        base_y - scale * 3.30F,
        scale * 0.65F,
        scale * 2.35F,
        {0.20F, 0.24F, 0.40F, 1.0F});
    append_hud_rect_top_left(
        vertices,
        viewport_width,
        viewport_height,
        center_x - scale * 1.95F,
        base_y - scale * 6.35F,
        scale * 3.90F,
        scale * 0.55F,
        {1.0F, 1.0F, 1.0F, 0.05F});

    const auto slot_border_thickness = std::max(2.0F, layout.slot_size * 0.08F);
    const auto slot_glow_padding = std::max(2.0F, layout.slot_size * 0.08F);
    for (const auto& slot : layout.slots) {
        if (slot.hovered) {
            append_hud_rect_top_left(
                vertices,
                viewport_width,
                viewport_height,
                slot.x - slot_glow_padding,
                slot.y - slot_glow_padding,
                slot.size + slot_glow_padding * 2.0F,
                slot.size + slot_glow_padding * 2.0F,
                {1.0F, 0.96F, 0.80F, 0.18F});
        } else if (slot.is_selected_hotbar) {
            append_hud_rect_top_left(
                vertices,
                viewport_width,
                viewport_height,
                slot.x - slot_glow_padding,
                slot.y - slot_glow_padding,
                slot.size + slot_glow_padding * 2.0F,
                slot.size + slot_glow_padding * 2.0F,
                {1.0F, 0.84F, 0.42F, 0.12F});
        }

        const auto border_color = slot.hovered
                                      ? std::array<float, 4> {0.94F, 0.96F, 0.99F, 1.0F}
                                      : (slot.is_selected_hotbar
                                             ? std::array<float, 4> {0.98F, 0.88F, 0.52F, 1.0F}
                                             : std::array<float, 4> {0.10F, 0.10F, 0.11F, 0.96F});
        const auto fill_color = slot.has_icon
                                    ? (slot.is_hotbar
                                           ? std::array<float, 4> {0.19F, 0.20F, 0.23F, 0.92F}
                                           : std::array<float, 4> {0.15F, 0.16F, 0.18F, 0.92F})
                                    : std::array<float, 4> {0.10F, 0.10F, 0.12F, 0.82F};
        const auto highlight_color = slot.hovered
                                         ? std::array<float, 4> {0.82F, 0.84F, 0.90F, 0.30F}
                                         : std::array<float, 4> {0.42F, 0.44F, 0.48F, 0.18F};
        const auto shadow_color = slot.hovered
                                      ? std::array<float, 4> {0.10F, 0.10F, 0.12F, 0.84F}
                                      : std::array<float, 4> {0.03F, 0.03F, 0.04F, 0.70F};

        append_hud_beveled_panel_top_left(
            vertices,
            viewport_width,
            viewport_height,
            slot.x,
            slot.y,
            slot.size,
            slot.size,
            slot_border_thickness,
            border_color,
            fill_color,
            highlight_color,
            shadow_color);

        if (!slot.has_icon) {
            continue;
        }

        const auto icon_size = std::max(8.0F, slot.size - layout.icon_inset * 2.0F);
        const auto icon_offset = (slot.size - icon_size) * 0.5F;
        append_hud_quad_top_left(
            vertices,
            viewport_width,
            viewport_height,
            slot.x + icon_offset,
            slot.y + icon_offset,
            icon_size,
            icon_size,
            {1.0F, 1.0F, 1.0F, 1.0F},
            atlas_uv_rect(slot.icon_tile),
            1.0F);
        append_stack_count(
            vertices,
            viewport_width,
            viewport_height,
            slot.x + slot.size - 5.0F,
            slot.y + slot.size - 4.0F,
            std::max(2.0F, static_cast<float>(std::floor(layout.slot_size / 18.0F))),
            slot.slot.count);
    }

    std::string tooltip_label;
    if (inventory_menu.carrying_item && inventory_slot_has_item(inventory_menu.carried_slot)) {
        tooltip_label = item_stack_display_label(inventory_menu.carried_slot);
    } else if (inventory_menu.hovered_slot.has_value()) {
        if (const auto* slot = inventory_slot_ptr(inventory_menu, hotbar, *inventory_menu.hovered_slot);
            slot != nullptr && inventory_slot_has_item(*slot)) {
            tooltip_label = item_stack_display_label(*slot);
        }
    }

    if (!tooltip_label.empty()) {
        const auto tooltip_pixel_size = subtitle_pixel_size;
        const auto tooltip_padding = 8.0F;
        const auto tooltip_width = measure_pixel_text(tooltip_label, tooltip_pixel_size) + tooltip_padding * 2.0F;
        const auto tooltip_height = tooltip_pixel_size * 7.0F + tooltip_padding * 2.0F;
        const auto tooltip_x = std::clamp(
            inventory_menu.cursor_x + 18.0F,
            layout.panel_x + 12.0F,
            layout.panel_x + layout.panel_width - tooltip_width - 12.0F);
        const auto tooltip_y = std::clamp(
            inventory_menu.cursor_y + 18.0F,
            layout.panel_y + 12.0F,
            layout.panel_y + layout.panel_height - tooltip_height - 12.0F);

        append_hud_beveled_panel_top_left(
            vertices,
            viewport_width,
            viewport_height,
            tooltip_x,
            tooltip_y,
            tooltip_width,
            tooltip_height,
            4.0F,
            {0.06F, 0.06F, 0.07F, 0.98F},
            {0.14F, 0.15F, 0.17F, 0.96F},
            {0.38F, 0.40F, 0.44F, 0.20F},
            {0.03F, 0.03F, 0.04F, 0.60F});
        append_pixel_text(
            vertices,
            viewport_width,
            viewport_height,
            tooltip_x + tooltip_width * 0.5F,
            tooltip_y + tooltip_padding,
            tooltip_pixel_size,
            tooltip_label,
            {0.96F, 0.97F, 0.99F, 1.0F},
            true);
    }

    if (inventory_menu.carrying_item && inventory_slot_has_item(inventory_menu.carried_slot)) {
        const auto carried_size = layout.slot_size;
        const auto carried_x = inventory_menu.cursor_x - carried_size * 0.5F;
        const auto carried_y = inventory_menu.cursor_y - carried_size * 0.5F;
        append_hud_beveled_panel_top_left(
            vertices,
            viewport_width,
            viewport_height,
            carried_x,
            carried_y,
            carried_size,
            carried_size,
            slot_border_thickness,
            {0.95F, 0.96F, 0.98F, 0.98F},
            {0.22F, 0.23F, 0.26F, 0.96F},
            {0.74F, 0.76F, 0.82F, 0.28F},
            {0.08F, 0.08F, 0.10F, 0.78F});

        const auto icon_size = std::max(8.0F, carried_size - layout.icon_inset * 2.0F);
        const auto icon_offset = (carried_size - icon_size) * 0.5F;
        append_hud_quad_top_left(
            vertices,
            viewport_width,
            viewport_height,
            carried_x + icon_offset,
            carried_y + icon_offset,
            icon_size,
            icon_size,
            {1.0F, 1.0F, 1.0F, 1.0F},
            atlas_uv_rect(inventory_slot_icon_tile(inventory_menu.carried_slot.block_id)),
            1.0F);
        append_stack_count(
            vertices,
            viewport_width,
            viewport_height,
            carried_x + carried_size - 5.0F,
            carried_y + carried_size - 4.0F,
            std::max(2.0F, static_cast<float>(std::floor(layout.slot_size / 18.0F))),
            inventory_menu.carried_slot.count);
    }
    }

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glUseProgram(hud_program_);
    glUniform1i(hud_uniforms_.atlas, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, atlas_texture_);

    upload_hud_vertices(vertices);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size()));

    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
}

void Renderer::draw_death_screen(const DeathScreenState& death_screen, int width, int height) {
    if (width <= 0 || height <= 0 || hud_program_ == 0 || hud_vao_ == 0 || hud_vbo_ == 0) {
        return;
    }

    const auto layout = build_death_screen_layout(width, height, death_screen);
    const auto viewport_width = static_cast<float>(width);
    const auto viewport_height = static_cast<float>(height);
    DeathHudCacheKey cache_key {};
    cache_key.death_screen = death_screen;
    cache_key.width = width;
    cache_key.height = height;

    auto& cache = death_cache_;
    auto& vertices = cache.vertices;
    const auto needs_rebuild = !cache.valid || cache.key != cache_key;
    if (needs_rebuild) {
        cache.valid = true;
        cache.key = cache_key;
        vertices.clear();
        vertices.reserve(12288U);

    append_hud_rect_top_left(
        vertices,
        viewport_width,
        viewport_height,
        0.0F,
        0.0F,
        viewport_width,
        viewport_height,
        {0.10F, 0.02F, 0.03F, 0.72F});

    append_hud_beveled_panel_top_left(
        vertices,
        viewport_width,
        viewport_height,
        layout.panel_x,
        layout.panel_y,
        layout.panel_width,
        layout.panel_height,
        8.0F,
        {0.11F, 0.02F, 0.03F, 0.98F},
        {0.24F, 0.07F, 0.09F, 0.94F},
        {0.72F, 0.18F, 0.22F, 0.18F},
        {0.05F, 0.01F, 0.02F, 0.82F});

    const auto title_pixel_size = static_cast<float>(std::floor(std::clamp(viewport_width * 0.0039F, 4.0F, 7.0F)));
    const auto subtitle_pixel_size = static_cast<float>(std::floor(std::clamp(viewport_width * 0.0020F, 2.0F, 3.0F)));

    append_pixel_text(
        vertices,
        viewport_width,
        viewport_height,
        layout.title_center_x,
        layout.panel_y + 12.0F,
        subtitle_pixel_size,
        kGameDisplayNamePixel,
        {0.92F, 0.78F, 0.80F, 0.90F},
        true);
    append_pixel_text(
        vertices,
        viewport_width,
        viewport_height,
        layout.title_center_x + title_pixel_size,
        layout.title_y + title_pixel_size,
        title_pixel_size,
        "VOUS ETES MORT",
        {0.0F, 0.0F, 0.0F, 0.45F},
        true);
    append_pixel_text(
        vertices,
        viewport_width,
        viewport_height,
        layout.title_center_x,
        layout.title_y,
        title_pixel_size,
        "VOUS ETES MORT",
        {1.0F, 0.95F, 0.96F, 1.0F},
        true);
    append_pixel_text(
        vertices,
        viewport_width,
        viewport_height,
        layout.subtitle_center_x,
        layout.subtitle_y,
        subtitle_pixel_size,
        "LA SURVIE RECOMMENCE ICI",
        {0.98F, 0.82F, 0.84F, 0.96F},
        true);
    append_pixel_text(
        vertices,
        viewport_width,
        viewport_height,
        layout.cause_center_x,
        layout.cause_y,
        subtitle_pixel_size,
        death_screen_cause_label(death_screen.cause),
        {0.98F, 0.90F, 0.92F, 0.92F},
        true);

    for (const auto& button : layout.buttons) {
        const auto selected = button.selected;
        const auto border_color = selected
                                      ? std::array<float, 4> {0.98F, 0.96F, 0.98F, 1.0F}
                                      : std::array<float, 4> {0.15F, 0.03F, 0.04F, 0.98F};
        const auto fill_color = selected
                                    ? std::array<float, 4> {0.58F, 0.18F, 0.22F, 0.96F}
                                    : std::array<float, 4> {0.36F, 0.11F, 0.13F, 0.94F};
        append_hud_beveled_panel_top_left(
            vertices,
            viewport_width,
            viewport_height,
            button.x,
            button.y,
            button.width,
            button.height,
            5.0F,
            border_color,
            fill_color,
            {1.0F, 1.0F, 1.0F, selected ? 0.18F : 0.08F},
            {0.0F, 0.0F, 0.0F, 0.42F});

        const auto button_pixel_size = static_cast<float>(std::floor(std::clamp(button.height / 11.0F, 3.0F, 4.0F)));
        const auto text_y = button.y + std::floor((button.height - button_pixel_size * 7.0F) * 0.5F);
        append_pixel_text(
            vertices,
            viewport_width,
            viewport_height,
            button.x + button.width * 0.5F,
            text_y,
            button_pixel_size,
            button.label,
            {0.0F, 0.0F, 0.0F, 0.38F},
            true);
        append_pixel_text(
            vertices,
            viewport_width,
            viewport_height,
            button.x + button.width * 0.5F,
            text_y - 1.0F,
            button_pixel_size,
            button.label,
            {1.0F, 0.96F, 0.97F, 1.0F},
            true);
    }
    }

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glUseProgram(hud_program_);
    glUniform1i(hud_uniforms_.atlas, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, atlas_texture_);

    upload_hud_vertices(vertices);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size()));

    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
}

void Renderer::draw_pause_menu(const PauseMenuState& pause_menu, int width, int height) {
    if (width <= 0 || height <= 0 || hud_program_ == 0 || hud_vao_ == 0 || hud_vbo_ == 0) {
        return;
    }

    const auto layout = build_pause_menu_layout(width, height, pause_menu);
    const auto viewport_width = static_cast<float>(width);
    const auto viewport_height = static_cast<float>(height);
    PauseHudCacheKey cache_key {};
    cache_key.pause_menu = pause_menu;
    cache_key.width = width;
    cache_key.height = height;

    auto& cache = pause_cache_;
    auto& vertices = cache.vertices;
    const auto needs_rebuild = !cache.valid || cache.key != cache_key;
    if (needs_rebuild) {
        cache.valid = true;
        cache.key = cache_key;
        vertices.clear();
        vertices.reserve(8192U);

    append_hud_rect_top_left(
        vertices,
        viewport_width,
        viewport_height,
        0.0F,
        0.0F,
        viewport_width,
        viewport_height,
        {0.02F, 0.02F, 0.03F, 0.58F});

    append_hud_beveled_panel_top_left(
        vertices,
        viewport_width,
        viewport_height,
        layout.panel_x,
        layout.panel_y,
        layout.panel_width,
        layout.panel_height,
        8.0F,
        {0.05F, 0.05F, 0.06F, 0.96F},
        {0.22F, 0.23F, 0.27F, 0.92F},
        {0.40F, 0.42F, 0.46F, 0.40F},
        {0.03F, 0.03F, 0.04F, 0.78F});

    const auto title_pixel_size = static_cast<float>(std::floor(std::clamp(viewport_width * 0.0035F, 4.0F, 6.0F)));
    const auto subtitle_pixel_size = static_cast<float>(std::floor(std::clamp(viewport_width * 0.0019F, 2.0F, 3.0F)));
    const auto footer_y = layout.panel_y + layout.panel_height - 34.0F;

    append_pixel_text(
        vertices,
        viewport_width,
        viewport_height,
        layout.title_center_x,
        layout.panel_y + 10.0F,
        subtitle_pixel_size,
        kGameDisplayNamePixel,
        {0.72F, 0.74F, 0.78F, 0.90F},
        true);

    append_pixel_text(
        vertices,
        viewport_width,
        viewport_height,
        layout.title_center_x + title_pixel_size,
        layout.title_y + title_pixel_size,
        title_pixel_size,
        "JEU EN PAUSE",
        {0.0F, 0.0F, 0.0F, 0.55F},
        true);
    append_pixel_text(
        vertices,
        viewport_width,
        viewport_height,
        layout.title_center_x,
        layout.title_y,
        title_pixel_size,
        "JEU EN PAUSE",
        {0.96F, 0.97F, 0.99F, 1.0F},
        true);
    append_pixel_text(
        vertices,
        viewport_width,
        viewport_height,
        layout.subtitle_center_x,
        layout.subtitle_y,
        subtitle_pixel_size,
        "ECHAP POUR REPRENDRE",
        {0.78F, 0.80F, 0.85F, 0.92F},
        true);
    append_pixel_text(
        vertices,
        viewport_width,
        viewport_height,
        layout.subtitle_center_x,
        footer_y,
        subtitle_pixel_size,
        "ENTREE POUR VALIDER",
        {0.66F, 0.68F, 0.72F, 0.88F},
        true);

    for (const auto& button : layout.buttons) {
        std::array<float, 4> border_color {};
        std::array<float, 4> fill_color {};
        std::array<float, 4> highlight_color {};
        std::array<float, 4> shadow_color {};
        std::array<float, 4> text_color {};

        if (!button.enabled) {
            border_color = {0.05F, 0.05F, 0.06F, 0.88F};
            fill_color = {0.17F, 0.18F, 0.20F, 0.80F};
            highlight_color = {0.28F, 0.29F, 0.31F, 0.24F};
            shadow_color = {0.03F, 0.03F, 0.04F, 0.56F};
            text_color = {0.56F, 0.57F, 0.60F, 0.72F};
        } else if (button.selected) {
            border_color = {0.93F, 0.95F, 0.99F, 1.0F};
            fill_color = {0.43F, 0.45F, 0.52F, 0.96F};
            highlight_color = {0.72F, 0.74F, 0.82F, 0.42F};
            shadow_color = {0.12F, 0.12F, 0.14F, 0.78F};
            text_color = {1.0F, 1.0F, 1.0F, 1.0F};
        } else {
            border_color = {0.08F, 0.08F, 0.09F, 0.98F};
            fill_color = {0.31F, 0.33F, 0.37F, 0.94F};
            highlight_color = {0.55F, 0.57F, 0.62F, 0.34F};
            shadow_color = {0.10F, 0.10F, 0.12F, 0.72F};
            text_color = {0.93F, 0.94F, 0.96F, 0.96F};
        }

        append_hud_beveled_panel_top_left(
            vertices,
            viewport_width,
            viewport_height,
            button.x,
            button.y,
            button.width,
            button.height,
            5.0F,
            border_color,
            fill_color,
            highlight_color,
            shadow_color);

        const auto button_pixel_size = static_cast<float>(std::floor(std::clamp(button.height / 11.0F, 3.0F, 4.0F)));
        const auto text_y = button.y + static_cast<float>(std::floor((button.height - button_pixel_size * 7.0F) * 0.5F));
        append_pixel_text(
            vertices,
            viewport_width,
            viewport_height,
            button.x + button.width * 0.5F,
            text_y,
            button_pixel_size,
            button.label,
            {0.0F, 0.0F, 0.0F, 0.45F},
            true);
        append_pixel_text(
            vertices,
            viewport_width,
            viewport_height,
            button.x + button.width * 0.5F,
            text_y - 1.0F,
            button_pixel_size,
            button.label,
            text_color,
            true);
    }
    }

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glUseProgram(hud_program_);
    glUniform1i(hud_uniforms_.atlas, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, atlas_texture_);

    upload_hud_vertices(vertices);
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
