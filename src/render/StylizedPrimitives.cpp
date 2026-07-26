#include "render/StylizedPrimitives.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/vec2.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <utility>

namespace valcraft {

namespace {

constexpr float kHalfExtent = 0.5F;
constexpr float kNormalEpsilonSquared = 1.0e-16F;
constexpr float kTau = std::numbers::pi_v<float> * 2.0F;

struct LodProfile {
    int rounded_box_segments = 2;
    int radial_segments = 8;
    int ellipsoid_stacks = 6;
    int capsule_hemisphere_rings = 3;
    int panel_segments = 2;
    int ribbon_segments = 8;
};

struct SurfacePoint {
    glm::vec3 position {0.0F};
    glm::vec3 normal {0.0F, 1.0F, 0.0F};
};

[[nodiscard]] auto profile_for(StylizedPrimitiveLod lod) noexcept -> LodProfile {
    switch (lod) {
    case StylizedPrimitiveLod::Low:
        return LodProfile {2, 8, 6, 3, 2, 8};
    case StylizedPrimitiveLod::Medium:
        return LodProfile {4, 16, 10, 5, 4, 16};
    case StylizedPrimitiveLod::High:
        return LodProfile {8, 24, 16, 8, 8, 32};
    }
    // Je garde un repli deterministe si une valeur enumeree corrompue arrive.
    return LodProfile {4, 16, 10, 5, 4, 16};
}

[[nodiscard]] auto safe_normalize(const glm::vec3& value, const glm::vec3& fallback) -> glm::vec3 {
    const auto length_squared = glm::dot(value, value);
    if (!std::isfinite(length_squared) || length_squared <= kNormalEpsilonSquared) {
        return fallback;
    }
    return value / std::sqrt(length_squared);
}

[[nodiscard]] auto dominant_face(const glm::vec3& value) noexcept -> std::uint8_t {
    const auto absolute = glm::abs(value);
    if (absolute.x >= absolute.y && absolute.x >= absolute.z) {
        return value.x >= 0.0F ? 0U : 1U;
    }
    if (absolute.y >= absolute.z) {
        return value.y >= 0.0F ? 2U : 3U;
    }
    return value.z >= 0.0F ? 4U : 5U;
}

[[nodiscard]] auto projected_face_uv(const glm::vec3& position, std::uint8_t face) noexcept -> glm::vec2 {
    glm::vec2 uv {0.5F};
    switch (face) {
    case 0U:
        uv = glm::vec2 {0.5F - position.z, position.y + 0.5F};
        break;
    case 1U:
        uv = glm::vec2 {position.z + 0.5F, position.y + 0.5F};
        break;
    case 2U:
        uv = glm::vec2 {position.z + 0.5F, position.x + 0.5F};
        break;
    case 3U:
        uv = glm::vec2 {0.5F - position.z, position.x + 0.5F};
        break;
    case 4U:
        uv = glm::vec2 {position.x + 0.5F, position.y + 0.5F};
        break;
    case 5U:
        uv = glm::vec2 {0.5F - position.x, position.y + 0.5F};
        break;
    default:
        break;
    }
    return glm::clamp(uv, glm::vec2 {0.0F}, glm::vec2 {1.0F});
}

[[nodiscard]] auto make_vertex(
    const glm::vec3& position,
    const glm::vec3& normal,
    const glm::vec2& uv,
    std::uint8_t face) -> StylizedPrimitiveVertex {
    const auto unit_normal = safe_normalize(normal, glm::vec3 {0.0F, 1.0F, 0.0F});
    return StylizedPrimitiveVertex {
        position.x,
        position.y,
        position.z,
        unit_normal.x,
        unit_normal.y,
        unit_normal.z,
        std::clamp(uv.x, 0.0F, 1.0F),
        std::clamp(uv.y, 0.0F, 1.0F),
        static_cast<float>(face),
    };
}

void append_oriented_triangle(
    StylizedPrimitiveMesh& mesh,
    std::array<SurfacePoint, 3> points,
    int forced_face = -1,
    const std::array<glm::vec2, 3>* explicit_uvs = nullptr) {
    auto face_normal = glm::cross(
        points[1].position - points[0].position,
        points[2].position - points[0].position);
    const auto area_squared = glm::dot(face_normal, face_normal);
    if (!std::isfinite(area_squared) || area_squared <= kNormalEpsilonSquared) {
        throw std::logic_error("stylized primitive emitted a degenerate triangle");
    }

    auto averaged_normal = points[0].normal + points[1].normal + points[2].normal;
    averaged_normal = safe_normalize(averaged_normal, safe_normalize(face_normal, glm::vec3 {0.0F, 1.0F, 0.0F}));

    std::array<glm::vec2, 3> uvs {};
    if (explicit_uvs != nullptr) {
        uvs = *explicit_uvs;
    }

    if (glm::dot(face_normal, averaged_normal) < 0.0F) {
        std::swap(points[1], points[2]);
        if (explicit_uvs != nullptr) {
            std::swap(uvs[1], uvs[2]);
        }
    }

    const auto face = forced_face >= 0
        ? static_cast<std::uint8_t>(forced_face)
        : dominant_face(averaged_normal);
    if (face > 5U) {
        throw std::logic_error("stylized primitive emitted an invalid atlas face");
    }

    if (mesh.vertices.size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()) - 3U) {
        throw std::length_error("stylized primitive exceeds 32-bit index capacity");
    }
    const auto first = static_cast<std::uint32_t>(mesh.vertices.size());
    for (std::size_t point_index = 0; point_index < points.size(); ++point_index) {
        const auto uv = explicit_uvs != nullptr
            ? uvs[point_index]
            : projected_face_uv(points[point_index].position, face);
        mesh.vertices.push_back(make_vertex(
            points[point_index].position,
            points[point_index].normal,
            uv,
            face));
    }
    mesh.indices.push_back(first);
    mesh.indices.push_back(first + 1U);
    mesh.indices.push_back(first + 2U);
}

void finish_bounds(StylizedPrimitiveMesh& mesh) noexcept {
    if (mesh.vertices.empty()) {
        mesh.bounds = {};
        return;
    }

    auto minimum = glm::vec3 {std::numeric_limits<float>::max()};
    auto maximum = glm::vec3 {std::numeric_limits<float>::lowest()};
    for (const auto& vertex : mesh.vertices) {
        const glm::vec3 position {vertex.x, vertex.y, vertex.z};
        minimum = glm::min(minimum, position);
        maximum = glm::max(maximum, position);
    }
    mesh.bounds = StylizedPrimitiveBounds {minimum, maximum};
}

[[nodiscard]] auto circle_components(int index, int segment_count) -> glm::vec2 {
    auto wrapped_index = index % segment_count;
    if (wrapped_index < 0) {
        wrapped_index += segment_count;
    }

    const auto quarter = segment_count / 4;
    if (wrapped_index == 0) {
        return glm::vec2 {1.0F, 0.0F};
    }
    if (wrapped_index == quarter) {
        return glm::vec2 {0.0F, 1.0F};
    }
    if (wrapped_index == quarter * 2) {
        return glm::vec2 {-1.0F, 0.0F};
    }
    if (wrapped_index == quarter * 3) {
        return glm::vec2 {0.0F, -1.0F};
    }

    const auto angle =
        kTau * static_cast<float>(wrapped_index) / static_cast<float>(segment_count);
    return glm::vec2 {std::cos(angle), std::sin(angle)};
}

[[nodiscard]] auto latitude_components(int stack, int stack_count) -> glm::vec2 {
    if (stack <= 0) {
        return glm::vec2 {0.0F, -1.0F};
    }
    if (stack >= stack_count) {
        return glm::vec2 {0.0F, 1.0F};
    }
    if (stack * 2 == stack_count) {
        return glm::vec2 {1.0F, 0.0F};
    }

    const auto angle = -std::numbers::pi_v<float> * 0.5F +
        std::numbers::pi_v<float> * static_cast<float>(stack) / static_cast<float>(stack_count);
    return glm::vec2 {std::cos(angle), std::sin(angle)};
}

[[nodiscard]] auto ellipsoid_point(int stack, int slice, int stack_count, int slice_count) -> SurfacePoint {
    const auto latitude = latitude_components(stack, stack_count);
    const auto longitude = circle_components(slice, slice_count);
    const glm::vec3 position {
        kHalfExtent * latitude.x * longitude.x,
        kHalfExtent * latitude.y,
        kHalfExtent * latitude.x * longitude.y,
    };
    return SurfacePoint {
        position,
        safe_normalize(position, glm::vec3 {0.0F, position.y >= 0.0F ? 1.0F : -1.0F, 0.0F}),
    };
}

void append_parametric_band(
    StylizedPrimitiveMesh& mesh,
    const SurfacePoint& lower_left,
    const SurfacePoint& lower_right,
    const SurfacePoint& upper_left,
    const SurfacePoint& upper_right) {
    append_oriented_triangle(mesh, {lower_left, lower_right, upper_right});
    append_oriented_triangle(mesh, {lower_left, upper_right, upper_left});
}

[[nodiscard]] auto capsule_hemisphere_point(
    bool top,
    int ring,
    int hemisphere_rings,
    int slice,
    int slice_count) -> SurfacePoint {
    const auto longitude = circle_components(slice, slice_count);

    float cosine = 0.0F;
    float sine = top ? 1.0F : -1.0F;
    if (ring > 0 && ring < hemisphere_rings) {
        const auto normalized_ring =
            static_cast<float>(ring) / static_cast<float>(hemisphere_rings);
        const auto angle = normalized_ring * std::numbers::pi_v<float> * 0.5F;
        cosine = std::sin(angle);
        sine = top ? std::cos(angle) : -std::cos(angle);
    } else if (ring == hemisphere_rings) {
        cosine = 1.0F;
        sine = 0.0F;
    }

    // Pour la calotte superieure, l'anneau zero est le pole et le dernier
    // anneau est l'equateur ; je remonte ensuite l'ordre lors de l'assemblage.
    const auto center_y = top
        ? kStylizedCapsuleCylinderHalfHeight
        : -kStylizedCapsuleCylinderHalfHeight;
    auto position = glm::vec3 {
        kStylizedCapsuleRadius * cosine * longitude.x,
        center_y + kStylizedCapsuleRadius * sine,
        kStylizedCapsuleRadius * cosine * longitude.y,
    };
    if (ring == 0) {
        position.y = top ? kHalfExtent : -kHalfExtent;
    }
    const glm::vec3 normal {
        cosine * longitude.x,
        sine,
        cosine * longitude.y,
    };
    return SurfacePoint {position, safe_normalize(normal, glm::vec3 {0.0F, top ? 1.0F : -1.0F, 0.0F})};
}

[[nodiscard]] auto capsule_cylinder_point(bool top, int slice, int slice_count) -> SurfacePoint {
    const auto longitude = circle_components(slice, slice_count);
    return SurfacePoint {
        glm::vec3 {
            kStylizedCapsuleRadius * longitude.x,
            top ? kStylizedCapsuleCylinderHalfHeight : -kStylizedCapsuleCylinderHalfHeight,
            kStylizedCapsuleRadius * longitude.y,
        },
        glm::vec3 {longitude.x, 0.0F, longitude.y},
    };
}

[[nodiscard]] auto tapered_cylinder_point(bool top, int slice, int slice_count) -> SurfacePoint {
    const auto longitude = circle_components(slice, slice_count);
    const auto radius = top ? kStylizedTaperedCylinderTopRadius : kHalfExtent;
    const auto slope = kHalfExtent - kStylizedTaperedCylinderTopRadius;
    return SurfacePoint {
        glm::vec3 {
            radius * longitude.x,
            top ? kHalfExtent : -kHalfExtent,
            radius * longitude.y,
        },
        safe_normalize(
            glm::vec3 {longitude.x, slope, longitude.y},
            glm::vec3 {longitude.x, 0.0F, longitude.y}),
    };
}

[[nodiscard]] auto rounded_box_source_point(std::uint8_t face, float u, float v) noexcept -> glm::vec3 {
    switch (face) {
    case 0U:
        return glm::vec3 {0.5F, v - 0.5F, 0.5F - u};
    case 1U:
        return glm::vec3 {-0.5F, v - 0.5F, u - 0.5F};
    case 2U:
        return glm::vec3 {v - 0.5F, 0.5F, u - 0.5F};
    case 3U:
        return glm::vec3 {v - 0.5F, -0.5F, 0.5F - u};
    case 4U:
        return glm::vec3 {u - 0.5F, v - 0.5F, 0.5F};
    case 5U:
        return glm::vec3 {0.5F - u, v - 0.5F, -0.5F};
    default:
        return glm::vec3 {0.0F};
    }
}

[[nodiscard]] auto rounded_box_point(const glm::vec3& source) -> SurfacePoint {
    constexpr auto inner_half_extent = kHalfExtent - kStylizedRoundedBoxRadius;
    const glm::vec3 inner {
        std::clamp(source.x, -inner_half_extent, inner_half_extent),
        std::clamp(source.y, -inner_half_extent, inner_half_extent),
        std::clamp(source.z, -inner_half_extent, inner_half_extent),
    };
    const auto delta = source - inner;
    const auto normal = safe_normalize(delta, safe_normalize(source, glm::vec3 {0.0F, 1.0F, 0.0F}));
    return SurfacePoint {inner + normal * kStylizedRoundedBoxRadius, normal};
}

[[nodiscard]] auto panel_point(float u, float v) -> SurfacePoint {
    const auto x = u - 0.5F;
    const auto y = v - 0.5F;
    const auto x_factor = 1.0F - 4.0F * x * x;
    const auto y_factor = 1.0F - 4.0F * y * y;
    const auto z = kStylizedPanelBow * x_factor * y_factor;
    const auto dz_dx = -8.0F * kStylizedPanelBow * x * y_factor;
    const auto dz_dy = -8.0F * kStylizedPanelBow * y * x_factor;
    return SurfacePoint {
        glm::vec3 {x, y, z},
        safe_normalize(glm::vec3 {-dz_dx, -dz_dy, 1.0F}, glm::vec3 {0.0F, 0.0F, 1.0F}),
    };
}

[[nodiscard]] auto periodic_wave(int step, int segment_count) -> float {
    const auto quarter = segment_count / 4;
    if (step == 0 || step == segment_count || step == quarter * 2) {
        return 0.0F;
    }
    if (step == quarter) {
        return 1.0F;
    }
    if (step == quarter * 3) {
        return -1.0F;
    }
    return std::sin(kTau * static_cast<float>(step) / static_cast<float>(segment_count));
}

[[nodiscard]] auto periodic_wave_derivative(int step, int segment_count) -> float {
    const auto quarter = segment_count / 4;
    if (step == 0 || step == segment_count) {
        return 1.0F;
    }
    if (step == quarter || step == quarter * 3) {
        return 0.0F;
    }
    if (step == quarter * 2) {
        return -1.0F;
    }
    return std::cos(kTau * static_cast<float>(step) / static_cast<float>(segment_count));
}

[[nodiscard]] auto ribbon_point(int step, int segment_count, bool upper_edge) -> SurfacePoint {
    const auto u = static_cast<float>(step) / static_cast<float>(segment_count);
    const auto z = kStylizedRibbonWaveAmplitude * periodic_wave(step, segment_count);
    const auto dz_dx =
        kStylizedRibbonWaveAmplitude * kTau * periodic_wave_derivative(step, segment_count);
    return SurfacePoint {
        glm::vec3 {
            u - 0.5F,
            upper_edge ? kStylizedRibbonHalfWidth : -kStylizedRibbonHalfWidth,
            z,
        },
        safe_normalize(glm::vec3 {-dz_dx, 0.0F, 1.0F}, glm::vec3 {0.0F, 0.0F, 1.0F}),
    };
}

} // namespace

