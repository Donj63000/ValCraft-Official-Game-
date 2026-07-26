#include "render/TerrainVisualSample.h"

#include <glm/geometric.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>

namespace valcraft {
namespace {

struct ClosestTrianglePoint {
    glm::vec3 position {0.0F};
    std::array<float, 3> barycentric {{1.0F, 0.0F, 0.0F}};
};

[[nodiscard]] auto finite_vec3(const glm::vec3& value) noexcept -> bool {
    return std::isfinite(value.x) &&
           std::isfinite(value.y) &&
           std::isfinite(value.z);
}

[[nodiscard]] auto closest_point_on_triangle(
    const glm::vec3& point,
    const glm::vec3& a,
    const glm::vec3& b,
    const glm::vec3& c) noexcept -> ClosestTrianglePoint {

    // Je suis la partition de Voronoï du triangle afin d'obtenir à la fois
    // le point le plus proche et des poids d'interpolation stables.
    const auto ab = b - a;
    const auto ac = c - a;
    const auto ap = point - a;
    const auto d1 = glm::dot(ab, ap);
    const auto d2 = glm::dot(ac, ap);
    if (d1 <= 0.0F && d2 <= 0.0F) {
        return {a, {{1.0F, 0.0F, 0.0F}}};
    }

    const auto bp = point - b;
    const auto d3 = glm::dot(ab, bp);
    const auto d4 = glm::dot(ac, bp);
    if (d3 >= 0.0F && d4 <= d3) {
        return {b, {{0.0F, 1.0F, 0.0F}}};
    }

    const auto vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0F && d1 >= 0.0F && d3 <= 0.0F) {
        const auto v = d1 / std::max(d1 - d3, 1.0e-12F);
        return {a + ab * v, {{1.0F - v, v, 0.0F}}};
    }

    const auto cp = point - c;
    const auto d5 = glm::dot(ab, cp);
    const auto d6 = glm::dot(ac, cp);
    if (d6 >= 0.0F && d5 <= d6) {
        return {c, {{0.0F, 0.0F, 1.0F}}};
    }

    const auto vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0F && d2 >= 0.0F && d6 <= 0.0F) {
        const auto w = d2 / std::max(d2 - d6, 1.0e-12F);
        return {a + ac * w, {{1.0F - w, 0.0F, w}}};
    }

    const auto va = d3 * d6 - d5 * d4;
    if (va <= 0.0F && d4 - d3 >= 0.0F && d5 - d6 >= 0.0F) {
        const auto denominator =
            std::max((d4 - d3) + (d5 - d6), 1.0e-12F);
        const auto w = (d4 - d3) / denominator;
        return {b + (c - b) * w, {{0.0F, 1.0F - w, w}}};
    }

    const auto denominator = va + vb + vc;
    if (!std::isfinite(denominator) ||
        std::abs(denominator) <= 1.0e-12F) {
        return {a, {{1.0F, 0.0F, 0.0F}}};
    }
    const auto inverse = 1.0F / denominator;
    const auto v = vb * inverse;
    const auto w = vc * inverse;
    return {a + ab * v + ac * w, {{1.0F - v - w, v, w}}};
}

[[nodiscard]] auto position_of(const TerrainVertex& vertex) noexcept
    -> glm::vec3 {
    return {vertex.x, vertex.y, vertex.z};
}

[[nodiscard]] auto normal_of(const TerrainVertex& vertex) noexcept
    -> glm::vec3 {
    return {vertex.nx, vertex.ny, vertex.nz};
}

[[nodiscard]] auto dominant_vertex_index(
    const std::array<float, 3>& barycentric) noexcept -> std::size_t {

    auto result = std::size_t {0U};
    for (std::size_t index = 1U; index < barycentric.size(); ++index) {
        if (barycentric[index] > barycentric[result]) {
            result = index;
        }
    }
    return result;
}

} // namespace

