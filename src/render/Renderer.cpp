#include "app/GameBranding.h"
#include "render/Renderer.h"
#include "gameplay/SeaAdventure.h"
#include "render/ItemDropGeometry.h"
#include "render/ModelIconAtlas.h"
#include "render/ModernHudStyle.h"
#include "render/MusketHudLayout.h"
#include "render/ModernTerrainShaderSource.h"
#include "render/MsdfFontAtlas.h"
#include "render/SceneSamplerBindings.h"
#include "render/ShipMesh.h"
#include "render/ShipProtectionShaderSource.h"
#include "render/ShadowCascades.h"
#include "render/ShadowCulling.h"
#include "render/SkyShaderSource.h"
#include "render/StylizedPrimitives.h"
#include "render/StylizedShipMesh.h"
#include "render/VisualEntityPrimitives.h"
#include "creatures/CreatureGeometry.h"
#include "creatures/OldGuardGeometry.h"
#include "render/HotbarLayout.h"
#include "world/BlockVisuals.h"
#include "world/OceanAdventureLayout.h"
#include "world/OceanSimulation.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace valcraft {

namespace {

constexpr auto kShadowDistance = 96.0F;
constexpr auto kInitialVertexBufferBytes = static_cast<GLsizeiptr>(sizeof(ChunkVertex) * 256U);
constexpr auto kInitialWaterVertexBufferBytes =
    static_cast<GLsizeiptr>(sizeof(WaterVertex) * 256U);
constexpr auto kInitialIndexBufferBytes = static_cast<GLsizeiptr>(sizeof(std::uint32_t) * 384U);
constexpr auto kInitialTerrainVertexBufferBytes = static_cast<GLsizeiptr>(sizeof(TerrainVertex) * 256U);
constexpr auto kInitialTerrainIndexBufferBytes = static_cast<GLsizeiptr>(sizeof(std::uint32_t) * 384U);
constexpr std::size_t kCreatureVerticesPerBox = 24U;
constexpr std::size_t kCreatureIndicesPerBox = 36U;
constexpr std::size_t kCreatureDayBoxBudget = 30U;
constexpr std::size_t kCreatureNightBoxBudget = 96U;
constexpr std::size_t kCreatureMaxBoxBudget = kCreatureDayBoxBudget > kCreatureNightBoxBudget ? kCreatureDayBoxBudget : kCreatureNightBoxBudget;
constexpr std::size_t kCreatureMaxRenderedCount = 12U;
constexpr auto kInitialCreatureInstanceBufferBytes =
    static_cast<GLsizeiptr>(sizeof(CreaturePartInstance) *
                           (kCreatureMaxBoxBudget * kCreatureMaxRenderedCount +
                            kCrewVisualPartBudget * kCrewVisualRenderCapacity +
                            kOldGuardVisualPartBudget * kOldGuardMemberCount));
constexpr auto kInitialItemDropInstanceBufferBytes =
    static_cast<GLsizeiptr>(sizeof(ItemDropGpuInstance) * 512U);
constexpr auto kInitialPrecipitationInstanceBufferBytes =
    static_cast<GLsizeiptr>(sizeof(float) * 12U * (6000U + 96U));
constexpr auto kInitialOldGuardEffectInstanceBufferBytes =
    static_cast<GLsizeiptr>(
        sizeof(OldGuardMuzzleFlashInstance) * kOldGuardFlashCapacity +
        sizeof(OldGuardSmokeInstance) * kOldGuardSmokeCapacity);
constexpr auto kInitialHudBufferBytes = static_cast<GLsizeiptr>(sizeof(float) * 9U * 6U * 32U);
constexpr std::size_t kMaxGpuMeshEventsPerFrame = 8;
constexpr double kMaxGpuMeshSyncMsPerFrame = 1.0;
constexpr GLenum kTextureMaxAnisotropyExt = 0x84FE;
constexpr GLenum kMaxTextureMaxAnisotropyExt = 0x84FF;

// Je garde les métriques CPU près des générateurs de géométrie HUD : le
// renderer OpenGL reste l'unique propriétaire de la texture correspondante.
std::optional<MsdfFontAtlas> g_modern_hud_font_atlas {};
bool g_modern_hud_font_enabled = false;

[[nodiscard]] auto active_modern_hud_font() noexcept
    -> const MsdfFontAtlas* {
    return g_modern_hud_font_enabled &&
                   g_modern_hud_font_atlas.has_value()
               ? &*g_modern_hud_font_atlas
               : nullptr;
}

[[nodiscard]] constexpr auto visual_entity_primitive_slot(
    StylizedPrimitiveType primitive) noexcept -> std::size_t {
    switch (primitive) {
    case StylizedPrimitiveType::RoundedBox:
        return 0U;
    case StylizedPrimitiveType::Capsule:
        return 1U;
    case StylizedPrimitiveType::Ellipsoid:
        return 2U;
    case StylizedPrimitiveType::TaperedCylinder:
        return 3U;
    case StylizedPrimitiveType::Panel:
        return 4U;
    case StylizedPrimitiveType::Ribbon:
        return 5U;
    }
    return 0U;
}

[[nodiscard]] constexpr auto visual_entity_lod_slot(
    StylizedPrimitiveLod lod) noexcept -> std::size_t {
    switch (lod) {
    case StylizedPrimitiveLod::Low:
        return 0U;
    case StylizedPrimitiveLod::Medium:
        return 1U;
    case StylizedPrimitiveLod::High:
        return 2U;
    }
    return 1U;
}

[[nodiscard]] constexpr auto visual_entity_batch_slot(
    StylizedPrimitiveType primitive,
    StylizedPrimitiveLod lod) noexcept -> std::size_t {
    return visual_entity_lod_slot(lod) *
               kVisualEntityPrimitiveTypeCount +
           visual_entity_primitive_slot(primitive);
}

[[nodiscard]] constexpr auto visual_entity_primitive_for_slot(
    std::size_t slot) noexcept -> StylizedPrimitiveType {
    switch (slot % kVisualEntityPrimitiveTypeCount) {
    case 1U:
        return StylizedPrimitiveType::Capsule;
    case 2U:
        return StylizedPrimitiveType::Ellipsoid;
    case 3U:
        return StylizedPrimitiveType::TaperedCylinder;
    case 4U:
        return StylizedPrimitiveType::Panel;
    case 5U:
        return StylizedPrimitiveType::Ribbon;
    case 0U:
    default:
        return StylizedPrimitiveType::RoundedBox;
    }
}

[[nodiscard]] constexpr auto visual_entity_lod_for_slot(
    std::size_t slot) noexcept -> StylizedPrimitiveLod {
    switch (slot / kVisualEntityPrimitiveTypeCount) {
    case 0U:
        return StylizedPrimitiveLod::Low;
    case 2U:
        return StylizedPrimitiveLod::High;
    case 1U:
    default:
        return StylizedPrimitiveLod::Medium;
    }
}

[[nodiscard]] auto supports_gl_extension(std::string_view requested) noexcept
    -> bool {
    GLint extension_count = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &extension_count);
    for (GLint index = 0; index < extension_count; ++index) {
        const auto* extension = reinterpret_cast<const char*>(
            glGetStringi(GL_EXTENSIONS, static_cast<GLuint>(index)));
        if (extension != nullptr && requested == extension) {
            return true;
        }
    }
    return false;
}

struct BoxTemplateVertex {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    float nx = 0.0F;
    float ny = 1.0F;
    float nz = 0.0F;
    float u = 0.0F;
    float v = 0.0F;
    float face_index = 0.0F;
};

auto box_template_vertices() -> const std::array<BoxTemplateVertex, kCreatureVerticesPerBox>& {
    static const std::array<BoxTemplateVertex, kCreatureVerticesPerBox> kVertices {{
        {0.5F, -0.5F, -0.5F, 1.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F},
        {0.5F, 0.5F, -0.5F, 1.0F, 0.0F, 0.0F, 1.0F, 1.0F, 0.0F},
        {0.5F, 0.5F, 0.5F, 1.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F},
        {0.5F, -0.5F, 0.5F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F},

        {-0.5F, -0.5F, 0.5F, -1.0F, 0.0F, 0.0F, 1.0F, 0.0F, 1.0F},
        {-0.5F, 0.5F, 0.5F, -1.0F, 0.0F, 0.0F, 1.0F, 1.0F, 1.0F},
        {-0.5F, 0.5F, -0.5F, -1.0F, 0.0F, 0.0F, 0.0F, 1.0F, 1.0F},
        {-0.5F, -0.5F, -0.5F, -1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F},

        {-0.5F, 0.5F, 0.5F, 0.0F, 1.0F, 0.0F, 1.0F, 0.0F, 2.0F},
        {0.5F, 0.5F, 0.5F, 0.0F, 1.0F, 0.0F, 1.0F, 1.0F, 2.0F},
        {0.5F, 0.5F, -0.5F, 0.0F, 1.0F, 0.0F, 0.0F, 1.0F, 2.0F},
        {-0.5F, 0.5F, -0.5F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 2.0F},

        {-0.5F, -0.5F, -0.5F, 0.0F, -1.0F, 0.0F, 1.0F, 0.0F, 3.0F},
        {0.5F, -0.5F, -0.5F, 0.0F, -1.0F, 0.0F, 1.0F, 1.0F, 3.0F},
        {0.5F, -0.5F, 0.5F, 0.0F, -1.0F, 0.0F, 0.0F, 1.0F, 3.0F},
        {-0.5F, -0.5F, 0.5F, 0.0F, -1.0F, 0.0F, 0.0F, 0.0F, 3.0F},

        {0.5F, -0.5F, 0.5F, 0.0F, 0.0F, 1.0F, 1.0F, 0.0F, 4.0F},
        {0.5F, 0.5F, 0.5F, 0.0F, 0.0F, 1.0F, 1.0F, 1.0F, 4.0F},
        {-0.5F, 0.5F, 0.5F, 0.0F, 0.0F, 1.0F, 0.0F, 1.0F, 4.0F},
        {-0.5F, -0.5F, 0.5F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 4.0F},

        {-0.5F, -0.5F, -0.5F, 0.0F, 0.0F, -1.0F, 1.0F, 0.0F, 5.0F},
        {-0.5F, 0.5F, -0.5F, 0.0F, 0.0F, -1.0F, 1.0F, 1.0F, 5.0F},
        {0.5F, 0.5F, -0.5F, 0.0F, 0.0F, -1.0F, 0.0F, 1.0F, 5.0F},
        {0.5F, -0.5F, -0.5F, 0.0F, 0.0F, -1.0F, 0.0F, 0.0F, 5.0F},
    }};
    return kVertices;
}

auto box_template_indices() -> const std::array<std::uint32_t, kCreatureIndicesPerBox>& {
    static const std::array<std::uint32_t, kCreatureIndicesPerBox> kIndices {{
        0U, 1U, 2U, 0U, 2U, 3U,
        4U, 5U, 6U, 4U, 6U, 7U,
        8U, 9U, 10U, 8U, 10U, 11U,
        12U, 13U, 14U, 12U, 14U, 15U,
        16U, 17U, 18U, 16U, 18U, 19U,
        20U, 21U, 22U, 20U, 22U, 23U,
    }};
    return kIndices;
}

auto grow_buffer_capacity(GLsizeiptr current_bytes, GLsizeiptr required_bytes, GLsizeiptr minimum_bytes) -> GLsizeiptr {
    auto capacity = std::max(current_bytes, minimum_bytes);
    while (capacity < required_bytes) {
        capacity = std::max(capacity * 2, required_bytes);
    }
    return capacity;
}

struct ColorTargetFormat {
    GLint internal_format = GL_RGBA16F;
    GLenum pixel_format = GL_RGBA;
    GLenum pixel_type = GL_FLOAT;
};

auto color_target_format(const RendererQualitySettings& quality_settings) noexcept -> ColorTargetFormat {
    if (quality_settings.high_precision_hdr) {
        return {};
    }
    return {GL_R11F_G11F_B10F, GL_RGB, GL_UNSIGNED_INT_10F_11F_11F_REV};
}

auto finite_vec3(const glm::vec3& value) noexcept -> bool {
    return std::isfinite(value.x) &&
           std::isfinite(value.y) &&
           std::isfinite(value.z);
}

auto finite_matrix(const glm::mat4& value) noexcept -> bool {
    for (glm::length_t column = 0; column < 4; ++column) {
        for (glm::length_t row = 0; row < 4; ++row) {
            if (!std::isfinite(value[column][row])) {
                return false;
            }
        }
    }
    return true;
}

auto finite_saturate(float value) noexcept -> float {
    return std::isfinite(value)
               ? std::clamp(
                     value,
                     0.0F,
                     1.0F)
               : 0.0F;
}

auto safe_direction(
    const glm::vec3& direction,
    const glm::vec3& fallback) noexcept
    -> glm::vec3 {
    if (!finite_vec3(direction)) {
        return fallback;
    }
    const auto length_squared =
        glm::dot(
            direction,
            direction);
    if (!std::isfinite(length_squared) ||
        length_squared <= 1.0e-6F) {
        return fallback;
    }
    return direction /
           std::sqrt(
               length_squared);
}

auto sanitize_weather_for_rendering(
    const EnvironmentState& source) noexcept
    -> EnvironmentState {
    auto state = source;
    state.cloud_intensity =
        finite_saturate(
            source.cloud_intensity);
    state.overcast_intensity =
        finite_saturate(
            source.overcast_intensity);
    state.precipitation_intensity =
        finite_saturate(
            source.precipitation_intensity);
    state.storm_intensity =
        finite_saturate(
            source.storm_intensity);
    state.violent_storm_intensity =
        finite_saturate(
            source.violent_storm_intensity);
    state.lightning_intensity =
        finite_saturate(
            source.lightning_intensity);
    state.lightning_bolt_intensity =
        finite_saturate(
            source.lightning_bolt_intensity);
    state.lightning_shape_seed =
        finite_saturate(
            source.lightning_shape_seed);
    state.weather_transition_factor =
        finite_saturate(
            source.weather_transition_factor);
    state.cloud_shadow_strength =
        finite_saturate(
            source.cloud_shadow_strength);
    state.wind_strength =
        finite_saturate(
            source.wind_strength);
    state.weather_time_seconds =
        std::isfinite(
            source.weather_time_seconds)
            ? std::max(
                  source.weather_time_seconds,
                  0.0F)
            : 0.0F;
    state.lightning_direction =
        safe_direction(
            source.lightning_direction,
            {0.0F, 0.35F, 0.93675F});
    state.sun_direction =
        safe_direction(
            source.sun_direction,
            {0.0F, 1.0F, 0.0F});

    const auto wind_length_squared =
        glm::dot(
            source.wind_direction_xz,
            source.wind_direction_xz);
    state.wind_direction_xz =
        std::isfinite(
            source.wind_direction_xz.x) &&
                std::isfinite(
                    source.wind_direction_xz.y) &&
                std::isfinite(
                    wind_length_squared) &&
                wind_length_squared > 1.0e-6F
            ? source.wind_direction_xz /
                  std::sqrt(
                      wind_length_squared)
            : glm::vec2 {0.0F, 1.0F};
    return state;
}

struct ScopedPrecipitationGlState {
    GLboolean depth_test_enabled = GL_FALSE;
    GLboolean cull_face_enabled = GL_FALSE;
    GLboolean blend_enabled = GL_FALSE;
    GLboolean depth_write_enabled = GL_TRUE;
    GLint blend_source_rgb = GL_ONE;
    GLint blend_destination_rgb = GL_ZERO;
    GLint blend_source_alpha = GL_ONE;
    GLint blend_destination_alpha = GL_ZERO;
    GLint current_program = 0;
    GLint vertex_array = 0;
    GLint array_buffer = 0;

    ScopedPrecipitationGlState() noexcept {
        depth_test_enabled = glIsEnabled(GL_DEPTH_TEST);
        cull_face_enabled = glIsEnabled(GL_CULL_FACE);
        blend_enabled = glIsEnabled(GL_BLEND);
        glGetBooleanv(
            GL_DEPTH_WRITEMASK,
            &depth_write_enabled);
        glGetIntegerv(
            GL_BLEND_SRC_RGB,
            &blend_source_rgb);
        glGetIntegerv(
            GL_BLEND_DST_RGB,
            &blend_destination_rgb);
        glGetIntegerv(
            GL_BLEND_SRC_ALPHA,
            &blend_source_alpha);
        glGetIntegerv(
            GL_BLEND_DST_ALPHA,
            &blend_destination_alpha);
        glGetIntegerv(
            GL_CURRENT_PROGRAM,
            &current_program);
        glGetIntegerv(
            GL_VERTEX_ARRAY_BINDING,
            &vertex_array);
        glGetIntegerv(
            GL_ARRAY_BUFFER_BINDING,
            &array_buffer);
    }

    ScopedPrecipitationGlState(
        const ScopedPrecipitationGlState&) = delete;
    auto operator=(
        const ScopedPrecipitationGlState&)
        -> ScopedPrecipitationGlState& = delete;

    ~ScopedPrecipitationGlState() noexcept {
        // Je restitue chaque etat que la passe modifie afin que les objets
        // suivants ne dependent jamais d'une hypothese sur l'etat precedent.
        set_capability(
            GL_DEPTH_TEST,
            depth_test_enabled);
        set_capability(
            GL_CULL_FACE,
            cull_face_enabled);
        set_capability(
            GL_BLEND,
            blend_enabled);
        glDepthMask(
            depth_write_enabled);
        glBlendFuncSeparate(
            static_cast<GLenum>(
                blend_source_rgb),
            static_cast<GLenum>(
                blend_destination_rgb),
            static_cast<GLenum>(
                blend_source_alpha),
            static_cast<GLenum>(
                blend_destination_alpha));
        glUseProgram(
            static_cast<GLuint>(
                current_program));
        glBindVertexArray(
            static_cast<GLuint>(
                vertex_array));
        glBindBuffer(
            GL_ARRAY_BUFFER,
            static_cast<GLuint>(
                array_buffer));
    }

private:
    static void set_capability(
        GLenum capability,
        GLboolean enabled) noexcept {
        if (enabled == GL_TRUE) {
            glEnable(
                capability);
        } else {
            glDisable(
                capability);
        }
    }
};

auto ship_protection_is_renderable(const ShipRenderState& ship) noexcept -> bool {
    if (!ship.visible ||
        ship.blueprint == nullptr ||
        !finite_matrix(ship.model_matrix) ||
        !finite_vec3(ship.world_bounds.min) ||
        !finite_vec3(ship.world_bounds.max)) {
        return false;
    }
    const auto& profile = ship.blueprint->protection_profile;
    return profile.maximum_half_width > 0.0F &&
           profile.stern_z < profile.bow_z &&
           profile.lower_hull_min_y < profile.main_deck_top_y;
}

auto ray_aabb_entry_distance(const glm::vec3& origin,
                             const glm::vec3& direction,
                             const glm::vec3& min_corner,
                             const glm::vec3& max_corner,
                             float max_distance) noexcept
    -> std::optional<float> {
    auto entry = 0.0F;
    auto exit = max_distance;
    for (glm::length_t axis = 0; axis < 3; ++axis) {
        if (std::abs(direction[axis]) <= 1.0e-6F) {
            if (origin[axis] < min_corner[axis] ||
                origin[axis] > max_corner[axis]) {
                return std::nullopt;
            }
            continue;
        }

        const auto inverse_direction = 1.0F / direction[axis];
        auto first = (min_corner[axis] - origin[axis]) * inverse_direction;
        auto second = (max_corner[axis] - origin[axis]) * inverse_direction;
        if (first > second) {
            std::swap(first, second);
        }
        entry = std::max(entry, first);
        exit = std::min(exit, second);
        if (entry > exit) {
            return std::nullopt;
        }
    }
    return entry >= 0.0F && entry <= max_distance
               ? std::optional<float> {entry}
               : std::nullopt;
}

void orphan_bound_buffer(GLenum target, GLsizeiptr capacity, GLenum usage = GL_STREAM_DRAW) {
    if (capacity > 0) {
        // Je donne un nouveau stockage au pilote avant chaque écriture dynamique pour éviter d'attendre le GPU.
        glBufferData(target, capacity, nullptr, usage);
    }
}

void merge_chunk_mesh_sections_into(
    const std::array<ChunkMeshData, kChunkSectionCount>& sections,
    ChunkMeshData& merged) {
    merged.vertices.clear();
    merged.indices.clear();
    merged.water_vertices.clear();
    merged.water_indices.clear();
    merged.face_count = 0U;
    merged.water_face_count = 0U;

    std::size_t vertex_count = 0U;
    std::size_t index_count = 0U;
    std::size_t water_vertex_count = 0U;
    std::size_t water_index_count = 0U;
    for (const auto& section : sections) {
        vertex_count += section.vertices.size();
        index_count += section.indices.size();
        water_vertex_count += section.water_vertices.size();
        water_index_count += section.water_indices.size();
    }
    merged.vertices.reserve(vertex_count);
    merged.indices.reserve(index_count);
    merged.water_vertices.reserve(water_vertex_count);
    merged.water_indices.reserve(water_index_count);

    for (const auto& section : sections) {
        const auto vertex_offset = static_cast<std::uint32_t>(merged.vertices.size());
        merged.vertices.insert(merged.vertices.end(), section.vertices.begin(), section.vertices.end());
        for (const auto index : section.indices) {
            merged.indices.push_back(index + vertex_offset);
        }

        const auto water_vertex_offset = static_cast<std::uint32_t>(merged.water_vertices.size());
        merged.water_vertices.insert(
            merged.water_vertices.end(),
            section.water_vertices.begin(),
            section.water_vertices.end());
        for (const auto index : section.water_indices) {
            merged.water_indices.push_back(index + water_vertex_offset);
        }
        merged.face_count += section.face_count;
        merged.water_face_count += section.water_face_count;
    }
}

void merge_organic_terrain_sections_into(
    const std::array<OrganicTerrainMesh, kChunkSectionCount>& sections,
    OrganicTerrainMesh& merged) {
    merged.vertices.clear();
    merged.indices.clear();
    merged.quad_count = 0U;

    std::size_t vertex_count = 0U;
    std::size_t index_count = 0U;
    for (const auto& section : sections) {
        vertex_count += section.vertices.size();
        index_count += section.indices.size();
    }
    merged.vertices.reserve(vertex_count);
    merged.indices.reserve(index_count);

    for (const auto& section : sections) {
        const auto vertex_offset = static_cast<std::uint32_t>(merged.vertices.size());
        merged.vertices.insert(
            merged.vertices.end(),
            section.vertices.begin(),
            section.vertices.end());
        for (const auto index : section.indices) {
            merged.indices.push_back(index + vertex_offset);
        }
        merged.quad_count += section.quad_count;
    }
}

void merge_architectural_sections_into(
    const std::array<ArchitecturalMesh, kChunkSectionCount>& sections,
    ArchitecturalMesh& merged) {

    merged = {};
    std::size_t vertex_count = 0U;
    std::size_t index_count = 0U;
    std::size_t quad_count = 0U;
    std::size_t fixture_count = 0U;
    for (const auto& section : sections) {
        vertex_count += section.vertices.size();
        index_count += section.indices.size();
        quad_count += section.quads.size();
        fixture_count += section.fixtures.size();
    }
    merged.vertices.reserve(vertex_count);
    merged.indices.reserve(index_count);
    merged.quads.reserve(quad_count);
    merged.fixtures.reserve(fixture_count);

    for (const auto& section : sections) {
        const auto vertex_offset =
            static_cast<std::uint32_t>(merged.vertices.size());
        const auto index_offset =
            static_cast<std::uint32_t>(merged.indices.size());
        merged.vertices.insert(
            merged.vertices.end(),
            section.vertices.begin(),
            section.vertices.end());
        for (const auto index : section.indices) {
            merged.indices.push_back(index + vertex_offset);
        }
        for (auto quad : section.quads) {
            quad.first_vertex += vertex_offset;
            quad.first_index += index_offset;
            merged.quads.push_back(quad);
        }
        merged.fixtures.insert(
            merged.fixtures.end(),
            section.fixtures.begin(),
            section.fixtures.end());

        if (!section.bounds.valid) {
            continue;
        }
        if (!merged.bounds.valid) {
            merged.bounds = section.bounds;
            continue;
        }
        merged.bounds.min_x = std::min(merged.bounds.min_x, section.bounds.min_x);
        merged.bounds.min_y = std::min(merged.bounds.min_y, section.bounds.min_y);
        merged.bounds.min_z = std::min(merged.bounds.min_z, section.bounds.min_z);
        merged.bounds.max_x = std::max(merged.bounds.max_x, section.bounds.max_x);
        merged.bounds.max_y = std::max(merged.bounds.max_y, section.bounds.max_y);
        merged.bounds.max_z = std::max(merged.bounds.max_z, section.bounds.max_z);
    }
}

[[nodiscard]] constexpr auto color_target_bytes_per_pixel(GLint internal_format) noexcept -> std::uint64_t {
    return internal_format == GL_RGBA16F ? 8U : 4U;
}

void configure_box_template_attributes(GLuint vao, GLuint vbo, GLuint ebo) {
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(BoxTemplateVertex), reinterpret_cast<void*>(offsetof(BoxTemplateVertex, x)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(BoxTemplateVertex), reinterpret_cast<void*>(offsetof(BoxTemplateVertex, nx)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(BoxTemplateVertex), reinterpret_cast<void*>(offsetof(BoxTemplateVertex, u)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(BoxTemplateVertex), reinterpret_cast<void*>(offsetof(BoxTemplateVertex, face_index)));
}

void configure_creature_instance_attributes(GLuint vao, GLuint instance_vbo) {
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, instance_vbo);

    constexpr GLuint kTransformLocation = 4;
    for (GLuint column = 0; column < 4; ++column) {
        const auto location = kTransformLocation + column;
        glEnableVertexAttribArray(location);
        glVertexAttribPointer(
            location,
            4,
            GL_FLOAT,
            GL_FALSE,
            sizeof(CreaturePartInstance),
            reinterpret_cast<void*>(offsetof(CreaturePartInstance, transform) + sizeof(glm::vec4) * column));
        glVertexAttribDivisor(location, 1);
    }

    constexpr GLuint kUvLocation = 8;
    for (GLuint face_index = 0; face_index < 6; ++face_index) {
        const auto location = kUvLocation + face_index;
        glEnableVertexAttribArray(location);
        glVertexAttribPointer(
            location,
            4,
            GL_FLOAT,
            GL_FALSE,
            sizeof(CreaturePartInstance),
            reinterpret_cast<void*>(offsetof(CreaturePartInstance, face_uvs) + sizeof(BoxUvRect) * face_index));
        glVertexAttribDivisor(location, 1);
    }

    glEnableVertexAttribArray(14);
    glVertexAttribPointer(14, 4, GL_FLOAT, GL_FALSE, sizeof(CreaturePartInstance), reinterpret_cast<void*>(offsetof(CreaturePartInstance, nightmare_factor)));
    glVertexAttribDivisor(14, 1);

    glEnableVertexAttribArray(15);
    glVertexAttribPointer(15, 4, GL_FLOAT, GL_FALSE, sizeof(CreaturePartInstance), reinterpret_cast<void*>(offsetof(CreaturePartInstance, emissive_strength)));
    glVertexAttribDivisor(15, 1);
}

void configure_item_drop_instance_attributes(GLuint vao, GLuint instance_vbo) {
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, instance_vbo);

    constexpr GLuint kTransformLocation = 4;
    for (GLuint column = 0; column < 4; ++column) {
        const auto location = kTransformLocation + column;
        glEnableVertexAttribArray(location);
        glVertexAttribPointer(
            location,
            4,
            GL_FLOAT,
            GL_FALSE,
            sizeof(ItemDropGpuInstance),
            reinterpret_cast<void*>(
                offsetof(ItemDropGpuInstance, transform) +
                sizeof(glm::vec4) * column));
        glVertexAttribDivisor(location, 1);
    }

    glEnableVertexAttribArray(8);
    glVertexAttribIPointer(8, 1, GL_UNSIGNED_BYTE, sizeof(ItemDropGpuInstance), reinterpret_cast<void*>(offsetof(ItemDropGpuInstance, block_id)));
    glVertexAttribDivisor(8, 1);

    glEnableVertexAttribArray(9);
    glVertexAttribPointer(9, 1, GL_FLOAT, GL_FALSE, sizeof(ItemDropGpuInstance), reinterpret_cast<void*>(offsetof(ItemDropGpuInstance, sky_light)));
    glVertexAttribDivisor(9, 1);

    glEnableVertexAttribArray(10);
    glVertexAttribPointer(10, 1, GL_FLOAT, GL_FALSE, sizeof(ItemDropGpuInstance), reinterpret_cast<void*>(offsetof(ItemDropGpuInstance, block_light)));
    glVertexAttribDivisor(10, 1);

    glEnableVertexAttribArray(11);
    glVertexAttribPointer(11, 1, GL_FLOAT, GL_FALSE, sizeof(ItemDropGpuInstance), reinterpret_cast<void*>(offsetof(ItemDropGpuInstance, material_class)));
    glVertexAttribDivisor(11, 1);

    glEnableVertexAttribArray(12);
    glVertexAttribPointer(12, 4, GL_FLOAT, GL_FALSE, sizeof(ItemDropGpuInstance), reinterpret_cast<void*>(offsetof(ItemDropGpuInstance, face_tiles_0_1)));
    glVertexAttribDivisor(12, 1);

    glEnableVertexAttribArray(13);
    glVertexAttribPointer(13, 4, GL_FLOAT, GL_FALSE, sizeof(ItemDropGpuInstance), reinterpret_cast<void*>(offsetof(ItemDropGpuInstance, face_tiles_2_3)));
    glVertexAttribDivisor(13, 1);

    glEnableVertexAttribArray(14);
    glVertexAttribPointer(14, 4, GL_FLOAT, GL_FALSE, sizeof(ItemDropGpuInstance), reinterpret_cast<void*>(offsetof(ItemDropGpuInstance, face_tiles_4_5)));
    glVertexAttribDivisor(14, 1);
}

auto quantize_hud_value(float value, float steps_per_unit) -> int {
    return static_cast<int>(std::lround(value * steps_per_unit));
}

auto format_save_slot_timestamp(std::uint64_t unix_seconds) -> std::string {
    if (unix_seconds == 0) {
        return "AUCUNE SAUVEGARDE";
    }

    const auto time_value = static_cast<std::time_t>(unix_seconds);
    std::tm local_time {};
#ifdef _WIN32
    localtime_s(&local_time, &time_value);
#else
    localtime_r(&time_value, &local_time);
#endif

    std::ostringstream stream;
    stream << std::setfill('0')
           << std::setw(2) << local_time.tm_mday << "/"
           << std::setw(2) << (local_time.tm_mon + 1) << "  "
           << std::setw(2) << local_time.tm_hour << ":"
           << std::setw(2) << local_time.tm_min;
    return stream.str();
}

auto format_save_slot_seed(int seed) -> std::string {
    return std::string("SEED ") + std::to_string(seed);
}

auto format_save_slot_time(float time_of_day) -> std::string {
    const auto hours = static_cast<int>(std::floor(time_of_day));
    const auto minutes = static_cast<int>(std::round((time_of_day - static_cast<float>(hours)) * 60.0F));
    std::ostringstream stream;
    stream << "HEURE "
           << std::setfill('0') << std::setw(2) << hours
           << ":"
           << std::setfill('0') << std::setw(2) << (minutes % 60);
    return stream.str();
}

auto format_save_slot_mode(GameMode mode) -> std::string {
    return std::string(game_mode_label(mode));
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

void append_hud_solid_triangle_top_left(
    std::vector<HudVertex>& vertices,
    float viewport_width,
    float viewport_height,
    const glm::vec2& first,
    const glm::vec2& second,
    const glm::vec2& third,
    const std::array<float, 4>& color) {
    const auto make_vertex = [&](const glm::vec2& point) {
        return HudVertex {
            pixel_to_ndc_x(point.x, viewport_width),
            pixel_to_ndc_y(
                viewport_height - point.y,
                viewport_height),
            0.0F,
            0.0F,
            color[0],
            color[1],
            color[2],
            color[3],
            0.0F,
        };
    };
    vertices.push_back(make_vertex(first));
    vertices.push_back(make_vertex(second));
    vertices.push_back(make_vertex(third));
}

void append_hud_rounded_rect_top_left(
    std::vector<HudVertex>& vertices,
    float viewport_width,
    float viewport_height,
    float x,
    float y,
    float width,
    float height,
    float preferred_radius,
    const std::array<float, 4>& color) {
    if (viewport_width <= 0.0F ||
        viewport_height <= 0.0F ||
        width <= 0.0F ||
        height <= 0.0F ||
        color[3] <= 0.0F) {
        return;
    }

    const auto metrics = modern_hud_rounded_rect_metrics(
        width,
        height,
        preferred_radius);
    if (metrics.corner_segments <= 0 ||
        metrics.radius <= 0.0F) {
        append_hud_rect_top_left(
            vertices,
            viewport_width,
            viewport_height,
            x,
            y,
            width,
            height,
            color);
        return;
    }

    const auto radius = metrics.radius;
    const auto center_width =
        std::max(0.0F, width - radius * 2.0F);
    const auto middle_height =
        std::max(0.0F, height - radius * 2.0F);
    if (center_width > 0.0F) {
        append_hud_rect_top_left(
            vertices,
            viewport_width,
            viewport_height,
            x + radius,
            y,
            center_width,
            height,
            color);
    }
    if (middle_height > 0.0F) {
        append_hud_rect_top_left(
            vertices,
            viewport_width,
            viewport_height,
            x,
            y + radius,
            radius,
            middle_height,
            color);
        append_hud_rect_top_left(
            vertices,
            viewport_width,
            viewport_height,
            x + width - radius,
            y + radius,
            radius,
            middle_height,
            color);
    }

    constexpr float kPi = 3.14159265358979323846F;
    const std::array<glm::vec2, 4> centers {{
        {x + radius, y + radius},
        {x + width - radius, y + radius},
        {x + width - radius, y + height - radius},
        {x + radius, y + height - radius},
    }};
    constexpr std::array<float, 4> start_angles {{
        kPi,
        kPi * 1.5F,
        0.0F,
        kPi * 0.5F,
    }};
    const auto angle_step =
        (kPi * 0.5F) /
        static_cast<float>(metrics.corner_segments);
    for (std::size_t corner = 0U;
         corner < centers.size();
         ++corner) {
        const auto center = centers[corner];
        for (int segment = 0;
             segment < metrics.corner_segments;
             ++segment) {
            const auto first_angle =
                start_angles[corner] +
                angle_step * static_cast<float>(segment);
            const auto second_angle =
                first_angle + angle_step;
            const auto first = center + glm::vec2 {
                std::cos(first_angle) * radius,
                std::sin(first_angle) * radius,
            };
            const auto second = center + glm::vec2 {
                std::cos(second_angle) * radius,
                std::sin(second_angle) * radius,
            };
            append_hud_solid_triangle_top_left(
                vertices,
                viewport_width,
                viewport_height,
                center,
                first,
                second,
                color);
        }
    }
}

[[maybe_unused]] void append_hud_rounded_rect_bottom_left(
    std::vector<HudVertex>& vertices,
    float viewport_width,
    float viewport_height,
    float x,
    float bottom,
    float width,
    float height,
    float radius,
    const std::array<float, 4>& color) {
    append_hud_rounded_rect_top_left(
        vertices,
        viewport_width,
        viewport_height,
        x,
        bottom_to_top_left_y(
            viewport_height,
            bottom,
            height),
        width,
        height,
        radius,
        color);
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
    case '%': return {{0x18, 0x19, 0x02, 0x04, 0x08, 0x13, 0x03}};
    case '\'': return {{0x06, 0x06, 0x04, 0x08, 0x00, 0x00, 0x00}};
    case ',': return {{0x00, 0x00, 0x00, 0x00, 0x06, 0x06, 0x04}};
    case ':': return {{0x00, 0x06, 0x06, 0x00, 0x06, 0x06, 0x00}};
    case '!': return {{0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04}};
    case '?': return {{0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04}};
    case '.': return {{0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x06}};
    case '-': return {{0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00}};
    case '/': return {{0x01, 0x02, 0x02, 0x04, 0x08, 0x08, 0x10}};
    case '>': return {{0x10, 0x08, 0x04, 0x02, 0x04, 0x08, 0x10}};
    case '_': return {{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F}};
    default: return {{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}};
    }
}

auto measure_pixel_text(std::string_view text, float pixel_size) -> float {
    if (const auto* font = active_modern_hud_font();
        font != nullptr && pixel_size > 0.0F) {
        const auto layout = font->build_quads(
            text,
            0.0F,
            0.0F,
            pixel_size * 7.0F);
        return layout.width;
    }

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
    if (const auto* font = active_modern_hud_font();
        font != nullptr && pixel_size > 0.0F) {
        const auto requested_height = pixel_size * 7.0F;
        const auto scale =
            requested_height / font->metadata().font_em_pixels;
        auto origin_x = x;
        const auto measured = font->build_quads(
            text,
            0.0F,
            0.0F,
            requested_height);
        if (centered) {
            origin_x -= measured.width * 0.5F;
        }
        const auto baseline_y =
            y + font->metadata().ascent * scale;
        const auto layout = font->build_quads(
            text,
            origin_x,
            baseline_y,
            requested_height);
        for (const auto& quad : layout.quads) {
            const auto quad_width = quad.x1 - quad.x0;
            const auto quad_height = quad.y1 - quad.y0;
            if (quad_width <= 0.0F || quad_height <= 0.0F) {
                continue;
            }
            // L'asset est rangé ligne par ligne depuis le haut. J'inverse V
            // ici une seule fois afin de respecter l'origine OpenGL.
            append_hud_quad_top_left(
                vertices,
                viewport_width,
                viewport_height,
                quad.x0,
                quad.y0,
                quad_width,
                quad_height,
                color,
                {quad.u0, quad.v1, quad.u1, quad.v0},
                2.0F);
        }
        return;
    }

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

using HudColor = std::array<float, 4>;

struct HudPanelPalette {
    HudColor frame {};
    HudColor fill {};
    HudColor highlight {};
    HudColor shadow {};
    HudColor trim {};
};

struct HudSlotPalette {
    HudPanelPalette shell {};
    HudColor accent {};
    HudColor glow {};
    HudColor motif {};
};

struct InventoryFocusItem {
    HotbarSlot slot {};
    bool has_item = false;
    bool from_carried_slot = false;
    InventorySlotGroup group = InventorySlotGroup::Storage;
};

auto hud_with_alpha(const HudColor& color, float alpha) -> HudColor {
    return {color[0], color[1], color[2], alpha};
}

auto hud_scale_rgb(const HudColor& color, float factor) -> HudColor {
    return {
        std::clamp(color[0] * factor, 0.0F, 1.0F),
        std::clamp(color[1] * factor, 0.0F, 1.0F),
        std::clamp(color[2] * factor, 0.0F, 1.0F),
        color[3],
    };
}

auto hud_mix(const HudColor& lhs, const HudColor& rhs, float t) -> HudColor {
    const auto blend = std::clamp(t, 0.0F, 1.0F);
    const auto inverse = 1.0F - blend;
    return {
        lhs[0] * inverse + rhs[0] * blend,
        lhs[1] * inverse + rhs[1] * blend,
        lhs[2] * inverse + rhs[2] * blend,
        lhs[3] * inverse + rhs[3] * blend,
    };
}

auto make_slate_panel_palette() -> HudPanelPalette {
    return {{0.06F, 0.07F, 0.08F, 0.98F},
            {0.15F, 0.16F, 0.18F, 0.94F},
            {0.44F, 0.46F, 0.50F, 0.22F},
            {0.02F, 0.02F, 0.03F, 0.58F},
            {0.72F, 0.74F, 0.78F, 0.10F}};
}

auto make_stone_panel_palette() -> HudPanelPalette {
    return {{0.08F, 0.08F, 0.10F, 0.98F},
            {0.22F, 0.23F, 0.26F, 0.95F},
            {0.66F, 0.68F, 0.72F, 0.18F},
            {0.03F, 0.03F, 0.04F, 0.62F},
            {0.86F, 0.88F, 0.92F, 0.07F}};
}

auto make_header_panel_palette() -> HudPanelPalette {
    return {{0.05F, 0.05F, 0.06F, 0.98F},
            {0.28F, 0.29F, 0.32F, 0.96F},
            {0.90F, 0.92F, 0.96F, 0.20F},
            {0.03F, 0.03F, 0.04F, 0.68F},
            {0.98F, 0.88F, 0.62F, 0.12F}};
}

auto make_warm_panel_palette(const HudColor& accent) -> HudPanelPalette {
    return {
        {0.10F, 0.08F, 0.05F, 0.98F},
        hud_mix(HudColor {0.20F, 0.16F, 0.12F, 0.96F}, hud_with_alpha(accent, 0.96F), 0.18F),
        hud_with_alpha(hud_scale_rgb(accent, 1.20F), 0.24F),
        {0.02F, 0.02F, 0.02F, 0.66F},
        hud_with_alpha(hud_scale_rgb(accent, 1.10F), 0.14F),
    };
}

auto make_modern_glass_panel_palette(
    const HudColor& accent,
    float accent_strength = 0.12F) -> HudPanelPalette {
    const auto clamped_strength =
        std::clamp(accent_strength, 0.0F, 0.35F);
    return {
        hud_mix(
            HudColor {0.16F, 0.20F, 0.25F, 0.82F},
            hud_with_alpha(accent, 0.82F),
            clamped_strength * 0.55F),
        hud_mix(
            HudColor {0.075F, 0.10F, 0.14F, 0.78F},
            hud_with_alpha(accent, 0.78F),
            clamped_strength),
        {0.82F, 0.90F, 0.98F, 0.12F},
        {0.01F, 0.02F, 0.035F, 0.32F},
        hud_with_alpha(
            hud_scale_rgb(accent, 1.08F),
            0.28F),
    };
}

auto make_modern_neutral_panel_palette() -> HudPanelPalette {
    return make_modern_glass_panel_palette(
        {0.48F, 0.68F, 0.86F, 1.0F},
        0.08F);
}

auto ui_material_accent(BlockId block_id) -> HudColor {
    if (block_id == to_block_id(BlockType::Air)) {
        return {0.56F, 0.60F, 0.66F, 1.0F};
    }
    if (is_resource_ore(block_id)) {
        switch (static_cast<BlockType>(block_item_id(block_id))) {
        case BlockType::CoalOre:
            return {0.32F, 0.32F, 0.34F, 1.0F};
        case BlockType::IronOre:
            return {0.86F, 0.50F, 0.28F, 1.0F};
        case BlockType::GoldOre:
            return {0.98F, 0.76F, 0.28F, 1.0F};
        case BlockType::DiamondOre:
            return {0.36F, 0.82F, 0.90F, 1.0F};
        case BlockType::MetallicAlloyOre:
            return {0.74F, 0.68F, 0.94F, 1.0F};
        case BlockType::Air:
        default:
            return {0.63F, 0.67F, 0.74F, 1.0F};
        }
    }
    if (is_weapon_item(block_id)) {
        return {0.82F, 0.80F, 0.74F, 1.0F};
    }
    if (is_tool_item(block_id)) {
        return {0.72F, 0.76F, 0.74F, 1.0F};
    }
    if (is_inventory_only_item(block_id)) {
        switch (static_cast<BlockType>(block_item_id(block_id))) {
        case BlockType::Pastron:
        case BlockType::RoundShield:
            return {0.86F, 0.60F, 0.28F, 1.0F};
        case BlockType::Shoes:
        case BlockType::Pants:
            return {0.50F, 0.62F, 0.78F, 1.0F};
        case BlockType::Sword:
        case BlockType::Spear:
        case BlockType::Air:
        default:
            return {0.82F, 0.80F, 0.74F, 1.0F};
        }
    }

    switch (block_visual_material(block_id)) {
    case BlockVisualMaterial::Terrain:
        return {0.46F, 0.66F, 0.34F, 1.0F};
    case BlockVisualMaterial::Rock:
        return {0.63F, 0.67F, 0.74F, 1.0F};
    case BlockVisualMaterial::Sand:
        return {0.83F, 0.75F, 0.49F, 1.0F};
    case BlockVisualMaterial::Wood:
        return {0.71F, 0.52F, 0.29F, 1.0F};
    case BlockVisualMaterial::Foliage:
        return {0.36F, 0.72F, 0.38F, 1.0F};
    case BlockVisualMaterial::Flora:
        return {0.86F, 0.48F, 0.36F, 1.0F};
    case BlockVisualMaterial::Water:
        return {0.33F, 0.60F, 0.96F, 1.0F};
    case BlockVisualMaterial::Emissive:
        return {0.98F, 0.78F, 0.30F, 1.0F};
    case BlockVisualMaterial::Snow:
        return {0.90F, 0.93F, 0.98F, 1.0F};
    case BlockVisualMaterial::Glass:
        return {0.62F, 0.84F, 0.98F, 1.0F};
    default:
        return {0.56F, 0.60F, 0.66F, 1.0F};
    }
}

auto item_material_label(BlockId block_id) -> std::string_view {
    if (block_id == to_block_id(BlockType::Air)) {
        return "VIDE";
    }
    if (is_weapon_item(block_id)) {
        return "ARME";
    }
    if (is_tool_item(block_id)) {
        return "OUTIL";
    }
    if (is_inventory_only_item(block_id)) {
        return "EQUIPEMENT";
    }
    if (is_resource_ore(block_id)) {
        return "MINERAI";
    }

    switch (block_visual_material(block_id)) {
    case BlockVisualMaterial::Terrain:
        return "SOL";
    case BlockVisualMaterial::Rock:
        return "ROCHE";
    case BlockVisualMaterial::Sand:
        return "SABLE";
    case BlockVisualMaterial::Wood:
        return "BOIS";
    case BlockVisualMaterial::Foliage:
        return "FEUILLAGE";
    case BlockVisualMaterial::Flora:
        return "FLORE";
    case BlockVisualMaterial::Water:
        return "EAU";
    case BlockVisualMaterial::Emissive:
        return "LUMIERE";
    case BlockVisualMaterial::Snow:
        return "NEIGE";
    case BlockVisualMaterial::Glass:
        return "VERRE";
    default:
        return "VIDE";
    }
}

auto inventory_slot_group_label(InventorySlotGroup group) -> std::string_view {
    switch (group) {
    case InventorySlotGroup::Hotbar:
        return "BARRE RAPIDE";
    case InventorySlotGroup::Equipment:
        return "EQUIPEMENT";
    case InventorySlotGroup::Storage:
    default:
        return "SAC";
    }
}

void append_hud_shadow_top_left(std::vector<HudVertex>& vertices,
                                float viewport_width,
                                float viewport_height,
                                float x,
                                float y,
                                float width,
                                float height,
                                float spread,
                                const HudColor& color) {
    if (width <= 0.0F || height <= 0.0F || spread <= 0.0F || color[3] <= 0.0F) {
        return;
    }

    constexpr int kShadowLayerCount = 3;
    for (int layer = 0; layer < kShadowLayerCount; ++layer) {
        const auto layer_factor = static_cast<float>(layer + 1) / static_cast<float>(kShadowLayerCount);
        const auto pad = spread * layer_factor;
        const auto offset_x = pad * 0.18F;
        const auto offset_y = pad * 0.30F;
        const auto alpha = color[3] * (1.0F - static_cast<float>(layer) * 0.24F);
        append_hud_rect_top_left(
            vertices,
            viewport_width,
            viewport_height,
            x - pad * 0.35F + offset_x,
            y - pad * 0.20F + offset_y,
            width + pad * 0.70F,
            height + pad * 0.70F,
            {color[0], color[1], color[2], alpha});
    }
}

void append_hud_shadow_bottom_left(std::vector<HudVertex>& vertices,
                                   float viewport_width,
                                   float viewport_height,
                                   float x,
                                   float bottom,
                                   float width,
                                   float height,
                                   float spread,
                                   const HudColor& color) {
    append_hud_shadow_top_left(
        vertices,
        viewport_width,
        viewport_height,
        x,
        bottom_to_top_left_y(viewport_height, bottom, height),
        width,
        height,
        spread,
        color);
}

void append_corner_brackets_top_left(std::vector<HudVertex>& vertices,
                                     float viewport_width,
                                     float viewport_height,
                                     float x,
                                     float y,
                                     float width,
                                     float height,
                                     float size,
                                     const HudColor& color) {
    if (width <= 0.0F || height <= 0.0F || size <= 0.0F || color[3] <= 0.0F) {
        return;
    }

    const auto arm = std::max(1.0F, size);
    const auto arm_length = std::min(std::min(width, height), std::max(arm * 2.4F, std::min(width, height) * 0.18F));

    append_hud_rect_top_left(vertices, viewport_width, viewport_height, x, y, arm_length, arm, color);
    append_hud_rect_top_left(vertices, viewport_width, viewport_height, x, y, arm, arm_length, color);
    append_hud_rect_top_left(vertices, viewport_width, viewport_height, x + width - arm_length, y, arm_length, arm, color);
    append_hud_rect_top_left(vertices, viewport_width, viewport_height, x + width - arm, y, arm, arm_length, color);
    append_hud_rect_top_left(vertices, viewport_width, viewport_height, x, y + height - arm, arm_length, arm, color);
    append_hud_rect_top_left(vertices, viewport_width, viewport_height, x, y + height - arm_length, arm, arm_length, color);
    append_hud_rect_top_left(vertices, viewport_width, viewport_height, x + width - arm_length, y + height - arm, arm_length, arm, color);
    append_hud_rect_top_left(vertices, viewport_width, viewport_height, x + width - arm, y + height - arm_length, arm, arm_length, color);
}

void append_empty_slot_motif_top_left(std::vector<HudVertex>& vertices,
                                      float viewport_width,
                                      float viewport_height,
                                      float x,
                                      float y,
                                      float size,
                                      const HudColor& color) {
    if (size <= 0.0F || color[3] <= 0.0F) {
        return;
    }

    const auto thickness = std::max(1.0F, size * 0.06F);
    const auto pad = size * 0.26F;
    append_hud_rect_top_left(
        vertices,
        viewport_width,
        viewport_height,
        x + pad,
        y + size * 0.5F - thickness * 0.5F,
        std::max(0.0F, size - pad * 2.0F),
        thickness,
        color);
    append_hud_rect_top_left(
        vertices,
        viewport_width,
        viewport_height,
        x + size * 0.5F - thickness * 0.5F,
        y + pad,
        thickness,
        std::max(0.0F, size - pad * 2.0F),
        color);
    append_hud_rect_top_left(
        vertices,
        viewport_width,
        viewport_height,
        x + size * 0.5F - thickness,
        y + size * 0.5F - thickness,
        thickness * 2.0F,
        thickness * 2.0F,
        hud_with_alpha(color, color[3] * 0.75F));
}

void append_stylized_panel_top_left(std::vector<HudVertex>& vertices,
                                    float viewport_width,
                                    float viewport_height,
                                    float x,
                                    float y,
                                    float width,
                                    float height,
                                    float border_thickness,
                                    const HudPanelPalette& palette,
                                    bool cast_shadow = true) {
    if (width <= 0.0F || height <= 0.0F) {
        return;
    }

    if (cast_shadow) {
        append_hud_shadow_top_left(
            vertices,
            viewport_width,
            viewport_height,
            x,
            y,
            width,
            height,
            std::max(4.0F, border_thickness * 2.2F),
            {0.0F, 0.0F, 0.0F, 0.18F});
    }

    append_hud_beveled_panel_top_left(
        vertices,
        viewport_width,
        viewport_height,
        x,
        y,
        width,
        height,
        border_thickness,
        palette.frame,
        palette.fill,
        palette.highlight,
        palette.shadow);

    const auto inner_x = x + border_thickness;
    const auto inner_y = y + border_thickness;
    const auto inner_width = std::max(0.0F, width - border_thickness * 2.0F);
    const auto inner_height = std::max(0.0F, height - border_thickness * 2.0F);
    if (inner_width <= 0.0F || inner_height <= 0.0F) {
        return;
    }

    const auto trim_height = std::max(1.0F, border_thickness * 0.72F);
    append_hud_rect_top_left(vertices, viewport_width, viewport_height, inner_x, inner_y, inner_width, trim_height, palette.trim);
    append_hud_rect_top_left(
        vertices,
        viewport_width,
        viewport_height,
        inner_x,
        inner_y + std::max(0.0F, inner_height - trim_height),
        inner_width,
        trim_height,
        {0.0F, 0.0F, 0.0F, palette.shadow[3] * 0.40F});
    append_hud_rect_top_left(
        vertices,
        viewport_width,
        viewport_height,
        inner_x,
        inner_y + trim_height + 1.0F,
        inner_width,
        std::max(1.0F, trim_height * 0.75F),
        {1.0F, 1.0F, 1.0F, palette.highlight[3] * 0.28F});
    append_corner_brackets_top_left(
        vertices,
        viewport_width,
        viewport_height,
        x + border_thickness * 0.5F,
        y + border_thickness * 0.5F,
        std::max(0.0F, width - border_thickness),
        std::max(0.0F, height - border_thickness),
        std::max(2.0F, border_thickness * 0.75F),
        hud_with_alpha(palette.trim, palette.trim[3] * 0.85F));
}

void append_stylized_panel_bottom_left(std::vector<HudVertex>& vertices,
                                       float viewport_width,
                                       float viewport_height,
                                       float x,
                                       float bottom,
                                       float width,
                                       float height,
                                       float border_thickness,
                                       const HudPanelPalette& palette,
                                       bool cast_shadow = true) {
    append_stylized_panel_top_left(
        vertices,
        viewport_width,
        viewport_height,
        x,
        bottom_to_top_left_y(viewport_height, bottom, height),
        width,
        height,
        border_thickness,
        palette,
        cast_shadow);
}

void append_modern_panel_top_left(std::vector<HudVertex>& vertices,
                                  float viewport_width,
                                  float viewport_height,
                                  float x,
                                  float y,
                                  float width,
                                  float height,
                                  float border_thickness,
                                  const HudPanelPalette& palette,
                                  bool cast_shadow = true) {
    if (width <= 0.0F || height <= 0.0F) {
        return;
    }

    const auto border = std::clamp(
        border_thickness,
        1.0F,
        std::min(width, height) * 0.25F);
    const auto radius =
        modern_hud_panel_radius(width, height, border);

    // Je compose le panneau avec des courbes simples et bornées : le HUD
    // moderne reste doux sans dépendre d'un shader ou d'une texture dédiée.
    if (cast_shadow) {
        append_hud_rounded_rect_top_left(
            vertices,
            viewport_width,
            viewport_height,
            x + std::max(1.0F, border * 0.45F),
            y + std::max(2.0F, border * 0.85F),
            width,
            height,
            radius,
            {0.0F, 0.0F, 0.0F, 0.24F});
    }

    append_hud_rounded_rect_top_left(
        vertices,
        viewport_width,
        viewport_height,
        x,
        y,
        width,
        height,
        radius,
        palette.frame);

    const auto inner_x = x + border;
    const auto inner_y = y + border;
    const auto inner_width =
        std::max(0.0F, width - border * 2.0F);
    const auto inner_height =
        std::max(0.0F, height - border * 2.0F);
    if (inner_width <= 0.0F || inner_height <= 0.0F) {
        return;
    }

    const auto inner_radius =
        std::max(0.0F, radius - border);
    append_hud_rounded_rect_top_left(
        vertices,
        viewport_width,
        viewport_height,
        inner_x,
        inner_y,
        inner_width,
        inner_height,
        inner_radius,
        palette.fill);

    const auto highlight_height = std::clamp(
        border * 0.70F,
        1.0F,
        inner_height * 0.22F);
    append_hud_rounded_rect_top_left(
        vertices,
        viewport_width,
        viewport_height,
        inner_x + inner_radius * 0.25F,
        inner_y + std::max(1.0F, border * 0.24F),
        std::max(
            0.0F,
            inner_width - inner_radius * 0.50F),
        highlight_height,
        highlight_height * 0.50F,
        palette.highlight);

    const auto trim_width =
        std::clamp(width * 0.28F, 12.0F, 72.0F);
    append_hud_rounded_rect_top_left(
        vertices,
        viewport_width,
        viewport_height,
        x + (width - trim_width) * 0.5F,
        y + border * 0.42F,
        trim_width,
        std::max(1.0F, border * 0.55F),
        std::max(0.5F, border * 0.28F),
        palette.trim);
}

void append_modern_panel_bottom_left(std::vector<HudVertex>& vertices,
                                     float viewport_width,
                                     float viewport_height,
                                     float x,
                                     float bottom,
                                     float width,
                                     float height,
                                     float border_thickness,
                                     const HudPanelPalette& palette,
                                     bool cast_shadow = true) {
    append_modern_panel_top_left(
        vertices,
        viewport_width,
        viewport_height,
        x,
        bottom_to_top_left_y(
            viewport_height,
            bottom,
            height),
        width,
        height,
        border_thickness,
        palette,
        cast_shadow);
}

void append_hud_scanlines_top_left(std::vector<HudVertex>& vertices,
                                   float viewport_width,
                                   float viewport_height,
                                   float x,
                                   float y,
                                   float width,
                                   float height,
                                   float spacing,
                                   const HudColor& color) {
    if (width <= 0.0F || height <= 0.0F || spacing <= 0.0F || color[3] <= 0.0F) {
        return;
    }

    for (float line_y = y + spacing; line_y < y + height - 1.0F; line_y += spacing) {
        append_hud_rect_top_left(
            vertices,
            viewport_width,
            viewport_height,
            x,
            line_y,
            width,
            1.0F,
            color);
    }
}

auto build_slot_palette(const HotbarSlot& slot, bool selected, bool hovered, bool hotbar_slot) -> HudSlotPalette {
    const auto accent = hotbar_slot_has_item(slot)
                            ? ui_material_accent(slot.block_id)
                            : HudColor {0.42F, 0.45F, 0.50F, 1.0F};
    const auto base_frame = hotbar_slot ? HudColor {0.07F, 0.08F, 0.09F, 0.98F} : HudColor {0.08F, 0.08F, 0.10F, 0.98F};
    const auto base_fill = hotbar_slot ? HudColor {0.16F, 0.17F, 0.19F, 0.94F} : HudColor {0.15F, 0.16F, 0.18F, 0.94F};
    const auto empty_fill = hotbar_slot ? HudColor {0.10F, 0.11F, 0.13F, 0.86F} : HudColor {0.09F, 0.10F, 0.12F, 0.82F};

    HudSlotPalette palette {};
    palette.accent = accent;
    palette.glow = selected
                       ? HudColor {1.0F, 0.88F, 0.48F, hotbar_slot ? 0.16F : 0.14F}
                       : (hovered ? hud_with_alpha(hud_scale_rgb(accent, 1.05F), 0.12F) : HudColor {0.0F, 0.0F, 0.0F, 0.0F});
    palette.motif = hotbar_slot_has_item(slot)
                        ? hud_with_alpha(hud_scale_rgb(accent, 1.08F), 0.16F)
                        : HudColor {0.38F, 0.40F, 0.45F, 0.10F};

    auto frame = base_frame;
    auto fill = hotbar_slot_has_item(slot)
                    ? hud_mix(base_fill, hud_with_alpha(accent, base_fill[3]), hotbar_slot ? 0.12F : 0.16F)
                    : empty_fill;
    auto highlight = hotbar_slot_has_item(slot)
                         ? hud_with_alpha(hud_scale_rgb(accent, 1.18F), hovered ? 0.24F : 0.18F)
                         : HudColor {0.55F, 0.58F, 0.64F, hovered ? 0.16F : 0.10F};
    auto trim = hotbar_slot_has_item(slot)
                    ? hud_with_alpha(hud_scale_rgb(accent, 1.08F), hotbar_slot ? 0.18F : 0.20F)
                    : HudColor {0.42F, 0.44F, 0.48F, 0.08F};

    if (selected) {
        frame = {0.98F, 0.89F, 0.58F, 1.0F};
        fill = hud_mix(fill, HudColor {0.28F, 0.22F, 0.15F, fill[3]}, 0.30F);
        highlight = {1.0F, 0.97F, 0.82F, 0.28F};
        trim = {1.0F, 0.90F, 0.58F, 0.28F};
        palette.motif = hud_with_alpha(hud_scale_rgb(accent, 1.12F), 0.22F);
    } else if (hovered) {
        frame = {0.92F, 0.94F, 0.98F, 0.98F};
        fill = hud_mix(fill, HudColor {0.22F, 0.24F, 0.28F, fill[3]}, 0.22F);
        trim = hud_with_alpha(hud_scale_rgb(accent, 1.15F), 0.24F);
    }

    palette.shell = {frame, fill, highlight, {0.02F, 0.02F, 0.03F, 0.60F}, trim};
    return palette;
}

void append_stylized_slot_top_left(std::vector<HudVertex>& vertices,
                                   float viewport_width,
                                   float viewport_height,
                                   float x,
                                   float y,
                                   float size,
                                   const HudSlotPalette& palette,
                                   bool has_item) {
    const auto border = std::max(2.0F, size * 0.08F);
    const auto glow_pad = std::max(2.0F, size * 0.08F);

    if (palette.glow[3] > 0.0F) {
        append_hud_shadow_top_left(
            vertices,
            viewport_width,
            viewport_height,
            x - glow_pad,
            y - glow_pad,
            size + glow_pad * 2.0F,
            size + glow_pad * 2.0F,
            glow_pad * 1.8F,
            hud_with_alpha(palette.glow, palette.glow[3] * 0.65F));
        append_hud_rect_top_left(
            vertices,
            viewport_width,
            viewport_height,
            x - glow_pad,
            y - glow_pad,
            size + glow_pad * 2.0F,
            size + glow_pad * 2.0F,
            palette.glow);
    }

    append_stylized_panel_top_left(vertices, viewport_width, viewport_height, x, y, size, size, border, palette.shell);

    const auto inset = border + std::max(1.0F, size * 0.06F);
    const auto inner_size = std::max(0.0F, size - inset * 2.0F);
    const auto inner_border = std::max(1.0F, border * 0.55F);
    const auto well_frame = hud_with_alpha(hud_scale_rgb(palette.accent, has_item ? 0.74F : 0.45F), has_item ? 0.38F : 0.16F);
    const auto well_fill = has_item ? hud_with_alpha(hud_scale_rgb(palette.accent, 0.28F), 0.22F) : HudColor {0.05F, 0.06F, 0.08F, 0.62F};
    append_hud_beveled_panel_top_left(
        vertices,
        viewport_width,
        viewport_height,
        x + inset,
        y + inset,
        inner_size,
        inner_size,
        inner_border,
        well_frame,
        well_fill,
        hud_with_alpha(palette.accent, has_item ? 0.12F : 0.04F),
        {0.0F, 0.0F, 0.0F, 0.34F});

    const auto trim_height = std::max(1.0F, size * 0.06F);
    append_hud_rect_top_left(
        vertices,
        viewport_width,
        viewport_height,
        x + border + 1.0F,
        y + border + 1.0F,
        std::max(0.0F, size - border * 2.0F - 2.0F),
        trim_height,
        palette.shell.trim);
    append_corner_brackets_top_left(
        vertices,
        viewport_width,
        viewport_height,
        x + 1.0F,
        y + 1.0F,
        std::max(0.0F, size - 2.0F),
        std::max(0.0F, size - 2.0F),
        std::max(2.0F, border * 0.85F),
        hud_with_alpha(palette.accent, has_item ? 0.20F : 0.08F));

    if (!has_item) {
        append_empty_slot_motif_top_left(vertices, viewport_width, viewport_height, x + inset, y + inset, inner_size, palette.motif);
    }
}

void append_stylized_slot_bottom_left(std::vector<HudVertex>& vertices,
                                      float viewport_width,
                                      float viewport_height,
                                      float x,
                                      float bottom,
                                      float size,
                                      const HudSlotPalette& palette,
                                      bool has_item) {
    append_stylized_slot_top_left(
        vertices,
        viewport_width,
        viewport_height,
        x,
        bottom_to_top_left_y(viewport_height, bottom, size),
        size,
        palette,
        has_item);
}

void append_modern_slot_top_left(std::vector<HudVertex>& vertices,
                                 float viewport_width,
                                 float viewport_height,
                                 float x,
                                 float y,
                                 float size,
                                 const HudSlotPalette& palette,
                                 bool has_item) {
    const auto border = std::max(2.0F, size * 0.065F);
    const auto glow_pad = std::max(2.0F, size * 0.055F);

    if (palette.glow[3] > 0.0F) {
        append_hud_rounded_rect_top_left(
            vertices,
            viewport_width,
            viewport_height,
            x - glow_pad,
            y - glow_pad,
            size + glow_pad * 2.0F,
            size + glow_pad * 2.0F,
            modern_hud_panel_radius(
                size + glow_pad * 2.0F,
                size + glow_pad * 2.0F,
                border),
            palette.glow);
    }

    append_modern_panel_top_left(
        vertices,
        viewport_width,
        viewport_height,
        x,
        y,
        size,
        size,
        border,
        palette.shell,
        false);

    const auto inset =
        border + std::max(1.0F, size * 0.055F);
    const auto inner_size =
        std::max(0.0F, size - inset * 2.0F);
    if (inner_size <= 0.0F) {
        return;
    }

    const auto well_frame = hud_with_alpha(
        hud_scale_rgb(
            palette.accent,
            has_item ? 0.74F : 0.45F),
        has_item ? 0.38F : 0.16F);
    const auto well_fill = has_item
                               ? hud_with_alpha(
                                     hud_scale_rgb(
                                         palette.accent,
                                         0.28F),
                                     0.22F)
                               : HudColor {
                                     0.05F,
                                     0.06F,
                                     0.08F,
                                     0.62F,
                                 };
    const auto inner_radius =
        modern_hud_panel_radius(
            inner_size,
            inner_size,
            std::max(1.0F, border * 0.45F));
    append_hud_rounded_rect_top_left(
        vertices,
        viewport_width,
        viewport_height,
        x + inset,
        y + inset,
        inner_size,
        inner_size,
        inner_radius,
        well_frame);

    const auto well_border =
        std::max(1.0F, border * 0.42F);
    append_hud_rounded_rect_top_left(
        vertices,
        viewport_width,
        viewport_height,
        x + inset + well_border,
        y + inset + well_border,
        std::max(0.0F, inner_size - well_border * 2.0F),
        std::max(0.0F, inner_size - well_border * 2.0F),
        std::max(0.0F, inner_radius - well_border),
        well_fill);

    if (!has_item) {
        append_empty_slot_motif_top_left(
            vertices,
            viewport_width,
            viewport_height,
            x + inset,
            y + inset,
            inner_size,
            palette.motif);
    }
}

void append_modern_slot_bottom_left(std::vector<HudVertex>& vertices,
                                    float viewport_width,
                                    float viewport_height,
                                    float x,
                                    float bottom,
                                    float size,
                                    const HudSlotPalette& palette,
                                    bool has_item) {
    append_modern_slot_top_left(
        vertices,
        viewport_width,
        viewport_height,
        x,
        bottom_to_top_left_y(
            viewport_height,
            bottom,
            size),
        size,
        palette,
        has_item);
}

void append_avatar_preview_art(std::vector<HudVertex>& vertices,
                               float viewport_width,
                               float viewport_height,
                               const InventoryMenuLayout& layout) {
    const auto scale = layout.silhouette_scale;
    const auto center_x = layout.preview_center_x;
    const auto base_y = layout.preview_base_y;
    const auto panel_x = layout.preview_panel_x;
    const auto panel_y = layout.preview_panel_y;
    const auto panel_width = layout.preview_panel_width;
    const auto panel_height = layout.preview_panel_height;
    const auto inner_pad = std::max(12.0F, layout.slot_size * 0.26F);
    const auto beam_width = std::clamp(panel_width * 0.38F, 42.0F, 84.0F);
    const auto beam_x = center_x - beam_width * 0.5F;
    const auto beam_y = panel_y + inner_pad + 10.0F;
    const auto beam_height = std::max(0.0F, panel_height - inner_pad * 2.0F - 36.0F);
    append_hud_rect_top_left(vertices, viewport_width, viewport_height, beam_x, beam_y, beam_width, beam_height, {1.0F, 1.0F, 1.0F, 0.04F});
    append_hud_rect_top_left(
        vertices,
        viewport_width,
        viewport_height,
        beam_x + beam_width * 0.16F,
        beam_y,
        beam_width * 0.68F,
        beam_height,
        {0.82F, 0.90F, 1.0F, 0.06F});

    const auto pedestal_width = std::clamp(panel_width * 0.54F, 74.0F, 124.0F);
    const auto pedestal_height = std::max(8.0F, layout.slot_size * 0.30F);
    const auto pedestal_x = center_x - pedestal_width * 0.5F;
    const auto pedestal_y = base_y + scale * 0.50F;
    append_stylized_panel_top_left(
        vertices,
        viewport_width,
        viewport_height,
        pedestal_x,
        pedestal_y,
        pedestal_width,
        pedestal_height + 8.0F,
        3.0F,
        make_slate_panel_palette(),
        false);
    append_hud_rect_top_left(
        vertices,
        viewport_width,
        viewport_height,
        center_x - pedestal_width * 0.34F,
        pedestal_y - scale * 0.34F,
        pedestal_width * 0.68F,
        std::max(2.0F, scale * 0.32F),
        {0.0F, 0.0F, 0.0F, 0.18F});

    append_hud_rect_top_left(
        vertices,
        viewport_width,
        viewport_height,
        center_x - scale * 1.40F,
        base_y - scale * 0.05F,
        scale * 2.80F,
        scale * 0.24F,
        {0.0F, 0.0F, 0.0F, 0.14F});

    append_hud_rect_top_left(
        vertices,
        viewport_width,
        viewport_height,
        center_x - scale * 0.76F,
        base_y - scale * 6.90F,
        scale * 1.52F,
        scale * 1.52F,
        {0.20F, 0.14F, 0.10F, 1.0F});
    append_hud_rect_top_left(
        vertices,
        viewport_width,
        viewport_height,
        center_x - scale * 0.68F,
        base_y - scale * 6.58F,
        scale * 1.36F,
        scale * 1.16F,
        {0.93F, 0.79F, 0.62F, 1.0F});
    append_hud_rect_top_left(
        vertices,
        viewport_width,
        viewport_height,
        center_x - scale * 0.56F,
        base_y - scale * 6.20F,
        scale * 1.12F,
        scale * 0.16F,
        {1.0F, 1.0F, 1.0F, 0.06F});

    append_hud_rect_top_left(
        vertices,
        viewport_width,
        viewport_height,
        center_x - scale * 0.96F,
        base_y - scale * 5.25F,
        scale * 1.92F,
        scale * 2.48F,
        {0.30F, 0.54F, 0.90F, 1.0F});
    append_hud_rect_top_left(
        vertices,
        viewport_width,
        viewport_height,
        center_x - scale * 0.96F,
        base_y - scale * 5.25F,
        scale * 1.92F,
        scale * 0.42F,
        {0.48F, 0.72F, 0.98F, 0.64F});
    append_hud_rect_top_left(
        vertices,
        viewport_width,
        viewport_height,
        center_x - scale * 0.72F,
        base_y - scale * 3.12F,
        scale * 1.44F,
        scale * 0.34F,
        {0.17F, 0.24F, 0.42F, 1.0F});

    append_hud_rect_top_left(
        vertices,
        viewport_width,
        viewport_height,
        center_x - scale * 1.64F,
        base_y - scale * 5.08F,
        scale * 0.56F,
        scale * 1.98F,
        {0.93F, 0.79F, 0.62F, 1.0F});
    append_hud_rect_top_left(
        vertices,
        viewport_width,
        viewport_height,
        center_x + scale * 1.08F,
        base_y - scale * 5.08F,
        scale * 0.56F,
        scale * 1.98F,
        {0.93F, 0.79F, 0.62F, 1.0F});

    append_hud_rect_top_left(
        vertices,
        viewport_width,
        viewport_height,
        center_x - scale * 0.82F,
        base_y - scale * 2.90F,
        scale * 0.66F,
        scale * 2.52F,
        {0.21F, 0.26F, 0.44F, 1.0F});
    append_hud_rect_top_left(
        vertices,
        viewport_width,
        viewport_height,
        center_x + scale * 0.16F,
        base_y - scale * 2.90F,
        scale * 0.66F,
        scale * 2.52F,
        {0.21F, 0.26F, 0.44F, 1.0F});
    append_hud_rect_top_left(
        vertices,
        viewport_width,
        viewport_height,
        center_x - scale * 0.82F,
        base_y - scale * 0.54F,
        scale * 0.66F,
        scale * 0.26F,
        {0.10F, 0.12F, 0.16F, 1.0F});
    append_hud_rect_top_left(
        vertices,
        viewport_width,
        viewport_height,
        center_x + scale * 0.16F,
        base_y - scale * 0.54F,
        scale * 0.66F,
        scale * 0.26F,
        {0.10F, 0.12F, 0.16F, 1.0F});

    append_corner_brackets_top_left(
        vertices,
        viewport_width,
        viewport_height,
        panel_x + inner_pad * 0.55F,
        panel_y + inner_pad * 0.65F,
        std::max(0.0F, panel_width - inner_pad * 1.10F),
        std::max(0.0F, panel_height - inner_pad * 1.30F),
        std::max(2.0F, scale * 0.22F),
        {1.0F, 1.0F, 1.0F, 0.08F});
}

void append_keycap_top_left(std::vector<HudVertex>& vertices,
                            float viewport_width,
                            float viewport_height,
                            const InventoryKeycapLayout& keycap,
                            float pixel_size) {
    const auto palette = keycap.selected ? make_warm_panel_palette({0.98F, 0.84F, 0.46F, 1.0F}) : make_slate_panel_palette();
    append_stylized_panel_top_left(
        vertices,
        viewport_width,
        viewport_height,
        keycap.x,
        keycap.y,
        keycap.width,
        keycap.height,
        std::max(1.0F, keycap.height * 0.18F),
        palette,
        false);

    const auto label = std::to_string(keycap.number);
    const auto text_y = keycap.y + std::max(0.0F, (keycap.height - pixel_size * 7.0F) * 0.5F);
    append_pixel_text(
        vertices,
        viewport_width,
        viewport_height,
        keycap.x + keycap.width * 0.5F + pixel_size,
        text_y + pixel_size,
        pixel_size,
        label,
        {0.0F, 0.0F, 0.0F, 0.52F},
        true);
    append_pixel_text(
        vertices,
        viewport_width,
        viewport_height,
        keycap.x + keycap.width * 0.5F,
        text_y,
        pixel_size,
        label,
        keycap.selected ? HudColor {0.99F, 0.96F, 0.88F, 1.0F} : HudColor {0.90F, 0.92F, 0.96F, 0.94F},
        true);
}

auto resolve_inventory_focus_item(const InventoryMenuState& inventory, const HotbarState& hotbar) -> InventoryFocusItem {
    InventoryFocusItem focus {};
    if (inventory.carrying_item && inventory_slot_has_item(inventory.carried_slot)) {
        focus.slot = inventory.carried_slot;
        focus.has_item = true;
        focus.from_carried_slot = true;
        return focus;
    }

    if (inventory.hovered_slot.has_value()) {
        if (const auto* hovered_slot = inventory_slot_ptr(inventory, hotbar, *inventory.hovered_slot);
            hovered_slot != nullptr && inventory_slot_has_item(*hovered_slot)) {
            focus.slot = *hovered_slot;
            focus.has_item = true;
            focus.group = inventory.hovered_slot->group;
            return focus;
        }
    }

    if (inventory_slot_has_item(hotbar.selected_slot())) {
        focus.slot = hotbar.selected_slot();
        focus.has_item = true;
        focus.group = InventorySlotGroup::Hotbar;
    }
    return focus;
}

auto resolve_viewmodel_held_item(const InventoryMenuState& inventory, const HotbarState& hotbar) noexcept -> BlockId {
    const auto& selected_slot = hotbar.selected_slot();
    if (is_musket_item(selected_slot)) {
        return to_block_id(BlockType::Musket);
    }
    if (hotbar_slot_has_item(selected_slot) && is_tool_item(selected_slot.block_id)) {
        return to_block_id(BlockType::Air);
    }

    const auto& equipped_weapon = inventory.equipment_slots[equipment_slot_index(EquipmentSlot::Weapon)];
    if (hotbar_slot_has_item(equipped_weapon) && is_weapon_item(equipped_weapon.block_id)) {
        return block_item_id(equipped_weapon.block_id);
    }

    if (hotbar_slot_has_item(selected_slot) && is_weapon_item(selected_slot.block_id)) {
        return block_item_id(selected_slot.block_id);
    }

    return to_block_id(BlockType::Air);
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
    const auto padding_x = std::max(2.0F, pixel_size * 1.15F);
    const auto padding_y = std::max(1.0F, pixel_size * 0.78F);
    const auto badge_width = text_width + padding_x * 2.0F;
    const auto badge_height = pixel_size * 7.0F + padding_y * 2.0F;
    const auto badge_x = right_x - badge_width;
    const auto badge_y = bottom_y - badge_height;
    const auto badge_border = std::max(1.0F, pixel_size * 0.55F);

    append_hud_shadow_top_left(
        vertices,
        viewport_width,
        viewport_height,
        badge_x,
        badge_y,
        badge_width,
        badge_height,
        std::max(2.0F, pixel_size * 1.6F),
        {0.0F, 0.0F, 0.0F, 0.20F});
    append_hud_beveled_panel_top_left(
        vertices,
        viewport_width,
        viewport_height,
        badge_x,
        badge_y,
        badge_width,
        badge_height,
        badge_border,
        {0.04F, 0.04F, 0.05F, 0.98F},
        {0.16F, 0.17F, 0.19F, 0.96F},
        {0.90F, 0.92F, 0.96F, 0.10F},
        {0.0F, 0.0F, 0.0F, 0.42F});
    append_hud_rect_top_left(
        vertices,
        viewport_width,
        viewport_height,
        badge_x + badge_border,
        badge_y + badge_border,
        std::max(0.0F, badge_width - badge_border * 2.0F),
        std::max(1.0F, badge_border * 0.75F),
        {1.0F, 0.92F, 0.72F, 0.08F});
    append_pixel_text(
        vertices,
        viewport_width,
        viewport_height,
        badge_x + padding_x + pixel_size,
        badge_y + padding_y + pixel_size,
        pixel_size,
        count_text,
        {0.0F, 0.0F, 0.0F, 0.58F});
    append_pixel_text(
        vertices,
        viewport_width,
        viewport_height,
        badge_x + padding_x,
        badge_y + padding_y,
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
    append_stack_count(vertices, viewport_width, viewport_height, right_x, viewport_height - bottom, pixel_size, count);
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

constexpr float kBlockBreakOverlayMaterialClass = 9.0F;
constexpr float kBlockBreakOverlaySkyLight = 1.0F;
constexpr float kBlockBreakOverlayBlockLight = 0.22F;
constexpr float kBlockBreakOverlayAo = 1.0F;
constexpr float kBlockBreakOverlayBaseInflate = 0.0035F;
constexpr float kBlockBreakOverlayProgressInflate = 0.0105F;

using OverlayQuad = std::array<std::array<float, 3>, 4>;
using OverlayUvs = std::array<std::array<float, 2>, 4>;

auto overlay_tile_uvs(const BlockAtlasTile& tile, float min_u = 0.0F, float min_v = 0.0F, float max_u = 1.0F, float max_v = 1.0F)
    -> OverlayUvs {
    const auto uv_step = 1.0F / kBlockAtlasTilesPerAxis;
    const auto tile_u0 = static_cast<float>(tile.x) * uv_step;
    const auto tile_v0 = static_cast<float>(tile.y) * uv_step;
    const auto u0 = tile_u0 + min_u * uv_step;
    const auto v0 = tile_v0 + min_v * uv_step;
    const auto u1 = tile_u0 + max_u * uv_step;
    const auto v1 = tile_v0 + max_v * uv_step;
    return {{
        {u1, v0},
        {u1, v1},
        {u0, v1},
        {u0, v0},
    }};
}

void append_block_break_quad(std::vector<ChunkVertex>& vertices,
                             std::vector<std::uint32_t>& indices,
                             const OverlayQuad& positions,
                             const OverlayUvs& uvs,
                             const std::array<float, 3>& normal,
                             float face_shade) {
    const auto base_index = static_cast<std::uint32_t>(vertices.size());
    for (std::size_t index = 0; index < positions.size(); ++index) {
        vertices.push_back({
            positions[index][0],
            positions[index][1],
            positions[index][2],
            uvs[index][0],
            uvs[index][1],
            normal[0],
            normal[1],
            normal[2],
            face_shade,
            kBlockBreakOverlayAo,
            kBlockBreakOverlaySkyLight,
            kBlockBreakOverlayBlockLight,
            kBlockBreakOverlayMaterialClass,
            0.0F,
        });
    }

    indices.insert(indices.end(), {
        base_index + 0U, base_index + 1U, base_index + 2U,
        base_index + 0U, base_index + 2U, base_index + 3U,
    });
}

void append_block_break_double_sided_quad(std::vector<ChunkVertex>& vertices,
                                          std::vector<std::uint32_t>& indices,
                                          const OverlayQuad& positions,
                                          const OverlayUvs& uvs,
                                          const std::array<float, 3>& normal,
                                          float face_shade) {
    append_block_break_quad(vertices, indices, positions, uvs, normal, face_shade);

    const OverlayQuad reversed_positions {{
        positions[3],
        positions[2],
        positions[1],
        positions[0],
    }};
    const std::array<float, 3> reversed_normal {{
        -normal[0],
        -normal[1],
        -normal[2],
    }};
    append_block_break_quad(vertices, indices, reversed_positions, uvs, reversed_normal, face_shade * 0.96F);
}

void append_block_break_cube_mesh(std::vector<ChunkVertex>& vertices,
                                  std::vector<std::uint32_t>& indices,
                                  const BlockCoord& block,
                                  const BlockAtlasTile& tile,
                                  float inflate,
                                  float top_height = 1.0F) {
    const auto min_x = static_cast<float>(block.x) - inflate;
    const auto max_x = static_cast<float>(block.x + 1) + inflate;
    const auto min_y = static_cast<float>(block.y) - inflate;
    const auto max_y = static_cast<float>(block.y) + top_height + inflate;
    const auto min_z = static_cast<float>(block.z) - inflate;
    const auto max_z = static_cast<float>(block.z + 1) + inflate;
    const auto uvs = overlay_tile_uvs(tile);

    append_block_break_quad(
        vertices,
        indices,
        {{{max_x, min_y, min_z}, {max_x, max_y, min_z}, {max_x, max_y, max_z}, {max_x, min_y, max_z}}},
        uvs,
        {1.0F, 0.0F, 0.0F},
        0.85F);
    append_block_break_quad(
        vertices,
        indices,
        {{{min_x, min_y, max_z}, {min_x, max_y, max_z}, {min_x, max_y, min_z}, {min_x, min_y, min_z}}},
        uvs,
        {-1.0F, 0.0F, 0.0F},
        0.85F);
    append_block_break_quad(
        vertices,
        indices,
        {{{min_x, max_y, max_z}, {max_x, max_y, max_z}, {max_x, max_y, min_z}, {min_x, max_y, min_z}}},
        uvs,
        {0.0F, 1.0F, 0.0F},
        1.0F);
    append_block_break_quad(
        vertices,
        indices,
        {{{min_x, min_y, min_z}, {max_x, min_y, min_z}, {max_x, min_y, max_z}, {min_x, min_y, max_z}}},
        uvs,
        {0.0F, -1.0F, 0.0F},
        0.65F);
    append_block_break_quad(
        vertices,
        indices,
        {{{max_x, min_y, max_z}, {max_x, max_y, max_z}, {min_x, max_y, max_z}, {min_x, min_y, max_z}}},
        uvs,
        {0.0F, 0.0F, 1.0F},
        0.75F);
    append_block_break_quad(
        vertices,
        indices,
        {{{min_x, min_y, min_z}, {min_x, max_y, min_z}, {max_x, max_y, min_z}, {max_x, min_y, min_z}}},
        uvs,
        {0.0F, 0.0F, -1.0F},
        0.75F);
}

void append_block_break_cross_mesh(std::vector<ChunkVertex>& vertices,
                                   std::vector<std::uint32_t>& indices,
                                   const BlockCoord& block,
                                   const BlockAtlasTile& tile,
                                   float inflate) {
    const auto min_edge = 0.18F - inflate;
    const auto max_edge = 0.82F + inflate;
    const auto min_y = static_cast<float>(block.y) - inflate;
    const auto max_y = static_cast<float>(block.y) + 0.95F + inflate;
    const auto world_x = static_cast<float>(block.x);
    const auto world_z = static_cast<float>(block.z);
    const auto uvs = overlay_tile_uvs(tile);

    append_block_break_double_sided_quad(
        vertices,
        indices,
        {{
            {world_x + min_edge, min_y, world_z + min_edge},
            {world_x + min_edge, max_y, world_z + min_edge},
            {world_x + max_edge, max_y, world_z + max_edge},
            {world_x + max_edge, min_y, world_z + max_edge},
        }},
        uvs,
        {0.70710677F, 0.0F, -0.70710677F},
        0.95F);
    append_block_break_double_sided_quad(
        vertices,
        indices,
        {{
            {world_x + max_edge, min_y, world_z + min_edge},
            {world_x + max_edge, max_y, world_z + min_edge},
            {world_x + min_edge, max_y, world_z + max_edge},
            {world_x + min_edge, min_y, world_z + max_edge},
        }},
        uvs,
        {0.70710677F, 0.0F, 0.70710677F},
        0.95F);
}

void append_block_break_torch_mesh(std::vector<ChunkVertex>& vertices,
                                   std::vector<std::uint32_t>& indices,
                                   const BlockCoord& block,
                                   BlockId block_id,
                                   const BlockAtlasTile& tile,
                                   float inflate) {
    constexpr float base_min_x = 6.0F / 16.0F;
    constexpr float base_max_x = 10.0F / 16.0F;
    constexpr float base_min_z = 6.0F / 16.0F;
    constexpr float base_max_z = 10.0F / 16.0F;
    constexpr float base_min_y = 0.0F;
    constexpr float base_shaft_max_y = 10.0F / 16.0F;
    constexpr float base_head_max_y = 14.0F / 16.0F;
    constexpr float wall_mount_offset = 4.5F / 16.0F;
    constexpr float wall_pivot_y = 3.5F / 16.0F;
    constexpr float wall_tilt_radians = 22.5F * 3.14159265358979323846F / 180.0F;

    const auto support_offset = torch_support_offset(block_id);
    const auto wall_torch = is_wall_torch_block(block_id);
    const auto uvs = overlay_tile_uvs(tile);

    const auto transform_local_position = [&](const std::array<float, 3>& local_position) {
        if (!wall_torch) {
            return std::array<float, 3> {
                static_cast<float>(block.x) + local_position[0],
                static_cast<float>(block.y) + local_position[1],
                static_cast<float>(block.z) + local_position[2],
            };
        }

        auto x = local_position[0] + static_cast<float>(support_offset.x) * wall_mount_offset;
        auto y = local_position[1];
        auto z = local_position[2] + static_cast<float>(support_offset.z) * wall_mount_offset;
        const auto pivot_x = 0.5F + static_cast<float>(support_offset.x) * wall_mount_offset;
        const auto pivot_z = 0.5F + static_cast<float>(support_offset.z) * wall_mount_offset;

        x -= pivot_x;
        y -= wall_pivot_y;
        z -= pivot_z;

        if (support_offset.x != 0) {
            const auto tilt = static_cast<float>(support_offset.x) * wall_tilt_radians;
            const auto cos_tilt = std::cos(tilt);
            const auto sin_tilt = std::sin(tilt);
            const auto rotated_x = x * cos_tilt - y * sin_tilt;
            const auto rotated_y = x * sin_tilt + y * cos_tilt;
            x = rotated_x;
            y = rotated_y;
        } else if (support_offset.z != 0) {
            const auto tilt = -static_cast<float>(support_offset.z) * wall_tilt_radians;
            const auto cos_tilt = std::cos(tilt);
            const auto sin_tilt = std::sin(tilt);
            const auto rotated_y = y * cos_tilt - z * sin_tilt;
            const auto rotated_z = y * sin_tilt + z * cos_tilt;
            y = rotated_y;
            z = rotated_z;
        }

        return std::array<float, 3> {
            static_cast<float>(block.x) + x + pivot_x,
            static_cast<float>(block.y) + y + wall_pivot_y,
            static_cast<float>(block.z) + z + pivot_z,
        };
    };

    const auto inflate_local_x = inflate;
    const auto inflate_local_z = inflate;
    const auto inflate_min_y = inflate;
    const auto inflate_shaft_y = inflate * 0.45F;
    const auto inflate_head_y = inflate;

    const auto make_face = [&](const std::array<std::array<float, 3>, 4>& local_positions) {
        std::array<std::array<float, 3>, 4> positions {};
        for (std::size_t i = 0; i < local_positions.size(); ++i) {
            positions[i] = transform_local_position(local_positions[i]);
        }
        return positions;
    };

    append_block_break_quad(
        vertices,
        indices,
        make_face({{{base_max_x + inflate_local_x, base_min_y - inflate_min_y, base_min_z - inflate_local_z},
                    {base_max_x + inflate_local_x, base_shaft_max_y + inflate_shaft_y, base_min_z - inflate_local_z},
                    {base_max_x + inflate_local_x, base_shaft_max_y + inflate_shaft_y, base_max_z + inflate_local_z},
                    {base_max_x + inflate_local_x, base_min_y - inflate_min_y, base_max_z + inflate_local_z}}}),
        uvs,
        {1.0F, 0.0F, 0.0F},
        0.85F);
    append_block_break_quad(
        vertices,
        indices,
        make_face({{{base_min_x - inflate_local_x, base_min_y - inflate_min_y, base_max_z + inflate_local_z},
                    {base_min_x - inflate_local_x, base_shaft_max_y + inflate_shaft_y, base_max_z + inflate_local_z},
                    {base_min_x - inflate_local_x, base_shaft_max_y + inflate_shaft_y, base_min_z - inflate_local_z},
                    {base_min_x - inflate_local_x, base_min_y - inflate_min_y, base_min_z - inflate_local_z}}}),
        uvs,
        {-1.0F, 0.0F, 0.0F},
        0.85F);
    append_block_break_quad(
        vertices,
        indices,
        make_face({{{base_max_x + inflate_local_x, base_min_y - inflate_min_y, base_max_z + inflate_local_z},
                    {base_max_x + inflate_local_x, base_shaft_max_y + inflate_shaft_y, base_max_z + inflate_local_z},
                    {base_min_x - inflate_local_x, base_shaft_max_y + inflate_shaft_y, base_max_z + inflate_local_z},
                    {base_min_x - inflate_local_x, base_min_y - inflate_min_y, base_max_z + inflate_local_z}}}),
        uvs,
        {0.0F, 0.0F, 1.0F},
        0.75F);
    append_block_break_quad(
        vertices,
        indices,
        make_face({{{base_min_x - inflate_local_x, base_min_y - inflate_min_y, base_min_z - inflate_local_z},
                    {base_min_x - inflate_local_x, base_shaft_max_y + inflate_shaft_y, base_min_z - inflate_local_z},
                    {base_max_x + inflate_local_x, base_shaft_max_y + inflate_shaft_y, base_min_z - inflate_local_z},
                    {base_max_x + inflate_local_x, base_min_y - inflate_min_y, base_min_z - inflate_local_z}}}),
        uvs,
        {0.0F, 0.0F, -1.0F},
        0.75F);
    append_block_break_quad(
        vertices,
        indices,
        make_face({{{base_min_x - inflate_local_x, base_min_y - inflate_min_y, base_min_z - inflate_local_z},
                    {base_max_x + inflate_local_x, base_min_y - inflate_min_y, base_min_z - inflate_local_z},
                    {base_max_x + inflate_local_x, base_min_y - inflate_min_y, base_max_z + inflate_local_z},
                    {base_min_x - inflate_local_x, base_min_y - inflate_min_y, base_max_z + inflate_local_z}}}),
        uvs,
        {0.0F, -1.0F, 0.0F},
        0.65F);

    append_block_break_quad(
        vertices,
        indices,
        make_face({{{base_max_x + inflate_local_x, base_shaft_max_y + inflate_shaft_y, base_min_z - inflate_local_z},
                    {base_max_x + inflate_local_x, base_head_max_y + inflate_head_y, base_min_z - inflate_local_z},
                    {base_max_x + inflate_local_x, base_head_max_y + inflate_head_y, base_max_z + inflate_local_z},
                    {base_max_x + inflate_local_x, base_shaft_max_y + inflate_shaft_y, base_max_z + inflate_local_z}}}),
        uvs,
        {1.0F, 0.0F, 0.0F},
        0.85F);
    append_block_break_quad(
        vertices,
        indices,
        make_face({{{base_min_x - inflate_local_x, base_shaft_max_y + inflate_shaft_y, base_max_z + inflate_local_z},
                    {base_min_x - inflate_local_x, base_head_max_y + inflate_head_y, base_max_z + inflate_local_z},
                    {base_min_x - inflate_local_x, base_head_max_y + inflate_head_y, base_min_z - inflate_local_z},
                    {base_min_x - inflate_local_x, base_shaft_max_y + inflate_shaft_y, base_min_z - inflate_local_z}}}),
        uvs,
        {-1.0F, 0.0F, 0.0F},
        0.85F);
    append_block_break_quad(
        vertices,
        indices,
        make_face({{{base_max_x + inflate_local_x, base_shaft_max_y + inflate_shaft_y, base_max_z + inflate_local_z},
                    {base_max_x + inflate_local_x, base_head_max_y + inflate_head_y, base_max_z + inflate_local_z},
                    {base_min_x - inflate_local_x, base_head_max_y + inflate_head_y, base_max_z + inflate_local_z},
                    {base_min_x - inflate_local_x, base_shaft_max_y + inflate_shaft_y, base_max_z + inflate_local_z}}}),
        uvs,
        {0.0F, 0.0F, 1.0F},
        0.75F);
    append_block_break_quad(
        vertices,
        indices,
        make_face({{{base_min_x - inflate_local_x, base_shaft_max_y + inflate_shaft_y, base_min_z - inflate_local_z},
                    {base_min_x - inflate_local_x, base_head_max_y + inflate_head_y, base_min_z - inflate_local_z},
                    {base_max_x + inflate_local_x, base_head_max_y + inflate_head_y, base_min_z - inflate_local_z},
                    {base_max_x + inflate_local_x, base_shaft_max_y + inflate_shaft_y, base_min_z - inflate_local_z}}}),
        uvs,
        {0.0F, 0.0F, -1.0F},
        0.75F);
    append_block_break_quad(
        vertices,
        indices,
        make_face({{{base_min_x - inflate_local_x, base_head_max_y + inflate_head_y, base_max_z + inflate_local_z},
                    {base_max_x + inflate_local_x, base_head_max_y + inflate_head_y, base_max_z + inflate_local_z},
                    {base_max_x + inflate_local_x, base_head_max_y + inflate_head_y, base_min_z - inflate_local_z},
                    {base_min_x - inflate_local_x, base_head_max_y + inflate_head_y, base_min_z - inflate_local_z}}}),
        uvs,
        {0.0F, 1.0F, 0.0F},
        1.0F);
}

void build_block_break_overlay_mesh_data_into(const BlockBreakProgress& break_progress, ChunkMeshData& mesh) {
    mesh.vertices.clear();
    mesh.indices.clear();
    mesh.water_vertices.clear();
    mesh.water_indices.clear();
    mesh.face_count = 0;
    mesh.water_face_count = 0;
    if (mesh.vertices.capacity() < 64U) {
        mesh.vertices.reserve(64U);
    }
    if (mesh.indices.capacity() < 96U) {
        mesh.indices.reserve(96U);
    }
    if (!break_progress.active || !is_block_breakable_at(break_progress.block, break_progress.block_id)) {
        return;
    }

    const auto tile = block_break_crack_tile(break_progress.crack_stage);
    const auto inflate =
        kBlockBreakOverlayBaseInflate + break_progress.progress * kBlockBreakOverlayProgressInflate;

    mesh.vertices.reserve(64U);
    mesh.indices.reserve(96U);

    switch (block_mesh_type(break_progress.block_id)) {
    case BlockMeshType::Cross:
        append_block_break_cross_mesh(mesh.vertices, mesh.indices, break_progress.block, tile, inflate);
        break;
    case BlockMeshType::Torch:
        append_block_break_torch_mesh(mesh.vertices, mesh.indices, break_progress.block, break_progress.block_id, tile, inflate);
        break;
    case BlockMeshType::Water:
        append_block_break_cube_mesh(
            mesh.vertices,
            mesh.indices,
            break_progress.block,
            tile,
            inflate,
            15.0F / 16.0F);
        break;
    case BlockMeshType::FullCube:
    default:
        append_block_break_cube_mesh(mesh.vertices, mesh.indices, break_progress.block, tile, inflate);
        break;
    }

    mesh.face_count = mesh.indices.size() / 6U;
}

void build_organic_block_break_overlay_mesh_data_into(
    const World& world,
    const BlockBreakProgress& break_progress,
    ChunkMeshData& mesh) {
    mesh.vertices.clear();
    mesh.indices.clear();
    mesh.water_vertices.clear();
    mesh.water_indices.clear();
    mesh.face_count = 0U;
    mesh.water_face_count = 0U;
    if (!break_progress.active ||
        !is_organic_terrain_block(break_progress.block_id) ||
        !is_block_breakable_at(
            break_progress.block,
            break_progress.block_id)) {
        return;
    }

    const OrganicTerrainSection target_section {
        break_progress.block,
        break_progress.block,
    };
    const OrganicTerrainMesher mesher {};
    const auto surface = mesher.build_mesh(
        target_section,
        [&world](int x, int y, int z) {
            return OrganicTerrainCellSample {
                world.peek_block_or_generated(x, y, z),
                world.get_sky_light(x, y, z),
                world.get_block_light(x, y, z),
            };
        },
        32U,
        48U);
    if (surface.empty()) {
        return;
    }

    const auto tile = block_break_crack_tile(break_progress.crack_stage);
    const auto tile_size = 1.0F / kBlockAtlasTilesPerAxis;
    const auto tile_u = static_cast<float>(tile.x) * tile_size;
    const auto tile_v = static_cast<float>(tile.y) * tile_size;
    const auto inflate =
        kBlockBreakOverlayBaseInflate +
        break_progress.progress * kBlockBreakOverlayProgressInflate;
    mesh.vertices.reserve(surface.vertices.size());
    mesh.indices = surface.indices;

    for (const auto& source : surface.vertices) {
        const auto normal = glm::normalize(
            glm::vec3 {source.nx, source.ny, source.nz});
        const auto absolute_normal = glm::abs(normal);
        float local_u = 0.0F;
        float local_v = 0.0F;
        if (absolute_normal.x >= absolute_normal.y &&
            absolute_normal.x >= absolute_normal.z) {
            local_u = source.z - static_cast<float>(break_progress.block.z);
            local_v = source.y - static_cast<float>(break_progress.block.y);
        } else if (absolute_normal.y >= absolute_normal.z) {
            local_u = source.x - static_cast<float>(break_progress.block.x);
            local_v = source.z - static_cast<float>(break_progress.block.z);
        } else {
            local_u = source.x - static_cast<float>(break_progress.block.x);
            local_v = source.y - static_cast<float>(break_progress.block.y);
        }
        local_u = glm::clamp(local_u, 0.0F, 1.0F);
        local_v = glm::clamp(local_v, 0.0F, 1.0F);

        mesh.vertices.push_back({
            source.x + normal.x * inflate,
            source.y + normal.y * inflate,
            source.z + normal.z * inflate,
            tile_u + local_u * tile_size,
            tile_v + local_v * tile_size,
            normal.x,
            normal.y,
            normal.z,
            1.0F,
            kBlockBreakOverlayAo,
            kBlockBreakOverlaySkyLight,
            kBlockBreakOverlayBlockLight,
            kBlockBreakOverlayMaterialClass,
            0.0F,
        });
    }
    mesh.face_count = mesh.indices.size() / 6U;
}

} // namespace

Renderer::~Renderer() {
    shutdown();
}

auto Renderer::initialize(const RendererOptions& options) -> bool {
    last_initialization_error_.clear();
    auto normalized_options = options;
    normalized_options.shadow_map_size = std::max(normalized_options.shadow_map_size, 1);
    normalized_options.viewmodel_fov_degrees = glm::clamp(normalized_options.viewmodel_fov_degrees, 35.0F, 100.0F);

    if (initialized_) {
        const auto visual_pipeline_changed =
            options_.visual_pipeline != normalized_options.visual_pipeline;
        const auto shadow_resource_changed =
            options_.shadows_enabled != normalized_options.shadows_enabled ||
            options_.shadow_map_size != normalized_options.shadow_map_size;
        const auto target_format_changed = options_.quality != normalized_options.quality;
        const auto post_process_disabled = options_.post_process_enabled && !normalized_options.post_process_enabled;
        options_ = normalized_options;
        if (target_format_changed) {
            adaptive_quality_controller_.reset(options_.quality, 1, 1);
            active_quality_settings_ = adaptive_quality_controller_.settings(options_.quality, 1, 1);
            adaptive_gpu_sample_consumed_ = false;
            pending_cpu_frame_time_ms_ = 0.0;
            pending_cpu_frame_time_valid_ = false;
        }

        try {
            // Je ne reconstruis que les ressources réellement dépendantes des options.
            if (shadow_resource_changed) {
                destroy_shadow_map();
                create_shadow_map();
            }
            if (target_format_changed) {
                destroy_water_scene_targets();
                destroy_post_process_targets();
            } else if (post_process_disabled) {
                destroy_glow_targets();
            }
            if (visual_pipeline_changed) {
                if (options_.visual_pipeline == VisualPipeline::ModernStylized) {
                    if (!create_modern_material_textures()) {
                        throw std::runtime_error(
                            last_initialization_error_.empty()
                                ? "Unable to load the modern visual material pack"
                                : last_initialization_error_);
                    }
                    if (!create_model_icon_texture()) {
                        throw std::runtime_error(
                            last_initialization_error_.empty()
                                ? "Unable to load the modern model icon atlas"
                                : last_initialization_error_);
                    }
                    if (!create_msdf_font_texture()) {
                        throw std::runtime_error(
                            last_initialization_error_.empty()
                                ? "Unable to load the modern UI font atlas"
                                : last_initialization_error_);
                    }
                } else {
                    destroy_modern_material_textures();
                    destroy_model_icon_texture();
                    destroy_msdf_font_texture();
                }

                // Je reconstruis les atlas colores lors d'un changement de
                // pipeline : le rendu moderne les decode en sRGB, tandis que
                // LegacyVoxel conserve exactement son ancien format lineaire.
                if (creature_atlas_texture_ != 0) {
                    glDeleteTextures(1, &creature_atlas_texture_);
                    creature_atlas_texture_ = 0;
                }
                if (player_atlas_texture_ != 0) {
                    glDeleteTextures(1, &player_atlas_texture_);
                    player_atlas_texture_ = 0;
                }
                create_creature_atlas_texture();
                create_player_atlas_texture();

                // Les caches incorporent les UV et le mode de texture du
                // pipeline actif : je les invalide sans modifier leurs états.
                hotbar_cache_.valid = false;
                inventory_cache_.valid = false;
                death_cache_.valid = false;
                pause_cache_.valid = false;
                main_menu_cache_.valid = false;
                save_slot_cache_.valid = false;
                options_cache_.valid = false;
                confirm_cache_.valid = false;
                maritime_cache_.valid = false;

                // Je reconstruis les gabarits partagés sans toucher aux rigs,
                // sockets ni instances qui portent le gameplay.
                glDeleteBuffers(1, &viewmodel_instance_vbo_);
                glDeleteVertexArrays(1, &viewmodel_vao_);
                glDeleteBuffers(1, &creature_instance_vbo_);
                glDeleteBuffers(1, &creature_ebo_);
                glDeleteBuffers(1, &creature_vbo_);
                glDeleteVertexArrays(1, &creature_vao_);
                glDeleteBuffers(1, &item_drop_instance_vbo_);
                glDeleteBuffers(1, &item_drop_ebo_);
                glDeleteBuffers(1, &item_drop_vbo_);
                glDeleteVertexArrays(1, &item_drop_vao_);
                viewmodel_instance_vbo_ = 0;
                viewmodel_vao_ = 0;
                creature_instance_vbo_ = 0;
                creature_ebo_ = 0;
                creature_vbo_ = 0;
                creature_vao_ = 0;
                item_drop_instance_vbo_ = 0;
                item_drop_ebo_ = 0;
                item_drop_vbo_ = 0;
                item_drop_vao_ = 0;
                create_creature_geometry();
                create_item_drop_geometry();

                // Je force aussi le navire a changer de representation : sa
                // revision logique ne varie pas lors d'un basculement visuel.
                destroy_gpu_mesh(ship_gpu_mesh_);
                ship_mesh_cache_.reset();
                active_ship_lod_ = StylizedShipLod::Near;
            }
            return true;
        } catch (const std::exception& exception) {
            last_initialization_error_ = exception.what();
            return false;
        } catch (...) {
            last_initialization_error_ =
                "Unknown exception while reconfiguring renderer resources";
            return false;
        }
    }

    options_ = normalized_options;
    try {
        adaptive_quality_controller_.reset(options_.quality, 1, 1);
        active_quality_settings_ = adaptive_quality_controller_.settings(options_.quality, 1, 1);
        gl_api_ready_ = true;
        create_programs();
        create_atlas_texture();
        if (options_.visual_pipeline == VisualPipeline::ModernStylized) {
            if (!create_modern_material_textures()) {
                throw std::runtime_error(
                    last_initialization_error_.empty()
                        ? "Unable to load the modern visual material pack"
                        : last_initialization_error_);
            }
            if (!create_model_icon_texture()) {
                throw std::runtime_error(
                    last_initialization_error_.empty()
                        ? "Unable to load the modern model icon atlas"
                        : last_initialization_error_);
            }
            if (!create_msdf_font_texture()) {
                throw std::runtime_error(
                    last_initialization_error_.empty()
                        ? "Unable to load the modern UI font atlas"
                        : last_initialization_error_);
            }
        }
        create_accent_texture();
        create_creature_atlas_texture();
        create_player_atlas_texture();
        create_shadow_map();
        create_scene_sampler_fallback_textures();
        create_creature_geometry();
        create_item_drop_geometry();
        create_precipitation_geometry();
        create_old_guard_effect_geometry();
        create_hud_geometry();
        create_screen_quad_geometry();
        create_crosshair_geometry();
        create_gpu_timers();
        initialized_ = true;
        return true;
    } catch (const std::exception& exception) {
        last_initialization_error_ = exception.what();
        shutdown();
        return false;
    } catch (...) {
        last_initialization_error_ =
            "Unknown exception while initializing renderer resources";
        shutdown();
        return false;
    }
}

void Renderer::shutdown() {
    if (gl_api_ready_) {
        destroy_gpu_timers();
        reset_world_resources();
        destroy_gpu_mesh(ship_gpu_mesh_);

        destroy_water_scene_targets();
        destroy_post_process_targets();
        destroy_modern_material_textures();
        destroy_model_icon_texture();
        destroy_msdf_font_texture();

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
        destroy_shadow_map();
        if (scene_fallback_depth_texture_ != 0) {
            glDeleteTextures(1, &scene_fallback_depth_texture_);
        }
        if (scene_fallback_color_texture_ != 0) {
            glDeleteTextures(1, &scene_fallback_color_texture_);
        }
        if (viewmodel_instance_vbo_ != 0) {
            glDeleteBuffers(1, &viewmodel_instance_vbo_);
        }
        if (viewmodel_vao_ != 0) {
            glDeleteVertexArrays(1, &viewmodel_vao_);
        }
        if (creature_instance_vbo_ != 0) {
            glDeleteBuffers(1, &creature_instance_vbo_);
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
        if (item_drop_instance_vbo_ != 0) {
            glDeleteBuffers(1, &item_drop_instance_vbo_);
        }
        if (item_drop_ebo_ != 0) {
            glDeleteBuffers(1, &item_drop_ebo_);
        }
        if (item_drop_vbo_ != 0) {
            glDeleteBuffers(1, &item_drop_vbo_);
        }
        if (item_drop_vao_ != 0) {
            glDeleteVertexArrays(1, &item_drop_vao_);
        }
        if (precipitation_instance_vbo_ != 0) {
            glDeleteBuffers(1, &precipitation_instance_vbo_);
        }
        if (precipitation_vbo_ != 0) {
            glDeleteBuffers(1, &precipitation_vbo_);
        }
        if (precipitation_vao_ != 0) {
            glDeleteVertexArrays(1, &precipitation_vao_);
        }
        if (old_guard_effect_instance_vbo_ != 0) {
            glDeleteBuffers(1, &old_guard_effect_instance_vbo_);
        }
        if (old_guard_effect_vbo_ != 0) {
            glDeleteBuffers(1, &old_guard_effect_vbo_);
        }
        if (old_guard_effect_vao_ != 0) {
            glDeleteVertexArrays(1, &old_guard_effect_vao_);
        }
        if (world_program_ != 0) {
            glDeleteProgram(world_program_);
        }
        if (modern_terrain_program_ != 0) {
            glDeleteProgram(modern_terrain_program_);
        }
        if (modern_architecture_program_ != 0) {
            glDeleteProgram(modern_architecture_program_);
        }
        if (modern_terrain_shadow_program_ != 0) {
            glDeleteProgram(modern_terrain_shadow_program_);
        }
        if (item_drop_program_ != 0) {
            glDeleteProgram(item_drop_program_);
        }
        if (precipitation_program_ != 0) {
            glDeleteProgram(precipitation_program_);
        }
        if (old_guard_effect_program_ != 0) {
            glDeleteProgram(old_guard_effect_program_);
        }
        if (creature_program_ != 0) {
            glDeleteProgram(creature_program_);
        }
        if (creature_shadow_program_ != 0) {
            glDeleteProgram(creature_shadow_program_);
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
        if (menu_background_program_ != 0) {
            glDeleteProgram(menu_background_program_);
        }
    }

    gpu_meshes_.clear();
    world_resource_reset_queue_.clear();
    block_break_overlay_mesh_ = {};
    ship_gpu_mesh_ = {};
    visible_chunks_cache_.clear();
    shadow_chunks_cache_.clear();
    visible_creatures_cache_.clear();
    visible_crew_cache_.clear();
    visible_old_guard_cache_.clear();
    screen_quad_vao_ = 0;
    crosshair_vbo_ = 0;
    crosshair_vao_ = 0;
    hud_vbo_ = 0;
    hud_vao_ = 0;
    atlas_texture_ = 0;
    msdf_font_texture_ = 0;
    model_icon_texture_ = 0;
    modern_material_albedo_texture_ = 0;
    modern_material_normal_height_texture_ = 0;
    modern_material_orm_emission_texture_ = 0;
    accent_texture_ = 0;
    creature_atlas_texture_ = 0;
    player_atlas_texture_ = 0;
    shadow_map_ = 0;
    shadow_framebuffer_ = 0;
    shadow_map_far_ = 0;
    shadow_framebuffer_far_ = 0;
    scene_fallback_color_texture_ = 0;
    scene_fallback_depth_texture_ = 0;
    water_scene_framebuffer_ = 0;
    water_scene_color_texture_ = 0;
    water_scene_depth_texture_ = 0;
    creature_vao_ = 0;
    creature_vbo_ = 0;
    creature_ebo_ = 0;
    creature_instance_vbo_ = 0;
    viewmodel_vao_ = 0;
    viewmodel_instance_vbo_ = 0;
    item_drop_vao_ = 0;
    item_drop_vbo_ = 0;
    item_drop_ebo_ = 0;
    item_drop_instance_vbo_ = 0;
    precipitation_vao_ = 0;
    precipitation_vbo_ = 0;
    precipitation_instance_vbo_ = 0;
    old_guard_effect_vao_ = 0;
    old_guard_effect_vbo_ = 0;
    old_guard_effect_instance_vbo_ = 0;
    world_program_ = 0;
    modern_terrain_program_ = 0;
    modern_architecture_program_ = 0;
    modern_terrain_shadow_program_ = 0;
    item_drop_program_ = 0;
    precipitation_program_ = 0;
    old_guard_effect_program_ = 0;
    creature_program_ = 0;
    creature_shadow_program_ = 0;
    shadow_program_ = 0;
    hud_program_ = 0;
    crosshair_program_ = 0;
    sky_program_ = 0;
    post_process_program_ = 0;
    glow_extract_program_ = 0;
    glow_blur_program_ = 0;
    menu_background_program_ = 0;
    world_uniforms_ = {};
    modern_terrain_uniforms_ = {};
    modern_architecture_uniforms_ = {};
    modern_terrain_shadow_uniforms_ = {};
    creature_uniforms_ = {};
    creature_shadow_light_view_projection_ = -1;
    item_drop_uniforms_ = {};
    shadow_uniforms_ = {};
    hud_uniforms_ = {};
    sky_uniforms_ = {};
    post_process_uniforms_ = {};
    precipitation_uniforms_ = {};
    old_guard_effect_uniforms_ = {};
    glow_extract_uniforms_ = {};
    glow_blur_uniforms_ = {};
    menu_background_uniforms_ = {};
    creature_instance_buffer_bytes_ = 0;
    viewmodel_instance_buffer_bytes_ = 0;
    item_drop_instance_buffer_bytes_ = 0;
    creature_template_vertex_buffer_bytes_ = 0;
    creature_template_index_buffer_bytes_ = 0;
    item_drop_template_vertex_buffer_bytes_ = 0;
    item_drop_template_index_buffer_bytes_ = 0;
    creature_template_index_count_ = 0;
    item_drop_template_index_count_ = 0;
    precipitation_instance_buffer_bytes_ = 0;
    old_guard_effect_instance_buffer_bytes_ = 0;
    hud_vertex_buffer_bytes_ = 0;
    last_frame_stats_ = {};
    water_scene_target_width_ = 0;
    water_scene_target_height_ = 0;
    scene_target_width_ = 0;
    scene_target_height_ = 0;
    glow_target_width_ = 0;
    glow_target_height_ = 0;
    water_scene_color_internal_format_ = 0;
    scene_color_internal_format_ = 0;
    glow_color_internal_format_ = 0;
    precipitation_field_.clear();
    precipitation_instances_scratch_.clear();
    old_guard_effect_instances_scratch_.clear();
    chunk_upload_scratch_ = {};
    terrain_upload_scratch_ = {};
    architecture_upload_scratch_ = {};
    architecture_indices_scratch_.clear();
    block_break_overlay_scratch_ = {};
    loading_vertices_scratch_.clear();
    gameplay_announcement_vertices_scratch_.clear();
    command_console_vertices_scratch_.clear();
    last_gpu_timings_ = {};
    gpu_frame_index_ = 0;
    adaptive_last_gpu_source_frame_ = 0;
    pending_cpu_frame_time_ms_ = 0.0;
    material_pack_checksum_ = 0U;
    material_pack_version_ = 0U;
    material_pack_width_ = 0U;
    material_pack_height_ = 0U;
    material_pack_layers_ = 0U;
    material_pack_mips_ = 0U;
    msdf_font_width_ = 0U;
    msdf_font_height_ = 0U;
    msdf_font_mips_ = 0U;
    frame_draw_calls_ = 0U;
    frame_triangles_ = 0U;
    frame_uploaded_bytes_ = 0U;
    world_resource_reset_progress_.finish();
    ship_mesh_cache_.reset();
    active_ship_lod_ = StylizedShipLod::Near;
    active_gpu_query_frame_ = -1;
    active_gpu_pass_ = -1;
    gpu_timers_supported_ = false;
    adaptive_gpu_sample_consumed_ = false;
    pending_cpu_frame_time_valid_ = false;
    adaptive_quality_controller_.reset(options_.quality, 1, 1);
    active_quality_settings_ = resolve_renderer_quality_settings(options_.quality, 1, 1);
    gl_api_ready_ = false;
    initialized_ = false;
}

void Renderer::render_frame(World& world,
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
                            int height) {
    render_frame(
        world,
        player,
        player_musket,
        hotbar,
        inventory_menu,
        death_screen,
        pause_menu,
        main_menu,
        save_slot_menu,
        options_menu,
        confirm_dialog,
        creatures,
        std::span<const CrewRenderInstance> {},
        std::span<const OldGuardRenderInstance> {},
        std::span<const OldGuardMuzzleFlashInstance> {},
        std::span<const OldGuardSmokeInstance> {},
        std::span<const OldGuardMuzzleFlashInstance> {},
        std::span<const OldGuardSmokeInstance> {},
        item_drops,
        ship,
        progression,
        super_vision_active,
        gameplay_announcement,
        maritime_hud,
        command_console,
        environment,
        width,
        height);
}

void Renderer::render_frame(World& world,
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
                            const EnvironmentState& raw_environment,
                            int width,
                            int height) {
    if (!initialized_) {
        return;
    }

    const auto environment =
        sanitize_weather_for_rendering(
            raw_environment);
    using clock = std::chrono::steady_clock;
    RendererFrameStats frame_stats {};
    frame_stats.visual_pipeline = options_.visual_pipeline;
    frame_draw_calls_ = 0U;
    frame_triangles_ = 0U;
    frame_uploaded_bytes_ = 0U;
    begin_gpu_frame(frame_stats);
    const auto render_width = std::max(width, 1);
    const auto render_height = std::max(height, 1);
    auto adaptive_sample_ms = 0.0;
    auto adaptive_sample_valid = false;
    if (frame_stats.gpu.valid &&
        (!adaptive_gpu_sample_consumed_ || frame_stats.gpu.source_frame != adaptive_last_gpu_source_frame_)) {
        adaptive_sample_ms = frame_stats.gpu.total_ms();
        adaptive_sample_valid = adaptive_sample_ms > 0.0;
        adaptive_last_gpu_source_frame_ = frame_stats.gpu.source_frame;
        adaptive_gpu_sample_consumed_ = true;
    } else if (!gpu_timers_supported_) {
        adaptive_sample_ms = last_frame_stats_.upload_ms + last_frame_stats_.world_ms;
        adaptive_sample_valid = adaptive_sample_ms > 0.0;
    }
    const auto resolved_adaptive_sample =
        resolve_adaptive_frame_time_sample(
            adaptive_sample_ms,
            adaptive_sample_valid,
            pending_cpu_frame_time_ms_,
            pending_cpu_frame_time_valid_);
    pending_cpu_frame_time_ms_ = 0.0;
    pending_cpu_frame_time_valid_ = false;

    const auto previous_quality_settings = active_quality_settings_;
    active_quality_settings_ = resolved_adaptive_sample.valid
                                   ? adaptive_quality_controller_.update(
                                         options_.quality,
                                         render_width,
                                         render_height,
                                         resolved_adaptive_sample.frame_time_ms)
                                   : adaptive_quality_controller_.settings(options_.quality, render_width, render_height);
    if (active_quality_settings_ != previous_quality_settings) {
        if (active_quality_settings_.high_precision_hdr != previous_quality_settings.high_precision_hdr) {
            destroy_water_scene_targets();
            destroy_post_process_targets();
        } else if (active_quality_settings_.glow_downsample != previous_quality_settings.glow_downsample) {
            destroy_glow_targets();
        }
    }
    const auto quality_settings =
        active_quality_settings_;

    const auto ocean_profile =
        OceanSimulation::surface_profile_for_world(
            world.generation_profile());

    const auto ocean =
        OceanSimulation::evaluate(
            environment,
            ocean_profile);

    std::array<glm::vec4, kOceanMaxWaveCount>
        ocean_wave_uniforms {};

    std::array<glm::vec2, kOceanMaxWaveCount>
        ocean_phase_uniforms {};

    for (std::size_t index = 0;
         index < ocean.waves.size();
         ++index) {

        const auto& wave = ocean.waves[index];

        ocean_wave_uniforms[index] = {
            wave.direction.x,
            wave.direction.y,
            wave.wave_number,
            wave.amplitude,
        };

        ocean_phase_uniforms[index] = {
            wave.phase,
            wave.steepness,
        };
    }

    const auto adaptive_state = adaptive_quality_controller_.state();
    frame_stats.resolved_quality = quality_settings.resolved_quality;
    frame_stats.adaptive_frame_ema_ms = adaptive_state.frame_time_ema_ms;
    frame_stats.adaptive_frame_p95_ms = adaptive_state.frame_time_p95_ms;

    const auto upload_start = clock::now();
    if (ship.visible) {
        if (options_.visual_pipeline == VisualPipeline::ModernStylized) {
            const auto ship_center =
                (ship.world_bounds.min + ship.world_bounds.max) * 0.5F;
            const auto distance_squared =
                glm::dot(ship_center - player.eye_position(), ship_center - player.eye_position());
            constexpr float kShipFarLodEnterDistance = 176.0F;
            constexpr float kShipFarLodExitDistance = 144.0F;
            if (active_ship_lod_ == StylizedShipLod::Near &&
                distance_squared >
                    kShipFarLodEnterDistance * kShipFarLodEnterDistance) {
                active_ship_lod_ = StylizedShipLod::Far;
            } else if (active_ship_lod_ == StylizedShipLod::Far &&
                       distance_squared <
                           kShipFarLodExitDistance * kShipFarLodExitDistance) {
                active_ship_lod_ = StylizedShipLod::Near;
            }
        } else {
            active_ship_lod_ = StylizedShipLod::Near;
        }
        ensure_ship_mesh(ship, active_ship_lod_);
    }
    sync_gpu_meshes(world, frame_stats, kMaxGpuMeshEventsPerFrame, kMaxGpuMeshSyncMsPerFrame);
    frame_stats.upload_ms = std::chrono::duration<double, std::milli>(clock::now() - upload_start).count();

    const auto aspect = static_cast<float>(render_width) / static_cast<float>(render_height);
    const auto musket_aim_ratio =
        player_musket.active
            ? std::clamp(
                  player_musket.aim_ratio,
                  0.0F,
                  1.0F)
            : 0.0F;
    const auto world_fov =
        player_musket_world_fov(
            musket_aim_ratio);
    const auto projection =
        glm::perspective(
            glm::radians(world_fov),
            aspect,
            0.1F,
            320.0F);
    const auto base_viewmodel_fov =
        glm::clamp(
            options_.viewmodel_fov_degrees,
            35.0F,
            100.0F);
    const auto musket_viewmodel_fov =
        player_musket_viewmodel_fov(
            base_viewmodel_fov,
            musket_aim_ratio);
    const auto viewmodel_projection = glm::perspective(
        glm::radians(musket_viewmodel_fov),
        aspect,
        0.02F,
        8.0F);
    const auto view = player.view_matrix();
    const auto inverse_view = glm::inverse(view);
    const auto view_projection = projection * view;
    const glm::mat4 identity_model {1.0F};
    auto sky_view = view;
    sky_view[3] = glm::vec4 {0.0F, 0.0F, 0.0F, 1.0F};
    const auto inverse_sky_view_projection = glm::inverse(projection * sky_view);
    const auto frustum_planes = extract_frustum_planes(view_projection);
    const auto eye = player.eye_position();
    auto camera_forward = player.look_direction();
    if (glm::dot(camera_forward, camera_forward) > 1.0e-6F) {
        camera_forward = glm::normalize(camera_forward);
    } else {
        camera_forward = {0.0F, 0.0F, -1.0F};
    }
    auto forward = camera_forward;
    forward.y = 0.0F;
    if (glm::dot(forward, forward) > 1.0e-6F) {
        forward = glm::normalize(forward);
    } else {
        forward = {0.0F, 0.0F, -1.0F};
    }

    const auto active_stream_radius =
        resolve_adaptive_stream_radius(
            world.stream_radius(),
            quality_settings.resolved_quality);
    const auto streamed_draw_distance =
        static_cast<float>(
            (active_stream_radius + 2) *
            kChunkSizeX);
    const auto draw_distance =
        std::min(
            streamed_draw_distance,
            quality_settings.terrain_lod_distance);
    const auto draw_distance_sq = draw_distance * draw_distance;
    constexpr float kBackCullStartDistance = 20.0F;
    constexpr float kBackCullStartDistanceSq = kBackCullStartDistance * kBackCullStartDistance;
    const auto sun_visible = environment.sun_direction.y > 0.0F;
    const auto super_vision_strength = super_vision_active ? 1.0F : 0.0F;
    glm::mat4 light_view_projection(1.0F);
    glm::mat4 light_view_projection_far(1.0F);
    ShadowCascadeSet shadow_cascades {};
    auto shadow_cascade_count = 1;
    auto shadow_split_distance = 320.0F;
    auto shadow_transition_width = 0.0F;
    ShadowPassContext shadow_context {};
    std::array<
        ShadowPassContext,
        kMaximumShadowCascadeCount>
        shadow_cascade_contexts {};
    auto shadow_map_size = 0;

    if (options_.shadows_enabled && sun_visible) {
        shadow_map_size = std::max(options_.shadow_map_size, 1);
        if (!is_modern_visual_pipeline(options_.visual_pipeline)) {
            // Je garde la cascade historique à l'identique : le pipeline de
            // repli doit pouvoir servir de témoin visuel au même commit.
            const auto snap =
                (kShadowDistance * 2.0F) /
                static_cast<float>(shadow_map_size);
            const auto focus =
                player.position() + glm::vec3 {0.0F, 18.0F, 0.0F};
            const auto snapped_focus = glm::vec3 {
                std::floor(focus.x / snap) * snap,
                std::floor(focus.y / snap) * snap,
                std::floor(focus.z / snap) * snap,
            };
            const auto light_position =
                snapped_focus +
                glm::normalize(environment.sun_direction) *
                    (kShadowDistance * 0.85F);
            const auto up =
                std::abs(environment.sun_direction.y) > 0.95F
                    ? glm::vec3 {0.0F, 0.0F, 1.0F}
                    : glm::vec3 {0.0F, 1.0F, 0.0F};
            const auto light_view =
                glm::lookAt(light_position, snapped_focus, up);
            const auto light_projection = glm::ortho(
                -kShadowDistance,
                kShadowDistance,
                -kShadowDistance,
                kShadowDistance,
                1.0F,
                kShadowDistance * 3.0F);
            light_view_projection =
                light_projection * light_view;
            light_view_projection_far =
                light_view_projection;
            shadow_cascade_count = 1;
            shadow_split_distance = 320.0F;
            shadow_transition_width = 0.0F;
            shadow_context.frustum =
                extract_frustum_planes(light_view_projection);
            shadow_context.focus = focus;
            const auto max_shadow_distance =
                kShadowDistance +
                static_cast<float>(kChunkSizeX);
            shadow_context.max_distance_sq =
                max_shadow_distance * max_shadow_distance;
            shadow_context.enabled = true;
            shadow_cascade_contexts[0] =
                shadow_context;
        } else {
            ShadowCascadeBuildParameters cascade_parameters {};
            cascade_parameters.quality =
                quality_settings.resolved_quality;
            cascade_parameters.cascade_count = std::clamp(
                quality_settings.shadow_cascade_count,
                1,
                static_cast<int>(kMaximumShadowCascadeCount));
            cascade_parameters.camera_position = eye;
            cascade_parameters.camera_forward = camera_forward;
            cascade_parameters.camera_up = glm::vec3 {inverse_view[1]};
            cascade_parameters.vertical_fov_radians =
                glm::radians(75.0F);
            cascade_parameters.aspect_ratio = aspect;
            cascade_parameters.near_distance = 0.1F;
            cascade_parameters.far_distance = std::clamp(
                draw_distance + static_cast<float>(kChunkSizeX),
                kShadowDistance,
                320.0F);
            cascade_parameters.sun_direction =
                environment.sun_direction;
            cascade_parameters.shadow_map_resolution =
                shadow_map_size;
            cascade_parameters.split_lambda = 0.65F;
            cascade_parameters.caster_depth_padding =
                static_cast<float>(kChunkSizeX) * 1.5F;
            shadow_cascades =
                build_shadow_cascade_set(cascade_parameters);
            shadow_cascade_count = static_cast<int>(
                shadow_cascades.cascade_count);
            light_view_projection =
                shadow_cascades.cascades[0]
                    .light_view_projection;
            light_view_projection_far =
                shadow_cascades.cascades[
                    shadow_cascades.cascade_count > 1U
                        ? 1U
                        : 0U]
                    .light_view_projection;
            shadow_split_distance =
                shadow_cascades.split_distances[1];
            shadow_transition_width =
                shadow_cascades.transition_width;

            const auto& widest_cascade =
                shadow_cascades.cascades[
                    shadow_cascades.cascade_count - 1U];
            shadow_context.frustum =
                widest_cascade.frustum;
            shadow_context.focus =
                widest_cascade.bounds.world_center;
            const auto max_shadow_distance =
                widest_cascade.bounds.bounding_radius +
                static_cast<float>(kChunkSizeX);
            shadow_context.max_distance_sq =
                max_shadow_distance * max_shadow_distance;
            shadow_context.enabled = true;
            for (std::size_t cascade_index = 0U;
                 cascade_index < shadow_cascades.cascade_count;
                 ++cascade_index) {
                const auto& cascade =
                    shadow_cascades.cascades[cascade_index];
                const auto cascade_distance =
                    cascade.bounds.bounding_radius +
                    static_cast<float>(kChunkSizeX);
                shadow_cascade_contexts[cascade_index] = {
                    cascade.frustum,
                    cascade.bounds.world_center,
                    cascade_distance * cascade_distance,
                    true,
                };
            }
        }
    }

    auto& visible_chunks = visible_chunks_cache_;
    auto& shadow_chunks = shadow_chunks_cache_;
    visible_chunks.clear();
    shadow_chunks.clear();
    if (visible_chunks.capacity() < gpu_meshes_.size()) {
        visible_chunks.reserve(gpu_meshes_.size());
    }
    if (shadow_chunks.capacity() < gpu_meshes_.size()) {
        shadow_chunks.reserve(gpu_meshes_.size());
    }

    for (const auto& [coord, gpu_mesh] : gpu_meshes_) {
        if (gpu_mesh.opaque_index_count == 0 &&
            gpu_mesh.terrain_index_count == 0 &&
            gpu_mesh.architecture_opaque_index_count == 0 &&
            gpu_mesh.architecture_transparent_index_count == 0 &&
            gpu_mesh.water_index_count == 0) {
            continue;
        }

        auto draw_bounds = gpu_mesh.bounds;

        if (gpu_mesh.water_index_count > 0) {
            // La géométrie CPU décrit la surface au repos. Cette marge évite que les
            // crêtes disparaissent prématurément au bord du frustum.
            const auto water_margin =
                std::max(
                    ocean.maximum_displacement,
                    0.0F) +
                0.02F;

            draw_bounds.min_corner.y -= water_margin;
            draw_bounds.max_corner.y += water_margin;

            draw_bounds.center =
                (draw_bounds.min_corner +
                 draw_bounds.max_corner) *
                0.5F;
        }

        const auto visibility = classify_chunk_visibility(
            draw_bounds,
            frustum_planes,
            eye,
            forward,
            draw_distance_sq,
            kBackCullStartDistanceSq,
            shadow_context,
            gpu_mesh.opaque_index_count > 0 ||
                gpu_mesh.terrain_index_count > 0 ||
                gpu_mesh.architecture_opaque_index_count > 0);
        if (visibility.camera) {
            visible_chunks.push_back({
                coord,
                &gpu_mesh,
                gpu_mesh.bounds.center,
                visibility.distance_squared,
            });
        }
        if (visibility.shadow) {
            shadow_chunks.push_back({&gpu_mesh});
        }
    }

    ChunkPassVisibility ship_visibility {};
    ChunkBounds ship_world_bounds {};
    auto ship_world_bounds_valid = false;
    if (ship.visible && ship_mesh_ready(ship, active_ship_lod_)) {
        // Je transforme les limites exactes du plan en espace monde pour ne
        // conserver le navire que dans les passes camera et ombre utiles.
        ship_world_bounds = {
            ship.world_bounds.min,
            ship.world_bounds.max,
            (ship.world_bounds.min +
             ship.world_bounds.max) *
                0.5F,
        };
        ship_world_bounds_valid = true;
        ship_visibility = classify_large_bounds_visibility(
            ship_world_bounds,
            frustum_planes,
            eye,
            draw_distance_sq,
            shadow_context,
            ship_gpu_mesh_.opaque_index_count > 0);
    }
    std::sort(visible_chunks.begin(), visible_chunks.end(), [](const VisibleChunk& lhs, const VisibleChunk& rhs) {
        return lhs.distance_squared < rhs.distance_squared;
    });
    frame_stats.visible_chunks = visible_chunks.size();

    if (shadow_context.enabled) {
        const auto shadow_start = clock::now();
        begin_gpu_pass(GpuTimedPass::Shadow);
        const std::array<glm::mat4, kMaximumShadowCascadeCount>
            cascade_matrices {{
                light_view_projection,
                light_view_projection_far,
            }};
        const std::array<GLuint, kMaximumShadowCascadeCount>
            cascade_framebuffers {{
                shadow_framebuffer_,
                shadow_framebuffer_far_,
            }};

        // Je rends une cascade en qualité basse/moyenne et deux en haute.
        // Les candidats restent conservateurs pour préserver les ombres des
        // objets placés juste avant la coupure entre les deux volumes.
        for (auto cascade_index = 0;
             cascade_index < shadow_cascade_count;
             ++cascade_index) {
        const auto& cascade_light_view_projection =
            cascade_matrices[
                static_cast<std::size_t>(cascade_index)];
        const auto& cascade_shadow_context =
            shadow_cascade_contexts[
                static_cast<std::size_t>(cascade_index)];
        const auto renders_in_cascade =
            [&cascade_shadow_context](
                const GpuMesh& mesh) {
                return should_render_chunk_in_shadow_pass(
                    mesh.bounds,
                    cascade_shadow_context.frustum,
                    cascade_shadow_context.focus,
                    cascade_shadow_context.max_distance_sq);
            };
        glViewport(0, 0, shadow_map_size, shadow_map_size);
        glBindFramebuffer(
            GL_FRAMEBUFFER,
            cascade_framebuffers[
                static_cast<std::size_t>(cascade_index)]);
        glClear(GL_DEPTH_BUFFER_BIT);
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(2.0F, 4.0F);

        glUseProgram(shadow_program_);
        glUniformMatrix4fv(shadow_uniforms_.model, 1, GL_FALSE, glm::value_ptr(identity_model));
        glUniformMatrix4fv(
            shadow_uniforms_.light_view_projection,
            1,
            GL_FALSE,
            glm::value_ptr(cascade_light_view_projection));
        glUniform1f(shadow_uniforms_.time_of_day, environment.time_of_day);
        glUniform1f(shadow_uniforms_.wind_strength, environment.wind_strength);
        glUniform1i(shadow_uniforms_.atlas, 0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, atlas_texture_);

        for (const auto& shadow_chunk : shadow_chunks) {
            if (shadow_chunk.mesh->opaque_index_count == 0 ||
                !renders_in_cascade(*shadow_chunk.mesh)) {
                continue;
            }
            glBindVertexArray(shadow_chunk.mesh->vao);
            glDrawElements(GL_TRIANGLES, shadow_chunk.mesh->opaque_index_count, GL_UNSIGNED_INT, nullptr);
            record_triangle_draw(shadow_chunk.mesh->opaque_index_count);
            ++frame_stats.shadow_chunks;
        }
        if (options_.visual_pipeline == VisualPipeline::ModernStylized &&
            modern_terrain_shadow_program_ != 0) {
            glUseProgram(modern_terrain_shadow_program_);
            glUniformMatrix4fv(
                modern_terrain_shadow_uniforms_.model,
                1,
                GL_FALSE,
                glm::value_ptr(identity_model));
            glUniformMatrix4fv(
                modern_terrain_shadow_uniforms_.light_view_projection,
                1,
                GL_FALSE,
                glm::value_ptr(cascade_light_view_projection));
            glUniform1i(
                modern_terrain_shadow_uniforms_.material_albedo,
                4);
            glActiveTexture(GL_TEXTURE4);
            glBindTexture(
                GL_TEXTURE_2D_ARRAY,
                modern_material_albedo_texture_);

            for (const auto& shadow_chunk : shadow_chunks) {
                if (shadow_chunk.mesh->terrain_index_count == 0 ||
                    !renders_in_cascade(*shadow_chunk.mesh)) {
                    continue;
                }
                glBindVertexArray(shadow_chunk.mesh->terrain_vao);
                glDrawElements(
                    GL_TRIANGLES,
                    shadow_chunk.mesh->terrain_index_count,
                    GL_UNSIGNED_INT,
                    nullptr);
                record_triangle_draw(shadow_chunk.mesh->terrain_index_count);
                if (shadow_chunk.mesh->opaque_index_count == 0) {
                    ++frame_stats.shadow_chunks;
                }
            }
            // Le VAO architectural n'expose pas l'attribut de flags en
            // location 4. Sa valeur générique doit donc rester à zéro, sinon
            // une valeur résiduelle pourrait transformer un mur opaque en
            // carte alpha dans le shader d'ombre partagé.
            glVertexAttribI1ui(4, 0U);
            for (const auto& shadow_chunk : shadow_chunks) {
                if (shadow_chunk.mesh->architecture_opaque_index_count == 0 ||
                    !renders_in_cascade(*shadow_chunk.mesh)) {
                    continue;
                }
                glBindVertexArray(shadow_chunk.mesh->architecture_vao);
                glDrawElements(
                    GL_TRIANGLES,
                    shadow_chunk.mesh->architecture_opaque_index_count,
                    GL_UNSIGNED_INT,
                    nullptr);
                record_triangle_draw(
                    shadow_chunk.mesh->architecture_opaque_index_count);
                if (shadow_chunk.mesh->opaque_index_count == 0 &&
                    shadow_chunk.mesh->terrain_index_count == 0) {
                    ++frame_stats.shadow_chunks;
                }
            }

            // Je restaure le programme historique avant les ombres du navire
            // et des entités, qui conservent encore leur format de sommet.
            glUseProgram(shadow_program_);
            glUniformMatrix4fv(
                shadow_uniforms_.model,
                1,
                GL_FALSE,
                glm::value_ptr(identity_model));
            glUniformMatrix4fv(
                shadow_uniforms_.light_view_projection,
                1,
                GL_FALSE,
                glm::value_ptr(cascade_light_view_projection));
            glUniform1f(shadow_uniforms_.time_of_day, environment.time_of_day);
            glUniform1f(shadow_uniforms_.wind_strength, environment.wind_strength);
            glUniform1i(shadow_uniforms_.atlas, 0);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, atlas_texture_);
        }
        const auto ship_visible_in_cascade =
            ship_visibility.shadow &&
            ship_world_bounds_valid &&
            should_render_chunk_in_shadow_pass(
                ship_world_bounds,
                cascade_shadow_context.frustum,
                cascade_shadow_context.focus,
                cascade_shadow_context.max_distance_sq);
        if (ship_visible_in_cascade) {
            glUniformMatrix4fv(
                shadow_uniforms_.model,
                1,
                GL_FALSE,
                glm::value_ptr(
                    ship.model_matrix));
            glBindVertexArray(ship_gpu_mesh_.vao);
            glDrawElements(GL_TRIANGLES, ship_gpu_mesh_.opaque_index_count, GL_UNSIGNED_INT, nullptr);
            record_triangle_draw(ship_gpu_mesh_.opaque_index_count);
            glUniformMatrix4fv(shadow_uniforms_.model, 1, GL_FALSE, glm::value_ptr(identity_model));
        }
        draw_creature_shadows(
            creatures,
            crew,
            old_guard,
            cascade_light_view_projection,
            cascade_shadow_context.focus);
        }

        glDisable(GL_POLYGON_OFFSET_FILL);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glActiveTexture(GL_TEXTURE0);
        end_gpu_pass(GpuTimedPass::Shadow);
        frame_stats.shadow_ms =
            std::chrono::duration<double, std::milli>(
                clock::now() - shadow_start)
                .count();
    }

    const auto world_start = clock::now();
    const auto optional_post_process_enabled =
        options_.post_process_enabled &&
        width > 0 &&
        height > 0;
    const auto menu_preview_visible =
        main_menu.visible ||
        (save_slot_menu.visible &&
         save_slot_menu.parent == SaveSlotMenuParent::MainMenu) ||
        (options_menu.visible &&
         options_menu.parent == OptionsMenuParent::MainMenu);
    const auto has_visible_water =
        std::any_of(
            visible_chunks.begin(),
            visible_chunks.end(),
            [](const VisibleChunk& visible_chunk) {
                return visible_chunk.mesh->water_index_count > 0;
            });
    const auto modern_output_resolve_required =
        is_modern_visual_pipeline(options_.visual_pipeline) &&
        width > 0 &&
        height > 0;
    const auto requires_scene_target =
        optional_post_process_enabled ||
        modern_output_resolve_required ||
        has_visible_water ||
        menu_preview_visible;

    if (has_visible_water) {
        ensure_water_scene_targets(render_width, render_height);
    }
    if (requires_scene_target) {
        ensure_post_process_targets(
            render_width,
            render_height,
            optional_post_process_enabled ||
                menu_preview_visible);
    }

    const auto final_target_framebuffer =
        requires_scene_target
            ? scene_framebuffer_
            : 0U;
    const auto opaque_target_framebuffer =
        has_visible_water
            ? water_scene_framebuffer_
            : final_target_framebuffer;
    const auto inverse_view_projection = glm::inverse(view_projection);

    glBindFramebuffer(GL_FRAMEBUFFER, opaque_target_framebuffer);
    glViewport(0, 0, render_width, render_height);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glDisable(GL_BLEND);
    glClearColor(environment.sky_zenith_color.r, environment.sky_zenith_color.g, environment.sky_zenith_color.b, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    begin_gpu_pass(GpuTimedPass::Opaque);

    glUseProgram(world_program_);
    upload_world_ship_protection(ship);
    glUniform4fv(
        world_uniforms_.ocean_waves,
        static_cast<GLsizei>(
            ocean_wave_uniforms.size()),
        glm::value_ptr(
            ocean_wave_uniforms.front()));

    glUniform2fv(
        world_uniforms_.ocean_wave_phases,
        static_cast<GLsizei>(
            ocean_phase_uniforms.size()),
        glm::value_ptr(
            ocean_phase_uniforms.front()));

    glUniform1i(
        world_uniforms_.ocean_wave_count,
        std::clamp(
            quality_settings.ocean_wave_count,
            1,
            static_cast<int>(
                kOceanMaxWaveCount)));

    glUniform1f(
        world_uniforms_.ocean_foam_threshold,
        ocean.foam_threshold);

    glUniform1f(
        world_uniforms_.ocean_detail_strength,
        ocean.detail_strength *
            quality_settings.ocean_detail_scale);

    glUniform1f(
        world_uniforms_.ocean_detail_phase,
        ocean.detail_phase);

    glUniform1f(
        world_uniforms_.ocean_severity,
        ocean.severity);

    glUniform1f(
        world_uniforms_.ocean_tempest_factor,
        ocean.tempest_factor);

    glUniform1f(
        world_uniforms_.ocean_open_sea,
        ocean_profile == OceanSurfaceProfile::OpenSea
            ? 1.0F
            : 0.0F);

    glUniformMatrix4fv(world_uniforms_.model, 1, GL_FALSE, glm::value_ptr(identity_model));
    glUniformMatrix4fv(world_uniforms_.view_projection, 1, GL_FALSE, glm::value_ptr(view_projection));
    glUniformMatrix4fv(world_uniforms_.light_view_projection, 1, GL_FALSE, glm::value_ptr(light_view_projection));
    glUniformMatrix4fv(
        world_uniforms_.light_view_projection_far,
        1,
        GL_FALSE,
        glm::value_ptr(light_view_projection_far));
    glUniformMatrix4fv(world_uniforms_.inverse_view_projection, 1, GL_FALSE, glm::value_ptr(inverse_view_projection));
    glUniform3fv(world_uniforms_.camera_position, 1, glm::value_ptr(eye));
    glUniform3fv(
        world_uniforms_.camera_forward,
        1,
        glm::value_ptr(camera_forward));
    glUniform3fv(world_uniforms_.sun_direction, 1, glm::value_ptr(environment.sun_direction));
    glUniform3fv(world_uniforms_.sun_color, 1, glm::value_ptr(environment.sun_color));
    glUniform3fv(world_uniforms_.ambient_color, 1, glm::value_ptr(environment.ambient_color));
    glUniform3fv(world_uniforms_.fog_color, 1, glm::value_ptr(environment.fog_color));
    glUniform3fv(world_uniforms_.distant_fog_color, 1, glm::value_ptr(environment.distant_fog_color));
    glUniform3fv(world_uniforms_.horizon_glow_color, 1, glm::value_ptr(environment.horizon_glow_color));
    glUniform3fv(world_uniforms_.night_tint_color, 1, glm::value_ptr(environment.night_tint_color));
    glUniform1f(world_uniforms_.daylight_factor, environment.daylight_factor);
    glUniform1f(world_uniforms_.sun_visibility, sun_visible ? 1.0F : 0.0F);
    glUniform1f(world_uniforms_.time_of_day, environment.time_of_day);
    glUniform1f(world_uniforms_.cloud_intensity, environment.cloud_intensity);
    glUniform1f(world_uniforms_.cloud_shadow_strength, environment.cloud_shadow_strength);
    glUniform1f(world_uniforms_.wind_strength, environment.wind_strength);
    glUniform1f(world_uniforms_.atmospheric_scatter_strength, environment.atmospheric_scatter_strength);
    glUniform1f(world_uniforms_.height_fog_density, environment.height_fog_density);
    glUniform1f(world_uniforms_.precipitation_intensity, environment.precipitation_intensity);
    glUniform1f(world_uniforms_.storm_intensity, environment.storm_intensity);
    glUniform1f(world_uniforms_.lightning_intensity, environment.lightning_intensity);
    glUniform1f(world_uniforms_.super_vision_strength, super_vision_strength);
    glUniform1i(world_uniforms_.atlas, 0);
    glUniform1i(world_uniforms_.shadow_map, 1);
    glUniform1i(world_uniforms_.shadow_map_far, 7);
    glUniform1i(
        world_uniforms_.shadow_cascade_count,
        shadow_cascade_count);
    glUniform1f(
        world_uniforms_.shadow_split_distance,
        shadow_split_distance);
    glUniform1f(
        world_uniforms_.shadow_transition_width,
        shadow_transition_width);
    glUniform1i(world_uniforms_.scene_color, 2);
    glUniform1i(world_uniforms_.scene_depth, 3);
    glUniform1i(world_uniforms_.shadows_enabled, options_.shadows_enabled ? 1 : 0);

    // Je force des textures neutres hors passe de refraction pour que les
    // samplers scene/depth ne pointent jamais vers des ressources sans lien.
    const auto opaque_scene_bindings = select_scene_sampler_bindings(
        false,
        scene_fallback_color_texture_,
        scene_fallback_depth_texture_,
        scene_color_texture_,
        scene_depth_texture_);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, atlas_texture_);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, shadow_map_);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, opaque_scene_bindings.color_texture);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, opaque_scene_bindings.depth_texture);
    glActiveTexture(GL_TEXTURE7);
    glBindTexture(GL_TEXTURE_2D, shadow_map_far_);

    for (const auto& visible_chunk : visible_chunks) {
        if (visible_chunk.mesh->opaque_index_count == 0) {
            continue;
        }
        glBindVertexArray(visible_chunk.mesh->vao);
        glDrawElements(GL_TRIANGLES, visible_chunk.mesh->opaque_index_count, GL_UNSIGNED_INT, nullptr);
        record_triangle_draw(visible_chunk.mesh->opaque_index_count);
        ++frame_stats.world_chunks;
    }

    const auto bind_modern_surface_program =
        [&](GLuint program,
            const ModernTerrainUniformLocations& uniforms,
            float triplanar_sharpness) {
            glUseProgram(program);
            glUniformMatrix4fv(
                uniforms.model,
                1,
                GL_FALSE,
                glm::value_ptr(identity_model));
            glUniformMatrix4fv(
                uniforms.view_projection,
                1,
                GL_FALSE,
                glm::value_ptr(view_projection));
            glUniformMatrix4fv(
                uniforms.light_view_projection,
                1,
                GL_FALSE,
                glm::value_ptr(light_view_projection));
            glUniformMatrix4fv(
                uniforms.light_view_projection_far,
                1,
                GL_FALSE,
                glm::value_ptr(light_view_projection_far));
            glUniform3fv(
                uniforms.camera_position,
                1,
                glm::value_ptr(eye));
            glUniform3fv(
                uniforms.camera_forward,
                1,
                glm::value_ptr(camera_forward));
            glUniform3fv(
                uniforms.sun_direction,
                1,
                glm::value_ptr(environment.sun_direction));
            glUniform3fv(
                uniforms.sun_color,
                1,
                glm::value_ptr(environment.sun_color));
            glUniform3fv(
                uniforms.ambient_color,
                1,
                glm::value_ptr(environment.ambient_color));
            glUniform3fv(
                uniforms.fog_color,
                1,
                glm::value_ptr(environment.fog_color));
            glUniform3fv(
                uniforms.distant_fog_color,
                1,
                glm::value_ptr(environment.distant_fog_color));
            glUniform3fv(
                uniforms.night_tint_color,
                1,
                glm::value_ptr(environment.night_tint_color));
            glUniform1f(uniforms.daylight_factor, environment.daylight_factor);
            glUniform1f(
                uniforms.sun_visibility,
                sun_visible ? 1.0F : 0.0F);
            glUniform1f(
                uniforms.precipitation_intensity,
                environment.precipitation_intensity);
            glUniform1f(uniforms.storm_intensity, environment.storm_intensity);
            glUniform1f(
                uniforms.lightning_intensity,
                environment.lightning_intensity);
            glUniform1f(uniforms.triplanar_sharpness, triplanar_sharpness);
            glUniform1f(
                uniforms.material_detail_scale,
                quality_settings.material_detail_scale);
            glUniform1i(
                uniforms.shadows_enabled,
                options_.shadows_enabled ? 1 : 0);
            glUniform1i(uniforms.material_albedo, 4);
            glUniform1i(uniforms.material_normal_height, 5);
            glUniform1i(uniforms.material_orm_emission, 6);
            glUniform1i(uniforms.shadow_map, 1);
            glUniform1i(uniforms.shadow_map_far, 7);
            glUniform1i(
                uniforms.shadow_cascade_count,
                shadow_cascade_count);
            glUniform1f(
                uniforms.shadow_split_distance,
                shadow_split_distance);
            glUniform1f(
                uniforms.shadow_transition_width,
                shadow_transition_width);

            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, shadow_map_);
            glActiveTexture(GL_TEXTURE7);
            glBindTexture(GL_TEXTURE_2D, shadow_map_far_);
            glActiveTexture(GL_TEXTURE4);
            glBindTexture(
                GL_TEXTURE_2D_ARRAY,
                modern_material_albedo_texture_);
            glActiveTexture(GL_TEXTURE5);
            glBindTexture(
                GL_TEXTURE_2D_ARRAY,
                modern_material_normal_height_texture_);
            glActiveTexture(GL_TEXTURE6);
            glBindTexture(
                GL_TEXTURE_2D_ARRAY,
                modern_material_orm_emission_texture_);
        };

    if (options_.visual_pipeline == VisualPipeline::ModernStylized &&
        modern_terrain_program_ != 0 &&
        modern_architecture_program_ != 0 &&
        modern_material_albedo_texture_ != 0 &&
        modern_material_normal_height_texture_ != 0 &&
        modern_material_orm_emission_texture_ != 0) {
        glUseProgram(modern_terrain_program_);
        glUniformMatrix4fv(
            modern_terrain_uniforms_.model,
            1,
            GL_FALSE,
            glm::value_ptr(identity_model));
        glUniformMatrix4fv(
            modern_terrain_uniforms_.view_projection,
            1,
            GL_FALSE,
            glm::value_ptr(view_projection));
        glUniformMatrix4fv(
            modern_terrain_uniforms_.light_view_projection,
            1,
            GL_FALSE,
            glm::value_ptr(light_view_projection));
        glUniformMatrix4fv(
            modern_terrain_uniforms_.light_view_projection_far,
            1,
            GL_FALSE,
            glm::value_ptr(light_view_projection_far));
        glUniform3fv(
            modern_terrain_uniforms_.camera_position,
            1,
            glm::value_ptr(eye));
        glUniform3fv(
            modern_terrain_uniforms_.camera_forward,
            1,
            glm::value_ptr(camera_forward));
        glUniform3fv(
            modern_terrain_uniforms_.sun_direction,
            1,
            glm::value_ptr(environment.sun_direction));
        glUniform3fv(
            modern_terrain_uniforms_.sun_color,
            1,
            glm::value_ptr(environment.sun_color));
        glUniform3fv(
            modern_terrain_uniforms_.ambient_color,
            1,
            glm::value_ptr(environment.ambient_color));
        glUniform3fv(
            modern_terrain_uniforms_.fog_color,
            1,
            glm::value_ptr(environment.fog_color));
        glUniform3fv(
            modern_terrain_uniforms_.distant_fog_color,
            1,
            glm::value_ptr(environment.distant_fog_color));
        glUniform3fv(
            modern_terrain_uniforms_.night_tint_color,
            1,
            glm::value_ptr(environment.night_tint_color));
        glUniform1f(
            modern_terrain_uniforms_.daylight_factor,
            environment.daylight_factor);
        glUniform1f(
            modern_terrain_uniforms_.sun_visibility,
            sun_visible ? 1.0F : 0.0F);
        glUniform1f(
            modern_terrain_uniforms_.precipitation_intensity,
            environment.precipitation_intensity);
        glUniform1f(
            modern_terrain_uniforms_.storm_intensity,
            environment.storm_intensity);
        glUniform1f(
            modern_terrain_uniforms_.lightning_intensity,
            environment.lightning_intensity);
        glUniform1f(
            modern_terrain_uniforms_.triplanar_sharpness,
            5.5F);
        glUniform1f(
            modern_terrain_uniforms_.material_detail_scale,
            quality_settings.material_detail_scale);
        glUniform1i(
            modern_terrain_uniforms_.shadows_enabled,
            options_.shadows_enabled ? 1 : 0);
        glUniform1i(modern_terrain_uniforms_.material_albedo, 4);
        glUniform1i(modern_terrain_uniforms_.material_normal_height, 5);
        glUniform1i(modern_terrain_uniforms_.material_orm_emission, 6);
        glUniform1i(modern_terrain_uniforms_.shadow_map, 1);
        glUniform1i(modern_terrain_uniforms_.shadow_map_far, 7);
        glUniform1i(
            modern_terrain_uniforms_.shadow_cascade_count,
            shadow_cascade_count);
        glUniform1f(
            modern_terrain_uniforms_.shadow_split_distance,
            shadow_split_distance);
        glUniform1f(
            modern_terrain_uniforms_.shadow_transition_width,
            shadow_transition_width);

        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, shadow_map_);
        glActiveTexture(GL_TEXTURE7);
        glBindTexture(GL_TEXTURE_2D, shadow_map_far_);
        glActiveTexture(GL_TEXTURE4);
        glBindTexture(GL_TEXTURE_2D_ARRAY, modern_material_albedo_texture_);
        glActiveTexture(GL_TEXTURE5);
        glBindTexture(
            GL_TEXTURE_2D_ARRAY,
            modern_material_normal_height_texture_);
        glActiveTexture(GL_TEXTURE6);
        glBindTexture(
            GL_TEXTURE_2D_ARRAY,
            modern_material_orm_emission_texture_);

        for (const auto& visible_chunk : visible_chunks) {
            if (visible_chunk.mesh->terrain_index_count == 0) {
                continue;
            }
            glBindVertexArray(visible_chunk.mesh->terrain_vao);
            glDrawElements(
                GL_TRIANGLES,
                visible_chunk.mesh->terrain_index_count,
                GL_UNSIGNED_INT,
                nullptr);
            record_triangle_draw(visible_chunk.mesh->terrain_index_count);
            if (visible_chunk.mesh->opaque_index_count == 0) {
                ++frame_stats.world_chunks;
            }
        }

        bind_modern_surface_program(
            modern_architecture_program_,
            modern_architecture_uniforms_,
            8.0F);
        for (const auto& visible_chunk : visible_chunks) {
            if (visible_chunk.mesh->architecture_opaque_index_count == 0) {
                continue;
            }
            glBindVertexArray(visible_chunk.mesh->architecture_vao);
            glDrawElements(
                GL_TRIANGLES,
                visible_chunk.mesh->architecture_opaque_index_count,
                GL_UNSIGNED_INT,
                nullptr);
            record_triangle_draw(
                visible_chunk.mesh->architecture_opaque_index_count);
            if (visible_chunk.mesh->opaque_index_count == 0 &&
                visible_chunk.mesh->terrain_index_count == 0) {
                ++frame_stats.world_chunks;
            }
        }

        // Je restaure le programme du monde pour le navire et l'eau.
        glUseProgram(world_program_);
    }
    if (ship_visibility.camera) {
        glUniformMatrix4fv(
            world_uniforms_.model,
            1,
            GL_FALSE,
            glm::value_ptr(
                ship.model_matrix));
        glBindVertexArray(ship_gpu_mesh_.vao);
        glDrawElements(GL_TRIANGLES, ship_gpu_mesh_.opaque_index_count, GL_UNSIGNED_INT, nullptr);
        record_triangle_draw(ship_gpu_mesh_.opaque_index_count);
        glUniformMatrix4fv(world_uniforms_.model, 1, GL_FALSE, glm::value_ptr(identity_model));
    }
    end_gpu_pass(GpuTimedPass::Opaque);

    begin_gpu_pass(GpuTimedPass::Entities);
    draw_item_drops(
        item_drops,
        view_projection,
        light_view_projection,
        light_view_projection_far,
        shadow_cascade_count,
        shadow_split_distance,
        shadow_transition_width,
        inverse_view_projection,
        eye,
        camera_forward,
        environment,
        sun_visible);
    draw_creatures(
        creatures,
        crew,
        old_guard,
        view_projection,
        light_view_projection,
        light_view_projection_far,
        shadow_cascade_count,
        shadow_split_distance,
        shadow_transition_width,
        eye,
        camera_forward,
        environment,
        selected_hotbar_emits_local_light(hotbar),
        super_vision_strength);
    end_gpu_pass(GpuTimedPass::Entities);

    begin_gpu_pass(GpuTimedPass::Sky);
    draw_sky(inverse_sky_view_projection, environment, quality_settings);
    end_gpu_pass(GpuTimedPass::Sky);

    begin_gpu_pass(GpuTimedPass::Water);
    if (has_visible_water) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, water_scene_framebuffer_);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, final_target_framebuffer);
        glBlitFramebuffer(
            0,
            0,
            render_width,
            render_height,
            0,
            0,
            render_width,
            render_height,
            GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT,
            GL_NEAREST);

        glBindFramebuffer(GL_FRAMEBUFFER, final_target_framebuffer);
        glViewport(0, 0, render_width, render_height);
        glUseProgram(world_program_);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, atlas_texture_);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, shadow_map_);
        const auto water_scene_bindings = select_scene_sampler_bindings(
            true,
            scene_fallback_color_texture_,
            scene_fallback_depth_texture_,
            water_scene_color_texture_,
            water_scene_depth_texture_);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, water_scene_bindings.color_texture);
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, water_scene_bindings.depth_texture);
        glEnable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);
        glDisable(GL_CULL_FACE);
        glDepthMask(GL_TRUE);

        for (const auto& visible_chunk : visible_chunks) {
            if (visible_chunk.mesh->water_index_count == 0) {
                continue;
            }
            glBindVertexArray(visible_chunk.mesh->water_vao);
            glDrawElements(
                GL_TRIANGLES,
                visible_chunk.mesh->water_index_count,
                GL_UNSIGNED_INT,
                nullptr);
            record_triangle_draw(visible_chunk.mesh->water_index_count);
        }

        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
    }

    // Le verre est composé après l'eau. Il n'écrit volontairement pas dans le
    // depth buffer ; lorsqu'il était dessiné avant l'eau, l'écriture de
    // profondeur de cette dernière pouvait l'effacer alors qu'il se trouvait
    // pourtant devant la surface.
    if (options_.visual_pipeline == VisualPipeline::ModernStylized &&
        modern_architecture_program_ != 0) {
        const auto has_transparent_architecture =
            std::any_of(
                visible_chunks.begin(),
                visible_chunks.end(),
                [](const VisibleChunk& visible_chunk) {
                    return visible_chunk.mesh
                               ->architecture_transparent_index_count > 0;
                });
        if (has_transparent_architecture) {
            bind_modern_surface_program(
                modern_architecture_program_,
                modern_architecture_uniforms_,
                8.0F);
            glEnable(GL_DEPTH_TEST);
            glDepthMask(GL_FALSE);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glEnable(GL_CULL_FACE);
            glCullFace(GL_BACK);

            // Le tri reste effectué du chunk le plus lointain au plus proche.
            // Cela stabilise la majorité des bâtiments sans coût de tri par
            // triangle à chaque frame.
            for (auto iterator = visible_chunks.rbegin();
                 iterator != visible_chunks.rend();
                 ++iterator) {
                const auto* mesh = iterator->mesh;
                if (mesh->architecture_transparent_index_count == 0) {
                    continue;
                }
                glBindVertexArray(mesh->architecture_vao);
                glDrawElements(
                    GL_TRIANGLES,
                    mesh->architecture_transparent_index_count,
                    GL_UNSIGNED_INT,
                    reinterpret_cast<const void*>(
                        static_cast<std::uintptr_t>(
                            mesh->architecture_transparent_index_offset_bytes)));
                record_triangle_draw(
                    mesh->architecture_transparent_index_count);
            }

            glDepthMask(GL_TRUE);
            glDisable(GL_BLEND);
        }
    }

    draw_precipitation(
        view_projection,
        inverse_view,
        eye,
        environment,
        ocean,
        ship,
        quality_settings,
        frame_stats);
    draw_old_guard_effects(
        old_guard_flashes,
        old_guard_smoke,
        view_projection,
        inverse_view,
        eye);
    draw_old_guard_effects(
        std::span<const OldGuardMuzzleFlashInstance> {},
        player_musket_smoke,
        view_projection,
        inverse_view,
        eye);
    end_gpu_pass(GpuTimedPass::Water);

    draw_block_break_overlay(world, player);

    if (!menu_preview_visible) {
        const auto viewmodel_pose =
            draw_player_viewmodel(
            player,
            resolve_viewmodel_held_item(inventory_menu, hotbar),
            player_musket,
            viewmodel_projection * view,
            light_view_projection,
            eye,
            environment);
        draw_old_guard_effects(
            player_musket_flashes,
            std::span<const OldGuardSmokeInstance> {},
            viewmodel_projection * view,
            inverse_view,
            eye,
            true,
            &viewmodel_pose);
    }

    auto camera_weather_exposure = 1.0F;
    if (ship_protection_is_renderable(ship)) {
        const auto local_eye =
            glm::vec3 {
                glm::inverse(ship.model_matrix) *
                glm::vec4 {eye, 1.0F},
            };
        if (ship.blueprint->protection_profile
                .shelters_from_weather_local(local_eye)) {
            camera_weather_exposure = 0.0F;
        }
    }

    begin_gpu_pass(GpuTimedPass::PostProcess);
    if (menu_preview_visible) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        run_menu_background_pass(
            render_width,
            render_height,
            environment.exposure);
    } else if (optional_post_process_enabled ||
               modern_output_resolve_required) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        run_post_process(
            environment,
            camera_weather_exposure,
            render_width,
            render_height,
            optional_post_process_enabled);
    } else if (has_visible_water) {
        // Le pipeline Legacy conserve son comportement historique. Le pipeline
        // moderne passe toujours par run_post_process afin de convertir sa
        // cible HDR linéaire vers l'écran SDR, même lorsque les effets sont
        // désactivés dans les options.
        glBindFramebuffer(GL_READ_FRAMEBUFFER, scene_framebuffer_);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        glBlitFramebuffer(
            0,
            0,
            render_width,
            render_height,
            0,
            0,
            render_width,
            render_height,
            GL_COLOR_BUFFER_BIT,
            GL_NEAREST);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    } else {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
    end_gpu_pass(GpuTimedPass::PostProcess);

    begin_gpu_pass(GpuTimedPass::Ui);
    if (main_menu.visible) {
        draw_main_menu(main_menu, width, height);
    } else if (save_slot_menu.visible) {
        draw_save_slot_menu(save_slot_menu, width, height);
    } else if (options_menu.visible) {
        draw_options_menu(options_menu, width, height);
    } else if (death_screen.visible) {
        draw_death_screen(death_screen, width, height);
    } else if (pause_menu.visible) {
        draw_pause_menu(pause_menu, width, height);
    } else if (inventory_menu.visible) {
        draw_inventory_menu(inventory_menu, hotbar, width, height);
    } else {
        draw_hotbar(player, hotbar, progression, environment, width, height);
        draw_maritime_hud(maritime_hud, width, height);
        if (player_musket.active) {
            draw_musket_hud(
                player_musket,
                width,
                height);
        } else {
            draw_crosshair();
        }
        draw_gameplay_announcement(gameplay_announcement, width, height);
    }
    if (confirm_dialog.visible) {
        draw_confirm_dialog(confirm_dialog, width, height);
    }
    draw_command_console(
        command_console,
        width,
        height);
    end_gpu_pass(GpuTimedPass::Ui);
    end_gpu_frame();
    frame_stats.world_ms = std::chrono::duration<double, std::milli>(clock::now() - world_start).count();
    frame_stats.draw_calls = frame_draw_calls_;
    frame_stats.triangles = frame_triangles_;
    frame_stats.uploaded_bytes = frame_uploaded_bytes_;
    if (options_.collect_detailed_stats) {
        frame_stats.gpu_buffer_bytes = estimate_gpu_buffer_bytes();
        frame_stats.gpu_texture_bytes = estimate_gpu_texture_bytes();
    }
    last_frame_stats_ = frame_stats;
}

void Renderer::render_loading_screen(std::string_view title,
                                     std::string_view detail,
                                     float progress,
                                     int width,
                                     int height) {
    LoadingScreenView view {};
    view.title = title;
    view.detail = detail;
    view.progress = progress;
    render_loading_screen(view, width, height);
}

void Renderer::render_loading_screen(const LoadingScreenView& view, int width, int height) {
    if (!initialized_ || width <= 0 || height <= 0 || hud_program_ == 0 || hud_vao_ == 0 || hud_vbo_ == 0) {
        return;
    }

    const auto layout = make_loading_screen_layout(view.theme, width, height);
    const auto viewport_width = layout.viewport_width;
    const auto viewport_height = layout.viewport_height;
    const auto clamped_progress = std::isfinite(view.progress) ? std::clamp(view.progress, 0.0F, 1.0F) : 0.0F;
    const auto animation_phase = std::isfinite(view.animation_phase)
                                     ? view.animation_phase - std::floor(view.animation_phase)
                                     : 0.0F;
    const auto title_text = view.title.empty() ? std::string_view("VALCRAFT") : view.title;
    const auto detail_text = view.detail.empty() ? std::string_view("CHARGEMENT DU MONDE") : view.detail;
    auto& vertices = loading_vertices_scratch_;
    vertices.clear();
    if (vertices.capacity() < 32768U) {
        vertices.reserve(32768U);
    }

    const auto fitted_pixel_size = [](std::string_view text, float preferred, float maximum_width) {
        const auto unit_width = measure_pixel_text(text, 1.0F);
        if (unit_width <= 0.0F) {
            return std::max(1.0F, preferred);
        }
        return std::max(1.0F, std::floor(std::min(preferred, maximum_width / unit_width)));
    };
    const auto title_pixel_size = fitted_pixel_size(title_text, layout.title_pixel_size, layout.content_width - 24.0F);
    const auto detail_pixel_size = fitted_pixel_size(detail_text, layout.detail_pixel_size, layout.content_width - 24.0F);
    const auto percent_pixel_size = std::max(2.0F, layout.detail_pixel_size);

    const auto draw_text = [&](float x,
                               float y,
                               float pixel_size,
                               std::string_view text,
                               const HudColor& color,
                               bool centered = false) {
        if (text.empty() || color[3] <= 0.0F) {
            return;
        }
        append_pixel_text(
            vertices,
            viewport_width,
            viewport_height,
            x + pixel_size,
            y + pixel_size,
            pixel_size,
            text,
            {0.0F, 0.0F, 0.0F, color[3] * 0.48F},
            centered);
        append_pixel_text(
            vertices,
            viewport_width,
            viewport_height,
            x,
            y,
            pixel_size,
            text,
            color,
            centered);
    };

    if (view.theme == LoadingScreenTheme::Maritime) {
        // Je construis le decor maritime avec des primitives deja presentes dans le HUD.
        append_hud_rect_top_left(vertices, viewport_width, viewport_height, 0.0F, 0.0F, viewport_width, viewport_height, {0.024F, 0.094F, 0.153F, 1.0F});
        append_hud_rect_top_left(vertices, viewport_width, viewport_height, 0.0F, viewport_height * 0.18F, viewport_width, viewport_height * 0.34F, {0.035F, 0.176F, 0.250F, 0.52F});
        append_hud_rect_top_left(vertices, viewport_width, viewport_height, 0.0F, layout.horizon_y - viewport_height * 0.06F, viewport_width, viewport_height * 0.12F, {0.36F, 0.53F, 0.55F, 0.12F});

        constexpr std::array<std::array<float, 2>, 14> kStars {{
            {{0.08F, 0.13F}}, {{0.16F, 0.24F}}, {{0.24F, 0.09F}}, {{0.32F, 0.19F}},
            {{0.41F, 0.12F}}, {{0.49F, 0.27F}}, {{0.57F, 0.08F}}, {{0.64F, 0.22F}},
            {{0.72F, 0.14F}}, {{0.79F, 0.28F}}, {{0.86F, 0.10F}}, {{0.92F, 0.21F}},
            {{0.37F, 0.30F}}, {{0.68F, 0.32F}},
        }};
        for (std::size_t index = 0; index < kStars.size(); ++index) {
            const auto twinkle = 0.38F + 0.30F * std::sin(
                animation_phase * 6.2831853F + static_cast<float>(index) * 1.73F);
            const auto star_size = index % 3U == 0U ? 2.0F : 1.0F;
            append_hud_rect_top_left(
                vertices,
                viewport_width,
                viewport_height,
                kStars[index][0] * viewport_width,
                kStars[index][1] * viewport_height,
                star_size,
                star_size,
                {0.90F, 0.90F, 0.78F, std::clamp(twinkle, 0.10F, 0.72F)});
        }

        const auto moon_size = std::clamp(std::min(viewport_width, viewport_height) * 0.036F, 14.0F, 44.0F);
        const auto moon_x = viewport_width * 0.80F;
        const auto moon_y = viewport_height * 0.13F;
        append_hud_rect_top_left(vertices, viewport_width, viewport_height, moon_x, moon_y + moon_size * 0.18F, moon_size, moon_size * 0.64F, {0.88F, 0.79F, 0.58F, 0.32F});
        append_hud_rect_top_left(vertices, viewport_width, viewport_height, moon_x + moon_size * 0.18F, moon_y, moon_size * 0.64F, moon_size, {0.94F, 0.86F, 0.64F, 0.32F});

        append_hud_rect_top_left(vertices, viewport_width, viewport_height, 0.0F, layout.horizon_y, viewport_width, viewport_height - layout.horizon_y, {0.025F, 0.225F, 0.290F, 1.0F});

        const auto ship_unit = std::clamp(std::min(viewport_width, viewport_height) * 0.012F, 3.0F, 12.0F);
        const auto ship_center_x = viewport_width * 0.5F;
        const auto ship_hull_y = layout.horizon_y - ship_unit * 0.25F;
        const auto silhouette = HudColor {0.015F, 0.050F, 0.070F, 0.98F};
        const auto sail_color =
            HudColor {
                0.008F,
                0.010F,
                0.016F,
                0.98F,
            };

        const auto mast_color =
            HudColor {
                0.86F,
                0.60F,
                0.16F,
                0.96F,
            };
        append_hud_rect_top_left(vertices, viewport_width, viewport_height, ship_center_x - ship_unit * 6.8F, ship_hull_y, ship_unit * 13.6F, ship_unit, silhouette);
        append_hud_rect_top_left(vertices, viewport_width, viewport_height, ship_center_x - ship_unit * 5.9F, ship_hull_y + ship_unit, ship_unit * 11.8F, ship_unit, silhouette);
        append_hud_rect_top_left(vertices, viewport_width, viewport_height, ship_center_x - ship_unit * 4.6F, ship_hull_y + ship_unit * 2.0F, ship_unit * 9.2F, ship_unit * 0.8F, silhouette);
        append_hud_rect_top_left(
            vertices,
            viewport_width,
            viewport_height,
            ship_center_x + ship_unit * 6.5F,
            ship_hull_y - ship_unit * 0.42F,
            ship_unit * 2.0F,
            ship_unit * 0.24F,
            mast_color);

        // Je sépare nettement les trois gréements pour garder la silhouette lisible même en petite résolution.
        const auto append_square_rig = [&](float mast_offset,
                                           float mast_height,
                                           float yard_height,
                                           float sail_width,
                                           int sail_steps) {
            const auto mast_x = ship_center_x + ship_unit * mast_offset;
            const auto mast_width = std::max(1.0F, ship_unit * 0.28F);
            const auto yard_thickness = std::max(1.0F, ship_unit * 0.22F);
            for (int step = 0; step < sail_steps; ++step) {
                const auto progress = sail_steps > 1
                                          ? static_cast<float>(step) / static_cast<float>(sail_steps - 1)
                                          : 1.0F;
                const auto row_width = ship_unit * sail_width * (0.30F + progress * 0.70F);
                append_hud_rect_top_left(
                    vertices,
                    viewport_width,
                    viewport_height,
                    mast_x - row_width * 0.5F,
                    ship_hull_y - ship_unit * yard_height + ship_unit * (0.45F + static_cast<float>(step) * 0.68F),
                    row_width,
                    ship_unit * 0.62F,
                    sail_color);
            }
            append_hud_rect_top_left(
                vertices,
                viewport_width,
                viewport_height,
                mast_x - mast_width * 0.5F,
                ship_hull_y - ship_unit * mast_height,
                mast_width,
                ship_unit * (mast_height + 0.2F),
                mast_color);
            append_hud_rect_top_left(
                vertices,
                viewport_width,
                viewport_height,
                mast_x - ship_unit * sail_width * 0.58F,
                ship_hull_y - ship_unit * yard_height,
                ship_unit * sail_width * 1.16F,
                yard_thickness,
                mast_color);
        };
        append_square_rig(-3.8F, 7.1F, 6.2F, 3.0F, 6);
        append_square_rig(0.0F, 9.2F, 8.0F, 3.7F, 8);
        append_square_rig(3.8F, 7.5F, 6.5F, 2.8F, 6);
        append_hud_rect_top_left(vertices, viewport_width, viewport_height, ship_center_x - ship_unit * 0.14F, ship_hull_y - ship_unit * 9.7F, ship_unit * 2.4F, ship_unit * 0.55F, {0.72F, 0.51F, 0.21F, 0.82F});

        const auto append_wave_layer = [&](float baseline, float amplitude, float segment_width, float phase_offset, const HudColor& color) {
            const auto segment_count = static_cast<int>(std::ceil(viewport_width / segment_width)) + 2;
            for (int segment = -1; segment < segment_count; ++segment) {
                const auto segment_value = static_cast<float>(segment);
                const auto wave = std::sin(segment_value * 0.72F + animation_phase * 6.2831853F + phase_offset);
                const auto y = baseline + wave * amplitude;
                append_hud_rect_top_left(
                    vertices,
                    viewport_width,
                    viewport_height,
                    segment_value * segment_width,
                    y,
                    segment_width + 1.0F,
                    std::max(0.0F, viewport_height - y),
                    color);
            }
        };
        append_wave_layer(layout.horizon_y + ship_unit * 1.2F, ship_unit * 0.65F, ship_unit * 2.4F, 1.8F, {0.025F, 0.300F, 0.355F, 0.96F});
        append_wave_layer(layout.horizon_y + ship_unit * 2.2F, ship_unit * 0.85F, ship_unit * 2.8F, 3.9F, {0.018F, 0.235F, 0.310F, 0.98F});
    } else {
        append_hud_rect_top_left(vertices, viewport_width, viewport_height, 0.0F, 0.0F, viewport_width, viewport_height, {0.04F, 0.05F, 0.06F, 1.0F});
        append_hud_rect_top_left(vertices, viewport_width, viewport_height, 0.0F, 0.0F, viewport_width, viewport_height * 0.36F, {0.07F, 0.08F, 0.09F, 0.72F});
        append_hud_rect_top_left(vertices, viewport_width, viewport_height, 0.0F, viewport_height * 0.64F, viewport_width, viewport_height * 0.36F, {0.02F, 0.03F, 0.04F, 0.84F});
    }

    const auto panel_palette = view.theme == LoadingScreenTheme::Maritime
                                   ? HudPanelPalette {
                                         {0.025F, 0.145F, 0.195F, 0.98F},
                                         {0.018F, 0.075F, 0.110F, 0.91F},
                                         {0.29F, 0.62F, 0.68F, 0.20F},
                                         {0.005F, 0.020F, 0.035F, 0.72F},
                                         {0.84F, 0.68F, 0.36F, 0.28F},
                                     }
                                   : make_stone_panel_palette();
    append_stylized_panel_top_left(
        vertices,
        viewport_width,
        viewport_height,
        layout.content_x,
        layout.panel_y,
        layout.content_width,
        layout.panel_height,
        view.theme == LoadingScreenTheme::Maritime ? 3.0F : 5.0F,
        panel_palette,
        false);

    draw_text(
        viewport_width * 0.5F,
        layout.title_y,
        title_pixel_size,
        title_text,
        view.theme == LoadingScreenTheme::Maritime
            ? HudColor {0.96F, 0.92F, 0.80F, 1.0F}
            : HudColor {0.98F, 0.95F, 0.88F, 1.0F},
        true);
    draw_text(
        viewport_width * 0.5F,
        layout.detail_y,
        detail_pixel_size,
        detail_text,
        view.theme == LoadingScreenTheme::Maritime
            ? HudColor {0.67F, 0.84F, 0.85F, 0.96F}
            : HudColor {0.84F, 0.86F, 0.90F, 0.96F},
        true);

    const auto track_border = std::clamp(layout.track_height * 0.22F, 3.0F, 7.0F);
    const auto track_inner_x = layout.track_x + track_border;
    const auto track_inner_y = layout.track_y + track_border;
    const auto track_inner_width = std::max(0.0F, layout.track_width - track_border * 2.0F);
    const auto track_inner_height = std::max(0.0F, layout.track_height - track_border * 2.0F);
    const auto fill_width = track_inner_width * clamped_progress;
    const auto accent = view.theme == LoadingScreenTheme::Maritime
                            ? HudColor {0.84F, 0.68F, 0.36F, 1.0F}
                            : HudColor {0.94F, 0.76F, 0.32F, 1.0F};

    append_hud_frame_top_left(
        vertices,
        viewport_width,
        viewport_height,
        layout.track_x,
        layout.track_y,
        layout.track_width,
        layout.track_height,
        track_border,
        view.theme == LoadingScreenTheme::Maritime
            ? HudColor {0.52F, 0.43F, 0.26F, 0.88F}
            : HudColor {0.08F, 0.09F, 0.10F, 0.98F},
        view.theme == LoadingScreenTheme::Maritime
            ? HudColor {0.012F, 0.055F, 0.078F, 0.96F}
            : HudColor {0.08F, 0.10F, 0.12F, 0.88F});
    if (fill_width > 0.0F) {
        append_hud_rect_top_left(
            vertices,
            viewport_width,
            viewport_height,
            track_inner_x,
            track_inner_y,
            fill_width,
            track_inner_height,
            hud_with_alpha(hud_scale_rgb(accent, 0.88F), 0.96F));
        append_hud_rect_top_left(
            vertices,
            viewport_width,
            viewport_height,
            track_inner_x,
            track_inner_y,
            fill_width,
            std::max(2.0F, track_inner_height * 0.36F),
            hud_with_alpha(hud_scale_rgb(accent, 1.18F), 0.42F));

        const auto shine_width = std::clamp(track_inner_width * 0.08F, 10.0F, 42.0F);
        const auto desired_shine_x = track_inner_x - shine_width +
                                     (track_inner_width + shine_width) * animation_phase;
        const auto shine_x = std::max(track_inner_x, desired_shine_x);
        const auto shine_right = std::min(track_inner_x + fill_width, desired_shine_x + shine_width);
        if (shine_right > shine_x) {
            append_hud_rect_top_left(
                vertices,
                viewport_width,
                viewport_height,
                shine_x,
                track_inner_y,
                shine_right - shine_x,
                track_inner_height,
                hud_with_alpha(hud_scale_rgb(accent, 1.28F), 0.42F));
        }
    }

    std::array<char, 8> percent_buffer {};
    const auto percent = static_cast<int>(std::lround(clamped_progress * 100.0F));
    const auto percent_result = std::to_chars(percent_buffer.data(), percent_buffer.data() + percent_buffer.size() - 2, percent);
    auto percent_end = percent_result.ptr;
    if (percent_result.ec == std::errc {} && percent_end + 2 <= percent_buffer.data() + percent_buffer.size()) {
        *percent_end = ' ';
        ++percent_end;
        *percent_end = '%';
        ++percent_end;
    }
    const auto percent_text = std::string_view(
        percent_buffer.data(),
        static_cast<std::size_t>(percent_end - percent_buffer.data()));
    draw_text(
        viewport_width * 0.5F,
        layout.track_y + std::max(1.0F, (layout.track_height - percent_pixel_size * 7.0F) * 0.5F),
        percent_pixel_size,
        percent_text,
        {0.96F, 0.97F, 0.99F, 0.98F},
        true);

    if (view.theme == LoadingScreenTheme::Maritime) {
        auto current_quote = view.current_quote;
        auto next_quote = view.next_quote;
        if (current_quote.line1.empty() && current_quote.line2.empty()) {
            const auto quotes = maritime_loading_quotes();
            if (!quotes.empty()) {
                current_quote = quotes.front();
            }
        }
        if (next_quote.line1.empty() && next_quote.line2.empty()) {
            next_quote = current_quote;
        }
        const auto quote_blend = std::isfinite(view.quote_blend)
                                     ? std::clamp(view.quote_blend, 0.0F, 1.0F)
                                     : 0.0F;
        const auto quote_pixel_size = std::min(
            fitted_pixel_size(current_quote.line1, layout.quote_pixel_size, layout.content_width - 36.0F),
            fitted_pixel_size(current_quote.line2, layout.quote_pixel_size, layout.content_width - 36.0F));
        const auto draw_quote = [&](const LoadingQuoteView& quote, float alpha) {
            if (alpha <= 0.0F) {
                return;
            }
            const auto quote_color = HudColor {0.89F, 0.91F, 0.86F, 0.88F * alpha};
            draw_text(viewport_width * 0.5F, layout.quote_y, quote_pixel_size, quote.line1, quote_color, true);
            draw_text(viewport_width * 0.5F, layout.quote_y + quote_pixel_size * 9.0F, quote_pixel_size, quote.line2, quote_color, true);
            const auto author_pixel_size = fitted_pixel_size(
                quote.author,
                std::max(1.0F, quote_pixel_size),
                layout.content_width - 36.0F);
            draw_text(
                viewport_width * 0.5F,
                layout.author_y,
                author_pixel_size,
                quote.author,
                {0.84F, 0.68F, 0.36F, 0.92F * alpha},
                true);
        };
        draw_quote(current_quote, 1.0F - quote_blend);
        if (next_quote != current_quote) {
            draw_quote(next_quote, quote_blend);
        }
    } else {
        draw_text(
            viewport_width * 0.5F,
            layout.quote_y,
            detail_pixel_size,
            "PREPARATION EN COURS",
            {0.76F, 0.79F, 0.84F, 0.90F},
            true);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, std::max(width, 1), std::max(height, 1));
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    if (view.theme == LoadingScreenTheme::Maritime) {
        glClearColor(0.024F, 0.094F, 0.153F, 1.0F);
    } else {
        glClearColor(0.04F, 0.05F, 0.06F, 1.0F);
    }
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(hud_program_);
    bind_hud_textures();
    upload_hud_vertices(vertices);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size()));
    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
}

auto Renderer::last_frame_stats() const noexcept -> const RendererFrameStats& {
    return last_frame_stats_;
}

void Renderer::submit_cpu_frame_time_sample(
    double active_frame_time_ms) noexcept {
    pending_cpu_frame_time_ms_ =
        active_frame_time_ms;
    pending_cpu_frame_time_valid_ =
        std::isfinite(active_frame_time_ms) &&
        active_frame_time_ms > 0.0;
}

auto Renderer::material_pack_version() const noexcept -> std::uint16_t {
    return material_pack_version_;
}

auto Renderer::material_pack_checksum() const noexcept -> std::uint64_t {
    return material_pack_checksum_;
}

auto Renderer::last_initialization_error() const noexcept
    -> std::string_view {
    return last_initialization_error_;
}

void Renderer::create_gpu_timers() {
    gpu_timers_supported_ = GLAD_GL_VERSION_3_3 != 0 &&
                            glGenQueries != nullptr &&
                            glDeleteQueries != nullptr &&
                            glBeginQuery != nullptr &&
                            glEndQuery != nullptr &&
                            glGetQueryObjectiv != nullptr &&
                            glGetQueryObjectui64v != nullptr;
    if (!gpu_timers_supported_) {
        return;
    }

    for (auto& frame : gpu_query_frames_) {
        glGenQueries(static_cast<GLsizei>(frame.queries.size()), frame.queries.data());
        frame.issued.fill(false);
        frame.pending = false;
    }
}

void Renderer::destroy_gpu_timers() {
    if (gpu_timers_supported_ && glDeleteQueries != nullptr) {
        for (auto& frame : gpu_query_frames_) {
            glDeleteQueries(static_cast<GLsizei>(frame.queries.size()), frame.queries.data());
        }
    }
    gpu_query_frames_ = {};
    active_gpu_query_frame_ = -1;
    active_gpu_pass_ = -1;
    gpu_timers_supported_ = false;
}

void Renderer::begin_gpu_frame(RendererFrameStats& frame_stats) {
    active_gpu_query_frame_ = -1;
    active_gpu_pass_ = -1;
    if (!gpu_timers_supported_) {
        frame_stats.gpu = {};
        return;
    }

    RendererGpuTimings newest_resolved {};
    for (auto& query_frame : gpu_query_frames_) {
        if (!query_frame.pending) {
            continue;
        }

        auto all_available = true;
        for (std::size_t pass_index = 0; pass_index < kGpuTimedPassCount; ++pass_index) {
            if (!query_frame.issued[pass_index]) {
                continue;
            }
            GLint available = GL_FALSE;
            glGetQueryObjectiv(query_frame.queries[pass_index], GL_QUERY_RESULT_AVAILABLE, &available);
            if (available != GL_TRUE) {
                all_available = false;
                break;
            }
        }
        if (!all_available) {
            continue;
        }

        RendererGpuTimings resolved {};
        resolved.valid = true;
        resolved.source_frame = query_frame.frame_index;
        const auto latency = gpu_frame_index_ >= query_frame.frame_index ? gpu_frame_index_ - query_frame.frame_index : 0U;
        resolved.latency_frames = static_cast<std::uint32_t>(std::min<std::uint64_t>(latency, UINT32_MAX));
        for (std::size_t pass_index = 0; pass_index < kGpuTimedPassCount; ++pass_index) {
            if (!query_frame.issued[pass_index]) {
                continue;
            }

            GLuint64 elapsed_nanoseconds = 0;
            glGetQueryObjectui64v(query_frame.queries[pass_index], GL_QUERY_RESULT, &elapsed_nanoseconds);
            const auto elapsed_ms = gpu_elapsed_nanoseconds_to_milliseconds(elapsed_nanoseconds);
            switch (static_cast<GpuTimedPass>(pass_index)) {
            case GpuTimedPass::Shadow:
                resolved.shadow_ms = elapsed_ms;
                break;
            case GpuTimedPass::Opaque:
                resolved.opaque_ms = elapsed_ms;
                break;
            case GpuTimedPass::Sky:
                resolved.sky_ms = elapsed_ms;
                break;
            case GpuTimedPass::Entities:
                resolved.entities_ms = elapsed_ms;
                break;
            case GpuTimedPass::Water:
                resolved.water_ms = elapsed_ms;
                break;
            case GpuTimedPass::PostProcess:
                resolved.post_process_ms = elapsed_ms;
                break;
            case GpuTimedPass::Ui:
                resolved.ui_ms = elapsed_ms;
                break;
            case GpuTimedPass::Count:
                break;
            }
        }

        if (!last_gpu_timings_.valid || resolved.source_frame >= last_gpu_timings_.source_frame) {
            last_gpu_timings_ = resolved;
        }
        if (!newest_resolved.valid || resolved.source_frame >= newest_resolved.source_frame) {
            newest_resolved = resolved;
        }
        query_frame.pending = false;
        query_frame.issued.fill(false);
    }

    // Je publie uniquement un résultat fraîchement résolu pour ne pas biaiser les agrégats de télémétrie.
    frame_stats.gpu = newest_resolved;
    for (std::size_t frame_index = 0; frame_index < gpu_query_frames_.size(); ++frame_index) {
        if (!gpu_query_frames_[frame_index].pending) {
            active_gpu_query_frame_ = static_cast<int>(frame_index);
            auto& query_frame = gpu_query_frames_[frame_index];
            query_frame.frame_index = gpu_frame_index_;
            query_frame.issued.fill(false);
            break;
        }
    }
}

void Renderer::end_gpu_frame() {
    if (active_gpu_pass_ >= 0) {
        glEndQuery(GL_TIME_ELAPSED);
        active_gpu_pass_ = -1;
    }
    if (active_gpu_query_frame_ >= 0) {
        auto& query_frame = gpu_query_frames_[static_cast<std::size_t>(active_gpu_query_frame_)];
        query_frame.pending = std::any_of(query_frame.issued.begin(), query_frame.issued.end(), [](bool issued) {
            return issued;
        });
    }
    active_gpu_query_frame_ = -1;
    ++gpu_frame_index_;
}

void Renderer::begin_gpu_pass(GpuTimedPass pass) {
    if (active_gpu_query_frame_ < 0 || active_gpu_pass_ >= 0) {
        return;
    }
    const auto pass_index = static_cast<std::size_t>(pass);
    auto& query_frame = gpu_query_frames_[static_cast<std::size_t>(active_gpu_query_frame_)];
    glBeginQuery(GL_TIME_ELAPSED, query_frame.queries[pass_index]);
    query_frame.issued[pass_index] = true;
    active_gpu_pass_ = static_cast<int>(pass_index);
}

void Renderer::end_gpu_pass(GpuTimedPass pass) {
    if (active_gpu_pass_ != static_cast<int>(pass)) {
        return;
    }
    glEndQuery(GL_TIME_ELAPSED);
    active_gpu_pass_ = -1;
}

void Renderer::record_triangle_draw(GLsizei index_or_vertex_count, GLsizei instance_count) noexcept {
    ++frame_draw_calls_;
    if (index_or_vertex_count <= 0 || instance_count <= 0) {
        return;
    }
    const auto triangle_count = static_cast<std::uint64_t>(index_or_vertex_count / 3);
    frame_triangles_ += triangle_count * static_cast<std::uint64_t>(instance_count);
}

void Renderer::record_draw_call() noexcept {
    ++frame_draw_calls_;
}

auto Renderer::estimate_gpu_buffer_bytes() const noexcept -> std::uint64_t {
    auto total = std::uint64_t {0};
    const auto add_mesh = [&total](const GpuMesh& mesh) {
        total += static_cast<std::uint64_t>(std::max<GLsizeiptr>(mesh.vertex_buffer_bytes, 0));
        total += static_cast<std::uint64_t>(std::max<GLsizeiptr>(mesh.index_buffer_bytes, 0));
        total += static_cast<std::uint64_t>(
            std::max<GLsizeiptr>(mesh.water_vertex_buffer_bytes, 0));
        total += static_cast<std::uint64_t>(
            std::max<GLsizeiptr>(mesh.water_index_buffer_bytes, 0));
        total += static_cast<std::uint64_t>(
            std::max<GLsizeiptr>(mesh.terrain_vertex_buffer_bytes, 0));
        total += static_cast<std::uint64_t>(
            std::max<GLsizeiptr>(mesh.terrain_index_buffer_bytes, 0));
        total += static_cast<std::uint64_t>(
            std::max<GLsizeiptr>(mesh.architecture_vertex_buffer_bytes, 0));
        total += static_cast<std::uint64_t>(
            std::max<GLsizeiptr>(mesh.architecture_index_buffer_bytes, 0));
    };
    for (const auto& [coord, mesh] : gpu_meshes_) {
        static_cast<void>(coord);
        add_mesh(mesh);
    }
    add_mesh(block_break_overlay_mesh_);
    add_mesh(ship_gpu_mesh_);

    if (creature_vbo_ != 0) {
        total += static_cast<std::uint64_t>(
            std::max<GLsizeiptr>(creature_template_vertex_buffer_bytes_, 0));
    }
    if (creature_ebo_ != 0) {
        total += static_cast<std::uint64_t>(
            std::max<GLsizeiptr>(creature_template_index_buffer_bytes_, 0));
    }
    if (item_drop_vbo_ != 0) {
        total += static_cast<std::uint64_t>(
            std::max<GLsizeiptr>(item_drop_template_vertex_buffer_bytes_, 0));
    }
    if (item_drop_ebo_ != 0) {
        total += static_cast<std::uint64_t>(
            std::max<GLsizeiptr>(item_drop_template_index_buffer_bytes_, 0));
    }
    if (creature_instance_vbo_ != 0) {
        total += static_cast<std::uint64_t>(std::max<GLsizeiptr>(creature_instance_buffer_bytes_, 0));
    }
    if (viewmodel_instance_vbo_ != 0) {
        total += static_cast<std::uint64_t>(std::max<GLsizeiptr>(viewmodel_instance_buffer_bytes_, 0));
    }
    if (item_drop_instance_vbo_ != 0) {
        total += static_cast<std::uint64_t>(std::max<GLsizeiptr>(item_drop_instance_buffer_bytes_, 0));
    }
    if (precipitation_vbo_ != 0) {
        total += sizeof(float) * 8U;
    }
    if (precipitation_instance_vbo_ != 0) {
        total += static_cast<std::uint64_t>(
            std::max<GLsizeiptr>(precipitation_instance_buffer_bytes_, 0));
    }
    if (old_guard_effect_vbo_ != 0) {
        total += sizeof(float) * 8U;
    }
    if (old_guard_effect_instance_vbo_ != 0) {
        total += static_cast<std::uint64_t>(
            std::max<GLsizeiptr>(
                old_guard_effect_instance_buffer_bytes_,
                0));
    }
    if (hud_vbo_ != 0) {
        total += static_cast<std::uint64_t>(std::max<GLsizeiptr>(hud_vertex_buffer_bytes_, 0));
    }
    if (crosshair_vbo_ != 0) {
        total += sizeof(float) * 8U;
    }
    return total;
}

auto Renderer::estimate_gpu_texture_bytes() const noexcept -> std::uint64_t {
    auto total = std::uint64_t {0};
    const auto image_bytes = [](int width, int height, std::uint64_t bytes_per_pixel) {
        return static_cast<std::uint64_t>(std::max(width, 0)) *
               static_cast<std::uint64_t>(std::max(height, 0)) * bytes_per_pixel;
    };
    if (atlas_texture_ != 0) {
        total += image_bytes(kBlockAtlasSize, kBlockAtlasSize, 4U);
    }
    if (msdf_font_texture_ != 0) {
        auto mip_texel_count = std::uint64_t {0U};
        auto mip_width = std::max(msdf_font_width_, 1U);
        auto mip_height = std::max(msdf_font_height_, 1U);
        for (std::uint32_t mip = 0U;
             mip < msdf_font_mips_;
             ++mip) {
            mip_texel_count +=
                static_cast<std::uint64_t>(mip_width) *
                static_cast<std::uint64_t>(mip_height);
            mip_width = std::max(mip_width / 2U, 1U);
            mip_height = std::max(mip_height / 2U, 1U);
        }
        total += mip_texel_count * 3U;
    }
    if (model_icon_texture_ != 0) {
        auto mip_texel_count = std::uint64_t {0U};
        auto mip_width =
            std::max<std::uint16_t>(model_icon_width_, 1U);
        auto mip_height =
            std::max<std::uint16_t>(model_icon_height_, 1U);
        for (std::uint16_t mip = 0U;
             mip < model_icon_mips_;
             ++mip) {
            mip_texel_count +=
                static_cast<std::uint64_t>(mip_width) *
                static_cast<std::uint64_t>(mip_height);
            mip_width =
                std::max<std::uint16_t>(mip_width / 2U, 1U);
            mip_height =
                std::max<std::uint16_t>(mip_height / 2U, 1U);
        }
        total += mip_texel_count *
                 static_cast<std::uint64_t>(model_icon_layers_) *
                 4U;
    }
    if (modern_material_albedo_texture_ != 0 ||
        modern_material_normal_height_texture_ != 0 ||
        modern_material_orm_emission_texture_ != 0) {
        auto mip_texel_count = std::uint64_t {0U};
        auto mip_width = std::max<std::uint16_t>(material_pack_width_, 1U);
        auto mip_height = std::max<std::uint16_t>(material_pack_height_, 1U);
        for (std::uint16_t mip = 0U; mip < material_pack_mips_; ++mip) {
            mip_texel_count +=
                static_cast<std::uint64_t>(mip_width) *
                static_cast<std::uint64_t>(mip_height);
            mip_width = std::max<std::uint16_t>(mip_width / 2U, 1U);
            mip_height = std::max<std::uint16_t>(mip_height / 2U, 1U);
        }
        total += mip_texel_count *
                 static_cast<std::uint64_t>(material_pack_layers_) *
                 4U *
                 3U;
    }
    if (accent_texture_ != 0) {
        total += image_bytes(kAccentAtlasSize, kAccentAtlasSize, 4U);
    }
    if (creature_atlas_texture_ != 0) {
        total += image_bytes(kCreatureAtlasSize, kCreatureAtlasSize, 4U);
    }
    if (player_atlas_texture_ != 0) {
        total += image_bytes(kPlayerAtlasSize, kPlayerAtlasSize, 4U);
    }
    if (shadow_map_ != 0) {
        const auto size = options_.shadows_enabled ? std::max(options_.shadow_map_size, 1) : 1;
        total += image_bytes(size, size, 4U);
    }
    if (shadow_map_far_ != 0) {
        const auto size = options_.shadows_enabled
                              ? std::max(options_.shadow_map_size, 1)
                              : 1;
        total += image_bytes(size, size, 4U);
    }
    if (scene_fallback_color_texture_ != 0) {
        total += 4U;
    }
    if (scene_fallback_depth_texture_ != 0) {
        total += 4U;
    }
    if (water_scene_color_texture_ != 0) {
        total += image_bytes(
            water_scene_target_width_,
            water_scene_target_height_,
            color_target_bytes_per_pixel(water_scene_color_internal_format_));
    }
    if (water_scene_depth_texture_ != 0) {
        total += image_bytes(water_scene_target_width_, water_scene_target_height_, 4U);
    }
    if (scene_color_texture_ != 0) {
        total += image_bytes(
            scene_target_width_,
            scene_target_height_,
            color_target_bytes_per_pixel(scene_color_internal_format_));
    }
    if (scene_depth_texture_ != 0) {
        total += image_bytes(scene_target_width_, scene_target_height_, 4U);
    }
    if (glow_extract_texture_ != 0) {
        total += image_bytes(
            glow_target_width_,
            glow_target_height_,
            color_target_bytes_per_pixel(glow_color_internal_format_));
    }
    if (glow_ping_texture_ != 0) {
        total += image_bytes(
            glow_target_width_,
            glow_target_height_,
            color_target_bytes_per_pixel(glow_color_internal_format_));
    }
    return total;
}

void Renderer::drain_pending_world_meshes(World& world, std::size_t max_events, double max_ms) {
    RendererFrameStats ignored_stats {};
    sync_gpu_meshes(world, ignored_stats, max_events, max_ms);
}

void Renderer::begin_world_resource_reset() {
    if (world_resource_reset_progress_.active()) {
        return;
    }

    // Je rends immediatement l'ancien monde invisible, puis je libere ses objets GPU par tranches.
    const auto overlay_has_resources = block_break_overlay_mesh_.vao != 0U ||
                                       block_break_overlay_mesh_.vbo != 0U ||
                                       block_break_overlay_mesh_.ebo != 0U ||
                                       block_break_overlay_mesh_.revision != 0U ||
                                       block_break_overlay_mesh_.opaque_index_count > 0 ||
                                       block_break_overlay_mesh_.water_index_count > 0;
    const auto required_queue_capacity = gpu_meshes_.size() + (overlay_has_resources ? 1U : 0U);
    world_resource_reset_queue_.clear();
    if (world_resource_reset_queue_.capacity() < required_queue_capacity) {
        world_resource_reset_queue_.reserve(required_queue_capacity);
    }
    for (auto& [coord, mesh] : gpu_meshes_) {
        static_cast<void>(coord);
        world_resource_reset_queue_.push_back(mesh);
        mesh = {};
    }
    gpu_meshes_.clear();
    visible_chunks_cache_.clear();
    shadow_chunks_cache_.clear();
    chunk_upload_scratch_.vertices.clear();
    chunk_upload_scratch_.indices.clear();
    chunk_upload_scratch_.water_vertices.clear();
    chunk_upload_scratch_.water_indices.clear();
    chunk_upload_scratch_.face_count = 0U;
    chunk_upload_scratch_.water_face_count = 0U;
    block_break_overlay_scratch_.vertices.clear();
    block_break_overlay_scratch_.indices.clear();
    block_break_overlay_scratch_.water_vertices.clear();
    block_break_overlay_scratch_.water_indices.clear();
    block_break_overlay_scratch_.face_count = 0U;
    block_break_overlay_scratch_.water_face_count = 0U;
    last_frame_stats_.uploaded_meshes = 0U;
    last_frame_stats_.visible_chunks = 0U;
    last_frame_stats_.shadow_chunks = 0U;
    last_frame_stats_.world_chunks = 0U;

    if (overlay_has_resources) {
        world_resource_reset_queue_.push_back(block_break_overlay_mesh_);
    }
    block_break_overlay_mesh_ = {};
    world_resource_reset_progress_.begin(world_resource_reset_queue_.size(), false);
}

auto Renderer::process_world_resource_reset(std::size_t max_events, double max_ms) -> bool {
    using clock = std::chrono::steady_clock;

    if (!world_resource_reset_progress_.active()) {
        return true;
    }
    if (max_events == 0U) {
        return false;
    }

    const auto time_limited = std::isfinite(max_ms);
    const auto deadline = time_limited
                              ? clock::now() + std::chrono::duration<double, std::milli>(std::max(0.0, max_ms))
                              : clock::time_point::max();
    std::size_t processed_events = 0U;
    while (processed_events < max_events && clock::now() < deadline) {
        if (!world_resource_reset_queue_.empty()) {
            auto& mesh = world_resource_reset_queue_.back();
            if (gl_api_ready_) {
                destroy_gpu_mesh(mesh);
            }
            world_resource_reset_queue_.pop_back();
            world_resource_reset_progress_.consume_one();
            ++processed_events;
            continue;
        }

        break;
    }

    if (world_resource_reset_queue_.empty()) {
        world_resource_reset_progress_.finish();
    }
    return world_resource_reset_progress_.complete();
}

auto Renderer::pending_world_resource_reset_count() const noexcept -> std::size_t {
    return world_resource_reset_progress_.remaining();
}

void Renderer::reset_world_resources() {
    begin_world_resource_reset();
    while (!process_world_resource_reset(std::numeric_limits<std::size_t>::max(),
                                         std::numeric_limits<double>::infinity())) {
        // Je draine sans limite uniquement pendant l'arret complet du renderer.
    }
}

auto Renderer::world_mesh_uploaded(const ChunkCoord& coord, std::uint64_t revision) const noexcept -> bool {
    if (revision == 0U || world_resource_reset_progress_.active()) {
        return false;
    }
    const auto iterator = gpu_meshes_.find(coord);
    return iterator != gpu_meshes_.end() && iterator->second.revision == revision;
}

void Renderer::sync_gpu_meshes(World& world, RendererFrameStats& frame_stats, std::size_t max_events, double max_ms) {
    using clock = std::chrono::steady_clock;

    if (max_events == 0 || world_resource_reset_progress_.active()) {
        return;
    }

    const auto time_limited = std::isfinite(max_ms);
    const auto deadline =
        time_limited ? clock::now() + std::chrono::duration<double, std::milli>(std::max(0.0, max_ms)) : clock::time_point::max();
    std::size_t processed_events = 0;
    while (processed_events < max_events && clock::now() < deadline) {
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

        const auto coord = uploads.front();
        const auto revision = world.mesh_revision(coord);
        const auto* section_meshes = world.section_meshes_for(coord);
        const auto* organic_section_meshes =
            options_.visual_pipeline == VisualPipeline::ModernStylized
                ? world.organic_section_meshes_for(coord)
                : nullptr;
        const auto* architectural_section_meshes =
            options_.visual_pipeline == VisualPipeline::ModernStylized
                ? world.architectural_section_meshes_for(coord)
                : nullptr;
        if (revision == 0 || section_meshes == nullptr) {
            ++processed_events;
            continue;
        }

        const auto existing_gpu_mesh = gpu_meshes_.find(coord);
        if (existing_gpu_mesh != gpu_meshes_.end() &&
            existing_gpu_mesh->second.revision == revision) {
            // Je conserve les meshes lors d'une reconfiguration et j'ignore les ré-enqueues identiques.
            ++processed_events;
            continue;
        }

        merge_chunk_mesh_sections_into(*section_meshes, chunk_upload_scratch_);
        const OrganicTerrainMesh* organic_mesh = nullptr;
        if (organic_section_meshes != nullptr) {
            merge_organic_terrain_sections_into(
                *organic_section_meshes,
                terrain_upload_scratch_);
            organic_mesh = &terrain_upload_scratch_;
        }
        const ArchitecturalMesh* architectural_mesh = nullptr;
        if (architectural_section_meshes != nullptr) {
            merge_architectural_sections_into(
                *architectural_section_meshes,
                architecture_upload_scratch_);
            architectural_mesh = &architecture_upload_scratch_;
        }
        upload_mesh(
            coord,
            chunk_upload_scratch_,
            organic_mesh,
            architectural_mesh,
            revision);
        ++frame_stats.uploaded_meshes;
        ++processed_events;
    }
}

void Renderer::upload_mesh(
    const ChunkCoord& coord,
    const ChunkMeshData& mesh,
    const OrganicTerrainMesh* terrain_mesh,
    const ArchitecturalMesh* architectural_mesh,
    std::uint64_t revision) {
    auto& gpu_mesh = gpu_meshes_[coord];
    ExactAabbAccumulator exact_bounds {};
    const auto include_vertices =
        [&exact_bounds](const auto& vertices) {
            for (const auto& vertex : vertices) {
                exact_bounds.add(
                    vertex.x,
                    vertex.y,
                    vertex.z);
            }
        };
    include_vertices(mesh.vertices);
    include_vertices(mesh.water_vertices);
    if (terrain_mesh != nullptr) {
        include_vertices(terrain_mesh->vertices);
    }
    if (architectural_mesh != nullptr) {
        include_vertices(architectural_mesh->vertices);
    }

    // Je remplace la boite haute de 128 blocs par les limites reelles du
    // maillage. Le frustum et chaque cascade rejettent ainsi les chunks vides
    // en altitude sans toucher a la geometrie envoyee au GPU.
    upload_mesh_data(
        gpu_mesh,
        mesh,
        revision,
        exact_bounds.bounds_or(
            make_chunk_bounds(coord)));
    if (terrain_mesh != nullptr) {
        upload_terrain_mesh_data(gpu_mesh, *terrain_mesh);
    } else {
        gpu_mesh.terrain_index_count = 0;
    }
    if (architectural_mesh != nullptr) {
        upload_architectural_mesh_data(gpu_mesh, *architectural_mesh);
    } else {
        gpu_mesh.architecture_opaque_index_count = 0;
        gpu_mesh.architecture_transparent_index_count = 0;
        gpu_mesh.architecture_transparent_index_offset_bytes = 0;
    }
}

void Renderer::upload_mesh_data(
    GpuMesh& gpu_mesh,
    const ChunkMeshData& mesh,
    std::uint64_t revision,
    const ChunkBounds& bounds) {
    gpu_mesh.revision = revision;
    gpu_mesh.bounds = bounds;
    gpu_mesh.opaque_index_count = 0;
    gpu_mesh.water_index_count = 0;

    if (mesh.total_index_count() == 0 || mesh.total_vertex_count() == 0) {
        return;
    }

    const auto opaque_vertex_bytes = static_cast<GLsizeiptr>(mesh.vertices.size() * sizeof(ChunkVertex));
    const auto opaque_index_bytes = static_cast<GLsizeiptr>(mesh.indices.size() * sizeof(std::uint32_t));
    if (opaque_vertex_bytes > 0 && opaque_index_bytes > 0) {
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
            glEnableVertexAttribArray(8);
            glVertexAttribPointer(8, 1, GL_FLOAT, GL_FALSE, sizeof(ChunkVertex), reinterpret_cast<void*>(offsetof(ChunkVertex, wave_weight)));
        } else {
            glBindVertexArray(gpu_mesh.vao);
            glBindBuffer(GL_ARRAY_BUFFER, gpu_mesh.vbo);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gpu_mesh.ebo);
        }

        if (gpu_mesh.vertex_buffer_bytes < opaque_vertex_bytes) {
            gpu_mesh.vertex_buffer_bytes = grow_buffer_capacity(
                gpu_mesh.vertex_buffer_bytes,
                opaque_vertex_bytes,
                kInitialVertexBufferBytes);
        }
        if (gpu_mesh.index_buffer_bytes < opaque_index_bytes) {
            gpu_mesh.index_buffer_bytes = grow_buffer_capacity(
                gpu_mesh.index_buffer_bytes,
                opaque_index_bytes,
                kInitialIndexBufferBytes);
        }

        orphan_bound_buffer(GL_ARRAY_BUFFER, gpu_mesh.vertex_buffer_bytes, GL_DYNAMIC_DRAW);
        orphan_bound_buffer(GL_ELEMENT_ARRAY_BUFFER, gpu_mesh.index_buffer_bytes, GL_DYNAMIC_DRAW);
        glBufferSubData(GL_ARRAY_BUFFER, 0, opaque_vertex_bytes, mesh.vertices.data());
        glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, opaque_index_bytes, mesh.indices.data());
        gpu_mesh.opaque_index_count = static_cast<GLsizei>(mesh.indices.size());
        frame_uploaded_bytes_ += static_cast<std::uint64_t>(opaque_vertex_bytes);
        frame_uploaded_bytes_ += static_cast<std::uint64_t>(opaque_index_bytes);
    }

    const auto water_vertex_bytes =
        static_cast<GLsizeiptr>(mesh.water_vertices.size() * sizeof(WaterVertex));
    const auto water_index_bytes = static_cast<GLsizeiptr>(mesh.water_indices.size() * sizeof(std::uint32_t));
    if (water_vertex_bytes > 0 && water_index_bytes > 0) {
        if (gpu_mesh.water_vao == 0) {
            glGenVertexArrays(1, &gpu_mesh.water_vao);
            glGenBuffers(1, &gpu_mesh.water_vbo);
            glGenBuffers(1, &gpu_mesh.water_ebo);

            glBindVertexArray(gpu_mesh.water_vao);
            glBindBuffer(GL_ARRAY_BUFFER, gpu_mesh.water_vbo);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gpu_mesh.water_ebo);

            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(WaterVertex), reinterpret_cast<void*>(offsetof(WaterVertex, x)));
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(WaterVertex), reinterpret_cast<void*>(offsetof(WaterVertex, u)));
            glEnableVertexAttribArray(2);
            glVertexAttribPointer(2, 3, GL_BYTE, GL_TRUE, sizeof(WaterVertex), reinterpret_cast<void*>(offsetof(WaterVertex, nx)));
            glEnableVertexAttribArray(3);
            glVertexAttribPointer(3, 1, GL_HALF_FLOAT, GL_FALSE, sizeof(WaterVertex), reinterpret_cast<void*>(offsetof(WaterVertex, face_shade_half)));
            glEnableVertexAttribArray(4);
            glVertexAttribPointer(4, 1, GL_UNSIGNED_BYTE, GL_FALSE, sizeof(WaterVertex), reinterpret_cast<void*>(offsetof(WaterVertex, ao)));
            glEnableVertexAttribArray(5);
            glVertexAttribPointer(5, 1, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(WaterVertex), reinterpret_cast<void*>(offsetof(WaterVertex, sky_light)));
            glEnableVertexAttribArray(6);
            glVertexAttribPointer(6, 1, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(WaterVertex), reinterpret_cast<void*>(offsetof(WaterVertex, block_light)));
            glEnableVertexAttribArray(7);
            glVertexAttribPointer(7, 1, GL_UNSIGNED_BYTE, GL_FALSE, sizeof(WaterVertex), reinterpret_cast<void*>(offsetof(WaterVertex, material_class)));
            glEnableVertexAttribArray(8);
            glVertexAttribPointer(8, 1, GL_UNSIGNED_BYTE, GL_FALSE, sizeof(WaterVertex), reinterpret_cast<void*>(offsetof(WaterVertex, wave_weight)));
        } else {
            glBindVertexArray(gpu_mesh.water_vao);
            glBindBuffer(GL_ARRAY_BUFFER, gpu_mesh.water_vbo);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gpu_mesh.water_ebo);
        }

        if (gpu_mesh.water_vertex_buffer_bytes < water_vertex_bytes) {
            gpu_mesh.water_vertex_buffer_bytes = grow_buffer_capacity(
                gpu_mesh.water_vertex_buffer_bytes,
                water_vertex_bytes,
                kInitialWaterVertexBufferBytes);
        }
        if (gpu_mesh.water_index_buffer_bytes < water_index_bytes) {
            gpu_mesh.water_index_buffer_bytes = grow_buffer_capacity(
                gpu_mesh.water_index_buffer_bytes,
                water_index_bytes,
                kInitialIndexBufferBytes);
        }

        orphan_bound_buffer(
            GL_ARRAY_BUFFER,
            gpu_mesh.water_vertex_buffer_bytes,
            GL_DYNAMIC_DRAW);
        orphan_bound_buffer(
            GL_ELEMENT_ARRAY_BUFFER,
            gpu_mesh.water_index_buffer_bytes,
            GL_DYNAMIC_DRAW);
        glBufferSubData(
            GL_ARRAY_BUFFER,
            0,
            water_vertex_bytes,
            mesh.water_vertices.data());
        glBufferSubData(
            GL_ELEMENT_ARRAY_BUFFER,
            0,
            water_index_bytes,
            mesh.water_indices.data());
        gpu_mesh.water_index_count = static_cast<GLsizei>(mesh.water_indices.size());
        frame_uploaded_bytes_ += static_cast<std::uint64_t>(water_vertex_bytes);
        frame_uploaded_bytes_ += static_cast<std::uint64_t>(water_index_bytes);
    }
}

void Renderer::upload_terrain_mesh_data(
    GpuMesh& gpu_mesh,
    const OrganicTerrainMesh& mesh) {
    gpu_mesh.terrain_index_count = 0;
    if (mesh.vertices.empty() || mesh.indices.empty()) {
        return;
    }

    if (gpu_mesh.terrain_vao == 0) {
        glGenVertexArrays(1, &gpu_mesh.terrain_vao);
        glGenBuffers(1, &gpu_mesh.terrain_vbo);
        glGenBuffers(1, &gpu_mesh.terrain_ebo);

        glBindVertexArray(gpu_mesh.terrain_vao);
        glBindBuffer(GL_ARRAY_BUFFER, gpu_mesh.terrain_vbo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gpu_mesh.terrain_ebo);

        glEnableVertexAttribArray(0);
        glVertexAttribPointer(
            0,
            3,
            GL_FLOAT,
            GL_FALSE,
            sizeof(TerrainVertex),
            reinterpret_cast<void*>(offsetof(TerrainVertex, x)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(
            1,
            3,
            GL_FLOAT,
            GL_FALSE,
            sizeof(TerrainVertex),
            reinterpret_cast<void*>(offsetof(TerrainVertex, nx)));
        glEnableVertexAttribArray(2);
        glVertexAttribIPointer(
            2,
            4,
            GL_UNSIGNED_BYTE,
            sizeof(TerrainVertex),
            reinterpret_cast<void*>(offsetof(TerrainVertex, primary_block_id)));
        glEnableVertexAttribArray(3);
        glVertexAttribIPointer(
            3,
            2,
            GL_UNSIGNED_BYTE,
            sizeof(TerrainVertex),
            reinterpret_cast<void*>(offsetof(TerrainVertex, sky_light)));
        glEnableVertexAttribArray(4);
        glVertexAttribIPointer(
            4,
            1,
            GL_UNSIGNED_SHORT,
            sizeof(TerrainVertex),
            reinterpret_cast<void*>(offsetof(TerrainVertex, surface_flags)));
    } else {
        glBindVertexArray(gpu_mesh.terrain_vao);
        glBindBuffer(GL_ARRAY_BUFFER, gpu_mesh.terrain_vbo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gpu_mesh.terrain_ebo);
    }

    const auto vertex_bytes =
        static_cast<GLsizeiptr>(mesh.vertices.size() * sizeof(TerrainVertex));
    const auto index_bytes =
        static_cast<GLsizeiptr>(mesh.indices.size() * sizeof(std::uint32_t));
    if (gpu_mesh.terrain_vertex_buffer_bytes < vertex_bytes) {
        gpu_mesh.terrain_vertex_buffer_bytes = grow_buffer_capacity(
            gpu_mesh.terrain_vertex_buffer_bytes,
            vertex_bytes,
            kInitialTerrainVertexBufferBytes);
    }
    if (gpu_mesh.terrain_index_buffer_bytes < index_bytes) {
        gpu_mesh.terrain_index_buffer_bytes = grow_buffer_capacity(
            gpu_mesh.terrain_index_buffer_bytes,
            index_bytes,
            kInitialTerrainIndexBufferBytes);
    }

    orphan_bound_buffer(
        GL_ARRAY_BUFFER,
        gpu_mesh.terrain_vertex_buffer_bytes,
        GL_DYNAMIC_DRAW);
    orphan_bound_buffer(
        GL_ELEMENT_ARRAY_BUFFER,
        gpu_mesh.terrain_index_buffer_bytes,
        GL_DYNAMIC_DRAW);
    glBufferSubData(GL_ARRAY_BUFFER, 0, vertex_bytes, mesh.vertices.data());
    glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, index_bytes, mesh.indices.data());

    gpu_mesh.terrain_index_count = static_cast<GLsizei>(mesh.indices.size());
    frame_uploaded_bytes_ +=
        static_cast<std::uint64_t>(std::max<GLsizeiptr>(vertex_bytes, 0));
    frame_uploaded_bytes_ +=
        static_cast<std::uint64_t>(std::max<GLsizeiptr>(index_bytes, 0));
}

void Renderer::upload_architectural_mesh_data(
    GpuMesh& gpu_mesh,
    const ArchitecturalMesh& mesh) {

    gpu_mesh.architecture_opaque_index_count = 0;
    gpu_mesh.architecture_transparent_index_count = 0;
    gpu_mesh.architecture_transparent_index_offset_bytes = 0;
    if (mesh.vertices.empty() || mesh.indices.empty()) {
        return;
    }

    if (gpu_mesh.architecture_vao == 0U) {
        glGenVertexArrays(1, &gpu_mesh.architecture_vao);
        glGenBuffers(1, &gpu_mesh.architecture_vbo);
        glGenBuffers(1, &gpu_mesh.architecture_ebo);

        glBindVertexArray(gpu_mesh.architecture_vao);
        glBindBuffer(GL_ARRAY_BUFFER, gpu_mesh.architecture_vbo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gpu_mesh.architecture_ebo);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(
            0,
            3,
            GL_FLOAT,
            GL_FALSE,
            sizeof(HardSurfaceVertex),
            reinterpret_cast<void*>(offsetof(HardSurfaceVertex, x)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(
            1,
            3,
            GL_FLOAT,
            GL_FALSE,
            sizeof(HardSurfaceVertex),
            reinterpret_cast<void*>(offsetof(HardSurfaceVertex, nx)));
        glEnableVertexAttribArray(2);
        glVertexAttribIPointer(
            2,
            2,
            GL_UNSIGNED_SHORT,
            sizeof(HardSurfaceVertex),
            reinterpret_cast<void*>(offsetof(HardSurfaceVertex, u_fixed)));
        glEnableVertexAttribArray(3);
        glVertexAttribIPointer(
            3,
            4,
            GL_UNSIGNED_BYTE,
            sizeof(HardSurfaceVertex),
            reinterpret_cast<void*>(offsetof(HardSurfaceVertex, material_block)));
    } else {
        glBindVertexArray(gpu_mesh.architecture_vao);
        glBindBuffer(GL_ARRAY_BUFFER, gpu_mesh.architecture_vbo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gpu_mesh.architecture_ebo);
    }

    auto& ordered_indices = architecture_indices_scratch_;
    ordered_indices.clear();
    ordered_indices.reserve(mesh.indices.size());
    auto covered_index_count = std::size_t {0U};
    for (const auto& quad : mesh.quads) {
        const auto first = static_cast<std::size_t>(quad.first_index);
        constexpr std::size_t kQuadIndexCount = 6U;
        if (first > mesh.indices.size() ||
            mesh.indices.size() - first < kQuadIndexCount) {
            continue;
        }
        covered_index_count = std::max(
            covered_index_count,
            first + kQuadIndexCount);
        if ((quad.surface_flags & ArchitecturalTransparent) != 0U) {
            continue;
        }
        ordered_indices.insert(
            ordered_indices.end(),
            mesh.indices.begin() + static_cast<std::ptrdiff_t>(first),
            mesh.indices.begin() +
                static_cast<std::ptrdiff_t>(first + kQuadIndexCount));
    }

    // Les primitives de fixtures sont ajoutees apres les quads greedy et sont
    // toujours opaques/cutout. Je les conserve dans la premiere plage.
    ordered_indices.insert(
        ordered_indices.end(),
        mesh.indices.begin() + static_cast<std::ptrdiff_t>(
                                   std::min(covered_index_count, mesh.indices.size())),
        mesh.indices.end());
    const auto opaque_index_count = ordered_indices.size();

    for (const auto& quad : mesh.quads) {
        if ((quad.surface_flags & ArchitecturalTransparent) == 0U) {
            continue;
        }
        const auto first = static_cast<std::size_t>(quad.first_index);
        constexpr std::size_t kQuadIndexCount = 6U;
        if (first > mesh.indices.size() ||
            mesh.indices.size() - first < kQuadIndexCount) {
            continue;
        }
        ordered_indices.insert(
            ordered_indices.end(),
            mesh.indices.begin() + static_cast<std::ptrdiff_t>(first),
            mesh.indices.begin() +
                static_cast<std::ptrdiff_t>(first + kQuadIndexCount));
    }

    const auto vertex_bytes = static_cast<GLsizeiptr>(
        mesh.vertices.size() * sizeof(HardSurfaceVertex));
    const auto index_bytes = static_cast<GLsizeiptr>(
        ordered_indices.size() * sizeof(std::uint32_t));
    if (gpu_mesh.architecture_vertex_buffer_bytes < vertex_bytes) {
        gpu_mesh.architecture_vertex_buffer_bytes = grow_buffer_capacity(
            gpu_mesh.architecture_vertex_buffer_bytes,
            vertex_bytes,
            kInitialTerrainVertexBufferBytes);
    }
    if (gpu_mesh.architecture_index_buffer_bytes < index_bytes) {
        gpu_mesh.architecture_index_buffer_bytes = grow_buffer_capacity(
            gpu_mesh.architecture_index_buffer_bytes,
            index_bytes,
            kInitialTerrainIndexBufferBytes);
    }

    orphan_bound_buffer(
        GL_ARRAY_BUFFER,
        gpu_mesh.architecture_vertex_buffer_bytes,
        GL_DYNAMIC_DRAW);
    orphan_bound_buffer(
        GL_ELEMENT_ARRAY_BUFFER,
        gpu_mesh.architecture_index_buffer_bytes,
        GL_DYNAMIC_DRAW);
    glBufferSubData(GL_ARRAY_BUFFER, 0, vertex_bytes, mesh.vertices.data());
    glBufferSubData(
        GL_ELEMENT_ARRAY_BUFFER,
        0,
        index_bytes,
        ordered_indices.data());

    gpu_mesh.architecture_opaque_index_count =
        static_cast<GLsizei>(opaque_index_count);
    gpu_mesh.architecture_transparent_index_count =
        static_cast<GLsizei>(ordered_indices.size() - opaque_index_count);
    gpu_mesh.architecture_transparent_index_offset_bytes =
        static_cast<GLsizeiptr>(
            opaque_index_count * sizeof(std::uint32_t));
    frame_uploaded_bytes_ +=
        static_cast<std::uint64_t>(std::max<GLsizeiptr>(vertex_bytes, 0));
    frame_uploaded_bytes_ +=
        static_cast<std::uint64_t>(std::max<GLsizeiptr>(index_bytes, 0));
}

namespace {

[[nodiscard]] constexpr auto ship_visual_variant(
    VisualPipeline pipeline,
    StylizedShipLod lod) noexcept -> std::uint8_t {

    if (pipeline == VisualPipeline::LegacyVoxel) {
        return 0U;
    }
    return lod == StylizedShipLod::Near ? 1U : 2U;
}

} // namespace

void Renderer::ensure_ship_mesh(
    const ShipRenderState& ship,
    StylizedShipLod lod) {

    if (!ship.visible || ship.parts.empty() || ship.geometry_revision == 0U) {
        return;
    }
    if (ship_mesh_ready(ship, lod)) {
        return;
    }

    if (options_.visual_pipeline == VisualPipeline::ModernStylized) {
        const auto stylized_mesh = build_stylized_ship_mesh(ship, lod);
        if (stylized_mesh.empty()) {
            return;
        }
        const ChunkBounds local_bounds {
            stylized_mesh.metrics.bounds.min,
            stylized_mesh.metrics.bounds.max,
            (stylized_mesh.metrics.bounds.min +
             stylized_mesh.metrics.bounds.max) *
                0.5F,
        };
        upload_mesh_data(
            ship_gpu_mesh_,
            stylized_mesh.mesh,
            ship.geometry_revision,
            local_bounds);
        ship_mesh_cache_.remember(
            ship.geometry_revision,
            ship.parts.size(),
            ship_visual_variant(options_.visual_pipeline, lod));
        return;
    }

    const auto legacy_mesh = build_ship_mesh_data(ship.parts);
    (void)upload_prepared_ship_mesh(ship, legacy_mesh);
}

auto Renderer::upload_prepared_ship_mesh(const ShipRenderState& ship, const ChunkMeshData& mesh) -> bool {
    if (!initialized_ || !ship.visible || ship.parts.empty() || ship.geometry_revision == 0U ||
        mesh.vertices.empty() || mesh.indices.empty()) {
        return false;
    }
    if (ship_mesh_ready(ship, StylizedShipLod::Near)) {
        return true;
    }

    // Je garde exclusivement l'upload OpenGL sur le thread du renderer. La
    // construction CPU peut ainsi etre preparee ailleurs sans partager d'etat GL.
    const ChunkBounds local_bounds {
        ship.local_bounds.min,
        ship.local_bounds.max,
        (ship.local_bounds.min + ship.local_bounds.max) * 0.5F,
    };
    upload_mesh_data(ship_gpu_mesh_, mesh, ship.geometry_revision, local_bounds);
    active_ship_lod_ = StylizedShipLod::Near;
    ship_mesh_cache_.remember(
        ship.geometry_revision,
        ship.parts.size(),
        ship_visual_variant(options_.visual_pipeline, active_ship_lod_));
    return ship_mesh_ready(ship, active_ship_lod_);
}

auto Renderer::prepare_ship_mesh(const ShipRenderState& ship) -> bool {
    if (!initialized_ || !ship.visible || ship.parts.empty() || ship.geometry_revision == 0U) {
        return false;
    }
    ensure_ship_mesh(ship, StylizedShipLod::Near);
    return ship_mesh_ready(ship);
}

auto Renderer::ship_mesh_ready(const ShipRenderState& ship) const noexcept -> bool {
    return ship_mesh_ready(ship, StylizedShipLod::Near);
}

auto Renderer::ship_mesh_ready(
    const ShipRenderState& ship,
    StylizedShipLod lod) const noexcept -> bool {

    const auto gpu_ready = ship_gpu_mesh_.vao != 0U &&
                           ship_gpu_mesh_.vbo != 0U &&
                           ship_gpu_mesh_.ebo != 0U &&
                           ship_gpu_mesh_.opaque_index_count > 0 &&
                           ship_gpu_mesh_.revision == ship.geometry_revision;
    return ship_mesh_cache_.ready(
        ship.geometry_revision,
        ship.parts.size(),
        initialized_,
        gpu_ready,
        ship_visual_variant(options_.visual_pipeline, lod));
}

void Renderer::upload_block_break_overlay_mesh(
    const World& world,
    const BlockBreakProgress& break_progress) {
    auto& mesh = block_break_overlay_scratch_;
    if (options_.visual_pipeline == VisualPipeline::ModernStylized &&
        is_organic_terrain_block(break_progress.block_id)) {
        build_organic_block_break_overlay_mesh_data_into(
            world,
            break_progress,
            mesh);
    } else {
        build_block_break_overlay_mesh_data_into(break_progress, mesh);
    }
    auto& gpu_mesh = block_break_overlay_mesh_;
    gpu_mesh.opaque_index_count = 0;
    gpu_mesh.water_index_count = 0;

    if (mesh.indices.empty() || mesh.vertices.empty()) {
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
        glEnableVertexAttribArray(7);
        glVertexAttribPointer(7, 1, GL_FLOAT, GL_FALSE, sizeof(ChunkVertex), reinterpret_cast<void*>(offsetof(ChunkVertex, material_class)));
        glEnableVertexAttribArray(8);
        glVertexAttribPointer(8, 1, GL_FLOAT, GL_FALSE, sizeof(ChunkVertex), reinterpret_cast<void*>(offsetof(ChunkVertex, wave_weight)));
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
    }
    if (gpu_mesh.index_buffer_bytes < index_bytes) {
        gpu_mesh.index_buffer_bytes = grow_buffer_capacity(
            gpu_mesh.index_buffer_bytes,
            index_bytes,
            kInitialIndexBufferBytes);
    }

    orphan_bound_buffer(GL_ARRAY_BUFFER, gpu_mesh.vertex_buffer_bytes);
    orphan_bound_buffer(GL_ELEMENT_ARRAY_BUFFER, gpu_mesh.index_buffer_bytes);
    glBufferSubData(GL_ARRAY_BUFFER, 0, vertex_bytes, mesh.vertices.data());
    glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, index_bytes, mesh.indices.data());
    frame_uploaded_bytes_ += static_cast<std::uint64_t>(std::max<GLsizeiptr>(vertex_bytes, 0));
    frame_uploaded_bytes_ += static_cast<std::uint64_t>(std::max<GLsizeiptr>(index_bytes, 0));
    gpu_mesh.opaque_index_count = static_cast<GLsizei>(mesh.indices.size());
}

void Renderer::destroy_gpu_mesh(GpuMesh& mesh) {
    if (mesh.water_ebo != 0U) {
        glDeleteBuffers(1, &mesh.water_ebo);
        mesh.water_ebo = 0U;
    }
    if (mesh.water_vbo != 0U) {
        glDeleteBuffers(1, &mesh.water_vbo);
        mesh.water_vbo = 0U;
    }
    if (mesh.water_vao != 0U) {
        glDeleteVertexArrays(1, &mesh.water_vao);
        mesh.water_vao = 0U;
    }
    if (mesh.architecture_ebo != 0U) {
        glDeleteBuffers(1, &mesh.architecture_ebo);
        mesh.architecture_ebo = 0U;
    }
    if (mesh.architecture_vbo != 0U) {
        glDeleteBuffers(1, &mesh.architecture_vbo);
        mesh.architecture_vbo = 0U;
    }
    if (mesh.architecture_vao != 0U) {
        glDeleteVertexArrays(1, &mesh.architecture_vao);
        mesh.architecture_vao = 0U;
    }
    if (mesh.terrain_ebo != 0) {
        glDeleteBuffers(1, &mesh.terrain_ebo);
        mesh.terrain_ebo = 0;
    }
    if (mesh.terrain_vbo != 0) {
        glDeleteBuffers(1, &mesh.terrain_vbo);
        mesh.terrain_vbo = 0;
    }
    if (mesh.terrain_vao != 0) {
        glDeleteVertexArrays(1, &mesh.terrain_vao);
        mesh.terrain_vao = 0;
    }
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
    mesh.revision = 0;
    mesh.vertex_buffer_bytes = 0;
    mesh.index_buffer_bytes = 0;
    mesh.water_vertex_buffer_bytes = 0;
    mesh.water_index_buffer_bytes = 0;
    mesh.terrain_index_count = 0;
    mesh.terrain_vertex_buffer_bytes = 0;
    mesh.terrain_index_buffer_bytes = 0;
    mesh.architecture_opaque_index_count = 0;
    mesh.architecture_transparent_index_count = 0;
    mesh.architecture_transparent_index_offset_bytes = 0;
    mesh.architecture_vertex_buffer_bytes = 0;
    mesh.architecture_index_buffer_bytes = 0;
}

void Renderer::upload_world_ship_protection(const ShipRenderState& ship) {
    const auto enabled = ship_protection_is_renderable(ship);
    glUniform1i(
        world_uniforms_.ship_protection_enabled,
        enabled ? 1 : 0);
    if (!enabled) {
        return;
    }

    const auto inverse_model = glm::inverse(ship.model_matrix);
    const auto& profile = ship.blueprint->protection_profile;
    glUniformMatrix4fv(
        world_uniforms_.ship_inverse_model,
        1,
        GL_FALSE,
        glm::value_ptr(inverse_model));
    glUniform3fv(
        world_uniforms_.ship_bounds_min,
        1,
        glm::value_ptr(ship.world_bounds.min));
    glUniform3fv(
        world_uniforms_.ship_bounds_max,
        1,
        glm::value_ptr(ship.world_bounds.max));
    glUniform4f(
        world_uniforms_.ship_profile_longitudinal,
        profile.stern_z,
        profile.bow_z,
        profile.maximum_half_width,
        profile.boundary_margin);
    glUniform4f(
        world_uniforms_.ship_profile_taper,
        profile.stern_width_loss,
        profile.bow_width_loss,
        profile.stern_taper_exponent,
        profile.bow_taper_exponent);
    glUniform4f(
        world_uniforms_.ship_profile_heights,
        profile.lower_hull_min_y,
        profile.middle_hull_min_y,
        profile.upper_hull_min_y,
        profile.main_deck_top_y);
    glUniform4f(
        world_uniforms_.ship_profile_widths,
        profile.lower_width_inset,
        profile.middle_width_inset,
        profile.lower_minimum_half_width,
        profile.middle_minimum_half_width);
    glUniform1f(
        world_uniforms_.ship_sheltered_floor,
        profile.sheltered_floor_y);
}

void Renderer::upload_precipitation_ship_protection(
    const ShipRenderState& ship) {
    const auto enabled = ship_protection_is_renderable(ship);
    glUniform1i(
        precipitation_uniforms_.ship_protection_enabled,
        enabled ? 1 : 0);
    if (!enabled) {
        return;
    }

    const auto inverse_model = glm::inverse(ship.model_matrix);
    const auto& profile = ship.blueprint->protection_profile;
    glUniformMatrix4fv(
        precipitation_uniforms_.ship_inverse_model,
        1,
        GL_FALSE,
        glm::value_ptr(inverse_model));
    glUniform3fv(
        precipitation_uniforms_.ship_bounds_min,
        1,
        glm::value_ptr(ship.world_bounds.min));
    glUniform3fv(
        precipitation_uniforms_.ship_bounds_max,
        1,
        glm::value_ptr(ship.world_bounds.max));
    glUniform4f(
        precipitation_uniforms_.ship_profile_longitudinal,
        profile.stern_z,
        profile.bow_z,
        profile.maximum_half_width,
        profile.boundary_margin);
    glUniform4f(
        precipitation_uniforms_.ship_profile_taper,
        profile.stern_width_loss,
        profile.bow_width_loss,
        profile.stern_taper_exponent,
        profile.bow_taper_exponent);
    glUniform4f(
        precipitation_uniforms_.ship_profile_heights,
        profile.lower_hull_min_y,
        profile.middle_hull_min_y,
        profile.upper_hull_min_y,
        profile.main_deck_top_y);
    glUniform4f(
        precipitation_uniforms_.ship_profile_widths,
        profile.lower_width_inset,
        profile.middle_width_inset,
        profile.lower_minimum_half_width,
        profile.middle_minimum_half_width);
    glUniform1f(
        precipitation_uniforms_.ship_sheltered_floor,
        profile.sheltered_floor_y);
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
    static_assert(
        kOceanMaxWaveCount == 6U,
        "Mettre a jour la taille des tableaux d'ondes dans le shader GLSL.");

    static constexpr auto* world_vertex_shader = R"(#version 330 core
layout(location = 0) in vec3 a_position;
layout(location = 1) in vec2 a_uv;
layout(location = 2) in vec3 a_normal;
layout(location = 3) in float a_face_shade;
layout(location = 4) in float a_ao;
layout(location = 5) in float a_sky_light;
layout(location = 6) in float a_block_light;
layout(location = 7) in float a_material_class;
layout(location = 8) in float a_wave_weight;

uniform mat4 u_model;
uniform mat4 u_view_projection;
uniform mat4 u_light_view_projection;
uniform vec3 u_camera_position;
uniform float u_time_of_day;
uniform float u_wind_strength;
uniform vec4 u_ocean_waves[6];
uniform vec2 u_ocean_wave_phases[6];
uniform int u_ocean_wave_count;

out vec2 v_uv;
out vec3 v_normal;
out float v_face_shade;
out float v_ao;
out float v_sky_light;
out float v_block_light;
out float v_material_class;
out float v_wave_weight;
out float v_distance;
out vec3 v_world_position;
out vec4 v_light_position;
out vec3 v_ocean_normal;
out float v_ocean_crest;

float material_mask(float material, float expected) {
    return 1.0 - step(0.25, abs(material - expected));
}

void sample_ocean(
    vec2 world_xz,
    out float height,
    out vec2 gradient,
    out float crest
) {
    height = 0.0;
    gradient = vec2(0.0);
    crest = 0.0;

    // La boucle possède une limite compile-time compatible OpenGL 3.3.
    // u_ocean_wave_count sélectionne le niveau de qualité.
    for (int index = 0; index < 6; ++index) {
        if (index >= u_ocean_wave_count) {
            break;
        }

        vec4 geometry = u_ocean_waves[index];
        vec2 phase_data = u_ocean_wave_phases[index];

        vec2 direction = geometry.xy;
        float wave_number = geometry.z;
        float amplitude = geometry.w;

        float theta =
            dot(direction, world_xz) *
                wave_number +
            phase_data.x;

        float harmonic =
            0.14 *
            clamp(phase_data.y, 0.0, 1.0);

        float sine = sin(theta);
        float cosine = cos(theta);
        float double_sine = sin(theta * 2.0);
        float double_cosine = cos(theta * 2.0);

        height +=
            amplitude *
            (sine +
             harmonic * double_sine);

        float derivative =
            amplitude *
            wave_number *
            (cosine +
             2.0 * harmonic * double_cosine);

        gradient +=
            direction * derivative;

        // Je garde la crete dominante : la moyenne des six directions
        // supprimait les lignes de houle visibles lorsque la mer etait calme.
        float normalized_wave_height =
            clamp(
                0.5 +
                    0.5 *
                        (sine +
                         harmonic * double_sine) /
                        (1.0 + harmonic),
                0.0,
                1.0);

        crest = max(
            crest,
            normalized_wave_height *
                normalized_wave_height *
                normalized_wave_height);
    }
}

vec2 vegetation_wind_offset(vec3 world_position, float material_class, float time_phase) {
    float foliage_mask = material_mask(material_class, 4.0);
    float flora_mask = material_mask(material_class, 5.0);
    float wind_mask = max(foliage_mask * 0.35, flora_mask);
    if (wind_mask <= 0.0) {
        return vec2(0.0);
    }

    float gust_a = sin(world_position.x * 0.18 + world_position.z * 0.11 + time_phase * 1.35);
    float gust_b = cos(world_position.x * -0.13 + world_position.z * 0.21 + time_phase * 1.65);
    float flutter = sin((world_position.x + world_position.z) * 0.75 + world_position.y * 0.45 + time_phase * 2.40);
    float local_height = clamp(fract(world_position.y), 0.0, 1.0);
    local_height = mix(1.0, smoothstep(0.02, 0.98, local_height), flora_mask);
    float amplitude = u_wind_strength * wind_mask * mix(0.010, 0.032, flora_mask);
    return vec2(gust_a * 0.70 + flutter * 0.30, gust_b * 0.60 - gust_a * 0.22) * amplitude * local_height;
}

vec3 fabric_wind_offset(
    vec3 world_position,
    float material_class,
    float vertex_weight,
    float time_phase
) {
    float fabric_mask = material_mask(material_class, 9.0);
    float flexibility = clamp(vertex_weight, 0.0, 1.0) * fabric_mask;
    if (flexibility <= 0.0) {
        return vec3(0.0);
    }

    // Je deplace les deux faces d'une voile dans la meme direction monde :
    // elles restent jointives, tandis que les sommets ancres (poids nul)
    // demeurent exactement alignes sur les vergues et les mats.
    float wind = clamp(u_wind_strength, 0.0, 1.0);
    vec2 wind_direction = normalize(vec2(0.82, 0.57));
    vec2 transverse = vec2(-wind_direction.y, wind_direction.x);
    float phase =
        dot(world_position.xz, vec2(0.17, 0.11)) +
        world_position.y * 0.19 +
        time_phase * 1.24;
    float billow = sin(phase) + sin(phase * 2.13 + 0.7) * 0.28;
    float flutter = sin(phase * 3.71 - world_position.y * 0.31);
    float amplitude = flexibility * (0.014 + wind * 0.082);
    vec2 horizontal =
        wind_direction * billow * amplitude +
        transverse * flutter * amplitude * 0.24;
    return vec3(
        horizontal.x,
        flutter * amplitude * 0.055,
        horizontal.y);
}

void main() {
    vec4 world_position = u_model * vec4(a_position, 1.0);
    world_position.xz += vegetation_wind_offset(world_position.xyz, a_material_class, u_time_of_day * 8.0);
    world_position.xyz += fabric_wind_offset(
        world_position.xyz,
        a_material_class,
        a_wave_weight,
        u_time_of_day * 8.0);

    float water_mask =
        material_mask(a_material_class, 6.0);

    float wave_weight =
        clamp(a_wave_weight, 0.0, 1.0) *
        water_mask;

    vec2 ocean_gradient = vec2(0.0);
    float ocean_crest = 0.0;

    if (wave_weight > 0.0) {
        float ocean_height = 0.0;

        sample_ocean(
            world_position.xz,
            ocean_height,
            ocean_gradient,
            ocean_crest);

        // Déplacement vertical uniquement : les limites entre chunks restent
        // parfaitement raccordées et les côtes voxelisées sont conservées.
        world_position.y +=
            ocean_height * wave_weight;
    }

    v_normal =
        normalize(mat3(u_model) * a_normal);

    v_ocean_normal = normalize(
        vec3(
            -ocean_gradient.x,
            1.0,
            -ocean_gradient.y));

    v_ocean_crest = ocean_crest;

    gl_Position = u_view_projection * world_position;
    v_uv = a_uv;
    v_face_shade = a_face_shade;
    v_ao = a_ao;
    v_sky_light = a_sky_light;
    v_block_light = a_block_light;
    v_material_class = a_material_class;
    v_wave_weight = wave_weight;
    v_distance = distance(world_position.xyz, u_camera_position);
    v_world_position = world_position.xyz;
    v_light_position = u_light_view_projection * world_position;
}
)";

    static constexpr auto* item_drop_vertex_shader = R"(#version 330 core
layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_face_uv;
layout(location = 3) in float a_face_index;
layout(location = 4) in mat4 i_transform;
layout(location = 8) in uint i_block_id;
layout(location = 9) in float i_sky_light;
layout(location = 10) in float i_block_light;
layout(location = 11) in float i_material_class;
layout(location = 12) in vec4 i_face_tiles_0_1;
layout(location = 13) in vec4 i_face_tiles_2_3;
layout(location = 14) in vec4 i_face_tiles_4_5;

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
out float v_wave_weight;
out float v_distance;
out vec3 v_world_position;
out vec4 v_light_position;
out vec3 v_ocean_normal;
out float v_ocean_crest;

vec4 atlas_uv_rect(vec2 tile) {
    float uv_step = 1.0 / 8.0;
    float u0 = tile.x * uv_step;
    float v0 = tile.y * uv_step;
    return vec4(u0, v0, u0 + uv_step, v0 + uv_step);
}

vec4 block_uv_rect(uint face_index) {
    if (face_index == 0u) {
        return atlas_uv_rect(i_face_tiles_0_1.xy);
    }
    if (face_index == 1u) {
        return atlas_uv_rect(i_face_tiles_0_1.zw);
    }
    if (face_index == 2u) {
        return atlas_uv_rect(i_face_tiles_2_3.xy);
    }
    if (face_index == 3u) {
        return atlas_uv_rect(i_face_tiles_2_3.zw);
    }
    if (face_index == 4u) {
        return atlas_uv_rect(i_face_tiles_4_5.xy);
    }
    return atlas_uv_rect(i_face_tiles_4_5.zw);
}

float face_shade(float face_index) {
    if (face_index < 1.5) {
        return 0.85;
    }
    if (face_index < 2.5) {
        return 1.0;
    }
    if (face_index < 3.5) {
        return 0.65;
    }
    return 0.75;
}

void main() {
    vec3 world_position3 =
        (i_transform * vec4(a_position, 1.0)).xyz;
    mat3 normal_matrix =
        transpose(inverse(mat3(i_transform)));
    vec3 world_normal =
        normalize(normal_matrix * a_normal);
    vec4 uv_rect = block_uv_rect(uint(a_face_index + 0.5));
    vec2 uv = mix(uv_rect.xy, uv_rect.zw, a_face_uv);
    vec4 world_position = vec4(world_position3, 1.0);

    gl_Position = u_view_projection * world_position;
    v_uv = uv;
    v_normal = world_normal;
    v_face_shade = face_shade(a_face_index);
    v_ao = 1.0;
    v_sky_light = i_sky_light;
    v_block_light = i_block_light;
    v_material_class = i_material_class;
    v_wave_weight = 0.0;
    v_distance = distance(world_position3, u_camera_position);
    v_world_position = world_position3;
    v_light_position = u_light_view_projection * world_position;
    v_ocean_normal = world_normal;
    v_ocean_crest = 0.0;
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
in float v_wave_weight;
in float v_distance;
in vec3 v_world_position;
in vec4 v_light_position;
in vec3 v_ocean_normal;
in float v_ocean_crest;

uniform float u_ocean_foam_threshold;
uniform float u_ocean_detail_strength;
uniform float u_ocean_detail_phase;
uniform float u_ocean_severity;
uniform float u_ocean_tempest_factor;
uniform float u_ocean_open_sea;

uniform sampler2D u_atlas;
uniform sampler2D u_shadow_map;
uniform sampler2D u_shadow_map_far;
uniform sampler2D u_scene_color;
uniform sampler2D u_scene_depth;
uniform mat4 u_light_view_projection_far;
uniform vec3 u_camera_position;
uniform vec3 u_camera_forward;
uniform vec3 u_sun_direction;
uniform vec3 u_sun_color;
uniform vec3 u_ambient_color;
uniform vec3 u_fog_color;
uniform vec3 u_distant_fog_color;
uniform vec3 u_horizon_glow_color;
uniform vec3 u_night_tint_color;
uniform mat4 u_inverse_view_projection;
uniform float u_daylight_factor;
uniform float u_sun_visibility;
uniform float u_time_of_day;
uniform float u_cloud_intensity;
uniform float u_cloud_shadow_strength;
uniform float u_atmospheric_scatter_strength;
uniform float u_height_fog_density;
uniform float u_precipitation_intensity;
uniform float u_storm_intensity;
uniform float u_lightning_intensity;
uniform float u_super_vision_strength;
uniform int u_shadows_enabled;
uniform int u_shadow_cascade_count;
uniform float u_shadow_split_distance;
uniform float u_shadow_transition_width;

out vec4 frag_color;
)" VALCRAFT_SHIP_PROTECTION_GLSL_SOURCE R"(

float material_mask(float material, float expected) {
    return 1.0 - step(0.25, abs(material - expected));
}

float shadow_visibility_at(
    vec2 uv,
    float receiver_depth,
    float bias,
    bool far_cascade
) {
    float sampled_depth = far_cascade
        ? texture(u_shadow_map_far, uv).r
        : texture(u_shadow_map, uv).r;
    return (receiver_depth - bias) <= sampled_depth ? 1.0 : 0.0;
}

float sample_shadow_cascade(vec3 normal, bool far_cascade) {
    vec4 light_position = far_cascade
        ? u_light_view_projection_far * vec4(v_world_position, 1.0)
        : v_light_position;
    vec3 projected = light_position.xyz / max(light_position.w, 0.0001);
    projected = projected * 0.5 + 0.5;
    if (projected.z < 0.0 || projected.z > 1.0 || projected.x < 0.0 || projected.x > 1.0 || projected.y < 0.0 || projected.y > 1.0) {
        return 1.0;
    }

    vec2 texel_size = far_cascade
        ? 1.0 / vec2(textureSize(u_shadow_map_far, 0))
        : 1.0 / vec2(textureSize(u_shadow_map, 0));
    float ndotl = max(dot(normalize(normal), normalize(u_sun_direction)), 0.0);
    float bias =
        max(0.00065 * (1.0 - ndotl), 0.00012) *
        (far_cascade ? 1.35 : 1.0);
    // Je garde un PCF en croix pour lisser les ombres sans payer neuf lectures texture par fragment.
    float visibility = shadow_visibility_at(projected.xy, projected.z, bias, far_cascade) * 0.36;
    visibility += shadow_visibility_at(projected.xy + vec2(texel_size.x, 0.0), projected.z, bias, far_cascade) * 0.16;
    visibility += shadow_visibility_at(projected.xy - vec2(texel_size.x, 0.0), projected.z, bias, far_cascade) * 0.16;
    visibility += shadow_visibility_at(projected.xy + vec2(0.0, texel_size.y), projected.z, bias, far_cascade) * 0.16;
    visibility += shadow_visibility_at(projected.xy - vec2(0.0, texel_size.y), projected.z, bias, far_cascade) * 0.16;
    return visibility;
}

float sample_shadow(vec3 normal) {
    if (u_sun_visibility < 0.5 || u_shadows_enabled == 0) {
        return 1.0;
    }
    if (u_shadow_cascade_count <= 1) {
        return sample_shadow_cascade(normal, false);
    }

    float view_depth = max(
        dot(v_world_position - u_camera_position, u_camera_forward),
        0.0);
    float transition_width = max(u_shadow_transition_width, 0.0);
    if (transition_width <= 0.0001) {
        return sample_shadow_cascade(
            normal,
            view_depth > u_shadow_split_distance);
    }

    float half_width = transition_width * 0.5;
    if (view_depth <= u_shadow_split_distance - half_width) {
        return sample_shadow_cascade(normal, false);
    }
    if (view_depth >= u_shadow_split_distance + half_width) {
        return sample_shadow_cascade(normal, true);
    }

    float blend = smoothstep(
        u_shadow_split_distance - half_width,
        u_shadow_split_distance + half_width,
        view_depth);
    return mix(
        sample_shadow_cascade(normal, false),
        sample_shadow_cascade(normal, true),
        blend);
}

float hash12(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123);
}

float value_noise2(vec2 p) {
    vec2 cell = floor(p);
    vec2 local = fract(p);
    vec2 blend = local * local * (3.0 - 2.0 * local);

    float n00 = hash12(cell);
    float n10 = hash12(cell + vec2(1.0, 0.0));
    float n01 = hash12(cell + vec2(0.0, 1.0));
    float n11 = hash12(cell + vec2(1.0, 1.0));
    float nx0 = mix(n00, n10, blend.x);
    float nx1 = mix(n01, n11, blend.x);
    return mix(nx0, nx1, blend.y);
}

float sample_cloud_shadow(vec3 world_position, vec3 sun_direction) {
    float cloud_factor = clamp(u_cloud_intensity, 0.0, 1.0);
    float daylight = clamp(u_daylight_factor, 0.0, 1.0);
    if (cloud_factor <= 0.01 || u_cloud_shadow_strength <= 0.001 || daylight <= 0.20 || sun_direction.y <= 0.02) {
        return 1.0;
    }

    float projection_scale = (96.0 - world_position.y) / max(sun_direction.y, 0.12);
    vec2 projected = world_position.xz + sun_direction.xz * projection_scale;
    vec2 flow = projected * 0.0032 + vec2(u_time_of_day * 0.085, -u_time_of_day * 0.061);
    float base = value_noise2(flow);
    float detail = value_noise2(flow * 2.17 + vec2(9.3, 4.7));
    float cloud = smoothstep(0.52, 0.84, base * 0.68 + detail * 0.32);
    float coverage = smoothstep(0.10, 0.58, cloud_factor);
    return 1.0 - cloud * coverage * u_cloud_shadow_strength;
}

vec2 water_detail_gradient(
    vec2 world_xz,
    float time_phase
) {
    // Multiplicateurs entiers afin que le bouclage de phase à 2*pi soit
    // parfaitement continu après une longue session.
    float phase_d =
        world_xz.x * 1.08 -
        world_xz.y * 0.74 +
        time_phase * 2.0;

    float phase_e =
        world_xz.x * 0.72 +
        world_xz.y * 1.16 -
        time_phase * 3.0;

    float strength =
        max(u_ocean_detail_strength, 0.0);

    float d_height_dx =
        (cos(phase_d) * 1.08 +
         cos(phase_e) * 0.72 * 0.68) *
        strength;

    float d_height_dz =
        (-cos(phase_d) * 0.74 +
         cos(phase_e) * 1.16 * 0.68) *
        strength;

    return vec2(
        d_height_dx,
        d_height_dz);
}

vec2 rain_dimple_gradient(
    vec2 world_xz,
    float time_phase
) {
    float rain = clamp(u_precipitation_intensity, 0.0, 1.0);
    if (rain <= 0.001) {
        return vec2(0.0);
    }

    vec2 cell_position = world_xz * 1.85;
    vec2 cell = floor(cell_position);
    vec2 local = fract(cell_position) - vec2(0.5);
    float random_phase = hash12(cell + vec2(43.0, 17.0));
    float age = fract(time_phase * 0.42 + random_phase);
    float radius = length(local);
    float front = age * 0.58;
    float ring = exp(-pow((radius - front) * 22.0, 2.0));
    float pulse = cos((radius - front) * 46.0) * (1.0 - age);
    vec2 direction = local / max(radius, 0.035);
    return direction * ring * pulse * rain * 0.085;
}

vec3 reconstruct_world_position(vec2 screen_uv, float depth_sample) {
    vec4 clip_position = vec4(screen_uv * 2.0 - 1.0, depth_sample * 2.0 - 1.0, 1.0);
    vec4 world_position = u_inverse_view_projection * clip_position;
    return world_position.xyz / max(world_position.w, 0.0001);
}

float atlas_tile_edge_factor(vec2 uv) {
    vec2 local_uv = fract(uv * 8.0);
    float edge_distance = min(min(local_uv.x, 1.0 - local_uv.x), min(local_uv.y, 1.0 - local_uv.y));
    return 1.0 - smoothstep(0.018, 0.092, edge_distance);
}

float material_grain(vec3 world_position, float material_class) {
    float height_slice = floor(world_position.y * 0.53);
    float coarse = hash12(floor(world_position.xz * 0.72) + vec2(height_slice * 0.37, material_class * 11.17));
    float fine = hash12(floor(world_position.xy * 2.35) + vec2(floor(world_position.z * 0.41), material_class * 7.91));
    return coarse * 0.66 + fine * 0.34 - 0.5;
}

float ordered_alpha_threshold(vec2 pixel_position) {
    const float pattern[16] = float[](
         0.0,  8.0,  2.0, 10.0,
        12.0,  4.0, 14.0,  6.0,
         3.0, 11.0,  1.0,  9.0,
        15.0,  7.0, 13.0,  5.0);
    ivec2 cell = ivec2(mod(floor(pixel_position), 4.0));
    return (pattern[cell.x + cell.y * 4] + 0.5) / 16.0;
}
)" R"(

void main() {
    float water_mask = material_mask(v_material_class, 6.0);
    if (water_mask > 0.5 &&
        ship_excludes_ocean(v_world_position)) {
        // Je supprime l'ocean avant toute lecture de refraction afin que la
        // coque reste etanche sans payer le shader d'eau sous le navire.
        discard;
    }
    float weather_exposure =
        ship_shelters_weather(v_world_position)
            ? 0.0
            : 1.0;
    // Je separe le masque de surface du poids de houle : les eaux locales
    // gardent leurs petites normales animees sans subir le deplacement marin.
    float water_surface_mask =
        water_mask *
        clamp(
            max(v_normal.y, 0.0),
            0.0,
            1.0);

    // Je ne decale pas les UV partages de l'atlas dans le fragment pour eviter
    // d'echantillonner les sprites transparents voisins et d'ouvrir des trous.
    vec2 animated_uv = v_uv;

    vec4 sampled = texture(u_atlas, animated_uv);
    float early_glass_mask = material_mask(v_material_class, 10.0);
    if (early_glass_mask > 0.5) {
        // Je ne paie le motif Bayer que pour le verre; les fragments opaques
        // du monde conservent le test alpha simple et peu couteux.
        if (sampled.a < ordered_alpha_threshold(gl_FragCoord.xy)) {
            discard;
        }
    } else if (sampled.a < 0.1) {
        discard;
    }

    vec3 albedo = sampled.rgb;
    vec3 normal = normalize(v_normal);

    if (water_surface_mask > 0.001) {
        vec2 detail_gradient = vec2(0.0);

        if (u_ocean_detail_strength > 0.000001) {
            // La condition dépend d'un uniform. En qualité basse, le pilote peut
            // éliminer entièrement ces évaluations trigonométriques.
            detail_gradient = water_detail_gradient(
                v_world_position.xz,
                u_ocean_detail_phase);
        }
        detail_gradient += rain_dimple_gradient(
            v_world_position.xz,
            u_ocean_detail_phase);

        vec3 ocean_normal = normalize(
            vec3(
                v_ocean_normal.x - detail_gradient.x,
                max(v_ocean_normal.y, 0.08),
                v_ocean_normal.z - detail_gradient.y));

        normal = normalize(
            mix(
                normal,
                ocean_normal,
                water_surface_mask));
    }
    if (!gl_FrontFacing && water_mask > 0.5) {
        normal = -normal;
    }
)" R"(

    vec3 view_direction = normalize(u_camera_position - v_world_position);
    vec3 sun_direction = normalize(u_sun_direction);
    float daylight = clamp(u_daylight_factor, 0.0, 1.0);
    float sky_light = clamp(v_sky_light, 0.0, 1.0);
    float block_light = clamp(v_block_light, 0.0, 1.0);
    float shadow = sample_shadow(normal);
    float cloud_shadow = sample_cloud_shadow(v_world_position + normal * 0.35, sun_direction);

    float terrain_mask = material_mask(v_material_class, 0.0);
    float rock_mask = material_mask(v_material_class, 1.0);
    float sand_mask = material_mask(v_material_class, 2.0);
    float wood_mask = material_mask(v_material_class, 3.0);
    float foliage_mask = material_mask(v_material_class, 4.0);
    float flora_mask = material_mask(v_material_class, 5.0);
    float emissive_mask = material_mask(v_material_class, 7.0);
    float snow_mask = material_mask(v_material_class, 8.0);
    float fabric_mask = material_mask(v_material_class, 9.0);
    float glass_mask = material_mask(v_material_class, 10.0);
    float iron_mask = material_mask(v_material_class, 11.0);
    float brass_mask = material_mask(v_material_class, 12.0);
    float metal_mask = clamp(iron_mask + brass_mask, 0.0, 1.0);

    float view_alignment = mix(max(dot(view_direction, normal), 0.0), abs(dot(view_direction, normal)), water_mask);
    float sun_alignment = mix(max(dot(normal, sun_direction), 0.0), abs(dot(normal, sun_direction)), water_mask);

    float face_light = mix(0.82, 1.10, clamp(v_face_shade, 0.0, 1.0)) * mix(0.78, 1.00, clamp(v_ao, 0.0, 1.0));
    vec3 ambient = u_ambient_color * mix(0.40, 1.12, sky_light);
    ambient *= mix(0.88, 1.08, smoothstep(-0.25, 1.0, normal.y));

    float direct = mix(sun_alignment, sun_alignment * sun_alignment, 0.45);
    vec3 sunlight = u_sun_color * direct * shadow * cloud_shadow * u_sun_visibility * daylight * (0.72 + 0.28 * sky_light);

    float bounce_factor = smoothstep(-0.35, 1.0, normal.y) * sky_light;
    vec3 bounce_light = mix(u_fog_color, u_distant_fog_color, 0.42) * bounce_factor * (0.12 + 0.12 * daylight);
    vec3 torch_light = vec3(1.14, 0.70, 0.32) * block_light * (1.18 + emissive_mask * 0.55);

    float rim = pow(1.0 - view_alignment, mix(3.0, 1.7, water_mask + foliage_mask * 0.35 + flora_mask * 0.45 + fabric_mask * 0.40 + glass_mask * 0.55));
    vec3 rim_color =
        mix(u_fog_color, u_sun_color, 0.55) * rim * (0.02 + 0.08 * daylight + 0.04 * foliage_mask + 0.05 * flora_mask + 0.04 * fabric_mask + 0.10 * glass_mask);

    vec3 reflected = reflect(-sun_direction, normal);
    float specular_power = mix(11.0, 34.0, rock_mask + snow_mask * 0.3 + sand_mask * 0.1 + glass_mask * 0.55);
    specular_power = mix(specular_power, 18.0, wood_mask);
    specular_power = mix(specular_power, 42.0, glass_mask);
    specular_power = mix(specular_power, 72.0, metal_mask);
    float specular = pow(max(dot(reflected, view_direction), 0.0), specular_power);
    vec3 specular_color =
        u_sun_color * specular * shadow * cloud_shadow *
        (0.12 * rock_mask + 0.08 * wood_mask + 0.05 * snow_mask + 0.22 * glass_mask + 0.34 * iron_mask + 0.48 * brass_mask);

    float leaf_backlight = pow(max(dot(-normal, sun_direction), 0.0), 1.8);
    vec3 leaf_translucency =
        albedo * u_sun_color * leaf_backlight * mix(0.0, 0.06, foliage_mask) * u_sun_visibility * daylight * (0.35 + 0.65 * sky_light);
    leaf_translucency +=
        albedo * u_sun_color * leaf_backlight * mix(0.0, 0.10, flora_mask) * u_sun_visibility * daylight * (0.40 + 0.60 * sky_light);
    leaf_translucency +=
        albedo * u_sun_color * leaf_backlight * mix(0.0, 0.07, fabric_mask) * u_sun_visibility * daylight * (0.42 + 0.58 * sky_light);
    leaf_translucency *= mix(0.75, 1.0, cloud_shadow);

    vec3 material_tint = vec3(1.0);
    material_tint = mix(material_tint, vec3(0.70, 0.82, 0.58), terrain_mask * smoothstep(0.15, 1.0, normal.y) * 0.52);
    material_tint = mix(material_tint, vec3(1.03, 0.99, 0.92), sand_mask);
    material_tint = mix(material_tint, vec3(0.94, 0.98, 1.06), snow_mask);
    material_tint = mix(material_tint, vec3(1.06, 1.00, 0.84), fabric_mask);
    material_tint = mix(material_tint, vec3(0.84, 0.94, 1.08), glass_mask);
    material_tint = mix(material_tint, vec3(0.72, 0.78, 0.84), iron_mask);
    material_tint = mix(material_tint, vec3(1.16, 0.83, 0.38), brass_mask);
    material_tint = mix(material_tint, vec3(0.58, 0.72, 0.44), foliage_mask * 0.84);
    material_tint = mix(material_tint, vec3(0.90, 1.00, 0.84), flora_mask * 0.54);
    material_tint = mix(material_tint, vec3(1.02, 0.98, 0.94), wood_mask * 0.45);

    float natural_material_mask = clamp(terrain_mask + rock_mask + sand_mask + wood_mask + foliage_mask + flora_mask + snow_mask + fabric_mask, 0.0, 1.0);
    float grain = material_grain(v_world_position, v_material_class);
    float grain_strength =
        terrain_mask * 0.032 + rock_mask * 0.045 + sand_mask * 0.030 + wood_mask * 0.038 +
        foliage_mask * 0.034 + flora_mask * 0.026 + snow_mask * 0.020 + fabric_mask * 0.018;
    albedo *= 1.0 + grain * grain_strength * (0.55 + 0.45 * sky_light);

    vec3 grain_tint = mix(vec3(0.97, 1.02, 0.98), vec3(1.04, 0.98, 0.92), smoothstep(-0.22, 0.26, grain));
    material_tint = mix(material_tint, material_tint * grain_tint, natural_material_mask * 0.22);

    float solid_edge_mask = clamp(terrain_mask + rock_mask + sand_mask + wood_mask + snow_mask + fabric_mask * 0.25 + glass_mask * 0.35 + metal_mask, 0.0, 1.0);
    float tile_edge = atlas_tile_edge_factor(v_uv) * solid_edge_mask;
    float bevel_shadow = tile_edge * (0.020 + 0.030 * (1.0 - smoothstep(-0.25, 0.85, normal.y)));
    material_tint *= 1.0 - bevel_shadow;

    vec3 lit_color = albedo * material_tint * face_light * (ambient + bounce_light + sunlight + torch_light);
    lit_color += leaf_translucency;
    lit_color += rim_color + specular_color;
    lit_color += u_night_tint_color * (0.05 + 0.05 * sky_light) * (1.0 - daylight);

    float output_alpha = 1.0;
    if (water_mask > 0.5) {
        float fresnel = pow(1.0 - view_alignment, 4.5);
        float water_time = u_time_of_day * 20.0;
        vec2 detail_flow = vec2(
            sin(v_world_position.x * 0.31 + v_world_position.z * 0.17 + water_time * 0.75),
            cos(v_world_position.z * 0.29 - v_world_position.x * 0.21 - water_time * 0.66));

        vec2 scene_texel = 1.0 / vec2(textureSize(u_scene_color, 0));
        vec2 scene_uv = gl_FragCoord.xy * scene_texel;
        vec2 refraction_offset = (normal.xz * (0.010 + 0.006 * water_surface_mask) + detail_flow * 0.0015) *
                                 (0.28 + 0.72 * (1.0 - view_alignment));
        vec2 refracted_uv = clamp(scene_uv + refraction_offset, scene_texel * 0.5, vec2(1.0) - scene_texel * 0.5);

        float base_scene_depth = texture(u_scene_depth, scene_uv).r;
        float refracted_scene_depth = texture(u_scene_depth, refracted_uv).r;
        if (refracted_scene_depth + 0.00005 < gl_FragCoord.z) {
            refracted_uv = scene_uv;
            refracted_scene_depth = base_scene_depth;
        }

        vec3 scene_color = texture(u_scene_color, refracted_uv).rgb;
        float water_depth = 0.0;
        if (refracted_scene_depth < 0.9999) {
            vec3 background_position = reconstruct_world_position(refracted_uv, refracted_scene_depth);
            water_depth = max(distance(background_position, v_world_position), 0.0);
        } else {
            water_depth = 7.0 + 12.0 * fresnel;
        }
        water_depth = clamp(water_depth, 0.0, 48.0);

        float body_depth = max(water_depth, 0.32 + 0.18 * water_surface_mask);
        vec3 absorption = mix(vec3(0.90, 0.34, 0.12), vec3(0.72, 0.28, 0.10), daylight);
        vec3 transmittance = exp(-absorption * body_depth);

        vec3 shallow_color = mix(vec3(0.07, 0.20, 0.26), vec3(0.10, 0.42, 0.55), daylight);
        vec3 deep_color = mix(vec3(0.02, 0.08, 0.13), vec3(0.04, 0.19, 0.30), daylight);
        vec3 water_volume_color = mix(shallow_color, deep_color, smoothstep(0.25, 6.0, body_depth));
        float tempest_factor = clamp(u_ocean_tempest_factor, 0.0, 1.0);
        water_volume_color = mix(
            water_volume_color,
            water_volume_color * vec3(0.54, 0.66, 0.72),
            tempest_factor * 0.34);
        vec3 water_light = ambient * 0.82 + bounce_light * 0.95 + sunlight * 0.40 + torch_light * 0.55;
        vec3 water_body = scene_color * transmittance + water_volume_color * water_light * (1.0 - transmittance);

        vec3 reflected_view = reflect(-view_direction, normal);
        float horizon = clamp(reflected_view.y * 0.5 + 0.5, 0.0, 1.0);
        vec3 sky_reflection = mix(u_fog_color, u_distant_fog_color, horizon);
        sky_reflection = mix(sky_reflection, u_sun_color, 0.08 + 0.10 * daylight);

        vec3 sun_reflection = reflect(-sun_direction, normal);
        float open_sea = clamp(u_ocean_open_sea, 0.0, 1.0);
        float sparkle_power = mix(72.0, 42.0, open_sea);
        float sparkle = pow(max(dot(sun_reflection, view_direction), 0.0), sparkle_power);
        vec3 reflection = sky_reflection * fresnel * (0.18 + 0.16 * daylight);
        reflection += u_sun_color * sparkle * shadow * cloud_shadow * (0.12 + 0.18 * daylight) *
                      (1.0 - tempest_factor * 0.78);

        float shallow_foam = (1.0 - smoothstep(0.08, 0.70, body_depth)) * (0.40 + 0.60 * water_surface_mask);

        float crest_noise = 0.5;

        if (u_ocean_detail_strength > 0.000001) {
            crest_noise = value_noise2(
                v_world_position.xz * 0.54 +
                vec2(
                    cos(u_ocean_detail_phase),
                    sin(u_ocean_detail_phase * 2.0)) *
                0.34);
        }

        float slope_energy =
            clamp(
                (1.0 - normal.y) * 3.4,
                0.0,
                1.0);

        float crest_signal =
            v_ocean_crest * 0.82 +
            slope_energy * 0.28 +
            crest_noise * 0.08;

        // Je separe le reflet fin d'une crete calme de l'ecume epaisse. Cette
        // ligne bleu clair rend la houle lisible au soleil sans blanchir la mer.
        float crest_sheen =
            smoothstep(
                clamp(
                    u_ocean_foam_threshold - 0.16,
                    0.52,
                    0.78),
                0.98,
                crest_signal) *
            water_surface_mask *
            (0.18 + 0.82 * fresnel) *
            open_sea;

        float crest_foam =
            smoothstep(
                clamp(
                    u_ocean_foam_threshold,
                    0.55,
                    0.98),
                1.06,
                crest_signal) *
            water_surface_mask *
            (0.24 + 0.76 * fresnel) *
            (mix(0.20, 0.30, open_sea) +
             mix(0.80, 0.70, open_sea) *
                 clamp(
                     u_ocean_severity,
                     0.0,
                     1.0));

        // Je fais apparaître les déferlantes sur les pentes fortes de la houle
        // extrême, sans blanchir les mers ordinaires ni les eaux intérieures.
        float breaking_foam =
            smoothstep(
                0.34,
                0.92,
                slope_energy * 0.72 +
                    v_ocean_crest * 0.46 +
                    crest_noise * 0.10) *
            water_surface_mask *
            open_sea *
            tempest_factor;

        vec3 foam_color = mix(
            u_fog_color,
            vec3(0.86, 0.94, 1.0),
            0.65);

        vec3 foam =
            foam_color *
            (
                shallow_foam *
                    (0.10 + 0.06 * daylight) +
                crest_foam *
                    (0.12 + 0.11 * daylight +
                     tempest_factor * 0.10) +
                breaking_foam *
                    (0.12 + 0.10 * daylight)
            );

        foam +=
            mix(
                water_volume_color,
                foam_color,
                0.62) *
            crest_sheen *
            (0.035 + 0.040 * daylight);

        float shimmer = 0.5 + 0.5 * sin(v_world_position.x * 0.26 + v_world_position.z * 0.30 + u_time_of_day * 21.0);
        lit_color = water_body + reflection + foam;
        lit_color += water_volume_color * shimmer * (0.018 + 0.025 * daylight) * water_surface_mask;
        output_alpha = 1.0;
    }

    float wetness = clamp(u_precipitation_intensity, 0.0, 1.0) *
                    sky_light *
                    weather_exposure *
                    (1.0 - water_mask);
    wetness *= 0.45 + 0.55 * smoothstep(-0.10, 1.0, normal.y);
    lit_color = mix(lit_color, lit_color * vec3(0.72, 0.78, 0.86), wetness * (0.16 + 0.14 * clamp(u_storm_intensity, 0.0, 1.0)));

    float lightning_surface = clamp(u_lightning_intensity, 0.0, 1.0) *
                              sky_light *
                              mix(0.16, 1.0, weather_exposure) *
                              (0.35 + 0.65 * smoothstep(-0.20, 1.0, normal.y));
    lit_color += albedo * vec3(0.62, 0.72, 1.00) * lightning_surface * (0.24 + 0.22 * clamp(u_storm_intensity, 0.0, 1.0));

    lit_color += vec3(1.24, 0.68, 0.24) * emissive_mask * (0.32 + 0.90 * block_light);
    float super_vision = clamp(u_super_vision_strength, 0.0, 1.0) * (1.0 - daylight);
    vec3 super_floor = albedo * vec3(0.58, 0.70, 0.78);
    lit_color = mix(lit_color, max(lit_color, super_floor), super_vision * 0.72);
    lit_color += vec3(0.05, 0.13, 0.16) * super_vision * (0.50 + 0.50 * sky_light);

    vec3 view_ray = normalize(v_world_position - u_camera_position);
    float weather_fog = 1.0 + clamp(u_precipitation_intensity, 0.0, 1.0) * 0.42 + clamp(u_storm_intensity, 0.0, 1.0) * 0.38;
    float distance_fog = 1.0 - exp(-v_distance * v_distance * 0.000008 * weather_fog);
    float height_haze = 1.0 - exp(-max(30.0 - v_world_position.y, 0.0) * u_height_fog_density);
    height_haze *= clamp(v_distance / 140.0, 0.0, 1.0) * (0.10 + 0.18 * (1.0 - daylight));
    float fog = clamp(distance_fog + height_haze, 0.0, 1.0);
    fog = mix(fog, fog * 0.45, super_vision);
    float sun_scatter = pow(max(dot(view_ray, sun_direction), 0.0), 6.0);
    float horizon = 1.0 - clamp(abs(view_ray.y), 0.0, 1.0);
    vec3 fog_color = mix(u_fog_color, u_distant_fog_color, sqrt(fog));
    fog_color += mix(u_horizon_glow_color, u_sun_color, 0.35 + 0.20 * daylight) *
                 sun_scatter * horizon * u_atmospheric_scatter_strength * (0.18 + 0.82 * daylight);
    frag_color = vec4(mix(lit_color, fog_color, fog), output_alpha);
}
)";

    static constexpr auto* creature_vertex_shader = R"(#version 330 core
layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_face_uv;
layout(location = 3) in float a_face_index;
layout(location = 4) in mat4 i_transform;
layout(location = 8) in vec4 i_uv_pos_x;
layout(location = 9) in vec4 i_uv_neg_x;
layout(location = 10) in vec4 i_uv_pos_y;
layout(location = 11) in vec4 i_uv_neg_y;
layout(location = 12) in vec4 i_uv_pos_z;
layout(location = 13) in vec4 i_uv_neg_z;
layout(location = 14) in vec4 i_surface;
layout(location = 15) in vec4 i_lighting;

uniform mat4 u_view_projection;
uniform mat4 u_light_view_projection;
uniform vec3 u_camera_position;
uniform int u_modern_pipeline;

out vec2 v_uv;
out vec3 v_normal;
out vec3 v_local_position;
out vec3 v_world_position;
out float v_distance;
out float v_nightmare_factor;
out float v_tension;
out float v_material_class;
out float v_cavity_mask;
out float v_emissive_strength;
out float v_sky_light;
out float v_block_light;
out float v_precipitation_exposure;
out vec4 v_light_position;

vec4 face_uv_rect(float face_index) {
    if (face_index < 0.5) {
        return i_uv_pos_x;
    }
    if (face_index < 1.5) {
        return i_uv_neg_x;
    }
    if (face_index < 2.5) {
        return i_uv_pos_y;
    }
    if (face_index < 3.5) {
        return i_uv_neg_y;
    }
    if (face_index < 4.5) {
        return i_uv_pos_z;
    }
    return i_uv_neg_z;
}

void main() {
    vec4 world_position = i_transform * vec4(a_position, 1.0);
    mat3 normal_matrix = transpose(inverse(mat3(i_transform)));
    vec3 world_normal = normalize(normal_matrix * a_normal);
    vec4 uv_rect = face_uv_rect(a_face_index);
    if (u_modern_pipeline != 0) {
        // Les atlas joueur et créatures font 128 px. Je reste au centre des
        // texels de bord pour profiter du filtrage linéaire sans lire la tuile
        // voisine.
        vec2 half_texel = vec2(0.5 / 128.0);
        uv_rect = vec4(
            uv_rect.xy + half_texel,
            uv_rect.zw - half_texel);
    }

    gl_Position = u_view_projection * world_position;
    v_uv = mix(uv_rect.xy, uv_rect.zw, a_face_uv);
    v_normal = world_normal;
    v_local_position = a_position;
    v_world_position = world_position.xyz;
    v_distance = distance(world_position.xyz, u_camera_position);
    v_nightmare_factor = i_surface.x;
    v_tension = i_surface.y;
    v_material_class = i_surface.z;
    v_cavity_mask = i_surface.w;
    v_emissive_strength = i_lighting.x;
    v_sky_light = i_lighting.y;
    v_block_light = i_lighting.z;
    v_precipitation_exposure = i_lighting.w;
    v_light_position = u_light_view_projection * world_position;
}
)";

    static constexpr auto* creature_fragment_shader = R"(#version 330 core
in vec2 v_uv;
in vec3 v_normal;
in vec3 v_local_position;
in vec3 v_world_position;
in float v_distance;
in float v_nightmare_factor;
in float v_tension;
in float v_material_class;
in float v_cavity_mask;
in float v_emissive_strength;
in float v_sky_light;
in float v_block_light;
in float v_precipitation_exposure;
in vec4 v_light_position;

uniform sampler2D u_atlas;
uniform sampler2D u_shadow_map;
uniform sampler2D u_shadow_map_far;
uniform mat4 u_light_view_projection_far;
uniform vec3 u_camera_position;
uniform vec3 u_camera_forward;
uniform vec3 u_sun_direction;
uniform vec3 u_sun_color;
uniform vec3 u_ambient_color;
uniform vec3 u_fog_color;
uniform vec3 u_distant_fog_color;
uniform vec3 u_horizon_glow_color;
uniform vec3 u_night_tint_color;
uniform float u_daylight_factor;
uniform float u_sun_visibility;
uniform float u_time_of_day;
uniform float u_cloud_intensity;
uniform float u_cloud_shadow_strength;
uniform float u_atmospheric_scatter_strength;
uniform float u_height_fog_density;
uniform float u_precipitation_intensity;
uniform float u_storm_intensity;
uniform float u_lightning_intensity;
uniform int u_shadows_enabled;
uniform int u_shadow_cascade_count;
uniform float u_shadow_split_distance;
uniform float u_shadow_transition_width;
uniform float u_player_light_strength;
uniform float u_super_vision_strength;
uniform int u_modern_pipeline;

out vec4 frag_color;

float shadow_visibility_at(
    vec2 uv,
    float receiver_depth,
    float bias,
    bool far_cascade
) {
    float sampled_depth = far_cascade
        ? texture(u_shadow_map_far, uv).r
        : texture(u_shadow_map, uv).r;
    return (receiver_depth - bias) <= sampled_depth ? 1.0 : 0.0;
}

float sample_shadow_cascade(vec3 normal, bool far_cascade) {
    vec4 light_position = far_cascade
        ? u_light_view_projection_far * vec4(v_world_position, 1.0)
        : v_light_position;
    vec3 projected = light_position.xyz / max(light_position.w, 0.0001);
    projected = projected * 0.5 + 0.5;
    if (projected.z < 0.0 || projected.z > 1.0 || projected.x < 0.0 || projected.x > 1.0 || projected.y < 0.0 || projected.y > 1.0) {
        return 1.0;
    }

    vec2 texel_size = far_cascade
        ? 1.0 / vec2(textureSize(u_shadow_map_far, 0))
        : 1.0 / vec2(textureSize(u_shadow_map, 0));
    float ndotl = max(dot(normalize(normal), normalize(u_sun_direction)), 0.0);
    float bias =
        max(0.00065 * (1.0 - ndotl), 0.00012) *
        (far_cascade ? 1.35 : 1.0);
    // Je garde un PCF en croix pour lisser les ombres sans payer neuf lectures texture par fragment.
    float visibility = shadow_visibility_at(projected.xy, projected.z, bias, far_cascade) * 0.36;
    visibility += shadow_visibility_at(projected.xy + vec2(texel_size.x, 0.0), projected.z, bias, far_cascade) * 0.16;
    visibility += shadow_visibility_at(projected.xy - vec2(texel_size.x, 0.0), projected.z, bias, far_cascade) * 0.16;
    visibility += shadow_visibility_at(projected.xy + vec2(0.0, texel_size.y), projected.z, bias, far_cascade) * 0.16;
    visibility += shadow_visibility_at(projected.xy - vec2(0.0, texel_size.y), projected.z, bias, far_cascade) * 0.16;
    return visibility;
}

float sample_shadow(vec3 normal) {
    if (u_sun_visibility < 0.5 || u_shadows_enabled == 0) {
        return 1.0;
    }
    if (u_shadow_cascade_count <= 1) {
        return sample_shadow_cascade(normal, false);
    }

    float view_depth = max(
        dot(v_world_position - u_camera_position, u_camera_forward),
        0.0);
    float transition_width = max(u_shadow_transition_width, 0.0);
    if (transition_width <= 0.0001) {
        return sample_shadow_cascade(
            normal,
            view_depth > u_shadow_split_distance);
    }

    float half_width = transition_width * 0.5;
    if (view_depth <= u_shadow_split_distance - half_width) {
        return sample_shadow_cascade(normal, false);
    }
    if (view_depth >= u_shadow_split_distance + half_width) {
        return sample_shadow_cascade(normal, true);
    }

    float blend = smoothstep(
        u_shadow_split_distance - half_width,
        u_shadow_split_distance + half_width,
        view_depth);
    return mix(
        sample_shadow_cascade(normal, false),
        sample_shadow_cascade(normal, true),
        blend);
}

float hash12(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123);
}

float value_noise2(vec2 p) {
    vec2 cell = floor(p);
    vec2 local = fract(p);
    vec2 blend = local * local * (3.0 - 2.0 * local);

    float n00 = hash12(cell);
    float n10 = hash12(cell + vec2(1.0, 0.0));
    float n01 = hash12(cell + vec2(0.0, 1.0));
    float n11 = hash12(cell + vec2(1.0, 1.0));
    float nx0 = mix(n00, n10, blend.x);
    float nx1 = mix(n01, n11, blend.x);
    return mix(nx0, nx1, blend.y);
}

float sample_cloud_shadow(vec3 world_position, vec3 sun_direction) {
    float cloud_factor = clamp(u_cloud_intensity, 0.0, 1.0);
    float daylight = clamp(u_daylight_factor, 0.0, 1.0);
    if (cloud_factor <= 0.01 || u_cloud_shadow_strength <= 0.001 || daylight <= 0.20 || sun_direction.y <= 0.02) {
        return 1.0;
    }

    float projection_scale = (96.0 - world_position.y) / max(sun_direction.y, 0.12);
    vec2 projected = world_position.xz + sun_direction.xz * projection_scale;
    vec2 flow = projected * 0.0032 + vec2(u_time_of_day * 0.085, -u_time_of_day * 0.061);
    float base = value_noise2(flow);
    float detail = value_noise2(flow * 2.17 + vec2(9.3, 4.7));
    float cloud = smoothstep(0.52, 0.84, base * 0.68 + detail * 0.32);
    float coverage = smoothstep(0.10, 0.58, cloud_factor);
    return 1.0 - cloud * coverage * u_cloud_shadow_strength;
}

void main() {
    vec4 sampled = texture(u_atlas, v_uv);
    vec3 albedo = sampled.rgb;
    float emissive_mask = sampled.a;
    vec3 normal = normalize(v_normal);
    vec3 view_direction = normalize(u_camera_position - v_world_position);
    vec3 sun_direction = normalize(u_sun_direction);

    float shadow = sample_shadow(normal);
    float cloud_shadow = sample_cloud_shadow(v_world_position + normal * 0.50, sun_direction);
    float instance_sky_light = clamp(v_sky_light, 0.0, 1.0);
    float instance_block_light = clamp(v_block_light, 0.0, 1.0);
    float sky_mix = clamp(u_daylight_factor, 0.0, 1.0) * instance_sky_light;
    float super_vision = clamp(u_super_vision_strength, 0.0, 1.0) * (1.0 - sky_mix);
    float cavity = clamp(v_cavity_mask, 0.0, 1.0);
    float hard_material = smoothstep(0.44, 0.90, v_material_class);
    float soft_fiber = 1.0 - smoothstep(0.28, 0.58, v_material_class);
    float thin_surface = 1.0 - smoothstep(0.22, 0.50, v_material_class);
    if (u_modern_pipeline != 0) {
        // Je fixe le grain dans l'espace local de chaque pièce : la matière
        // suit l'animation au lieu de glisser sur l'animal quand il avance.
        float coarse_surface = value_noise2(
            v_local_position.xz * 5.4 +
            vec2(v_local_position.y * 1.7, v_material_class * 13.1));
        float fine_surface = value_noise2(
            v_local_position.xy * 12.0 +
            vec2(v_local_position.z * 2.3, v_material_class * 7.7));
        float surface_variation =
            (coarse_surface - 0.5) * 0.12 +
            (fine_surface - 0.5) * 0.045;
        float organic_surface = 1.0 - hard_material;
        albedo *= 1.0 + surface_variation * organic_surface;

        // Une variation très légère sous le volume sépare mieux le ventre et
        // les membres sans inventer un nouveau matériau ou modifier le rig.
        float underside =
            1.0 - smoothstep(-0.72, 0.10, normal.y);
        albedo = mix(
            albedo,
            albedo * 1.065 + vec3(0.006),
            underside * soft_fiber * 0.32);
    }

    float cavity_occlusion = mix(1.0, 0.54, cavity * (0.62 + 0.14 * v_nightmare_factor));
    float ambient_strength = mix(0.42, 1.02, sky_mix) * mix(1.08, 0.84, hard_material) * cavity_occlusion;
    vec3 ambient = u_ambient_color * ambient_strength;

    float wrap = mix(0.34, 0.10, hard_material);
    float sun_wrap = clamp((dot(normal, sun_direction) + wrap) / (1.0 + wrap), 0.0, 1.0);
    vec3 sunlight = u_sun_color * (sun_wrap * sky_mix * shadow * cloud_shadow * u_sun_visibility);

    float backlight = pow(max(dot(normal, -sun_direction), 0.0), 1.8);
    vec3 translucency = u_sun_color * backlight * thin_surface * sky_mix * u_sun_visibility * cloud_shadow * (0.04 + 0.10 * soft_fiber);

    float player_light_distance = length((u_camera_position + vec3(0.0, -0.18, 0.0)) - v_world_position);
    float player_light_falloff = 1.0 - smoothstep(1.2, 8.8, player_light_distance);
    float player_light_facing = 0.55 + 0.45 * max(dot(normal, view_direction), 0.0);
    float player_light_night_boost = mix(0.18, 1.0, 1.0 - sky_mix);
    vec3 player_light =
        vec3(1.18, 0.78, 0.36) * u_player_light_strength * player_light_falloff * player_light_facing * player_light_night_boost;
    float local_light_facing = 0.62 + 0.38 * max(dot(normal, view_direction), 0.0);
    vec3 local_light = vec3(1.16, 0.73, 0.34) * instance_block_light * local_light_facing * (0.34 + 0.42 * albedo);

    float rim = pow(1.0 - max(dot(view_direction, normal), 0.0), 2.45);
    vec3 rim_light = mix(vec3(0.12, 0.10, 0.08), vec3(0.34, 0.50, 0.60), 1.0 - sky_mix);
    rim_light *= rim * mix(0.08, 0.16, 1.0 - hard_material) * mix(0.78, 1.04, v_nightmare_factor);
    vec3 super_vision_glow = mix(vec3(0.12, 0.72, 0.90), vec3(1.00, 0.24, 0.14), v_nightmare_factor);
    super_vision_glow *= super_vision * (0.24 + 0.32 * rim + 0.18 * emissive_mask + 0.16 * v_nightmare_factor);

    vec3 reflected = reflect(-sun_direction, normal);
    float specular = pow(max(dot(reflected, view_direction), 0.0), mix(42.0, 16.0, hard_material));
    float hard_specular = specular * smoothstep(0.52, 0.90, v_material_class);
    vec3 specular_color = u_sun_color * hard_specular * shadow * cloud_shadow * sky_mix * u_sun_visibility * (0.03 + 0.18 * v_nightmare_factor);

    float pulse = 0.84 + 0.16 * sin(u_time_of_day * 1.7 + v_tension * 7.0 + v_world_position.y * 2.2);
    vec3 nightmare_glow =
        vec3(1.00, 0.18, 0.12) * emissive_mask * v_emissive_strength * v_nightmare_factor * (0.24 + v_tension * 0.30) * pulse;

    vec3 lit_color = albedo * (ambient + sunlight + translucency + player_light) + local_light;
    lit_color *= cavity_occlusion;
    lit_color += rim_light + specular_color;
    lit_color = mix(lit_color, max(lit_color, albedo * vec3(0.62, 0.82, 0.88)), super_vision * 0.72);
    lit_color += u_night_tint_color * (0.09 + 0.08 * v_nightmare_factor) * (1.0 - sky_mix);
    float wetness = clamp(u_precipitation_intensity, 0.0, 1.0) * clamp(v_precipitation_exposure, 0.0, 1.0) *
                    (0.40 + 0.60 * smoothstep(-0.10, 1.0, normal.y));
    lit_color = mix(lit_color, lit_color * vec3(0.74, 0.80, 0.88), wetness * (0.12 + 0.12 * clamp(u_storm_intensity, 0.0, 1.0)));
    float lightning_surface = clamp(u_lightning_intensity, 0.0, 1.0) * instance_sky_light *
                              clamp(v_precipitation_exposure, 0.0, 1.0) *
                              (0.35 + 0.65 * smoothstep(-0.20, 1.0, normal.y));
    lit_color += albedo * vec3(0.62, 0.72, 1.00) * lightning_surface * (0.18 + 0.22 * clamp(u_storm_intensity, 0.0, 1.0));
    vec3 view_ray = normalize(v_world_position - u_camera_position);
    float weather_fog = 1.0 + clamp(u_precipitation_intensity, 0.0, 1.0) * 0.38 + clamp(u_storm_intensity, 0.0, 1.0) * 0.34;
    float distance_fog = 1.0 - exp(-v_distance * v_distance * 0.000009 * weather_fog);
    float height_haze = 1.0 - exp(-max(28.0 - v_world_position.y, 0.0) * u_height_fog_density);
    height_haze *= clamp(v_distance / 130.0, 0.0, 1.0) * (0.08 + 0.12 * (1.0 - sky_mix));
    float fog = clamp(distance_fog + height_haze, 0.0, 1.0);
    fog = mix(fog, fog * 0.38, super_vision);
    float sun_scatter = pow(max(dot(view_ray, sun_direction), 0.0), 6.0);
    float horizon = 1.0 - clamp(abs(view_ray.y), 0.0, 1.0);
    vec3 fog_color = mix(u_fog_color, u_distant_fog_color, sqrt(fog));
    fog_color += mix(u_horizon_glow_color, u_sun_color, 0.34 + 0.20 * sky_mix) *
                 sun_scatter * horizon * u_atmospheric_scatter_strength * (0.16 + 0.78 * sky_mix);
    vec3 fogged_color = mix(lit_color, fog_color, fog);
    vec3 fogged_glow = (nightmare_glow + super_vision_glow) * (1.0 - fog * 0.72);
    frag_color = vec4(fogged_color + fogged_glow, 1.0);
}
)";

    static constexpr auto* creature_shadow_vertex_shader = R"(#version 330 core
layout(location = 0) in vec3 a_position;
layout(location = 4) in mat4 i_transform;

uniform mat4 u_light_view_projection;

void main() {
    gl_Position = u_light_view_projection * i_transform * vec4(a_position, 1.0);
}
)";

    static constexpr auto* creature_shadow_fragment_shader = R"(#version 330 core
void main() {
}
)";

    static constexpr auto* shadow_vertex_shader = R"(#version 330 core
layout(location = 0) in vec3 a_position;
layout(location = 1) in vec2 a_uv;
layout(location = 7) in float a_material_class;
layout(location = 8) in float a_wave_weight;

uniform mat4 u_model;
uniform mat4 u_light_view_projection;
uniform float u_time_of_day;
uniform float u_wind_strength;

out vec2 v_uv;
flat out float v_material_class;

float material_mask(float material, float expected) {
    return 1.0 - step(0.25, abs(material - expected));
}

vec2 vegetation_wind_offset(vec3 world_position, float material_class, float time_phase) {
    float foliage_mask = material_mask(material_class, 4.0);
    float flora_mask = material_mask(material_class, 5.0);
    float wind_mask = max(foliage_mask * 0.35, flora_mask);
    if (wind_mask <= 0.0) {
        return vec2(0.0);
    }

    float gust_a = sin(world_position.x * 0.18 + world_position.z * 0.11 + time_phase * 1.35);
    float gust_b = cos(world_position.x * -0.13 + world_position.z * 0.21 + time_phase * 1.65);
    float flutter = sin((world_position.x + world_position.z) * 0.75 + world_position.y * 0.45 + time_phase * 2.40);
    float local_height = clamp(fract(world_position.y), 0.0, 1.0);
    local_height = mix(1.0, smoothstep(0.02, 0.98, local_height), flora_mask);
    float amplitude = u_wind_strength * wind_mask * mix(0.010, 0.032, flora_mask);
    return vec2(gust_a * 0.70 + flutter * 0.30, gust_b * 0.60 - gust_a * 0.22) * amplitude * local_height;
}

vec3 fabric_wind_offset(
    vec3 world_position,
    float material_class,
    float vertex_weight,
    float time_phase
) {
    float fabric_mask = material_mask(material_class, 9.0);
    float flexibility = clamp(vertex_weight, 0.0, 1.0) * fabric_mask;
    if (flexibility <= 0.0) {
        return vec3(0.0);
    }

    float wind = clamp(u_wind_strength, 0.0, 1.0);
    vec2 wind_direction = normalize(vec2(0.82, 0.57));
    vec2 transverse = vec2(-wind_direction.y, wind_direction.x);
    float phase =
        dot(world_position.xz, vec2(0.17, 0.11)) +
        world_position.y * 0.19 +
        time_phase * 1.24;
    float billow = sin(phase) + sin(phase * 2.13 + 0.7) * 0.28;
    float flutter = sin(phase * 3.71 - world_position.y * 0.31);
    float amplitude = flexibility * (0.014 + wind * 0.082);
    vec2 horizontal =
        wind_direction * billow * amplitude +
        transverse * flutter * amplitude * 0.24;
    return vec3(
        horizontal.x,
        flutter * amplitude * 0.055,
        horizontal.y);
}

void main() {
    vec4 world_position = u_model * vec4(a_position, 1.0);
    world_position.xz += vegetation_wind_offset(world_position.xyz, a_material_class, u_time_of_day * 8.0);
    world_position.xyz += fabric_wind_offset(
        world_position.xyz,
        a_material_class,
        a_wave_weight,
        u_time_of_day * 8.0);
    gl_Position = u_light_view_projection * world_position;
    v_uv = a_uv;
    v_material_class = a_material_class;
}
)";

    static constexpr auto* shadow_fragment_shader = R"(#version 330 core
in vec2 v_uv;
flat in float v_material_class;

uniform sampler2D u_atlas;

float material_mask(float material, float expected) {
    return 1.0 - step(0.25, abs(material - expected));
}

float ordered_alpha_threshold(vec2 pixel_position) {
    const float pattern[16] = float[](
         0.0,  8.0,  2.0, 10.0,
        12.0,  4.0, 14.0,  6.0,
         3.0, 11.0,  1.0,  9.0,
        15.0,  7.0, 13.0,  5.0);
    ivec2 cell = ivec2(mod(floor(pixel_position), 4.0));
    return (pattern[cell.x + cell.y * 4] + 0.5) / 16.0;
}

void main() {
    float alpha = texture(u_atlas, v_uv).a;
    float glass_mask = material_mask(v_material_class, 10.0);
    float threshold = glass_mask > 0.5 ? ordered_alpha_threshold(gl_FragCoord.xy) : 0.1;
    if (alpha < threshold) {
        discard;
    }
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
uniform sampler2D u_font_atlas;
uniform sampler2DArray u_model_icon_atlas;

out vec4 frag_color;

float median3(vec3 value) {
    return max(min(value.r, value.g), min(max(value.r, value.g), value.b));
}

void main() {
    vec4 color = v_color;
    if (v_textured > 2.5) {
        float layer = floor(v_textured - 3.0 + 0.5);
        color *= texture(u_model_icon_atlas, vec3(v_uv, layer));
    } else if (v_textured > 1.5) {
        float signed_distance = median3(texture(u_font_atlas, v_uv).rgb);
        float antialias_width = max(fwidth(signed_distance), 1.0 / 255.0);
        float coverage = smoothstep(
            0.5 - antialias_width,
            0.5 + antialias_width,
            signed_distance);
        color.a *= coverage;
    } else if (v_textured > 0.5) {
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

    static constexpr auto* sky_fragment_shader = kSkyFragmentShaderSource;

    static constexpr auto* glow_extract_fragment_shader = R"(#version 330 core
in vec2 v_uv;

uniform sampler2D u_scene_texture;
uniform float u_threshold;

out vec4 frag_color;

void main() {
    vec3 color = texture(u_scene_texture, v_uv).rgb;
    float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));
    // Je conserve progressivement les hautes lumières autour du seuil afin
    // d'éviter un halo qui apparaît brutalement d'une image à l'autre.
    float knee = max(u_threshold * 0.45, 0.0001);
    float soft = clamp(luminance - u_threshold + knee, 0.0, 2.0 * knee);
    soft = soft * soft / (4.0 * knee + 0.0001);
    float bloom = max(luminance - u_threshold, soft);
    frag_color = vec4(color * bloom / max(luminance, 0.0001), 1.0);
}
)";

    static constexpr auto* precipitation_vertex_shader = R"(#version 330 core
layout(location = 0) in vec2 a_quad_position;
layout(location = 1) in vec4 i_position_length;
layout(location = 2) in vec4 i_velocity_width;
layout(location = 3) in vec4 i_appearance;

uniform mat4 u_view_projection;
uniform vec3 u_camera_position;
uniform vec3 u_camera_right;
uniform vec3 u_camera_up;

out vec2 v_uv;
out vec3 v_world_position;
out float v_opacity;
out float v_kind;
out float v_age_ratio;

void main() {
    vec3 instance_position = i_position_length.xyz;
    float length_value = max(i_position_length.w, 0.001);
    vec3 velocity = i_velocity_width.xyz;
    float width_value = max(i_velocity_width.w, 0.001);
    float kind = i_appearance.y;
    vec3 world_position;

    if (kind < 0.5) {
        vec3 fall_direction = normalize(
            dot(velocity, velocity) > 0.000001
                ? velocity
                : vec3(0.0, -1.0, 0.0));
        vec3 view_direction = normalize(u_camera_position - instance_position);
        vec3 side = cross(fall_direction, view_direction);
        if (dot(side, side) <= 0.00001) {
            side = u_camera_right;
        } else {
            side = normalize(side);
        }
        world_position =
            instance_position +
            fall_direction * ((a_quad_position.y - 0.5) * length_value) +
            side * (a_quad_position.x * width_value);
    } else {
        float age = clamp(i_appearance.z, 0.0, 1.0);
        float radius = max(i_appearance.w, 0.01) * mix(0.62, 1.18, age);
        world_position =
            instance_position +
            u_camera_right * (a_quad_position.x * radius * 2.0) +
            u_camera_up * (a_quad_position.y * radius);
    }

    gl_Position = u_view_projection * vec4(world_position, 1.0);
    v_uv = vec2(a_quad_position.x + 0.5, a_quad_position.y);
    v_world_position = world_position;
    v_opacity = clamp(i_appearance.x, 0.0, 1.0);
    v_kind = kind;
    v_age_ratio = clamp(i_appearance.z, 0.0, 1.0);
}
)";

    static constexpr auto* precipitation_fragment_shader = R"(#version 330 core
in vec2 v_uv;
in vec3 v_world_position;
in float v_opacity;
in float v_kind;
in float v_age_ratio;

uniform vec3 u_camera_position;
uniform vec3 u_fog_color;
uniform float u_lightning_intensity;
uniform float u_storm_intensity;

out vec4 frag_color;
)" VALCRAFT_SHIP_PROTECTION_GLSL_SOURCE R"(

void main() {
    if (ship_shelters_weather(v_world_position) ||
        (v_kind >= 0.5 &&
         ship_excludes_ocean(v_world_position))) {
        discard;
    }

    float alpha;
    vec3 color;
    if (v_kind < 0.5) {
        float horizontal = 1.0 - smoothstep(0.18, 0.50, abs(v_uv.x - 0.5));
        float head = smoothstep(0.0, 0.10, v_uv.y);
        float tail = 1.0 - smoothstep(0.70, 1.0, v_uv.y);
        alpha = horizontal * head * tail * v_opacity;
        color = mix(vec3(0.48, 0.62, 0.78), vec3(0.82, 0.91, 1.0), v_uv.y);
    } else {
        float centered_x = abs(v_uv.x - 0.5) * 2.0;
        float crown = 1.0 - smoothstep(0.08, 0.82, centered_x);
        float stem = 1.0 - smoothstep(0.12, 0.72, v_uv.y);
        float droplets = smoothstep(0.18, 0.55, v_uv.y) *
                         (1.0 - smoothstep(0.58, 1.0, v_uv.y)) *
                         (0.55 + 0.45 * cos((v_uv.x - 0.5) * 18.0));
        alpha = max(crown * stem * 0.52, droplets) *
                (1.0 - v_age_ratio) *
                v_opacity;
        color = vec3(0.58, 0.78, 0.92);
    }

    float distance_fade =
        1.0 - smoothstep(22.0, 62.0, distance(v_world_position, u_camera_position));
    alpha *= distance_fade;
    if (alpha <= 0.003) {
        discard;
    }

    float lightning = clamp(u_lightning_intensity, 0.0, 1.0);
    float storm = clamp(u_storm_intensity, 0.0, 1.0);
    color = mix(color, u_fog_color + vec3(0.10, 0.16, 0.24), storm * 0.22);
    color += vec3(0.52, 0.62, 0.90) * lightning * 0.65;
    frag_color = vec4(color, alpha);
}
)";

    static constexpr auto* old_guard_effect_vertex_shader = R"(#version 330 core
layout(location = 0) in vec2 a_quad_position;
layout(location = 1) in vec4 i_position_size;
layout(location = 2) in vec4 i_appearance;

uniform mat4 u_view_projection;
uniform vec3 u_camera_right;
uniform vec3 u_camera_up;

out vec2 v_uv;
out float v_opacity;
out float v_kind;
out float v_intensity;

void main() {
    float angle = i_appearance.z;
    float sine = sin(angle);
    float cosine = cos(angle);
    vec2 rotated = vec2(
        a_quad_position.x * cosine - a_quad_position.y * sine,
        a_quad_position.x * sine + a_quad_position.y * cosine);
    float size_value = max(i_position_size.w, 0.001);
    vec3 world_position =
        i_position_size.xyz +
        u_camera_right * rotated.x * size_value +
        u_camera_up * rotated.y * size_value;
    gl_Position = u_view_projection * vec4(world_position, 1.0);
    v_uv = a_quad_position + vec2(0.5);
    v_opacity = clamp(i_appearance.x, 0.0, 1.0);
    v_kind = i_appearance.y;
    v_intensity = max(i_appearance.w, 0.0);
}
)";

    static constexpr auto* old_guard_effect_fragment_shader = R"(#version 330 core
in vec2 v_uv;
in float v_opacity;
in float v_kind;
in float v_intensity;

out vec4 frag_color;

float smoke_noise(vec2 point) {
    return fract(sin(dot(point, vec2(37.13, 91.73))) * 43758.5453);
}

void main() {
    vec2 centered = v_uv - vec2(0.5);
    float radius = length(centered);
    float alpha;
    vec3 color;

    if (v_kind < 0.5) {
        float body = 1.0 - smoothstep(0.18, 0.52, radius);
        float turbulence =
            0.78 +
            0.22 * smoke_noise(floor(v_uv * 9.0) + vec2(v_intensity * 3.1));
        alpha = body * turbulence * v_opacity;
        color = mix(
            vec3(0.46, 0.48, 0.50),
            vec3(0.77, 0.75, 0.69),
            clamp(v_intensity, 0.0, 1.0));
    } else {
        float core = 1.0 - smoothstep(0.02, 0.22, radius);
        float horizontal =
            1.0 - smoothstep(0.015, 0.11, abs(centered.y));
        float vertical =
            1.0 - smoothstep(0.015, 0.10, abs(centered.x));
        float star = max(core, max(horizontal, vertical) * (1.0 - radius * 1.65));
        alpha = clamp(star, 0.0, 1.0) * v_opacity;
        color = mix(vec3(1.0, 0.34, 0.05), vec3(1.0, 0.94, 0.62), core);
        color *= 1.0 + min(v_intensity, 2.0) * 1.4;
    }

    if (alpha <= 0.003) {
        discard;
    }
    frag_color = vec4(color, alpha);
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
uniform sampler2D u_scene_depth;
uniform float u_exposure;
uniform float u_saturation_boost;
uniform float u_contrast;
uniform float u_vignette_strength;
uniform vec3 u_night_tint_color;
uniform float u_glow_strength;
uniform float u_sharpen_strength;
uniform float u_edge_strength;
uniform int u_fxaa_enabled;
uniform int u_modern_pipeline;
uniform int u_resolve_only;
uniform float u_storm_intensity;
uniform float u_lightning_intensity;
uniform float u_weather_exposure;

out vec4 frag_color;

vec3 apply_saturation(vec3 color, float saturation) {
    float luma = dot(color, vec3(0.2126, 0.7152, 0.0722));
    return mix(vec3(luma), color, saturation);
}

vec3 sample_scene(vec2 uv) {
    return texture(u_scene_texture, clamp(uv, vec2(0.0), vec2(1.0))).rgb;
}

float scene_luma(vec3 color) {
    return dot(color, vec3(0.299, 0.587, 0.114));
}

vec3 sample_scene_fxaa(vec2 uv, vec2 texel) {
    vec3 center = sample_scene(uv);
    if (u_fxaa_enabled == 0) {
        return center;
    }

    vec3 north_west = sample_scene(uv + vec2(-texel.x, texel.y));
    vec3 north_east = sample_scene(uv + vec2(texel.x, texel.y));
    vec3 south_west = sample_scene(uv + vec2(-texel.x, -texel.y));
    vec3 south_east = sample_scene(uv + vec2(texel.x, -texel.y));
    float luma_center = scene_luma(center);
    float luma_north_west = scene_luma(north_west);
    float luma_north_east = scene_luma(north_east);
    float luma_south_west = scene_luma(south_west);
    float luma_south_east = scene_luma(south_east);
    float luma_min = min(
        luma_center,
        min(
            min(luma_north_west, luma_north_east),
            min(luma_south_west, luma_south_east)));
    float luma_max = max(
        luma_center,
        max(
            max(luma_north_west, luma_north_east),
            max(luma_south_west, luma_south_east)));
    if (luma_max - luma_min < max(0.0312, luma_max * 0.125)) {
        return center;
    }

    vec2 direction;
    direction.x =
        -((luma_north_west + luma_north_east) -
          (luma_south_west + luma_south_east));
    direction.y =
        (luma_north_west + luma_south_west) -
        (luma_north_east + luma_south_east);
    float direction_reduce = max(
        (luma_north_west + luma_north_east +
         luma_south_west + luma_south_east) *
            0.03125,
        0.0078125);
    float reciprocal_minimum =
        1.0 / (min(abs(direction.x), abs(direction.y)) + direction_reduce);
    direction =
        clamp(direction * reciprocal_minimum, vec2(-8.0), vec2(8.0)) *
        texel;

    vec3 result_a =
        0.5 *
        (sample_scene(uv + direction * (1.0 / 3.0 - 0.5)) +
         sample_scene(uv + direction * (2.0 / 3.0 - 0.5)));
    vec3 result_b =
        result_a * 0.5 +
        0.25 *
        (sample_scene(uv + direction * -0.5) +
         sample_scene(uv + direction * 0.5));
    float result_b_luma = scene_luma(result_b);
    return result_b_luma < luma_min || result_b_luma > luma_max
               ? result_a
               : result_b;
}

vec3 aces_fitted(vec3 color) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp(
        (color * (a * color + b)) /
            (color * (c * color + d) + e),
        0.0,
        1.0);
}

float linearize_depth(float depth_sample) {
    const float near_plane = 0.1;
    const float far_plane = 320.0;
    float z = depth_sample * 2.0 - 1.0;
    return (2.0 * near_plane * far_plane) / max(far_plane + near_plane - z * (far_plane - near_plane), 0.0001);
}

bool depth_sample_is_usable(float depth_sample) {
    return depth_sample > 0.00001 && depth_sample < 0.9999;
}

vec3 apply_palette_grade(vec3 color, float storm, float lightning) {
    float luma = dot(color, vec3(0.2126, 0.7152, 0.0722));
    vec3 shadow_tint = vec3(0.93, 0.97, 1.05);
    vec3 highlight_tint = vec3(1.055, 1.010, 0.940);
    vec3 graded = color;
    graded *= mix(shadow_tint, vec3(1.0), smoothstep(0.18, 0.55, luma));
    graded *= mix(vec3(1.0), highlight_tint, smoothstep(0.48, 0.94, luma));
    float grade_strength = (0.34 + 0.10 * smoothstep(0.10, 0.78, luma)) * (1.0 - storm * 0.28) * (1.0 - lightning * 0.55);
    return mix(color, graded, clamp(grade_strength, 0.0, 0.46));
}

void main() {
    vec2 texel = 1.0 / vec2(textureSize(u_scene_texture, 0));
    vec3 scene =
        u_resolve_only != 0
            ? sample_scene(v_uv)
            : sample_scene_fxaa(v_uv, texel);

    if (u_resolve_only != 0) {
        // Même lorsque les effets optionnels sont désactivés, la cible de la
        // version moderne reste HDR et linéaire. Ce chemin minimal effectue
        // uniquement la conversion HDR -> SDR et l'encodage gamma.
        vec3 non_negative_scene = max(scene, vec3(0.0));
        vec3 color =
            u_modern_pipeline != 0
                ? aces_fitted(
                      non_negative_scene *
                      max(u_exposure, 0.001))
                : vec3(1.0) -
                      exp(
                          -non_negative_scene *
                          max(u_exposure, 0.001));
        color = pow(
            clamp(color, 0.0, 1.0),
            vec3(1.0 / 2.2));
        frag_color = vec4(color, 1.0);
        return;
    }

    float depth_edge = 0.0;
    float geometry_mask = 0.0;

    // Je coupe réellement les lectures voisines quand le profil désactive les détails.
    if (max(u_edge_strength, u_sharpen_strength) > 0.001) {
        float center_depth_sample = texture(u_scene_depth, v_uv).r;
        if (depth_sample_is_usable(center_depth_sample)) {
            float center_depth = linearize_depth(center_depth_sample);
            geometry_mask = 1.0 - smoothstep(120.0, 280.0, center_depth);

            if (u_edge_strength > 0.001) {
                float left_depth_sample = texture(u_scene_depth, v_uv + vec2(-texel.x, 0.0)).r;
                float right_depth_sample = texture(u_scene_depth, v_uv + vec2(texel.x, 0.0)).r;
                float down_depth_sample = texture(u_scene_depth, v_uv + vec2(0.0, -texel.y)).r;
                float up_depth_sample = texture(u_scene_depth, v_uv + vec2(0.0, texel.y)).r;

                float left_weight = depth_sample_is_usable(left_depth_sample) ? 1.0 : 0.0;
                float right_weight = depth_sample_is_usable(right_depth_sample) ? 1.0 : 0.0;
                float down_weight = depth_sample_is_usable(down_depth_sample) ? 1.0 : 0.0;
                float up_weight = depth_sample_is_usable(up_depth_sample) ? 1.0 : 0.0;
                float neighbor_count = left_weight + right_weight + down_weight + up_weight;

                if (neighbor_count > 0.0) {
                    float left_depth = linearize_depth(left_depth_sample);
                    float right_depth = linearize_depth(right_depth_sample);
                    float down_depth = linearize_depth(down_depth_sample);
                    float up_depth = linearize_depth(up_depth_sample);

                    depth_edge = abs(left_depth - center_depth) * left_weight +
                                 abs(right_depth - center_depth) * right_weight +
                                 abs(down_depth - center_depth) * down_weight +
                                 abs(up_depth - center_depth) * up_weight;
                    depth_edge = (depth_edge / neighbor_count) / max(center_depth * 0.75, 1.0);
                    depth_edge = smoothstep(0.002, 0.035, depth_edge);
                }
            }
        }
    }
    scene *= 1.0 - depth_edge * u_edge_strength * geometry_mask;

    if (u_sharpen_strength > 0.001) {
        vec3 blur = scene * 0.50;
        blur += sample_scene(v_uv + vec2(texel.x, 0.0)) * 0.125;
        blur += sample_scene(v_uv + vec2(-texel.x, 0.0)) * 0.125;
        blur += sample_scene(v_uv + vec2(0.0, texel.y)) * 0.125;
        blur += sample_scene(v_uv + vec2(0.0, -texel.y)) * 0.125;

        vec3 detail = scene - blur;
        float sharpen_mask = geometry_mask * (1.0 - depth_edge * 0.85);
        scene += detail * u_sharpen_strength * sharpen_mask;
    }

    vec3 glow = texture(u_glow_texture, v_uv).rgb * u_glow_strength;
    vec3 color = scene + glow;
    float weather_exposure = clamp(u_weather_exposure, 0.0, 1.0);
    float flash_exposure = mix(0.12, 1.0, weather_exposure);
    vec3 lightning =
        vec3(0.62, 0.72, 1.00) *
        clamp(u_lightning_intensity, 0.0, 1.0) *
        flash_exposure *
        (0.08 + clamp(u_storm_intensity, 0.0, 1.0) * 0.12);
    if (u_modern_pipeline != 0) {
        color = aces_fitted(
            max(color + lightning, vec3(0.0)) *
            max(u_exposure, 0.001));
    } else {
        // Je conserve strictement la courbe et l'ordre historiques pour que
        // LegacyVoxel reste une référence visuelle fiable de la refonte.
        color = vec3(1.0) -
                exp(-color * max(u_exposure, 0.001));
    }
    color = apply_saturation(color, u_saturation_boost);
    color = (color - 0.5) * u_contrast + 0.5;
    color = apply_palette_grade(color, clamp(u_storm_intensity, 0.0, 1.0), clamp(u_lightning_intensity, 0.0, 1.0));
    if (u_modern_pipeline == 0) {
        color += lightning;
    }

    float vignette = smoothstep(0.92, 0.22, distance(v_uv, vec2(0.5)));
    color *= mix(1.0 - u_vignette_strength, 1.0, vignette);
    color = mix(color, color + u_night_tint_color * 0.28, clamp(length(u_night_tint_color) * 2.0, 0.0, 1.0));
    color = pow(clamp(color, 0.0, 16.0), vec3(1.0 / 2.2));
    frag_color = vec4(clamp(color, 0.0, 1.0), 1.0);
}
)";

    static constexpr auto* menu_background_fragment_shader = R"(#version 330 core
in vec2 v_uv;

uniform sampler2D u_scene_texture;
uniform sampler2D u_blur_texture;
uniform float u_blur_mix;
uniform vec3 u_tint_color;
uniform float u_vignette_strength;
uniform float u_exposure;
uniform int u_modern_pipeline;

out vec4 frag_color;

vec3 aces_fitted(vec3 color) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp(
        (color * (a * color + b)) /
            (color * (c * color + d) + e),
        0.0,
        1.0);
}

void main() {
    vec3 scene = texture(u_scene_texture, v_uv).rgb;
    vec3 blurred = texture(u_blur_texture, v_uv).rgb;
    vec3 color = mix(scene, blurred, clamp(u_blur_mix, 0.0, 1.0));
    color = mix(color, color * u_tint_color, 0.22);
    float vignette = smoothstep(0.94, 0.18, distance(v_uv, vec2(0.5)));
    color *= mix(1.0 - u_vignette_strength, 1.0, vignette);

    // La prévisualisation moderne est elle aussi rendue dans une cible HDR
    // linéaire. Sans tone mapping, les hautes lumières du menu étaient
    // écrêtées avant même l'encodage gamma.
    if (u_modern_pipeline != 0) {
        color = aces_fitted(
            max(color, vec3(0.0)) *
            max(u_exposure, 0.001));
    }
    color = pow(clamp(color, 0.0, 1.0), vec3(1.0 / 2.2));
    frag_color = vec4(color, 1.0);
}
)";

    world_program_ = link_program(
        compile_shader(GL_VERTEX_SHADER, world_vertex_shader),
        compile_shader(GL_FRAGMENT_SHADER, world_fragment_shader));
    modern_terrain_program_ = link_program(
        compile_shader(
            GL_VERTEX_SHADER,
            kModernTerrainVertexShaderSource.data()),
        compile_shader(
            GL_FRAGMENT_SHADER,
            kModernTerrainFragmentShaderSource.data()));
    modern_architecture_program_ = link_program(
        compile_shader(
            GL_VERTEX_SHADER,
            kModernArchitectureVertexShaderSource.data()),
        compile_shader(
            GL_FRAGMENT_SHADER,
            kModernTerrainFragmentShaderSource.data()));
    modern_terrain_shadow_program_ = link_program(
        compile_shader(
            GL_VERTEX_SHADER,
            kModernTerrainShadowVertexShaderSource.data()),
        compile_shader(
            GL_FRAGMENT_SHADER,
            kModernTerrainShadowFragmentShaderSource.data()));
    item_drop_program_ = link_program(
        compile_shader(GL_VERTEX_SHADER, item_drop_vertex_shader),
        compile_shader(GL_FRAGMENT_SHADER, world_fragment_shader));
    precipitation_program_ = link_program(
        compile_shader(GL_VERTEX_SHADER, precipitation_vertex_shader),
        compile_shader(GL_FRAGMENT_SHADER, precipitation_fragment_shader));
    old_guard_effect_program_ = link_program(
        compile_shader(GL_VERTEX_SHADER, old_guard_effect_vertex_shader),
        compile_shader(GL_FRAGMENT_SHADER, old_guard_effect_fragment_shader));
    creature_program_ = link_program(
        compile_shader(GL_VERTEX_SHADER, creature_vertex_shader),
        compile_shader(GL_FRAGMENT_SHADER, creature_fragment_shader));
    creature_shadow_program_ = link_program(
        compile_shader(GL_VERTEX_SHADER, creature_shadow_vertex_shader),
        compile_shader(GL_FRAGMENT_SHADER, creature_shadow_fragment_shader));
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
        compile_shader(GL_VERTEX_SHADER, kSkyVertexShaderSource),
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
    menu_background_program_ = link_program(
        compile_shader(GL_VERTEX_SHADER, screen_vertex_shader),
        compile_shader(GL_FRAGMENT_SHADER, menu_background_fragment_shader));

    modern_terrain_uniforms_.model =
        glGetUniformLocation(modern_terrain_program_, "u_model");
    modern_terrain_uniforms_.view_projection =
        glGetUniformLocation(modern_terrain_program_, "u_view_projection");
    modern_terrain_uniforms_.light_view_projection =
        glGetUniformLocation(modern_terrain_program_, "u_light_view_projection");
    modern_terrain_uniforms_.light_view_projection_far =
        glGetUniformLocation(
            modern_terrain_program_,
            "u_light_view_projection_far");
    modern_terrain_uniforms_.camera_position =
        glGetUniformLocation(modern_terrain_program_, "u_camera_position");
    modern_terrain_uniforms_.camera_forward =
        glGetUniformLocation(modern_terrain_program_, "u_camera_forward");
    modern_terrain_uniforms_.sun_direction =
        glGetUniformLocation(modern_terrain_program_, "u_sun_direction");
    modern_terrain_uniforms_.sun_color =
        glGetUniformLocation(modern_terrain_program_, "u_sun_color");
    modern_terrain_uniforms_.ambient_color =
        glGetUniformLocation(modern_terrain_program_, "u_ambient_color");
    modern_terrain_uniforms_.fog_color =
        glGetUniformLocation(modern_terrain_program_, "u_fog_color");
    modern_terrain_uniforms_.distant_fog_color =
        glGetUniformLocation(modern_terrain_program_, "u_distant_fog_color");
    modern_terrain_uniforms_.night_tint_color =
        glGetUniformLocation(modern_terrain_program_, "u_night_tint_color");
    modern_terrain_uniforms_.daylight_factor =
        glGetUniformLocation(modern_terrain_program_, "u_daylight_factor");
    modern_terrain_uniforms_.sun_visibility =
        glGetUniformLocation(modern_terrain_program_, "u_sun_visibility");
    modern_terrain_uniforms_.precipitation_intensity =
        glGetUniformLocation(modern_terrain_program_, "u_precipitation_intensity");
    modern_terrain_uniforms_.storm_intensity =
        glGetUniformLocation(modern_terrain_program_, "u_storm_intensity");
    modern_terrain_uniforms_.lightning_intensity =
        glGetUniformLocation(modern_terrain_program_, "u_lightning_intensity");
    modern_terrain_uniforms_.triplanar_sharpness =
        glGetUniformLocation(modern_terrain_program_, "u_triplanar_sharpness");
    modern_terrain_uniforms_.material_detail_scale =
        glGetUniformLocation(modern_terrain_program_, "u_material_detail_scale");
    modern_terrain_uniforms_.shadows_enabled =
        glGetUniformLocation(modern_terrain_program_, "u_shadows_enabled");
    modern_terrain_uniforms_.material_albedo =
        glGetUniformLocation(modern_terrain_program_, "u_material_albedo");
    modern_terrain_uniforms_.material_normal_height =
        glGetUniformLocation(modern_terrain_program_, "u_material_normal_height");
    modern_terrain_uniforms_.material_orm_emission =
        glGetUniformLocation(modern_terrain_program_, "u_material_orm_emission");
    modern_terrain_uniforms_.shadow_map =
        glGetUniformLocation(modern_terrain_program_, "u_shadow_map");
    modern_terrain_uniforms_.shadow_map_far =
        glGetUniformLocation(modern_terrain_program_, "u_shadow_map_far");
    modern_terrain_uniforms_.shadow_cascade_count =
        glGetUniformLocation(
            modern_terrain_program_,
            "u_shadow_cascade_count");
    modern_terrain_uniforms_.shadow_split_distance =
        glGetUniformLocation(
            modern_terrain_program_,
            "u_shadow_split_distance");
    modern_terrain_uniforms_.shadow_transition_width =
        glGetUniformLocation(
            modern_terrain_program_,
            "u_shadow_transition_width");

    const auto load_modern_surface_uniforms =
        [](GLuint program, ModernTerrainUniformLocations& uniforms) {
            uniforms.model = glGetUniformLocation(program, "u_model");
            uniforms.view_projection =
                glGetUniformLocation(program, "u_view_projection");
            uniforms.light_view_projection =
                glGetUniformLocation(program, "u_light_view_projection");
            uniforms.light_view_projection_far =
                glGetUniformLocation(
                    program,
                    "u_light_view_projection_far");
            uniforms.camera_position =
                glGetUniformLocation(program, "u_camera_position");
            uniforms.camera_forward =
                glGetUniformLocation(program, "u_camera_forward");
            uniforms.sun_direction =
                glGetUniformLocation(program, "u_sun_direction");
            uniforms.sun_color =
                glGetUniformLocation(program, "u_sun_color");
            uniforms.ambient_color =
                glGetUniformLocation(program, "u_ambient_color");
            uniforms.fog_color =
                glGetUniformLocation(program, "u_fog_color");
            uniforms.distant_fog_color =
                glGetUniformLocation(program, "u_distant_fog_color");
            uniforms.night_tint_color =
                glGetUniformLocation(program, "u_night_tint_color");
            uniforms.daylight_factor =
                glGetUniformLocation(program, "u_daylight_factor");
            uniforms.sun_visibility =
                glGetUniformLocation(program, "u_sun_visibility");
            uniforms.precipitation_intensity =
                glGetUniformLocation(program, "u_precipitation_intensity");
            uniforms.storm_intensity =
                glGetUniformLocation(program, "u_storm_intensity");
            uniforms.lightning_intensity =
                glGetUniformLocation(program, "u_lightning_intensity");
            uniforms.triplanar_sharpness =
                glGetUniformLocation(program, "u_triplanar_sharpness");
            uniforms.material_detail_scale =
                glGetUniformLocation(program, "u_material_detail_scale");
            uniforms.shadows_enabled =
                glGetUniformLocation(program, "u_shadows_enabled");
            uniforms.material_albedo =
                glGetUniformLocation(program, "u_material_albedo");
            uniforms.material_normal_height =
                glGetUniformLocation(program, "u_material_normal_height");
            uniforms.material_orm_emission =
                glGetUniformLocation(program, "u_material_orm_emission");
            uniforms.shadow_map =
                glGetUniformLocation(program, "u_shadow_map");
            uniforms.shadow_map_far =
                glGetUniformLocation(program, "u_shadow_map_far");
            uniforms.shadow_cascade_count =
                glGetUniformLocation(
                    program,
                    "u_shadow_cascade_count");
            uniforms.shadow_split_distance =
                glGetUniformLocation(
                    program,
                    "u_shadow_split_distance");
            uniforms.shadow_transition_width =
                glGetUniformLocation(
                    program,
                    "u_shadow_transition_width");
        };
    load_modern_surface_uniforms(
        modern_architecture_program_,
        modern_architecture_uniforms_);
    modern_terrain_shadow_uniforms_.model =
        glGetUniformLocation(modern_terrain_shadow_program_, "u_model");
    modern_terrain_shadow_uniforms_.light_view_projection =
        glGetUniformLocation(
            modern_terrain_shadow_program_,
            "u_light_view_projection");
    modern_terrain_shadow_uniforms_.material_albedo =
        glGetUniformLocation(
            modern_terrain_shadow_program_,
            "u_material_albedo");

    const std::array<GLint, 31> modern_terrain_uniform_locations {{
        modern_terrain_uniforms_.model,
        modern_terrain_uniforms_.view_projection,
        modern_terrain_uniforms_.light_view_projection,
        modern_terrain_uniforms_.light_view_projection_far,
        modern_terrain_uniforms_.camera_position,
        modern_terrain_uniforms_.camera_forward,
        modern_terrain_uniforms_.sun_direction,
        modern_terrain_uniforms_.sun_color,
        modern_terrain_uniforms_.ambient_color,
        modern_terrain_uniforms_.fog_color,
        modern_terrain_uniforms_.distant_fog_color,
        modern_terrain_uniforms_.night_tint_color,
        modern_terrain_uniforms_.daylight_factor,
        modern_terrain_uniforms_.sun_visibility,
        modern_terrain_uniforms_.precipitation_intensity,
        modern_terrain_uniforms_.storm_intensity,
        modern_terrain_uniforms_.lightning_intensity,
        modern_terrain_uniforms_.triplanar_sharpness,
        modern_terrain_uniforms_.material_detail_scale,
        modern_terrain_uniforms_.shadows_enabled,
        modern_terrain_uniforms_.material_albedo,
        modern_terrain_uniforms_.material_normal_height,
        modern_terrain_uniforms_.material_orm_emission,
        modern_terrain_uniforms_.shadow_map,
        modern_terrain_uniforms_.shadow_map_far,
        modern_terrain_uniforms_.shadow_cascade_count,
        modern_terrain_uniforms_.shadow_split_distance,
        modern_terrain_uniforms_.shadow_transition_width,
        modern_terrain_shadow_uniforms_.model,
        modern_terrain_shadow_uniforms_.light_view_projection,
        modern_terrain_shadow_uniforms_.material_albedo,
    }};
    if (std::any_of(
            modern_terrain_uniform_locations.begin(),
            modern_terrain_uniform_locations.end(),
            [](GLint location) noexcept {
                return location < 0;
            })) {
        throw std::runtime_error(
            "Modern terrain shader is missing one or more required uniforms");
    }

    const std::array<GLint, 28> modern_architecture_uniform_locations {{
        modern_architecture_uniforms_.model,
        modern_architecture_uniforms_.view_projection,
        modern_architecture_uniforms_.light_view_projection,
        modern_architecture_uniforms_.light_view_projection_far,
        modern_architecture_uniforms_.camera_position,
        modern_architecture_uniforms_.camera_forward,
        modern_architecture_uniforms_.sun_direction,
        modern_architecture_uniforms_.sun_color,
        modern_architecture_uniforms_.ambient_color,
        modern_architecture_uniforms_.fog_color,
        modern_architecture_uniforms_.distant_fog_color,
        modern_architecture_uniforms_.night_tint_color,
        modern_architecture_uniforms_.daylight_factor,
        modern_architecture_uniforms_.sun_visibility,
        modern_architecture_uniforms_.precipitation_intensity,
        modern_architecture_uniforms_.storm_intensity,
        modern_architecture_uniforms_.lightning_intensity,
        modern_architecture_uniforms_.triplanar_sharpness,
        modern_architecture_uniforms_.material_detail_scale,
        modern_architecture_uniforms_.shadows_enabled,
        modern_architecture_uniforms_.material_albedo,
        modern_architecture_uniforms_.material_normal_height,
        modern_architecture_uniforms_.material_orm_emission,
        modern_architecture_uniforms_.shadow_map,
        modern_architecture_uniforms_.shadow_map_far,
        modern_architecture_uniforms_.shadow_cascade_count,
        modern_architecture_uniforms_.shadow_split_distance,
        modern_architecture_uniforms_.shadow_transition_width,
    }};
    if (std::any_of(
            modern_architecture_uniform_locations.begin(),
            modern_architecture_uniform_locations.end(),
            [](GLint location) noexcept {
                return location < 0;
            })) {
        throw std::runtime_error(
            "Modern architecture shader is missing one or more required uniforms");
    }

    world_uniforms_.model = glGetUniformLocation(world_program_, "u_model");
    world_uniforms_.view_projection = glGetUniformLocation(world_program_, "u_view_projection");
    world_uniforms_.light_view_projection = glGetUniformLocation(world_program_, "u_light_view_projection");
    world_uniforms_.light_view_projection_far =
        glGetUniformLocation(
            world_program_,
            "u_light_view_projection_far");
    world_uniforms_.camera_position = glGetUniformLocation(world_program_, "u_camera_position");
    world_uniforms_.camera_forward = glGetUniformLocation(world_program_, "u_camera_forward");
    world_uniforms_.sun_direction = glGetUniformLocation(world_program_, "u_sun_direction");
    world_uniforms_.sun_color = glGetUniformLocation(world_program_, "u_sun_color");
    world_uniforms_.ambient_color = glGetUniformLocation(world_program_, "u_ambient_color");
    world_uniforms_.fog_color = glGetUniformLocation(world_program_, "u_fog_color");
    world_uniforms_.distant_fog_color = glGetUniformLocation(world_program_, "u_distant_fog_color");
    world_uniforms_.horizon_glow_color = glGetUniformLocation(world_program_, "u_horizon_glow_color");
    world_uniforms_.night_tint_color = glGetUniformLocation(world_program_, "u_night_tint_color");
    world_uniforms_.daylight_factor = glGetUniformLocation(world_program_, "u_daylight_factor");
    world_uniforms_.sun_visibility = glGetUniformLocation(world_program_, "u_sun_visibility");
    world_uniforms_.time_of_day = glGetUniformLocation(world_program_, "u_time_of_day");
    world_uniforms_.cloud_intensity = glGetUniformLocation(world_program_, "u_cloud_intensity");
    world_uniforms_.cloud_shadow_strength = glGetUniformLocation(world_program_, "u_cloud_shadow_strength");
    world_uniforms_.wind_strength = glGetUniformLocation(world_program_, "u_wind_strength");
    world_uniforms_.atmospheric_scatter_strength = glGetUniformLocation(world_program_, "u_atmospheric_scatter_strength");
    world_uniforms_.height_fog_density = glGetUniformLocation(world_program_, "u_height_fog_density");
    world_uniforms_.precipitation_intensity = glGetUniformLocation(world_program_, "u_precipitation_intensity");
    world_uniforms_.storm_intensity = glGetUniformLocation(world_program_, "u_storm_intensity");
    world_uniforms_.lightning_intensity = glGetUniformLocation(world_program_, "u_lightning_intensity");
    world_uniforms_.ocean_waves =
        glGetUniformLocation(
            world_program_,
            "u_ocean_waves[0]");

    world_uniforms_.ocean_wave_phases =
        glGetUniformLocation(
            world_program_,
            "u_ocean_wave_phases[0]");

    world_uniforms_.ocean_wave_count =
        glGetUniformLocation(
            world_program_,
            "u_ocean_wave_count");

    world_uniforms_.ocean_foam_threshold =
        glGetUniformLocation(
            world_program_,
            "u_ocean_foam_threshold");

    world_uniforms_.ocean_detail_strength =
        glGetUniformLocation(
            world_program_,
            "u_ocean_detail_strength");

    world_uniforms_.ocean_detail_phase =
        glGetUniformLocation(
            world_program_,
            "u_ocean_detail_phase");

    world_uniforms_.ocean_severity =
        glGetUniformLocation(
            world_program_,
            "u_ocean_severity");

    world_uniforms_.ocean_tempest_factor =
        glGetUniformLocation(
            world_program_,
            "u_ocean_tempest_factor");

    world_uniforms_.ocean_open_sea =
        glGetUniformLocation(
            world_program_,
            "u_ocean_open_sea");

    const std::array<GLint, 9> ocean_uniform_locations {{
        world_uniforms_.ocean_waves,
        world_uniforms_.ocean_wave_phases,
        world_uniforms_.ocean_wave_count,
        world_uniforms_.ocean_foam_threshold,
        world_uniforms_.ocean_detail_strength,
        world_uniforms_.ocean_detail_phase,
        world_uniforms_.ocean_severity,
        world_uniforms_.ocean_tempest_factor,
        world_uniforms_.ocean_open_sea,
    }};
    // Je refuse une initialisation partielle : un uniform optimise ou mal
    // orthographie rendrait l'ocean visuellement incoherent sans erreur OpenGL.
    if (std::any_of(
            ocean_uniform_locations.begin(),
            ocean_uniform_locations.end(),
            [](GLint location) noexcept {
                return location < 0;
            })) {
        throw std::runtime_error(
            "Ocean shader is missing one or more required uniforms");
    }
    world_uniforms_.atlas = glGetUniformLocation(world_program_, "u_atlas");
    world_uniforms_.shadow_map = glGetUniformLocation(world_program_, "u_shadow_map");
    world_uniforms_.shadow_map_far =
        glGetUniformLocation(world_program_, "u_shadow_map_far");
    world_uniforms_.shadow_cascade_count =
        glGetUniformLocation(
            world_program_,
            "u_shadow_cascade_count");
    world_uniforms_.shadow_split_distance =
        glGetUniformLocation(
            world_program_,
            "u_shadow_split_distance");
    world_uniforms_.shadow_transition_width =
        glGetUniformLocation(
            world_program_,
            "u_shadow_transition_width");
    world_uniforms_.scene_color = glGetUniformLocation(world_program_, "u_scene_color");
    world_uniforms_.scene_depth = glGetUniformLocation(world_program_, "u_scene_depth");
    world_uniforms_.inverse_view_projection = glGetUniformLocation(world_program_, "u_inverse_view_projection");
    world_uniforms_.shadows_enabled = glGetUniformLocation(world_program_, "u_shadows_enabled");
    world_uniforms_.super_vision_strength = glGetUniformLocation(world_program_, "u_super_vision_strength");
    world_uniforms_.ship_protection_enabled =
        glGetUniformLocation(world_program_, "u_ship_protection_enabled");
    world_uniforms_.ship_inverse_model =
        glGetUniformLocation(world_program_, "u_ship_inverse_model");
    world_uniforms_.ship_bounds_min =
        glGetUniformLocation(world_program_, "u_ship_bounds_min");
    world_uniforms_.ship_bounds_max =
        glGetUniformLocation(world_program_, "u_ship_bounds_max");
    world_uniforms_.ship_profile_longitudinal =
        glGetUniformLocation(world_program_, "u_ship_profile_longitudinal");
    world_uniforms_.ship_profile_taper =
        glGetUniformLocation(world_program_, "u_ship_profile_taper");
    world_uniforms_.ship_profile_heights =
        glGetUniformLocation(world_program_, "u_ship_profile_heights");
    world_uniforms_.ship_profile_widths =
        glGetUniformLocation(world_program_, "u_ship_profile_widths");
    world_uniforms_.ship_sheltered_floor =
        glGetUniformLocation(world_program_, "u_ship_sheltered_floor");

    const std::array<GLint, 9> ship_protection_uniform_locations {{
        world_uniforms_.ship_protection_enabled,
        world_uniforms_.ship_inverse_model,
        world_uniforms_.ship_bounds_min,
        world_uniforms_.ship_bounds_max,
        world_uniforms_.ship_profile_longitudinal,
        world_uniforms_.ship_profile_taper,
        world_uniforms_.ship_profile_heights,
        world_uniforms_.ship_profile_widths,
        world_uniforms_.ship_sheltered_floor,
    }};
    if (std::any_of(
            ship_protection_uniform_locations.begin(),
            ship_protection_uniform_locations.end(),
            [](GLint location) noexcept {
                return location < 0;
            })) {
        throw std::runtime_error(
            "World shader is missing one or more ship protection uniforms");
    }

    item_drop_uniforms_.view_projection = glGetUniformLocation(item_drop_program_, "u_view_projection");
    item_drop_uniforms_.light_view_projection = glGetUniformLocation(item_drop_program_, "u_light_view_projection");
    item_drop_uniforms_.light_view_projection_far =
        glGetUniformLocation(
            item_drop_program_,
            "u_light_view_projection_far");
    item_drop_uniforms_.camera_position = glGetUniformLocation(item_drop_program_, "u_camera_position");
    item_drop_uniforms_.camera_forward = glGetUniformLocation(item_drop_program_, "u_camera_forward");
    item_drop_uniforms_.sun_direction = glGetUniformLocation(item_drop_program_, "u_sun_direction");
    item_drop_uniforms_.sun_color = glGetUniformLocation(item_drop_program_, "u_sun_color");
    item_drop_uniforms_.ambient_color = glGetUniformLocation(item_drop_program_, "u_ambient_color");
    item_drop_uniforms_.fog_color = glGetUniformLocation(item_drop_program_, "u_fog_color");
    item_drop_uniforms_.distant_fog_color = glGetUniformLocation(item_drop_program_, "u_distant_fog_color");
    item_drop_uniforms_.horizon_glow_color = glGetUniformLocation(item_drop_program_, "u_horizon_glow_color");
    item_drop_uniforms_.night_tint_color = glGetUniformLocation(item_drop_program_, "u_night_tint_color");
    item_drop_uniforms_.daylight_factor = glGetUniformLocation(item_drop_program_, "u_daylight_factor");
    item_drop_uniforms_.sun_visibility = glGetUniformLocation(item_drop_program_, "u_sun_visibility");
    item_drop_uniforms_.time_of_day = glGetUniformLocation(item_drop_program_, "u_time_of_day");
    item_drop_uniforms_.cloud_intensity = glGetUniformLocation(item_drop_program_, "u_cloud_intensity");
    item_drop_uniforms_.cloud_shadow_strength = glGetUniformLocation(item_drop_program_, "u_cloud_shadow_strength");
    item_drop_uniforms_.wind_strength = glGetUniformLocation(item_drop_program_, "u_wind_strength");
    item_drop_uniforms_.atmospheric_scatter_strength = glGetUniformLocation(item_drop_program_, "u_atmospheric_scatter_strength");
    item_drop_uniforms_.height_fog_density = glGetUniformLocation(item_drop_program_, "u_height_fog_density");
    item_drop_uniforms_.precipitation_intensity = glGetUniformLocation(item_drop_program_, "u_precipitation_intensity");
    item_drop_uniforms_.storm_intensity = glGetUniformLocation(item_drop_program_, "u_storm_intensity");
    item_drop_uniforms_.lightning_intensity = glGetUniformLocation(item_drop_program_, "u_lightning_intensity");
    item_drop_uniforms_.atlas = glGetUniformLocation(item_drop_program_, "u_atlas");
    item_drop_uniforms_.shadow_map = glGetUniformLocation(item_drop_program_, "u_shadow_map");
    item_drop_uniforms_.shadow_map_far =
        glGetUniformLocation(item_drop_program_, "u_shadow_map_far");
    item_drop_uniforms_.shadow_cascade_count =
        glGetUniformLocation(
            item_drop_program_,
            "u_shadow_cascade_count");
    item_drop_uniforms_.shadow_split_distance =
        glGetUniformLocation(
            item_drop_program_,
            "u_shadow_split_distance");
    item_drop_uniforms_.shadow_transition_width =
        glGetUniformLocation(
            item_drop_program_,
            "u_shadow_transition_width");
    item_drop_uniforms_.scene_color = glGetUniformLocation(item_drop_program_, "u_scene_color");
    item_drop_uniforms_.scene_depth = glGetUniformLocation(item_drop_program_, "u_scene_depth");
    item_drop_uniforms_.inverse_view_projection = glGetUniformLocation(item_drop_program_, "u_inverse_view_projection");
    item_drop_uniforms_.shadows_enabled = glGetUniformLocation(item_drop_program_, "u_shadows_enabled");

    precipitation_uniforms_.view_projection =
        glGetUniformLocation(precipitation_program_, "u_view_projection");
    precipitation_uniforms_.camera_position =
        glGetUniformLocation(precipitation_program_, "u_camera_position");
    precipitation_uniforms_.camera_right =
        glGetUniformLocation(precipitation_program_, "u_camera_right");
    precipitation_uniforms_.camera_up =
        glGetUniformLocation(precipitation_program_, "u_camera_up");
    precipitation_uniforms_.fog_color =
        glGetUniformLocation(precipitation_program_, "u_fog_color");
    precipitation_uniforms_.lightning_intensity =
        glGetUniformLocation(precipitation_program_, "u_lightning_intensity");
    precipitation_uniforms_.storm_intensity =
        glGetUniformLocation(precipitation_program_, "u_storm_intensity");
    precipitation_uniforms_.ship_protection_enabled =
        glGetUniformLocation(precipitation_program_, "u_ship_protection_enabled");
    precipitation_uniforms_.ship_inverse_model =
        glGetUniformLocation(precipitation_program_, "u_ship_inverse_model");
    precipitation_uniforms_.ship_bounds_min =
        glGetUniformLocation(precipitation_program_, "u_ship_bounds_min");
    precipitation_uniforms_.ship_bounds_max =
        glGetUniformLocation(precipitation_program_, "u_ship_bounds_max");
    precipitation_uniforms_.ship_profile_longitudinal =
        glGetUniformLocation(precipitation_program_, "u_ship_profile_longitudinal");
    precipitation_uniforms_.ship_profile_taper =
        glGetUniformLocation(precipitation_program_, "u_ship_profile_taper");
    precipitation_uniforms_.ship_profile_heights =
        glGetUniformLocation(precipitation_program_, "u_ship_profile_heights");
    precipitation_uniforms_.ship_profile_widths =
        glGetUniformLocation(precipitation_program_, "u_ship_profile_widths");
    precipitation_uniforms_.ship_sheltered_floor =
        glGetUniformLocation(precipitation_program_, "u_ship_sheltered_floor");

    old_guard_effect_uniforms_.view_projection =
        glGetUniformLocation(old_guard_effect_program_, "u_view_projection");
    old_guard_effect_uniforms_.camera_right =
        glGetUniformLocation(old_guard_effect_program_, "u_camera_right");
    old_guard_effect_uniforms_.camera_up =
        glGetUniformLocation(old_guard_effect_program_, "u_camera_up");

    const std::array<GLint, 16> precipitation_uniform_locations {{
        precipitation_uniforms_.view_projection,
        precipitation_uniforms_.camera_position,
        precipitation_uniforms_.camera_right,
        precipitation_uniforms_.camera_up,
        precipitation_uniforms_.fog_color,
        precipitation_uniforms_.lightning_intensity,
        precipitation_uniforms_.storm_intensity,
        precipitation_uniforms_.ship_protection_enabled,
        precipitation_uniforms_.ship_inverse_model,
        precipitation_uniforms_.ship_bounds_min,
        precipitation_uniforms_.ship_bounds_max,
        precipitation_uniforms_.ship_profile_longitudinal,
        precipitation_uniforms_.ship_profile_taper,
        precipitation_uniforms_.ship_profile_heights,
        precipitation_uniforms_.ship_profile_widths,
        precipitation_uniforms_.ship_sheltered_floor,
    }};
    if (std::any_of(
            precipitation_uniform_locations.begin(),
            precipitation_uniform_locations.end(),
            [](GLint location) noexcept {
                return location < 0;
            })) {
        throw std::runtime_error(
            "Precipitation shader is missing one or more required uniforms");
    }

    creature_uniforms_.view_projection = glGetUniformLocation(creature_program_, "u_view_projection");
    creature_uniforms_.light_view_projection = glGetUniformLocation(creature_program_, "u_light_view_projection");
    creature_uniforms_.light_view_projection_far =
        glGetUniformLocation(
            creature_program_,
            "u_light_view_projection_far");
    creature_uniforms_.camera_position = glGetUniformLocation(creature_program_, "u_camera_position");
    creature_uniforms_.camera_forward = glGetUniformLocation(creature_program_, "u_camera_forward");
    creature_uniforms_.sun_direction = glGetUniformLocation(creature_program_, "u_sun_direction");
    creature_uniforms_.sun_color = glGetUniformLocation(creature_program_, "u_sun_color");
    creature_uniforms_.ambient_color = glGetUniformLocation(creature_program_, "u_ambient_color");
    creature_uniforms_.fog_color = glGetUniformLocation(creature_program_, "u_fog_color");
    creature_uniforms_.distant_fog_color = glGetUniformLocation(creature_program_, "u_distant_fog_color");
    creature_uniforms_.horizon_glow_color = glGetUniformLocation(creature_program_, "u_horizon_glow_color");
    creature_uniforms_.night_tint_color = glGetUniformLocation(creature_program_, "u_night_tint_color");
    creature_uniforms_.daylight_factor = glGetUniformLocation(creature_program_, "u_daylight_factor");
    creature_uniforms_.sun_visibility = glGetUniformLocation(creature_program_, "u_sun_visibility");
    creature_uniforms_.cloud_intensity = glGetUniformLocation(creature_program_, "u_cloud_intensity");
    creature_uniforms_.cloud_shadow_strength = glGetUniformLocation(creature_program_, "u_cloud_shadow_strength");
    creature_uniforms_.atmospheric_scatter_strength = glGetUniformLocation(creature_program_, "u_atmospheric_scatter_strength");
    creature_uniforms_.height_fog_density = glGetUniformLocation(creature_program_, "u_height_fog_density");
    creature_uniforms_.precipitation_intensity = glGetUniformLocation(creature_program_, "u_precipitation_intensity");
    creature_uniforms_.storm_intensity = glGetUniformLocation(creature_program_, "u_storm_intensity");
    creature_uniforms_.lightning_intensity = glGetUniformLocation(creature_program_, "u_lightning_intensity");
    creature_uniforms_.atlas = glGetUniformLocation(creature_program_, "u_atlas");
    creature_uniforms_.shadow_map = glGetUniformLocation(creature_program_, "u_shadow_map");
    creature_uniforms_.shadow_map_far =
        glGetUniformLocation(creature_program_, "u_shadow_map_far");
    creature_uniforms_.shadow_cascade_count =
        glGetUniformLocation(
            creature_program_,
            "u_shadow_cascade_count");
    creature_uniforms_.shadow_split_distance =
        glGetUniformLocation(
            creature_program_,
            "u_shadow_split_distance");
    creature_uniforms_.shadow_transition_width =
        glGetUniformLocation(
            creature_program_,
            "u_shadow_transition_width");
    creature_uniforms_.shadows_enabled = glGetUniformLocation(creature_program_, "u_shadows_enabled");
    creature_uniforms_.time_of_day = glGetUniformLocation(creature_program_, "u_time_of_day");
    creature_uniforms_.player_light_strength = glGetUniformLocation(creature_program_, "u_player_light_strength");
    creature_uniforms_.super_vision_strength = glGetUniformLocation(creature_program_, "u_super_vision_strength");
    creature_uniforms_.modern_pipeline =
        glGetUniformLocation(creature_program_, "u_modern_pipeline");
    creature_shadow_light_view_projection_ =
        glGetUniformLocation(creature_shadow_program_, "u_light_view_projection");

    shadow_uniforms_.model = glGetUniformLocation(shadow_program_, "u_model");
    shadow_uniforms_.light_view_projection = glGetUniformLocation(shadow_program_, "u_light_view_projection");
    shadow_uniforms_.time_of_day = glGetUniformLocation(shadow_program_, "u_time_of_day");
    shadow_uniforms_.wind_strength = glGetUniformLocation(shadow_program_, "u_wind_strength");
    shadow_uniforms_.atlas = glGetUniformLocation(shadow_program_, "u_atlas");
    hud_uniforms_.atlas = glGetUniformLocation(hud_program_, "u_atlas");
    hud_uniforms_.font_atlas =
        glGetUniformLocation(hud_program_, "u_font_atlas");
    hud_uniforms_.model_icon_atlas =
        glGetUniformLocation(hud_program_, "u_model_icon_atlas");
    if (hud_uniforms_.atlas < 0 ||
        hud_uniforms_.font_atlas < 0 ||
        hud_uniforms_.model_icon_atlas < 0) {
        throw std::runtime_error(
            "HUD shader is missing one or more atlas uniforms");
    }
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
    sky_uniforms_.overcast_intensity = glGetUniformLocation(sky_program_, "u_overcast_intensity");
    sky_uniforms_.precipitation_intensity = glGetUniformLocation(sky_program_, "u_precipitation_intensity");
    sky_uniforms_.storm_intensity = glGetUniformLocation(sky_program_, "u_storm_intensity");
    sky_uniforms_.violent_storm_intensity =
        glGetUniformLocation(
            sky_program_,
            "u_violent_storm_intensity");
    sky_uniforms_.lightning_intensity = glGetUniformLocation(sky_program_, "u_lightning_intensity");
    sky_uniforms_.lightning_bolt_intensity =
        glGetUniformLocation(
            sky_program_,
            "u_lightning_bolt_intensity");
    sky_uniforms_.lightning_direction =
        glGetUniformLocation(
            sky_program_,
            "u_lightning_direction");
    sky_uniforms_.lightning_shape_seed =
        glGetUniformLocation(
            sky_program_,
            "u_lightning_shape_seed");
    const std::array<GLint, 4>
        violent_storm_sky_uniform_locations {{
            sky_uniforms_.violent_storm_intensity,
            sky_uniforms_.lightning_bolt_intensity,
            sky_uniforms_.lightning_direction,
            sky_uniforms_.lightning_shape_seed,
        }};
    // Je refuse de masquer une faute d'uniform avec le comportement silencieux
    // de glUniform(-1, ...), car elle désactiverait une partie de la Tempest.
    if (std::any_of(
            violent_storm_sky_uniform_locations.begin(),
            violent_storm_sky_uniform_locations.end(),
            [](GLint location) noexcept {
                return location < 0;
            })) {
        throw std::runtime_error(
            "Sky shader is missing one or more violent storm uniforms");
    }
    sky_uniforms_.weather_time = glGetUniformLocation(sky_program_, "u_weather_time");
    sky_uniforms_.cloud_steps = glGetUniformLocation(sky_program_, "u_cloud_steps");
    sky_uniforms_.cloud_detail = glGetUniformLocation(sky_program_, "u_cloud_detail");
    sky_uniforms_.accent_atlas = glGetUniformLocation(sky_program_, "u_accent_atlas");
    glow_extract_uniforms_.scene_texture = glGetUniformLocation(glow_extract_program_, "u_scene_texture");
    glow_extract_uniforms_.threshold = glGetUniformLocation(glow_extract_program_, "u_threshold");
    glow_blur_uniforms_.source_texture = glGetUniformLocation(glow_blur_program_, "u_source_texture");
    glow_blur_uniforms_.texel_direction = glGetUniformLocation(glow_blur_program_, "u_texel_direction");
    post_process_uniforms_.scene_texture = glGetUniformLocation(post_process_program_, "u_scene_texture");
    post_process_uniforms_.glow_texture = glGetUniformLocation(post_process_program_, "u_glow_texture");
    post_process_uniforms_.scene_depth = glGetUniformLocation(post_process_program_, "u_scene_depth");
    post_process_uniforms_.exposure = glGetUniformLocation(post_process_program_, "u_exposure");
    post_process_uniforms_.saturation_boost = glGetUniformLocation(post_process_program_, "u_saturation_boost");
    post_process_uniforms_.contrast = glGetUniformLocation(post_process_program_, "u_contrast");
    post_process_uniforms_.vignette_strength = glGetUniformLocation(post_process_program_, "u_vignette_strength");
    post_process_uniforms_.night_tint_color = glGetUniformLocation(post_process_program_, "u_night_tint_color");
    post_process_uniforms_.glow_strength = glGetUniformLocation(post_process_program_, "u_glow_strength");
    post_process_uniforms_.sharpen_strength = glGetUniformLocation(post_process_program_, "u_sharpen_strength");
    post_process_uniforms_.edge_strength = glGetUniformLocation(post_process_program_, "u_edge_strength");
    post_process_uniforms_.fxaa_enabled = glGetUniformLocation(post_process_program_, "u_fxaa_enabled");
    post_process_uniforms_.modern_pipeline =
        glGetUniformLocation(post_process_program_, "u_modern_pipeline");
    post_process_uniforms_.resolve_only =
        glGetUniformLocation(post_process_program_, "u_resolve_only");
    post_process_uniforms_.storm_intensity = glGetUniformLocation(post_process_program_, "u_storm_intensity");
    post_process_uniforms_.lightning_intensity = glGetUniformLocation(post_process_program_, "u_lightning_intensity");
    post_process_uniforms_.weather_exposure =
        glGetUniformLocation(post_process_program_, "u_weather_exposure");
    if (post_process_uniforms_.weather_exposure < 0 ||
        post_process_uniforms_.fxaa_enabled < 0 ||
        post_process_uniforms_.modern_pipeline < 0 ||
        post_process_uniforms_.resolve_only < 0) {
        throw std::runtime_error(
            "Post-process shader is missing a required modern uniform");
    }
    menu_background_uniforms_.scene_texture = glGetUniformLocation(menu_background_program_, "u_scene_texture");
    menu_background_uniforms_.blur_texture = glGetUniformLocation(menu_background_program_, "u_blur_texture");
    menu_background_uniforms_.blur_mix = glGetUniformLocation(menu_background_program_, "u_blur_mix");
    menu_background_uniforms_.tint_color =
        glGetUniformLocation(
            menu_background_program_,
            "u_tint_color");
    menu_background_uniforms_.vignette_strength =
        glGetUniformLocation(
            menu_background_program_,
            "u_vignette_strength");
    menu_background_uniforms_.exposure =
        glGetUniformLocation(
            menu_background_program_,
            "u_exposure");
    menu_background_uniforms_.modern_pipeline =
        glGetUniformLocation(
            menu_background_program_,
            "u_modern_pipeline");
    if (menu_background_uniforms_.scene_texture < 0 ||
        menu_background_uniforms_.blur_texture < 0 ||
        menu_background_uniforms_.blur_mix < 0 ||
        menu_background_uniforms_.tint_color < 0 ||
        menu_background_uniforms_.vignette_strength < 0 ||
        menu_background_uniforms_.exposure < 0 ||
        menu_background_uniforms_.modern_pipeline < 0) {
        throw std::runtime_error(
            "Menu background shader is missing a required uniform");
    }
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

auto Renderer::create_msdf_font_texture() -> bool {
    if (msdf_font_texture_ != 0 &&
        g_modern_hud_font_atlas.has_value()) {
        g_modern_hud_font_enabled = true;
        return true;
    }

    g_modern_hud_font_enabled = false;
    std::error_code path_error;
    const auto working_directory =
        std::filesystem::current_path(path_error);
    if (path_error) {
        last_initialization_error_ =
            "Unable to resolve the working directory for the UI font atlas";
        return false;
    }
    const std::array candidates {
        working_directory / "assets" / "fonts" /
            "valcraft_ui_font.msdfa",
        working_directory / "bin" / "assets" / "fonts" /
            "valcraft_ui_font.msdfa",
        working_directory.parent_path() / "assets" / "fonts" /
            "valcraft_ui_font.msdfa",
        working_directory.parent_path().parent_path() / "assets" /
            "fonts" / "valcraft_ui_font.msdfa",
    };

    std::optional<MsdfFontAtlas> loaded_atlas;
    for (const auto& candidate : candidates) {
        std::error_code exists_error;
        if (!std::filesystem::is_regular_file(
                candidate,
                exists_error) ||
            exists_error) {
            continue;
        }
        auto loaded = load_msdf_font_atlas_file(candidate);
        if (!loaded) {
            last_initialization_error_ =
                "Invalid modern UI font atlas '" +
                candidate.string() + "': " + loaded.error;
            return false;
        }
        loaded_atlas = std::move(loaded.atlas);
        break;
    }
    if (!loaded_atlas.has_value()) {
        last_initialization_error_ =
            "Unable to find assets/fonts/valcraft_ui_font.msdfa";
        return false;
    }

    const auto& atlas = *loaded_atlas;
    GLint maximum_texture_size = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maximum_texture_size);
    if (atlas.metadata().width >
            static_cast<std::uint32_t>(
                std::max(maximum_texture_size, 0)) ||
        atlas.metadata().height >
            static_cast<std::uint32_t>(
                std::max(maximum_texture_size, 0))) {
        last_initialization_error_ =
            "The modern UI font atlas exceeds GL_MAX_TEXTURE_SIZE";
        return false;
    }

    // Je retire une éventuelle erreur antérieure avant de contrôler
    // exclusivement les allocations de cette ressource.
    for (int error_index = 0;
         error_index < 16 && glGetError() != GL_NO_ERROR;
         ++error_index) {
    }
    glGenTextures(1, &msdf_font_texture_);
    glBindTexture(GL_TEXTURE_2D, msdf_font_texture_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexParameteri(
        GL_TEXTURE_2D,
        GL_TEXTURE_MIN_FILTER,
        GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(
        GL_TEXTURE_2D,
        GL_TEXTURE_MAX_LEVEL,
        static_cast<GLint>(atlas.metadata().mip_count - 1U));

    const auto all_pixels = atlas.pixels();
    for (std::size_t mip_index = 0U;
         mip_index < atlas.mip_levels().size();
         ++mip_index) {
        const auto& mip = atlas.mip_levels()[mip_index];
        if (mip.byte_offset > all_pixels.size() ||
            mip.byte_size > all_pixels.size() - mip.byte_offset) {
            destroy_msdf_font_texture();
            last_initialization_error_ =
                "The modern UI font mip chain is out of bounds";
            return false;
        }
        const auto pixels =
            all_pixels.subspan(mip.byte_offset, mip.byte_size);
        glTexImage2D(
            GL_TEXTURE_2D,
            static_cast<GLint>(mip_index),
            GL_RGB8,
            static_cast<GLsizei>(mip.width),
            static_cast<GLsizei>(mip.height),
            0,
            GL_RGB,
            GL_UNSIGNED_BYTE,
            pixels.data());
    }
    if (glGetError() != GL_NO_ERROR) {
        destroy_msdf_font_texture();
        last_initialization_error_ =
            "OpenGL rejected the modern UI font atlas upload";
        return false;
    }

    msdf_font_width_ = atlas.metadata().width;
    msdf_font_height_ = atlas.metadata().height;
    msdf_font_mips_ = atlas.metadata().mip_count;
    g_modern_hud_font_atlas = std::move(*loaded_atlas);
    g_modern_hud_font_enabled = true;
    glBindTexture(GL_TEXTURE_2D, 0);
    return true;
}

void Renderer::destroy_msdf_font_texture() {
    if (msdf_font_texture_ != 0) {
        glDeleteTextures(1, &msdf_font_texture_);
    }
    msdf_font_texture_ = 0;
    msdf_font_width_ = 0U;
    msdf_font_height_ = 0U;
    msdf_font_mips_ = 0U;
    g_modern_hud_font_enabled = false;
    g_modern_hud_font_atlas.reset();
}

auto Renderer::create_model_icon_texture() -> bool {
    if (model_icon_texture_ != 0 && model_icon_layers_ > 0U) {
        return true;
    }

    std::error_code path_error;
    const auto working_directory =
        std::filesystem::current_path(path_error);
    if (path_error) {
        last_initialization_error_ =
            "Unable to resolve the working directory for the model icon atlas";
        return false;
    }
    const std::array candidates {
        working_directory / "assets" / "visual" /
            "valcraft_model_icons.vmia",
        working_directory / "bin" / "assets" / "visual" /
            "valcraft_model_icons.vmia",
        working_directory.parent_path() / "assets" / "visual" /
            "valcraft_model_icons.vmia",
        working_directory.parent_path().parent_path() / "assets" /
            "visual" / "valcraft_model_icons.vmia",
    };

    std::optional<ModelIconAtlas> loaded_atlas;
    for (const auto& candidate : candidates) {
        std::error_code exists_error;
        if (!std::filesystem::is_regular_file(
                candidate,
                exists_error) ||
            exists_error) {
            continue;
        }
        auto loaded = load_model_icon_atlas(candidate);
        if (!loaded || !loaded.atlas.has_value()) {
            last_initialization_error_ =
                "Invalid modern model icon atlas '" +
                candidate.string() + "': " + loaded.message;
            return false;
        }
        loaded_atlas = std::move(loaded.atlas);
        break;
    }
    if (!loaded_atlas.has_value()) {
        last_initialization_error_ =
            "Unable to find assets/visual/valcraft_model_icons.vmia";
        return false;
    }

    const auto& atlas = *loaded_atlas;
    GLint maximum_texture_size = 0;
    GLint maximum_array_layers = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maximum_texture_size);
    glGetIntegerv(
        GL_MAX_ARRAY_TEXTURE_LAYERS,
        &maximum_array_layers);
    if (atlas.metadata.width >
            static_cast<std::uint16_t>(
                std::max(maximum_texture_size, 0)) ||
        atlas.metadata.height >
            static_cast<std::uint16_t>(
                std::max(maximum_texture_size, 0)) ||
        atlas.layers.size() >
            static_cast<std::size_t>(
                std::max(maximum_array_layers, 0))) {
        last_initialization_error_ =
            "The modern model icon atlas exceeds the OpenGL 3.3 limits";
        return false;
    }

    for (int error_index = 0;
         error_index < 16 && glGetError() != GL_NO_ERROR;
         ++error_index) {
    }
    glGenTextures(1, &model_icon_texture_);
    glBindTexture(GL_TEXTURE_2D_ARRAY, model_icon_texture_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexParameteri(
        GL_TEXTURE_2D_ARRAY,
        GL_TEXTURE_MIN_FILTER,
        GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(
        GL_TEXTURE_2D_ARRAY,
        GL_TEXTURE_MAG_FILTER,
        GL_LINEAR);
    glTexParameteri(
        GL_TEXTURE_2D_ARRAY,
        GL_TEXTURE_WRAP_S,
        GL_CLAMP_TO_EDGE);
    glTexParameteri(
        GL_TEXTURE_2D_ARRAY,
        GL_TEXTURE_WRAP_T,
        GL_CLAMP_TO_EDGE);
    glTexParameteri(
        GL_TEXTURE_2D_ARRAY,
        GL_TEXTURE_MAX_LEVEL,
        static_cast<GLint>(atlas.metadata.mip_count - 1U));

    for (std::size_t mip_index = 0U;
         mip_index < atlas.mip_levels.size();
         ++mip_index) {
        const auto& mip = atlas.mip_levels[mip_index];
        glTexImage3D(
            GL_TEXTURE_2D_ARRAY,
            static_cast<GLint>(mip_index),
            GL_SRGB8_ALPHA8,
            static_cast<GLsizei>(mip.width),
            static_cast<GLsizei>(mip.height),
            static_cast<GLsizei>(atlas.layers.size()),
            0,
            GL_RGBA,
            GL_UNSIGNED_BYTE,
            nullptr);
        for (std::size_t layer_index = 0U;
             layer_index < atlas.layers.size();
             ++layer_index) {
            const auto pixels = atlas.texels_for(
                atlas.layers[layer_index].item_id,
                static_cast<std::uint16_t>(mip_index));
            if (pixels.size() != mip.byte_count) {
                destroy_model_icon_texture();
                last_initialization_error_ =
                    "The modern model icon mip chain is out of bounds";
                return false;
            }
            glTexSubImage3D(
                GL_TEXTURE_2D_ARRAY,
                static_cast<GLint>(mip_index),
                0,
                0,
                static_cast<GLint>(layer_index),
                static_cast<GLsizei>(mip.width),
                static_cast<GLsizei>(mip.height),
                1,
                GL_RGBA,
                GL_UNSIGNED_BYTE,
                pixels.data());
        }
    }
    if (glGetError() != GL_NO_ERROR) {
        destroy_model_icon_texture();
        last_initialization_error_ =
            "OpenGL rejected the modern model icon atlas upload";
        return false;
    }

    model_icon_layer_by_block_.fill(0U);
    for (std::size_t block_index = 0U;
         block_index < model_icon_layer_by_block_.size();
         ++block_index) {
        const auto layer_index = visual_item_layer_index(
            static_cast<BlockId>(block_index));
        if (layer_index < atlas.layers.size()) {
            model_icon_layer_by_block_[block_index] =
                static_cast<std::uint16_t>(layer_index + 1U);
        }
    }
    model_icon_width_ = atlas.metadata.width;
    model_icon_height_ = atlas.metadata.height;
    model_icon_layers_ =
        static_cast<std::uint16_t>(atlas.layers.size());
    model_icon_mips_ = atlas.metadata.mip_count;
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
    return true;
}

void Renderer::destroy_model_icon_texture() {
    if (model_icon_texture_ != 0) {
        glDeleteTextures(1, &model_icon_texture_);
    }
    model_icon_texture_ = 0;
    model_icon_width_ = 0U;
    model_icon_height_ = 0U;
    model_icon_layers_ = 0U;
    model_icon_mips_ = 0U;
    model_icon_layer_by_block_.fill(0U);
}

auto Renderer::hud_item_texture_mode(
    BlockId block_id) const noexcept -> float {
    const auto layer =
        model_icon_layer_by_block_[static_cast<std::size_t>(block_id)];
    if (options_.visual_pipeline != VisualPipeline::ModernStylized ||
        model_icon_texture_ == 0 ||
        layer == 0U) {
        return 1.0F;
    }
    return 3.0F + static_cast<float>(layer - 1U);
}

void Renderer::bind_hud_textures() {
    glUniform1i(hud_uniforms_.atlas, 0);
    glUniform1i(hud_uniforms_.font_atlas, 1);
    glUniform1i(hud_uniforms_.model_icon_atlas, 2);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, atlas_texture_);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, msdf_font_texture_);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D_ARRAY, model_icon_texture_);
    glActiveTexture(GL_TEXTURE0);
}

auto Renderer::create_modern_material_textures() -> bool {
    if (modern_material_albedo_texture_ != 0 &&
        modern_material_normal_height_texture_ != 0 &&
        modern_material_orm_emission_texture_ != 0) {
        return true;
    }

    std::error_code path_error;
    const auto working_directory = std::filesystem::current_path(path_error);
    if (path_error) {
        return false;
    }
    const std::array candidates {
        working_directory / "assets" / "visual" /
            "valcraft_visual_materials.vmp",
        working_directory / "bin" / "assets" / "visual" /
            "valcraft_visual_materials.vmp",
        working_directory.parent_path() / "assets" / "visual" /
            "valcraft_visual_materials.vmp",
        working_directory.parent_path().parent_path() / "assets" / "visual" /
            "valcraft_visual_materials.vmp",
    };

    const VisualMaterialPack* selected_pack = nullptr;
    VisualMaterialPackLoadResult load_result {};
    for (const auto& candidate : candidates) {
        std::error_code exists_error;
        if (!std::filesystem::is_regular_file(candidate, exists_error) ||
            exists_error) {
            continue;
        }
        load_result = load_visual_material_pack(candidate);
        if (!load_result) {
            return false;
        }
        selected_pack = &*load_result.pack;
        break;
    }
    if (selected_pack == nullptr || selected_pack->layers.empty()) {
        return false;
    }

    const auto& pack = *selected_pack;
    GLint maximum_layers = 0;
    GLint maximum_texture_size = 0;
    glGetIntegerv(GL_MAX_ARRAY_TEXTURE_LAYERS, &maximum_layers);
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maximum_texture_size);
    if (pack.layers.size() > static_cast<std::size_t>(
                                 std::max(maximum_layers, 0)) ||
        pack.width > static_cast<std::uint16_t>(
                         std::max(maximum_texture_size, 0)) ||
        pack.height > static_cast<std::uint16_t>(
                          std::max(maximum_texture_size, 0))) {
        return false;
    }

    const auto anisotropy_supported =
        supports_gl_extension("GL_EXT_texture_filter_anisotropic") ||
        supports_gl_extension("GL_ARB_texture_filter_anisotropic");
    GLfloat maximum_anisotropy = 1.0F;
    if (anisotropy_supported) {
        glGetFloatv(kMaxTextureMaxAnisotropyExt, &maximum_anisotropy);
    }

    const auto upload_array =
        [&pack, anisotropy_supported, maximum_anisotropy](
            GLuint& texture,
            VisualMaterialTexture material_texture,
            GLint internal_format) -> bool {
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D_ARRAY, texture);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexParameteri(
            GL_TEXTURE_2D_ARRAY,
            GL_TEXTURE_MIN_FILTER,
            GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(
            GL_TEXTURE_2D_ARRAY,
            GL_TEXTURE_MAG_FILTER,
            GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(
            GL_TEXTURE_2D_ARRAY,
            GL_TEXTURE_MAX_LEVEL,
            static_cast<GLint>(pack.mip_count - 1U));
        if (anisotropy_supported) {
            glTexParameterf(
                GL_TEXTURE_2D_ARRAY,
                kTextureMaxAnisotropyExt,
                std::min(maximum_anisotropy, 8.0F));
        }

        auto mip_width = pack.width;
        auto mip_height = pack.height;
        for (std::uint16_t mip = 0U; mip < pack.mip_count; ++mip) {
            glTexImage3D(
                GL_TEXTURE_2D_ARRAY,
                static_cast<GLint>(mip),
                internal_format,
                static_cast<GLsizei>(mip_width),
                static_cast<GLsizei>(mip_height),
                static_cast<GLsizei>(pack.layers.size()),
                0,
                GL_RGBA,
                GL_UNSIGNED_BYTE,
                nullptr);
            for (std::size_t layer = 0U; layer < pack.layers.size(); ++layer) {
                const auto texels = pack.texels_for(
                    pack.layers[layer].material_id,
                    material_texture,
                    mip);
                const auto expected_size =
                    static_cast<std::size_t>(mip_width) *
                    static_cast<std::size_t>(mip_height) *
                    kVisualMaterialPackChannelCount;
                if (texels.size() != expected_size) {
                    return false;
                }
                glTexSubImage3D(
                    GL_TEXTURE_2D_ARRAY,
                    static_cast<GLint>(mip),
                    0,
                    0,
                    static_cast<GLint>(layer),
                    static_cast<GLsizei>(mip_width),
                    static_cast<GLsizei>(mip_height),
                    1,
                    GL_RGBA,
                    GL_UNSIGNED_BYTE,
                    texels.data());
            }
            mip_width = std::max<std::uint16_t>(mip_width / 2U, 1U);
            mip_height = std::max<std::uint16_t>(mip_height / 2U, 1U);
        }
        return glGetError() == GL_NO_ERROR;
    };

    if (!upload_array(
            modern_material_albedo_texture_,
            VisualMaterialTexture::Albedo,
            GL_SRGB8_ALPHA8) ||
        !upload_array(
            modern_material_normal_height_texture_,
            VisualMaterialTexture::NormalHeight,
            GL_RGBA8) ||
        !upload_array(
            modern_material_orm_emission_texture_,
            VisualMaterialTexture::OrmEmission,
            GL_RGBA8)) {
        destroy_modern_material_textures();
        return false;
    }

    material_pack_version_ = pack.format_version;
    material_pack_checksum_ = pack.content_checksum;
    material_pack_width_ = pack.width;
    material_pack_height_ = pack.height;
    material_pack_layers_ =
        static_cast<std::uint16_t>(pack.layers.size());
    material_pack_mips_ = pack.mip_count;
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
    return true;
}

void Renderer::destroy_modern_material_textures() {
    if (modern_material_orm_emission_texture_ != 0) {
        glDeleteTextures(1, &modern_material_orm_emission_texture_);
        modern_material_orm_emission_texture_ = 0;
    }
    if (modern_material_normal_height_texture_ != 0) {
        glDeleteTextures(1, &modern_material_normal_height_texture_);
        modern_material_normal_height_texture_ = 0;
    }
    if (modern_material_albedo_texture_ != 0) {
        glDeleteTextures(1, &modern_material_albedo_texture_);
        modern_material_albedo_texture_ = 0;
    }
    material_pack_checksum_ = 0U;
    material_pack_version_ = 0U;
    material_pack_width_ = 0U;
    material_pack_height_ = 0U;
    material_pack_layers_ = 0U;
    material_pack_mips_ = 0U;
}

void Renderer::create_accent_texture() {
    const auto pixels = build_accent_atlas_pixels();

    glGenTextures(1, &accent_texture_);
    glBindTexture(GL_TEXTURE_2D, accent_texture_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kAccentAtlasSize, kAccentAtlasSize, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

void Renderer::create_creature_atlas_texture() {
    const auto pixels = build_creature_atlas_pixels();
    const auto color_format =
        is_modern_visual_pipeline(options_.visual_pipeline)
            ? GL_SRGB8_ALPHA8
            : GL_RGBA8;

    glGenTextures(1, &creature_atlas_texture_);
    glBindTexture(GL_TEXTURE_2D, creature_atlas_texture_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        color_format,
        kCreatureAtlasSize,
        kCreatureAtlasSize,
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        pixels.data());
    const auto filter =
        is_modern_visual_pipeline(options_.visual_pipeline)
            ? GL_LINEAR
            : GL_NEAREST;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

void Renderer::create_player_atlas_texture() {
    const auto pixels = build_player_atlas_pixels();
    const auto color_format =
        is_modern_visual_pipeline(options_.visual_pipeline)
            ? GL_SRGB8_ALPHA8
            : GL_RGBA8;

    glGenTextures(1, &player_atlas_texture_);
    glBindTexture(GL_TEXTURE_2D, player_atlas_texture_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        color_format,
        kPlayerAtlasSize,
        kPlayerAtlasSize,
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        pixels.data());
    const auto filter =
        is_modern_visual_pipeline(options_.visual_pipeline)
            ? GL_LINEAR
            : GL_NEAREST;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

void Renderer::create_shadow_map() {
    const std::array<float, 4> border_color {{1.0F, 1.0F, 1.0F, 1.0F}};
    const auto initialize_depth_texture =
        [&](GLuint& texture) {
            glGenTextures(1, &texture);
            glBindTexture(GL_TEXTURE_2D, texture);
            if (!options_.shadows_enabled) {
                const float depth_value = 1.0F;
                glTexImage2D(
                    GL_TEXTURE_2D,
                    0,
                    GL_DEPTH_COMPONENT24,
                    1,
                    1,
                    0,
                    GL_DEPTH_COMPONENT,
                    GL_FLOAT,
                    &depth_value);
                glTexParameteri(
                    GL_TEXTURE_2D,
                    GL_TEXTURE_MIN_FILTER,
                    GL_NEAREST);
                glTexParameteri(
                    GL_TEXTURE_2D,
                    GL_TEXTURE_MAG_FILTER,
                    GL_NEAREST);
                glTexParameteri(
                    GL_TEXTURE_2D,
                    GL_TEXTURE_WRAP_S,
                    GL_CLAMP_TO_EDGE);
                glTexParameteri(
                    GL_TEXTURE_2D,
                    GL_TEXTURE_WRAP_T,
                    GL_CLAMP_TO_EDGE);
                return;
            }

            const auto shadow_map_size =
                std::max(options_.shadow_map_size, 1);
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
            glTexParameteri(
                GL_TEXTURE_2D,
                GL_TEXTURE_MIN_FILTER,
                GL_LINEAR);
            glTexParameteri(
                GL_TEXTURE_2D,
                GL_TEXTURE_MAG_FILTER,
                GL_LINEAR);
            glTexParameteri(
                GL_TEXTURE_2D,
                GL_TEXTURE_WRAP_S,
                GL_CLAMP_TO_BORDER);
            glTexParameteri(
                GL_TEXTURE_2D,
                GL_TEXTURE_WRAP_T,
                GL_CLAMP_TO_BORDER);
            glTexParameterfv(
                GL_TEXTURE_2D,
                GL_TEXTURE_BORDER_COLOR,
                border_color.data());
        };
    initialize_depth_texture(shadow_map_);
    initialize_depth_texture(shadow_map_far_);

    if (!options_.shadows_enabled) {
        return;
    }

    const auto initialize_framebuffer =
        [](GLuint texture,
           GLuint& framebuffer,
           const char* label) {
            glGenFramebuffers(1, &framebuffer);
            glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
            glFramebufferTexture2D(
                GL_FRAMEBUFFER,
                GL_DEPTH_ATTACHMENT,
                GL_TEXTURE_2D,
                texture,
                0);
            glDrawBuffer(GL_NONE);
            glReadBuffer(GL_NONE);
            if (glCheckFramebufferStatus(GL_FRAMEBUFFER) !=
                GL_FRAMEBUFFER_COMPLETE) {
                throw std::runtime_error(
                    std::string {label} +
                    " shadow framebuffer is incomplete");
            }
        };
    initialize_framebuffer(
        shadow_map_,
        shadow_framebuffer_,
        "Near cascade");
    initialize_framebuffer(
        shadow_map_far_,
        shadow_framebuffer_far_,
        "Far cascade");
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Renderer::destroy_shadow_map() {
    if (shadow_framebuffer_far_ != 0) {
        glDeleteFramebuffers(1, &shadow_framebuffer_far_);
        shadow_framebuffer_far_ = 0;
    }
    if (shadow_framebuffer_ != 0) {
        glDeleteFramebuffers(1, &shadow_framebuffer_);
        shadow_framebuffer_ = 0;
    }
    if (shadow_map_far_ != 0) {
        glDeleteTextures(1, &shadow_map_far_);
        shadow_map_far_ = 0;
    }
    if (shadow_map_ != 0) {
        glDeleteTextures(1, &shadow_map_);
        shadow_map_ = 0;
    }
}

void Renderer::create_scene_sampler_fallback_textures() {
    glGenTextures(1, &scene_fallback_color_texture_);
    glBindTexture(GL_TEXTURE_2D, scene_fallback_color_texture_);
    const std::array<std::uint8_t, 4> fallback_color {{0U, 0U, 0U, 255U}};
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, fallback_color.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenTextures(1, &scene_fallback_depth_texture_);
    glBindTexture(GL_TEXTURE_2D, scene_fallback_depth_texture_);
    const float fallback_depth = 1.0F;
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, 1, 1, 0, GL_DEPTH_COMPONENT, GL_FLOAT, &fallback_depth);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

void Renderer::create_creature_geometry() {
    glGenBuffers(1, &creature_vbo_);
    glGenBuffers(1, &creature_ebo_);
    glBindBuffer(GL_ARRAY_BUFFER, creature_vbo_);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, creature_ebo_);
    if (options_.visual_pipeline == VisualPipeline::ModernStylized) {
        std::vector<StylizedPrimitiveVertex> template_vertices {};
        std::vector<std::uint32_t> template_indices {};
        const auto& primitive_cache = visual_entity_primitive_cache();
        for (std::size_t slot = 0U;
             slot < visual_entity_draw_ranges_.size();
             ++slot) {
            const auto primitive = visual_entity_primitive_for_slot(slot);
            const auto lod = visual_entity_lod_for_slot(slot);
            const auto& template_mesh =
                primitive_cache.mesh(primitive, lod);
            const auto base_vertex =
                static_cast<std::uint32_t>(template_vertices.size());
            auto& range = visual_entity_draw_ranges_[slot];
            range.first_index = template_indices.size();
            range.index_count =
                static_cast<GLsizei>(template_mesh.indices.size());
            range.primitive = primitive;
            range.lod = lod;

            template_vertices.insert(
                template_vertices.end(),
                template_mesh.vertices.begin(),
                template_mesh.vertices.end());
            template_indices.reserve(
                template_indices.size() + template_mesh.indices.size());
            for (const auto index : template_mesh.indices) {
                template_indices.push_back(base_vertex + index);
            }
        }
        creature_template_vertex_buffer_bytes_ = static_cast<GLsizeiptr>(
            template_vertices.size() * sizeof(StylizedPrimitiveVertex));
        creature_template_index_buffer_bytes_ = static_cast<GLsizeiptr>(
            template_indices.size() * sizeof(std::uint32_t));
        creature_template_index_count_ =
            static_cast<GLsizei>(template_indices.size());
        glBufferData(
            GL_ARRAY_BUFFER,
            creature_template_vertex_buffer_bytes_,
            template_vertices.data(),
            GL_STATIC_DRAW);
        glBufferData(
            GL_ELEMENT_ARRAY_BUFFER,
            creature_template_index_buffer_bytes_,
            template_indices.data(),
            GL_STATIC_DRAW);
    } else {
        visual_entity_draw_ranges_ = {};
        const auto& template_vertices = box_template_vertices();
        const auto& template_indices = box_template_indices();
        creature_template_vertex_buffer_bytes_ = static_cast<GLsizeiptr>(
            template_vertices.size() * sizeof(BoxTemplateVertex));
        creature_template_index_buffer_bytes_ = static_cast<GLsizeiptr>(
            template_indices.size() * sizeof(std::uint32_t));
        creature_template_index_count_ =
            static_cast<GLsizei>(template_indices.size());
        glBufferData(
            GL_ARRAY_BUFFER,
            creature_template_vertex_buffer_bytes_,
            template_vertices.data(),
            GL_STATIC_DRAW);
        glBufferData(
            GL_ELEMENT_ARRAY_BUFFER,
            creature_template_index_buffer_bytes_,
            template_indices.data(),
            GL_STATIC_DRAW);
    }

    glGenVertexArrays(1, &creature_vao_);
    glGenBuffers(1, &creature_instance_vbo_);
    configure_box_template_attributes(creature_vao_, creature_vbo_, creature_ebo_);
    glBindBuffer(GL_ARRAY_BUFFER, creature_instance_vbo_);
    glBufferData(GL_ARRAY_BUFFER, kInitialCreatureInstanceBufferBytes, nullptr, GL_STREAM_DRAW);
    configure_creature_instance_attributes(creature_vao_, creature_instance_vbo_);

    glGenVertexArrays(1, &viewmodel_vao_);
    glGenBuffers(1, &viewmodel_instance_vbo_);
    configure_box_template_attributes(viewmodel_vao_, creature_vbo_, creature_ebo_);
    glBindBuffer(GL_ARRAY_BUFFER, viewmodel_instance_vbo_);
    glBufferData(GL_ARRAY_BUFFER, kInitialCreatureInstanceBufferBytes, nullptr, GL_STREAM_DRAW);
    configure_creature_instance_attributes(viewmodel_vao_, viewmodel_instance_vbo_);

    creature_instance_buffer_bytes_ = kInitialCreatureInstanceBufferBytes;
    viewmodel_instance_buffer_bytes_ = kInitialCreatureInstanceBufferBytes;
}

void Renderer::create_item_drop_geometry() {
    glGenBuffers(1, &item_drop_vbo_);
    glGenBuffers(1, &item_drop_ebo_);
    glBindBuffer(GL_ARRAY_BUFFER, item_drop_vbo_);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, item_drop_ebo_);
    if (item_drop_uses_rounded_template(options_.visual_pipeline)) {
        const auto template_mesh =
            build_stylized_rounded_box(StylizedPrimitiveLod::Low);
        item_drop_template_vertex_buffer_bytes_ = static_cast<GLsizeiptr>(
            template_mesh.vertices.size() * sizeof(StylizedPrimitiveVertex));
        item_drop_template_index_buffer_bytes_ = static_cast<GLsizeiptr>(
            template_mesh.indices.size() * sizeof(std::uint32_t));
        item_drop_template_index_count_ =
            static_cast<GLsizei>(template_mesh.indices.size());
        glBufferData(
            GL_ARRAY_BUFFER,
            item_drop_template_vertex_buffer_bytes_,
            template_mesh.vertices.data(),
            GL_STATIC_DRAW);
        glBufferData(
            GL_ELEMENT_ARRAY_BUFFER,
            item_drop_template_index_buffer_bytes_,
            template_mesh.indices.data(),
            GL_STATIC_DRAW);
    } else {
        const auto& template_vertices = box_template_vertices();
        const auto& template_indices = box_template_indices();
        item_drop_template_vertex_buffer_bytes_ = static_cast<GLsizeiptr>(
            template_vertices.size() * sizeof(BoxTemplateVertex));
        item_drop_template_index_buffer_bytes_ = static_cast<GLsizeiptr>(
            template_indices.size() * sizeof(std::uint32_t));
        item_drop_template_index_count_ =
            static_cast<GLsizei>(template_indices.size());
        glBufferData(
            GL_ARRAY_BUFFER,
            item_drop_template_vertex_buffer_bytes_,
            template_vertices.data(),
            GL_STATIC_DRAW);
        glBufferData(
            GL_ELEMENT_ARRAY_BUFFER,
            item_drop_template_index_buffer_bytes_,
            template_indices.data(),
            GL_STATIC_DRAW);
    }

    glGenVertexArrays(1, &item_drop_vao_);
    glGenBuffers(1, &item_drop_instance_vbo_);
    configure_box_template_attributes(item_drop_vao_, item_drop_vbo_, item_drop_ebo_);
    glBindBuffer(GL_ARRAY_BUFFER, item_drop_instance_vbo_);
    glBufferData(GL_ARRAY_BUFFER, kInitialItemDropInstanceBufferBytes, nullptr, GL_STREAM_DRAW);
    configure_item_drop_instance_attributes(item_drop_vao_, item_drop_instance_vbo_);

    item_drop_instance_buffer_bytes_ = kInitialItemDropInstanceBufferBytes;
}

void Renderer::create_precipitation_geometry() {
    constexpr std::array<float, 8> kQuadVertices {{
        -0.5F, 0.0F,
         0.5F, 0.0F,
        -0.5F, 1.0F,
         0.5F, 1.0F,
    }};

    glGenVertexArrays(1, &precipitation_vao_);
    glGenBuffers(1, &precipitation_vbo_);
    glGenBuffers(1, &precipitation_instance_vbo_);

    glBindVertexArray(precipitation_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, precipitation_vbo_);
    glBufferData(
        GL_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(kQuadVertices.size() * sizeof(float)),
        kQuadVertices.data(),
        GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(
        0,
        2,
        GL_FLOAT,
        GL_FALSE,
        static_cast<GLsizei>(sizeof(float) * 2U),
        nullptr);

    glBindBuffer(GL_ARRAY_BUFFER, precipitation_instance_vbo_);
    glBufferData(
        GL_ARRAY_BUFFER,
        kInitialPrecipitationInstanceBufferBytes,
        nullptr,
        GL_STREAM_DRAW);

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(
        1,
        4,
        GL_FLOAT,
        GL_FALSE,
        static_cast<GLsizei>(sizeof(PrecipitationGpuInstance)),
        reinterpret_cast<void*>(offsetof(PrecipitationGpuInstance, position_length)));
    glVertexAttribDivisor(1, 1);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(
        2,
        4,
        GL_FLOAT,
        GL_FALSE,
        static_cast<GLsizei>(sizeof(PrecipitationGpuInstance)),
        reinterpret_cast<void*>(offsetof(PrecipitationGpuInstance, velocity_width)));
    glVertexAttribDivisor(2, 1);
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(
        3,
        4,
        GL_FLOAT,
        GL_FALSE,
        static_cast<GLsizei>(sizeof(PrecipitationGpuInstance)),
        reinterpret_cast<void*>(offsetof(PrecipitationGpuInstance, appearance)));
    glVertexAttribDivisor(3, 1);

    precipitation_instance_buffer_bytes_ =
        kInitialPrecipitationInstanceBufferBytes;
}

void Renderer::create_old_guard_effect_geometry() {
    constexpr std::array<float, 8> kQuadVertices {{
        -0.5F, -0.5F,
         0.5F, -0.5F,
        -0.5F,  0.5F,
         0.5F,  0.5F,
    }};

    glGenVertexArrays(1, &old_guard_effect_vao_);
    glGenBuffers(1, &old_guard_effect_vbo_);
    glGenBuffers(1, &old_guard_effect_instance_vbo_);

    glBindVertexArray(old_guard_effect_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, old_guard_effect_vbo_);
    glBufferData(
        GL_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(kQuadVertices.size() * sizeof(float)),
        kQuadVertices.data(),
        GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(
        0,
        2,
        GL_FLOAT,
        GL_FALSE,
        static_cast<GLsizei>(sizeof(float) * 2U),
        nullptr);

    glBindBuffer(GL_ARRAY_BUFFER, old_guard_effect_instance_vbo_);
    glBufferData(
        GL_ARRAY_BUFFER,
        kInitialOldGuardEffectInstanceBufferBytes,
        nullptr,
        GL_STREAM_DRAW);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(
        1,
        4,
        GL_FLOAT,
        GL_FALSE,
        static_cast<GLsizei>(sizeof(OldGuardEffectGpuInstance)),
        reinterpret_cast<void*>(offsetof(OldGuardEffectGpuInstance, position_size)));
    glVertexAttribDivisor(1, 1);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(
        2,
        4,
        GL_FLOAT,
        GL_FALSE,
        static_cast<GLsizei>(sizeof(OldGuardEffectGpuInstance)),
        reinterpret_cast<void*>(offsetof(OldGuardEffectGpuInstance, appearance)));
    glVertexAttribDivisor(2, 1);

    old_guard_effect_instance_buffer_bytes_ =
        kInitialOldGuardEffectInstanceBufferBytes;
}

void Renderer::create_screen_quad_geometry() {
    glGenVertexArrays(1, &screen_quad_vao_);
}

void Renderer::create_hud_geometry() {
    glGenVertexArrays(1, &hud_vao_);
    glGenBuffers(1, &hud_vbo_);
    glBindVertexArray(hud_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, hud_vbo_);
    glBufferData(GL_ARRAY_BUFFER, kInitialHudBufferBytes, nullptr, GL_STREAM_DRAW);
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
}

void Renderer::upload_hud_vertices(std::span<const HudVertex> vertices) {
    glBindVertexArray(hud_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, hud_vbo_);
    ensure_hud_buffer_capacity(vertices.size());
    orphan_bound_buffer(GL_ARRAY_BUFFER, hud_vertex_buffer_bytes_);
    const auto upload_bytes = static_cast<GLsizeiptr>(vertices.size() * sizeof(HudVertex));
    glBufferSubData(
        GL_ARRAY_BUFFER,
        0,
        upload_bytes,
        vertices.data());
    frame_uploaded_bytes_ += static_cast<std::uint64_t>(std::max<GLsizeiptr>(upload_bytes, 0));
    record_triangle_draw(static_cast<GLsizei>(vertices.size()));
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

void Renderer::ensure_water_scene_targets(int width, int height) {
    const auto target_width = std::max(width, 1);
    const auto target_height = std::max(height, 1);
    const auto quality_settings = active_quality_settings_;
    const auto color_format = color_target_format(quality_settings);
    const auto targets_match =
        water_scene_framebuffer_ != 0 && water_scene_color_texture_ != 0 && water_scene_depth_texture_ != 0 &&
        water_scene_target_width_ == target_width && water_scene_target_height_ == target_height &&
        water_scene_color_internal_format_ == color_format.internal_format;
    if (targets_match) {
        return;
    }

    destroy_water_scene_targets();

    glGenFramebuffers(1, &water_scene_framebuffer_);
    glBindFramebuffer(GL_FRAMEBUFFER, water_scene_framebuffer_);

    glGenTextures(1, &water_scene_color_texture_);
    glBindTexture(GL_TEXTURE_2D, water_scene_color_texture_);
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        color_format.internal_format,
        target_width,
        target_height,
        0,
        color_format.pixel_format,
        color_format.pixel_type,
        nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, water_scene_color_texture_, 0);

    glGenTextures(1, &water_scene_depth_texture_);
    glBindTexture(GL_TEXTURE_2D, water_scene_depth_texture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, target_width, target_height, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, water_scene_depth_texture_, 0);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        throw std::runtime_error("Water scene framebuffer is incomplete");
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    water_scene_target_width_ = target_width;
    water_scene_target_height_ = target_height;
    water_scene_color_internal_format_ = color_format.internal_format;
}

void Renderer::destroy_water_scene_targets() {
    if (water_scene_depth_texture_ != 0) {
        glDeleteTextures(1, &water_scene_depth_texture_);
        water_scene_depth_texture_ = 0;
    }
    if (water_scene_color_texture_ != 0) {
        glDeleteTextures(1, &water_scene_color_texture_);
        water_scene_color_texture_ = 0;
    }
    if (water_scene_framebuffer_ != 0) {
        glDeleteFramebuffers(1, &water_scene_framebuffer_);
        water_scene_framebuffer_ = 0;
    }
    water_scene_target_width_ = 0;
    water_scene_target_height_ = 0;
    water_scene_color_internal_format_ = 0;
}

void Renderer::ensure_post_process_targets(int width, int height, bool require_glow_targets) {
    const auto target_width = std::max(width, 1);
    const auto target_height = std::max(height, 1);
    const auto quality_settings = active_quality_settings_;
    const auto color_format = color_target_format(quality_settings);

    const auto scene_matches = scene_framebuffer_ != 0 && scene_color_texture_ != 0 && scene_depth_texture_ != 0 &&
                                scene_target_width_ == target_width && scene_target_height_ == target_height &&
                                scene_color_internal_format_ == color_format.internal_format;
    if (!scene_matches) {
        destroy_scene_targets();

        glGenFramebuffers(1, &scene_framebuffer_);
        glBindFramebuffer(GL_FRAMEBUFFER, scene_framebuffer_);

        glGenTextures(1, &scene_color_texture_);
        glBindTexture(GL_TEXTURE_2D, scene_color_texture_);
        glTexImage2D(
            GL_TEXTURE_2D,
            0,
            color_format.internal_format,
            target_width,
            target_height,
            0,
            color_format.pixel_format,
            color_format.pixel_type,
            nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, scene_color_texture_, 0);

        glGenTextures(1, &scene_depth_texture_);
        glBindTexture(GL_TEXTURE_2D, scene_depth_texture_);
        glTexImage2D(
            GL_TEXTURE_2D,
            0,
            GL_DEPTH_COMPONENT24,
            target_width,
            target_height,
            0,
            GL_DEPTH_COMPONENT,
            GL_UNSIGNED_INT,
            nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, scene_depth_texture_, 0);

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            throw std::runtime_error("Scene framebuffer is incomplete");
        }
        scene_target_width_ = target_width;
        scene_target_height_ = target_height;
        scene_color_internal_format_ = color_format.internal_format;
    }

    if (require_glow_targets) {
        const auto glow_divisor = std::max(quality_settings.glow_downsample, 1);
        const auto glow_width = std::max(target_width / glow_divisor, 1);
        const auto glow_height = std::max(target_height / glow_divisor, 1);
        const auto glow_matches =
            glow_extract_framebuffer_ != 0 && glow_extract_texture_ != 0 && glow_ping_framebuffer_ != 0 &&
            glow_ping_texture_ != 0 && glow_target_width_ == glow_width && glow_target_height_ == glow_height &&
            glow_color_internal_format_ == color_format.internal_format;
        if (!glow_matches) {
            destroy_glow_targets();

            glGenFramebuffers(1, &glow_extract_framebuffer_);
            glBindFramebuffer(GL_FRAMEBUFFER, glow_extract_framebuffer_);
            glGenTextures(1, &glow_extract_texture_);
            glBindTexture(GL_TEXTURE_2D, glow_extract_texture_);
            glTexImage2D(
                GL_TEXTURE_2D,
                0,
                color_format.internal_format,
                glow_width,
                glow_height,
                0,
                color_format.pixel_format,
                color_format.pixel_type,
                nullptr);
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
            glTexImage2D(
                GL_TEXTURE_2D,
                0,
                color_format.internal_format,
                glow_width,
                glow_height,
                0,
                color_format.pixel_format,
                color_format.pixel_type,
                nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, glow_ping_texture_, 0);
            if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
                throw std::runtime_error("Glow blur framebuffer is incomplete");
            }

            glow_target_width_ = glow_width;
            glow_target_height_ = glow_height;
            glow_color_internal_format_ = color_format.internal_format;
        }
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Renderer::destroy_post_process_targets() {
    destroy_scene_targets();
    destroy_glow_targets();
}

void Renderer::destroy_scene_targets() {
    if (scene_depth_texture_ != 0) {
        glDeleteTextures(1, &scene_depth_texture_);
        scene_depth_texture_ = 0;
    }
    if (scene_color_texture_ != 0) {
        glDeleteTextures(1, &scene_color_texture_);
        scene_color_texture_ = 0;
    }
    if (scene_framebuffer_ != 0) {
        glDeleteFramebuffers(1, &scene_framebuffer_);
        scene_framebuffer_ = 0;
    }
    scene_target_width_ = 0;
    scene_target_height_ = 0;
    scene_color_internal_format_ = 0;
}

void Renderer::destroy_glow_targets() {
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
    glow_target_width_ = 0;
    glow_target_height_ = 0;
    glow_color_internal_format_ = 0;
}

void Renderer::draw_sky(const glm::mat4& inverse_view_projection,
                        const EnvironmentState& environment,
                        const RendererQualitySettings& quality_settings) {
    if (sky_program_ == 0 || screen_quad_vao_ == 0 || accent_texture_ == 0) {
        return;
    }

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
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
    glUniform1f(sky_uniforms_.overcast_intensity, environment.overcast_intensity);
    glUniform1f(sky_uniforms_.precipitation_intensity, environment.precipitation_intensity);
    glUniform1f(sky_uniforms_.storm_intensity, environment.storm_intensity);
    glUniform1f(
        sky_uniforms_.violent_storm_intensity,
        environment.violent_storm_intensity);
    glUniform1f(sky_uniforms_.lightning_intensity, environment.lightning_intensity);
    glUniform1f(
        sky_uniforms_.lightning_bolt_intensity,
        environment.lightning_bolt_intensity);
    glUniform3fv(
        sky_uniforms_.lightning_direction,
        1,
        glm::value_ptr(
            environment.lightning_direction));
    glUniform1f(
        sky_uniforms_.lightning_shape_seed,
        environment.lightning_shape_seed);
    glUniform1f(sky_uniforms_.weather_time, environment.weather_time_seconds);
    glUniform1i(sky_uniforms_.cloud_steps, quality_settings.cloud_steps);
    glUniform1f(sky_uniforms_.cloud_detail, quality_settings.cloud_detail);
    glUniform1i(sky_uniforms_.accent_atlas, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, accent_texture_);
    glBindVertexArray(screen_quad_vao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    record_triangle_draw(3);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
}

void Renderer::run_post_process(const EnvironmentState& environment,
                                float weather_exposure,
                                int width,
                                int height,
                                bool optional_effects_enabled) {
    if (post_process_program_ == 0 ||
        screen_quad_vao_ == 0 ||
        scene_color_texture_ == 0 ||
        scene_depth_texture_ == 0) {
        return;
    }

    const auto glow_resources_ready =
        glow_extract_program_ != 0 &&
        glow_blur_program_ != 0 &&
        glow_extract_framebuffer_ != 0 &&
        glow_extract_texture_ != 0 &&
        glow_ping_framebuffer_ != 0 &&
        glow_ping_texture_ != 0 &&
        glow_target_width_ > 0 &&
        glow_target_height_ > 0;
    const auto run_optional_effects =
        optional_effects_enabled &&
        glow_resources_ready;
    const auto quality_settings = active_quality_settings_;

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glBindVertexArray(screen_quad_vao_);

    if (run_optional_effects) {
        glViewport(
            0,
            0,
            glow_target_width_,
            glow_target_height_);
        glBindFramebuffer(
            GL_FRAMEBUFFER,
            glow_extract_framebuffer_);
        glClearColor(0.0F, 0.0F, 0.0F, 1.0F);
        glClear(GL_COLOR_BUFFER_BIT);
        glUseProgram(glow_extract_program_);
        glUniform1i(
            glow_extract_uniforms_.scene_texture,
            0);
        glUniform1f(
            glow_extract_uniforms_.threshold,
            visual_pipeline_glow_threshold(
                options_.visual_pipeline,
                environment.glow_threshold));
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(
            GL_TEXTURE_2D,
            scene_color_texture_);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        record_triangle_draw(3);

        glBindFramebuffer(
            GL_FRAMEBUFFER,
            glow_ping_framebuffer_);
        glClear(GL_COLOR_BUFFER_BIT);
        glUseProgram(glow_blur_program_);
        glUniform1i(
            glow_blur_uniforms_.source_texture,
            0);
        glUniform2f(
            glow_blur_uniforms_.texel_direction,
            1.0F /
                static_cast<float>(glow_target_width_),
            0.0F);
        glBindTexture(
            GL_TEXTURE_2D,
            glow_extract_texture_);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        record_triangle_draw(3);

        glBindFramebuffer(
            GL_FRAMEBUFFER,
            glow_extract_framebuffer_);
        glClear(GL_COLOR_BUFFER_BIT);
        glUniform2f(
            glow_blur_uniforms_.texel_direction,
            0.0F,
            1.0F /
                static_cast<float>(glow_target_height_));
        glBindTexture(
            GL_TEXTURE_2D,
            glow_ping_texture_);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        record_triangle_draw(3);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, width, std::max(height, 1));
    glUseProgram(post_process_program_);
    glUniform1i(
        post_process_uniforms_.scene_texture,
        0);
    glUniform1i(
        post_process_uniforms_.glow_texture,
        1);
    glUniform1i(
        post_process_uniforms_.scene_depth,
        2);
    glUniform1f(
        post_process_uniforms_.exposure,
        std::isfinite(environment.exposure)
            ? std::max(environment.exposure, 0.001F)
            : 1.0F);
    glUniform1f(
        post_process_uniforms_.saturation_boost,
        run_optional_effects
            ? environment.saturation_boost
            : 1.0F);
    glUniform1f(
        post_process_uniforms_.contrast,
        run_optional_effects
            ? visual_pipeline_post_contrast(
                  options_.visual_pipeline,
                  environment.contrast)
            : 1.0F);
    glUniform1f(
        post_process_uniforms_.vignette_strength,
        run_optional_effects
            ? environment.vignette_strength
            : 0.0F);
    const auto night_tint =
        run_optional_effects
            ? environment.night_tint_color
            : glm::vec3 {0.0F};
    glUniform3fv(
        post_process_uniforms_.night_tint_color,
        1,
        glm::value_ptr(night_tint));
    glUniform1f(
        post_process_uniforms_.glow_strength,
        run_optional_effects
            ? visual_pipeline_glow_strength(
                  options_.visual_pipeline,
                  environment.glow_strength)
            : 0.0F);
    glUniform1f(
        post_process_uniforms_.sharpen_strength,
        run_optional_effects
            ? environment.post_sharpen_strength *
                  quality_settings.post_detail_scale
            : 0.0F);
    glUniform1f(
        post_process_uniforms_.edge_strength,
        run_optional_effects
            ? environment.post_edge_strength *
                  quality_settings.post_detail_scale *
                  (options_.visual_pipeline ==
                           VisualPipeline::ModernStylized
                       ? 0.32F
                       : 1.0F)
            : 0.0F);
    glUniform1i(
        post_process_uniforms_.fxaa_enabled,
        run_optional_effects &&
                is_modern_visual_pipeline(
                    options_.visual_pipeline) &&
                quality_settings.fxaa_enabled
            ? 1
            : 0);
    glUniform1i(
        post_process_uniforms_.modern_pipeline,
        is_modern_visual_pipeline(
            options_.visual_pipeline)
            ? 1
            : 0);
    glUniform1i(
        post_process_uniforms_.resolve_only,
        run_optional_effects
            ? 0
            : 1);
    glUniform1f(
        post_process_uniforms_.storm_intensity,
        run_optional_effects
            ? environment.storm_intensity
            : 0.0F);
    glUniform1f(
        post_process_uniforms_.lightning_intensity,
        run_optional_effects
            ? environment.lightning_intensity
            : 0.0F);
    glUniform1f(
        post_process_uniforms_.weather_exposure,
        glm::clamp(
            std::isfinite(weather_exposure)
                ? weather_exposure
                : 1.0F,
            0.0F,
            1.0F));

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(
        GL_TEXTURE_2D,
        scene_color_texture_);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(
        GL_TEXTURE_2D,
        run_optional_effects
            ? glow_extract_texture_
            : scene_color_texture_);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(
        GL_TEXTURE_2D,
        scene_depth_texture_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    record_triangle_draw(3);
    glActiveTexture(GL_TEXTURE0);
}

void Renderer::run_menu_background_pass(
    int width,
    int height,
    float exposure) {
    if (menu_background_program_ == 0 || glow_blur_program_ == 0 || screen_quad_vao_ == 0 ||
        scene_color_texture_ == 0 || glow_extract_texture_ == 0 || glow_ping_texture_ == 0) {
        return;
    }

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glBindVertexArray(screen_quad_vao_);

    glViewport(0, 0, glow_target_width_, glow_target_height_);
    glBindFramebuffer(GL_FRAMEBUFFER, glow_ping_framebuffer_);
    glClearColor(0.0F, 0.0F, 0.0F, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(glow_blur_program_);
    glUniform1i(glow_blur_uniforms_.source_texture, 0);
    glUniform2f(glow_blur_uniforms_.texel_direction, 1.0F / static_cast<float>(glow_target_width_), 0.0F);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, scene_color_texture_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    record_triangle_draw(3);

    glBindFramebuffer(GL_FRAMEBUFFER, glow_extract_framebuffer_);
    glClear(GL_COLOR_BUFFER_BIT);
    glUniform2f(glow_blur_uniforms_.texel_direction, 0.0F, 1.0F / static_cast<float>(glow_target_height_));
    glBindTexture(GL_TEXTURE_2D, glow_ping_texture_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    record_triangle_draw(3);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, width, std::max(height, 1));
    glUseProgram(menu_background_program_);
    glUniform1i(menu_background_uniforms_.scene_texture, 0);
    glUniform1i(menu_background_uniforms_.blur_texture, 1);
    glUniform1f(menu_background_uniforms_.blur_mix, 0.80F);
    const glm::vec3 tint_color {0.66F, 0.72F, 0.78F};
    glUniform3fv(
        menu_background_uniforms_.tint_color,
        1,
        glm::value_ptr(tint_color));
    glUniform1f(
        menu_background_uniforms_.vignette_strength,
        0.30F);
    glUniform1f(
        menu_background_uniforms_.exposure,
        std::isfinite(exposure)
            ? std::max(exposure, 0.001F)
            : 1.0F);
    glUniform1i(
        menu_background_uniforms_.modern_pipeline,
        is_modern_visual_pipeline(options_.visual_pipeline)
            ? 1
            : 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, scene_color_texture_);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, glow_extract_texture_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    record_triangle_draw(3);
    glActiveTexture(GL_TEXTURE0);
}

void Renderer::draw_precipitation(
    const glm::mat4& view_projection,
    const glm::mat4& inverse_view,
    const glm::vec3& camera_position,
    const EnvironmentState& environment,
    const OceanState& ocean,
    const ShipRenderState& ship,
    const RendererQualitySettings& quality_settings,
    RendererFrameStats& frame_stats) {
    if (precipitation_program_ == 0 ||
        precipitation_vao_ == 0 ||
        precipitation_instance_vbo_ == 0 ||
        !std::isfinite(environment.precipitation_intensity) ||
        environment.precipitation_intensity <= 0.0F) {
        precipitation_field_.clear();
        return;
    }

    const PrecipitationBudget budget {
        quality_settings.precipitation_drop_budget,
        quality_settings.precipitation_impact_budget,
        quality_settings.precipitation_radius,
    };
    const auto ocean_displacement =
        std::isfinite(
            ocean.maximum_displacement)
            ? std::clamp(
                  ocean.maximum_displacement,
                  0.0F,
                  16.0F)
            : 0.0F;
    // Je prolonge les trajectoires sous le creux theorique le plus bas ; le
    // depth test les coupe ensuite sur la surface animee exacte de la mer.
    const auto precipitation_floor =
        static_cast<float>(
            kSeaLevel + 1) -
        ocean_displacement -
        0.50F;
    const auto& precipitation = precipitation_field_.sample(
        environment,
        camera_position,
        precipitation_floor,
        budget);

    auto& instances = precipitation_instances_scratch_;
    instances.clear();
    instances.reserve(
        precipitation.drops.size() +
        precipitation.impacts.size());

    const auto protection_enabled =
        ship_protection_is_renderable(ship);
    const auto inverse_ship_model =
        protection_enabled
            ? glm::inverse(ship.model_matrix)
            : glm::mat4 {1.0F};
    const auto* protection_profile =
        protection_enabled
            ? &ship.blueprint->protection_profile
            : nullptr;
    const auto to_ship_local =
        [&inverse_ship_model](const glm::vec3& world_point) noexcept {
            return glm::vec3 {
                inverse_ship_model *
                glm::vec4 {world_point, 1.0F},
            };
        };
    const auto point_is_sheltered =
        [&to_ship_local, protection_profile](const glm::vec3& world_point) noexcept {
            return protection_profile != nullptr &&
                   protection_profile->shelters_from_weather_local(
                       to_ship_local(world_point));
        };
    const auto point_excludes_ocean =
        [&to_ship_local, protection_profile](const glm::vec3& world_point) noexcept {
            return protection_profile != nullptr &&
                   protection_profile->excludes_ocean_local(
                       to_ship_local(world_point));
        };

    auto visible_drop_count = std::size_t {0U};
    for (const auto& drop : precipitation.drops) {
        const auto speed_squared =
            glm::dot(drop.velocity, drop.velocity);
        const auto direction =
            std::isfinite(speed_squared) &&
                    speed_squared > 1.0e-6F
                ? drop.velocity /
                      std::sqrt(speed_squared)
                : glm::vec3 {0.0F, -1.0F, 0.0F};
        const auto half_segment =
            direction *
            (std::max(drop.length, 0.0F) * 0.5F);
        if (point_is_sheltered(drop.position) ||
            point_is_sheltered(drop.position - half_segment) ||
            point_is_sheltered(drop.position + half_segment)) {
            continue;
        }

        instances.push_back({
            {
                drop.position.x,
                drop.position.y,
                drop.position.z,
                drop.length,
            },
            {
                drop.velocity.x,
                drop.velocity.y,
                drop.velocity.z,
                drop.width,
            },
            {
                drop.opacity,
                0.0F,
                0.0F,
                0.0F,
            },
        });
        ++visible_drop_count;
    }

    auto wind_direction =
        glm::vec2 {
            environment.wind_direction_xz.x,
            environment.wind_direction_xz.y,
        };
    const auto wind_length_squared =
        glm::dot(wind_direction, wind_direction);
    if (!std::isfinite(wind_length_squared) ||
        wind_length_squared <= 1.0e-6F) {
        wind_direction = {0.0F, 1.0F};
    } else {
        wind_direction /= std::sqrt(wind_length_squared);
    }
    const auto fall_direction =
        glm::normalize(
            glm::vec3 {
                wind_direction.x *
                    glm::clamp(environment.wind_strength, 0.0F, 1.0F) *
                    0.32F,
                -1.0F,
                wind_direction.y *
                    glm::clamp(environment.wind_strength, 0.0F, 1.0F) *
                    0.32F,
            });
    constexpr auto kImpactRayLength = 96.0F;
    auto visible_impact_count = std::size_t {0U};
    for (const auto& impact : precipitation.impacts) {
        auto impact_position = impact.position;
        auto deck_hit = false;

        if (protection_enabled) {
            const auto ray_end =
                glm::vec3 {
                    impact.position.x,
                    static_cast<float>(kSeaLevel + 1),
                    impact.position.z,
                };
            const auto ray_origin_world =
                ray_end -
                fall_direction *
                    kImpactRayLength;
            const auto ray_origin_local =
                to_ship_local(ray_origin_world);
            auto ray_direction_local =
                glm::vec3 {
                    inverse_ship_model *
                    glm::vec4 {fall_direction, 0.0F},
                };
            const auto local_direction_length_squared =
                glm::dot(
                    ray_direction_local,
                    ray_direction_local);
            if (std::isfinite(local_direction_length_squared) &&
                local_direction_length_squared > 1.0e-6F) {
                ray_direction_local /=
                    std::sqrt(
                        local_direction_length_squared);
                auto nearest_hit =
                    std::numeric_limits<float>::max();
                auto nearest_local_position =
                    glm::vec3 {0.0F};

                for (const auto& part : ship.parts) {
                    if (!part.supports_player) {
                        continue;
                    }
                    const auto min_corner =
                        glm::min(
                            part.local_start,
                            part.local_end) -
                        glm::vec3 {0.01F};
                    const auto max_corner =
                        glm::max(
                            part.local_start,
                            part.local_end) +
                        glm::vec3 {0.01F};
                    const auto distance =
                        ray_aabb_entry_distance(
                            ray_origin_local,
                            ray_direction_local,
                            min_corner,
                            max_corner,
                            kImpactRayLength);
                    if (!distance.has_value() ||
                        *distance >= nearest_hit) {
                        continue;
                    }

                    const auto local_hit =
                        ray_origin_local +
                        ray_direction_local *
                            *distance;
                    const auto outside_probe =
                        local_hit -
                        ray_direction_local *
                            0.05F;
                    if (protection_profile->shelters_from_weather_local(
                            outside_probe)) {
                        continue;
                    }
                    nearest_hit = *distance;
                    nearest_local_position = local_hit;
                }

                if (nearest_hit <
                    std::numeric_limits<float>::max()) {
                    impact_position =
                        glm::vec3 {
                            ship.model_matrix *
                            glm::vec4 {
                                nearest_local_position -
                                    ray_direction_local *
                                        0.025F,
                                1.0F,
                            },
                        };
                    deck_hit = true;
                }
            }
        }

        if (!deck_hit) {
            const auto ocean_sample =
                OceanSimulation::sample(
                    ocean,
                    {impact_position.x, impact_position.z},
                    static_cast<std::size_t>(
                        std::clamp(
                            quality_settings.ocean_wave_count,
                            1,
                            static_cast<int>(
                                kOceanMaxWaveCount))));
            impact_position.y =
                static_cast<float>(kSeaLevel + 1) +
                ocean_sample.height +
                0.035F;
            if (point_excludes_ocean(impact_position)) {
                continue;
            }
        }
        if (point_is_sheltered(impact_position) ||
            !finite_vec3(impact_position) ||
            impact.opacity <= 0.003F) {
            continue;
        }

        const auto age_ratio =
            impact.lifetime_seconds > 1.0e-4F
                ? glm::clamp(
                      impact.age_seconds /
                          impact.lifetime_seconds,
                      0.0F,
                      1.0F)
                : 1.0F;
        instances.push_back({
            {
                impact_position.x,
                impact_position.y,
                impact_position.z,
                impact.radius,
            },
            {
                0.0F,
                1.0F,
                0.0F,
                impact.radius,
            },
            {
                impact.opacity,
                1.0F,
                age_ratio,
                impact.radius,
            },
        });
        ++visible_impact_count;
    }

    if (instances.empty()) {
        return;
    }

    const auto instance_bytes =
        static_cast<GLsizeiptr>(
            instances.size() *
            sizeof(PrecipitationGpuInstance));
    const ScopedPrecipitationGlState previous_gl_state {};
    glBindVertexArray(precipitation_vao_);
    glBindBuffer(
        GL_ARRAY_BUFFER,
        precipitation_instance_vbo_);
    if (precipitation_instance_buffer_bytes_ <
        instance_bytes) {
        precipitation_instance_buffer_bytes_ =
            grow_buffer_capacity(
                precipitation_instance_buffer_bytes_,
                instance_bytes,
                kInitialPrecipitationInstanceBufferBytes);
    }
    orphan_bound_buffer(
        GL_ARRAY_BUFFER,
        precipitation_instance_buffer_bytes_);
    glBufferSubData(
        GL_ARRAY_BUFFER,
        0,
        instance_bytes,
        instances.data());
    frame_uploaded_bytes_ +=
        static_cast<std::uint64_t>(
            std::max<GLsizeiptr>(
                instance_bytes,
                0));

    const auto camera_right =
        glm::normalize(
            glm::vec3 {
                inverse_view[0],
            });
    const auto camera_up =
        glm::normalize(
            glm::vec3 {
                inverse_view[1],
            });

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(
        GL_SRC_ALPHA,
        GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(precipitation_program_);
    glUniformMatrix4fv(
        precipitation_uniforms_.view_projection,
        1,
        GL_FALSE,
        glm::value_ptr(view_projection));
    glUniform3fv(
        precipitation_uniforms_.camera_position,
        1,
        glm::value_ptr(camera_position));
    glUniform3fv(
        precipitation_uniforms_.camera_right,
        1,
        glm::value_ptr(camera_right));
    glUniform3fv(
        precipitation_uniforms_.camera_up,
        1,
        glm::value_ptr(camera_up));
    glUniform3fv(
        precipitation_uniforms_.fog_color,
        1,
        glm::value_ptr(environment.fog_color));
    glUniform1f(
        precipitation_uniforms_.lightning_intensity,
        glm::clamp(
            environment.lightning_intensity,
            0.0F,
            1.0F));
    glUniform1f(
        precipitation_uniforms_.storm_intensity,
        glm::clamp(
            environment.storm_intensity,
            0.0F,
            1.0F));
    upload_precipitation_ship_protection(ship);

    glDrawArraysInstanced(
        GL_TRIANGLE_STRIP,
        0,
        4,
        static_cast<GLsizei>(
            instances.size()));
    record_triangle_draw(
        6,
        static_cast<GLsizei>(
            instances.size()));

    frame_stats.precipitation_drops =
        visible_drop_count;
    frame_stats.precipitation_impacts =
        visible_impact_count;
}

void Renderer::draw_old_guard_effects(
    std::span<const OldGuardMuzzleFlashInstance> flashes,
    std::span<const OldGuardSmokeInstance> smoke,
    const glm::mat4& view_projection,
    const glm::mat4& inverse_view,
    const glm::vec3& camera_position,
    bool viewmodel_overlay,
    const PlayerViewModelPose* viewmodel_pose) {
    if ((flashes.empty() && smoke.empty()) ||
        old_guard_effect_program_ == 0 ||
        old_guard_effect_vao_ == 0 ||
        old_guard_effect_instance_vbo_ == 0) {
        return;
    }

    auto& instances = old_guard_effect_instances_scratch_;
    instances.clear();
    const auto maximum_count =
        std::min(smoke.size(), kOldGuardSmokeCapacity) +
        std::min(flashes.size(), kOldGuardFlashCapacity);
    if (instances.capacity() < maximum_count) {
        instances.reserve(maximum_count);
    }

    constexpr auto kEffectDrawDistanceSquared =
        kOldGuardRenderDistance * kOldGuardRenderDistance;
    const auto append_if_visible =
        [&](const glm::vec3& position,
            float size,
            float opacity,
            float kind,
            float rotation,
            float intensity) {
            if (!std::isfinite(position.x) ||
                !std::isfinite(position.y) ||
                !std::isfinite(position.z) ||
                !std::isfinite(size) ||
                !std::isfinite(opacity) ||
                !std::isfinite(rotation) ||
                !std::isfinite(intensity) ||
                size <= 0.0F ||
                opacity <= 0.0F) {
                return;
            }
            const auto delta = position - camera_position;
            const auto distance_squared = glm::dot(delta, delta);
            if (!std::isfinite(distance_squared) ||
                distance_squared > kEffectDrawDistanceSquared) {
                return;
            }
            instances.push_back({
                .position_size = glm::vec4 {
                    position,
                    std::clamp(size, 0.01F, 3.0F),
                },
                .appearance = glm::vec4 {
                    std::clamp(opacity, 0.0F, 1.0F),
                    kind,
                    rotation,
                    std::max(intensity, 0.0F),
                },
            });
        };

    for (const auto& puff : smoke.first(
             std::min(smoke.size(), kOldGuardSmokeCapacity))) {
        if (!std::isfinite(puff.age) ||
            !std::isfinite(puff.lifetime) ||
            puff.lifetime <= 0.0F ||
            puff.age < 0.0F ||
            puff.age >= puff.lifetime) {
            continue;
        }
        const auto age_ratio =
            std::clamp(puff.age / puff.lifetime, 0.0F, 1.0F);
        const auto fade =
            std::pow(1.0F - age_ratio, 1.25F);
        const auto seed_variation =
            static_cast<float>(puff.seed % 997U) / 997.0F;
        append_if_visible(
            puff.position,
            puff.size * (1.0F + age_ratio * 2.35F),
            puff.opacity * fade,
            0.0F,
            puff.rotation_radians,
            seed_variation);
    }
    for (const auto& flash : flashes.first(
             std::min(flashes.size(), kOldGuardFlashCapacity))) {
        if (!std::isfinite(flash.age) ||
            !std::isfinite(flash.lifetime) ||
            flash.lifetime <= 0.0F ||
            flash.age < 0.0F ||
            flash.age >= flash.lifetime) {
            continue;
        }
        const auto age_ratio =
            std::clamp(flash.age / flash.lifetime, 0.0F, 1.0F);
        const auto flash_position =
            viewmodel_overlay &&
                    viewmodel_pose != nullptr &&
                    viewmodel_pose->musket_active
                ? viewmodel_pose->muzzle_position
                : flash.position;
        const auto flash_direction =
            viewmodel_overlay &&
                    viewmodel_pose != nullptr &&
                    viewmodel_pose->musket_active
                ? viewmodel_pose->muzzle_forward
                : flash.direction;
        append_if_visible(
            flash_position,
            flash.size * (1.0F + age_ratio * 0.55F),
            (1.0F - age_ratio) * (1.0F - age_ratio),
            1.0F,
            std::atan2(
                flash_direction.y,
                flash_direction.x),
            flash.intensity);
    }
    if (instances.empty()) {
        return;
    }

    // Je trie toutes les transparences du fond vers la camera avant l'upload ;
    // la profondeur reste lue mais aucune bouffee ne masque les suivantes.
    std::stable_sort(
        instances.begin(),
        instances.end(),
        [&](const OldGuardEffectGpuInstance& left,
            const OldGuardEffectGpuInstance& right) noexcept {
            const auto left_delta =
                glm::vec3 {left.position_size} -
                camera_position;
            const auto right_delta =
                glm::vec3 {right.position_size} -
                camera_position;
            return glm::dot(left_delta, left_delta) >
                   glm::dot(right_delta, right_delta);
        });

    const auto instance_bytes =
        static_cast<GLsizeiptr>(
            instances.size() *
            sizeof(OldGuardEffectGpuInstance));
    const ScopedPrecipitationGlState previous_gl_state {};
    glBindVertexArray(old_guard_effect_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, old_guard_effect_instance_vbo_);
    if (old_guard_effect_instance_buffer_bytes_ < instance_bytes) {
        old_guard_effect_instance_buffer_bytes_ =
            grow_buffer_capacity(
                old_guard_effect_instance_buffer_bytes_,
                instance_bytes,
                kInitialOldGuardEffectInstanceBufferBytes);
    }
    orphan_bound_buffer(
        GL_ARRAY_BUFFER,
        old_guard_effect_instance_buffer_bytes_);
    glBufferSubData(
        GL_ARRAY_BUFFER,
        0,
        instance_bytes,
        instances.data());
    frame_uploaded_bytes_ +=
        static_cast<std::uint64_t>(
            std::max<GLsizeiptr>(instance_bytes, 0));

    const auto camera_right =
        glm::normalize(glm::vec3 {inverse_view[0]});
    const auto camera_up =
        glm::normalize(glm::vec3 {inverse_view[1]});

    if (viewmodel_overlay) {
        // Je dessine le flash avec la projection du fusil et sans profondeur :
        // il reste ainsi solidaire du viewmodel avant que la fumee ne vive
        // independamment dans le monde.
        glDisable(GL_DEPTH_TEST);
    } else {
        glEnable(GL_DEPTH_TEST);
    }
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(old_guard_effect_program_);
    glUniformMatrix4fv(
        old_guard_effect_uniforms_.view_projection,
        1,
        GL_FALSE,
        glm::value_ptr(view_projection));
    glUniform3fv(
        old_guard_effect_uniforms_.camera_right,
        1,
        glm::value_ptr(camera_right));
    glUniform3fv(
        old_guard_effect_uniforms_.camera_up,
        1,
        glm::value_ptr(camera_up));
    glDrawArraysInstanced(
        GL_TRIANGLE_STRIP,
        0,
        4,
        static_cast<GLsizei>(instances.size()));
    record_triangle_draw(
        6,
        static_cast<GLsizei>(instances.size()));
}

void Renderer::draw_item_drops(std::span<const ItemDropRenderInstance> item_drops,
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
                               bool sun_visible) {
    if (item_drops.empty() || item_drop_program_ == 0 || item_drop_vao_ == 0 || item_drop_instance_vbo_ == 0 || item_drop_ebo_ == 0) {
        return;
    }

    auto& instances = item_drop_instances_scratch_;
    build_item_drop_gpu_instances_into(item_drops, instances);
    if (instances.empty()) {
        return;
    }

    const auto instance_bytes = static_cast<GLsizeiptr>(instances.size() * sizeof(ItemDropGpuInstance));
    glBindVertexArray(item_drop_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, item_drop_instance_vbo_);
    if (item_drop_instance_buffer_bytes_ < instance_bytes) {
        item_drop_instance_buffer_bytes_ = grow_buffer_capacity(
            item_drop_instance_buffer_bytes_,
            instance_bytes,
            kInitialItemDropInstanceBufferBytes);
    }
    orphan_bound_buffer(GL_ARRAY_BUFFER, item_drop_instance_buffer_bytes_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, instance_bytes, instances.data());
    frame_uploaded_bytes_ += static_cast<std::uint64_t>(std::max<GLsizeiptr>(instance_bytes, 0));

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glDisable(GL_BLEND);

    glUseProgram(item_drop_program_);
    glUniformMatrix4fv(item_drop_uniforms_.view_projection, 1, GL_FALSE, glm::value_ptr(view_projection));
    glUniformMatrix4fv(item_drop_uniforms_.light_view_projection, 1, GL_FALSE, glm::value_ptr(light_view_projection));
    glUniformMatrix4fv(
        item_drop_uniforms_.light_view_projection_far,
        1,
        GL_FALSE,
        glm::value_ptr(light_view_projection_far));
    glUniformMatrix4fv(item_drop_uniforms_.inverse_view_projection, 1, GL_FALSE, glm::value_ptr(inverse_view_projection));
    glUniform3fv(item_drop_uniforms_.camera_position, 1, glm::value_ptr(camera_position));
    glUniform3fv(
        item_drop_uniforms_.camera_forward,
        1,
        glm::value_ptr(camera_forward));
    glUniform3fv(item_drop_uniforms_.sun_direction, 1, glm::value_ptr(environment.sun_direction));
    glUniform3fv(item_drop_uniforms_.sun_color, 1, glm::value_ptr(environment.sun_color));
    glUniform3fv(item_drop_uniforms_.ambient_color, 1, glm::value_ptr(environment.ambient_color));
    glUniform3fv(item_drop_uniforms_.fog_color, 1, glm::value_ptr(environment.fog_color));
    glUniform3fv(item_drop_uniforms_.distant_fog_color, 1, glm::value_ptr(environment.distant_fog_color));
    glUniform3fv(item_drop_uniforms_.horizon_glow_color, 1, glm::value_ptr(environment.horizon_glow_color));
    glUniform3fv(item_drop_uniforms_.night_tint_color, 1, glm::value_ptr(environment.night_tint_color));
    glUniform1f(item_drop_uniforms_.daylight_factor, environment.daylight_factor);
    glUniform1f(item_drop_uniforms_.sun_visibility, sun_visible ? 1.0F : 0.0F);
    glUniform1f(item_drop_uniforms_.time_of_day, environment.time_of_day);
    glUniform1f(item_drop_uniforms_.cloud_intensity, environment.cloud_intensity);
    glUniform1f(item_drop_uniforms_.cloud_shadow_strength, environment.cloud_shadow_strength);
    glUniform1f(item_drop_uniforms_.wind_strength, environment.wind_strength);
    glUniform1f(item_drop_uniforms_.atmospheric_scatter_strength, environment.atmospheric_scatter_strength);
    glUniform1f(item_drop_uniforms_.height_fog_density, environment.height_fog_density);
    glUniform1f(item_drop_uniforms_.precipitation_intensity, environment.precipitation_intensity);
    glUniform1f(item_drop_uniforms_.storm_intensity, environment.storm_intensity);
    glUniform1f(item_drop_uniforms_.lightning_intensity, environment.lightning_intensity);
    glUniform1i(item_drop_uniforms_.atlas, 0);
    glUniform1i(item_drop_uniforms_.shadow_map, 1);
    glUniform1i(item_drop_uniforms_.shadow_map_far, 7);
    glUniform1i(
        item_drop_uniforms_.shadow_cascade_count,
        shadow_cascade_count);
    glUniform1f(
        item_drop_uniforms_.shadow_split_distance,
        shadow_split_distance);
    glUniform1f(
        item_drop_uniforms_.shadow_transition_width,
        shadow_transition_width);
    glUniform1i(item_drop_uniforms_.scene_color, 2);
    glUniform1i(item_drop_uniforms_.scene_depth, 3);
    glUniform1i(item_drop_uniforms_.shadows_enabled, options_.shadows_enabled ? 1 : 0);

    const auto scene_bindings = select_scene_sampler_bindings(
        false,
        scene_fallback_color_texture_,
        scene_fallback_depth_texture_,
        scene_color_texture_,
        scene_depth_texture_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, atlas_texture_);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, shadow_map_);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, scene_bindings.color_texture);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, scene_bindings.depth_texture);
    glActiveTexture(GL_TEXTURE7);
    glBindTexture(GL_TEXTURE_2D, shadow_map_far_);

    glDrawElementsInstanced(
        GL_TRIANGLES,
        item_drop_template_index_count_,
        GL_UNSIGNED_INT,
        nullptr,
        static_cast<GLsizei>(instances.size()));
    record_triangle_draw(
        item_drop_template_index_count_,
        static_cast<GLsizei>(instances.size()));
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glActiveTexture(GL_TEXTURE0);
}

auto Renderer::collect_visible_creature_parts(std::span<const CreatureRenderInstance> creatures,
                                              std::span<const CrewRenderInstance> crew,
                                              std::span<const OldGuardRenderInstance> old_guard,
                                              const glm::vec3& focus,
                                              float creature_draw_distance,
                                              float crew_draw_distance,
                                              float old_guard_draw_distance)
    -> std::span<const CreaturePartInstance> {
    auto& visible_creatures = visible_creatures_cache_;
    auto& visible_crew = visible_crew_cache_;
    auto& visible_old_guard = visible_old_guard_cache_;
    auto& parts = creature_parts_scratch_;
    visible_creatures.clear();
    visible_crew.clear();
    visible_old_guard.clear();
    parts.clear();
    const auto safe_creature_distance =
        std::isfinite(creature_draw_distance) ? std::max(creature_draw_distance, 0.0F) : 0.0F;
    const auto safe_crew_distance =
        std::isfinite(crew_draw_distance) ? std::max(crew_draw_distance, 0.0F) : 0.0F;
    const auto safe_old_guard_distance =
        std::isfinite(old_guard_draw_distance)
            ? std::max(old_guard_draw_distance, 0.0F)
            : 0.0F;
    const auto creature_draw_distance_sq = safe_creature_distance * safe_creature_distance;
    const auto crew_draw_distance_sq = safe_crew_distance * safe_crew_distance;
    const auto old_guard_draw_distance_sq =
        safe_old_guard_distance * safe_old_guard_distance;

    if (visible_creatures.capacity() < creatures.size()) {
        visible_creatures.reserve(creatures.size());
    }
    for (const auto& creature : creatures) {
        const auto dx = creature.position.x - focus.x;
        const auto dz = creature.position.z - focus.z;
        const auto distance_squared = dx * dx + dz * dz;
        if (!std::isfinite(distance_squared) || distance_squared > creature_draw_distance_sq) {
            continue;
        }

        visible_creatures.push_back({&creature, distance_squared});
    }

    std::sort(visible_creatures.begin(), visible_creatures.end(), [](const VisibleCreature& lhs, const VisibleCreature& rhs) {
        return lhs.distance_squared < rhs.distance_squared;
    });
    if (visible_creatures.size() > kCreatureMaxRenderedCount) {
        visible_creatures.resize(kCreatureMaxRenderedCount);
    }

    if (visible_crew.capacity() < crew.size()) {
        visible_crew.reserve(crew.size());
    }
    for (const auto& crew_member : crew) {
        const auto dx = crew_member.position.x - focus.x;
        const auto dz = crew_member.position.z - focus.z;
        const auto distance_squared = dx * dx + dz * dz;
        if (!std::isfinite(distance_squared) || distance_squared > crew_draw_distance_sq) {
            continue;
        }
        visible_crew.push_back({&crew_member, distance_squared});
    }
    std::sort(visible_crew.begin(), visible_crew.end(), [](const VisibleCrewMember& lhs, const VisibleCrewMember& rhs) {
        return lhs.distance_squared < rhs.distance_squared;
    });
    if (visible_crew.size() > kCrewVisualRenderCapacity) {
        visible_crew.resize(kCrewVisualRenderCapacity);
    }

    if (visible_old_guard.capacity() < old_guard.size()) {
        visible_old_guard.reserve(old_guard.size());
    }
    for (const auto& guard : old_guard) {
        const auto dx = guard.position.x - focus.x;
        const auto dz = guard.position.z - focus.z;
        const auto distance_squared = dx * dx + dz * dz;
        if (!std::isfinite(distance_squared) ||
            distance_squared > old_guard_draw_distance_sq) {
            continue;
        }
        visible_old_guard.push_back({&guard, distance_squared});
    }
    std::sort(
        visible_old_guard.begin(),
        visible_old_guard.end(),
        [](const VisibleOldGuardMember& left,
           const VisibleOldGuardMember& right) noexcept {
            return left.distance_squared < right.distance_squared;
        });
    if (visible_old_guard.size() > kOldGuardMemberCount) {
        visible_old_guard.resize(kOldGuardMemberCount);
    }

    if (visible_creatures.empty() &&
        visible_crew.empty() &&
        visible_old_guard.empty()) {
        return {};
    }

    const auto required_part_capacity =
        visible_creatures.size() * kCreatureMaxBoxBudget +
        visible_crew.size() * kCrewVisualPartBudget +
        visible_old_guard.size() * kOldGuardVisualPartBudget;
    if (parts.capacity() < required_part_capacity) {
        parts.reserve(required_part_capacity);
    }

    for (const auto& visible_creature : visible_creatures) {
        const auto creature_parts = build_creature_parts(*visible_creature.creature);
        if (creature_parts.empty()) {
            continue;
        }

        parts.insert(parts.end(), creature_parts.begin(), creature_parts.end());
    }
    for (const auto& visible_member : visible_crew) {
        append_crew_parts(parts, *visible_member.crew);
    }
    for (const auto& visible_member : visible_old_guard) {
        append_old_guard_parts(parts, *visible_member.guard);
    }
    return parts;
}

void Renderer::prepare_visual_entity_batches(
    std::span<const CreaturePartInstance> parts,
    VisualEntityContext context,
    const glm::vec3& focus,
    bool simplified_shadow,
    bool viewmodel) {

    for (auto& batch : visual_entity_batches_) {
        batch.clear();
    }

    for (const auto& part : parts) {
        const auto classification =
            classify_visual_entity_part(part, context);
        if (!classification.valid_transform) {
            continue;
        }

        auto lod = StylizedPrimitiveLod::Low;
        if (viewmodel) {
            lod = StylizedPrimitiveLod::High;
        } else if (!simplified_shadow) {
            const glm::vec3 position {part.transform[3]};
            const auto offset = position - focus;
            const auto distance_squared = glm::dot(offset, offset);
            if (std::isfinite(distance_squared) &&
                active_quality_settings_.terrain_lod_count >= 3 &&
                distance_squared <= 18.0F * 18.0F) {
                lod = StylizedPrimitiveLod::High;
            } else if (std::isfinite(distance_squared) &&
                       active_quality_settings_.terrain_lod_count >= 2 &&
                       distance_squared <= 56.0F * 56.0F) {
                lod = StylizedPrimitiveLod::Medium;
            }
        }

        const auto slot =
            visual_entity_batch_slot(classification.primitive, lod);
        auto visual_part = part;
        // Je compose uniquement le gabarit visuel dans le volume de la pièce.
        // La matrice du rig source, ses sockets et ses animations restent
        // strictement inchangés côté gameplay.
        visual_part.transform =
            part.transform * classification.primitive_to_part_local;
        visual_entity_batches_[slot].push_back(std::move(visual_part));
    }
}

void Renderer::draw_visual_entity_batches(
    GLuint instance_vbo,
    GLsizeiptr& instance_buffer_bytes) {

    glBindBuffer(GL_ARRAY_BUFFER, instance_vbo);
    for (std::size_t slot = 0U;
         slot < visual_entity_batches_.size();
         ++slot) {
        const auto& batch = visual_entity_batches_[slot];
        const auto& range = visual_entity_draw_ranges_[slot];
        if (batch.empty() || range.index_count <= 0) {
            continue;
        }

        const auto instance_bytes = static_cast<GLsizeiptr>(
            batch.size() * sizeof(CreaturePartInstance));
        if (instance_buffer_bytes < instance_bytes) {
            instance_buffer_bytes = grow_buffer_capacity(
                instance_buffer_bytes,
                instance_bytes,
                kInitialCreatureInstanceBufferBytes);
        }
        orphan_bound_buffer(GL_ARRAY_BUFFER, instance_buffer_bytes);
        glBufferSubData(
            GL_ARRAY_BUFFER,
            0,
            instance_bytes,
            batch.data());
        frame_uploaded_bytes_ += static_cast<std::uint64_t>(
            std::max<GLsizeiptr>(instance_bytes, 0));

        const auto two_sided =
            range.primitive == StylizedPrimitiveType::Panel ||
            range.primitive == StylizedPrimitiveType::Ribbon;
        if (two_sided) {
            glDisable(GL_CULL_FACE);
        } else {
            glEnable(GL_CULL_FACE);
        }
        const auto index_byte_offset =
            range.first_index * sizeof(std::uint32_t);
        glDrawElementsInstanced(
            GL_TRIANGLES,
            range.index_count,
            GL_UNSIGNED_INT,
            reinterpret_cast<const void*>(
                static_cast<std::uintptr_t>(index_byte_offset)),
            static_cast<GLsizei>(batch.size()));
        record_triangle_draw(
            range.index_count,
            static_cast<GLsizei>(batch.size()));
    }
    glEnable(GL_CULL_FACE);
}

void Renderer::draw_creature_shadows(std::span<const CreatureRenderInstance> creatures,
                                     std::span<const CrewRenderInstance> crew,
                                     std::span<const OldGuardRenderInstance> old_guard,
                                     const glm::mat4& light_view_projection,
                                     const glm::vec3& shadow_focus) {
    if ((creatures.empty() && crew.empty() && old_guard.empty()) ||
        creature_shadow_program_ == 0 || creature_vao_ == 0 ||
        creature_instance_vbo_ == 0 || creature_ebo_ == 0) {
        return;
    }

    const auto parts = collect_visible_creature_parts(
        creatures,
        crew,
        old_guard,
        shadow_focus,
        kShadowDistance + static_cast<float>(kChunkSizeX),
        kCrewVisualDrawDistance,
        kOldGuardRenderDistance);
    if (parts.empty()) {
        return;
    }

    glBindVertexArray(creature_vao_);
    if (options_.visual_pipeline == VisualPipeline::ModernStylized) {
        prepare_visual_entity_batches(
            parts,
            VisualEntityContext::Creature,
            shadow_focus,
            true,
            false);
    } else {
        glBindBuffer(GL_ARRAY_BUFFER, creature_instance_vbo_);
        const auto instance_bytes =
            static_cast<GLsizeiptr>(parts.size_bytes());
        if (creature_instance_buffer_bytes_ < instance_bytes) {
            creature_instance_buffer_bytes_ = grow_buffer_capacity(
                creature_instance_buffer_bytes_,
                instance_bytes,
                kInitialCreatureInstanceBufferBytes);
        }
        orphan_bound_buffer(
            GL_ARRAY_BUFFER,
            creature_instance_buffer_bytes_);
        glBufferSubData(
            GL_ARRAY_BUFFER,
            0,
            instance_bytes,
            parts.data());
        frame_uploaded_bytes_ += static_cast<std::uint64_t>(
            std::max<GLsizeiptr>(instance_bytes, 0));
    }

    glUseProgram(creature_shadow_program_);
    glUniformMatrix4fv(
        creature_shadow_light_view_projection_,
        1,
        GL_FALSE,
        glm::value_ptr(light_view_projection));
    if (options_.visual_pipeline == VisualPipeline::ModernStylized) {
        draw_visual_entity_batches(
            creature_instance_vbo_,
            creature_instance_buffer_bytes_);
    } else {
        glDrawElementsInstanced(
            GL_TRIANGLES,
            creature_template_index_count_,
            GL_UNSIGNED_INT,
            nullptr,
            static_cast<GLsizei>(parts.size()));
        record_triangle_draw(
            creature_template_index_count_,
            static_cast<GLsizei>(parts.size()));
    }
}

void Renderer::draw_creatures(std::span<const CreatureRenderInstance> creatures,
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
                              float super_vision_strength) {
    if ((creatures.empty() && crew.empty() && old_guard.empty()) ||
        creature_program_ == 0 || creature_vao_ == 0 ||
        creature_instance_vbo_ == 0 || creature_ebo_ == 0) {
        return;
    }

    const auto parts = collect_visible_creature_parts(
        creatures,
        crew,
        old_guard,
        camera_position,
        64.0F,
        kCrewVisualDrawDistance,
        kOldGuardRenderDistance);

    if (parts.empty()) {
        return;
    }

    glBindVertexArray(creature_vao_);
    if (options_.visual_pipeline == VisualPipeline::ModernStylized) {
        prepare_visual_entity_batches(
            parts,
            VisualEntityContext::Creature,
            camera_position,
            false,
            false);
    } else {
        glBindBuffer(GL_ARRAY_BUFFER, creature_instance_vbo_);
        const auto instance_bytes = static_cast<GLsizeiptr>(
            parts.size() * sizeof(CreaturePartInstance));
        if (creature_instance_buffer_bytes_ < instance_bytes) {
            creature_instance_buffer_bytes_ = grow_buffer_capacity(
                creature_instance_buffer_bytes_,
                instance_bytes,
                kInitialCreatureInstanceBufferBytes);
        }
        orphan_bound_buffer(
            GL_ARRAY_BUFFER,
            creature_instance_buffer_bytes_);
        glBufferSubData(
            GL_ARRAY_BUFFER,
            0,
            instance_bytes,
            parts.data());
        frame_uploaded_bytes_ += static_cast<std::uint64_t>(
            std::max<GLsizeiptr>(instance_bytes, 0));
    }

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glUseProgram(creature_program_);
    glUniformMatrix4fv(creature_uniforms_.view_projection, 1, GL_FALSE, glm::value_ptr(view_projection));
    glUniformMatrix4fv(creature_uniforms_.light_view_projection, 1, GL_FALSE, glm::value_ptr(light_view_projection));
    glUniformMatrix4fv(
        creature_uniforms_.light_view_projection_far,
        1,
        GL_FALSE,
        glm::value_ptr(light_view_projection_far));
    glUniform3fv(creature_uniforms_.camera_position, 1, glm::value_ptr(camera_position));
    glUniform3fv(
        creature_uniforms_.camera_forward,
        1,
        glm::value_ptr(camera_forward));
    glUniform3fv(creature_uniforms_.sun_direction, 1, glm::value_ptr(environment.sun_direction));
    glUniform3fv(creature_uniforms_.sun_color, 1, glm::value_ptr(environment.sun_color));
    glUniform3fv(creature_uniforms_.ambient_color, 1, glm::value_ptr(environment.ambient_color));
    glUniform3fv(creature_uniforms_.fog_color, 1, glm::value_ptr(environment.fog_color));
    glUniform3fv(creature_uniforms_.distant_fog_color, 1, glm::value_ptr(environment.distant_fog_color));
    glUniform3fv(creature_uniforms_.horizon_glow_color, 1, glm::value_ptr(environment.horizon_glow_color));
    glUniform3fv(creature_uniforms_.night_tint_color, 1, glm::value_ptr(environment.night_tint_color));
    glUniform1f(creature_uniforms_.daylight_factor, environment.daylight_factor);
    glUniform1f(creature_uniforms_.sun_visibility, environment.sun_direction.y > 0.0F ? 1.0F : 0.0F);
    glUniform1f(creature_uniforms_.cloud_intensity, environment.cloud_intensity);
    glUniform1f(creature_uniforms_.cloud_shadow_strength, environment.cloud_shadow_strength);
    glUniform1f(creature_uniforms_.atmospheric_scatter_strength, environment.atmospheric_scatter_strength);
    glUniform1f(creature_uniforms_.height_fog_density, environment.height_fog_density);
    glUniform1f(creature_uniforms_.precipitation_intensity, environment.precipitation_intensity);
    glUniform1f(creature_uniforms_.storm_intensity, environment.storm_intensity);
    glUniform1f(creature_uniforms_.lightning_intensity, environment.lightning_intensity);
    glUniform1i(creature_uniforms_.atlas, 0);
    glUniform1i(creature_uniforms_.shadow_map, 1);
    glUniform1i(creature_uniforms_.shadow_map_far, 7);
    glUniform1i(
        creature_uniforms_.shadow_cascade_count,
        shadow_cascade_count);
    glUniform1f(
        creature_uniforms_.shadow_split_distance,
        shadow_split_distance);
    glUniform1f(
        creature_uniforms_.shadow_transition_width,
        shadow_transition_width);
    glUniform1i(creature_uniforms_.shadows_enabled, options_.shadows_enabled ? 1 : 0);
    glUniform1f(creature_uniforms_.time_of_day, environment.time_of_day);
    glUniform1f(creature_uniforms_.player_light_strength, player_light_active ? 1.0F : 0.0F);
    glUniform1f(creature_uniforms_.super_vision_strength, std::clamp(super_vision_strength, 0.0F, 1.0F));
    glUniform1i(
        creature_uniforms_.modern_pipeline,
        is_modern_visual_pipeline(options_.visual_pipeline) ? 1 : 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, creature_atlas_texture_);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, shadow_map_);
    glActiveTexture(GL_TEXTURE7);
    glBindTexture(GL_TEXTURE_2D, shadow_map_far_);
    if (options_.visual_pipeline == VisualPipeline::ModernStylized) {
        draw_visual_entity_batches(
            creature_instance_vbo_,
            creature_instance_buffer_bytes_);
        glCullFace(GL_BACK);
    } else {
        glDrawElementsInstanced(
            GL_TRIANGLES,
            creature_template_index_count_,
            GL_UNSIGNED_INT,
            nullptr,
            static_cast<GLsizei>(parts.size()));
        record_triangle_draw(
            creature_template_index_count_,
            static_cast<GLsizei>(parts.size()));
    }
    glActiveTexture(GL_TEXTURE0);
}

auto Renderer::draw_player_viewmodel(
    const PlayerController& player,
    BlockId held_item,
    const PlayerMusketView& player_musket,
    const glm::mat4& view_projection,
    const glm::mat4& light_view_projection,
    const glm::vec3& camera_position,
    const EnvironmentState& environment)
    -> PlayerViewModelPose {
    if (player.is_dead() || creature_program_ == 0 || viewmodel_vao_ == 0 || viewmodel_instance_vbo_ == 0 || creature_ebo_ == 0 || player_atlas_texture_ == 0) {
        return {};
    }

    const auto viewmodel =
        build_player_viewmodel_parts(
            player,
            held_item,
            player_musket);
    if (viewmodel.empty()) {
        return {};
    }

    glBindVertexArray(viewmodel_vao_);
    if (options_.visual_pipeline == VisualPipeline::ModernStylized) {
        prepare_visual_entity_batches(
            viewmodel.parts,
            VisualEntityContext::PlayerViewModel,
            camera_position,
            false,
            true);
    } else {
        glBindBuffer(GL_ARRAY_BUFFER, viewmodel_instance_vbo_);
        const auto instance_bytes = static_cast<GLsizeiptr>(
            viewmodel.parts.size() * sizeof(CreaturePartInstance));
        if (viewmodel_instance_buffer_bytes_ < instance_bytes) {
            viewmodel_instance_buffer_bytes_ = grow_buffer_capacity(
                viewmodel_instance_buffer_bytes_,
                instance_bytes,
                kInitialCreatureInstanceBufferBytes);
        }
        orphan_bound_buffer(
            GL_ARRAY_BUFFER,
            viewmodel_instance_buffer_bytes_);
        glBufferSubData(
            GL_ARRAY_BUFFER,
            0,
            instance_bytes,
            viewmodel.parts.data());
        frame_uploaded_bytes_ += static_cast<std::uint64_t>(
            std::max<GLsizeiptr>(instance_bytes, 0));
    }

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glUseProgram(creature_program_);
    glUniformMatrix4fv(creature_uniforms_.view_projection, 1, GL_FALSE, glm::value_ptr(view_projection));
    glUniformMatrix4fv(creature_uniforms_.light_view_projection, 1, GL_FALSE, glm::value_ptr(light_view_projection));
    glUniform3fv(creature_uniforms_.camera_position, 1, glm::value_ptr(camera_position));
    glUniform3fv(creature_uniforms_.sun_direction, 1, glm::value_ptr(environment.sun_direction));
    glUniform3fv(creature_uniforms_.sun_color, 1, glm::value_ptr(environment.sun_color));
    const auto viewmodel_ambient = glm::max(environment.ambient_color, glm::vec3 {0.22F, 0.22F, 0.24F});
    const auto viewmodel_fog = glm::mix(environment.fog_color, viewmodel_ambient, 0.80F);
    glUniform3fv(creature_uniforms_.ambient_color, 1, glm::value_ptr(viewmodel_ambient));
    glUniform3fv(creature_uniforms_.fog_color, 1, glm::value_ptr(viewmodel_fog));
    glUniform3fv(creature_uniforms_.distant_fog_color, 1, glm::value_ptr(viewmodel_fog));
    glUniform3fv(creature_uniforms_.horizon_glow_color, 1, glm::value_ptr(environment.horizon_glow_color));
    glUniform3fv(creature_uniforms_.night_tint_color, 1, glm::value_ptr(environment.night_tint_color));
    glUniform1f(creature_uniforms_.daylight_factor, std::max(environment.daylight_factor, 0.20F));
    glUniform1f(creature_uniforms_.sun_visibility, environment.sun_direction.y > 0.0F ? 1.0F : 0.0F);
    glUniform1f(creature_uniforms_.cloud_intensity, environment.cloud_intensity);
    glUniform1f(creature_uniforms_.cloud_shadow_strength, environment.cloud_shadow_strength);
    glUniform1f(creature_uniforms_.atmospheric_scatter_strength, environment.atmospheric_scatter_strength);
    glUniform1f(creature_uniforms_.height_fog_density, environment.height_fog_density);
    glUniform1f(creature_uniforms_.precipitation_intensity, environment.precipitation_intensity);
    glUniform1f(creature_uniforms_.storm_intensity, environment.storm_intensity);
    glUniform1f(creature_uniforms_.lightning_intensity, environment.lightning_intensity);
    glUniform1i(creature_uniforms_.atlas, 0);
    glUniform1i(creature_uniforms_.shadow_map, 1);
    glUniform1i(creature_uniforms_.shadows_enabled, 0);
    glUniform1f(creature_uniforms_.time_of_day, environment.time_of_day);
    glUniform1f(creature_uniforms_.player_light_strength, 0.0F);
    glUniform1f(creature_uniforms_.super_vision_strength, 0.0F);
    glUniform1i(
        creature_uniforms_.modern_pipeline,
        is_modern_visual_pipeline(options_.visual_pipeline) ? 1 : 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, player_atlas_texture_);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, shadow_map_);
    if (options_.visual_pipeline == VisualPipeline::ModernStylized) {
        draw_visual_entity_batches(
            viewmodel_instance_vbo_,
            viewmodel_instance_buffer_bytes_);
    } else {
        glDrawElementsInstanced(
            GL_TRIANGLES,
            creature_template_index_count_,
            GL_UNSIGNED_INT,
            nullptr,
            static_cast<GLsizei>(viewmodel.parts.size()));
        record_triangle_draw(
            creature_template_index_count_,
            static_cast<GLsizei>(viewmodel.parts.size()));
    }
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glActiveTexture(GL_TEXTURE0);
    return viewmodel.pose;
}

void Renderer::draw_block_break_overlay(
    const World& world,
    const PlayerController& player) {
    const auto& break_progress = player.block_break_progress();
    if (!break_progress.active || break_progress.progress <= 0.0F) {
        block_break_overlay_mesh_.opaque_index_count = 0;
        return;
    }

    upload_block_break_overlay_mesh(world, break_progress);
    if (block_break_overlay_mesh_.vao == 0 || block_break_overlay_mesh_.opaque_index_count == 0) {
        return;
    }

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glUseProgram(world_program_);
    glBindVertexArray(block_break_overlay_mesh_.vao);
    glDrawElements(GL_TRIANGLES, block_break_overlay_mesh_.opaque_index_count, GL_UNSIGNED_INT, nullptr);
    record_triangle_draw(block_break_overlay_mesh_.opaque_index_count);
    glDepthMask(GL_TRUE);
}

void Renderer::draw_hotbar(const PlayerController& player,
                           const HotbarState& hotbar,
                           const PlayerProgressionState& progression,
                           const EnvironmentState& /*environment*/,
                           int width,
                           int height) {
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
    const auto normalized_progression = sanitize_player_progression_state(progression);
    const auto experience_for_next_level = player_experience_for_next_level(normalized_progression.level);
    const auto level_progress = experience_for_next_level == 0ULL
                                    ? 1.0F
                                    : std::clamp(
                                          static_cast<float>(normalized_progression.experience) /
                                              static_cast<float>(experience_for_next_level),
                                          0.0F,
                                          1.0F);
    const auto level_progress_step = std::clamp(quantize_hud_value(level_progress, 128.0F), 0, 128);
    const auto visible_level_progress = static_cast<float>(level_progress_step) / 128.0F;
    const auto hud_layout =
        build_gameplay_hud_layout(
            width,
            height,
            hotbar,
            player_state.health,
            max_health,
            player_state.air_seconds,
            max_air,
            air_visible,
            visible_level_progress);

    HotbarHudCacheKey cache_key {};
    cache_key.hotbar = hotbar;
    cache_key.width = width;
    cache_key.height = height;
    cache_key.health_steps = quantize_hud_value(player_state.health, 16.0F);
    cache_key.air_steps = quantize_hud_value(player_state.air_seconds, 64.0F);
    cache_key.damage_flash_step = quantize_hud_value(damage_flash, 128.0F);
    cache_key.player_level = normalized_progression.level;
    cache_key.level_progress_step = level_progress_step;
    cache_key.air_visible = air_visible;
    cache_key.underwater = player_state.head_underwater;

    auto& cache = hotbar_cache_;
    auto& vertices = cache.vertices;
    const auto needs_rebuild = !cache.valid || cache.key != cache_key;
    if (needs_rebuild) {
        cache.valid = true;
        cache.key = cache_key;
        vertices.clear();
        vertices.reserve(24576U);

        const auto viewport_width = static_cast<float>(width);
        const auto viewport_height = static_cast<float>(height);
        const auto modern_hud =
            is_modern_visual_pipeline(
                options_.visual_pipeline);
        const auto draw_text_bottom = [&](float x,
                                          float bottom,
                                          float pixel_size,
                                          std::string_view text,
                                          const HudColor& color,
                                          bool centered = false) {
            append_pixel_text_bottom_left(
                vertices,
                viewport_width,
                viewport_height,
                x + pixel_size,
                bottom - pixel_size,
                pixel_size,
                text,
                {0.0F, 0.0F, 0.0F, 0.56F},
                centered);
            append_pixel_text_bottom_left(
                vertices,
                viewport_width,
                viewport_height,
                x,
                bottom,
                pixel_size,
                text,
                color,
                centered);
        };

        if (cache_key.underwater) {
            const auto overlay_edge = std::clamp(std::min(viewport_width, viewport_height) * 0.17F, 72.0F, 180.0F);
            append_hud_rect_top_left(vertices, viewport_width, viewport_height, 0.0F, 0.0F, viewport_width, viewport_height, {0.03F, 0.18F, 0.25F, 0.18F});
            append_hud_rect_top_left(vertices, viewport_width, viewport_height, 0.0F, 0.0F, viewport_width, overlay_edge, {0.12F, 0.42F, 0.46F, 0.08F});
            append_hud_rect_top_left(
                vertices,
                viewport_width,
                viewport_height,
                0.0F,
                viewport_height - overlay_edge,
                viewport_width,
                overlay_edge,
                {0.02F, 0.09F, 0.15F, 0.16F});
            append_hud_rect_top_left(vertices, viewport_width, viewport_height, 0.0F, 0.0F, overlay_edge, viewport_height, {0.02F, 0.11F, 0.17F, 0.10F});
            append_hud_rect_top_left(
                vertices,
                viewport_width,
                viewport_height,
                viewport_width - overlay_edge,
                0.0F,
                overlay_edge,
                viewport_height,
                {0.02F, 0.11F, 0.17F, 0.10F});
        }

        if (damage_flash > 0.0F) {
            const auto edge_size = std::clamp(std::min(viewport_width, viewport_height) * 0.09F, 28.0F, 72.0F);
            append_hud_rect_top_left(vertices, viewport_width, viewport_height, 0.0F, 0.0F, viewport_width, viewport_height, {0.44F, 0.03F, 0.05F, damage_flash * 0.12F});
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

        const auto dock_palette =
            modern_hud
                ? make_modern_neutral_panel_palette()
                : make_slate_panel_palette();
        const auto rail_palette =
            modern_hud
                ? make_modern_glass_panel_palette(
                      {0.83F, 0.67F, 0.34F, 1.0F},
                      0.16F)
                : make_warm_panel_palette(
                      {0.70F, 0.56F, 0.30F, 1.0F});
        const auto heart_panel_palette =
            modern_hud
                ? make_modern_glass_panel_palette(
                      {0.94F, 0.30F, 0.38F, 1.0F},
                      0.18F)
                : make_warm_panel_palette(
                      {0.90F, 0.28F, 0.32F, 1.0F});
        const auto bubble_panel_palette =
            modern_hud
                ? make_modern_glass_panel_palette(
                      {0.42F, 0.80F, 0.98F, 1.0F},
                      0.16F)
                : make_warm_panel_palette(
                      {0.42F, 0.80F, 0.98F, 1.0F});
        const auto level_panel_palette =
            modern_hud
                ? make_modern_glass_panel_palette(
                      {0.88F, 0.72F, 0.35F, 1.0F},
                      0.17F)
                : make_warm_panel_palette(
                      {0.78F, 0.66F, 0.36F, 1.0F});

        if (modern_hud) {
            append_modern_panel_top_left(
                vertices,
                viewport_width,
                viewport_height,
                hud_layout.level.x,
                hud_layout.level.y,
                hud_layout.level.width,
                hud_layout.level.height,
                3.0F,
                level_panel_palette,
                true);
        } else {
            append_stylized_panel_top_left(
                vertices,
                viewport_width,
                viewport_height,
                hud_layout.level.x,
                hud_layout.level.y,
                hud_layout.level.width,
                hud_layout.level.height,
                3.0F,
                level_panel_palette,
                true);
        }
        append_hud_rect_top_left(
            vertices,
            viewport_width,
            viewport_height,
            hud_layout.level.progress_x,
            hud_layout.level.progress_y,
            hud_layout.level.progress_width,
            hud_layout.level.progress_height,
            {0.03F, 0.04F, 0.05F, 0.58F});
        append_hud_rect_top_left(
            vertices,
            viewport_width,
            viewport_height,
            hud_layout.level.progress_x,
            hud_layout.level.progress_y,
            hud_layout.level.progress_fill_width,
            hud_layout.level.progress_height,
            {0.97F, 0.78F, 0.35F, 0.92F});
        append_hud_rect_top_left(
            vertices,
            viewport_width,
            viewport_height,
            hud_layout.level.progress_x,
            hud_layout.level.progress_y,
            hud_layout.level.progress_fill_width,
            std::max(1.0F, hud_layout.level.progress_height * 0.32F),
            {1.0F, 0.96F, 0.74F, 0.24F});
        const auto level_label = std::string("LV ") + std::to_string(normalized_progression.level);
        draw_text_bottom(
            hud_layout.level.text_center_x,
            viewport_height - hud_layout.level.text_y - hud_layout.level.text_pixel_size * 7.0F,
            hud_layout.level.text_pixel_size,
            level_label,
            {0.98F, 0.96F, 0.88F, 0.98F},
            true);

        append_hud_shadow_bottom_left(
            vertices,
            viewport_width,
            viewport_height,
            hud_layout.hotbar_panel_x,
            hud_layout.hotbar_panel_bottom,
            hud_layout.hotbar_panel_width,
            hud_layout.hotbar_panel_height,
            14.0F,
            {0.0F, 0.0F, 0.0F, 0.24F});
        if (modern_hud) {
            append_modern_panel_bottom_left(
                vertices,
                viewport_width,
                viewport_height,
                hud_layout.hotbar_panel_x,
                hud_layout.hotbar_panel_bottom,
                hud_layout.hotbar_panel_width,
                hud_layout.hotbar_panel_height,
                4.0F,
                dock_palette,
                false);
            append_modern_panel_bottom_left(
                vertices,
                viewport_width,
                viewport_height,
                hud_layout.hotbar_rail_x,
                hud_layout.hotbar_rail_bottom,
                hud_layout.hotbar_rail_width,
                hud_layout.hotbar_rail_height,
                2.0F,
                rail_palette,
                false);
        } else {
            append_stylized_panel_bottom_left(
                vertices,
                viewport_width,
                viewport_height,
                hud_layout.hotbar_panel_x,
                hud_layout.hotbar_panel_bottom,
                hud_layout.hotbar_panel_width,
                hud_layout.hotbar_panel_height,
                4.0F,
                dock_palette,
                false);
            append_stylized_panel_bottom_left(
                vertices,
                viewport_width,
                viewport_height,
                hud_layout.hotbar_rail_x,
                hud_layout.hotbar_rail_bottom,
                hud_layout.hotbar_rail_width,
                hud_layout.hotbar_rail_height,
                2.0F,
                rail_palette,
                false);
        }
        append_hud_rect(
            vertices,
            viewport_width,
            viewport_height,
            hud_layout.hotbar_panel_x + 12.0F,
            hud_layout.hotbar_panel_bottom + hud_layout.hotbar_panel_height - 8.0F,
            std::max(0.0F, hud_layout.hotbar_panel_width - 24.0F),
            2.0F,
            {1.0F, 1.0F, 1.0F, 0.06F});

        if (modern_hud) {
            append_modern_panel_bottom_left(
                vertices,
                viewport_width,
                viewport_height,
                hud_layout.hearts_panel_x,
                hud_layout.hearts_panel_bottom,
                hud_layout.hearts_panel_width,
                hud_layout.hearts_panel_height,
                3.0F,
                heart_panel_palette,
                false);
        } else {
            append_stylized_panel_bottom_left(
                vertices,
                viewport_width,
                viewport_height,
                hud_layout.hearts_panel_x,
                hud_layout.hearts_panel_bottom,
                hud_layout.hearts_panel_width,
                hud_layout.hearts_panel_height,
                3.0F,
                heart_panel_palette,
                false);
        }
        if (hud_layout.air_visible) {
            if (modern_hud) {
                append_modern_panel_bottom_left(
                    vertices,
                    viewport_width,
                    viewport_height,
                    hud_layout.bubbles_panel_x,
                    hud_layout.bubbles_panel_bottom,
                    hud_layout.bubbles_panel_width,
                    hud_layout.bubbles_panel_height,
                    3.0F,
                    bubble_panel_palette,
                    false);
            } else {
                append_stylized_panel_bottom_left(
                    vertices,
                    viewport_width,
                    viewport_height,
                    hud_layout.bubbles_panel_x,
                    hud_layout.bubbles_panel_bottom,
                    hud_layout.bubbles_panel_width,
                    hud_layout.bubbles_panel_height,
                    3.0F,
                    bubble_panel_palette,
                    false);
            }
        }

        for (const auto& heart : hud_layout.hearts) {
            append_heart_glyph_bottom_left(vertices, viewport_width, viewport_height, heart);
        }
        if (hud_layout.air_visible) {
            for (const auto& bubble : hud_layout.bubbles) {
                append_bubble_glyph_bottom_left(vertices, viewport_width, viewport_height, bubble);
            }
        }

        const auto stack_pixel_size = std::max(2.0F, static_cast<float>(std::floor(hud_layout.hotbar.slot_size / 18.0F)));
        for (const auto& slot : hud_layout.slots) {
            const auto palette = build_slot_palette(slot.slot, slot.is_selected, false, true);
            if (modern_hud) {
                append_modern_slot_bottom_left(
                    vertices,
                    viewport_width,
                    viewport_height,
                    slot.x,
                    slot.bottom,
                    slot.size,
                    palette,
                    slot.has_icon);
            } else {
                append_stylized_slot_bottom_left(
                    vertices,
                    viewport_width,
                    viewport_height,
                    slot.x,
                    slot.bottom,
                    slot.size,
                    palette,
                    slot.has_icon);
            }

            if (!slot.has_icon) {
                continue;
            }

            const auto icon_texture_mode =
                hud_item_texture_mode(slot.slot.block_id);
            append_hud_quad(
                vertices,
                viewport_width,
                viewport_height,
                slot.icon_x,
                slot.icon_bottom,
                slot.icon_size,
                slot.icon_size,
                {1.0F, 1.0F, 1.0F, slot.is_selected ? 1.0F : 0.98F},
                icon_texture_mode > 2.5F
                    ? std::array<float, 4> {0.0F, 1.0F, 1.0F, 0.0F}
                    : atlas_uv_rect(slot.icon_tile),
                icon_texture_mode);
            if (slot.show_stack_count) {
                append_stack_count_bottom_left(
                    vertices,
                    viewport_width,
                    viewport_height,
                    slot.count_right_x,
                    slot.count_bottom,
                    stack_pixel_size,
                    slot.slot.count);
            }
        }

        const auto selected_label = item_stack_display_label(hotbar.selected_slot());
        if (!selected_label.empty()) {
            const auto label_padding_x = std::max(10.0F, hud_layout.label.pixel_size * 3.0F);
            const auto label_padding_y = std::max(6.0F, hud_layout.label.pixel_size * 2.0F);
            const auto label_width = measure_pixel_text(selected_label, hud_layout.label.pixel_size) + label_padding_x * 2.0F;
            const auto label_height = hud_layout.label.height + label_padding_y * 2.0F;
            const auto label_x = hud_layout.label.center_x - label_width * 0.5F;
            const auto label_y = bottom_to_top_left_y(viewport_height, hud_layout.label.bottom, label_height);
            const auto label_accent =
                ui_material_accent(
                    hotbar.selected_slot().block_id);
            const auto label_palette =
                modern_hud
                    ? make_modern_glass_panel_palette(
                          label_accent,
                          0.18F)
                    : make_warm_panel_palette(
                          label_accent);
            if (modern_hud) {
                append_modern_panel_top_left(
                    vertices,
                    viewport_width,
                    viewport_height,
                    label_x,
                    label_y,
                    label_width,
                    label_height,
                    3.0F,
                    label_palette,
                    true);
            } else {
                append_stylized_panel_top_left(
                    vertices,
                    viewport_width,
                    viewport_height,
                    label_x,
                    label_y,
                    label_width,
                    label_height,
                    3.0F,
                    label_palette,
                    true);
            }
            draw_text_bottom(
                hud_layout.label.center_x,
                hud_layout.label.bottom + label_padding_y - 1.0F,
                hud_layout.label.pixel_size,
                selected_label,
                {0.98F, 0.98F, 0.96F, 0.98F},
                true);
        }
    }

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glUseProgram(hud_program_);
    bind_hud_textures();

    upload_hud_vertices(vertices);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size()));

    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
}

void Renderer::draw_maritime_hud(const MaritimeHudView& maritime_hud, int width, int height) {
    if (!maritime_hud.visible || width <= 0 || height <= 0 || hud_program_ == 0 || hud_vao_ == 0 || hud_vbo_ == 0) {
        return;
    }

    const auto viewport_width = static_cast<float>(width);
    const auto viewport_height = static_cast<float>(height);
    const auto layout = build_maritime_hud_layout(
        width,
        height,
        maritime_hud.visible,
        maritime_hud.hunger_ratio,
        maritime_hud.thirst_ratio,
        maritime_hud.stamina_ratio,
        maritime_hud.fishing_active,
        maritime_hud.fishing_ratio,
        maritime_hud.crew_focus_visible,
        maritime_hud.crew_progress_ratio);

    MaritimeHudCacheKey cache_key {};
    cache_key.width = width;
    cache_key.height = height;
    cache_key.visible = maritime_hud.visible;
    cache_key.on_ship = maritime_hud.on_ship;
    cache_key.fishing_active = maritime_hud.fishing_active;
    cache_key.danger = maritime_hud.danger;
    cache_key.moored = maritime_hud.moored;
    cache_key.departing = maritime_hud.departing;
    cache_key.hunger_step = quantize_hud_value(maritime_hud.hunger_ratio, 100.0F);
    cache_key.thirst_step = quantize_hud_value(maritime_hud.thirst_ratio, 100.0F);
    cache_key.stamina_step = quantize_hud_value(maritime_hud.stamina_ratio, 100.0F);
    cache_key.fishing_step = quantize_hud_value(maritime_hud.fishing_ratio, 100.0F);
    cache_key.ship_distance_step = quantize_hud_value(maritime_hud.ship_distance, 0.2F);
    cache_key.ship_speed_step = quantize_hud_value(maritime_hud.ship_speed, 10.0F);
    cache_key.departure_seconds_step = static_cast<int>(std::ceil(
        std::max(0.0F, maritime_hud.departure_seconds_remaining)));
    cache_key.food_rations = maritime_hud.food_rations;
    cache_key.water_flasks = maritime_hud.water_flasks;
    cache_key.fish = maritime_hud.fish;
    cache_key.crew_focus_visible = maritime_hud.crew_focus_visible;
    cache_key.crew_moving = maritime_hud.crew_moving;
    cache_key.crew_blocked = maritime_hud.crew_blocked;
    cache_key.crew_knocked_out = maritime_hud.crew_knocked_out;
    cache_key.crew_has_progress = maritime_hud.crew_has_progress;
    cache_key.crew_role = maritime_hud.crew_role;
    cache_key.crew_activity = maritime_hud.crew_activity;
    cache_key.crew_cargo = maritime_hud.crew_cargo;
    cache_key.crew_destination = maritime_hud.crew_destination;
    cache_key.crew_progress_step = quantize_hud_value(maritime_hud.crew_progress_ratio, 100.0F);
    cache_key.crew_health_step = quantize_hud_value(maritime_hud.crew_health_ratio, 100.0F);
    cache_key.crew_distance_step = quantize_hud_value(maritime_hud.crew_distance, 10.0F);

    auto& cache = maritime_cache_;
    auto& vertices = cache.vertices;
    const auto needs_rebuild = !cache.valid || cache.key != cache_key;
    if (needs_rebuild) {
        cache.valid = true;
        cache.key = cache_key;
        vertices.clear();
        vertices.reserve(4096U);

        const auto accent = maritime_hud.danger ? HudColor {0.96F, 0.38F, 0.28F, 1.0F} : HudColor {0.38F, 0.78F, 1.0F, 1.0F};
        auto palette = make_warm_panel_palette(accent);
        palette.fill = {0.04F, 0.08F, 0.10F, 0.84F};
        palette.frame = {0.04F, 0.12F, 0.16F, 0.94F};
        palette.trim = hud_with_alpha(accent, 0.34F);

        const auto draw_text = [&](float x, float y, float pixel_size, std::string_view text, const HudColor& color, bool centered = false) {
            append_pixel_text(vertices, viewport_width, viewport_height, x + pixel_size, y + pixel_size, pixel_size, text, {0.0F, 0.0F, 0.0F, 0.45F}, centered);
            append_pixel_text(vertices, viewport_width, viewport_height, x, y, pixel_size, text, color, centered);
        };
        const auto draw_bar = [&](const MaritimeHudBarLayout& bar, const HudColor& fill_color) {
            append_hud_frame_top_left(
                vertices,
                viewport_width,
                viewport_height,
                bar.x,
                bar.y,
                bar.width,
                bar.height,
                1.5F,
                {0.0F, 0.0F, 0.0F, 0.72F},
                {0.02F, 0.03F, 0.04F, 0.72F});
            append_hud_rect_top_left(
                vertices,
                viewport_width,
                viewport_height,
                bar.x + 2.0F,
                bar.y + 2.0F,
                std::max(0.0F, bar.fill_width - 4.0F),
                std::max(1.0F, bar.height - 4.0F),
                fill_color);
        };

        append_stylized_panel_top_left(
            vertices,
            viewport_width,
            viewport_height,
            layout.panel_x,
            layout.panel_y,
            layout.panel_width,
            layout.panel_height,
            3.0F,
            palette,
            true);

        draw_text(layout.title_x, layout.title_y, layout.text_pixel_size, "AVENTURE EN MER", {0.94F, 0.98F, 1.0F, 0.98F});
        draw_bar(layout.hunger_bar, {0.86F, 0.48F, 0.22F, 0.92F});
        draw_bar(layout.thirst_bar, {0.30F, 0.70F, 1.0F, 0.92F});
        draw_bar(layout.stamina_bar, {0.40F, 0.92F, 0.52F, 0.92F});
        draw_text(layout.hunger_bar.x, layout.hunger_bar.y - 10.0F, layout.body_pixel_size, "FAIM", {0.88F, 0.88F, 0.84F, 0.92F});
        draw_text(layout.thirst_bar.x, layout.thirst_bar.y - 10.0F, layout.body_pixel_size, "SOIF", {0.88F, 0.88F, 0.84F, 0.92F});
        draw_text(layout.stamina_bar.x, layout.stamina_bar.y - 10.0F, layout.body_pixel_size, "ENDURANCE", {0.88F, 0.88F, 0.84F, 0.92F});

        auto status_text = std::string {};
        if (maritime_hud.moored) {
            status_text = layout.compact ? "QUAI " : "A QUAI - DEPART ";
            status_text += std::to_string(cache_key.departure_seconds_step) + "S";
        } else if (maritime_hud.departing) {
            status_text = layout.compact ? "DEPART " : "DEPART EN COURS ";
            status_text += std::to_string(cache_key.departure_seconds_step) + "S";
        } else {
            status_text = maritime_hud.on_ship
                              ? (layout.compact ? std::string("BORD  ") : std::string("A BORD  "))
                              : (layout.compact ? std::string("MER  ") : std::string("A LA MER  "));
            status_text += std::to_string(static_cast<int>(std::round(maritime_hud.ship_distance))) + "M";
        }
        draw_text(
            layout.status_x,
            layout.status_y,
            layout.body_pixel_size,
            status_text,
            maritime_hud.danger ? HudColor {1.0F, 0.62F, 0.48F, 0.96F} : HudColor {0.72F, 0.88F, 0.96F, 0.94F});

        const auto cargo_text = layout.compact
                                    ? std::string("V ") + std::to_string(maritime_hud.food_rations) +
                                          "  E " + std::to_string(maritime_hud.water_flasks) +
                                          "  P " + std::to_string(maritime_hud.fish)
                                    : std::string("VIVRES ") + std::to_string(maritime_hud.food_rations) +
                                          "  EAU " + std::to_string(maritime_hud.water_flasks) +
                                          "  POISSONS " + std::to_string(maritime_hud.fish);
        draw_text(layout.cargo_x, layout.cargo_y, layout.body_pixel_size, cargo_text, {0.82F, 0.86F, 0.90F, 0.94F});

        if (maritime_hud.fishing_active) {
            draw_bar(layout.fishing_bar, {0.94F, 0.84F, 0.38F, 0.94F});
            draw_text(layout.fishing_bar.x, layout.fishing_bar.y - 10.0F, layout.body_pixel_size, "PECHE", {0.94F, 0.90F, 0.72F, 0.94F});
        }

        if (layout.crew_focus.visible) {
            // Le panneau contextuel rend l'intention du PNJ lisible sans ouvrir de menu.
            const auto focus_accent =
                maritime_hud.crew_knocked_out
                    ? HudColor {0.96F, 0.34F, 0.28F, 1.0F}
                    : (maritime_hud.crew_blocked
                           ? HudColor {0.98F, 0.74F, 0.28F, 1.0F}
                           : HudColor {0.34F, 0.88F, 0.94F, 1.0F});
            auto focus_palette = make_warm_panel_palette(focus_accent);
            focus_palette.fill = {0.025F, 0.055F, 0.070F, 0.90F};
            focus_palette.frame = {0.035F, 0.105F, 0.130F, 0.98F};
            focus_palette.trim = hud_with_alpha(focus_accent, 0.42F);

            append_stylized_panel_top_left(
                vertices,
                viewport_width,
                viewport_height,
                layout.crew_focus.panel_x,
                layout.crew_focus.panel_y,
                layout.crew_focus.panel_width,
                layout.crew_focus.panel_height,
                3.0F,
                focus_palette,
                true);

            auto title =
                maritime_hud.crew_role.empty()
                    ? std::string("MARIN")
                    : std::string(maritime_hud.crew_role);

            auto detail = std::string {};
            if (maritime_hud.crew_knocked_out) {
                detail = "ASSOMME - RECUPERATION";
            } else if (maritime_hud.crew_blocked) {
                detail = "PASSAGE OCCUPE - PATIENTE";
            } else if (maritime_hud.crew_moving) {
                if (!maritime_hud.crew_cargo.empty()) {
                    detail = std::string(maritime_hud.crew_cargo) + " VERS " +
                             std::string(maritime_hud.crew_destination);
                } else {
                    detail = "VERS ";
                    detail += maritime_hud.crew_destination;
                }
            } else {
                detail = maritime_hud.crew_activity;
            }

            auto status = std::to_string(static_cast<int>(std::round(maritime_hud.crew_distance)));
            status += "M  SANTE ";
            status += std::to_string(static_cast<int>(std::round(
                std::clamp(maritime_hud.crew_health_ratio, 0.0F, 1.0F) * 100.0F)));
            status += "%";

            draw_text(
                layout.crew_focus.title_x,
                layout.crew_focus.title_y,
                layout.text_pixel_size,
                title,
                {0.94F, 0.99F, 1.0F, 0.98F});
            // Les destinations les plus longues doivent rester dans le panneau
            // meme sur une petite fenetre. La reduction reste quantifiee par
            // quarts de pixel afin de conserver l'aspect net de la police bitmap.
            const auto detail_width_limit =
                layout.crew_focus.panel_width -
                2.0F * (layout.crew_focus.detail_x -
                        layout.crew_focus.panel_x);
            auto detail_pixel_size = layout.body_pixel_size;
            while (detail_pixel_size > 1.25F &&
                   measure_pixel_text(detail, detail_pixel_size) >
                       detail_width_limit) {
                detail_pixel_size -= 0.25F;
            }

            draw_text(
                layout.crew_focus.detail_x,
                layout.crew_focus.detail_y,
                detail_pixel_size,
                detail,
                maritime_hud.crew_blocked
                    ? HudColor {1.0F, 0.84F, 0.48F, 0.96F}
                    : HudColor {0.76F, 0.92F, 0.98F, 0.96F});
            draw_text(
                layout.crew_focus.status_x,
                layout.crew_focus.status_y,
                layout.body_pixel_size,
                status,
                {0.76F, 0.80F, 0.84F, 0.94F});

            if (maritime_hud.crew_has_progress) {
                draw_bar(layout.crew_focus.progress_bar, focus_accent);
            }
        }
    }

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glUseProgram(hud_program_);
    bind_hud_textures();

    upload_hud_vertices(vertices);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size()));

    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
}

void Renderer::draw_gameplay_announcement(const GameplayHudAnnouncementView& announcement, int width, int height) {
    if (!announcement.visible || width <= 0 || height <= 0 || hud_program_ == 0 || hud_vao_ == 0 || hud_vbo_ == 0) {
        return;
    }

    const auto viewport_width = static_cast<float>(width);
    const auto viewport_height = static_cast<float>(height);
    const auto progress = std::clamp(announcement.normalized_time, 0.0F, 1.0F);
    const auto fade_in = std::clamp(progress / 0.10F, 0.0F, 1.0F);
    const auto fade_out = std::clamp((1.0F - progress) / 0.22F, 0.0F, 1.0F);
    const auto alpha = std::min(fade_in, fade_out);
    if (alpha <= 0.01F) {
        return;
    }

    auto title_pixel_size = viewport_width < 720.0F ? 3.0F : 4.0F;
    auto detail_pixel_size = viewport_width < 720.0F ? 2.0F : 3.0F;
    const auto panel_max_width = std::max(180.0F, viewport_width - 40.0F);
    const auto padding_x = viewport_width < 720.0F ? 16.0F : 24.0F;
    while (title_pixel_size > 2.0F &&
           measure_pixel_text(announcement.title, title_pixel_size) > panel_max_width - padding_x * 2.0F) {
        title_pixel_size -= 1.0F;
    }
    while (detail_pixel_size > 2.0F &&
           measure_pixel_text(announcement.detail, detail_pixel_size) > panel_max_width - padding_x * 2.0F) {
        detail_pixel_size -= 1.0F;
    }

    const auto title_width = measure_pixel_text(announcement.title, title_pixel_size);
    const auto detail_width = measure_pixel_text(announcement.detail, detail_pixel_size);
    const auto panel_width = std::min(panel_max_width, std::max(title_width, detail_width) + padding_x * 2.0F);
    const auto panel_height = (detail_width > 0.0F ? 58.0F : 44.0F) + (viewport_width < 720.0F ? -8.0F : 0.0F);
    const auto panel_x = (viewport_width - panel_width) * 0.5F;
    const auto panel_y = std::max(18.0F, viewport_height * 0.055F);
    const auto accent = HudColor {0.34F, 0.92F, 1.0F, 1.0F};
    auto palette = make_warm_panel_palette(accent);
    palette.frame = hud_with_alpha(palette.frame, palette.frame[3] * alpha);
    palette.fill = hud_with_alpha(palette.fill, palette.fill[3] * alpha);
    palette.highlight = hud_with_alpha(palette.highlight, palette.highlight[3] * alpha);
    palette.shadow = hud_with_alpha(palette.shadow, palette.shadow[3] * alpha);
    palette.trim = hud_with_alpha(palette.trim, palette.trim[3] * alpha);

    auto& vertices = gameplay_announcement_vertices_scratch_;
    vertices.clear();
    if (vertices.capacity() < 1536U) {
        vertices.reserve(1536U);
    }
    append_stylized_panel_top_left(
        vertices,
        viewport_width,
        viewport_height,
        panel_x,
        panel_y,
        panel_width,
        panel_height,
        3.0F,
        palette,
        true);

    const auto draw_text = [&](float center_x, float y, float pixel_size, std::string_view text, const HudColor& color) {
        append_pixel_text(
            vertices,
            viewport_width,
            viewport_height,
            center_x + pixel_size,
            y + pixel_size,
            pixel_size,
            text,
            {0.0F, 0.0F, 0.0F, 0.48F * alpha},
            true);
        append_pixel_text(vertices, viewport_width, viewport_height, center_x, y, pixel_size, text, hud_with_alpha(color, color[3] * alpha), true);
    };

    const auto title_y = panel_y + (announcement.detail.empty() ? 15.0F : 12.0F);
    draw_text(panel_x + panel_width * 0.5F, title_y, title_pixel_size, announcement.title, {0.96F, 0.99F, 1.0F, 1.0F});
    if (!announcement.detail.empty()) {
        draw_text(
            panel_x + panel_width * 0.5F,
            title_y + title_pixel_size * 8.0F + 7.0F,
            detail_pixel_size,
            announcement.detail,
            {0.72F, 0.94F, 1.0F, 0.92F});
    }

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glUseProgram(hud_program_);
    bind_hud_textures();

    upload_hud_vertices(vertices);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size()));

    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
}

void Renderer::draw_command_console(
    const CommandConsoleView& command_console,
    int width,
    int height) {
    if (!command_console.visible ||
        width <= 0 ||
        height <= 0 ||
        hud_program_ == 0 ||
        hud_vao_ == 0 ||
        hud_vbo_ == 0) {
        return;
    }

    const auto viewport_width =
        static_cast<float>(width);
    const auto viewport_height =
        static_cast<float>(height);
    const auto layout =
        build_command_console_layout(
            width,
            height);
    const auto pixel_size =
        layout.text_pixel_size;
    constexpr std::string_view kPrompt = "> ";
    const auto horizontal_padding =
        std::max(8.0F, pixel_size * 4.0F);
    const auto available_text_width =
        std::max(
            pixel_size * 6.0F,
            layout.input_width -
                horizontal_padding * 2.0F -
                measure_pixel_text(
                    kPrompt,
                    pixel_size));
    const auto maximum_visible_characters =
        std::max<std::size_t>(
            static_cast<std::size_t>(
                std::floor(
                    available_text_width /
                    (pixel_size * 6.0F))),
            1U);
    const auto text_window =
        build_command_console_text_window(
            command_console.input,
            command_console.cursor_byte_offset,
            maximum_visible_characters);
    const auto visible_input =
        command_console.input.substr(
            text_window.start,
            text_window.length);

    auto& vertices =
        command_console_vertices_scratch_;
    vertices.clear();
    if (vertices.capacity() < 16'384U) {
        vertices.reserve(16'384U);
    }

    append_hud_shadow_top_left(
        vertices,
        viewport_width,
        viewport_height,
        layout.panel_x,
        layout.panel_y,
        layout.panel_width,
        layout.panel_height,
        14.0F,
        {0.0F, 0.0F, 0.0F, 0.26F});
    append_hud_rect_top_left(
        vertices,
        viewport_width,
        viewport_height,
        layout.panel_x,
        layout.panel_y,
        layout.panel_width,
        layout.panel_height,
        {0.04F, 0.28F, 0.32F, 0.48F});
    append_hud_rect_top_left(
        vertices,
        viewport_width,
        viewport_height,
        layout.panel_x + 1.5F,
        layout.panel_y + 1.5F,
        std::max(0.0F, layout.panel_width - 3.0F),
        std::max(0.0F, layout.panel_height - 3.0F),
        {0.01F, 0.025F, 0.03F, 0.55F});
    append_hud_rect_top_left(
        vertices,
        viewport_width,
        viewport_height,
        layout.panel_x + 2.0F,
        layout.panel_y + 2.0F,
        std::max(0.0F, layout.panel_width - 4.0F),
        2.0F,
        {0.30F, 0.92F, 0.98F, 0.84F});
    append_hud_rect_top_left(
        vertices,
        viewport_width,
        viewport_height,
        layout.input_x,
        layout.input_y,
        layout.input_width,
        layout.input_height,
        {0.10F, 0.46F, 0.50F, 0.50F});
    append_hud_rect_top_left(
        vertices,
        viewport_width,
        viewport_height,
        layout.input_x + 1.5F,
        layout.input_y + 1.5F,
        std::max(0.0F, layout.input_width - 3.0F),
        std::max(0.0F, layout.input_height - 3.0F),
        {0.005F, 0.015F, 0.02F, 0.58F});

    const auto draw_text =
        [&](float x,
            float y,
            float size,
            std::string_view text,
            const HudColor& color) {
            append_pixel_text(
                vertices,
                viewport_width,
                viewport_height,
                x + size,
                y + size,
                size,
                text,
                {0.0F, 0.0F, 0.0F, 0.52F});
            append_pixel_text(
                vertices,
                viewport_width,
                viewport_height,
                x,
                y,
                size,
                text,
                color);
        };

    const auto title_size =
        layout.panel_width < 420.0F ? 1.5F : 2.0F;
    draw_text(
        layout.input_x,
        layout.panel_y + 11.0F,
        title_size,
        "CONSOLE DE COMMANDE",
        {0.62F, 0.94F, 0.98F, 0.96F});

    const auto input_text_y =
        layout.input_y +
        std::max(
            0.0F,
            (layout.input_height -
             pixel_size * 7.0F) *
                0.5F);
    const auto prompt_x =
        layout.input_x +
        horizontal_padding;
    draw_text(
        prompt_x,
        input_text_y,
        pixel_size,
        kPrompt,
        {0.38F, 0.94F, 0.72F, 0.98F});
    const auto input_text_x =
        prompt_x +
        measure_pixel_text(
            kPrompt,
            pixel_size);
    draw_text(
        input_text_x,
        input_text_y,
        pixel_size,
        visible_input,
        {0.92F, 0.98F, 1.0F, 0.98F});

    const auto cursor_prefix =
        visible_input.substr(
            0U,
            std::min(
                text_window.cursor_offset,
                visible_input.size()));
    const auto cursor_x =
        input_text_x +
        measure_pixel_text(
            cursor_prefix,
            pixel_size);
    append_hud_rect_top_left(
        vertices,
        viewport_width,
        viewport_height,
        cursor_x,
        input_text_y,
        std::max(1.0F, pixel_size * 0.65F),
        pixel_size * 7.0F,
        {0.48F, 1.0F, 0.82F, 0.92F});

    const auto feedback =
        command_console.feedback.empty()
            ? std::string_view(
                  "ENTREE POUR VALIDER - ECHAP POUR FERMER")
            : command_console.feedback;
    const auto feedback_size =
        layout.panel_width < 420.0F ? 1.25F : 2.0F;
    draw_text(
        layout.input_x,
        layout.panel_y +
            layout.panel_height -
            feedback_size * 7.0F -
            9.0F,
        feedback_size,
        feedback,
        command_console.feedback_is_error
            ? HudColor {1.0F, 0.46F, 0.40F, 0.96F}
            : HudColor {0.48F, 0.94F, 0.72F, 0.94F});

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(
        GL_SRC_ALPHA,
        GL_ONE_MINUS_SRC_ALPHA);

    glUseProgram(hud_program_);
    bind_hud_textures();

    upload_hud_vertices(vertices);
    glDrawArrays(
        GL_TRIANGLES,
        0,
        static_cast<GLsizei>(
            vertices.size()));

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
        vertices.reserve(49152U);

        const auto draw_text = [&](float x,
                                   float y,
                                   float pixel_size,
                                   std::string_view text,
                                   const HudColor& color,
                                   bool centered = false) {
            append_pixel_text(
                vertices,
                viewport_width,
                viewport_height,
                x + pixel_size,
                y + pixel_size,
                pixel_size,
                text,
                {0.0F, 0.0F, 0.0F, 0.58F},
                centered);
            append_pixel_text(
                vertices,
                viewport_width,
                viewport_height,
                x,
                y,
                pixel_size,
                text,
                color,
                centered);
        };

        const auto title_pixel_size = std::clamp(static_cast<float>(std::floor(layout.slot_size / 12.0F)), 3.0F, 4.0F);
        const auto subtitle_pixel_size = std::clamp(static_cast<float>(std::floor(layout.slot_size / 18.0F)), 2.0F, 3.0F);
        const auto label_pixel_size = std::clamp(static_cast<float>(std::floor(layout.slot_size / 17.0F)), 2.0F, 3.0F);
        const auto body_pixel_size = std::max(2.0F, subtitle_pixel_size);
        const auto stack_pixel_size = std::max(2.0F, static_cast<float>(std::floor(layout.slot_size / 18.0F)));
        const auto focus_item = resolve_inventory_focus_item(inventory_menu, hotbar);
        const auto focus_accent =
            focus_item.has_item ? ui_material_accent(focus_item.slot.block_id) : HudColor {0.64F, 0.68F, 0.74F, 1.0F};

        const auto frame_palette = make_stone_panel_palette();
        const auto header_palette = make_header_panel_palette();
        const auto preview_palette = make_slate_panel_palette();
        const auto storage_palette = make_slate_panel_palette();
        const auto hotbar_palette = make_warm_panel_palette({0.70F, 0.56F, 0.30F, 1.0F});
        const auto footer_palette = make_slate_panel_palette();
        const auto detail_palette = make_warm_panel_palette(focus_accent);

        append_hud_rect_top_left(
            vertices,
            viewport_width,
            viewport_height,
            0.0F,
            0.0F,
            viewport_width,
            viewport_height,
            {0.02F, 0.03F, 0.04F, 0.66F});
        const auto vignette_edge = std::clamp(std::min(viewport_width, viewport_height) * 0.18F, 72.0F, 180.0F);
        append_hud_rect_top_left(vertices, viewport_width, viewport_height, 0.0F, 0.0F, viewport_width, vignette_edge, {0.08F, 0.10F, 0.12F, 0.10F});
        append_hud_rect_top_left(
            vertices,
            viewport_width,
            viewport_height,
            0.0F,
            viewport_height - vignette_edge,
            viewport_width,
            vignette_edge,
            {0.01F, 0.02F, 0.03F, 0.22F});
        append_hud_rect_top_left(vertices, viewport_width, viewport_height, 0.0F, 0.0F, vignette_edge, viewport_height, {0.01F, 0.02F, 0.03F, 0.12F});
        append_hud_rect_top_left(
            vertices,
            viewport_width,
            viewport_height,
            viewport_width - vignette_edge,
            0.0F,
            vignette_edge,
            viewport_height,
            {0.01F, 0.02F, 0.03F, 0.12F});

        append_hud_shadow_top_left(
            vertices,
            viewport_width,
            viewport_height,
            layout.panel_x,
            layout.panel_y,
            layout.panel_width,
            layout.panel_height,
            18.0F,
            {0.0F, 0.0F, 0.0F, 0.28F});
        append_stylized_panel_top_left(
            vertices,
            viewport_width,
            viewport_height,
            layout.panel_x,
            layout.panel_y,
            layout.panel_width,
            layout.panel_height,
            5.0F,
            frame_palette,
            false);

        append_stylized_panel_top_left(
            vertices,
            viewport_width,
            viewport_height,
            layout.header_panel_x,
            layout.header_panel_y,
            layout.header_panel_width,
            layout.header_panel_height,
            4.0F,
            header_palette,
            false);
        append_stylized_panel_top_left(
            vertices,
            viewport_width,
            viewport_height,
            layout.preview_panel_x,
            layout.preview_panel_y,
            layout.preview_panel_width,
            layout.preview_panel_height,
            3.0F,
            preview_palette,
            true);
        append_stylized_panel_top_left(
            vertices,
            viewport_width,
            viewport_height,
            layout.storage_panel_x,
            layout.storage_panel_y,
            layout.storage_panel_width,
            layout.storage_panel_height,
            3.0F,
            storage_palette,
            true);
        append_stylized_panel_top_left(
            vertices,
            viewport_width,
            viewport_height,
            layout.hotbar_panel_x,
            layout.hotbar_panel_y,
            layout.hotbar_panel_width,
            layout.hotbar_panel_height,
            3.0F,
            hotbar_palette,
            true);
        append_stylized_panel_top_left(
            vertices,
            viewport_width,
            viewport_height,
            layout.detail_panel_x,
            layout.detail_panel_y,
            layout.detail_panel_width,
            layout.detail_panel_height,
            3.0F,
            detail_palette,
            true);
        append_stylized_panel_top_left(
            vertices,
            viewport_width,
            viewport_height,
            layout.footer_panel_x,
            layout.footer_panel_y,
            layout.footer_panel_width,
            layout.footer_panel_height,
            3.0F,
            footer_palette,
            false);

        append_hud_scanlines_top_left(
            vertices,
            viewport_width,
            viewport_height,
            layout.panel_x + 10.0F,
            layout.panel_y + 10.0F,
            std::max(0.0F, layout.panel_width - 20.0F),
            std::max(0.0F, layout.panel_height - 20.0F),
            12.0F,
            {1.0F, 1.0F, 1.0F, 0.014F});
        append_hud_scanlines_top_left(
            vertices,
            viewport_width,
            viewport_height,
            layout.storage_panel_x + 8.0F,
            layout.storage_panel_y + 32.0F,
            std::max(0.0F, layout.storage_panel_width - 16.0F),
            std::max(0.0F, layout.storage_panel_height - 40.0F),
            10.0F,
            {1.0F, 1.0F, 1.0F, 0.018F});
        append_hud_scanlines_top_left(
            vertices,
            viewport_width,
            viewport_height,
            layout.hotbar_panel_x + 8.0F,
            layout.hotbar_panel_y + 32.0F,
            std::max(0.0F, layout.hotbar_panel_width - 16.0F),
            std::max(0.0F, layout.hotbar_panel_height - 40.0F),
            10.0F,
            {1.0F, 0.92F, 0.68F, 0.020F});
        append_hud_rect_top_left(
            vertices,
            viewport_width,
            viewport_height,
            layout.header_panel_x + 10.0F,
            layout.header_panel_y + 10.0F,
            4.0F,
            std::max(0.0F, layout.header_panel_height - 20.0F),
            {0.98F, 0.76F, 0.34F, 0.38F});
        append_hud_rect_top_left(
            vertices,
            viewport_width,
            viewport_height,
            layout.detail_panel_x + 8.0F,
            layout.detail_panel_y + 8.0F,
            3.0F,
            std::max(0.0F, layout.detail_panel_height - 16.0F),
            hud_with_alpha(focus_accent, 0.34F));

        append_hud_rect_top_left(
            vertices,
            viewport_width,
            viewport_height,
            layout.header_panel_x + 12.0F,
            layout.header_panel_y + layout.header_panel_height - 10.0F,
            std::max(0.0F, layout.header_panel_width - 24.0F),
            2.0F,
            {1.0F, 1.0F, 1.0F, 0.06F});
        append_hud_rect_top_left(
            vertices,
            viewport_width,
            viewport_height,
            layout.preview_panel_x + 10.0F,
            layout.preview_panel_y + 26.0F,
            std::max(0.0F, layout.preview_panel_width - 20.0F),
            2.0F,
            {0.86F, 0.90F, 0.96F, 0.08F});
        append_hud_rect_top_left(
            vertices,
            viewport_width,
            viewport_height,
            layout.storage_panel_x + 10.0F,
            layout.storage_panel_y + 26.0F,
            std::max(0.0F, layout.storage_panel_width - 20.0F),
            2.0F,
            {0.86F, 0.90F, 0.96F, 0.08F});
        append_hud_rect_top_left(
            vertices,
            viewport_width,
            viewport_height,
            layout.hotbar_panel_x + 10.0F,
            layout.hotbar_panel_y + 26.0F,
            std::max(0.0F, layout.hotbar_panel_width - 20.0F),
            2.0F,
            {1.0F, 0.90F, 0.66F, 0.12F});
        append_hud_rect_top_left(
            vertices,
            viewport_width,
            viewport_height,
            layout.detail_panel_x + 10.0F,
            layout.detail_panel_y + 26.0F,
            std::max(0.0F, layout.detail_panel_width - 20.0F),
            2.0F,
            hud_with_alpha(hud_scale_rgb(focus_accent, 1.14F), 0.18F));
        append_hud_rect_top_left(
            vertices,
            viewport_width,
            viewport_height,
            layout.footer_panel_x + 10.0F,
            layout.footer_panel_y + 8.0F,
            std::max(0.0F, layout.footer_panel_width - 20.0F),
            1.0F,
            {1.0F, 1.0F, 1.0F, 0.05F});

        const auto title_y = layout.header_panel_y + 12.0F;
        const auto subtitle_y = title_y + title_pixel_size * 7.0F + 4.0F;
        draw_text(
            layout.title_center_x,
            title_y,
            title_pixel_size,
            "INVENTAIRE",
            {0.98F, 0.98F, 0.99F, 1.0F},
            true);
        draw_text(
            layout.subtitle_center_x,
            subtitle_y,
            subtitle_pixel_size,
            "EQUIPEMENT ET STOCKAGE",
            {0.82F, 0.84F, 0.88F, 0.96F},
            true);
        draw_text(
            layout.preview_panel_x + layout.preview_panel_width * 0.5F,
            layout.preview_panel_y + 10.0F,
            label_pixel_size,
            "AVATAR",
            {0.90F, 0.92F, 0.96F, 0.98F},
            true);
        draw_text(
            layout.equipment_label_x,
            layout.equipment_label_y,
            label_pixel_size,
            "EQUIPEMENT",
            {0.90F, 0.92F, 0.96F, 0.96F},
            true);
        draw_text(
            layout.storage_label_x,
            layout.storage_label_y,
            label_pixel_size,
            "STOCKAGE",
            {0.90F, 0.92F, 0.96F, 0.98F});
        draw_text(
            layout.hotbar_label_x,
            layout.hotbar_label_y,
            label_pixel_size,
            "BARRE RAPIDE",
            {0.98F, 0.94F, 0.84F, 0.98F});
        draw_text(
            layout.detail_label_x,
            layout.detail_label_y,
            label_pixel_size,
            "DETAIL",
            hud_scale_rgb(focus_accent, 1.18F));

        append_avatar_preview_art(vertices, viewport_width, viewport_height, layout);
        const auto preview_caption_y =
            layout.preview_panel_y + layout.preview_panel_height - body_pixel_size * 7.0F - std::max(10.0F, layout.slot_size * 0.20F);
        draw_text(
            layout.preview_panel_x + layout.preview_panel_width * 0.5F,
            preview_caption_y,
            body_pixel_size,
            "MODELE JOUEUR",
            {0.76F, 0.79F, 0.84F, 0.88F},
            true);

        for (const auto& keycap : layout.hotbar_keycaps) {
            append_keycap_top_left(vertices, viewport_width, viewport_height, keycap, body_pixel_size);
        }

        for (const auto& slot : layout.slots) {
            const auto palette = build_slot_palette(slot.slot, slot.is_selected_hotbar, slot.hovered, slot.is_hotbar);
            append_stylized_slot_top_left(
                vertices,
                viewport_width,
                viewport_height,
                slot.x,
                slot.y,
                slot.size,
                palette,
                slot.has_icon);

            if (!slot.has_icon) {
                continue;
            }

            const auto icon_size = std::max(8.0F, slot.size - layout.icon_inset * 2.0F);
            const auto icon_offset = (slot.size - icon_size) * 0.5F;
            const auto icon_texture_mode =
                hud_item_texture_mode(slot.slot.block_id);
            append_hud_quad_top_left(
                vertices,
                viewport_width,
                viewport_height,
                slot.x + icon_offset,
                slot.y + icon_offset,
                icon_size,
                icon_size,
                {1.0F, 1.0F, 1.0F, 1.0F},
                icon_texture_mode > 2.5F
                    ? std::array<float, 4> {0.0F, 1.0F, 1.0F, 0.0F}
                    : atlas_uv_rect(slot.icon_tile),
                icon_texture_mode);
            append_stack_count(
                vertices,
                viewport_width,
                viewport_height,
                slot.x + slot.size - 4.0F,
                slot.y + slot.size - 4.0F,
                stack_pixel_size,
                slot.slot.count);
        }

        const auto detail_padding = std::max(10.0F, layout.slot_size * 0.26F);
        const auto detail_content_y = layout.detail_label_y + label_pixel_size * 7.0F + std::max(10.0F, layout.slot_size * 0.22F);
        const auto detail_slot_size = std::clamp(
            std::min(layout.detail_panel_width - detail_padding * 2.0F, layout.slot_size * (layout.compact_detail ? 1.20F : 1.52F)),
            layout.slot_size,
            layout.compact_detail ? 68.0F : 84.0F);
        const auto detail_slot_x = layout.detail_panel_x + (layout.detail_panel_width - detail_slot_size) * 0.5F;
        const auto detail_slot_y = detail_content_y;
        const auto detail_slot_palette = build_slot_palette(focus_item.slot, focus_item.has_item, false, false);
        append_stylized_slot_top_left(
            vertices,
            viewport_width,
            viewport_height,
            detail_slot_x,
            detail_slot_y,
            detail_slot_size,
            detail_slot_palette,
            focus_item.has_item);

        if (focus_item.has_item) {
            const auto icon_size = std::max(12.0F, detail_slot_size - detail_padding * 1.40F);
            const auto icon_offset = (detail_slot_size - icon_size) * 0.5F;
            const auto icon_texture_mode =
                hud_item_texture_mode(focus_item.slot.block_id);
            append_hud_quad_top_left(
                vertices,
                viewport_width,
                viewport_height,
                detail_slot_x + icon_offset,
                detail_slot_y + icon_offset,
                icon_size,
                icon_size,
                {1.0F, 1.0F, 1.0F, 1.0F},
                icon_texture_mode > 2.5F
                    ? std::array<float, 4> {0.0F, 1.0F, 1.0F, 0.0F}
                    : atlas_uv_rect(
                          inventory_slot_icon_tile(
                              focus_item.slot.block_id)),
                icon_texture_mode);
            append_stack_count(
                vertices,
                viewport_width,
                viewport_height,
                detail_slot_x + detail_slot_size - 4.0F,
                detail_slot_y + detail_slot_size - 4.0F,
                stack_pixel_size,
                focus_item.slot.count);
        }

        auto detail_name = focus_item.has_item ? std::string(inventory_item_label(focus_item.slot.block_id)) : std::string("SURVOLE UN OBJET");
        auto detail_name_pixel_size = std::clamp(label_pixel_size + (layout.compact_detail ? 0.0F : 1.0F), 2.0F, 4.0F);
        while (detail_name_pixel_size > 2.0F &&
               measure_pixel_text(detail_name, detail_name_pixel_size) > layout.detail_panel_width - detail_padding * 2.0F) {
            detail_name_pixel_size -= 1.0F;
        }
        const auto detail_name_y = detail_slot_y + detail_slot_size + std::max(10.0F, layout.slot_size * 0.18F);
        draw_text(
            layout.detail_panel_x + layout.detail_panel_width * 0.5F,
            detail_name_y,
            detail_name_pixel_size,
            detail_name,
            focus_item.has_item ? hud_scale_rgb(focus_accent, 1.18F) : HudColor {0.90F, 0.92F, 0.96F, 0.98F},
            true);

        const auto detail_rule_y = detail_name_y + detail_name_pixel_size * 7.0F + 8.0F;
        append_hud_rect_top_left(
            vertices,
            viewport_width,
            viewport_height,
            layout.detail_panel_x + detail_padding,
            detail_rule_y,
            std::max(0.0F, layout.detail_panel_width - detail_padding * 2.0F),
            1.0F,
            hud_with_alpha(hud_scale_rgb(focus_accent, 1.10F), 0.16F));

        if (focus_item.has_item) {
            std::string pile_line = "PILE ";
            pile_line += std::to_string(static_cast<int>(focus_item.slot.count));
            pile_line += " SUR ";
            pile_line += std::to_string(static_cast<int>(max_item_stack_count(focus_item.slot.block_id)));

            const auto tool_material_count = inventory_available_tool_crafting_material(inventory_menu, hotbar);
            std::string material_line = "MATIERE ";
            material_line += item_material_label(focus_item.slot.block_id);
            if (const auto stats = weapon_stats(focus_item.slot.block_id); stats.has_value()) {
                material_line = "DEGATS ";
                material_line += std::to_string(static_cast<int>(std::round(stats->damage)));
                material_line += "  PORTEE ";
                material_line += std::to_string(static_cast<int>(std::round(stats->range)));
            } else if (const auto resistance = armor_resistance_percent(focus_item.slot.block_id); resistance > 0.0F) {
                material_line = "RESISTANCE +";
                material_line += std::to_string(static_cast<int>(std::round(resistance)));
                material_line += "%";
            } else if (is_tool_item(focus_item.slot.block_id)) {
                material_line = "OUTIL MINAGE";
            } else if (inventory_is_tool_crafting_material(focus_item.slot.block_id)) {
                material_line = "MATERIAU OUTILS";
            }

            std::string source_line = "SOURCE ";
            if (focus_item.from_carried_slot) {
                source_line += "MAIN";
            } else if (focus_item.group == InventorySlotGroup::Hotbar) {
                source_line += "BARRE";
            } else if (focus_item.group == InventorySlotGroup::Equipment) {
                source_line += "EQUIP";
            } else {
                source_line += inventory_slot_group_label(focus_item.group);
            }
            if (is_tool_item(focus_item.slot.block_id) ||
                inventory_is_tool_crafting_material(focus_item.slot.block_id)) {
                source_line = "STOCK BOIS ";
                source_line += std::to_string(static_cast<int>(tool_material_count));
            }

            const auto info_y = detail_rule_y + 8.0F;
            const auto line_step = body_pixel_size * 7.0F + 6.0F;
            draw_text(
                layout.detail_panel_x + layout.detail_panel_width * 0.5F,
                info_y,
                body_pixel_size,
                pile_line,
                {0.96F, 0.97F, 0.98F, 0.96F},
                true);
            draw_text(
                layout.detail_panel_x + layout.detail_panel_width * 0.5F,
                info_y + line_step,
                body_pixel_size,
                material_line,
                {0.84F, 0.86F, 0.90F, 0.94F},
                true);
            draw_text(
                layout.detail_panel_x + layout.detail_panel_width * 0.5F,
                info_y + line_step * 2.0F,
                body_pixel_size,
                source_line,
                {0.84F, 0.86F, 0.90F, 0.94F},
                true);
        } else {
            draw_text(
                layout.detail_panel_x + layout.detail_panel_width * 0.5F,
                detail_rule_y + 10.0F,
                body_pixel_size,
                "OU PRENDS EN UN",
                {0.76F, 0.79F, 0.84F, 0.90F},
                true);
        }

        for (const auto& hint : layout.footer_hints) {
            const auto hint_accent = hint.emphasized ? HudColor {0.96F, 0.78F, 0.36F, 1.0F} : HudColor {0.52F, 0.74F, 0.92F, 1.0F};
            append_stylized_panel_top_left(
                vertices,
                viewport_width,
                viewport_height,
                hint.x,
                hint.y,
                hint.width,
                hint.height,
                2.0F,
                hint.emphasized ? make_warm_panel_palette(hint_accent) : make_slate_panel_palette(),
                false);
            append_hud_rect_top_left(
                vertices,
                viewport_width,
                viewport_height,
                hint.x + 4.0F,
                hint.y + 4.0F,
                3.0F,
                std::max(0.0F, hint.height - 8.0F),
                hud_with_alpha(hint_accent, hint.emphasized ? 0.42F : 0.24F));

            auto hint_pixel_size = subtitle_pixel_size;
            while (hint_pixel_size > 2.0F &&
                   measure_pixel_text(hint.label, hint_pixel_size) > hint.width - 18.0F) {
                hint_pixel_size -= 1.0F;
            }
            const auto hint_text_y = hint.y + std::max(0.0F, (hint.height - hint_pixel_size * 7.0F) * 0.5F);
            draw_text(
                hint.x + hint.width * 0.5F,
                hint_text_y,
                hint_pixel_size,
                hint.label,
                hint.emphasized ? HudColor {0.99F, 0.96F, 0.86F, 0.98F} : HudColor {0.82F, 0.86F, 0.92F, 0.96F},
                true);
        }

        std::string tooltip_label;
        auto tooltip_accent = focus_accent;
        if (inventory_menu.carrying_item && inventory_slot_has_item(inventory_menu.carried_slot)) {
            tooltip_label = item_stack_display_label(inventory_menu.carried_slot);
            tooltip_accent = ui_material_accent(inventory_menu.carried_slot.block_id);
        } else if (inventory_menu.hovered_slot.has_value()) {
            if (const auto* slot = inventory_slot_ptr(inventory_menu, hotbar, *inventory_menu.hovered_slot);
                slot != nullptr && inventory_slot_has_item(*slot)) {
                tooltip_label = item_stack_display_label(*slot);
                tooltip_accent = ui_material_accent(slot->block_id);
            }
        }

        if (!tooltip_label.empty()) {
            const auto tooltip_pixel_size = subtitle_pixel_size;
            const auto tooltip_padding_x = std::max(8.0F, tooltip_pixel_size * 2.8F);
            const auto tooltip_padding_y = std::max(6.0F, tooltip_pixel_size * 2.0F);
            const auto tooltip_width = measure_pixel_text(tooltip_label, tooltip_pixel_size) + tooltip_padding_x * 2.0F;
            const auto tooltip_height = tooltip_pixel_size * 7.0F + tooltip_padding_y * 2.0F;
            const auto tooltip_x = std::clamp(
                inventory_menu.cursor_x + 18.0F,
                layout.panel_x + 12.0F,
                layout.panel_x + layout.panel_width - tooltip_width - 12.0F);
            const auto tooltip_y = std::clamp(
                inventory_menu.cursor_y + 18.0F,
                layout.panel_y + 12.0F,
                layout.panel_y + layout.panel_height - tooltip_height - 12.0F);
            append_stylized_panel_top_left(
                vertices,
                viewport_width,
                viewport_height,
                tooltip_x,
                tooltip_y,
                tooltip_width,
                tooltip_height,
                3.0F,
                make_warm_panel_palette(tooltip_accent),
                true);
            draw_text(
                tooltip_x + tooltip_width * 0.5F,
                tooltip_y + tooltip_padding_y,
                tooltip_pixel_size,
                tooltip_label,
                {0.98F, 0.98F, 0.96F, 0.98F},
                true);
        }

        if (inventory_menu.carrying_item && inventory_slot_has_item(inventory_menu.carried_slot)) {
            const auto carried_size = layout.slot_size;
            const auto carried_x = inventory_menu.cursor_x - carried_size * 0.5F;
            const auto carried_y = inventory_menu.cursor_y - carried_size * 0.5F;
            const auto carried_palette = build_slot_palette(inventory_menu.carried_slot, true, false, false);
            append_stylized_slot_top_left(
                vertices,
                viewport_width,
                viewport_height,
                carried_x,
                carried_y,
                carried_size,
                carried_palette,
                true);

            const auto icon_size = std::max(8.0F, carried_size - layout.icon_inset * 2.0F);
            const auto icon_offset = (carried_size - icon_size) * 0.5F;
            const auto icon_texture_mode =
                hud_item_texture_mode(
                    inventory_menu.carried_slot.block_id);
            append_hud_quad_top_left(
                vertices,
                viewport_width,
                viewport_height,
                carried_x + icon_offset,
                carried_y + icon_offset,
                icon_size,
                icon_size,
                {1.0F, 1.0F, 1.0F, 1.0F},
                icon_texture_mode > 2.5F
                    ? std::array<float, 4> {0.0F, 1.0F, 1.0F, 0.0F}
                    : atlas_uv_rect(
                          inventory_slot_icon_tile(
                              inventory_menu.carried_slot.block_id)),
                icon_texture_mode);
            append_stack_count(
                vertices,
                viewport_width,
                viewport_height,
                carried_x + carried_size - 4.0F,
                carried_y + carried_size - 4.0F,
                stack_pixel_size,
                inventory_menu.carried_slot.count);
        }
    }

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glUseProgram(hud_program_);
    bind_hud_textures();

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
    bind_hud_textures();

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
        vertices.reserve(16384U);

        const auto draw_text = [&](float x,
                                   float y,
                                   float pixel_size,
                                   std::string_view text,
                                   const HudColor& color,
                                   bool centered = false) {
            append_pixel_text(
                vertices,
                viewport_width,
                viewport_height,
                x + pixel_size,
                y + pixel_size,
                pixel_size,
                text,
                {0.0F, 0.0F, 0.0F, 0.58F},
                centered);
            append_pixel_text(
                vertices,
                viewport_width,
                viewport_height,
                x,
                y,
                pixel_size,
                text,
                color,
                centered);
        };

        const auto primary_accent = HudColor {0.96F, 0.74F, 0.32F, 1.0F};
        const auto secondary_accent = HudColor {0.34F, 0.72F, 0.92F, 1.0F};
        const auto title_pixel_size = static_cast<float>(std::floor(std::clamp(layout.panel_width / 100.0F, 4.0F, 5.0F)));
        const auto subtitle_pixel_size = static_cast<float>(std::floor(std::clamp(layout.panel_width / 160.0F, 2.0F, 3.0F)));

        append_hud_rect_top_left(
            vertices,
            viewport_width,
            viewport_height,
            0.0F,
            0.0F,
            viewport_width,
            viewport_height,
            {0.02F, 0.02F, 0.03F, 0.66F});
        const auto vignette_edge = std::clamp(std::min(viewport_width, viewport_height) * 0.22F, 72.0F, 210.0F);
        append_hud_rect_top_left(vertices, viewport_width, viewport_height, 0.0F, 0.0F, viewport_width, vignette_edge, {0.10F, 0.10F, 0.12F, 0.10F});
        append_hud_rect_top_left(vertices, viewport_width, viewport_height, 0.0F, viewport_height - vignette_edge, viewport_width, vignette_edge, {0.0F, 0.0F, 0.02F, 0.24F});
        append_hud_rect_top_left(vertices, viewport_width, viewport_height, 0.0F, 0.0F, vignette_edge, viewport_height, {0.0F, 0.0F, 0.02F, 0.12F});
        append_hud_rect_top_left(vertices, viewport_width, viewport_height, viewport_width - vignette_edge, 0.0F, vignette_edge, viewport_height, {0.0F, 0.0F, 0.02F, 0.12F});

        append_hud_shadow_top_left(
            vertices,
            viewport_width,
            viewport_height,
            layout.panel_x,
            layout.panel_y,
            layout.panel_width,
            layout.panel_height,
            20.0F,
            {0.0F, 0.0F, 0.0F, 0.32F});
        append_stylized_panel_top_left(
            vertices,
            viewport_width,
            viewport_height,
            layout.panel_x,
            layout.panel_y,
            layout.panel_width,
            layout.panel_height,
            5.0F,
            make_stone_panel_palette(),
            false);
        append_hud_scanlines_top_left(
            vertices,
            viewport_width,
            viewport_height,
            layout.panel_x + 8.0F,
            layout.panel_y + 8.0F,
            std::max(0.0F, layout.panel_width - 16.0F),
            std::max(0.0F, layout.panel_height - 16.0F),
            12.0F,
            {1.0F, 1.0F, 1.0F, 0.018F});
        append_hud_rect_top_left(
            vertices,
            viewport_width,
            viewport_height,
            layout.accent_rail_x,
            layout.accent_rail_y,
            layout.accent_rail_width,
            layout.accent_rail_height,
            hud_with_alpha(primary_accent, 0.52F));
        append_hud_rect_top_left(
            vertices,
            viewport_width,
            viewport_height,
            layout.accent_rail_x + layout.accent_rail_width + 3.0F,
            layout.accent_rail_y,
            std::max(2.0F, layout.accent_rail_width * 0.70F),
            layout.accent_rail_height,
            hud_with_alpha(secondary_accent, 0.18F));

        append_stylized_panel_top_left(
            vertices,
            viewport_width,
            viewport_height,
            layout.header_panel_x,
            layout.header_panel_y,
            layout.header_panel_width,
            layout.header_panel_height,
            4.0F,
            make_warm_panel_palette(primary_accent),
            false);
        append_stylized_panel_top_left(
            vertices,
            viewport_width,
            viewport_height,
            layout.footer_panel_x,
            layout.footer_panel_y,
            layout.footer_panel_width,
            layout.footer_panel_height,
            3.0F,
            make_slate_panel_palette(),
            false);

        append_hud_rect_top_left(
            vertices,
            viewport_width,
            viewport_height,
            layout.header_panel_x + 14.0F,
            layout.header_panel_y + layout.header_panel_height - 10.0F,
            std::max(0.0F, layout.header_panel_width - 28.0F),
            2.0F,
            hud_with_alpha(primary_accent, 0.20F));
        append_corner_brackets_top_left(
            vertices,
            viewport_width,
            viewport_height,
            layout.panel_x + 7.0F,
            layout.panel_y + 7.0F,
            std::max(0.0F, layout.panel_width - 14.0F),
            std::max(0.0F, layout.panel_height - 14.0F),
            4.0F,
            {1.0F, 1.0F, 1.0F, 0.08F});

        draw_text(
            layout.brand_center_x,
            layout.brand_y,
            subtitle_pixel_size,
            kGameDisplayNamePixel,
            {0.84F, 0.86F, 0.90F, 0.86F},
            true);
        draw_text(
            layout.title_center_x,
            layout.title_y,
            title_pixel_size,
            "JEU EN PAUSE",
            {0.99F, 0.98F, 0.94F, 1.0F},
            true);
        draw_text(
            layout.subtitle_center_x,
            layout.subtitle_y,
            subtitle_pixel_size,
            "SESSION SUSPENDUE",
            {0.86F, 0.88F, 0.92F, 0.94F},
            true);

        for (std::size_t index = 0; index < layout.buttons.size(); ++index) {
            const auto& button = layout.buttons[index];
            const auto selected_accent = button.selected ? primary_accent : secondary_accent;
            const auto button_palette = !button.enabled
                                            ? make_slate_panel_palette()
                                            : (button.selected ? make_warm_panel_palette(primary_accent) : make_slate_panel_palette());
            if (button.selected) {
                append_hud_shadow_top_left(
                    vertices,
                    viewport_width,
                    viewport_height,
                    button.x - 3.0F,
                    button.y - 3.0F,
                    button.width + 6.0F,
                    button.height + 6.0F,
                    7.0F,
                    hud_with_alpha(primary_accent, 0.16F));
                append_hud_rect_top_left(
                    vertices,
                    viewport_width,
                    viewport_height,
                    button.x - 3.0F,
                    button.y - 3.0F,
                    button.width + 6.0F,
                    button.height + 6.0F,
                    hud_with_alpha(primary_accent, 0.06F));
            }

            append_stylized_panel_top_left(
                vertices,
                viewport_width,
                viewport_height,
                button.x,
                button.y,
                button.width,
                button.height,
                4.0F,
                button_palette,
                true);
            append_hud_rect_top_left(
                vertices,
                viewport_width,
                viewport_height,
                button.x + 7.0F,
                button.y + 7.0F,
                4.0F,
                std::max(0.0F, button.height - 14.0F),
                hud_with_alpha(selected_accent, button.selected ? 0.68F : 0.22F));

            const auto chip_size = std::clamp(button.height - 18.0F, 18.0F, 28.0F);
            const auto chip_x = button.x + 18.0F;
            const auto chip_y = button.y + (button.height - chip_size) * 0.5F;
            append_stylized_panel_top_left(
                vertices,
                viewport_width,
                viewport_height,
                chip_x,
                chip_y,
                chip_size,
                chip_size,
                2.0F,
                button.selected ? make_warm_panel_palette(primary_accent) : make_slate_panel_palette(),
                false);

            const auto number_label = std::to_string(index + 1U);
            const auto chip_pixel_size = std::max(2.0F, subtitle_pixel_size);
            draw_text(
                chip_x + chip_size * 0.5F,
                chip_y + std::max(0.0F, (chip_size - chip_pixel_size * 7.0F) * 0.5F),
                chip_pixel_size,
                number_label,
                button.selected ? HudColor {0.99F, 0.96F, 0.84F, 1.0F} : HudColor {0.70F, 0.74F, 0.82F, 0.92F},
                true);

            auto button_pixel_size = static_cast<float>(std::floor(std::clamp(button.height / 12.0F, 3.0F, 4.0F)));
            const auto label_x = chip_x + chip_size + 16.0F;
            const auto label_max_width = std::max(24.0F, button.x + button.width - label_x - 34.0F);
            while (button_pixel_size > 2.0F && measure_pixel_text(button.label, button_pixel_size) > label_max_width) {
                button_pixel_size -= 1.0F;
            }
            const auto text_y = button.y + static_cast<float>(std::floor((button.height - button_pixel_size * 7.0F) * 0.5F));
            draw_text(
                label_x,
                text_y,
                button_pixel_size,
                button.label,
                !button.enabled
                    ? HudColor {0.58F, 0.60F, 0.66F, 0.72F}
                    : (button.selected ? HudColor {1.0F, 0.98F, 0.90F, 1.0F} : HudColor {0.90F, 0.92F, 0.96F, 0.96F}));

            if (button.selected) {
                draw_text(
                    button.x + button.width - 22.0F,
                    text_y,
                    button_pixel_size,
                    ">",
                    {0.99F, 0.86F, 0.48F, 0.96F});
            }
        }

        auto footer_text = std::string("ENTREE / ESPACE VALIDER    ECHAP REPRENDRE");
        auto footer_pixel_size = subtitle_pixel_size;
        while (footer_pixel_size > 2.0F &&
               measure_pixel_text(footer_text, footer_pixel_size) > layout.footer_panel_width - 20.0F) {
            footer_pixel_size -= 1.0F;
        }
        if (measure_pixel_text(footer_text, footer_pixel_size) > layout.footer_panel_width - 20.0F) {
            footer_text = "ENTREE VALIDER  ECHAP REPRENDRE";
        }
        draw_text(
            layout.footer_center_x,
            layout.footer_y,
            footer_pixel_size,
            footer_text,
            {0.80F, 0.83F, 0.88F, 0.94F},
            true);
    }

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glUseProgram(hud_program_);
    bind_hud_textures();

    upload_hud_vertices(vertices);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size()));

    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
}

void Renderer::draw_main_menu(const MainMenuState& main_menu, int width, int height) {
    if (width <= 0 || height <= 0 || hud_program_ == 0 || hud_vao_ == 0 || hud_vbo_ == 0) {
        return;
    }

    const auto layout = build_main_menu_layout(width, height, main_menu);
    const auto viewport_width = static_cast<float>(width);
    const auto viewport_height = static_cast<float>(height);
    MainMenuHudCacheKey cache_key {};
    cache_key.main_menu = main_menu;
    cache_key.width = width;
    cache_key.height = height;

    auto& cache = main_menu_cache_;
    auto& vertices = cache.vertices;
    const auto needs_rebuild = !cache.valid || cache.key != cache_key;
    if (needs_rebuild) {
        cache.valid = true;
        cache.key = cache_key;
        vertices.clear();
        vertices.reserve(16384U);

        const auto draw_text = [&](float x, float y, float pixel_size, std::string_view text, const HudColor& color, bool centered = false) {
            append_pixel_text(
                vertices,
                viewport_width,
                viewport_height,
                x + pixel_size,
                y + pixel_size,
                pixel_size,
                text,
                {0.0F, 0.0F, 0.0F, 0.45F},
                centered);
            append_pixel_text(
                vertices,
                viewport_width,
                viewport_height,
                x,
                y,
                pixel_size,
                text,
                color,
                centered);
        };

        append_hud_rect_top_left(vertices, viewport_width, viewport_height, 0.0F, 0.0F, viewport_width, viewport_height, {0.03F, 0.04F, 0.05F, 0.42F});
        append_hud_rect_top_left(vertices, viewport_width, viewport_height, 0.0F, 0.0F, viewport_width, viewport_height * 0.32F, {0.04F, 0.05F, 0.06F, 0.18F});
        append_hud_rect_top_left(vertices, viewport_width, viewport_height, 0.0F, viewport_height * 0.68F, viewport_width, viewport_height * 0.32F, {0.01F, 0.02F, 0.03F, 0.28F});
        append_hud_rect_top_left(vertices, viewport_width, viewport_height, viewport_width * 0.18F, 0.0F, viewport_width * 0.64F, viewport_height, {0.32F, 0.28F, 0.18F, 0.06F});
        append_hud_rect_top_left(vertices, viewport_width, viewport_height, viewport_width * 0.22F, layout.hero_y + 92.0F, viewport_width * 0.56F, 2.0F, {1.0F, 0.84F, 0.48F, 0.10F});

        const auto title_pixel_size = std::floor(std::clamp(viewport_width * 0.0064F, 6.0F, 11.0F));
        const auto logo_glow = HudColor {0.96F, 0.82F, 0.46F, 0.16F};
        append_hud_shadow_top_left(
            vertices,
            viewport_width,
            viewport_height,
            layout.hero_center_x - measure_pixel_text("VALCRAFT", title_pixel_size) * 0.5F - 20.0F,
            layout.hero_y - 16.0F,
            measure_pixel_text("VALCRAFT", title_pixel_size) + 40.0F,
            title_pixel_size * 8.0F + 24.0F,
            26.0F,
            logo_glow);
        draw_text(layout.hero_center_x, layout.hero_y, title_pixel_size, "VALCRAFT", {0.16F, 0.12F, 0.05F, 0.88F}, true);
        draw_text(layout.hero_center_x - 2.0F, layout.hero_y - 2.0F, title_pixel_size, "VALCRAFT", {1.00F, 0.86F, 0.54F, 0.96F}, true);
        draw_text(layout.hero_center_x, layout.hero_y - 4.0F, title_pixel_size, "VALCRAFT", {0.98F, 0.95F, 0.88F, 1.0F}, true);
        draw_text(
            layout.tagline_center_x,
            layout.tagline_y,
            std::floor(std::clamp(viewport_width * 0.0019F, 2.0F, 4.0F)),
            "CONSTRUIRE  EXPLORER  SURVIVRE",
            {0.92F, 0.93F, 0.96F, 0.92F},
            true);

        for (const auto& button : layout.buttons) {
            const auto palette = button.selected
                                     ? make_warm_panel_palette({0.96F, 0.78F, 0.34F, 1.0F})
                                     : make_slate_panel_palette();
            append_stylized_panel_top_left(
                vertices,
                viewport_width,
                viewport_height,
                button.x,
                button.y,
                button.width,
                button.height,
                4.0F,
                palette,
                true);

            const auto button_pixel_size = std::floor(std::clamp(button.height / 11.0F, 3.0F, 4.0F));
            draw_text(
                button.x + button.width * 0.5F,
                button.y + std::floor((button.height - button_pixel_size * 7.0F) * 0.5F),
                button_pixel_size,
                button.label,
                button.selected ? HudColor {1.0F, 0.98F, 0.92F, 1.0F} : HudColor {0.92F, 0.94F, 0.98F, 0.96F},
                true);
        }

        draw_text(
            viewport_width * 0.5F,
            layout.button_stack_y + layout.button_stack_height + 26.0F,
            2.0F,
            "ENTREE POUR VALIDER",
            {0.80F, 0.82F, 0.86F, 0.84F},
            true);
    }

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(hud_program_);
    bind_hud_textures();
    upload_hud_vertices(vertices);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size()));
    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
}

void Renderer::draw_save_slot_menu(const SaveSlotMenuState& save_slot_menu, int width, int height) {
    if (width <= 0 || height <= 0 || hud_program_ == 0 || hud_vao_ == 0 || hud_vbo_ == 0) {
        return;
    }

    const auto layout = build_save_slot_menu_layout(width, height, save_slot_menu);
    const auto viewport_width = static_cast<float>(width);
    const auto viewport_height = static_cast<float>(height);
    SaveSlotHudCacheKey cache_key {};
    cache_key.save_slot_menu = save_slot_menu;
    cache_key.width = width;
    cache_key.height = height;

    auto& cache = save_slot_cache_;
    auto& vertices = cache.vertices;
    const auto needs_rebuild = !cache.valid || cache.key != cache_key;
    if (needs_rebuild) {
        cache.valid = true;
        cache.key = cache_key;
        vertices.clear();
        vertices.reserve(32768U);

        const auto draw_text = [&](float x, float y, float pixel_size, std::string_view text, const HudColor& color, bool centered = false) {
            append_pixel_text(vertices, viewport_width, viewport_height, x + pixel_size, y + pixel_size, pixel_size, text, {0.0F, 0.0F, 0.0F, 0.44F}, centered);
            append_pixel_text(vertices, viewport_width, viewport_height, x, y, pixel_size, text, color, centered);
        };

        append_hud_rect_top_left(vertices, viewport_width, viewport_height, 0.0F, 0.0F, viewport_width, viewport_height, {0.02F, 0.03F, 0.04F, 0.52F});
        append_stylized_panel_top_left(
            vertices,
            viewport_width,
            viewport_height,
            layout.panel_x,
            layout.panel_y,
            layout.panel_width,
            layout.panel_height,
            5.0F,
            make_stone_panel_palette(),
            false);

        const auto title_pixel_size = std::floor(std::clamp(viewport_width * 0.0038F, 4.0F, 6.0F));
        const auto subtitle_pixel_size = std::floor(std::clamp(viewport_width * 0.0018F, 2.0F, 3.0F));
        draw_text(layout.title_center_x, layout.title_y, title_pixel_size, save_slot_menu_title(save_slot_menu), {0.98F, 0.97F, 0.94F, 1.0F}, true);
        draw_text(layout.subtitle_center_x, layout.subtitle_y, subtitle_pixel_size, save_slot_menu_subtitle(save_slot_menu), {0.82F, 0.84F, 0.88F, 0.94F}, true);

        for (const auto& card : layout.cards) {
            auto palette = card.selected
                               ? make_warm_panel_palette(card.occupied ? HudColor {0.92F, 0.74F, 0.34F, 1.0F} : HudColor {0.70F, 0.86F, 0.98F, 1.0F})
                               : make_slate_panel_palette();
            if (!card.enabled) {
                palette.fill = {0.13F, 0.14F, 0.16F, 0.84F};
                palette.highlight = {0.20F, 0.20F, 0.22F, 0.12F};
            }
            append_stylized_panel_top_left(
                vertices,
                viewport_width,
                viewport_height,
                card.x,
                card.y,
                card.width,
                card.height,
                3.0F,
                palette,
                true);

            const auto heading_size = 3.0F;
            const auto body_size = 2.0F;
            const auto padding = 12.0F;
            const auto slot_label = std::string("SLOT ") + std::to_string(static_cast<int>(card.slot_index + 1U));
            draw_text(card.x + padding, card.y + 10.0F, heading_size, slot_label, {0.98F, 0.99F, 1.0F, 0.98F});

            if (card.delete_visible) {
                const auto delete_palette = card.delete_hovered
                                                ? make_warm_panel_palette({0.88F, 0.33F, 0.27F, 1.0F})
                                                : make_slate_panel_palette();
                append_stylized_panel_top_left(
                    vertices,
                    viewport_width,
                    viewport_height,
                    card.delete_x,
                    card.delete_y,
                    card.delete_size,
                    card.delete_size,
                    2.0F,
                    delete_palette,
                    true);
                draw_text(
                    card.delete_x + card.delete_size * 0.5F,
                    card.delete_y + std::floor((card.delete_size - 21.0F) * 0.5F),
                    3.0F,
                    "X",
                    {0.98F, 0.97F, 0.95F, 0.98F},
                    true);
            }

            if (!card.occupied) {
                draw_text(card.x + padding, card.y + 34.0F, body_size, "VIDE", {0.74F, 0.78F, 0.84F, 0.90F});
                if (save_slot_menu.mode == SaveSlotMenuMode::NewGame) {
                    const auto mode_text = format_save_slot_mode(save_slot_menu.new_game_mode);
                    draw_text(card.x + padding, card.y + 52.0F, body_size, mode_text, {0.62F, 0.78F, 0.90F, 0.90F});
                }
                if (card.active_slot) {
                    draw_text(card.x + card.width - 50.0F, card.y + 10.0F, body_size, "ACTIF", {1.0F, 0.88F, 0.56F, 0.94F});
                }
                continue;
            }

            const auto timestamp = format_save_slot_timestamp(card.metadata.saved_at_unix_seconds);
            const auto seed_text = format_save_slot_seed(card.metadata.seed);
            const auto time_text = format_save_slot_time(card.metadata.time_of_day);
            const auto mode_text = format_save_slot_mode(card.metadata.game_mode);
            draw_text(card.x + padding, card.y + 34.0F, body_size, timestamp, {0.86F, 0.88F, 0.92F, 0.96F});
            draw_text(card.x + padding, card.y + 52.0F, body_size, seed_text, {0.80F, 0.83F, 0.88F, 0.94F});
            draw_text(card.x + padding, card.y + 70.0F, body_size, time_text, {0.80F, 0.83F, 0.88F, 0.94F});
            const auto mode_x = std::max(card.x + padding, card.x + card.width - padding - measure_pixel_text(mode_text, body_size));
            draw_text(mode_x, card.y + 70.0F, body_size, mode_text, {0.64F, 0.82F, 0.94F, 0.90F});
            if (card.active_slot) {
                const auto active_x = card.delete_visible ? card.delete_x - 52.0F : card.x + card.width - 50.0F;
                draw_text(active_x, card.y + 10.0F, body_size, "ACTIF", {1.0F, 0.88F, 0.56F, 0.94F});
            }
        }

        const auto back_palette = layout.back_button.selected
                                      ? make_warm_panel_palette({0.88F, 0.72F, 0.34F, 1.0F})
                                      : make_slate_panel_palette();
        append_stylized_panel_top_left(
            vertices,
            viewport_width,
            viewport_height,
            layout.back_button.x,
            layout.back_button.y,
            layout.back_button.width,
            layout.back_button.height,
            3.0F,
            back_palette,
            true);
        draw_text(
            layout.back_button.x + layout.back_button.width * 0.5F,
            layout.back_button.y + std::floor((layout.back_button.height - 21.0F) * 0.5F),
            3.0F,
            "RETOUR",
            {0.96F, 0.97F, 0.99F, 0.98F},
            true);
    }

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(hud_program_);
    bind_hud_textures();
    upload_hud_vertices(vertices);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size()));
    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
}

void Renderer::draw_options_menu(const OptionsMenuState& options_menu, int width, int height) {
    if (width <= 0 || height <= 0 || hud_program_ == 0 || hud_vao_ == 0 || hud_vbo_ == 0) {
        return;
    }

    const auto layout = build_options_menu_layout(width, height, options_menu);
    const auto viewport_width = static_cast<float>(width);
    const auto viewport_height = static_cast<float>(height);
    OptionsHudCacheKey cache_key {};
    cache_key.options_menu = options_menu;
    cache_key.width = width;
    cache_key.height = height;

    auto& cache = options_cache_;
    auto& vertices = cache.vertices;
    const auto needs_rebuild = !cache.valid || cache.key != cache_key;
    if (needs_rebuild) {
        cache.valid = true;
        cache.key = cache_key;
        vertices.clear();
        vertices.reserve(12288U);

        const auto draw_text = [&](float x, float y, float pixel_size, std::string_view text, const HudColor& color, bool centered = false) {
            append_pixel_text(vertices, viewport_width, viewport_height, x + pixel_size, y + pixel_size, pixel_size, text, {0.0F, 0.0F, 0.0F, 0.44F}, centered);
            append_pixel_text(vertices, viewport_width, viewport_height, x, y, pixel_size, text, color, centered);
        };

        append_hud_rect_top_left(vertices, viewport_width, viewport_height, 0.0F, 0.0F, viewport_width, viewport_height, {0.03F, 0.04F, 0.05F, 0.56F});
        append_stylized_panel_top_left(
            vertices,
            viewport_width,
            viewport_height,
            layout.panel_x,
            layout.panel_y,
            layout.panel_width,
            layout.panel_height,
            5.0F,
            make_stone_panel_palette(),
            false);

        const auto title_pixel_size = std::floor(std::clamp(viewport_width * 0.0038F, 4.0F, 6.0F));
        const auto subtitle_pixel_size = std::floor(std::clamp(viewport_width * 0.0018F, 2.0F, 3.0F));
        draw_text(layout.title_center_x, layout.title_y, title_pixel_size, "OPTIONS", {0.98F, 0.97F, 0.94F, 1.0F}, true);
        draw_text(layout.subtitle_center_x, layout.subtitle_y, subtitle_pixel_size, options_menu_subtitle(options_menu.parent), {0.82F, 0.84F, 0.88F, 0.94F}, true);

        for (const auto& button : layout.buttons) {
            const auto palette = button.selected
                                     ? make_warm_panel_palette({0.90F, 0.74F, 0.34F, 1.0F})
                                     : make_slate_panel_palette();
            append_stylized_panel_top_left(
                vertices,
                viewport_width,
                viewport_height,
                button.x,
                button.y,
                button.width,
                button.height,
                4.0F,
                palette,
                true);
            const auto button_pixel_size = std::floor(std::clamp(button.height / 11.0F, 3.0F, 4.0F));
            draw_text(
                button.x + button.width * 0.5F,
                button.y + std::floor((button.height - button_pixel_size * 7.0F) * 0.5F),
                button_pixel_size,
                button.label,
                {0.96F, 0.97F, 0.99F, 0.98F},
                true);
        }
    }

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(hud_program_);
    bind_hud_textures();
    upload_hud_vertices(vertices);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size()));
    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
}

void Renderer::draw_confirm_dialog(const ConfirmDialogState& confirm_dialog, int width, int height) {
    if (width <= 0 || height <= 0 || hud_program_ == 0 || hud_vao_ == 0 || hud_vbo_ == 0) {
        return;
    }

    const auto layout = build_confirm_dialog_layout(width, height, confirm_dialog);
    const auto viewport_width = static_cast<float>(width);
    const auto viewport_height = static_cast<float>(height);
    ConfirmHudCacheKey cache_key {};
    cache_key.confirm_dialog = confirm_dialog;
    cache_key.width = width;
    cache_key.height = height;

    auto& cache = confirm_cache_;
    auto& vertices = cache.vertices;
    const auto needs_rebuild = !cache.valid || cache.key != cache_key;
    if (needs_rebuild) {
        cache.valid = true;
        cache.key = cache_key;
        vertices.clear();
        vertices.reserve(8192U);

        const auto draw_text = [&](float x, float y, float pixel_size, std::string_view text, const HudColor& color, bool centered = false) {
            append_pixel_text(vertices, viewport_width, viewport_height, x + pixel_size, y + pixel_size, pixel_size, text, {0.0F, 0.0F, 0.0F, 0.44F}, centered);
            append_pixel_text(vertices, viewport_width, viewport_height, x, y, pixel_size, text, color, centered);
        };

        append_hud_rect_top_left(vertices, viewport_width, viewport_height, 0.0F, 0.0F, viewport_width, viewport_height, {0.02F, 0.03F, 0.04F, 0.62F});
        append_stylized_panel_top_left(
            vertices,
            viewport_width,
            viewport_height,
            layout.panel_x,
            layout.panel_y,
            layout.panel_width,
            layout.panel_height,
            4.0F,
            make_stone_panel_palette(),
            false);

        draw_text(layout.title_center_x, layout.title_y, 4.0F, confirm_dialog_title(confirm_dialog.intent), {0.98F, 0.97F, 0.94F, 1.0F}, true);
        draw_text(layout.subtitle_center_x, layout.subtitle_y, 2.0F, confirm_dialog_subtitle(confirm_dialog.intent), {0.84F, 0.86F, 0.90F, 0.94F}, true);

        for (const auto& button : layout.buttons) {
            const auto palette = button.selected
                                     ? make_warm_panel_palette(button.choice == ConfirmDialogChoice::Confirm
                                                                   ? HudColor {0.90F, 0.74F, 0.34F, 1.0F}
                                                                   : HudColor {0.72F, 0.78F, 0.88F, 1.0F})
                                     : make_slate_panel_palette();
            append_stylized_panel_top_left(
                vertices,
                viewport_width,
                viewport_height,
                button.x,
                button.y,
                button.width,
                button.height,
                3.0F,
                palette,
                true);
            draw_text(
                button.x + button.width * 0.5F,
                button.y + std::floor((button.height - 21.0F) * 0.5F),
                3.0F,
                button.label,
                {0.96F, 0.97F, 0.99F, 0.98F},
                true);
        }
    }

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(hud_program_);
    bind_hud_textures();
    upload_hud_vertices(vertices);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size()));
    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
}

void Renderer::draw_musket_hud(
    const PlayerMusketView& musket,
    int width,
    int height) {
    if (!musket.active ||
        width <= 0 ||
        height <= 0 ||
        hud_program_ == 0 ||
        hud_vao_ == 0 ||
        hud_vbo_ == 0) {
        return;
    }

    const auto layout =
        resolve_musket_hud_layout(
            width,
            height,
            musket.aim_ratio);
    if (!layout.valid) {
        return;
    }
    const auto viewport_width =
        layout.viewport_width;
    const auto viewport_height =
        layout.viewport_height;
    const auto hud_scale =
        layout.scale;
    const auto center_x =
        layout.center_x;
    const auto center_y =
        layout.center_y;
    const auto outline =
        layout.outline;
    const auto dark = std::array<float, 4> {0.015F, 0.018F, 0.022F, 0.92F};
    const auto white = std::array<float, 4> {0.98F, 0.985F, 1.0F, 0.98F};
    const auto red = std::array<float, 4> {0.96F, 0.035F, 0.025F, 1.0F};

    std::vector<HudVertex> vertices;
    vertices.reserve(1'024U);

    const auto append_outlined_rect =
        [&](float x, float y, float rectangle_width, float rectangle_height) {
            append_hud_rect_top_left(
                vertices,
                viewport_width,
                viewport_height,
                x - outline,
                y - outline,
                rectangle_width + outline * 2.0F,
                rectangle_height + outline * 2.0F,
                dark);
            append_hud_rect_top_left(
                vertices,
                viewport_width,
                viewport_height,
                x,
                y,
                rectangle_width,
                rectangle_height,
                white);
        };

    for (const auto& branch : layout.branches) {
        append_outlined_rect(
            branch.x,
            branch.y,
            branch.width,
            branch.height);
    }

    const auto dot_outline_size =
        layout.dot_outline_size;
    const auto dot_size =
        layout.dot_size;
    append_hud_rect_top_left(
        vertices,
        viewport_width,
        viewport_height,
        center_x - dot_outline_size * 0.5F,
        center_y - dot_outline_size * 0.5F,
        dot_outline_size,
        dot_outline_size,
        dark);
    append_hud_rect_top_left(
        vertices,
        viewport_width,
        viewport_height,
        center_x - dot_size * 0.5F,
        center_y - dot_size * 0.5F,
        dot_size,
        dot_size,
        red);

    const auto text_pixel_size = std::max(1.25F, 1.55F * hud_scale);
    const auto text_y =
        layout.text_y;
    std::string status_text;
    std::string ammo_prefix;
    std::string ammo_suffix;
    auto show_infinite_reserve = false;
    if (musket.reloading()) {
        const auto percentage = std::clamp(
            static_cast<int>(std::lround(
                std::clamp(musket.reload_progress, 0.0F, 1.0F) * 100.0F)),
            0,
            100);
        status_text = "RECHARGEMENT " + std::to_string(percentage) + "%";
    } else if (musket.loaded()) {
        ammo_prefix = "1 / ";
        show_infinite_reserve = true;
    } else {
        ammo_prefix = "0 / ";
        ammo_suffix = " - R RECHARGER";
        show_infinite_reserve = true;
    }

    const auto infinity_width =
        text_pixel_size * 10.0F;
    const auto status_width =
        show_infinite_reserve
            ? measure_pixel_text(
                  ammo_prefix,
                  text_pixel_size) +
                  infinity_width +
                  measure_pixel_text(
                      ammo_suffix,
                      text_pixel_size)
            : measure_pixel_text(
                  status_text,
                  text_pixel_size);
    const auto panel_padding_x = 5.0F * hud_scale;
    const auto panel_padding_y = 3.0F * hud_scale;
    const auto text_height = text_pixel_size * 7.0F;
    append_hud_rect_top_left(
        vertices,
        viewport_width,
        viewport_height,
        center_x - status_width * 0.5F - panel_padding_x,
        text_y - panel_padding_y,
        status_width + panel_padding_x * 2.0F,
        text_height + panel_padding_y * 2.0F,
        {0.01F, 0.012F, 0.016F, 0.58F});
    if (!show_infinite_reserve) {
        append_pixel_text(
            vertices,
            viewport_width,
            viewport_height,
            center_x,
            text_y,
            text_pixel_size,
            status_text,
            white,
            true);
    } else {
        auto cursor_x =
            center_x -
            status_width * 0.5F;
        append_pixel_text(
            vertices,
            viewport_width,
            viewport_height,
            cursor_x,
            text_y,
            text_pixel_size,
            ammo_prefix,
            white);
        cursor_x +=
            measure_pixel_text(
                ammo_prefix,
                text_pixel_size);

        // Je trace moi-meme l'infini pour qu'il existe aussi dans la fonte
        // pixel Legacy et ne depende jamais d'un glyphe optionnel de l'atlas.
        constexpr std::array<std::uint16_t, 5U> kInfinityRows {{
            0b011000110U,
            0b100101001U,
            0b100010001U,
            0b100101001U,
            0b011000110U,
        }};
        for (std::size_t row = 0U;
             row < kInfinityRows.size();
             ++row) {
            for (int column = 0;
                 column < 9;
                 ++column) {
                const auto bit =
                    static_cast<std::uint16_t>(
                        1U <<
                        (8 - column));
                if ((kInfinityRows[row] &
                     bit) == 0U) {
                    continue;
                }
                append_hud_rect_top_left(
                    vertices,
                    viewport_width,
                    viewport_height,
                    cursor_x +
                        static_cast<float>(
                            column) *
                            text_pixel_size,
                    text_y +
                        (static_cast<float>(
                             row) +
                         1.0F) *
                            text_pixel_size,
                    text_pixel_size,
                    text_pixel_size,
                    white);
            }
        }
        cursor_x += infinity_width;
        append_pixel_text(
            vertices,
            viewport_width,
            viewport_height,
            cursor_x,
            text_y,
            text_pixel_size,
            ammo_suffix,
            white);
    }

    if (musket.reloading()) {
        const auto bar_width = std::max(94.0F * hud_scale, status_width);
        const auto bar_height = std::max(2.0F, 2.5F * hud_scale);
        const auto bar_y = text_y + text_height + 6.0F * hud_scale;
        append_hud_rect_top_left(
            vertices,
            viewport_width,
            viewport_height,
            center_x - bar_width * 0.5F - outline,
            bar_y - outline,
            bar_width + outline * 2.0F,
            bar_height + outline * 2.0F,
            dark);
        append_hud_rect_top_left(
            vertices,
            viewport_width,
            viewport_height,
            center_x - bar_width * 0.5F,
            bar_y,
            bar_width,
            bar_height,
            {0.18F, 0.18F, 0.20F, 0.86F});
        append_hud_rect_top_left(
            vertices,
            viewport_width,
            viewport_height,
            center_x - bar_width * 0.5F,
            bar_y,
            bar_width * std::clamp(musket.reload_progress, 0.0F, 1.0F),
            bar_height,
            {0.84F, 0.63F, 0.25F, 0.98F});
    }

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(hud_program_);
    bind_hud_textures();
    upload_hud_vertices(vertices);
    glDrawArrays(
        GL_TRIANGLES,
        0,
        static_cast<GLsizei>(vertices.size()));
    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
}

void Renderer::draw_crosshair() {
    glDisable(GL_DEPTH_TEST);
    glUseProgram(crosshair_program_);
    glBindVertexArray(crosshair_vao_);
    glDrawArrays(GL_LINES, 0, 4);
    record_draw_call();
    glEnable(GL_DEPTH_TEST);
}

} // namespace valcraft