auto build_stylized_rounded_box(StylizedPrimitiveLod lod) -> StylizedPrimitiveMesh {
    const auto profile = profile_for(lod);
    const auto segments = profile.rounded_box_segments;
    StylizedPrimitiveMesh mesh;

    const auto vertices_per_face =
        static_cast<std::size_t>((segments + 1) * (segments + 1));
    mesh.vertices.reserve(vertices_per_face * 6U);
    mesh.indices.reserve(static_cast<std::size_t>(segments * segments) * 36U);

    for (std::uint8_t face = 0U; face < 6U; ++face) {
        const auto face_start = static_cast<std::uint32_t>(mesh.vertices.size());
        for (int v_index = 0; v_index <= segments; ++v_index) {
            const auto v = static_cast<float>(v_index) / static_cast<float>(segments);
            for (int u_index = 0; u_index <= segments; ++u_index) {
                const auto u = static_cast<float>(u_index) / static_cast<float>(segments);
                const auto point = rounded_box_point(rounded_box_source_point(face, u, v));
                mesh.vertices.push_back(make_vertex(
                    point.position,
                    point.normal,
                    glm::vec2 {u, v},
                    face));
            }
        }

        const auto row = static_cast<std::uint32_t>(segments + 1);
        for (int v_index = 0; v_index < segments; ++v_index) {
            for (int u_index = 0; u_index < segments; ++u_index) {
                const auto a = face_start +
                    static_cast<std::uint32_t>(v_index) * row +
                    static_cast<std::uint32_t>(u_index);
                const auto b = a + 1U;
                const auto d = a + row;
                const auto c = d + 1U;
                mesh.indices.insert(mesh.indices.end(), {a, b, c, a, c, d});
            }
        }
    }

    finish_bounds(mesh);
    return mesh;
}