auto sample_terrain_visual_mesh(
    const OrganicTerrainMesh& mesh,
    const TerrainVisualQuery& query,
    std::uint64_t mesh_revision) noexcept
    -> std::optional<TerrainVisualSample> {

    if (!finite_vec3(query.world_position) ||
        !std::isfinite(query.maximum_distance) ||
        !std::isfinite(query.minimum_normal_y) ||
        query.maximum_distance < 0.0F) {
        return std::nullopt;
    }

    const auto maximum_distance_squared =
        query.maximum_distance * query.maximum_distance;
    auto closest_distance_squared =
        (std::numeric_limits<float>::max)();
    std::optional<TerrainVisualSample> result {};

    for (std::size_t triangle = 0U;
         triangle + 2U < mesh.indices.size();
         triangle += 3U) {
        const std::array<std::uint32_t, 3> indices {{
            mesh.indices[triangle],
            mesh.indices[triangle + 1U],
            mesh.indices[triangle + 2U],
        }};
        if (indices[0] >= mesh.vertices.size() ||
            indices[1] >= mesh.vertices.size() ||
            indices[2] >= mesh.vertices.size()) {
            continue;
        }

        const std::array<const TerrainVertex*, 3> vertices {{
            &mesh.vertices[indices[0]],
            &mesh.vertices[indices[1]],
            &mesh.vertices[indices[2]],
        }};
        const auto closest = closest_point_on_triangle(
            query.world_position,
            position_of(*vertices[0]),
            position_of(*vertices[1]),
            position_of(*vertices[2]));
        if (!finite_vec3(closest.position)) {
            continue;
        }

        auto normal =
            normal_of(*vertices[0]) * closest.barycentric[0] +
            normal_of(*vertices[1]) * closest.barycentric[1] +
            normal_of(*vertices[2]) * closest.barycentric[2];
        const auto normal_length_squared = glm::dot(normal, normal);
        if (!std::isfinite(normal_length_squared) ||
            normal_length_squared <= 1.0e-12F) {
            continue;
        }
        normal *= 1.0F / std::sqrt(normal_length_squared);
        if (normal.y < query.minimum_normal_y) {
            continue;
        }

        const auto offset = query.world_position - closest.position;
        const auto distance_squared = glm::dot(offset, offset);
        if (!std::isfinite(distance_squared) ||
            distance_squared > maximum_distance_squared ||
            distance_squared >= closest_distance_squared) {
            continue;
        }

        const auto dominant = dominant_vertex_index(closest.barycentric);
        const auto& material_vertex = *vertices[dominant];
        const auto cutout_surface =
            (material_vertex.surface_flags & 1U) != 0U;
        const auto interpolate_byte =
            [&](auto member) noexcept {
                return (
                    static_cast<float>(vertices[0]->*member) *
                        closest.barycentric[0] +
                    static_cast<float>(vertices[1]->*member) *
                        closest.barycentric[1] +
                    static_cast<float>(vertices[2]->*member) *
                        closest.barycentric[2]);
            };

        closest_distance_squared = distance_squared;
        result = TerrainVisualSample {
            closest.position,
            normal,
            visual_material_for_block(
                material_vertex.primary_block_id),
            cutout_surface
                ? VisualMaterialId::None
                : visual_material_for_block(
                      material_vertex.secondary_block_id),
            cutout_surface
                ? 0.0F
                : std::clamp(
                      interpolate_byte(
                          &TerrainVertex::material_blend) /
                          255.0F,
                      0.0F,
                      1.0F),
            std::clamp(
                interpolate_byte(&TerrainVertex::ambient_occlusion) / 255.0F,
                0.0F,
                1.0F),
            std::clamp(
                interpolate_byte(&TerrainVertex::sky_light) / 15.0F,
                0.0F,
                1.0F),
            std::clamp(
                interpolate_byte(&TerrainVertex::block_light) / 15.0F,
                0.0F,
                1.0F),
            distance_squared,
            mesh_revision,
        };
    }

    return result;
}

} // namespace valcraft
