#include "render/ShadowCulling.h"

#include <doctest/doctest.h>

#include <glm/gtc/matrix_transform.hpp>

namespace valcraft {

TEST_CASE("camera culling can reject a chunk that still belongs in the shadow pass") {
    constexpr float shadow_distance = 96.0F;
    constexpr float back_cull_start_distance = 20.0F;

    const glm::vec3 eye {8.0F, 36.0F, 8.0F};
    const glm::vec3 forward {0.0F, 0.0F, -1.0F};
    const auto camera_projection = glm::perspective(glm::radians(75.0F), 16.0F / 9.0F, 0.1F, 320.0F);
    const auto camera_view = glm::lookAt(eye, eye + forward, glm::vec3 {0.0F, 1.0F, 0.0F});
    const auto camera_frustum = extract_frustum_planes(camera_projection * camera_view);

    const glm::vec3 focus {8.0F, 18.0F, 8.0F};
    const auto sun_direction = glm::normalize(glm::vec3 {0.25F, 0.9F, 0.35F});
    const auto light_position = focus + sun_direction * (shadow_distance * 0.85F);
    const auto light_view = glm::lookAt(light_position, focus, glm::vec3 {0.0F, 1.0F, 0.0F});
    const auto light_projection = glm::ortho(
        -shadow_distance,
        shadow_distance,
        -shadow_distance,
        shadow_distance,
        1.0F,
        shadow_distance * 3.0F);
    const auto light_frustum = extract_frustum_planes(light_projection * light_view);

    const auto bounds = make_chunk_bounds({0, 2});
    const auto draw_distance_sq = 160.0F * 160.0F;
    const auto max_shadow_distance = shadow_distance + static_cast<float>(kChunkSizeX);
    const auto max_shadow_distance_sq = max_shadow_distance * max_shadow_distance;

    CHECK_FALSE(should_render_chunk_in_camera_pass(
        bounds,
        camera_frustum,
        eye,
        forward,
        draw_distance_sq,
        back_cull_start_distance * back_cull_start_distance));
    CHECK(should_render_chunk_in_shadow_pass(bounds, light_frustum, focus, max_shadow_distance_sq));
}

TEST_CASE("shadow culling rejects chunks outside the light coverage volume") {
    constexpr float shadow_distance = 96.0F;

    const glm::vec3 focus {8.0F, 18.0F, 8.0F};
    const auto sun_direction = glm::normalize(glm::vec3 {0.25F, 0.9F, 0.35F});
    const auto light_position = focus + sun_direction * (shadow_distance * 0.85F);
    const auto light_view = glm::lookAt(light_position, focus, glm::vec3 {0.0F, 1.0F, 0.0F});
    const auto light_projection = glm::ortho(
        -shadow_distance,
        shadow_distance,
        -shadow_distance,
        shadow_distance,
        1.0F,
        shadow_distance * 3.0F);
    const auto light_frustum = extract_frustum_planes(light_projection * light_view);

    const auto far_bounds = make_chunk_bounds({16, 16});
    const auto max_shadow_distance = shadow_distance + static_cast<float>(kChunkSizeX);
    const auto max_shadow_distance_sq = max_shadow_distance * max_shadow_distance;

    CHECK_FALSE(should_render_chunk_in_shadow_pass(far_bounds, light_frustum, focus, max_shadow_distance_sq));
}

} // namespace valcraft