auto build_stylized_capsule(StylizedPrimitiveLod lod) -> StylizedPrimitiveMesh {
    const auto profile = profile_for(lod);
    const auto slices = profile.radial_segments;
    const auto rings = profile.capsule_hemisphere_rings;
    StylizedPrimitiveMesh mesh;
    mesh.vertices.reserve(static_cast<std::size_t>(slices * (rings * 4 + 2)) * 3U);
    mesh.indices.reserve(static_cast<std::size_t>(slices * (rings * 4 + 2)) * 3U);

    for (int slice = 0; slice < slices; ++slice) {
        for (int band = 0; band < rings; ++band) {
            const auto lower_left = capsule_hemisphere_point(false, band, rings, slice, slices);
            const auto lower_right = capsule_hemisphere_point(false, band, rings, slice + 1, slices);
            const auto upper_left = capsule_hemisphere_point(false, band + 1, rings, slice, slices);
            const auto upper_right = capsule_hemisphere_point(false, band + 1, rings, slice + 1, slices);
            if (band == 0) {
                append_oriented_triangle(mesh, {lower_left, upper_right, upper_left});
            } else {
                append_parametric_band(mesh, lower_left, lower_right, upper_left, upper_right);
            }
        }

        append_parametric_band(
            mesh,
            capsule_cylinder_point(false, slice, slices),
            capsule_cylinder_point(false, slice + 1, slices),
            capsule_cylinder_point(true, slice, slices),
            capsule_cylinder_point(true, slice + 1, slices));

        for (int band = rings; band > 0; --band) {
            const auto lower_left = capsule_hemisphere_point(true, band, rings, slice, slices);
            const auto lower_right = capsule_hemisphere_point(true, band, rings, slice + 1, slices);
            const auto upper_left = capsule_hemisphere_point(true, band - 1, rings, slice, slices);
            const auto upper_right = capsule_hemisphere_point(true, band - 1, rings, slice + 1, slices);
            if (band == 1) {
                append_oriented_triangle(mesh, {lower_left, lower_right, upper_left});
            } else {
                append_parametric_band(mesh, lower_left, lower_right, upper_left, upper_right);
            }
        }
    }

    finish_bounds(mesh);
    return mesh;
}

