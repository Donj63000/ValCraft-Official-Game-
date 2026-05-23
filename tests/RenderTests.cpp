#include "render/ShadowCulling.h"
#include "render/ItemDropGeometry.h"
#include "render/SceneSamplerBindings.h"
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

auto sample_accent_atlas_pixel(const std::vector<std::uint8_t>& pixels, int tile_x, int tile_y, int x, int y)
    -> std::array<std::uint8_t, 4> {
    const auto atlas_x = tile_x * kAccentAtlasTileSize + x;
    const auto atlas_y = tile_y * kAccentAtlasTileSize + y;
    const auto index = static_cast<std::size_t>((atlas_y * kAccentAtlasSize + atlas_x) * 4);
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
    CHECK(shader_source.find("u_overcast_intensity") != std::string_view::npos);
    CHECK(shader_source.find("u_precipitation_intensity") != std::string_view::npos);
    CHECK(shader_source.find("u_lightning_intensity") != std::string_view::npos);
    CHECK(shader_source.find("volumetric_cloud_minimum") != std::string_view::npos);
    CHECK(shader_source.find("max(cloud_factor, overcast_factor) > volumetric_cloud_minimum") != std::string_view::npos);
    CHECK(shader_source.find("star_spawn") != std::string_view::npos);
    CHECK(shader_source.find("star_weather_visibility") != std::string_view::npos);
}

TEST_CASE("accent atlas keeps celestial sprites bright and readable") {
    const auto atlas_pixels = build_accent_atlas_pixels();
    REQUIRE(atlas_pixels.size() == static_cast<std::size_t>(kAccentAtlasSize * kAccentAtlasSize * 4));

    const auto sun_tile = accent_atlas_tile(AccentAtlasSprite::Sun);
    const auto moon_tile = accent_atlas_tile(AccentAtlasSprite::Moon);
    const auto star_tile = accent_atlas_tile(AccentAtlasSprite::Star);
    const auto sun_core = sample_accent_atlas_pixel(atlas_pixels, sun_tile.x, sun_tile.y, 7, 7);
    const auto moon_core = sample_accent_atlas_pixel(atlas_pixels, moon_tile.x, moon_tile.y, 7, 7);
    const auto star_core = sample_accent_atlas_pixel(atlas_pixels, star_tile.x, star_tile.y, 7, 7);

    CHECK(sun_core[0] > 230);
    CHECK(sun_core[1] > 210);
    CHECK(moon_core[2] > moon_core[0]);
    CHECK(moon_core[3] > 200);
    CHECK(star_core[3] > 220);
    CHECK(star_core[0] > 220);
}

