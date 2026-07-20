#pragma once

#include "gameplay/SeaAdventure.h"
#include "world/BlockVisuals.h"
#include "world/ChunkMesher.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/vec3.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

namespace valcraft {

namespace ship_mesh_detail {

constexpr float kGeometryEpsilon = 1.0e-4F;
constexpr float kClimbableNetSpacing = 0.50F;
constexpr float kClimbableNetMinimumDiameter = 0.025F;
constexpr float kClimbableNetBorderScale = 1.65F;
constexpr int kClimbableNetMaximumSections = 128;

struct LocalBounds {
    glm::vec3 min {0.0F};
    glm::vec3 max {0.0F};
};

struct FaceDefinition {
    int axis = 0;
    int direction = 1;
    BlockVisualFace visual_face = BlockVisualFace::PositiveX;
    std::array<glm::vec3, 4> corners {};
    glm::vec3 normal {1.0F, 0.0F, 0.0F};
};

inline const std::array<FaceDefinition, 6> kFaces {{
    {0, 1, BlockVisualFace::PositiveX,
     {{{1.0F, 0.0F, 0.0F}, {1.0F, 1.0F, 0.0F}, {1.0F, 1.0F, 1.0F}, {1.0F, 0.0F, 1.0F}}},
     {1.0F, 0.0F, 0.0F}},
    {0, -1, BlockVisualFace::NegativeX,
     {{{0.0F, 0.0F, 1.0F}, {0.0F, 1.0F, 1.0F}, {0.0F, 1.0F, 0.0F}, {0.0F, 0.0F, 0.0F}}},
     {-1.0F, 0.0F, 0.0F}},
    {1, 1, BlockVisualFace::PositiveY,
     {{{0.0F, 1.0F, 1.0F}, {1.0F, 1.0F, 1.0F}, {1.0F, 1.0F, 0.0F}, {0.0F, 1.0F, 0.0F}}},
     {0.0F, 1.0F, 0.0F}},
    {1, -1, BlockVisualFace::NegativeY,
     {{{0.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 1.0F}, {0.0F, 0.0F, 1.0F}}},
     {0.0F, -1.0F, 0.0F}},
    {2, 1, BlockVisualFace::PositiveZ,
     {{{1.0F, 0.0F, 1.0F}, {1.0F, 1.0F, 1.0F}, {0.0F, 1.0F, 1.0F}, {0.0F, 0.0F, 1.0F}}},
     {0.0F, 0.0F, 1.0F}},
    {2, -1, BlockVisualFace::NegativeZ,
     {{{0.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F}, {1.0F, 1.0F, 0.0F}, {1.0F, 0.0F, 0.0F}}},
     {0.0F, 0.0F, -1.0F}},
}};

struct MaterialVisual {
    BlockAtlasTile tile {};
    float material_class = 0.0F;
    float emissive_light = 0.0F;
};

[[nodiscard]] inline auto material_visual(
    ShipMaterial material,
    BlockVisualFace face) noexcept -> MaterialVisual {

    if (material == ShipMaterial::Glass) {
        const auto glass =
            to_block_id(BlockType::Glass);

        return {
            block_atlas_tile(glass, face),
            block_visual_material_value(glass),
            0.0F,
        };
    }

    auto atlas_material =
        ShipAtlasMaterial::DarkHull;

    switch (material) {
    case ShipMaterial::DarkHull:
        atlas_material = ShipAtlasMaterial::DarkHull;
        break;

    case ShipMaterial::LightDeck:
        atlas_material = ShipAtlasMaterial::LightDeck;
        break;

    case ShipMaterial::CleanBeam:
        atlas_material = ShipAtlasMaterial::CleanBeam;
        break;

    case ShipMaterial::CreamCanvas:
        atlas_material = ShipAtlasMaterial::CreamCanvas;
        break;

    case ShipMaterial::Rope:
        atlas_material = ShipAtlasMaterial::Rope;
        break;

    case ShipMaterial::Iron:
        atlas_material = ShipAtlasMaterial::Iron;
        break;

    case ShipMaterial::Brass:
        atlas_material = ShipAtlasMaterial::Brass;
        break;

    case ShipMaterial::Lantern:
        atlas_material = ShipAtlasMaterial::Lantern;
        break;

    case ShipMaterial::BlackCanvas:
        atlas_material = ShipAtlasMaterial::BlackCanvas;
        break;

    case ShipMaterial::SolidGold:
        atlas_material = ShipAtlasMaterial::SolidGold;
        break;

    case ShipMaterial::Glass:
        // Le verre a déjà été traité au début de la fonction.
        break;
    }

    return {
        ship_atlas_tile(atlas_material),
        static_cast<float>(
            static_cast<std::uint8_t>(
                ship_visual_material(atlas_material))),
        material == ShipMaterial::Lantern
            ? 11.0F / 15.0F
            : 0.0F,
    };
}

[[nodiscard]] inline auto is_volume_shape(
    ShipPartShape shape) noexcept -> bool {

    return shape == ShipPartShape::Box ||
           shape == ShipPartShape::Slab ||
           shape == ShipPartShape::Stair;
}

[[nodiscard]] constexpr auto is_canvas_material(
    ShipMaterial material) noexcept -> bool {

    // Les deux toiles partagent la même géométrie souple, tout en gardant
    // chacune une texture indépendante.
    return material == ShipMaterial::CreamCanvas ||
           material == ShipMaterial::BlackCanvas;
}

[[nodiscard]] inline auto is_sky_occluder(
    const ShipPart& part) noexcept -> bool {

    if (!is_volume_shape(part.shape)) {
        return false;
    }

    return part.material != ShipMaterial::Glass &&
           !is_canvas_material(part.material) &&
           part.material != ShipMaterial::Rope &&
           part.material != ShipMaterial::Lantern;
}

[[nodiscard]] inline auto valid_bounds(const LocalBounds& bounds) noexcept -> bool {
    return bounds.max.x - bounds.min.x > kGeometryEpsilon &&
           bounds.max.y - bounds.min.y > kGeometryEpsilon &&
           bounds.max.z - bounds.min.z > kGeometryEpsilon;
}

[[nodiscard]] inline auto climbable_net_diameter(const ShipPart& part) noexcept -> float {
    if (!std::isfinite(part.thickness)) {
        return kClimbableNetMinimumDiameter;
    }
    return std::clamp(std::abs(part.thickness), kClimbableNetMinimumDiameter, 0.12F);
}

[[nodiscard]] inline auto render_bounds(const ShipPart& part) noexcept -> LocalBounds {
    auto bounds = LocalBounds {glm::min(part.local_start, part.local_end), glm::max(part.local_start, part.local_end)};
    if (part.shape == ShipPartShape::ClimbableNet) {
        // J'inclus la bordure renforcee dans les limites de rendu afin que le
        // maillage ne depasse jamais silencieusement l'enveloppe annoncee.
        const auto padding = std::max(
            climbable_net_diameter(part) * kClimbableNetBorderScale * 0.5F,
            0.01F);
        bounds.min -= glm::vec3 {padding};
        bounds.max += glm::vec3 {padding};
        return bounds;
    }
    if (part.shape != ShipPartShape::Panel) {
        return bounds;
    }

    const auto normal = glm::abs(part.orientation);
    const auto half_thickness = std::max(part.thickness * 0.5F, 0.01F);
    const auto center = (bounds.min + bounds.max) * 0.5F;
    if (normal.x >= normal.y && normal.x >= normal.z) {
        bounds.min.x = center.x - half_thickness;
        bounds.max.x = center.x + half_thickness;
    } else if (normal.y >= normal.z) {
        bounds.min.y = center.y - half_thickness;
        bounds.max.y = center.y + half_thickness;
    } else {
        bounds.min.z = center.z - half_thickness;
        bounds.max.z = center.z + half_thickness;
    }
    return bounds;
}

struct LightingContext {
    std::vector<LocalBounds> part_bounds {};
    std::unordered_map<BlockCoord, std::vector<std::uint32_t>, BlockCoordHash> volume_cells {};
    std::unordered_map<BlockCoord, std::vector<std::uint32_t>, BlockCoordHash> sky_columns {};
    std::vector<glm::vec3> lanterns {};

