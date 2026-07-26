#include "render/ShadowCascades.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/vec4.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace valcraft {
namespace {

constexpr float kDefaultVerticalFovRadians = 1.30899694F;
constexpr float kDefaultAspectRatio = 16.0F / 9.0F;
constexpr float kDefaultNearDistance = 0.1F;
constexpr float kDefaultFarDistance = 320.0F;
constexpr float kMinimumNearDistance = 0.01F;
constexpr float kMaximumFarDistance = 100'000.0F;
constexpr float kMinimumVerticalFovRadians = 0.0174532925F;
constexpr float kMaximumVerticalFovRadians = 3.05432619F;
constexpr float kMaximumWorldCoordinate = 10'000'000.0F;
constexpr int kMinimumShadowMapResolution = 64;
constexpr int kMaximumShadowMapResolution = 16'384;
constexpr float kDirectionEpsilonSquared = 1.0e-10F;

struct SanitizedShadowParameters {
    std::size_t cascade_count = 2U;
    glm::vec3 camera_position {0.0F};
    glm::vec3 camera_forward {0.0F, 0.0F, -1.0F};
    glm::vec3 camera_right {1.0F, 0.0F, 0.0F};
    glm::vec3 camera_up {0.0F, 1.0F, 0.0F};
    glm::vec3 sun_direction {0.0F, 1.0F, 0.0F};
    float vertical_fov_radians = kDefaultVerticalFovRadians;
    float aspect_ratio = kDefaultAspectRatio;
    float near_distance = kDefaultNearDistance;
    float far_distance = kDefaultFarDistance;
    float split_lambda = 0.65F;
    float caster_depth_padding = 24.0F;
    int shadow_map_resolution = 2048;
    bool changed = false;
};

[[nodiscard]] auto finite_scalar(float value) noexcept -> bool {
    return std::isfinite(value);
}

[[nodiscard]] auto finite_vector(const glm::vec3& value) noexcept -> bool {
    return finite_scalar(value.x) &&
           finite_scalar(value.y) &&
           finite_scalar(value.z);
}

[[nodiscard]] auto sanitize_scalar(
    float value,
    float fallback,
    float minimum,
    float maximum,
    bool& changed) noexcept -> float {
    if (!finite_scalar(value)) {
        changed = true;
        return fallback;
    }
    const auto sanitized = std::clamp(value, minimum, maximum);
    changed = changed || sanitized != value;
    return sanitized;
}

[[nodiscard]] auto sanitize_position(
    const glm::vec3& position,
    bool& changed) noexcept -> glm::vec3 {
    auto sanitized = position;
    for (int component = 0; component < 3; ++component) {
        if (!finite_scalar(sanitized[component])) {
            sanitized[component] = 0.0F;
            changed = true;
            continue;
        }
        const auto clamped = std::clamp(
            sanitized[component],
            -kMaximumWorldCoordinate,
            kMaximumWorldCoordinate);
        changed = changed || clamped != sanitized[component];
        sanitized[component] = clamped;
    }
    return sanitized;
}

[[nodiscard]] auto safe_normalize(
    const glm::vec3& value,
    const glm::vec3& fallback,
    bool& changed) noexcept -> glm::vec3 {
    if (!finite_vector(value)) {
        changed = true;
        return fallback;
    }
    const auto length_squared = glm::dot(value, value);
    if (!finite_scalar(length_squared) ||
        length_squared <= kDirectionEpsilonSquared) {
        changed = true;
        return fallback;
    }
    return value / std::sqrt(length_squared);
}

[[nodiscard]] auto fallback_up_for(
    const glm::vec3& direction) noexcept -> glm::vec3 {
    if (std::abs(direction.y) < 0.9F) {
        return {0.0F, 1.0F, 0.0F};
    }
    if (std::abs(direction.z) < 0.9F) {
        return {0.0F, 0.0F, 1.0F};
    }
    return {1.0F, 0.0F, 0.0F};
}

[[nodiscard]] auto sanitize_parameters(
    const ShadowCascadeBuildParameters& source) noexcept
    -> SanitizedShadowParameters {
    SanitizedShadowParameters result {};

    const auto quality_value = static_cast<std::uint8_t>(source.quality);
    const auto quality_valid =
        quality_value <= static_cast<std::uint8_t>(RendererQuality::Dynamic);
    if (!quality_valid) {
        result.changed = true;
    }
    const auto safe_quality =
        quality_valid ? source.quality : RendererQuality::High;

    if (source.cascade_count == 0) {
        result.cascade_count =
            shadow_cascade_count_for_quality(safe_quality);
    } else {
        const auto clamped_count = std::clamp(
            source.cascade_count,
            1,
            static_cast<int>(kMaximumShadowCascadeCount));
        result.changed =
            result.changed || clamped_count != source.cascade_count;
        result.cascade_count =
            static_cast<std::size_t>(clamped_count);
    }

    result.camera_position =
        sanitize_position(source.camera_position, result.changed);
    result.camera_forward = safe_normalize(
        source.camera_forward,
        glm::vec3 {0.0F, 0.0F, -1.0F},
        result.changed);

    auto source_up = safe_normalize(
        source.camera_up,
        fallback_up_for(result.camera_forward),
        result.changed);
    auto camera_right =
        glm::cross(result.camera_forward, source_up);
    if (!finite_vector(camera_right) ||
        glm::dot(camera_right, camera_right) <=
            kDirectionEpsilonSquared) {
        source_up = fallback_up_for(result.camera_forward);
        camera_right =
            glm::cross(result.camera_forward, source_up);
        result.changed = true;
    }
    result.camera_right = safe_normalize(
        camera_right,
        glm::vec3 {1.0F, 0.0F, 0.0F},
        result.changed);
    result.camera_up = safe_normalize(
        glm::cross(result.camera_right, result.camera_forward),
        fallback_up_for(result.camera_forward),
        result.changed);

    result.sun_direction = safe_normalize(
        source.sun_direction,
        glm::normalize(glm::vec3 {0.35F, 0.85F, 0.25F}),
        result.changed);
    result.vertical_fov_radians = sanitize_scalar(
        source.vertical_fov_radians,
        kDefaultVerticalFovRadians,
        kMinimumVerticalFovRadians,
        kMaximumVerticalFovRadians,
        result.changed);
    result.aspect_ratio = sanitize_scalar(
        source.aspect_ratio,
        kDefaultAspectRatio,
        0.1F,
        10.0F,
        result.changed);
    result.near_distance = sanitize_scalar(
        source.near_distance,
        kDefaultNearDistance,
        kMinimumNearDistance,
        kMaximumFarDistance - 1.0F,
        result.changed);
    result.far_distance = sanitize_scalar(
        source.far_distance,
        kDefaultFarDistance,
        kMinimumNearDistance,
        kMaximumFarDistance,
        result.changed);
    if (result.far_distance <= result.near_distance + 0.01F) {
        result.far_distance = std::min(
            std::max(
                kDefaultFarDistance,
                result.near_distance + 1.0F),
            kMaximumFarDistance);
        result.changed = true;
    }
    result.split_lambda = sanitize_scalar(
        source.split_lambda,
        0.65F,
        0.0F,
        1.0F,
        result.changed);
    result.caster_depth_padding = sanitize_scalar(
        source.caster_depth_padding,
        24.0F,
        0.0F,
        10'000.0F,
        result.changed);

    result.shadow_map_resolution = std::clamp(
        source.shadow_map_resolution,
        kMinimumShadowMapResolution,
        kMaximumShadowMapResolution);
    result.changed =
        result.changed ||
        result.shadow_map_resolution != source.shadow_map_resolution;
    return result;
}

[[nodiscard]] auto make_frustum_corners(
    const SanitizedShadowParameters& parameters,
    float near_distance,
    float far_distance) noexcept
    -> std::array<glm::vec3, 8> {
    const auto tangent =
        std::tan(parameters.vertical_fov_radians * 0.5F);
    const auto near_half_height = tangent * near_distance;
    const auto near_half_width =
        near_half_height * parameters.aspect_ratio;
    const auto far_half_height = tangent * far_distance;
    const auto far_half_width =
        far_half_height * parameters.aspect_ratio;
    const auto near_center =
        parameters.camera_position +
        parameters.camera_forward * near_distance;
    const auto far_center =
        parameters.camera_position +
        parameters.camera_forward * far_distance;

    const auto corner = [&](const glm::vec3& center,
                            float half_width,
                            float half_height,
                            float horizontal_sign,
                            float vertical_sign) {
        return center +
               parameters.camera_right *
                   (half_width * horizontal_sign) +
               parameters.camera_up *
                   (half_height * vertical_sign);
    };

    return {{
        corner(near_center, near_half_width, near_half_height, -1.0F, -1.0F),
        corner(near_center, near_half_width, near_half_height, 1.0F, -1.0F),
        corner(near_center, near_half_width, near_half_height, 1.0F, 1.0F),
        corner(near_center, near_half_width, near_half_height, -1.0F, 1.0F),
        corner(far_center, far_half_width, far_half_height, -1.0F, -1.0F),
        corner(far_center, far_half_width, far_half_height, 1.0F, -1.0F),
        corner(far_center, far_half_width, far_half_height, 1.0F, 1.0F),
        corner(far_center, far_half_width, far_half_height, -1.0F, 1.0F),
    }};
}

[[nodiscard]] auto snap_to_step(
    float value,
    float step) noexcept -> float {
    if (!finite_scalar(value) ||
        !finite_scalar(step) ||
        step <= std::numeric_limits<float>::epsilon()) {
        return finite_scalar(value) ? value : 0.0F;
    }
    const auto quotient =
        static_cast<double>(value) / static_cast<double>(step);
    const auto snapped =
        std::round(quotient) * static_cast<double>(step);
    return static_cast<float>(snapped);
}

[[nodiscard]] auto matrix_is_finite(
    const glm::mat4& matrix) noexcept -> bool {
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            if (!finite_scalar(matrix[column][row])) {
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] auto make_cascade(
    const SanitizedShadowParameters& parameters,
    const glm::mat4& light_view,
    float near_distance,
    float far_distance) noexcept -> ShadowCascade {
    ShadowCascade cascade {};
    cascade.near_distance = near_distance;
    cascade.far_distance = far_distance;
    cascade.light_view = light_view;
    cascade.bounds.world_frustum_corners =
        make_frustum_corners(
            parameters,
            near_distance,
            far_distance);

    auto world_min = glm::vec3 {
        std::numeric_limits<float>::max()};
    auto world_max = glm::vec3 {
        std::numeric_limits<float>::lowest()};
    auto world_center = glm::vec3 {0.0F};
    for (const auto& corner :
         cascade.bounds.world_frustum_corners) {
        world_min = glm::min(world_min, corner);
        world_max = glm::max(world_max, corner);
        world_center += corner;
    }
    world_center /= static_cast<float>(
        cascade.bounds.world_frustum_corners.size());

    auto raw_radius = 0.0F;
    for (const auto& corner :
         cascade.bounds.world_frustum_corners) {
        raw_radius = std::max(
            raw_radius,
            glm::length(corner - world_center));
    }

    // Je réserve une petite couronne avant l'arrondi. Elle absorbe le demi
    // texel de déplacement sans jamais rogner un coin du frustum, même avec la
    // résolution minimale acceptée.
    const auto stabilization_guard =
        std::max(0.25F, raw_radius * 0.025F);
    constexpr float radius_quantization = 16.0F;
    const auto stabilized_radius =
        std::ceil(
            (raw_radius + stabilization_guard) *
            radius_quantization) /
        radius_quantization;
    const auto world_units_per_texel =
        (stabilized_radius * 2.0F) /
        static_cast<float>(parameters.shadow_map_resolution);

    const auto unsnapped_center4 =
        light_view * glm::vec4 {world_center, 1.0F};
    const auto unsnapped_center =
        glm::vec3 {unsnapped_center4};
    const auto stabilized_center = glm::vec3 {
        snap_to_step(
            unsnapped_center.x,
            world_units_per_texel),
        snap_to_step(
            unsnapped_center.y,
            world_units_per_texel),
        snap_to_step(
            unsnapped_center.z,
            world_units_per_texel),
    };
    const auto depth_radius =
        stabilized_radius + parameters.caster_depth_padding;
    const auto light_min = glm::vec3 {
        stabilized_center.x - stabilized_radius,
        stabilized_center.y - stabilized_radius,
        stabilized_center.z - depth_radius,
    };
    const auto light_max = glm::vec3 {
        stabilized_center.x + stabilized_radius,
        stabilized_center.y + stabilized_radius,
        stabilized_center.z + depth_radius,
    };

    cascade.light_projection = glm::ortho(
        light_min.x,
        light_max.x,
        light_min.y,
        light_max.y,
        -light_max.z,
        -light_min.z);
    cascade.light_view_projection =
        cascade.light_projection * light_view;
    cascade.frustum =
        extract_frustum_planes(cascade.light_view_projection);
    cascade.world_units_per_texel = world_units_per_texel;
    cascade.bounds.world_min = world_min;
    cascade.bounds.world_max = world_max;
    cascade.bounds.world_center = world_center;
    cascade.bounds.light_space_min = light_min;
    cascade.bounds.light_space_max = light_max;
    cascade.bounds.unsnapped_light_space_center =
        unsnapped_center;
    cascade.bounds.stabilized_light_space_center =
        stabilized_center;
    cascade.bounds.bounding_radius = stabilized_radius;
    return cascade;
}

} // namespace

auto shadow_cascade_transition_width(
    const ShadowCascadeSet& cascades) noexcept -> float {
    if (cascades.cascade_count < 2U) {
        return 0.0F;
    }

    const auto near_distance = cascades.split_distances[0];
    const auto split_distance = cascades.split_distances[1];
    const auto far_distance = cascades.split_distances[2];
    if (!finite_scalar(near_distance) ||
        !finite_scalar(split_distance) ||
        !finite_scalar(far_distance) ||
        split_distance <= near_distance ||
        far_distance <= split_distance) {
        return 0.0F;
    }

    const auto preferred_width = std::clamp(
        split_distance * 0.08F,
        2.0F,
        12.0F);
    const auto available_width =
        std::min(
            split_distance - near_distance,
            far_distance - split_distance) *
        0.5F;
    return std::clamp(
        preferred_width,
        0.0F,
        std::max(available_width, 0.0F));
}

auto shadow_cascade_blend_factor(
    const ShadowCascadeSet& cascades,
    float positive_view_distance) noexcept -> float {
    if (!finite_scalar(positive_view_distance) ||
        positive_view_distance < 0.0F ||
        cascades.cascade_count < 2U) {
        return 0.0F;
    }

    const auto width = cascades.transition_width;
    const auto split_distance = cascades.split_distances[1];
    if (!finite_scalar(width) ||
        !finite_scalar(split_distance) ||
        width <= std::numeric_limits<float>::epsilon()) {
        return positive_view_distance > split_distance
                   ? 1.0F
                   : 0.0F;
    }

    const auto blend_start = split_distance - width * 0.5F;
    const auto normalized = std::clamp(
        (positive_view_distance - blend_start) / width,
        0.0F,
        1.0F);
    return normalized * normalized * (3.0F - 2.0F * normalized);
}

auto build_shadow_cascade_set(
    const ShadowCascadeBuildParameters& parameters) noexcept
    -> ShadowCascadeSet {
    const auto sanitized = sanitize_parameters(parameters);
    ShadowCascadeSet result {};
    result.cascade_count = sanitized.cascade_count;
    result.shadow_map_resolution =
        sanitized.shadow_map_resolution;
    result.input_was_sanitized = sanitized.changed;

    const auto light_up_hint =
        fallback_up_for(sanitized.sun_direction);
    // Je garde cette vue ancrée à l'origine : seule la projection est déplacée
    // par pas de texel. Une translation sub-texel de la caméra ne peut donc pas
    // réintroduire de translation fractionnaire dans la matrice finale.
    const auto light_view = glm::lookAt(
        sanitized.sun_direction * 1'000.0F,
        glm::vec3 {0.0F},
        light_up_hint);
    result.light_right_world = {
        light_view[0][0],
        light_view[1][0],
        light_view[2][0],
    };
    result.light_up_world = {
        light_view[0][1],
        light_view[1][1],
        light_view[2][1],
    };
    result.light_forward_world = {
        -light_view[0][2],
        -light_view[1][2],
        -light_view[2][2],
    };

    result.split_distances[0] =
        sanitized.near_distance;
    auto previous_split = sanitized.near_distance;
    for (std::size_t cascade_index = 0;
         cascade_index < sanitized.cascade_count;
         ++cascade_index) {
        const auto split_index = cascade_index + 1U;
        const auto ratio =
            static_cast<double>(split_index) /
            static_cast<double>(sanitized.cascade_count);
        const auto logarithmic =
            static_cast<double>(sanitized.near_distance) *
            std::pow(
                static_cast<double>(sanitized.far_distance) /
                    static_cast<double>(sanitized.near_distance),
                ratio);
        const auto uniform =
            static_cast<double>(sanitized.near_distance) +
            (static_cast<double>(sanitized.far_distance) -
             static_cast<double>(sanitized.near_distance)) *
                ratio;
        auto split = static_cast<float>(
            static_cast<double>(sanitized.split_lambda) *
                logarithmic +
            (1.0 - static_cast<double>(
                       sanitized.split_lambda)) *
                uniform);
        if (split_index == sanitized.cascade_count) {
            split = sanitized.far_distance;
        }
        split = std::clamp(
            split,
            previous_split + 0.001F,
            sanitized.far_distance);
        result.split_distances[split_index] = split;
        previous_split = split;
    }

    // Je prolonge les cases inutilisées avec la dernière distance valide pour
    // que leur lecture accidentelle reste finie et déterministe.
    for (std::size_t split_index =
             sanitized.cascade_count + 1U;
         split_index < result.split_distances.size();
         ++split_index) {
        result.split_distances[split_index] =
            sanitized.far_distance;
    }

    result.transition_width =
        shadow_cascade_transition_width(result);
    const auto transition_half_width =
        result.transition_width * 0.5F;
    for (std::size_t cascade_index = 0;
         cascade_index < sanitized.cascade_count;
         ++cascade_index) {
        const auto logical_near =
            result.split_distances[cascade_index];
        const auto logical_far =
            result.split_distances[cascade_index + 1U];
        const auto coverage_near =
            cascade_index == 0U
                ? logical_near
                : std::max(
                      sanitized.near_distance,
                      logical_near - transition_half_width);
        const auto coverage_far =
            cascade_index + 1U == sanitized.cascade_count
                ? logical_far
                : std::min(
                      sanitized.far_distance,
                      logical_far + transition_half_width);
        result.cascades[cascade_index] = make_cascade(
            sanitized,
            light_view,
            coverage_near,
            coverage_far);
    }
    return result;
}

auto select_shadow_cascade(
    const ShadowCascadeSet& cascades,
    float positive_view_distance) noexcept
    -> std::optional<std::size_t> {
    if (!finite_scalar(positive_view_distance) ||
        positive_view_distance < 0.0F ||
        cascades.cascade_count == 0U) {
        return std::nullopt;
    }
    const auto safe_count = std::min(
        cascades.cascade_count,
        kMaximumShadowCascadeCount);
    for (std::size_t cascade_index = 0;
         cascade_index < safe_count;
         ++cascade_index) {
        const auto cascade_far_distance =
            cascades.split_distances[cascade_index + 1U];
        if (!finite_scalar(cascade_far_distance)) {
            return std::nullopt;
        }
        if (positive_view_distance <=
            cascade_far_distance) {
            return cascade_index;
        }
    }
    return std::nullopt;
}

auto shadow_cascade_is_finite(
    const ShadowCascade& cascade) noexcept -> bool {
    if (!finite_scalar(cascade.near_distance) ||
        !finite_scalar(cascade.far_distance) ||
        !finite_scalar(cascade.world_units_per_texel) ||
        cascade.near_distance <= 0.0F ||
        cascade.far_distance <= cascade.near_distance ||
        cascade.world_units_per_texel <= 0.0F ||
        !matrix_is_finite(cascade.light_view) ||
        !matrix_is_finite(cascade.light_projection) ||
        !matrix_is_finite(cascade.light_view_projection) ||
        !finite_vector(cascade.bounds.world_min) ||
        !finite_vector(cascade.bounds.world_max) ||
        !finite_vector(cascade.bounds.world_center) ||
        !finite_vector(cascade.bounds.light_space_min) ||
        !finite_vector(cascade.bounds.light_space_max) ||
        !finite_vector(
            cascade.bounds.unsnapped_light_space_center) ||
        !finite_vector(
            cascade.bounds.stabilized_light_space_center) ||
        !finite_scalar(cascade.bounds.bounding_radius) ||
        cascade.bounds.bounding_radius <= 0.0F) {
        return false;
    }
    for (const auto& corner :
         cascade.bounds.world_frustum_corners) {
        if (!finite_vector(corner)) {
            return false;
        }
    }
    for (const auto& plane : cascade.frustum) {
        if (!finite_vector(plane.normal) ||
            !finite_scalar(plane.distance)) {
            return false;
        }
    }
    return true;
}

} // namespace valcraft
