#include "render/ShadowCulling.h"
#include "render/ItemDropGeometry.h"
#include "render/SkyShaderSource.h"
#include "world/BlockVisuals.h"

#include <doctest/doctest.h>

#include <glm/gtc/matrix_transform.hpp>

#include <array>
#include <cstdint>
#include <cmath>
#include <set>
#include <string_view>

namespace valcraft {

namespace {

struct FaceSample {
    std::array<int, 3> normal {};
    std::array<float, 4> uv_rect {};
};

auto atlas_uv_rect(const BlockAtlasTile& tile) -> std::array<float, 4> {
    const auto uv_step = 1.0F / kBlockAtlasTilesPerAxis;
    const auto u0 = static_cast<float>(tile.x) * uv_step;
    const auto v0 = static_cast<float>(tile.y) * uv_step;
    return {u0, v0, u0 + uv_step, v0 + uv_step};
}

auto rounded_normal(const ChunkVertex& vertex) -> std::array<int, 3> {
    return {
        static_cast<int>(std::lround(vertex.nx)),
        static_cast<int>(std::lround(vertex.ny)),
        static_cast<int>(std::lround(vertex.nz)),
    };
}

auto sample_block_atlas_pixel(const std::vector<std::uint8_t>& pixels, int tile_x, int tile_y, int x, int y)
    -> std::array<std::uint8_t, 4> {
    const auto atlas_x = tile_x * kBlockAtlasTileSize + x;
    const auto atlas_y = tile_y * kBlockAtlasTileSize + y;
    const auto index = static_cast<std::size_t>((atlas_y * kBlockAtlasSize + atlas_x) * 4);
    return {pixels[index + 0], pixels[index + 1], pixels[index + 2], pixels[index + 3]};
}

auto collect_face_samples(std::span<const ChunkVertex> vertices) -> std::vector<FaceSample> {
    std::vector<FaceSample> samples;
    samples.reserve(vertices.size() / 6U);

    for (std::size_t face_begin = 0; face_begin + 5 < vertices.size(); face_begin += 6U) {
        float min_u = vertices[face_begin].u;
        float min_v = vertices[face_begin].v;
        float max_u = vertices[face_begin].u;
        float max_v = vertices[face_begin].v;

        for (std::size_t offset = 0; offset < 6U; ++offset) {
            const auto& vertex = vertices[face_begin + offset];
            min_u = std::min(min_u, vertex.u);
            min_v = std::min(min_v, vertex.v);
            max_u = std::max(max_u, vertex.u);
            max_v = std::max(max_v, vertex.v);
        }

        samples.push_back({
            rounded_normal(vertices[face_begin]),
            {min_u, min_v, max_u, max_v},
        });
    }

    return samples;
}

void check_face_sample(const FaceSample& sample,
                       const std::array<int, 3>& expected_normal,
                       const std::array<float, 4>& expected_uv_rect) {
    CHECK(sample.normal == expected_normal);
    CHECK(sample.uv_rect[0] == doctest::Approx(expected_uv_rect[0]));
    CHECK(sample.uv_rect[1] == doctest::Approx(expected_uv_rect[1]));
    CHECK(sample.uv_rect[2] == doctest::Approx(expected_uv_rect[2]));
    CHECK(sample.uv_rect[3] == doctest::Approx(expected_uv_rect[3]));
}

} // namespace

TEST_CASE("sky shader avoids reserved GLSL noise identifiers") {
    const std::string_view shader_source {kSkyFragmentShaderSource};

    CHECK(shader_source.find("float noise3(") == std::string_view::npos);
    CHECK(shader_source.find("float value_noise3(") != std::string_view::npos);
    CHECK(shader_source.find("value_noise3(p)") != std::string_view::npos);
}

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

TEST_CASE("combined chunk visibility keeps shadow-only chunks in the shadow cache") {
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

    ShadowPassContext shadow_context {};
    shadow_context.frustum = extract_frustum_planes(light_projection * light_view);
    shadow_context.focus = focus;
    const auto max_shadow_distance = shadow_distance + static_cast<float>(kChunkSizeX);
    shadow_context.max_distance_sq = max_shadow_distance * max_shadow_distance;
    shadow_context.enabled = true;

    const auto visibility = classify_chunk_visibility(
        make_chunk_bounds({0, 2}),
        camera_frustum,
        eye,
        forward,
        160.0F * 160.0F,
        back_cull_start_distance * back_cull_start_distance,
        shadow_context,
        true);

    CHECK_FALSE(visibility.camera);
    CHECK(visibility.shadow);
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

TEST_CASE("item drop geometry keeps cube layers and atlas faces aligned with gpu instances") {
    ItemDropRenderInstance drop {};
    drop.position = {2.0F, 4.0F, 6.0F};
    drop.block_id = to_block_id(BlockType::Grass);
    drop.count = 1;
    drop.sky_light = 1.0F;

    const auto single_instances = build_item_drop_gpu_instances(std::span<const ItemDropRenderInstance>(&drop, 1));
    const auto single_vertices = build_item_drop_vertices(std::span<const ItemDropRenderInstance>(&drop, 1));
    REQUIRE(single_instances.size() == 1);
    CHECK(single_vertices.size() == 36);

    std::set<std::array<int, 3>> normals;
    for (const auto& vertex : single_vertices) {
        normals.insert(rounded_normal(vertex));
    }

    CHECK(normals.size() == 6);
    CHECK(normals.find(std::array<int, 3> {1, 0, 0}) != normals.end());
    CHECK(normals.find(std::array<int, 3> {-1, 0, 0}) != normals.end());
    CHECK(normals.find(std::array<int, 3> {0, 1, 0}) != normals.end());
    CHECK(normals.find(std::array<int, 3> {0, -1, 0}) != normals.end());

    const auto single_faces = collect_face_samples(single_vertices);
    REQUIRE(single_faces.size() == 6);
    const auto item_block = block_item_id(drop.block_id);
    check_face_sample(single_faces[0], {1, 0, 0}, atlas_uv_rect(block_atlas_tile(item_block, BlockVisualFace::PositiveX)));
    check_face_sample(single_faces[1], {-1, 0, 0}, atlas_uv_rect(block_atlas_tile(item_block, BlockVisualFace::NegativeX)));
    check_face_sample(single_faces[2], {0, 1, 0}, atlas_uv_rect(block_atlas_tile(item_block, BlockVisualFace::PositiveY)));
    check_face_sample(single_faces[3], {0, -1, 0}, atlas_uv_rect(block_atlas_tile(item_block, BlockVisualFace::NegativeY)));
    check_face_sample(single_faces[4], {0, 0, 1}, atlas_uv_rect(block_atlas_tile(item_block, BlockVisualFace::PositiveZ)));
    check_face_sample(single_faces[5], {0, 0, -1}, atlas_uv_rect(block_atlas_tile(item_block, BlockVisualFace::NegativeZ)));

    drop.count = 2;
    const auto double_instances = build_item_drop_gpu_instances(std::span<const ItemDropRenderInstance>(&drop, 1));
    const auto double_vertices = build_item_drop_vertices(std::span<const ItemDropRenderInstance>(&drop, 1));
    CHECK(double_instances.size() == 2);
    CHECK(double_vertices.size() == 72);

    drop.count = 32;
    const auto stacked_instances = build_item_drop_gpu_instances(std::span<const ItemDropRenderInstance>(&drop, 1));
    const auto stacked_vertices = build_item_drop_vertices(std::span<const ItemDropRenderInstance>(&drop, 1));
    CHECK(stacked_instances.size() == 3);
    CHECK(stacked_vertices.size() == 108);
}

TEST_CASE("glass block exposes a readable window tile for village construction") {
    const auto glass_id = to_block_id(BlockType::Glass);
    const auto glass_properties = block_properties(glass_id);

    CHECK_FALSE(glass_properties.opaque);
    CHECK(glass_properties.collidable);
    CHECK_FALSE(glass_properties.surface_support);
    CHECK_FALSE(glass_properties.replaceable);
    CHECK(glass_properties.mesh_type == BlockMeshType::FullCube);
    CHECK(block_atlas_tile(glass_id, BlockVisualFace::PositiveX) == BlockAtlasTile {1, 4});
    CHECK(block_hotbar_tile(glass_id) == BlockAtlasTile {1, 4});
    CHECK(block_visual_material(glass_id) == BlockVisualMaterial::Glass);

    const auto atlas_pixels = build_block_atlas_pixels();
    REQUIRE(atlas_pixels.size() == static_cast<std::size_t>(kBlockAtlasSize * kBlockAtlasSize * 4));

    const auto frame_pixel = sample_block_atlas_pixel(atlas_pixels, 1, 4, 0, 0);
    const auto pane_reflection_pixel = sample_block_atlas_pixel(atlas_pixels, 1, 4, 2, 2);
    const auto mullion_pixel = sample_block_atlas_pixel(atlas_pixels, 1, 4, 7, 7);
    const auto transparent_pane = sample_block_atlas_pixel(atlas_pixels, 1, 4, 5, 5);

    CHECK(frame_pixel[3] == 255);
    CHECK(mullion_pixel[3] == 255);
    CHECK(transparent_pane[3] == 0);
    CHECK(frame_pixel[0] > frame_pixel[2]);
    CHECK(pane_reflection_pixel[2] > pane_reflection_pixel[0]);
}

} // namespace valcraft
