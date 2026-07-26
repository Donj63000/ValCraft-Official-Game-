#include "render/ShadowCascades.h"
#include "render/ShadowCulling.h"

#include <doctest/doctest.h>

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/vec4.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <limits>

namespace valcraft {
namespace {

[[nodiscard]] auto matrix_matches_exactly(
    const glm::mat4& left,
    const glm::mat4& right) -> bool {
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            if (left[column][row] != right[column][row]) {
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] auto point_in_clip_volume(
    const glm::mat4& view_projection,
    const glm::vec3& point,
    float tolerance = 0.001F) -> bool {
    const auto clip =
        view_projection * glm::vec4 {point, 1.0F};
    if (!std::isfinite(clip.x) ||
        !std::isfinite(clip.y) ||
        !std::isfinite(clip.z) ||
        !std::isfinite(clip.w) ||
        std::abs(clip.w) <= 1.0e-7F) {
        return false;
    }
    const auto ndc = glm::vec3 {clip} / clip.w;
    return std::abs(ndc.x) <= 1.0F + tolerance &&
           std::abs(ndc.y) <= 1.0F + tolerance &&
           std::abs(ndc.z) <= 1.0F + tolerance;
}

} // namespace

TEST_CASE("exact AABB accumulator keeps mixed coordinate extrema") {
    ExactAabbAccumulator accumulator {};
    accumulator.add(-8.0F, 4.0F, 12.0F);
    accumulator.add(6.0F, -10.0F, -3.0F);
    accumulator.add(-2.0F, 9.0F, 5.0F);

    const ChunkBounds fallback {
        glm::vec3 {-100.0F},
        glm::vec3 {100.0F},
        glm::vec3 {0.0F},
    };
    const auto bounds = accumulator.bounds_or(fallback);

    CHECK_FALSE(accumulator.empty());
    CHECK(bounds.min_corner == glm::vec3 {-8.0F, -10.0F, -3.0F});
    CHECK(bounds.max_corner == glm::vec3 {6.0F, 9.0F, 12.0F});
}

TEST_CASE("exact AABB accumulator computes center from extrema") {
    ExactAabbAccumulator accumulator {};
    accumulator.add(-10.0F, -4.0F, 2.0F);
    accumulator.add(6.0F, 8.0F, 14.0F);
    accumulator.add(1.0F, 0.0F, 7.0F);

    const auto bounds = accumulator.bounds_or(ChunkBounds {});

    CHECK(bounds.center == glm::vec3 {-2.0F, 2.0F, 8.0F});
}

TEST_CASE("exact AABB accumulator maps one point to all bounds") {
    ExactAabbAccumulator accumulator {};
    const glm::vec3 point {-3.5F, 0.25F, 17.0F};
    accumulator.add(point.x, point.y, point.z);

    const auto bounds = accumulator.bounds_or(ChunkBounds {});

    CHECK(bounds.min_corner == point);
    CHECK(bounds.max_corner == point);
    CHECK(bounds.center == point);
}

TEST_CASE("empty exact AABB accumulator returns caller fallback") {
    ExactAabbAccumulator accumulator {};
    const ChunkBounds fallback {
        glm::vec3 {-32.0F, -16.0F, -8.0F},
        glm::vec3 {64.0F, 48.0F, 24.0F},
        glm::vec3 {16.0F, 16.0F, 8.0F},
    };

    const auto bounds = accumulator.bounds_or(fallback);

    CHECK(accumulator.empty());
    CHECK(bounds.min_corner == fallback.min_corner);
    CHECK(bounds.max_corner == fallback.max_corner);
    CHECK(bounds.center == fallback.center);
}

TEST_CASE("shadow cascade quality selects two levels only on high") {
    CHECK(
        shadow_cascade_count_for_quality(
            RendererQuality::High) == 2U);
    CHECK(
        shadow_cascade_count_for_quality(
            RendererQuality::Dynamic) == 2U);
    CHECK(
        shadow_cascade_count_for_quality(
            RendererQuality::Medium) == 1U);
    CHECK(
        shadow_cascade_count_for_quality(
            RendererQuality::Low) == 1U);

    ShadowCascadeBuildParameters parameters {};
    parameters.quality = RendererQuality::High;
    const auto high = build_shadow_cascade_set(parameters);
    CHECK(high.cascade_count == 2U);

    parameters.quality = RendererQuality::Low;
    const auto low = build_shadow_cascade_set(parameters);
    CHECK(low.cascade_count == 1U);

    parameters.cascade_count = 2;
    const auto explicit_count =
        build_shadow_cascade_set(parameters);
    CHECK(explicit_count.cascade_count == 2U);
}

TEST_CASE("shadow cascade splits and matrices are deterministic at negative coordinates") {
    ShadowCascadeBuildParameters parameters {};
    parameters.camera_position = {
        -4'321.25F,
        78.5F,
        -9'876.75F,
    };
    parameters.camera_forward =
        glm::normalize(glm::vec3 {0.31F, -0.19F, -0.93F});
    parameters.sun_direction =
        glm::normalize(glm::vec3 {-0.37F, 0.82F, 0.44F});
    parameters.near_distance = 0.15F;
    parameters.far_distance = 384.0F;
    parameters.shadow_map_resolution = 4096;

    const auto first = build_shadow_cascade_set(parameters);
    const auto second = build_shadow_cascade_set(parameters);

    REQUIRE(first.cascade_count == 2U);
    REQUIRE(second.cascade_count == first.cascade_count);
    CHECK(first.split_distances == second.split_distances);
    CHECK(first.split_distances[0] == parameters.near_distance);
    CHECK(first.split_distances[1] >
          first.split_distances[0]);
    CHECK(first.split_distances[1] <
          first.split_distances[2]);
    CHECK(first.split_distances[2] == parameters.far_distance);
    for (std::size_t index = 0;
         index < first.cascade_count;
         ++index) {
        CHECK(shadow_cascade_is_finite(first.cascades[index]));
        CHECK(
            matrix_matches_exactly(
                first.cascades[index].light_view_projection,
                second.cascades[index].light_view_projection));
        CHECK(
            first.cascades[index].bounds.world_frustum_corners ==
            second.cascades[index].bounds.world_frustum_corners);
    }
}

TEST_CASE("each stabilized cascade covers its complete camera slice") {
    ShadowCascadeBuildParameters parameters {};
    parameters.camera_position = {-517.0F, 83.0F, -1'029.0F};
    parameters.camera_forward =
        glm::normalize(glm::vec3 {0.24F, -0.11F, -0.96F});
    parameters.camera_up = {0.0F, 1.0F, 0.0F};
    parameters.sun_direction =
        glm::normalize(glm::vec3 {0.28F, 0.91F, -0.30F});
    parameters.vertical_fov_radians = 1.3962634F;
    parameters.aspect_ratio = 21.0F / 9.0F;
    parameters.near_distance = 0.1F;
    parameters.far_distance = 320.0F;

    const auto cascades =
        build_shadow_cascade_set(parameters);
    REQUIRE(cascades.cascade_count == 2U);
    for (std::size_t index = 0;
         index < cascades.cascade_count;
         ++index) {
        const auto& cascade = cascades.cascades[index];
        CAPTURE(index);
        CHECK(shadow_cascade_is_finite(cascade));
        for (const auto& corner :
             cascade.bounds.world_frustum_corners) {
            CHECK(
                point_in_clip_volume(
                    cascade.light_view_projection,
                    corner));
        }
        CHECK(cascade.bounds.world_min.x <=
              cascade.bounds.world_max.x);
        CHECK(cascade.bounds.world_min.y <=
              cascade.bounds.world_max.y);
        CHECK(cascade.bounds.world_min.z <=
              cascade.bounds.world_max.z);
        CHECK(cascade.bounds.light_space_min.x <
              cascade.bounds.light_space_max.x);
        CHECK(cascade.bounds.light_space_min.y <
              cascade.bounds.light_space_max.y);
        CHECK(cascade.bounds.light_space_min.z <
              cascade.bounds.light_space_max.z);
    }
}

TEST_CASE("cascade selection includes near split and final far plane") {
    ShadowCascadeBuildParameters parameters {};
    parameters.near_distance = 0.2F;
    parameters.far_distance = 240.0F;
    const auto cascades =
        build_shadow_cascade_set(parameters);
    REQUIRE(cascades.cascade_count == 2U);

    const auto first_split =
        cascades.split_distances[1];
    CHECK(select_shadow_cascade(cascades, 0.0F) ==
          std::optional<std::size_t> {0U});
    CHECK(
        select_shadow_cascade(
            cascades,
            parameters.near_distance) ==
        std::optional<std::size_t> {0U});
    CHECK(
        select_shadow_cascade(cascades, first_split) ==
        std::optional<std::size_t> {0U});
    CHECK(
        select_shadow_cascade(
            cascades,
            std::nextafter(
                first_split,
                std::numeric_limits<float>::infinity())) ==
        std::optional<std::size_t> {1U});
    CHECK(
        select_shadow_cascade(
            cascades,
            parameters.far_distance) ==
        std::optional<std::size_t> {1U});
    CHECK_FALSE(
        select_shadow_cascade(
            cascades,
            std::nextafter(
                parameters.far_distance,
                std::numeric_limits<float>::infinity()))
            .has_value());
    CHECK_FALSE(
        select_shadow_cascade(cascades, -0.01F).has_value());
    CHECK_FALSE(
        select_shadow_cascade(
            cascades,
            std::numeric_limits<float>::quiet_NaN())
            .has_value());
}

TEST_CASE("cascade transition overlaps both slices and blends smoothly by view depth") {
    ShadowCascadeBuildParameters parameters {};
    parameters.camera_position = {-37.0F, 24.0F, 81.0F};
    parameters.camera_forward =
        glm::normalize(glm::vec3 {0.42F, -0.16F, -0.89F});
    parameters.near_distance = 0.2F;
    parameters.far_distance = 240.0F;

    const auto cascades =
        build_shadow_cascade_set(parameters);
    REQUIRE(cascades.cascade_count == 2U);
    REQUIRE(cascades.transition_width > 0.0F);
    CHECK(cascades.transition_width <= 12.0F);

    const auto split = cascades.split_distances[1];
    const auto half_width =
        cascades.transition_width * 0.5F;
    CHECK(cascades.cascades[0].far_distance ==
          doctest::Approx(split + half_width));
    CHECK(cascades.cascades[1].near_distance ==
          doctest::Approx(split - half_width));

    const auto blend_start = split - half_width;
    const auto blend_end = split + half_width;
    CHECK(shadow_cascade_blend_factor(cascades, blend_start) ==
          doctest::Approx(0.0F));
    CHECK(shadow_cascade_blend_factor(cascades, split) ==
          doctest::Approx(0.5F));
    CHECK(shadow_cascade_blend_factor(cascades, blend_end) ==
          doctest::Approx(1.0F));
    CHECK(
        shadow_cascade_blend_factor(
            cascades,
            split - half_width * 0.5F) <
        shadow_cascade_blend_factor(
            cascades,
            split + half_width * 0.5F));

    const auto blend_start_world =
        parameters.camera_position +
        parameters.camera_forward * blend_start;
    const auto blend_end_world =
        parameters.camera_position +
        parameters.camera_forward * blend_end;
    CHECK(
        point_in_clip_volume(
            cascades.cascades[0].light_view_projection,
            blend_end_world));
    CHECK(
        point_in_clip_volume(
            cascades.cascades[1].light_view_projection,
            blend_start_world));
}

TEST_CASE("single cascade disables transition math deterministically") {
    ShadowCascadeBuildParameters parameters {};
    parameters.cascade_count = 1;
    const auto cascades =
        build_shadow_cascade_set(parameters);

    REQUIRE(cascades.cascade_count == 1U);
    CHECK(cascades.transition_width == 0.0F);
    CHECK(shadow_cascade_transition_width(cascades) == 0.0F);
    CHECK(shadow_cascade_blend_factor(cascades, 48.0F) == 0.0F);
    CHECK(
        shadow_cascade_blend_factor(
            cascades,
            std::numeric_limits<float>::quiet_NaN()) == 0.0F);
}

TEST_CASE("sub texel camera translation keeps the stabilized matrix unchanged") {
    ShadowCascadeBuildParameters parameters {};
    parameters.camera_position = {12.0F, 51.0F, -28.0F};
    parameters.camera_forward =
        glm::normalize(glm::vec3 {0.19F, -0.08F, -0.98F});
    parameters.sun_direction =
        glm::normalize(glm::vec3 {-0.33F, 0.88F, 0.34F});
    parameters.shadow_map_resolution = 2048;

    const auto reference =
        build_shadow_cascade_set(parameters);
    REQUIRE(reference.cascade_count == 2U);
    auto stable_lower_bound =
        -std::numeric_limits<float>::infinity();
    auto stable_upper_bound =
        std::numeric_limits<float>::infinity();
    auto smallest_texel =
        std::numeric_limits<float>::max();
    for (std::size_t index = 0;
         index < reference.cascade_count;
         ++index) {
        const auto& cascade = reference.cascades[index];
        const auto unsnapped =
            cascade.bounds.unsnapped_light_space_center.x;
        const auto snapped =
            cascade.bounds.stabilized_light_space_center.x;
        const auto half_safe_texel =
            cascade.world_units_per_texel * 0.49F;
        stable_lower_bound = std::max(
            stable_lower_bound,
            snapped - half_safe_texel - unsnapped);
        stable_upper_bound = std::min(
            stable_upper_bound,
            snapped + half_safe_texel - unsnapped);
        smallest_texel = std::min(
            smallest_texel,
            cascade.world_units_per_texel);
    }
    REQUIRE(stable_lower_bound < 0.0F);
    REQUIRE(stable_upper_bound > 0.0F);
    const auto positive_movement = std::min(
        smallest_texel * 0.2F,
        stable_upper_bound * 0.5F);
    const auto negative_movement = std::max(
        -smallest_texel * 0.2F,
        stable_lower_bound * 0.5F);
    const auto movement =
        positive_movement > smallest_texel * 0.01F
            ? positive_movement
            : negative_movement;
    REQUIRE(std::abs(movement) <
            smallest_texel);
    REQUIRE(std::abs(movement) >
            smallest_texel * 0.001F);

    // Je choisis l'intersection des cellules courantes des deux cascades : le
    // déplacement reste donc sub-texel pour chacune sans dépendre d'une
    // position initiale proche d'une frontière.
    parameters.camera_position +=
        reference.light_right_world * movement;
    const auto translated =
        build_shadow_cascade_set(parameters);

    REQUIRE(translated.cascade_count == 2U);
    for (std::size_t index = 0;
         index < reference.cascade_count;
         ++index) {
        CHECK(
            matrix_matches_exactly(
                reference.cascades[index]
                    .light_view_projection,
                translated.cascades[index]
                    .light_view_projection));
    }
}

TEST_CASE("invalid cascade inputs are repaired into finite deterministic data") {
    ShadowCascadeBuildParameters parameters {};
    parameters.quality =
        static_cast<RendererQuality>(255U);
    parameters.cascade_count = -7;
    parameters.camera_position = glm::vec3 {
        std::numeric_limits<float>::quiet_NaN(),
        -std::numeric_limits<float>::infinity(),
        std::numeric_limits<float>::infinity(),
    };
    parameters.camera_forward = glm::vec3 {0.0F};
    parameters.camera_up = glm::vec3 {0.0F};
    parameters.vertical_fov_radians =
        std::numeric_limits<float>::quiet_NaN();
    parameters.aspect_ratio = -2.0F;
    parameters.near_distance = -5.0F;
    parameters.far_distance = -10.0F;
    parameters.sun_direction = glm::vec3 {0.0F};
    parameters.shadow_map_resolution = -1;
    parameters.split_lambda =
        std::numeric_limits<float>::infinity();
    parameters.caster_depth_padding = -3.0F;

    const auto first = build_shadow_cascade_set(parameters);
    const auto second = build_shadow_cascade_set(parameters);
    REQUIRE(first.input_was_sanitized);
    REQUIRE(first.cascade_count == 1U);
    CHECK(first.shadow_map_resolution == 64);
    CHECK(first.split_distances == second.split_distances);
    CHECK(shadow_cascade_is_finite(first.cascades[0]));
    CHECK(
        matrix_matches_exactly(
            first.cascades[0].light_view_projection,
            second.cascades[0].light_view_projection));
}

} // namespace valcraft