auto build_stylized_ellipsoid(StylizedPrimitiveLod lod) -> StylizedPrimitiveMesh {
    const auto profile = profile_for(lod);
    const auto slices = profile.radial_segments;
    const auto stacks = profile.ellipsoid_stacks;
    StylizedPrimitiveMesh mesh;
    mesh.vertices.reserve(static_cast<std::size_t>(slices * (stacks - 1) * 2) * 3U);
    mesh.indices.reserve(static_cast<std::size_t>(slices * (stacks - 1) * 2) * 3U);

    for (int stack = 0; stack < stacks; ++stack) {
        for (int slice = 0; slice < slices; ++slice) {
            const auto lower_left = ellipsoid_point(stack, slice, stacks, slices);
            const auto lower_right = ellipsoid_point(stack, slice + 1, stacks, slices);
            const auto upper_left = ellipsoid_point(stack + 1, slice, stacks, slices);
            const auto upper_right = ellipsoid_point(stack + 1, slice + 1, stacks, slices);
            if (stack == 0) {
                append_oriented_triangle(mesh, {lower_left, upper_right, upper_left});
            } else if (stack == stacks - 1) {
                append_oriented_triangle(mesh, {lower_left, lower_right, upper_left});
            } else {
                append_parametric_band(mesh, lower_left, lower_right, upper_left, upper_right);
            }
        }
    }

    finish_bounds(mesh);
    return mesh;
}

