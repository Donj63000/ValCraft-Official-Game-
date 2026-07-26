#pragma once

#include "world/Block.h"

#include <glm/geometric.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <array>
#include <algorithm>
#include <cmath>

namespace valcraft {

struct FrustumPlane {
    glm::vec3 normal {0.0F, 1.0F, 0.0F};
    float distance = 0.0F;
};

struct ChunkBounds {
    glm::vec3 min_corner {0.0F};
    glm::vec3 max_corner {0.0F};
    glm::vec3 center {0.0F};
};

class ExactAabbAccumulator {
public:
    void add(float x, float y, float z) noexcept {
        const glm::vec3 point {x, y, z};
        if (empty_) {
            // Je prends le premier point comme AABB initiale afin de ne pas
            // introduire de sentinelle infinie ni de marge artificielle.
            min_corner_ = point;
            max_corner_ = point;
            empty_ = false;
            return;
        }

        min_corner_.x = std::min(min_corner_.x, point.x);
        min_corner_.y = std::min(min_corner_.y, point.y);
        min_corner_.z = std::min(min_corner_.z, point.z);
        max_corner_.x = std::max(max_corner_.x, point.x);
        max_corner_.y = std::max(max_corner_.y, point.y);
        max_corner_.z = std::max(max_corner_.z, point.z);
    }

    [[nodiscard]] auto empty() const noexcept -> bool {
        return empty_;
    }