    [[nodiscard]] auto sky_light(const glm::vec3& point) const noexcept -> float {
        const auto column = sky_columns.find({
            static_cast<int>(std::floor(point.x)),
            0,
            static_cast<int>(std::floor(point.z)),
        });
        if (column == sky_columns.end()) {
            return 1.0F;
        }
        auto blockers = 0;
        for (const auto part_index : column->second) {
            const auto& bounds = part_bounds[part_index];
            if (bounds.min.y <= point.y + 0.005F) {
                continue;
            }
            if (point.x > bounds.min.x + 0.015F && point.x < bounds.max.x - 0.015F &&
                point.z > bounds.min.z + 0.015F && point.z < bounds.max.z - 0.015F) {
                ++blockers;
            }
        }
        if (blockers >= 2) {
            return 0.22F;
        }
        return blockers == 1 ? 0.34F : 1.0F;
    }

    [[nodiscard]] auto block_light(const glm::vec3& point) const noexcept -> float {
        auto light = 0.0F;
        const auto point_below_main_deck = point.y < 3.0F;
        for (const auto& lantern : lanterns) {
            if ((lantern.y < 3.0F) != point_below_main_deck) {
                continue;
            }
            const auto distance = glm::length(point - lantern);
            light = std::max(light, (1.0F - distance / 7.0F) * (10.0F / 15.0F));
        }
        return std::clamp(light, 0.0F, 10.0F / 15.0F);
    }