auto build_stylized_tapered_cylinder(StylizedPrimitiveLod lod) -> StylizedPrimitiveMesh {
    const auto slices = profile_for(lod).radial_segments;
    StylizedPrimitiveMesh mesh;
    mesh.vertices.reserve(static_cast<std::size_t>(slices) * 12U);
    mesh.indices.reserve(static_cast<std::size_t>(slices) * 12U);

    const SurfacePoint bottom_center {
        glm::vec3 {0.0F, -0.5F, 0.0F},
        glm::vec3 {0.0F, -1.0F, 0.0F},
    };
    const SurfacePoint top_center {
        glm::vec3 {0.0F, 0.5F, 0.0F},
        glm::vec3 {0.0F, 1.0F, 0.0F},
    };

    for (int slice = 0; slice < slices; ++slice) {
        const auto bottom_left = tapered_cylinder_point(false, slice, slices);
        const auto bottom_right = tapered_cylinder_point(false, slice + 1, slices);
        const auto top_left = tapered_cylinder_point(true, slice, slices);
        const auto top_right = tapered_cylinder_point(true, slice + 1, slices);
        append_parametric_band(mesh, bottom_left, bottom_right, top_left, top_right);

        const auto bottom_cap_left = SurfacePoint {bottom_left.position, glm::vec3 {0.0F, -1.0F, 0.0F}};
        const auto bottom_cap_right = SurfacePoint {bottom_right.position, glm::vec3 {0.0F, -1.0F, 0.0F}};
        const std::array<glm::vec2, 3> bottom_uvs {{
            glm::vec2 {0.5F, 0.5F},
            glm::vec2 {0.5F - bottom_left.position.z, bottom_left.position.x + 0.5F},
            glm::vec2 {0.5F - bottom_right.position.z, bottom_right.position.x + 0.5F},
        }};
        append_oriented_triangle(
            mesh,
            {bottom_center, bottom_cap_left, bottom_cap_right},
            3,
            &bottom_uvs);

        const auto top_cap_left = SurfacePoint {top_left.position, glm::vec3 {0.0F, 1.0F, 0.0F}};
        const auto top_cap_right = SurfacePoint {top_right.position, glm::vec3 {0.0F, 1.0F, 0.0F}};
        const std::array<glm::vec2, 3> top_uvs {{
            glm::vec2 {0.5F, 0.5F},
            glm::vec2 {top_right.position.z + 0.5F, top_right.position.x + 0.5F},
            glm::vec2 {top_left.position.z + 0.5F, top_left.position.x + 0.5F},
        }};
        append_oriented_triangle(
            mesh,
            {top_center, top_cap_right, top_cap_left},
            2,
            &top_uvs);
    }

    finish_bounds(mesh);
    return mesh;
}