TEST_CASE("scene sampler bindings keep neutral fallback textures outside refraction passes") {
    constexpr std::uint32_t kFallbackColor = 11U;
    constexpr std::uint32_t kFallbackDepth = 12U;
    constexpr std::uint32_t kSceneColor = 21U;
    constexpr std::uint32_t kSceneDepth = 22U;
    const SceneSamplerBindings fallback_bindings {kFallbackColor, kFallbackDepth};
    const SceneSamplerBindings scene_bindings {kSceneColor, kSceneDepth};

    CHECK(select_scene_sampler_bindings(false, kFallbackColor, kFallbackDepth, kSceneColor, kSceneDepth) == fallback_bindings);
    CHECK(select_scene_sampler_bindings(true, kFallbackColor, kFallbackDepth, kSceneColor, kSceneDepth) == scene_bindings);
    CHECK(select_scene_sampler_bindings(true, kFallbackColor, kFallbackDepth, kSceneColor, 0U) == fallback_bindings);
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

TEST_CASE("equipment items stay inventory-only and expose readable antique icons") {
    struct GearVisualCase {
        BlockType type;
        BlockAtlasTile tile;
        std::array<int, 2> sample;
        BlockVisualMaterial material;
    };

    const std::array<GearVisualCase, 6> gear_items {{
        {BlockType::Pastron, {2, 4}, {5, 7}, BlockVisualMaterial::Rock},
        {BlockType::RoundShield, {3, 4}, {7, 7}, BlockVisualMaterial::Rock},
        {BlockType::Sword, {4, 4}, {8, 7}, BlockVisualMaterial::Rock},
        {BlockType::Spear, {5, 4}, {8, 8}, BlockVisualMaterial::Rock},
        {BlockType::Shoes, {6, 4}, {4, 11}, BlockVisualMaterial::Wood},
        {BlockType::Pants, {7, 4}, {5, 8}, BlockVisualMaterial::Wood},
    }};
    const auto atlas_pixels = build_block_atlas_pixels();
    REQUIRE(atlas_pixels.size() == static_cast<std::size_t>(kBlockAtlasSize * kBlockAtlasSize * 4));

    for (const auto& gear : gear_items) {
        const auto block_id = to_block_id(gear.type);
        const auto properties = block_properties(block_id);

        CHECK(is_inventory_only_item(block_id));
        CHECK_FALSE(is_placeable_item(block_id));
        CHECK_FALSE(has_block_mesh(block_id));
        CHECK_FALSE(is_block_breakable(block_id));
        CHECK_FALSE(properties.collidable);
        CHECK(block_hotbar_tile(block_id) == gear.tile);
        CHECK(block_visual_material(block_id) == gear.material);

        const auto icon_pixel =
            sample_block_atlas_pixel(atlas_pixels, gear.tile.x, gear.tile.y, gear.sample[0], gear.sample[1]);
        CHECK(icon_pixel[3] == 255);
    }
}

TEST_CASE("resource ore blocks expose distinct atlas tiles and item-drop materials") {
    struct OreVisualCase {
        BlockType type;
        BlockAtlasTile tile;
    };

    const std::array<OreVisualCase, 5> ore_items {{
        {BlockType::CoalOre, {0, 6}},
        {BlockType::IronOre, {1, 6}},
        {BlockType::GoldOre, {2, 6}},
        {BlockType::DiamondOre, {3, 6}},
        {BlockType::MetallicAlloyOre, {4, 6}},
    }};

    const auto atlas_pixels = build_block_atlas_pixels();
    REQUIRE(atlas_pixels.size() == static_cast<std::size_t>(kBlockAtlasSize * kBlockAtlasSize * 4));
    const auto stone_tile = block_atlas_tile(to_block_id(BlockType::Stone), BlockVisualFace::PositiveX);

    for (const auto& ore : ore_items) {
        const auto block_id = to_block_id(ore.type);
        auto opaque_pixels = 0;
        auto differentiated_pixels = 0;

        CHECK(is_resource_ore(block_id));
        CHECK_FALSE(is_inventory_only_item(block_id));
        CHECK(is_placeable_item(block_id));
        CHECK(block_atlas_tile(block_id, BlockVisualFace::PositiveX) == ore.tile);
        CHECK(block_hotbar_tile(block_id) == ore.tile);
        CHECK(block_visual_material(block_id) == BlockVisualMaterial::Rock);

        for (int y = 0; y < kBlockAtlasTileSize; ++y) {
            for (int x = 0; x < kBlockAtlasTileSize; ++x) {
                const auto ore_pixel = sample_block_atlas_pixel(atlas_pixels, ore.tile.x, ore.tile.y, x, y);
                const auto stone_pixel = sample_block_atlas_pixel(atlas_pixels, stone_tile.x, stone_tile.y, x, y);
                opaque_pixels += ore_pixel[3] == 255 ? 1 : 0;

                const auto red_delta = std::abs(static_cast<int>(ore_pixel[0]) - static_cast<int>(stone_pixel[0]));
                const auto green_delta = std::abs(static_cast<int>(ore_pixel[1]) - static_cast<int>(stone_pixel[1]));
                const auto blue_delta = std::abs(static_cast<int>(ore_pixel[2]) - static_cast<int>(stone_pixel[2]));
                differentiated_pixels += red_delta + green_delta + blue_delta > 42 ? 1 : 0;
            }
        }

        CAPTURE(static_cast<int>(ore.type));
        CHECK(opaque_pixels == kBlockAtlasTileSize * kBlockAtlasTileSize);
        CHECK(differentiated_pixels > 64);
    }
}

TEST_CASE("block atlas supports the antique Greek village material palette") {
    const auto atlas_pixels = build_block_atlas_pixels();
    REQUIRE(atlas_pixels.size() == static_cast<std::size_t>(kBlockAtlasSize * kBlockAtlasSize * 4));

    const auto stone_tile = block_atlas_tile(to_block_id(BlockType::Stone), BlockVisualFace::PositiveX);
    const auto sand_tile = block_atlas_tile(to_block_id(BlockType::Sand), BlockVisualFace::PositiveX);
    const auto roof_tile = block_atlas_tile(to_block_id(BlockType::Planks), BlockVisualFace::PositiveX);
    const auto stone_center = sample_block_atlas_pixel(atlas_pixels, stone_tile.x, stone_tile.y, 8, 8);
    const auto sand_center = sample_block_atlas_pixel(atlas_pixels, sand_tile.x, sand_tile.y, 8, 8);
    const auto roof_center = sample_block_atlas_pixel(atlas_pixels, roof_tile.x, roof_tile.y, 8, 10);
    const auto roof_seam = sample_block_atlas_pixel(atlas_pixels, roof_tile.x, roof_tile.y, 8, 4);

    CHECK(stone_center[0] > stone_center[2] + 20);
    CHECK(stone_center[1] > stone_center[2] + 12);
    CHECK(sand_center[0] > sand_center[2] + 40);
    CHECK(sand_center[1] > sand_center[2] + 24);
    CHECK(roof_center[0] > roof_center[1] + 45);
    CHECK(roof_center[1] > roof_center[2] + 24);
    CHECK(roof_seam[0] < roof_center[0]);
}

} // namespace valcraft