    [[nodiscard]] auto bounds_or(const ChunkBounds& fallback) const noexcept
        -> ChunkBounds {
        if (empty_) {
            return fallback;
        }

        // Je divise chaque borne avant l'addition pour garder un centre fini
        // meme lorsque les coordonnees valides approchent la limite d'un float.
        const auto center =
            min_corner_ * 0.5F + max_corner_ * 0.5F;
        return {min_corner_, max_corner_, center};
    }

private:
    glm::vec3 min_corner_ {0.0F};
    glm::vec3 max_corner_ {0.0F};
    bool empty_ = true;
};

struct ShadowPassContext {
    std::array<FrustumPlane, 6> frustum {};
    glm::vec3 focus {0.0F};
    float max_distance_sq = 0.0F;
    bool enabled = false;
};

struct ChunkPassVisibility {
    bool camera = false;
    bool shadow = false;
    float distance_squared = 0.0F;
};

inline auto make_frustum_plane(const glm::vec4& equation) -> FrustumPlane {
    const auto normal = glm::vec3 {equation.x, equation.y, equation.z};
    const auto length = glm::length(normal);
    if (length <= 1.0e-6F) {
        return {};
    }
    return {normal / length, equation.w / length};
}

inline auto extract_frustum_planes(const glm::mat4& matrix) -> std::array<FrustumPlane, 6> {
    const glm::vec4 row0 {matrix[0][0], matrix[1][0], matrix[2][0], matrix[3][0]};
    const glm::vec4 row1 {matrix[0][1], matrix[1][1], matrix[2][1], matrix[3][1]};
    const glm::vec4 row2 {matrix[0][2], matrix[1][2], matrix[2][2], matrix[3][2]};
    const glm::vec4 row3 {matrix[0][3], matrix[1][3], matrix[2][3], matrix[3][3]};

    return {{
        make_frustum_plane(row3 + row0),
        make_frustum_plane(row3 - row0),
        make_frustum_plane(row3 + row1),
        make_frustum_plane(row3 - row1),
        make_frustum_plane(row3 + row2),
        make_frustum_plane(row3 - row2),
    }};
}

inline auto intersects_frustum(const std::array<FrustumPlane, 6>& planes,
                               const glm::vec3& min_corner,
                               const glm::vec3& max_corner) -> bool {
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

inline auto make_chunk_bounds(const ChunkCoord& coord) -> ChunkBounds {
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
    return {
        min_corner,
        max_corner,
        (min_corner + max_corner) * 0.5F,
    };
}

inline auto chunk_horizontal_distance_sq(const glm::vec3& center, const glm::vec3& reference_position) -> float {
    const auto horizontal = glm::vec3 {
        center.x - reference_position.x,
        0.0F,
        center.z - reference_position.z,
    };
    return glm::dot(horizontal, horizontal);
}

inline auto bounds_horizontal_distance_sq(const ChunkBounds& bounds, const glm::vec3& reference_position) -> float {
    const auto closest_x = std::clamp(reference_position.x, bounds.min_corner.x, bounds.max_corner.x);
    const auto closest_z = std::clamp(reference_position.z, bounds.min_corner.z, bounds.max_corner.z);
    const glm::vec3 horizontal {
        closest_x - reference_position.x,
        0.0F,
        closest_z - reference_position.z,
    };
    return glm::dot(horizontal, horizontal);
}

inline auto should_render_chunk_in_camera_pass(const ChunkBounds& bounds,
                                               const std::array<FrustumPlane, 6>& camera_frustum,
                                               const glm::vec3& eye,
                                               const glm::vec3& forward,
                                               float draw_distance_sq,
                                               float back_cull_start_distance_sq,
                                               float back_cull_dot_threshold = -0.45F) -> bool {
    if (!intersects_frustum(camera_frustum, bounds.min_corner, bounds.max_corner)) {
        return false;
    }

    const auto distance_sq = chunk_horizontal_distance_sq(bounds.center, eye);
    if (distance_sq > draw_distance_sq) {
        return false;
    }

    if (distance_sq > back_cull_start_distance_sq) {
        const auto inverse_length = 1.0F / std::sqrt(distance_sq);
        const auto chunk_dir = glm::vec3 {
            (bounds.center.x - eye.x) * inverse_length,
            0.0F,
            (bounds.center.z - eye.z) * inverse_length,
        };
        if (glm::dot(forward, chunk_dir) < back_cull_dot_threshold) {
            return false;
        }
    }

    return true;
}

inline auto should_render_chunk_in_shadow_pass(const ChunkBounds& bounds,
                                               const std::array<FrustumPlane, 6>& light_frustum,
                                               const glm::vec3& focus,
                                               float max_shadow_distance_sq) -> bool {
    if (!intersects_frustum(light_frustum, bounds.min_corner, bounds.max_corner)) {
        return false;
    }

    return chunk_horizontal_distance_sq(bounds.center, focus) <= max_shadow_distance_sq;
}

inline auto classify_chunk_visibility(const ChunkBounds& bounds,
                                      const std::array<FrustumPlane, 6>& camera_frustum,
                                      const glm::vec3& eye,
                                      const glm::vec3& forward,
                                      float draw_distance_sq,
                                      float back_cull_start_distance_sq,
                                      const ShadowPassContext& shadow_context,
                                      bool shadow_candidate,
                                      float back_cull_dot_threshold = -0.45F) -> ChunkPassVisibility {
    ChunkPassVisibility visibility {};
    visibility.distance_squared = chunk_horizontal_distance_sq(bounds.center, eye);
    visibility.camera = should_render_chunk_in_camera_pass(
        bounds,
        camera_frustum,
        eye,
        forward,
        draw_distance_sq,
        back_cull_start_distance_sq,
        back_cull_dot_threshold);

    visibility.shadow =
        shadow_context.enabled &&
        shadow_candidate &&
        should_render_chunk_in_shadow_pass(
            bounds,
            shadow_context.frustum,
            shadow_context.focus,
            shadow_context.max_distance_sq);
    return visibility;
}

inline auto classify_large_bounds_visibility(const ChunkBounds& bounds,
                                             const std::array<FrustumPlane, 6>& camera_frustum,
                                             const glm::vec3& eye,
                                             float draw_distance_sq,
                                             const ShadowPassContext& shadow_context,
                                             bool shadow_candidate) -> ChunkPassVisibility {
    ChunkPassVisibility visibility {};
    // Je mesure un grand objet depuis le point de son AABB le plus proche : son
    // centre peut etre derriere la camera alors que sa poupe reste juste devant moi.
    visibility.distance_squared = bounds_horizontal_distance_sq(bounds, eye);
    visibility.camera = visibility.distance_squared <= draw_distance_sq &&
                        intersects_frustum(camera_frustum, bounds.min_corner, bounds.max_corner);
    visibility.shadow = shadow_context.enabled &&
                        shadow_candidate &&
                        bounds_horizontal_distance_sq(bounds, shadow_context.focus) <= shadow_context.max_distance_sq &&
                        intersects_frustum(shadow_context.frustum, bounds.min_corner, bounds.max_corner);
    return visibility;
}

} // namespace valcraft