auto build_stylized_panel(StylizedPrimitiveLod lod) -> StylizedPrimitiveMesh {
    const auto segments = profile_for(lod).panel_segments;
    StylizedPrimitiveMesh mesh;
    mesh.vertices.reserve(static_cast<std::size_t>(segments * segments) * 12U);
    mesh.indices.reserve(static_cast<std::size_t>(segments * segments) * 12U);

    for (int v_index = 0; v_index < segments; ++v_index) {
        const auto v0 = static_cast<float>(v_index) / static_cast<float>(segments);
        const auto v1 = static_cast<float>(v_index + 1) / static_cast<float>(segments);
        for (int u_index = 0; u_index < segments; ++u_index) {
            const auto u0 = static_cast<float>(u_index) / static_cast<float>(segments);
            const auto u1 = static_cast<float>(u_index + 1) / static_cast<float>(segments);
            const auto a = panel_point(u0, v0);
            const auto b = panel_point(u1, v0);
            const auto c = panel_point(u1, v1);
            const auto d = panel_point(u0, v1);

            const std::array<glm::vec2, 3> front_abc {{
                glm::vec2 {u0, v0}, glm::vec2 {u1, v0}, glm::vec2 {u1, v1},
            }};
            const std::array<glm::vec2, 3> front_acd {{
                glm::vec2 {u0, v0}, glm::vec2 {u1, v1}, glm::vec2 {u0, v1},
            }};
            append_oriented_triangle(mesh, {a, b, c}, 4, &front_abc);
            append_oriented_triangle(mesh, {a, c, d}, 4, &front_acd);

            const auto back_a = SurfacePoint {a.position, -a.normal};
            const auto back_b = SurfacePoint {b.position, -b.normal};
            const auto back_c = SurfacePoint {c.position, -c.normal};
            const auto back_d = SurfacePoint {d.position, -d.normal};
            const std::array<glm::vec2, 3> back_abc {{
                glm::vec2 {1.0F - u0, v0}, glm::vec2 {1.0F - u1, v0}, glm::vec2 {1.0F - u1, v1},
            }};
            const std::array<glm::vec2, 3> back_acd {{
                glm::vec2 {1.0F - u0, v0}, glm::vec2 {1.0F - u1, v1}, glm::vec2 {1.0F - u0, v1},
            }};
            append_oriented_triangle(mesh, {back_a, back_b, back_c}, 5, &back_abc);
            append_oriented_triangle(mesh, {back_a, back_c, back_d}, 5, &back_acd);
        }
    }

    finish_bounds(mesh);
    return mesh;
}

