#include "render/StylizedShipMesh.h"

#include "world/BlockVisuals.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>
#include <span>
#include <utility>
#include <vector>

namespace valcraft {

namespace {

constexpr float kGeometryEpsilon = 1.0e-5F;
constexpr float kGeometryEpsilonSquared =
    kGeometryEpsilon *
    kGeometryEpsilon;
constexpr float kTau =
    std::numbers::pi_v<float> *
    2.0F;
constexpr std::uint64_t kFnvOffset =
    14695981039346656037ULL;
constexpr std::uint64_t kFnvPrime =
    1099511628211ULL;

struct MaterialVisual {
    BlockAtlasTile tile {};
    float material_class = 0.0F;
    float emission = 0.0F;
};

struct LocalBounds {
    glm::vec3 min {0.0F};
    glm::vec3 max {0.0F};
};

struct LodSettings {
    int hull_segments = 18;
    int cylinder_segments = 5;
    int square_sail_horizontal_segments = 3;
    int square_sail_vertical_segments = 2;
    int triangular_sail_segments = 3;
    float net_spacing = 1.5F;
    bool keep_small_structures = false;
};

[[nodiscard]] auto settings_for(
    StylizedShipLod lod) noexcept -> LodSettings {

    if (lod == StylizedShipLod::Near) {
        return {
            48,
            10,
            8,
            6,
            8,
            0.65F,
            true,
        };
    }
    return {};
}

[[nodiscard]] auto finite_vec3(
    const glm::vec3& value) noexcept -> bool {

    return std::isfinite(value.x) &&
           std::isfinite(value.y) &&
           std::isfinite(value.z);
}

[[nodiscard]] auto safe_normalize(
    const glm::vec3& value,
    const glm::vec3& fallback) noexcept -> glm::vec3 {

    const auto length_squared =
        glm::dot(value, value);
    if (!std::isfinite(length_squared) ||
        length_squared <=
            kGeometryEpsilonSquared) {
        return fallback;
    }
    return value /
           std::sqrt(length_squared);
}

[[nodiscard]] auto bounds_for(
    const ShipPart& part) noexcept -> LocalBounds {

    auto bounds = LocalBounds {
        glm::min(
            part.local_start,
            part.local_end),
        glm::max(
            part.local_start,
            part.local_end),
    };
    if (part.shape !=
        ShipPartShape::Panel) {
        return bounds;
    }

    const auto absolute_normal =
        glm::abs(part.orientation);
    const auto half_thickness =
        std::max(
            std::abs(part.thickness) *
                0.5F,
            0.01F);
    const auto center =
        (bounds.min +
         bounds.max) *
        0.5F;
    if (absolute_normal.x >=
            absolute_normal.y &&
        absolute_normal.x >=
            absolute_normal.z) {
        bounds.min.x =
            center.x -
            half_thickness;
        bounds.max.x =
            center.x +
            half_thickness;
    } else if (
        absolute_normal.y >=
        absolute_normal.z) {
        bounds.min.y =
            center.y -
            half_thickness;
        bounds.max.y =
            center.y +
            half_thickness;
    } else {
        bounds.min.z =
            center.z -
            half_thickness;
        bounds.max.z =
            center.z +
            half_thickness;
    }
    return bounds;
}

[[nodiscard]] auto valid_bounds(
    const LocalBounds& bounds) noexcept -> bool {

    return finite_vec3(bounds.min) &&
           finite_vec3(bounds.max) &&
           bounds.max.x >
               bounds.min.x +
                   kGeometryEpsilon &&
           bounds.max.y >
               bounds.min.y +
                   kGeometryEpsilon &&
           bounds.max.z >
               bounds.min.z +
                   kGeometryEpsilon;
}

[[nodiscard]] constexpr auto is_canvas(
    ShipMaterial material) noexcept -> bool {

    return material ==
               ShipMaterial::CreamCanvas ||
           material ==
               ShipMaterial::BlackCanvas;
}

[[nodiscard]] auto material_visual(
    ShipMaterial material,
    BlockVisualFace face) noexcept -> MaterialVisual {

    if (material ==
        ShipMaterial::Glass) {
        const auto glass =
            to_block_id(
                BlockType::Glass);
        return {
            block_atlas_tile(
                glass,
                face),
            block_visual_material_value(
                glass),
            0.0F,
        };
    }

    auto atlas_material =
        ShipAtlasMaterial::DarkHull;
    switch (material) {
    case ShipMaterial::DarkHull:
        atlas_material =
            ShipAtlasMaterial::DarkHull;
        break;
    case ShipMaterial::LightDeck:
        atlas_material =
            ShipAtlasMaterial::LightDeck;
        break;
    case ShipMaterial::CleanBeam:
        atlas_material =
            ShipAtlasMaterial::CleanBeam;
        break;
    case ShipMaterial::CreamCanvas:
        atlas_material =
            ShipAtlasMaterial::CreamCanvas;
        break;
    case ShipMaterial::Rope:
        atlas_material =
            ShipAtlasMaterial::Rope;
        break;
    case ShipMaterial::Iron:
        atlas_material =
            ShipAtlasMaterial::Iron;
        break;
    case ShipMaterial::Brass:
        atlas_material =
            ShipAtlasMaterial::Brass;
        break;
    case ShipMaterial::Lantern:
        atlas_material =
            ShipAtlasMaterial::Lantern;
        break;
    case ShipMaterial::BlackCanvas:
        atlas_material =
            ShipAtlasMaterial::BlackCanvas;
        break;
    case ShipMaterial::SolidGold:
        atlas_material =
            ShipAtlasMaterial::SolidGold;
        break;
    case ShipMaterial::Glass:
        break;
    }

    return {
        ship_atlas_tile(
            atlas_material),
        block_visual_material_value(
            ship_visual_material(
                atlas_material)),
        material ==
                ShipMaterial::Lantern
            ? 11.0F / 15.0F
            : 0.0F,
    };
}

[[nodiscard]] auto visual_face_for(
    const glm::vec3& normal) noexcept -> BlockVisualFace {

    const auto absolute =
        glm::abs(normal);
    if (absolute.x >=
            absolute.y &&
        absolute.x >=
            absolute.z) {
        return normal.x >= 0.0F
                   ? BlockVisualFace::PositiveX
                   : BlockVisualFace::NegativeX;
    }
    if (absolute.y >=
        absolute.z) {
        return normal.y >= 0.0F
                   ? BlockVisualFace::PositiveY
                   : BlockVisualFace::NegativeY;
    }
    return normal.z >= 0.0F
               ? BlockVisualFace::PositiveZ
               : BlockVisualFace::NegativeZ;
}

[[nodiscard]] auto face_shade_for(
    const glm::vec3& normal) noexcept -> float {

    if (normal.y > 0.5F) {
        return 1.0F;
    }
    if (normal.y < -0.5F) {
        return 0.66F;
    }
    return std::abs(normal.x) > 0.5F
               ? 0.86F
               : 0.78F;
}

[[nodiscard]] auto atlas_uv(
    const BlockAtlasTile& tile,
    const glm::vec2& local_uv) noexcept -> glm::vec2 {

    constexpr auto atlas_step =
        1.0F /
        kBlockAtlasTilesPerAxis;
    constexpr auto half_texel =
        0.5F /
        static_cast<float>(
            kBlockAtlasSize);
    const auto minimum =
        glm::vec2 {
            static_cast<float>(tile.x) *
                    atlas_step +
                half_texel,
            static_cast<float>(tile.y) *
                    atlas_step +
                half_texel,
        };
    const auto maximum =
        glm::vec2 {
            static_cast<float>(tile.x + 1) *
                    atlas_step -
                half_texel,
            static_cast<float>(tile.y + 1) *
                    atlas_step -
                half_texel,
        };
    return glm::mix(
        minimum,
        maximum,
        glm::clamp(
            local_uv,
            glm::vec2 {0.0F},
            glm::vec2 {1.0F}));
}

[[nodiscard]] auto make_vertex(
    const glm::vec3& position,
    const glm::vec3& normal,
    const glm::vec2& local_uv,
    ShipMaterial material,
    float wave_weight) noexcept -> ChunkVertex {

    const auto unit_normal =
        safe_normalize(
            normal,
            glm::vec3 {
                0.0F,
                1.0F,
                0.0F,
            });
    const auto visual =
        material_visual(
            material,
            visual_face_for(
                unit_normal));
    const auto uv =
        atlas_uv(
            visual.tile,
            local_uv);
    return {
        position.x,
        position.y,
        position.z,
        uv.x,
        uv.y,
        unit_normal.x,
        unit_normal.y,
        unit_normal.z,
        face_shade_for(
            unit_normal),
        1.0F,
        1.0F,
        visual.emission,
        visual.material_class,
        std::clamp(
            wave_weight,
            0.0F,
            1.0F),
    };
}

auto append_triangle_with_normals(
    ChunkMeshData& mesh,
    std::array<glm::vec3, 3> points,
    std::array<glm::vec3, 3> preferred_normals,
    ShipMaterial material,
    std::array<glm::vec2, 3> uvs,
    std::array<float, 3> wave_weights = {}) -> bool {

    if (!finite_vec3(points[0]) ||
        !finite_vec3(points[1]) ||
        !finite_vec3(points[2])) {
        return false;
    }

    auto geometric_normal =
        glm::cross(
            points[1] -
                points[0],
            points[2] -
                points[0]);
    const auto area_squared =
        glm::dot(
            geometric_normal,
            geometric_normal);
    if (!std::isfinite(area_squared) ||
        area_squared <=
            kGeometryEpsilonSquared) {
        return false;
    }
    geometric_normal =
        safe_normalize(
            geometric_normal,
            glm::vec3 {
                0.0F,
                1.0F,
                0.0F,
            });
    auto average_normal =
        safe_normalize(
            preferred_normals[0] +
                preferred_normals[1] +
                preferred_normals[2],
            geometric_normal);
    if (glm::dot(
            geometric_normal,
            average_normal) < 0.0F) {
        std::swap(
            points[1],
            points[2]);
        std::swap(
            preferred_normals[1],
            preferred_normals[2]);
        std::swap(
            uvs[1],
            uvs[2]);
        std::swap(
            wave_weights[1],
            wave_weights[2]);
        geometric_normal =
            -geometric_normal;
    }

    if (mesh.vertices.size() >
        static_cast<std::size_t>(
            (std::numeric_limits<std::uint32_t>::max)()) -
            3U) {
        return false;
    }
    const auto base_index =
        static_cast<std::uint32_t>(
            mesh.vertices.size());
    for (std::size_t index = 0U;
         index < points.size();
         ++index) {
        auto normal =
            safe_normalize(
                preferred_normals[index],
                geometric_normal);
        if (glm::dot(
                geometric_normal,
                normal) <
            0.0F) {
            normal =
                -normal;
        }
        mesh.vertices.push_back(
            make_vertex(
                points[index],
                normal,
                uvs[index],
                material,
                wave_weights[index]));
    }
    mesh.indices.insert(
        mesh.indices.end(),
        {
            base_index,
            base_index + 1U,
            base_index + 2U,
        });
    return true;
}

auto append_triangle(
    ChunkMeshData& mesh,
    std::array<glm::vec3, 3> points,
    glm::vec3 preferred_normal,
    ShipMaterial material,
    std::array<glm::vec2, 3> uvs,
    std::array<float, 3> wave_weights = {}) -> bool {

    return append_triangle_with_normals(
        mesh,
        std::move(points),
        {
            preferred_normal,
            preferred_normal,
            preferred_normal,
        },
        material,
        std::move(uvs),
        std::move(wave_weights));
}

void append_quad_with_normals(
    ChunkMeshData& mesh,
    std::array<glm::vec3, 4> points,
    std::array<glm::vec3, 4> normals,
    ShipMaterial material,
    const std::array<glm::vec2, 4>& uvs,
    const std::array<float, 4>& wave_weights = {}) {

    const auto first =
        append_triangle_with_normals(
            mesh,
            {
                points[0],
                points[1],
                points[2],
            },
            {
                normals[0],
                normals[1],
                normals[2],
            },
            material,
            {
                uvs[0],
                uvs[1],
                uvs[2],
            },
            {
                wave_weights[0],
                wave_weights[1],
                wave_weights[2],
            });
    const auto second =
        append_triangle_with_normals(
            mesh,
            {
                points[0],
                points[2],
                points[3],
            },
            {
                normals[0],
                normals[2],
                normals[3],
            },
            material,
            {
                uvs[0],
                uvs[2],
                uvs[3],
            },
            {
                wave_weights[0],
                wave_weights[2],
                wave_weights[3],
            });
    if (first || second) {
        ++mesh.face_count;
    }
}

void append_quad(
    ChunkMeshData& mesh,
    std::array<glm::vec3, 4> points,
    const glm::vec3& preferred_normal,
    ShipMaterial material,
    const std::array<float, 4>& wave_weights = {}) {

    constexpr std::array<glm::vec2, 4> uvs {{
        {0.0F, 0.0F},
        {1.0F, 0.0F},
        {1.0F, 1.0F},
        {0.0F, 1.0F},
    }};
    append_quad_with_normals(
        mesh,
        std::move(points),
        {
            preferred_normal,
            preferred_normal,
            preferred_normal,
            preferred_normal,
        },
        material,
        uvs,
        wave_weights);
}

void begin_range(
    StylizedShipIndexRange& range,
    const ChunkMeshData& mesh) noexcept {

    range.first_index =
        mesh.indices.size();
}

void finish_range(
    StylizedShipIndexRange& range,
    const ChunkMeshData& mesh) noexcept {

    range.index_count =
        mesh.indices.size() -
        range.first_index;
}

[[nodiscard]] auto allowed_profile_half_width(
    const ShipProtectionProfile& profile,
    float local_y,
    float local_z) noexcept -> float {

    const auto hull_half_width =
        profile.half_width_at(
            local_z);
    if (local_y <
        profile.middle_hull_min_y) {
        return std::max(
            profile.lower_minimum_half_width,
            hull_half_width -
                profile.lower_width_inset);
    }
    if (local_y <
        profile.upper_hull_min_y) {
        return std::max(
            profile.middle_minimum_half_width,
            hull_half_width -
                profile.middle_width_inset);
    }
    return hull_half_width;
}

void measure_hull_point(
    StylizedShipMeshMetrics& metrics,
    const ShipProtectionProfile& profile,
    const glm::vec3& point,
    bool profile_boundary) noexcept {

    const auto clamped_z =
        std::clamp(
            point.z,
            profile.stern_z,
            profile.bow_z);
    const auto allowed_half_width =
        allowed_profile_half_width(
            profile,
            point.y,
            clamped_z);
    metrics.maximum_protection_excess =
        std::max(
            metrics.maximum_protection_excess,
            std::max(
                0.0F,
                std::abs(point.x) -
                    allowed_half_width -
                    profile.boundary_margin));
    metrics.maximum_protection_excess =
        std::max(
            metrics.maximum_protection_excess,
            std::max(
                0.0F,
                profile.stern_z -
                    profile.boundary_margin -
                    point.z));
    metrics.maximum_protection_excess =
        std::max(
            metrics.maximum_protection_excess,
            std::max(
                0.0F,
                point.z -
                    profile.bow_z -
                    profile.boundary_margin));
    metrics.maximum_protection_excess =
        std::max(
            metrics.maximum_protection_excess,
            std::max(
                0.0F,
                profile.lower_hull_min_y -
                    point.y));
    metrics.maximum_protection_excess =
        std::max(
            metrics.maximum_protection_excess,
            std::max(
                0.0F,
                point.y -
                    profile.main_deck_top_y));

    if (profile_boundary) {
        metrics.maximum_profile_deviation =
            std::max(
                metrics.maximum_profile_deviation,
                std::abs(
                    std::abs(point.x) -
                    allowed_half_width));
    }
}

[[nodiscard]] auto hull_cross_section(
    const ShipProtectionProfile& profile,
    float local_z,
    StylizedShipLod lod) -> std::vector<glm::vec3> {

    const auto upper_half_width =
        profile.half_width_at(
            local_z);
    const auto middle_half_width =
        std::max(
            profile.middle_minimum_half_width,
            upper_half_width -
                profile.middle_width_inset);
    const auto lower_half_width =
        std::max(
            profile.lower_minimum_half_width,
            upper_half_width -
                profile.lower_width_inset);
    const auto lower_shoulder_y =
        std::lerp(
            profile.lower_hull_min_y,
            profile.middle_hull_min_y,
            0.32F);

    if (lod ==
        StylizedShipLod::Far) {
        return {
            {
                -upper_half_width,
                profile.main_deck_top_y,
                local_z,
            },
            {
                -upper_half_width,
                profile.upper_hull_min_y,
                local_z,
            },
            {
                -middle_half_width,
                profile.middle_hull_min_y,
                local_z,
            },
            {
                0.0F,
                profile.lower_hull_min_y,
                local_z,
            },
            {
                middle_half_width,
                profile.middle_hull_min_y,
                local_z,
            },
            {
                upper_half_width,
                profile.upper_hull_min_y,
                local_z,
            },
            {
                upper_half_width,
                profile.main_deck_top_y,
                local_z,
            },
        };
    }

    return {
        {
            -upper_half_width,
            profile.main_deck_top_y,
            local_z,
        },
        {
            -upper_half_width,
            profile.upper_hull_min_y,
            local_z,
        },
        {
            -middle_half_width,
            profile.middle_hull_min_y,
            local_z,
        },
        {
            -lower_half_width,
            lower_shoulder_y,
            local_z,
        },
        {
            0.0F,
            profile.lower_hull_min_y,
            local_z,
        },
        {
            lower_half_width,
            lower_shoulder_y,
            local_z,
        },
        {
            middle_half_width,
            profile.middle_hull_min_y,
            local_z,
        },
        {
            upper_half_width,
            profile.upper_hull_min_y,
            local_z,
        },
        {
            upper_half_width,
            profile.main_deck_top_y,
            local_z,
        },
    };
}

[[nodiscard]] auto hull_normal_at(
    const std::vector<std::vector<glm::vec3>>& sections,
    std::size_t ring,
    std::size_t point) noexcept -> glm::vec3 {

    if (sections.empty() ||
        ring >=
            sections.size() ||
        point >=
            sections[ring].size()) {
        return {
            0.0F,
            -1.0F,
            0.0F,
        };
    }

    const auto previous_ring =
        ring > 0U
            ? ring - 1U
            : ring;
    const auto next_ring =
        ring + 1U <
                sections.size()
            ? ring + 1U
            : ring;
    const auto previous_point =
        point > 0U
            ? point - 1U
            : point;
    const auto next_point =
        point + 1U <
                sections[ring].size()
            ? point + 1U
            : point;

    const auto longitudinal =
        sections[next_ring][point] -
        sections[previous_ring][point];
    const auto transverse =
        sections[ring][next_point] -
        sections[ring][previous_point];
    auto normal =
        safe_normalize(
            glm::cross(
                transverse,
                longitudinal),
            glm::vec3 {
                sections[ring][point].x < 0.0F
                    ? -1.0F
                    : 1.0F,
                0.0F,
                0.0F,
            });

    // Je verrouille ici le sens exterieur de la coque. Le maillage historique
    // balayait les anneaux dans l'ordre inverse et produisait des normales
    // rentrantes, ce qui ecrasait l'eclairage sur les flancs.
    const auto radial_direction =
        safe_normalize(
            glm::vec3 {
                sections[ring][point].x,
                sections[ring][point].y -
                    sections[ring].front().y,
                0.0F,
            },
            normal);
    if (glm::dot(
            normal,
            radial_direction) <
        0.0F) {
        normal =
            -normal;
    }
    return normal;
}

void append_hull_cap(
    ChunkMeshData& mesh,
    std::span<const glm::vec3> section,
    float normal_z) {

    if (section.size() <
        3U) {
        return;
    }
    const auto center =
        glm::vec3 {
            0.0F,
            (section.front().y +
             section[section.size() / 2U].y) *
                0.5F,
            section.front().z,
        };
    const auto normal =
        glm::vec3 {
            0.0F,
            0.0F,
            normal_z,
        };
    for (std::size_t index = 0U;
         index + 1U < section.size();
         ++index) {
        static_cast<void>(
            append_triangle(
                mesh,
                {
                    center,
                    section[index],
                    section[index + 1U],
                },
                normal,
                ShipMaterial::DarkHull,
                {
                    glm::vec2 {0.5F, 0.5F},
                    glm::vec2 {0.0F, 0.0F},
                    glm::vec2 {1.0F, 0.0F},
                }));
    }
    static_cast<void>(
        append_triangle(
            mesh,
            {
                center,
                section.back(),
                section.front(),
            },
            normal,
            ShipMaterial::DarkHull,
            {
                glm::vec2 {0.5F, 0.5F},
                glm::vec2 {0.0F, 1.0F},
                glm::vec2 {1.0F, 1.0F},
            }));
}

void append_hull(
    ChunkMeshData& mesh,
    StylizedShipMeshMetrics& metrics,
    const ShipProtectionProfile& profile,
    const LodSettings& settings,
    StylizedShipLod lod) {

    const auto hull_length =
        profile.bow_z -
        profile.stern_z;
    std::vector<std::vector<glm::vec3>> sections;
    sections.reserve(
        static_cast<std::size_t>(
            settings.hull_segments +
            1));
    for (int segment = 0;
         segment <=
             settings.hull_segments;
         ++segment) {
        const auto ratio =
            static_cast<float>(segment) /
            static_cast<float>(
                settings.hull_segments);
        const auto local_z =
            profile.stern_z +
            hull_length *
                ratio;
        sections.push_back(
            hull_cross_section(
                profile,
                local_z,
                lod));

        const auto& section =
            sections.back();
        for (std::size_t point_index = 0U;
             point_index < section.size();
             ++point_index) {
            const auto boundary =
                point_index !=
                section.size() / 2U;
            measure_hull_point(
                metrics,
                profile,
                section[point_index],
                boundary);
        }
    }

    for (std::size_t ring = 0U;
         ring + 1U < sections.size();
         ++ring) {
        const auto& first =
            sections[ring];
        const auto& second =
            sections[ring + 1U];
        const auto point_count =
            std::min(
                first.size(),
                second.size());
        for (std::size_t point = 0U;
             point + 1U < point_count;
             ++point) {
            const std::array quad {
                first[point],
                first[point + 1U],
                second[point + 1U],
                second[point],
            };
            append_quad_with_normals(
                mesh,
                quad,
                {
                    hull_normal_at(
                        sections,
                        ring,
                        point),
                    hull_normal_at(
                        sections,
                        ring,
                        point + 1U),
                    hull_normal_at(
                        sections,
                        ring + 1U,
                        point + 1U),
                    hull_normal_at(
                        sections,
                        ring + 1U,
                        point),
                },
                ShipMaterial::DarkHull,
                {{
                    {0.0F, 0.0F},
                    {1.0F, 0.0F},
                    {1.0F, 1.0F},
                    {0.0F, 1.0F},
                }});
        }
    }

    append_hull_cap(
        mesh,
        sections.front(),
        -1.0F);
    append_hull_cap(
        mesh,
        sections.back(),
        1.0F);

    metrics.midship_half_width =
        profile.half_width_at(
            0.0F);
    metrics.stern_half_width =
        profile.half_width_at(
            profile.stern_z);
    metrics.bow_half_width =
        profile.half_width_at(
            profile.bow_z);
}

void append_cuboid(
    ChunkMeshData& mesh,
    const LocalBounds& bounds,
    ShipMaterial material) {

    if (!valid_bounds(bounds)) {
        return;
    }
    const auto& minimum =
        bounds.min;
    const auto& maximum =
        bounds.max;
    append_quad(
        mesh,
        {{
            {maximum.x, minimum.y, minimum.z},
            {maximum.x, maximum.y, minimum.z},
            {maximum.x, maximum.y, maximum.z},
            {maximum.x, minimum.y, maximum.z},
        }},
        {1.0F, 0.0F, 0.0F},
        material);
    append_quad(
        mesh,
        {{
            {minimum.x, minimum.y, maximum.z},
            {minimum.x, maximum.y, maximum.z},
            {minimum.x, maximum.y, minimum.z},
            {minimum.x, minimum.y, minimum.z},
        }},
        {-1.0F, 0.0F, 0.0F},
        material);
    append_quad(
        mesh,
        {{
            {minimum.x, maximum.y, maximum.z},
            {maximum.x, maximum.y, maximum.z},
            {maximum.x, maximum.y, minimum.z},
            {minimum.x, maximum.y, minimum.z},
        }},
        {0.0F, 1.0F, 0.0F},
        material);
    append_quad(
        mesh,
        {{
            {minimum.x, minimum.y, minimum.z},
            {maximum.x, minimum.y, minimum.z},
            {maximum.x, minimum.y, maximum.z},
            {minimum.x, minimum.y, maximum.z},
        }},
        {0.0F, -1.0F, 0.0F},
        material);
    append_quad(
        mesh,
        {{
            {maximum.x, minimum.y, maximum.z},
            {maximum.x, maximum.y, maximum.z},
            {minimum.x, maximum.y, maximum.z},
            {minimum.x, minimum.y, maximum.z},
        }},
        {0.0F, 0.0F, 1.0F},
        material);
    append_quad(
        mesh,
        {{
            {minimum.x, minimum.y, minimum.z},
            {minimum.x, maximum.y, minimum.z},
            {maximum.x, maximum.y, minimum.z},
            {maximum.x, minimum.y, minimum.z},
        }},
        {0.0F, 0.0F, -1.0F},
        material);
}

void append_decks(
    ChunkMeshData& mesh,
    StylizedShipMeshMetrics& metrics,
    std::span<const ShipPart> parts) {

    for (const auto& part : parts) {
        if (!part.supports_player ||
            part.material !=
                ShipMaterial::LightDeck ||
            (part.shape !=
                 ShipPartShape::Box &&
             part.shape !=
                 ShipPartShape::Slab &&
             part.shape !=
                 ShipPartShape::Stair)) {
            continue;
        }
        const auto bounds =
            bounds_for(part);
        if (!valid_bounds(bounds)) {
            continue;
        }
        const auto deck_y =
            bounds.max.y;
        append_quad(
            mesh,
            {{
                {
                    bounds.min.x,
                    deck_y,
                    bounds.max.z,
                },
                {
                    bounds.max.x,
                    deck_y,
                    bounds.max.z,
                },
                {
                    bounds.max.x,
                    deck_y,
                    bounds.min.z,
                },
                {
                    bounds.min.x,
                    deck_y,
                    bounds.min.z,
                },
            }},
            {0.0F, 1.0F, 0.0F},
            ShipMaterial::LightDeck);
        metrics.maximum_deck_alignment_error =
            std::max(
                metrics.maximum_deck_alignment_error,
                std::abs(
                    deck_y -
                    bounds.max.y));
    }
}

void append_panel(
    ChunkMeshData& mesh,
    const ShipPart& part) {

    const auto bounds =
        bounds_for(part);
    if (!valid_bounds(bounds)) {
        return;
    }
    const auto normal =
        safe_normalize(
            part.orientation,
            glm::vec3 {
                0.0F,
                0.0F,
                1.0F,
            });
    const auto absolute_normal =
        glm::abs(normal);
    std::array<glm::vec3, 4> front {};
    std::array<glm::vec3, 4> back {};
    if (absolute_normal.x >=
            absolute_normal.y &&
        absolute_normal.x >=
            absolute_normal.z) {
        const auto front_x =
            normal.x >= 0.0F
                ? bounds.max.x
                : bounds.min.x;
        const auto back_x =
            normal.x >= 0.0F
                ? bounds.min.x
                : bounds.max.x;
        front = {{
            {front_x, bounds.min.y, bounds.min.z},
            {front_x, bounds.min.y, bounds.max.z},
            {front_x, bounds.max.y, bounds.max.z},
            {front_x, bounds.max.y, bounds.min.z},
        }};
        back = {{
            {back_x, bounds.min.y, bounds.min.z},
            {back_x, bounds.max.y, bounds.min.z},
            {back_x, bounds.max.y, bounds.max.z},
            {back_x, bounds.min.y, bounds.max.z},
        }};
    } else if (
        absolute_normal.y >=
        absolute_normal.z) {
        const auto front_y =
            normal.y >= 0.0F
                ? bounds.max.y
                : bounds.min.y;
        const auto back_y =
            normal.y >= 0.0F
                ? bounds.min.y
                : bounds.max.y;
        front = {{
            {bounds.min.x, front_y, bounds.max.z},
            {bounds.max.x, front_y, bounds.max.z},
            {bounds.max.x, front_y, bounds.min.z},
            {bounds.min.x, front_y, bounds.min.z},
        }};
        back = {{
            {bounds.min.x, back_y, bounds.min.z},
            {bounds.max.x, back_y, bounds.min.z},
            {bounds.max.x, back_y, bounds.max.z},
            {bounds.min.x, back_y, bounds.max.z},
        }};
    } else {
        const auto front_z =
            normal.z >= 0.0F
                ? bounds.max.z
                : bounds.min.z;
        const auto back_z =
            normal.z >= 0.0F
                ? bounds.min.z
                : bounds.max.z;
        front = {{
            {bounds.min.x, bounds.min.y, front_z},
            {bounds.max.x, bounds.min.y, front_z},
            {bounds.max.x, bounds.max.y, front_z},
            {bounds.min.x, bounds.max.y, front_z},
        }};
        back = {{
            {bounds.min.x, bounds.min.y, back_z},
            {bounds.min.x, bounds.max.y, back_z},
            {bounds.max.x, bounds.max.y, back_z},
            {bounds.max.x, bounds.min.y, back_z},
        }};
    }
    append_quad(
        mesh,
        front,
        normal,
        part.material);
    append_quad(
        mesh,
        back,
        -normal,
        part.material);
}

void append_structures(
    ChunkMeshData& mesh,
    std::span<const ShipPart> parts,
    const LodSettings& settings) {

    for (const auto& part : parts) {
        if (part.shape ==
                ShipPartShape::Panel &&
            !is_canvas(
                part.material)) {
            append_panel(
                mesh,
                part);
            continue;
        }
        if (part.shape !=
                ShipPartShape::Box &&
            part.shape !=
                ShipPartShape::Slab &&
            part.shape !=
                ShipPartShape::Stair) {
            continue;
        }
        if (part.material ==
            ShipMaterial::DarkHull) {
            continue;
        }
        if (part.supports_player &&
            part.material ==
                ShipMaterial::LightDeck) {
            continue;
        }
        const auto bounds =
            bounds_for(part);
        if (!valid_bounds(bounds)) {
            continue;
        }
        const auto extent =
            bounds.max -
            bounds.min;
        const auto maximum_extent =
            std::max(
                extent.x,
                std::max(
                    extent.y,
                    extent.z));
        if (!settings.keep_small_structures &&
            maximum_extent < 0.35F) {
            continue;
        }
        append_cuboid(
            mesh,
            bounds,
            part.material);
    }
}

void append_cylinder(
    ChunkMeshData& mesh,
    const glm::vec3& start,
    const glm::vec3& end,
    float diameter,
    ShipMaterial material,
    int radial_segments) {

    const auto axis =
        end -
        start;
    const auto length =
        glm::length(axis);
    if (!finite_vec3(start) ||
        !finite_vec3(end) ||
        !std::isfinite(diameter) ||
        length <=
            kGeometryEpsilon ||
        diameter <=
            kGeometryEpsilon ||
        radial_segments < 3) {
        return;
    }

    const auto direction =
        axis /
        length;
    const auto reference =
        std::abs(direction.y) <
                0.92F
            ? glm::vec3 {
                  0.0F,
                  1.0F,
                  0.0F,
              }
            : glm::vec3 {
                  1.0F,
                  0.0F,
                  0.0F,
              };
    const auto side =
        safe_normalize(
            glm::cross(
                direction,
                reference),
            glm::vec3 {
                1.0F,
                0.0F,
                0.0F,
            });
    const auto up =
        safe_normalize(
            glm::cross(
                side,
                direction),
            glm::vec3 {
                0.0F,
                0.0F,
                1.0F,
            });
    const auto radius =
        std::max(
            diameter *
                0.5F,
            0.01F);

    for (int segment = 0;
         segment < radial_segments;
         ++segment) {
        const auto first_angle =
            kTau *
            static_cast<float>(segment) /
            static_cast<float>(
                radial_segments);
        const auto second_angle =
            kTau *
            static_cast<float>(segment + 1) /
            static_cast<float>(
                radial_segments);
        const auto first_offset =
            (
                side *
                    std::cos(first_angle) +
                up *
                    std::sin(first_angle)
            ) *
            radius;
        const auto second_offset =
            (
                side *
                    std::cos(second_angle) +
                up *
                    std::sin(second_angle)
            ) *
            radius;
        const auto normal =
            safe_normalize(
                first_offset +
                    second_offset,
                side);
        const auto first_normal =
            safe_normalize(
                first_offset,
                normal);
        const auto second_normal =
            safe_normalize(
                second_offset,
                normal);
        const auto first_ratio =
            static_cast<float>(segment) /
            static_cast<float>(
                radial_segments);
        const auto second_ratio =
            static_cast<float>(segment + 1) /
            static_cast<float>(
                radial_segments);
        append_quad_with_normals(
            mesh,
            {{
                start +
                    first_offset,
                end +
                    first_offset,
                end +
                    second_offset,
                start +
                    second_offset,
            }},
            {
                first_normal,
                first_normal,
                second_normal,
                second_normal,
            },
            material,
            {{
                {first_ratio, 0.0F},
                {first_ratio, 1.0F},
                {second_ratio, 1.0F},
                {second_ratio, 0.0F},
            }});
        const auto first_cap_uv =
            glm::vec2 {
                0.5F +
                    std::cos(first_angle) *
                        0.5F,
                0.5F +
                    std::sin(first_angle) *
                        0.5F,
            };
        const auto second_cap_uv =
            glm::vec2 {
                0.5F +
                    std::cos(second_angle) *
                        0.5F,
                0.5F +
                    std::sin(second_angle) *
                        0.5F,
            };
        static_cast<void>(
            append_triangle(
                mesh,
                {
                    start,
                    start +
                        second_offset,
                    start +
                        first_offset,
                },
                -direction,
                material,
                {
                    glm::vec2 {0.5F, 0.5F},
                    second_cap_uv,
                    first_cap_uv,
                }));
        static_cast<void>(
            append_triangle(
                mesh,
                {
                    end,
                    end +
                        first_offset,
                    end +
                        second_offset,
                },
                direction,
                material,
                {
                    glm::vec2 {0.5F, 0.5F},
                    first_cap_uv,
                    second_cap_uv,
                }));
    }
}

void append_net(
    ChunkMeshData& mesh,
    const ShipPart& part,
    const LodSettings& settings) {

    const auto bounds =
        LocalBounds {
            glm::min(
                part.local_start,
                part.local_end),
            glm::max(
                part.local_start,
                part.local_end),
        };
    if (!finite_vec3(bounds.min) ||
        !finite_vec3(bounds.max)) {
        return;
    }
    const auto absolute_normal =
        glm::abs(part.orientation);
    const auto normal_axis =
        absolute_normal.x >=
                absolute_normal.z
            ? 0
            : 2;
    const auto tangent_axis =
        normal_axis == 0
            ? 2
            : 0;
    const auto plane =
        (
            bounds.min[normal_axis] +
            bounds.max[normal_axis]
        ) *
        0.5F;
    const auto tangent_span =
        bounds.max[tangent_axis] -
        bounds.min[tangent_axis];
    const auto vertical_span =
        bounds.max.y -
        bounds.min.y;
    if (tangent_span <=
            kGeometryEpsilon ||
        vertical_span <=
            kGeometryEpsilon) {
        return;
    }
    const auto tangent_sections =
        std::max(
            1,
            static_cast<int>(
                std::ceil(
                    tangent_span /
                    settings.net_spacing)));
    const auto vertical_sections =
        std::max(
            1,
            static_cast<int>(
                std::ceil(
                    vertical_span /
                    settings.net_spacing)));
    const auto point =
        [&](float tangent,
            float vertical) {
            auto result =
                glm::vec3 {0.0F};
            result[normal_axis] =
                plane;
            result[tangent_axis] =
                tangent;
            result.y =
                vertical;
            return result;
        };
    const auto diameter =
        std::max(
            std::abs(part.thickness),
            0.035F);

    for (int section = 0;
         section <=
             tangent_sections;
         ++section) {
        const auto ratio =
            static_cast<float>(section) /
            static_cast<float>(
                tangent_sections);
        const auto tangent =
            std::lerp(
                bounds.min[tangent_axis],
                bounds.max[tangent_axis],
                ratio);
        append_cylinder(
            mesh,
            point(
                tangent,
                bounds.min.y),
            point(
                tangent,
                bounds.max.y),
            diameter,
            ShipMaterial::Rope,
            settings.cylinder_segments);
    }
    for (int section = 0;
         section <=
             vertical_sections;
         ++section) {
        const auto ratio =
            static_cast<float>(section) /
            static_cast<float>(
                vertical_sections);
        const auto vertical =
            std::lerp(
                bounds.min.y,
                bounds.max.y,
                ratio);
        append_cylinder(
            mesh,
            point(
                bounds.min[tangent_axis],
                vertical),
            point(
                bounds.max[tangent_axis],
                vertical),
            diameter,
            ShipMaterial::Rope,
            settings.cylinder_segments);
    }
}

void append_wheel(
    ChunkMeshData& mesh,
    const ShipPart& part,
    const LodSettings& settings) {

    const auto bounds =
        bounds_for(part);
    if (!valid_bounds(bounds)) {
        return;
    }
    const auto center =
        (bounds.min +
         bounds.max) *
        0.5F;
    const auto radius =
        std::min(
            bounds.max.x -
                bounds.min.x,
            bounds.max.y -
                bounds.min.y) *
        0.5F;
    const auto ring_segments =
        settings.keep_small_structures
            ? 20
            : 10;
    const auto spoke_count =
        settings.keep_small_structures
            ? 8
            : 4;
    for (int segment = 0;
         segment < ring_segments;
         ++segment) {
        const auto first_angle =
            kTau *
            static_cast<float>(segment) /
            static_cast<float>(
                ring_segments);
        const auto second_angle =
            kTau *
            static_cast<float>(segment + 1) /
            static_cast<float>(
                ring_segments);
        append_cylinder(
            mesh,
            center +
                glm::vec3 {
                    std::cos(first_angle) *
                        radius,
                    std::sin(first_angle) *
                        radius,
                    0.0F,
                },
            center +
                glm::vec3 {
                    std::cos(second_angle) *
                        radius,
                    std::sin(second_angle) *
                        radius,
                    0.0F,
                },
            part.thickness,
            part.material,
            settings.cylinder_segments);
    }
    for (int spoke = 0;
         spoke < spoke_count;
         ++spoke) {
        const auto angle =
            kTau *
            static_cast<float>(spoke) /
            static_cast<float>(
                spoke_count);
        append_cylinder(
            mesh,
            center,
            center +
                glm::vec3 {
                    std::cos(angle) *
                        radius *
                        0.86F,
                    std::sin(angle) *
                        radius *
                        0.86F,
                    0.0F,
                },
            part.thickness *
                0.72F,
            part.material,
            settings.cylinder_segments);
    }
}

void append_rigging(
    ChunkMeshData& mesh,
    std::span<const ShipPart> parts,
    const LodSettings& settings) {

    for (const auto& part : parts) {
        if (part.shape ==
            ShipPartShape::Segment) {
            if (!settings.keep_small_structures &&
                part.thickness < 0.05F) {
                continue;
            }
            append_cylinder(
                mesh,
                part.local_start,
                part.local_end,
                part.thickness,
                part.material,
                settings.cylinder_segments);
        } else if (
            part.shape ==
            ShipPartShape::ClimbableNet) {
            append_net(
                mesh,
                part,
                settings);
        } else if (
            part.shape ==
            ShipPartShape::Wheel) {
            append_wheel(
                mesh,
                part,
                settings);
        }
    }
}

[[nodiscard]] auto bowed_square_sail_point(
    const LocalBounds& bounds,
    float normal_sign,
    float horizontal,
    float vertical,
    float surface_sign) noexcept -> glm::vec3 {

    const auto center_x =
        (
            bounds.min.x +
            bounds.max.x
        ) *
        0.5F;
    const auto bottom_half_width =
        (
            bounds.max.x -
            bounds.min.x
        ) *
        0.5F;
    const auto half_width =
        std::lerp(
            bottom_half_width,
            bottom_half_width *
                0.78F,
            vertical);
    const auto local_x =
        center_x +
        std::lerp(
            -half_width,
            half_width,
            horizontal);
    const auto local_y =
        std::lerp(
            bounds.min.y,
            bounds.max.y,
            vertical);
    const auto center_z =
        (
            bounds.min.z +
            bounds.max.z
        ) *
        0.5F;
    const auto half_thickness =
        (
            bounds.max.z -
            bounds.min.z
        ) *
        0.5F;
    const auto bow =
        std::sin(
            std::numbers::pi_v<float> *
            horizontal) *
        std::sin(
            std::numbers::pi_v<float> *
            vertical) *
        0.12F;
    return {
        local_x,
        local_y,
        center_z +
            normal_sign *
                (
                    surface_sign *
                        half_thickness +
                    bow
                ),
    };
}

[[nodiscard]] auto square_sail_normal(
    const LocalBounds& bounds,
    float normal_sign,
    float horizontal,
    float vertical,
    float surface_sign) noexcept -> glm::vec3 {

    constexpr auto derivative_step =
        1.0e-3F;
    const auto horizontal_min =
        std::max(
            0.0F,
            horizontal -
                derivative_step);
    const auto horizontal_max =
        std::min(
            1.0F,
            horizontal +
                derivative_step);
    const auto vertical_min =
        std::max(
            0.0F,
            vertical -
                derivative_step);
    const auto vertical_max =
        std::min(
            1.0F,
            vertical +
                derivative_step);
    const auto horizontal_tangent =
        bowed_square_sail_point(
            bounds,
            normal_sign,
            horizontal_max,
            vertical,
            surface_sign) -
        bowed_square_sail_point(
            bounds,
            normal_sign,
            horizontal_min,
            vertical,
            surface_sign);
    const auto vertical_tangent =
        bowed_square_sail_point(
            bounds,
            normal_sign,
            horizontal,
            vertical_max,
            surface_sign) -
        bowed_square_sail_point(
            bounds,
            normal_sign,
            horizontal,
            vertical_min,
            surface_sign);
    const auto expected =
        glm::vec3 {
            0.0F,
            0.0F,
            normal_sign *
                surface_sign,
        };
    auto normal =
        safe_normalize(
            glm::cross(
                horizontal_tangent,
                vertical_tangent),
            expected);
    if (glm::dot(
            normal,
            expected) <
        0.0F) {
        normal =
            -normal;
    }
    return normal;
}

void append_square_sail(
    ChunkMeshData& mesh,
    const ShipPart& part,
    const LodSettings& settings) {

    const auto bounds =
        bounds_for(part);
    if (!valid_bounds(bounds)) {
        return;
    }
    const auto normal_sign =
        part.orientation.z >= 0.0F
            ? 1.0F
            : -1.0F;
    for (int vertical_segment = 0;
         vertical_segment <
             settings.square_sail_vertical_segments;
         ++vertical_segment) {
        const auto vertical_min =
            static_cast<float>(
                vertical_segment) /
            static_cast<float>(
                settings.square_sail_vertical_segments);
        const auto vertical_max =
            static_cast<float>(
                vertical_segment + 1) /
            static_cast<float>(
                settings.square_sail_vertical_segments);
        for (int horizontal_segment = 0;
             horizontal_segment <
                 settings.square_sail_horizontal_segments;
             ++horizontal_segment) {
            const auto horizontal_min =
                static_cast<float>(
                    horizontal_segment) /
                static_cast<float>(
                    settings.square_sail_horizontal_segments);
            const auto horizontal_max =
                static_cast<float>(
                    horizontal_segment + 1) /
                static_cast<float>(
                    settings.square_sail_horizontal_segments);
            const std::array weights {
                vertical_min,
                vertical_min,
                vertical_max,
                vertical_max,
            };
            append_quad_with_normals(
                mesh,
                {{
                    bowed_square_sail_point(
                        bounds,
                        normal_sign,
                        horizontal_min,
                        vertical_min,
                        1.0F),
                    bowed_square_sail_point(
                        bounds,
                        normal_sign,
                        horizontal_max,
                        vertical_min,
                        1.0F),
                    bowed_square_sail_point(
                        bounds,
                        normal_sign,
                        horizontal_max,
                        vertical_max,
                        1.0F),
                    bowed_square_sail_point(
                        bounds,
                        normal_sign,
                        horizontal_min,
                        vertical_max,
                        1.0F),
                }},
                {
                    square_sail_normal(
                        bounds,
                        normal_sign,
                        horizontal_min,
                        vertical_min,
                        1.0F),
                    square_sail_normal(
                        bounds,
                        normal_sign,
                        horizontal_max,
                        vertical_min,
                        1.0F),
                    square_sail_normal(
                        bounds,
                        normal_sign,
                        horizontal_max,
                        vertical_max,
                        1.0F),
                    square_sail_normal(
                        bounds,
                        normal_sign,
                        horizontal_min,
                        vertical_max,
                        1.0F),
                },
                part.material,
                {{
                    {horizontal_min, vertical_min},
                    {horizontal_max, vertical_min},
                    {horizontal_max, vertical_max},
                    {horizontal_min, vertical_max},
                }},
                weights);
            append_quad_with_normals(
                mesh,
                {{
                    bowed_square_sail_point(
                        bounds,
                        normal_sign,
                        horizontal_max,
                        vertical_min,
                        -1.0F),
                    bowed_square_sail_point(
                        bounds,
                        normal_sign,
                        horizontal_min,
                        vertical_min,
                        -1.0F),
                    bowed_square_sail_point(
                        bounds,
                        normal_sign,
                        horizontal_min,
                        vertical_max,
                        -1.0F),
                    bowed_square_sail_point(
                        bounds,
                        normal_sign,
                        horizontal_max,
                        vertical_max,
                        -1.0F),
                }},
                {
                    square_sail_normal(
                        bounds,
                        normal_sign,
                        horizontal_max,
                        vertical_min,
                        -1.0F),
                    square_sail_normal(
                        bounds,
                        normal_sign,
                        horizontal_min,
                        vertical_min,
                        -1.0F),
                    square_sail_normal(
                        bounds,
                        normal_sign,
                        horizontal_min,
                        vertical_max,
                        -1.0F),
                    square_sail_normal(
                        bounds,
                        normal_sign,
                        horizontal_max,
                        vertical_max,
                        -1.0F),
                },
                part.material,
                {{
                    {horizontal_max, vertical_min},
                    {horizontal_min, vertical_min},
                    {horizontal_min, vertical_max},
                    {horizontal_max, vertical_max},
                }},
                {
                    vertical_min,
                    vertical_min,
                    vertical_max,
                    vertical_max,
                });
        }
    }
}

[[nodiscard]] auto triangular_sail_point(
    const LocalBounds& bounds,
    float normal_sign,
    float vertical,
    float horizontal,
    float surface_sign) noexcept -> glm::vec3 {

    const auto center_x =
        (
            bounds.min.x +
            bounds.max.x
        ) *
        0.5F;
    const auto half_thickness =
        (
            bounds.max.x -
            bounds.min.x
        ) *
        0.5F;
    const auto apex_z =
        normal_sign > 0.0F
            ? bounds.min.z
            : bounds.max.z;
    const auto base_z =
        std::lerp(
            bounds.min.z,
            bounds.max.z,
            horizontal);
    const auto local_z =
        std::lerp(
            base_z,
            apex_z,
            vertical);
    const auto local_y =
        std::lerp(
            bounds.min.y,
            bounds.max.y,
            vertical);
    const auto bow =
        std::sin(
            std::numbers::pi_v<float> *
            vertical) *
        std::sin(
            std::numbers::pi_v<float> *
            horizontal) *
        0.10F;
    return {
        center_x +
            normal_sign *
                (
                    surface_sign *
                        half_thickness +
                    bow
                ),
        local_y,
        local_z,
    };
}

void append_triangular_sail_surface(
    ChunkMeshData& mesh,
    const ShipPart& part,
    const LocalBounds& bounds,
    const LodSettings& settings,
    float normal_sign,
    float surface_sign) {

    const auto segment_count =
        settings.triangular_sail_segments;
    const auto normal =
        glm::vec3 {
            normal_sign *
                surface_sign,
            0.0F,
            0.0F,
        };
    for (int row = 0;
         row < segment_count;
         ++row) {
        const auto current_segments =
            segment_count -
            row;
        const auto next_segments =
            current_segments -
            1;
        const auto vertical_min =
            static_cast<float>(row) /
            static_cast<float>(
                segment_count);
        const auto vertical_max =
            static_cast<float>(row + 1) /
            static_cast<float>(
                segment_count);
        for (int column = 0;
             column <
                 current_segments;
             ++column) {
            const auto current_min =
                static_cast<float>(column) /
                static_cast<float>(
                    current_segments);
            const auto current_max =
                static_cast<float>(column + 1) /
                static_cast<float>(
                    current_segments);
            const auto next_min =
                next_segments > 0
                    ? static_cast<float>(column) /
                          static_cast<float>(
                              next_segments)
                    : 0.5F;
            const auto first =
                triangular_sail_point(
                    bounds,
                    normal_sign,
                    vertical_min,
                    current_min,
                    surface_sign);
            const auto second =
                triangular_sail_point(
                    bounds,
                    normal_sign,
                    vertical_min,
                    current_max,
                    surface_sign);
            const auto third =
                triangular_sail_point(
                    bounds,
                    normal_sign,
                    vertical_max,
                    next_min,
                    surface_sign);
            static_cast<void>(
                append_triangle(
                    mesh,
                    {
                        first,
                        second,
                        third,
                    },
                    normal,
                    part.material,
                    {
                        glm::vec2 {current_min, vertical_min},
                        glm::vec2 {current_max, vertical_min},
                        glm::vec2 {next_min, vertical_max},
                    },
                    {
                        vertical_min,
                        vertical_min,
                        vertical_max,
                    }));

            if (column <
                next_segments) {
                const auto next_max =
                    static_cast<float>(column + 1) /
                    static_cast<float>(
                        next_segments);
                const auto fourth =
                    triangular_sail_point(
                        bounds,
                        normal_sign,
                        vertical_max,
                        next_max,
                        surface_sign);
                static_cast<void>(
                    append_triangle(
                        mesh,
                        {
                            second,
                            fourth,
                            third,
                        },
                        normal,
                        part.material,
                        {
                            glm::vec2 {current_max, vertical_min},
                            glm::vec2 {next_max, vertical_max},
                            glm::vec2 {next_min, vertical_max},
                        },
                        {
                            vertical_min,
                            vertical_max,
                            vertical_max,
                        }));
            }
        }
    }
}

void append_sails(
    ChunkMeshData& mesh,
    std::span<const ShipPart> parts,
    const LodSettings& settings) {

    for (const auto& part : parts) {
        if (part.shape !=
                ShipPartShape::Panel ||
            !is_canvas(
                part.material)) {
            continue;
        }
        const auto bounds =
            bounds_for(part);
        if (!valid_bounds(bounds)) {
            continue;
        }
        const auto absolute_normal =
            glm::abs(part.orientation);
        if (absolute_normal.x >=
                absolute_normal.y &&
            absolute_normal.x >=
                absolute_normal.z) {
            const auto normal_sign =
                part.orientation.x >= 0.0F
                    ? 1.0F
                    : -1.0F;
            append_triangular_sail_surface(
                mesh,
                part,
                bounds,
                settings,
                normal_sign,
                1.0F);
            append_triangular_sail_surface(
                mesh,
                part,
                bounds,
                settings,
                normal_sign,
                -1.0F);
        } else {
            append_square_sail(
                mesh,
                part,
                settings);
        }
    }
}

[[nodiscard]] auto segment_intersects_triangle(
    const glm::vec3& origin,
    const glm::vec3& direction,
    float maximum_distance,
    const glm::vec3& first,
    const glm::vec3& second,
    const glm::vec3& third) noexcept -> bool {

    const auto first_edge =
        second -
        first;
    const auto second_edge =
        third -
        first;
    const auto cross_direction =
        glm::cross(
            direction,
            second_edge);
    const auto determinant =
        glm::dot(
            first_edge,
            cross_direction);
    if (std::abs(determinant) <=
        kGeometryEpsilon) {
        return false;
    }
    const auto inverse_determinant =
        1.0F /
        determinant;
    const auto translated_origin =
        origin -
        first;
    const auto first_coordinate =
        glm::dot(
            translated_origin,
            cross_direction) *
        inverse_determinant;
    if (first_coordinate < 0.0F ||
        first_coordinate > 1.0F) {
        return false;
    }
    const auto cross_origin =
        glm::cross(
            translated_origin,
            first_edge);
    const auto second_coordinate =
        glm::dot(
            direction,
            cross_origin) *
        inverse_determinant;
    if (second_coordinate < 0.0F ||
        first_coordinate +
                second_coordinate >
            1.0F) {
        return false;
    }
    const auto distance =
        glm::dot(
            second_edge,
            cross_origin) *
        inverse_determinant;
    return distance >= 0.0F &&
           distance <=
               maximum_distance;
}

[[nodiscard]] auto interior_axis_is_open(
    const ChunkMeshData& mesh,
    const StylizedShipIndexRange& hull,
    const ShipProtectionProfile& profile) noexcept -> bool {

    if (hull.index_count == 0U ||
        hull.first_index +
                hull.index_count >
            mesh.indices.size()) {
        return false;
    }
    const auto margin =
        std::min(
            0.5F,
            (
                profile.bow_z -
                profile.stern_z
            ) *
                0.1F);
    const auto origin =
        glm::vec3 {
            0.0F,
            std::lerp(
                profile.upper_hull_min_y,
                profile.main_deck_top_y,
                0.45F),
            profile.stern_z +
                margin,
        };
    const auto maximum_distance =
        profile.bow_z -
        profile.stern_z -
        margin *
            2.0F;
    const auto end_index =
        hull.first_index +
        hull.index_count;
    for (std::size_t index = hull.first_index;
         index + 2U < end_index;
         index += 3U) {
        const auto first_index =
            mesh.indices[index];
        const auto second_index =
            mesh.indices[index + 1U];
        const auto third_index =
            mesh.indices[index + 2U];
        if (first_index >=
                mesh.vertices.size() ||
            second_index >=
                mesh.vertices.size() ||
            third_index >=
                mesh.vertices.size()) {
            return false;
        }
        const auto position =
            [&](std::uint32_t vertex_index) {
                const auto& vertex =
                    mesh.vertices[vertex_index];
                return glm::vec3 {
                    vertex.x,
                    vertex.y,
                    vertex.z,
                };
            };
        if (segment_intersects_triangle(
                origin,
                {0.0F, 0.0F, 1.0F},
                maximum_distance,
                position(first_index),
                position(second_index),
                position(third_index))) {
            return false;
        }
    }
    return true;
}

void finish_bounds(
    StylizedShipMeshMetrics& metrics,
    const ChunkMeshData& mesh) noexcept {

    if (mesh.vertices.empty()) {
        metrics.bounds = {};
        return;
    }
    auto minimum =
        glm::vec3 {
            (std::numeric_limits<float>::max)(),
        };
    auto maximum =
        glm::vec3 {
            (std::numeric_limits<float>::lowest)(),
        };
    for (const auto& vertex : mesh.vertices) {
        const auto position =
            glm::vec3 {
                vertex.x,
                vertex.y,
                vertex.z,
            };
        minimum =
            glm::min(
                minimum,
                position);
        maximum =
            glm::max(
                maximum,
                position);
    }
    metrics.bounds = {
        minimum,
        maximum,
    };
}

void hash_u32(
    std::uint64_t& hash,
    std::uint32_t value) noexcept {

    for (unsigned shift = 0U;
         shift < 32U;
         shift += 8U) {
        hash ^=
            static_cast<std::uint8_t>(
                value >>
                shift);
        hash *=
            kFnvPrime;
    }
}

[[nodiscard]] auto mesh_checksum(
    const ChunkMeshData& mesh) noexcept -> std::uint64_t {

    auto hash =
        kFnvOffset;
    for (const auto& vertex : mesh.vertices) {
        const std::array values {
            vertex.x,
            vertex.y,
            vertex.z,
            vertex.u,
            vertex.v,
            vertex.nx,
            vertex.ny,
            vertex.nz,
            vertex.face_shade,
            vertex.ao,
            vertex.sky_light,
            vertex.block_light,
            vertex.material_class,
            vertex.wave_weight,
        };
        for (const auto value : values) {
            hash_u32(
                hash,
                std::bit_cast<std::uint32_t>(
                    value));
        }
    }
    for (const auto index : mesh.indices) {
        hash_u32(
            hash,
            index);
    }
    return hash;
}

[[nodiscard]] auto valid_profile(
    const ShipProtectionProfile& profile) noexcept -> bool {

    return std::isfinite(profile.stern_z) &&
           std::isfinite(profile.bow_z) &&
           std::isfinite(profile.lower_hull_min_y) &&
           std::isfinite(profile.middle_hull_min_y) &&
           std::isfinite(profile.upper_hull_min_y) &&
           std::isfinite(profile.main_deck_top_y) &&
           profile.stern_z <
               profile.bow_z &&
           profile.lower_hull_min_y <
               profile.middle_hull_min_y &&
           profile.middle_hull_min_y <
               profile.upper_hull_min_y &&
           profile.upper_hull_min_y <
               profile.main_deck_top_y &&
           profile.half_width_at(0.0F) >
               kGeometryEpsilon;
}

} // namespace

auto build_stylized_ship_mesh(
    const ShipBlueprint& blueprint,
    StylizedShipLod lod) -> StylizedShipMeshData {

    StylizedShipMeshData output {};
    output.cache_key = {
        blueprint.geometry_revision,
        lod,
    };
    if (blueprint.parts.empty() ||
        blueprint.geometry_revision == 0U ||
        !valid_profile(
            blueprint.protection_profile)) {
        return output;
    }

    const auto settings =
        settings_for(lod);
    // Je reserve un budget proportionnel aux pieces sans faire dependre le
    // resultat de la capacite choisie par l'allocateur.
    output.mesh.vertices.reserve(
        blueprint.parts.size() *
        (
            lod ==
                    StylizedShipLod::Near
                ? 36U
                : 18U
        ));
    output.mesh.indices.reserve(
        blueprint.parts.size() *
        (
            lod ==
                    StylizedShipLod::Near
                ? 54U
                : 27U
        ));

    begin_range(
        output.metrics.hull,
        output.mesh);
    append_hull(
        output.mesh,
        output.metrics,
        blueprint.protection_profile,
        settings,
        lod);
    finish_range(
        output.metrics.hull,
        output.mesh);

    begin_range(
        output.metrics.decks,
        output.mesh);
    append_decks(
        output.mesh,
        output.metrics,
        blueprint.parts);
    finish_range(
        output.metrics.decks,
        output.mesh);

    begin_range(
        output.metrics.structures,
        output.mesh);
    append_structures(
        output.mesh,
        blueprint.parts,
        settings);
    finish_range(
        output.metrics.structures,
        output.mesh);

    begin_range(
        output.metrics.rigging,
        output.mesh);
    append_rigging(
        output.mesh,
        blueprint.parts,
        settings);
    finish_range(
        output.metrics.rigging,
        output.mesh);

    begin_range(
        output.metrics.sails,
        output.mesh);
    append_sails(
        output.mesh,
        blueprint.parts,
        settings);
    finish_range(
        output.metrics.sails,
        output.mesh);

    finish_bounds(
        output.metrics,
        output.mesh);
    output.metrics.interior_axis_open =
        interior_axis_is_open(
            output.mesh,
            output.metrics.hull,
            blueprint.protection_profile);
    output.metrics.content_checksum =
        mesh_checksum(
            output.mesh);
    return output;
}

auto build_stylized_ship_mesh(
    const ShipRenderState& render_state,
    StylizedShipLod lod) -> StylizedShipMeshData {

    if (render_state.blueprint ==
        nullptr) {
        StylizedShipMeshData output {};
        output.cache_key = {
            render_state.geometry_revision,
            lod,
        };
        return output;
    }

    auto blueprint =
        *render_state.blueprint;
    if (!render_state.parts.empty()) {
        blueprint.parts =
            render_state.parts;
    }
    if (render_state.geometry_revision !=
        0U) {
        blueprint.geometry_revision =
            render_state.geometry_revision;
    }
    return build_stylized_ship_mesh(
        blueprint,
        lod);
}

} // namespace valcraft