    [[nodiscard]] auto ambient_occlusion(const glm::vec3& point,
                                         const glm::vec3& normal,
                                         float sky) const noexcept -> float {
        (void)point;
        auto ao = sky < 0.9F ? 0.86F : 1.0F;
        if (normal.y < -0.5F) {
            ao -= 0.08F;
        } else if (sky < 0.9F && std::abs(normal.y) < 0.5F) {
            ao -= 0.035F;
        }
        return std::clamp(ao, 0.62F, 1.0F);
    }
};

[[nodiscard]] inline auto make_lighting_context(std::span<const ShipPart> parts) -> LightingContext {
    LightingContext context;
    context.part_bounds.resize(parts.size());
    context.volume_cells.reserve(parts.size() * 8U);
    context.sky_columns.reserve(parts.size() * 4U);
    context.lanterns.reserve(16U);
    for (std::size_t index = 0; index < parts.size(); ++index) {
        const auto& part = parts[index];
        const auto bounds = render_bounds(part);
        context.part_bounds[index] = bounds;
        if (is_sky_occluder(part) && valid_bounds(bounds)) {
            const auto min_x = static_cast<int>(std::floor(bounds.min.x));
            const auto min_y = static_cast<int>(std::floor(bounds.min.y));
            const auto min_z = static_cast<int>(std::floor(bounds.min.z));
            const auto max_x = static_cast<int>(std::floor(bounds.max.x - kGeometryEpsilon));
            const auto max_y = static_cast<int>(std::floor(bounds.max.y - kGeometryEpsilon));
            const auto max_z = static_cast<int>(std::floor(bounds.max.z - kGeometryEpsilon));
            const auto part_index = static_cast<std::uint32_t>(index);
            for (int z = min_z; z <= max_z; ++z) {
                for (int x = min_x; x <= max_x; ++x) {
                    context.sky_columns[{x, 0, z}].push_back(part_index);
                    for (int y = min_y; y <= max_y; ++y) {
                        context.volume_cells[{x, y, z}].push_back(part_index);
                    }
                }
            }
        }
        if (part.material == ShipMaterial::Lantern && valid_bounds(bounds)) {
            context.lanterns.push_back((bounds.min + bounds.max) * 0.5F);
        }
    }
    return context;
}

[[nodiscard]] inline auto visual_face_for_normal(const glm::vec3& normal) noexcept -> BlockVisualFace {
    const auto absolute = glm::abs(normal);
    if (absolute.x >= absolute.y && absolute.x >= absolute.z) {
        return normal.x >= 0.0F ? BlockVisualFace::PositiveX : BlockVisualFace::NegativeX;
    }
    if (absolute.y >= absolute.z) {
        return normal.y >= 0.0F ? BlockVisualFace::PositiveY : BlockVisualFace::NegativeY;
    }
    return normal.z >= 0.0F ? BlockVisualFace::PositiveZ : BlockVisualFace::NegativeZ;
}

[[nodiscard]] inline auto face_shade(const glm::vec3& normal) noexcept -> float {
    if (normal.y > 0.5F) {
        return 1.0F;
    }
    if (normal.y < -0.5F) {
        return 0.64F;
    }
    return std::abs(normal.x) > 0.5F ? 0.84F : 0.76F;
}

inline void append_quad(ChunkMeshData& mesh,
                        std::array<glm::vec3, 4> points,
                        glm::vec3 normal,
                        ShipMaterial material,
                        const LightingContext& lighting,
                        std::size_t source_index) {
    (void)source_index;
    const auto normal_length = glm::length(normal);
    if (normal_length <= kGeometryEpsilon) {
        return;
    }
    normal /= normal_length;
    if (glm::dot(glm::cross(points[1] - points[0], points[2] - points[0]), normal) < 0.0F) {
        std::swap(points[1], points[3]);
    }

    const auto visual = material_visual(material, visual_face_for_normal(normal));
    constexpr auto kAtlasUvStep = 1.0F / static_cast<float>(kBlockAtlasTilesPerAxis);
    constexpr auto kHalfTexel = 0.5F / static_cast<float>(kBlockAtlasSize);
    const auto u0 = static_cast<float>(visual.tile.x) * kAtlasUvStep + kHalfTexel;
    const auto v0 = static_cast<float>(visual.tile.y) * kAtlasUvStep + kHalfTexel;
    const auto u1 = static_cast<float>(visual.tile.x + 1) * kAtlasUvStep - kHalfTexel;
    const auto v1 = static_cast<float>(visual.tile.y + 1) * kAtlasUvStep - kHalfTexel;
    constexpr std::array<std::array<float, 2>, 4> kUvPattern {{{1.0F, 0.0F}, {1.0F, 1.0F}, {0.0F, 1.0F}, {0.0F, 0.0F}}};
    const auto face_center = (points[0] + points[1] + points[2] + points[3]) * 0.25F;
    const auto center_sky = lighting.sky_light(face_center + normal * 0.02F);
    const auto base_index = static_cast<std::uint32_t>(mesh.vertices.size());

    for (std::size_t index = 0; index < points.size(); ++index) {
        const auto u = kUvPattern[index][0] > 0.5F ? u1 : u0;
        const auto v = kUvPattern[index][1] > 0.5F ? v1 : v0;
        const auto lighting_point = points[index] + normal * 0.02F;
        const auto sky = std::min(center_sky, lighting.sky_light(lighting_point));
        const auto block = std::max(visual.emissive_light, lighting.block_light(lighting_point));
        const auto ao = lighting.ambient_occlusion(lighting_point, normal, sky);
        mesh.vertices.push_back({
            points[index].x, points[index].y, points[index].z,
            u, v,
            normal.x, normal.y, normal.z,
            face_shade(normal), ao, sky, block, visual.material_class, 0.0F,
        });
    }
    mesh.indices.insert(mesh.indices.end(), {
        base_index, base_index + 1U, base_index + 2U,
        base_index, base_index + 2U, base_index + 3U,
    });
    ++mesh.face_count;
}

inline void append_tiled_quad(ChunkMeshData& mesh,
                              const std::array<glm::vec3, 4>& points,
                              const glm::vec3& normal,
                              ShipMaterial material,
                              const LightingContext& lighting,
                              std::size_t source_index) {
    constexpr float kMaximumTextureSpan = 2.0F;
    const auto first_edge = points[1] - points[0];
    const auto second_edge = points[3] - points[0];
    const auto first_sections = std::max(1, static_cast<int>(std::ceil(glm::length(first_edge) / kMaximumTextureSpan)));
    const auto second_sections = std::max(1, static_cast<int>(std::ceil(glm::length(second_edge) / kMaximumTextureSpan)));
    const auto interpolate = [&](float first, float second) {
        // Je fais une interpolation bilineaire entre les quatre vrais coins :
        // la meme subdivision fonctionne ainsi pour un rectangle, un trapeze
        // ou un triangle dont l'arete superieure se replie sur son sommet.
        const auto near_edge = glm::mix(points[0], points[1], first);
        const auto far_edge = glm::mix(points[3], points[2], first);
        return glm::mix(near_edge, far_edge, second);
    };

    for (int second = 0; second < second_sections; ++second) {
        const auto second_min = static_cast<float>(second) / static_cast<float>(second_sections);
        const auto second_max = static_cast<float>(second + 1) / static_cast<float>(second_sections);
        for (int first = 0; first < first_sections; ++first) {
            const auto first_min = static_cast<float>(first) / static_cast<float>(first_sections);
            const auto first_max = static_cast<float>(first + 1) / static_cast<float>(first_sections);
            append_quad(
                mesh,
                {{
                    interpolate(first_min, second_min),
                    interpolate(first_max, second_min),
                    interpolate(first_max, second_max),
                    interpolate(first_min, second_max),
                }},
                normal,
                material,
                lighting,
                source_index);
        }
    }
}

[[nodiscard]] inline auto face_fully_occluded(const LocalBounds& bounds,
                                               const FaceDefinition& face,
                                               std::size_t source_index,
                                               const LightingContext& lighting) noexcept -> bool {
    const auto plane = face.direction > 0 ? bounds.max[face.axis] : bounds.min[face.axis];
    const auto first_other_axis = (face.axis + 1) % 3;
    const auto second_other_axis = (face.axis + 2) % 3;
    auto probe = (bounds.min + bounds.max) * 0.5F;
    probe[face.axis] = plane;
    probe += face.normal * 0.002F;
    const auto cell = lighting.volume_cells.find({
        static_cast<int>(std::floor(probe.x)),
        static_cast<int>(std::floor(probe.y)),
        static_cast<int>(std::floor(probe.z)),
    });
    if (cell == lighting.volume_cells.end()) {
        return false;
    }
    for (const auto part_index : cell->second) {
        if (part_index == source_index) {
            continue;
        }
        const auto& other = lighting.part_bounds[part_index];
        const auto touching_plane = face.direction > 0 ? other.min[face.axis] : other.max[face.axis];
        if (std::abs(plane - touching_plane) > kGeometryEpsilon) {
            continue;
        }
        if (other.min[first_other_axis] <= bounds.min[first_other_axis] + kGeometryEpsilon &&
            other.max[first_other_axis] >= bounds.max[first_other_axis] - kGeometryEpsilon &&
            other.min[second_other_axis] <= bounds.min[second_other_axis] + kGeometryEpsilon &&
            other.max[second_other_axis] >= bounds.max[second_other_axis] - kGeometryEpsilon) {
            return true;
        }
    }
    return false;
}

inline void append_cuboid(ChunkMeshData& mesh,
                          const LocalBounds& bounds,
                          ShipMaterial material,
                          const LightingContext& lighting,
                          std::size_t source_index,
                          bool cull_joined_faces) {
    if (!valid_bounds(bounds)) {
        return;
    }
    const auto extent = bounds.max - bounds.min;
    for (const auto& face : kFaces) {
        if (cull_joined_faces && face_fully_occluded(bounds, face, source_index, lighting)) {
            continue;
        }
        std::array<glm::vec3, 4> points {};
        for (std::size_t index = 0; index < points.size(); ++index) {
            points[index] = bounds.min + face.corners[index] * extent;
        }
        append_tiled_quad(mesh, points, face.normal, material, lighting, source_index);
    }
}

inline void append_triangle(ChunkMeshData& mesh,
                            const glm::vec3& first,
                            const glm::vec3& second,
                            const glm::vec3& third,
                            const glm::vec3& normal,
                            ShipMaterial material,
                            const LightingContext& lighting,
                            std::size_t source_index) {
    // Je conserve le format uniforme de quatre sommets tout en subdivisant le
    // triangle : sa texture garde la meme densite que les voiles rectangulaires.
    append_tiled_quad(mesh, {{first, second, third, third}}, normal, material, lighting, source_index);
}

inline void append_canvas_panel(ChunkMeshData& mesh,
                                const ShipPart& part,
                                const LightingContext& lighting,
                                std::size_t source_index) {
    const auto bounds = render_bounds(part);
    if (!valid_bounds(bounds)) {
        return;
    }
    const auto absolute_normal = glm::abs(part.orientation);
    if (absolute_normal.x >= absolute_normal.y && absolute_normal.x >= absolute_normal.z) {
        const auto sign = part.orientation.x >= 0.0F ? 1.0F : -1.0F;
        const auto center_x = (bounds.min.x + bounds.max.x) * 0.5F;
        const auto half_thickness = (bounds.max.x - bounds.min.x) * 0.5F;
        const auto front_x = center_x + sign * half_thickness;
        const auto back_x = center_x - sign * half_thickness;
        const auto apex_z = sign > 0.0F ? bounds.min.z : bounds.max.z;
        const std::array<glm::vec3, 3> front {{
            {front_x, bounds.min.y, bounds.min.z},
            {front_x, bounds.min.y, bounds.max.z},
            {front_x, bounds.max.y, apex_z},
        }};
        const std::array<glm::vec3, 3> back {{
            {back_x, bounds.min.y, bounds.min.z},
            {back_x, bounds.min.y, bounds.max.z},
            {back_x, bounds.max.y, apex_z},
        }};
        const auto front_normal = glm::vec3 {sign, 0.0F, 0.0F};
        append_triangle(mesh, front[0], front[1], front[2], front_normal, part.material, lighting, source_index);
        append_triangle(mesh, back[0], back[2], back[1], -front_normal, part.material, lighting, source_index);
        for (std::size_t edge = 0; edge < front.size(); ++edge) {
            const auto next = (edge + 1U) % front.size();
            const auto edge_direction = front[next] - front[edge];
            const auto edge_normal = glm::normalize(glm::cross(edge_direction, front_normal));
            append_tiled_quad(
                mesh,
                {{front[edge], front[next], back[next], back[edge]}},
                edge_normal,
                part.material,
                lighting,
                source_index);
        }
        return;
    }

    if (absolute_normal.z >= absolute_normal.y) {
        const auto sign = part.orientation.z >= 0.0F ? 1.0F : -1.0F;
        const auto center_z = (bounds.min.z + bounds.max.z) * 0.5F;
        const auto half_thickness = (bounds.max.z - bounds.min.z) * 0.5F;
        const auto front_z = center_z + sign * half_thickness;
        const auto back_z = center_z - sign * half_thickness;
        const auto center_x = (bounds.min.x + bounds.max.x) * 0.5F;
        const auto top_half_width = (bounds.max.x - bounds.min.x) * 0.39F;
        const std::array<glm::vec3, 4> front {{
            {bounds.min.x, bounds.min.y, front_z},
            {bounds.max.x, bounds.min.y, front_z},
            {center_x + top_half_width, bounds.max.y, front_z},
            {center_x - top_half_width, bounds.max.y, front_z},
        }};
        const std::array<glm::vec3, 4> back {{
            {bounds.min.x, bounds.min.y, back_z},
            {bounds.max.x, bounds.min.y, back_z},
            {center_x + top_half_width, bounds.max.y, back_z},
            {center_x - top_half_width, bounds.max.y, back_z},
        }};
        const auto front_normal = glm::vec3 {0.0F, 0.0F, sign};
        append_tiled_quad(mesh, front, front_normal, part.material, lighting, source_index);
        append_tiled_quad(mesh, {{back[0], back[3], back[2], back[1]}}, -front_normal, part.material, lighting, source_index);
        for (std::size_t edge = 0; edge < front.size(); ++edge) {
            const auto next = (edge + 1U) % front.size();
            const auto edge_direction = front[next] - front[edge];
            const auto edge_normal = glm::normalize(glm::cross(edge_direction, front_normal));
            append_tiled_quad(
                mesh,
                {{front[edge], front[next], back[next], back[edge]}},
                edge_normal,
                part.material,
                lighting,
                source_index);
        }
        return;
    }

    append_cuboid(mesh, bounds, part.material, lighting, source_index, false);
}

inline void append_segment(ChunkMeshData& mesh,
                           const glm::vec3& start,
                           const glm::vec3& end,
                           float diameter,
                           ShipMaterial material,
                           const LightingContext& lighting,
                           std::size_t source_index) {
    const auto axis_vector = end - start;
    const auto length = glm::length(axis_vector);
    if (length <= kGeometryEpsilon || diameter <= kGeometryEpsilon) {
        return;
    }
    const auto direction = axis_vector / length;
    const auto reference = std::abs(direction.y) < 0.92F ? glm::vec3 {0.0F, 1.0F, 0.0F}
                                                         : glm::vec3 {1.0F, 0.0F, 0.0F};
    const auto side = glm::normalize(glm::cross(direction, reference));
    const auto up = glm::normalize(glm::cross(side, direction));
    const auto half = std::max(diameter * 0.5F, 0.01F);
    const auto s = side * half;
    const auto u = up * half;
    const std::array<glm::vec3, 8> corners {{
        start - s - u, start + s - u, start + s + u, start - s + u,
        end - s - u, end + s - u, end + s + u, end - s + u,
    }};
    append_quad(mesh, {{corners[0], corners[1], corners[2], corners[3]}}, -direction, material, lighting, source_index);
    append_quad(mesh, {{corners[4], corners[5], corners[6], corners[7]}}, direction, material, lighting, source_index);
    append_tiled_quad(mesh, {{corners[0], corners[4], corners[5], corners[1]}}, -up, material, lighting, source_index);
    append_tiled_quad(mesh, {{corners[3], corners[2], corners[6], corners[7]}}, up, material, lighting, source_index);
    append_tiled_quad(mesh, {{corners[0], corners[3], corners[7], corners[4]}}, -side, material, lighting, source_index);
    append_tiled_quad(mesh, {{corners[1], corners[5], corners[6], corners[2]}}, side, material, lighting, source_index);
}

[[nodiscard]] inline auto climbable_net_normal_axis(const ShipPart& part) noexcept -> int {
    const auto absolute_normal = glm::abs(part.orientation);
    if (absolute_normal.x > kGeometryEpsilon || absolute_normal.z > kGeometryEpsilon) {
        return absolute_normal.x >= absolute_normal.z ? 0 : 2;
    }

    // Je deduis aussi le plan depuis son axe aplati pour garder un rendu sain
    // si une donnee ancienne ne fournit pas encore de normale explicite.
    const auto extent = glm::abs(part.local_end - part.local_start);
    return extent.x <= extent.z ? 0 : 2;
}

[[nodiscard]] inline auto climbable_net_section_count(float span) noexcept -> int {
    if (!std::isfinite(span) || span <= kGeometryEpsilon) {
        return 0;
    }
    const auto requested = std::ceil(span / kClimbableNetSpacing);
    return std::max(1, static_cast<int>(std::min(requested, static_cast<float>(kClimbableNetMaximumSections))));
}

inline void append_climbable_net(ChunkMeshData& mesh,
                                 const ShipPart& part,
                                 const LightingContext& lighting,
                                 std::size_t source_index) {
    const auto bounds = LocalBounds {
        glm::min(part.local_start, part.local_end),
        glm::max(part.local_start, part.local_end),
    };
    const auto normal_axis = climbable_net_normal_axis(part);
    const auto tangent_axis = normal_axis == 0 ? 2 : 0;
    const auto vertical_span = bounds.max.y - bounds.min.y;
    const auto tangent_span = bounds.max[tangent_axis] - bounds.min[tangent_axis];
    const auto vertical_sections = climbable_net_section_count(vertical_span);
    const auto tangent_sections = climbable_net_section_count(tangent_span);
    if (vertical_sections == 0 || tangent_sections == 0 ||
        !std::isfinite(bounds.min[normal_axis]) || !std::isfinite(bounds.max[normal_axis])) {
        return;
    }

    const auto plane = (bounds.min[normal_axis] + bounds.max[normal_axis]) * 0.5F;
    if (!std::isfinite(plane)) {
        return;
    }
    const auto diameter = climbable_net_diameter(part);
    const auto point = [&](float tangent, float vertical) {
        auto result = glm::vec3 {0.0F};
        result[normal_axis] = plane;
        result[tangent_axis] = tangent;
        result.y = vertical;
        return result;
    };
    const auto line_diameter = [&](int section, int section_count) {
        return section == 0 || section == section_count
                   ? diameter * kClimbableNetBorderScale
                   : diameter;
    };

    // Je dessine des cordes distinctes : les espaces entre elles restent donc
    // vraiment ouverts et ne peuvent ni masquer le ciel ni simuler un panneau.
    for (int section = 0; section <= tangent_sections; ++section) {
        const auto ratio = static_cast<float>(section) / static_cast<float>(tangent_sections);
        const auto tangent = std::lerp(bounds.min[tangent_axis], bounds.max[tangent_axis], ratio);
        append_segment(
            mesh,
            point(tangent, bounds.min.y),
            point(tangent, bounds.max.y),
            line_diameter(section, tangent_sections),
            ShipMaterial::Rope,
            lighting,
            source_index);
    }
    for (int section = 0; section <= vertical_sections; ++section) {
        const auto ratio = static_cast<float>(section) / static_cast<float>(vertical_sections);
        const auto vertical = std::lerp(bounds.min.y, bounds.max.y, ratio);
        append_segment(
            mesh,
            point(bounds.min[tangent_axis], vertical),
            point(bounds.max[tangent_axis], vertical),
            line_diameter(section, vertical_sections),
            ShipMaterial::Rope,
            lighting,
            source_index);
    }
}

[[nodiscard]] inline auto glyph_rows(char32_t glyph) noexcept -> std::array<std::uint8_t, 7> {
    switch (glyph) {
    case U'L': return {{0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}};
    case U'\'': return {{0x0C, 0x0C, 0x08, 0x10, 0x00, 0x00, 0x00}};
    case U'A': return {{0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}};
    case U'a':
        return {{
            0x00,
            0x00,
            0x0E,
            0x01,
            0x0F,
            0x11,
            0x0F,
        }};
    case U'm': return {{0x00, 0x00, 0x1A, 0x15, 0x15, 0x15, 0x15}};
    case U'\u00E9': return {{0x04, 0x08, 0x0E, 0x11, 0x1F, 0x10, 0x0F}};
    case U'l': return {{0x0C, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}};
    case U'i': return {{0x04, 0x00, 0x0C, 0x04, 0x04, 0x04, 0x0E}};
    case U'e': return {{0x00, 0x00, 0x0E, 0x11, 0x1F, 0x10, 0x0F}};
    default: return {};
    }
}

inline void append_glyph(ChunkMeshData& mesh,
                         const ShipPart& part,
                         const LightingContext& lighting,
                         std::size_t source_index) {
    const auto bounds = LocalBounds {glm::min(part.local_start, part.local_end), glm::max(part.local_start, part.local_end)};
    if (!valid_bounds(bounds)) {
        return;
    }
    const auto rows = glyph_rows(part.glyph);
    const auto pixel_width = (bounds.max.x - bounds.min.x) / 5.0F;
    const auto pixel_height = (bounds.max.y - bounds.min.y) / 7.0F;
    for (std::size_t row = 0; row < rows.size(); ++row) {
        for (int column = 0; column < 5; ++column) {
            if ((rows[row] & (1U << static_cast<unsigned>(4 - column))) == 0U) {
                continue;
            }
            const auto displayed_column = part.orientation.z < 0.0F ? 4 - column : column;
            const auto min_x = bounds.min.x + static_cast<float>(displayed_column) * pixel_width;
            const auto max_y = bounds.max.y - static_cast<float>(row) * pixel_height;
            const LocalBounds pixel {
                {min_x, max_y - pixel_height, bounds.min.z},
                {min_x + pixel_width * 0.82F, max_y - pixel_height * 0.10F, bounds.max.z},
            };
            append_cuboid(mesh, pixel, part.material, lighting, source_index, false);
        }
    }
}

inline void append_wheel(ChunkMeshData& mesh,
                         const ShipPart& part,
                         const LightingContext& lighting,
                         std::size_t source_index) {
    const auto bounds = LocalBounds {glm::min(part.local_start, part.local_end), glm::max(part.local_start, part.local_end)};
    const auto center = (bounds.min + bounds.max) * 0.5F;
    const auto radius = std::min(bounds.max.x - bounds.min.x, bounds.max.y - bounds.min.y) * 0.5F;
    if (radius <= kGeometryEpsilon) {
        return;
    }
    constexpr int kRingSections = 24;
    constexpr int kSpokes = 8;
    constexpr float kPi = 3.14159265358979323846F;
    for (int section = 0; section < kRingSections; ++section) {
        const auto first_angle = static_cast<float>(section) * (2.0F * kPi / static_cast<float>(kRingSections));
        const auto second_angle = static_cast<float>(section + 1) * (2.0F * kPi / static_cast<float>(kRingSections));
        const auto first = center + glm::vec3 {std::cos(first_angle) * radius, std::sin(first_angle) * radius, 0.0F};
        const auto second = center + glm::vec3 {std::cos(second_angle) * radius, std::sin(second_angle) * radius, 0.0F};
        append_segment(mesh, first, second, part.thickness, part.material, lighting, source_index);
    }
    for (int spoke = 0; spoke < kSpokes; ++spoke) {
        const auto angle = static_cast<float>(spoke) * (2.0F * kPi / static_cast<float>(kSpokes));
        const auto end = center + glm::vec3 {std::cos(angle) * radius * 0.88F, std::sin(angle) * radius * 0.88F, 0.0F};
        append_segment(mesh, center, end, part.thickness * 0.72F, part.material, lighting, source_index);
    }
    append_segment(mesh,
                   center - glm::vec3 {0.0F, 0.0F, std::max(part.thickness, 0.08F)},
                   center + glm::vec3 {0.0F, 0.0F, std::max(part.thickness, 0.08F)},
                   part.thickness * 1.8F,
                   part.material,
                   lighting,
                   source_index);
}

} // namespace ship_mesh_detail

[[nodiscard]] inline auto build_ship_mesh_data(std::span<const ShipPart> parts) -> ChunkMeshData {
    ChunkMeshData mesh {};
    if (parts.empty()) {
        return mesh;
    }

    // Je construis ce maillage une seule fois par revision et mes index locaux
    // gardent le culling des jonctions ainsi que l'eclairage sous le budget de chargement.
    mesh.vertices.reserve(parts.size() * 28U);
    mesh.indices.reserve(parts.size() * 42U);
    const auto lighting = ship_mesh_detail::make_lighting_context(parts);
    for (std::size_t index = 0; index < parts.size(); ++index) {
        const auto& part = parts[index];
        switch (part.shape) {
        case ShipPartShape::Box:
        case ShipPartShape::Slab:
        case ShipPartShape::Stair:
            ship_mesh_detail::append_cuboid(
                mesh,
                ship_mesh_detail::render_bounds(part),
                part.material,
                lighting,
                index,
                true);
            break;
        case ShipPartShape::Panel:
            if (ship_mesh_detail::is_canvas_material(part.material)) {
                ship_mesh_detail::append_canvas_panel(
                    mesh,
                    part,
                    lighting,
                    index);
            } else {
                ship_mesh_detail::append_cuboid(
                    mesh,
                    ship_mesh_detail::render_bounds(part),
                    part.material,
                    lighting,
                    index,
                    false);
            }
            break;
        case ShipPartShape::Segment:
            ship_mesh_detail::append_segment(
                mesh,
                part.local_start,
                part.local_end,
                part.thickness,
                part.material,
                lighting,
                index);
            break;
        case ShipPartShape::ClimbableNet:
            ship_mesh_detail::append_climbable_net(mesh, part, lighting, index);
            break;
        case ShipPartShape::Wheel:
            ship_mesh_detail::append_wheel(mesh, part, lighting, index);
            break;
        case ShipPartShape::Glyph:
            ship_mesh_detail::append_glyph(mesh, part, lighting, index);
            break;
        }
    }
    return mesh;
}

} // namespace valcraft