auto build_stylized_ribbon(StylizedPrimitiveLod lod) -> StylizedPrimitiveMesh {
    const auto segments = profile_for(lod).ribbon_segments;
    StylizedPrimitiveMesh mesh;
    mesh.vertices.reserve(static_cast<std::size_t>(segments) * 12U);
    mesh.indices.reserve(static_cast<std::size_t>(segments) * 12U);

    for (int step = 0; step < segments; ++step) {
        const auto u0 = static_cast<float>(step) / static_cast<float>(segments);
        const auto u1 = static_cast<float>(step + 1) / static_cast<float>(segments);
        const auto a = ribbon_point(step, segments, false);
        const auto b = ribbon_point(step + 1, segments, false);
        const auto c = ribbon_point(step + 1, segments, true);
        const auto d = ribbon_point(step, segments, true);

        const std::array<glm::vec2, 3> front_abc {{
            glm::vec2 {u0, 0.0F}, glm::vec2 {u1, 0.0F}, glm::vec2 {u1, 1.0F},
        }};
        const std::array<glm::vec2, 3> front_acd {{
            glm::vec2 {u0, 0.0F}, glm::vec2 {u1, 1.0F}, glm::vec2 {u0, 1.0F},
        }};
        append_oriented_triangle(mesh, {a, b, c}, 4, &front_abc);
        append_oriented_triangle(mesh, {a, c, d}, 4, &front_acd);

        const auto back_a = SurfacePoint {a.position, -a.normal};
        const auto back_b = SurfacePoint {b.position, -b.normal};
        const auto back_c = SurfacePoint {c.position, -c.normal};
        const auto back_d = SurfacePoint {d.position, -d.normal};
        const std::array<glm::vec2, 3> back_abc {{
            glm::vec2 {1.0F - u0, 0.0F}, glm::vec2 {1.0F - u1, 0.0F}, glm::vec2 {1.0F - u1, 1.0F},
        }};
        const std::array<glm::vec2, 3> back_acd {{
            glm::vec2 {1.0F - u0, 0.0F}, glm::vec2 {1.0F - u1, 1.0F}, glm::vec2 {1.0F - u0, 1.0F},
        }};
        append_oriented_triangle(mesh, {back_a, back_b, back_c}, 5, &back_abc);
        append_oriented_triangle(mesh, {back_a, back_c, back_d}, 5, &back_acd);
    }

    finish_bounds(mesh);
    return mesh;
}

auto build_stylized_primitive(
    StylizedPrimitiveType type,
    StylizedPrimitiveLod lod) -> StylizedPrimitiveMesh {
    switch (type) {
    case StylizedPrimitiveType::RoundedBox:
        return build_stylized_rounded_box(lod);
    case StylizedPrimitiveType::Capsule:
        return build_stylized_capsule(lod);
    case StylizedPrimitiveType::Ellipsoid:
        return build_stylized_ellipsoid(lod);
    case StylizedPrimitiveType::TaperedCylinder:
        return build_stylized_tapered_cylinder(lod);
    case StylizedPrimitiveType::Panel:
        return build_stylized_panel(lod);
    case StylizedPrimitiveType::Ribbon:
        return build_stylized_ribbon(lod);
    }
    return build_stylized_rounded_box(lod);
}

} // namespace valcraft
