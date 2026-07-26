#include "app/LoadingScreen.h"
#include "render/ShadowCulling.h"
#include "render/ItemDropGeometry.h"
#include "render/ModernHudStyle.h"
#include "render/SceneSamplerBindings.h"
#include "render/RendererQuality.h"
#include "render/Renderer.h"
#include "render/ShipMesh.h"
#include "render/ShipProtectionShaderSource.h"
#include "render/SkyShaderSource.h"
#include "render/VisualPipeline.h"
#include "world/BlockVisuals.h"

#include <doctest/doctest.h>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <set>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

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

TEST_CASE("loading progress stays finite monotone and phase weighted") {
    LoadingProgressTracker tracker;
    CHECK(tracker.progress() == doctest::Approx(0.0F));
    CHECK(tracker.update(LoadingPhase::Preparation, 1.0F) == doctest::Approx(0.05F));
    CHECK(tracker.update(LoadingPhase::Generation, 0.5F) == doctest::Approx(0.39F));
    CHECK(tracker.update(LoadingPhase::SaveRead, 1.0F) == doctest::Approx(0.39F));
    CHECK(tracker.update_absolute(0.80F) == doctest::Approx(0.80F));
    CHECK(tracker.update_absolute(-1.0F) == doctest::Approx(0.80F));
    CHECK(tracker.update_absolute(std::numeric_limits<float>::quiet_NaN()) == doctest::Approx(0.80F));
    CHECK(tracker.update_absolute(std::numeric_limits<float>::infinity()) == doctest::Approx(0.80F));
    CHECK(tracker.update(LoadingPhase::Finalization, 1.5F) == doctest::Approx(0.999F));
    CHECK_FALSE(tracker.completed());
    CHECK(tracker.complete() == doctest::Approx(1.0F));
    CHECK(tracker.completed());
    CHECK(tracker.phase() == LoadingPhase::Complete);
}

TEST_CASE("maritime loading quotes rotate deterministically without allocations in their views") {
    const auto quotes = maritime_loading_quotes();
    REQUIRE(quotes.size() >= 6U);

    const auto initial = make_maritime_loading_quote_view(1337U, 0.0);
    const auto repeated = make_maritime_loading_quote_view(1337U, 0.0);
    CHECK(initial.current == repeated.current);
    CHECK(initial.current_index == repeated.current_index);
    CHECK(initial.blend == doctest::Approx(0.0F));
    CHECK(make_maritime_loading_quote_view(1337U, -8.0).cycle == 0U);
    CHECK(make_maritime_loading_quote_view(1337U, std::numeric_limits<double>::quiet_NaN()).cycle == 0U);

    std::set<std::size_t> visited;
    for (std::size_t cycle = 0; cycle < quotes.size(); ++cycle) {
        const auto selection = make_maritime_loading_quote_view(
            1337U,
            static_cast<double>(cycle) * 5.0);
        CHECK(selection.current_index < quotes.size());
        CHECK(selection.current != selection.next);
        visited.insert(selection.current_index);
    }
    CHECK(visited.size() == quotes.size());
    CHECK(make_maritime_loading_quote_view(1337U, 4.7).blend == doctest::Approx(0.5F));

    for (const auto& quote : quotes) {
        CHECK_FALSE(quote.line1.empty());
        CHECK_FALSE(quote.line2.empty());
        CHECK_FALSE(quote.author.empty());
        for (const auto line : {quote.line1, quote.line2, quote.author}) {
            for (const auto character : line) {
                CHECK(is_loading_screen_character_supported(character));
            }
        }
    }
}

TEST_CASE("loading screen layout remains inside compact desktop and 4k viewports") {
    for (const auto dimensions : std::array<std::array<int, 2>, 3> {{{640, 360}, {1600, 900}, {3840, 2160}}}) {
        const auto layout = make_loading_screen_layout(
            LoadingScreenTheme::Maritime,
            dimensions[0],
            dimensions[1]);
        CHECK(layout.content_x >= 0.0F);
        CHECK(layout.content_x + layout.content_width <= layout.viewport_width);
        CHECK(layout.panel_y >= 0.0F);
        CHECK(layout.panel_y + layout.panel_height <= layout.viewport_height);
        CHECK(layout.track_x >= layout.content_x);
        CHECK(layout.track_x + layout.track_width <= layout.content_x + layout.content_width);
        CHECK(layout.track_y >= layout.panel_y);
        CHECK(layout.track_y + layout.track_height <= layout.panel_y + layout.panel_height);
        CHECK(layout.quote_y >= layout.panel_y);
        CHECK(layout.author_y + layout.quote_pixel_size * 7.0F <= layout.panel_y + layout.panel_height);
        CHECK(layout.title_y >= 0.0F);
        CHECK(layout.detail_y < layout.horizon_y);
    }
}

TEST_CASE("renderer world resource reset progress handles initial empty and incremental states") {
    RendererResourceResetProgress progress;
    CHECK(progress.complete());
    CHECK_FALSE(progress.active());
    CHECK(progress.remaining() == 0U);

    progress.begin(0U, false);
    CHECK(progress.complete());
    CHECK(progress.remaining() == 0U);

    progress.begin(3U, true);
    CHECK(progress.active());
    CHECK(progress.remaining() == 4U);
    progress.consume_one();
    CHECK(progress.remaining() == 3U);
    progress.consume_one();
    progress.consume_one();
    CHECK(progress.active());
    progress.consume_one();
    CHECK(progress.complete());
    CHECK(progress.remaining() == 0U);
}

TEST_CASE("visual pipeline contract keeps the legacy image as the renderer default") {
    constexpr RendererOptions default_options {};
    constexpr RendererFrameStats default_stats {};

    CHECK(default_options.visual_pipeline ==
          VisualPipeline::LegacyVoxel);
    CHECK(default_stats.visual_pipeline ==
          VisualPipeline::LegacyVoxel);
    CHECK(visual_pipeline_name(VisualPipeline::LegacyVoxel) ==
          std::string_view {"legacy"});
    CHECK(visual_pipeline_name(VisualPipeline::ModernStylized) ==
          std::string_view {"modern"});
    CHECK_FALSE(
        is_modern_visual_pipeline(VisualPipeline::LegacyVoxel));
    CHECK(
        is_modern_visual_pipeline(VisualPipeline::ModernStylized));
    CHECK(parse_visual_pipeline("legacy") ==
          VisualPipeline::LegacyVoxel);
    CHECK(parse_visual_pipeline("modern") ==
          VisualPipeline::ModernStylized);
    CHECK_FALSE(parse_visual_pipeline("").has_value());
    CHECK_FALSE(parse_visual_pipeline("stylized").has_value());

    CHECK(visual_pipeline_post_contrast(
              VisualPipeline::LegacyVoxel,
              1.12F) ==
          doctest::Approx(1.12F));
    CHECK(visual_pipeline_post_contrast(
              VisualPipeline::ModernStylized,
              1.12F) ==
          doctest::Approx(1.04F));
    CHECK(visual_pipeline_glow_threshold(
              VisualPipeline::LegacyVoxel,
              0.78F) ==
          doctest::Approx(0.78F));
    CHECK(visual_pipeline_glow_threshold(
              VisualPipeline::ModernStylized,
              0.78F) ==
          doctest::Approx(0.90F));
    CHECK(visual_pipeline_glow_threshold(
              VisualPipeline::ModernStylized,
              0.96F) ==
          doctest::Approx(1.0F));
    CHECK(visual_pipeline_glow_strength(
              VisualPipeline::LegacyVoxel,
              0.20F) ==
          doctest::Approx(0.20F));
    CHECK(visual_pipeline_glow_strength(
              VisualPipeline::ModernStylized,
              0.20F) ==
          doctest::Approx(0.11F));

    auto modern_options = default_options;
    modern_options.visual_pipeline =
        VisualPipeline::ModernStylized;
    CHECK(modern_options != default_options);
}

TEST_CASE("modern HUD rounded geometry stays finite and bounded") {
    const auto compact =
        modern_hud_rounded_rect_metrics(
            48.0F,
            48.0F,
            9.0F);
    CHECK(compact.radius == doctest::Approx(9.0F));
    CHECK(compact.corner_segments >= 4);
    CHECK(compact.corner_segments <= 10);
    CHECK(compact.vertex_count >= 54U);
    CHECK(compact.vertex_count <= 138U);

    const auto ultrawide =
        modern_hud_rounded_rect_metrics(
            3840.0F,
            120.0F,
            64.0F);
    CHECK(ultrawide.radius == doctest::Approx(60.0F));
    CHECK(ultrawide.corner_segments == 10);
    CHECK(ultrawide.vertex_count <= 138U);

    const auto clamped_radius =
        modern_hud_panel_radius(
            40.0F,
            18.0F,
            12.0F);
    CHECK(clamped_radius > 0.0F);
    CHECK(clamped_radius <= 9.0F);

    CHECK(
        modern_hud_rounded_rect_metrics(
            std::numeric_limits<float>::quiet_NaN(),
            20.0F,
            4.0F)
            .vertex_count == 0U);
    CHECK(
        modern_hud_rounded_rect_metrics(
            20.0F,
            -1.0F,
            4.0F)
            .corner_segments == 0);
}

TEST_CASE("renderer ship mesh cache remains not ready until revision renderer and gpu agree") {
    RendererShipMeshCacheState cache;
    constexpr std::uint64_t geometry_revision = 0xA6E11EULL;
    constexpr std::size_t part_count = 490U;
    CHECK_FALSE(cache.matches(geometry_revision, part_count));
    CHECK_FALSE(cache.ready(geometry_revision, part_count, false, false));

    cache.remember(geometry_revision, part_count);
    CHECK(cache.matches(geometry_revision, part_count));
    CHECK_FALSE(cache.ready(geometry_revision, part_count, false, true));
    CHECK_FALSE(cache.ready(geometry_revision, part_count, true, false));
    CHECK(cache.ready(geometry_revision, part_count, true, true));
    CHECK_FALSE(cache.ready(geometry_revision + 1U, part_count, true, true));
    CHECK_FALSE(cache.ready(geometry_revision, part_count - 1U, true, true));

    cache.reset();
    CHECK_FALSE(cache.matches(geometry_revision, part_count));
    CHECK_FALSE(cache.ready(geometry_revision, part_count, true, true));
}

static_assert(std::is_same_v<
              decltype(std::declval<Renderer&>().prepare_ship_mesh(std::declval<const ShipRenderState&>())),
              bool>);
static_assert(std::is_same_v<
              decltype(std::declval<Renderer&>().process_world_resource_reset(std::size_t {}, 0.0)),
              bool>);

using RenderFrameWithCrew = void (Renderer::*)(
    World&,
    const PlayerController&,
    const HotbarState&,
    const InventoryMenuState&,
    const DeathScreenState&,
    const PauseMenuState&,
    const MainMenuState&,
    const SaveSlotMenuState&,
    const OptionsMenuState&,
    const ConfirmDialogState&,
    std::span<const CreatureRenderInstance>,
    std::span<const CrewRenderInstance>,
    std::span<const OldGuardRenderInstance>,
    std::span<const OldGuardMuzzleFlashInstance>,
    std::span<const OldGuardSmokeInstance>,
    std::span<const ItemDropRenderInstance>,
    const ShipRenderState&,
    const PlayerProgressionState&,
    bool,
    const GameplayHudAnnouncementView&,
    const MaritimeHudView&,
    const CommandConsoleView&,
    const EnvironmentState&,
    int,
    int);

static_assert(std::is_same_v<
              decltype(static_cast<RenderFrameWithCrew>(&Renderer::render_frame)),
              RenderFrameWithCrew>);

TEST_CASE("crew renderer owns six additional long range slots and packed local lighting") {
    CHECK(kCrewVisualRenderCapacity == 6U);
    CHECK(kCrewVisualPartBudget == 64U);
    CHECK(kCrewVisualDrawDistance == doctest::Approx(96.0F));

    // Je garde ces quatre flottants contigus car ils partagent l'attribut GPU 15.
    CHECK(offsetof(CreaturePartInstance, sky_light) ==
          offsetof(CreaturePartInstance, emissive_strength) + sizeof(float));
    CHECK(offsetof(CreaturePartInstance, block_light) ==
          offsetof(CreaturePartInstance, sky_light) + sizeof(float));
    CHECK(offsetof(CreaturePartInstance, precipitation_exposure) ==
          offsetof(CreaturePartInstance, block_light) + sizeof(float));
}

TEST_CASE("sky shader avoids reserved GLSL noise identifiers") {
    const std::string_view shader_source {kSkyFragmentShaderSource};
    const std::string_view vertex_source {kSkyVertexShaderSource};

    CHECK(shader_source.find("float noise3(") == std::string_view::npos);
    CHECK(shader_source.find("float value_noise3(") != std::string_view::npos);
    CHECK(shader_source.find("value_noise3(p)") != std::string_view::npos);
    CHECK(shader_source.find("u_overcast_intensity") != std::string_view::npos);
    CHECK(shader_source.find("u_precipitation_intensity") != std::string_view::npos);
    CHECK(shader_source.find("uniform float u_violent_storm_intensity") != std::string_view::npos);
    CHECK(shader_source.find("uniform float u_lightning_intensity") != std::string_view::npos);
    CHECK(shader_source.find("uniform float u_lightning_bolt_intensity") != std::string_view::npos);
    CHECK(shader_source.find("uniform vec3 u_lightning_direction") != std::string_view::npos);
    CHECK(shader_source.find("uniform float u_lightning_shape_seed") != std::string_view::npos);
    CHECK(shader_source.find("float lightning_bolt_mask(vec3 view_direction)") != std::string_view::npos);
    CHECK(shader_source.find("u_lightning_direction.xz") != std::string_view::npos);
    CHECK(shader_source.find("u_lightning_direction.y") != std::string_view::npos);
    CHECK(shader_source.find("lightning_bolt_mask(direction)") != std::string_view::npos);
    CHECK(shader_source.find("volumetric_cloud_minimum") != std::string_view::npos);
    CHECK(shader_source.find("max(cloud_factor, overcast_factor) > volumetric_cloud_minimum") != std::string_view::npos);
    CHECK(shader_source.find("star_spawn") != std::string_view::npos);
    CHECK(shader_source.find("star_weather_visibility") != std::string_view::npos);
    CHECK(shader_source.find("uniform int u_cloud_steps") != std::string_view::npos);
    CHECK(shader_source.find("uniform float u_cloud_detail") != std::string_view::npos);
    CHECK(shader_source.find("step >= cloud_steps") != std::string_view::npos);
    CHECK(vertex_source.find("vec4(clip, 1.0, 1.0)") != std::string_view::npos);
}

TEST_CASE("ship protection GLSL keeps water and weather masks on one contract") {
    const auto shader_source = kShipProtectionGlslSource;

    CHECK(shader_source.find("uniform int u_ship_protection_enabled") !=
          std::string_view::npos);
    CHECK(shader_source.find("uniform mat4 u_ship_inverse_model") !=
          std::string_view::npos);
    CHECK(shader_source.find("uniform vec3 u_ship_bounds_min") !=
          std::string_view::npos);
    CHECK(shader_source.find("uniform vec3 u_ship_bounds_max") !=
          std::string_view::npos);
    CHECK(shader_source.find("uniform vec4 u_ship_profile_longitudinal") !=
          std::string_view::npos);
    CHECK(shader_source.find("uniform vec4 u_ship_profile_taper") !=
          std::string_view::npos);
    CHECK(shader_source.find("uniform vec4 u_ship_profile_heights") !=
          std::string_view::npos);
    CHECK(shader_source.find("uniform vec4 u_ship_profile_widths") !=
          std::string_view::npos);
    CHECK(shader_source.find("uniform float u_ship_sheltered_floor") !=
          std::string_view::npos);
    CHECK(shader_source.find("u_ship_protection_enabled == 0") !=
          std::string_view::npos);
    CHECK(shader_source.find("bool ship_excludes_ocean") !=
          std::string_view::npos);
    CHECK(shader_source.find("bool ship_shelters_weather") !=
          std::string_view::npos);
}

TEST_CASE("renderer precipitation pass keeps its shader and OpenGL contracts") {
    const auto renderer_path =
        std::filesystem::path {
            __FILE__,
        }
            .parent_path()
            .parent_path() /
        "src" /
        "render" /
        "Renderer.cpp";
    std::ifstream input(
        renderer_path,
        std::ios::binary);
    REQUIRE(input.is_open());
    const std::string source {
        std::istreambuf_iterator<char> {
            input,
        },
        std::istreambuf_iterator<char> {},
    };

    CHECK(source.find("rain_streak_layer") ==
          std::string::npos);
    CHECK(source.find("glDrawArraysInstanced") !=
          std::string::npos);
    CHECK(source.find("ScopedPrecipitationGlState") !=
          std::string::npos);
    CHECK(source.find("GL_DEPTH_WRITEMASK") !=
          std::string::npos);
    CHECK(source.find("glBlendFuncSeparate") !=
          std::string::npos);
    CHECK(source.find("precipitation_uniforms_.ship_protection_enabled") !=
          std::string::npos);
    CHECK(source.find("post_process_uniforms_.weather_exposure") !=
          std::string::npos);
    CHECK(source.find("ship_excludes_ocean(v_world_position)") !=
          std::string::npos);
}

TEST_CASE("renderer quality profiles bound expensive passes predictably") {
    const auto high = resolve_renderer_quality_settings(RendererQuality::High, 3840, 2160);
    const auto medium = resolve_renderer_quality_settings(RendererQuality::Medium, 1920, 1080);
    const auto low = resolve_renderer_quality_settings(RendererQuality::Low, 1280, 720);

    CHECK(high.resolved_quality == RendererQuality::High);
    CHECK(high.cloud_steps == 7);
    CHECK(high.cloud_detail == doctest::Approx(1.0F));
    CHECK(high.glow_downsample == 2);
    CHECK(high.high_precision_hdr);
    CHECK(high.ocean_wave_count == 6);
    CHECK(high.ocean_detail_scale == doctest::Approx(1.0F));

    CHECK(medium.cloud_steps == 4);
    CHECK(medium.cloud_detail < high.cloud_detail);
    CHECK(medium.glow_downsample == 3);
    CHECK_FALSE(medium.high_precision_hdr);
    CHECK(medium.ocean_wave_count == 4);
    CHECK(medium.ocean_detail_scale == doctest::Approx(0.70F));

    CHECK(low.cloud_steps == 2);
    CHECK(low.cloud_detail == doctest::Approx(0.0F));
    CHECK(low.post_detail_scale == doctest::Approx(0.0F));
    CHECK(low.glow_downsample == 4);
    CHECK(low.ocean_wave_count == 3);
    CHECK(low.ocean_detail_scale == doctest::Approx(0.0F));

    CHECK(resolve_renderer_quality_settings(RendererQuality::Dynamic, 1920, 1080).resolved_quality == RendererQuality::High);
    CHECK(resolve_renderer_quality_settings(RendererQuality::Dynamic, 2560, 1440).resolved_quality == RendererQuality::Medium);
    CHECK(resolve_renderer_quality_settings(RendererQuality::Dynamic, 3840, 2160).resolved_quality == RendererQuality::Low);
}

TEST_CASE("gpu timer conversion keeps asynchronous query units explicit") {
    CHECK(gpu_elapsed_nanoseconds_to_milliseconds(0U) == doctest::Approx(0.0));
    CHECK(gpu_elapsed_nanoseconds_to_milliseconds(1'000'000U) == doctest::Approx(1.0));
    CHECK(gpu_elapsed_nanoseconds_to_milliseconds(16'666'667U) == doctest::Approx(16.666667));
}

TEST_CASE("adaptive frame timing follows the slowest valid CPU or GPU sample") {
    const auto cpu_bound =
        resolve_adaptive_frame_time_sample(
            4.0,
            true,
            24.0,
            true);
    REQUIRE(cpu_bound.valid);
    CHECK(cpu_bound.frame_time_ms ==
          doctest::Approx(24.0));

    const auto gpu_bound =
        resolve_adaptive_frame_time_sample(
            31.0,
            true,
            7.0,
            true);
    REQUIRE(gpu_bound.valid);
    CHECK(gpu_bound.frame_time_ms ==
          doctest::Approx(31.0));

    const auto cpu_only =
        resolve_adaptive_frame_time_sample(
            0.0,
            false,
            18.5,
            true);
    REQUIRE(cpu_only.valid);
    CHECK(cpu_only.frame_time_ms ==
          doctest::Approx(18.5));

    const auto invalid =
        resolve_adaptive_frame_time_sample(
            std::numeric_limits<double>::quiet_NaN(),
            true,
            -1.0,
            true);
    CHECK_FALSE(invalid.valid);
}

TEST_CASE("adaptive stream radius sheds world work progressively") {
    CHECK(
        resolve_adaptive_stream_radius(
            5,
            RendererQuality::High) == 5);
    CHECK(
        resolve_adaptive_stream_radius(
            5,
            RendererQuality::Medium) == 4);
    CHECK(
        resolve_adaptive_stream_radius(
            5,
            RendererQuality::Low) == 3);
    CHECK(
        resolve_adaptive_stream_radius(
            1,
            RendererQuality::Low) == 0);
    CHECK(
        resolve_adaptive_stream_radius(
            -4,
            RendererQuality::Medium) == 0);
}

TEST_CASE("dynamic renderer quality downgrades quickly and upgrades with hysteresis") {
    RendererAdaptiveQualityController controller;
    CHECK(controller.settings(RendererQuality::Dynamic, 1920, 1080).resolved_quality == RendererQuality::High);

    static_cast<void>(controller.update(RendererQuality::Dynamic, 1920, 1080, 35.0));
    const auto downgraded = controller.update(RendererQuality::Dynamic, 1920, 1080, 35.0);
    CHECK(downgraded.resolved_quality == RendererQuality::Medium);

    for (std::size_t sample = 0; sample < 280U; ++sample) {
        static_cast<void>(controller.update(RendererQuality::Dynamic, 1920, 1080, 8.0));
    }
    CHECK(controller.state().resolved_quality == RendererQuality::High);
    CHECK(controller.state().frame_time_ema_ms == doctest::Approx(8.0));
    CHECK(controller.state().frame_time_p95_ms == doctest::Approx(8.0));
}

TEST_CASE("fixed renderer quality ignores adaptive timing samples") {
    RendererAdaptiveQualityController controller;
    for (std::size_t sample = 0; sample < 32U; ++sample) {
        static_cast<void>(controller.update(RendererQuality::High, 3840, 2160, 80.0));
    }
    CHECK(controller.state().resolved_quality == RendererQuality::High);
}

TEST_CASE("ship mesh removes joined box faces and keeps local coordinates") {
    const std::array<ShipPart, 2> parts {{
        {ShipPartShape::Box, ShipMaterial::LightDeck, {0.0F, 0.0F, 0.0F}, {1.0F, 1.0F, 1.0F}},
        {ShipPartShape::Box, ShipMaterial::LightDeck, {1.0F, 0.0F, 0.0F}, {2.0F, 1.0F, 1.0F}},
    }};
    const auto mesh = build_ship_mesh_data(parts);

    CHECK(mesh.face_count == 10U);
    CHECK(mesh.vertices.size() == 40U);
    CHECK(mesh.indices.size() == 60U);
    const auto max_x = std::max_element(mesh.vertices.begin(), mesh.vertices.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.x < rhs.x;
    });
    REQUIRE(max_x != mesh.vertices.end());
    CHECK(max_x->x == doctest::Approx(2.0F));
}

TEST_CASE("ship deck underside blocks its own sky lighting") {
    const std::array<ShipPart, 1> parts {{
        {ShipPartShape::Box, ShipMaterial::LightDeck, {0.0F, 0.0F, 0.0F}, {2.0F, 1.0F, 2.0F}},
    }};
    const auto mesh = build_ship_mesh_data(parts);
    auto underside_vertices = std::size_t {0U};
    auto top_vertices = std::size_t {0U};

    for (const auto& vertex : mesh.vertices) {
        if (vertex.ny < -0.9F) {
            ++underside_vertices;
            CHECK(vertex.sky_light < 0.5F);
        } else if (vertex.ny > 0.9F) {
            ++top_vertices;
            CHECK(vertex.sky_light == doctest::Approx(1.0F));
        }
    }
    CHECK(underside_vertices == 4U);
    CHECK(top_vertices == 4U);
}

TEST_CASE("ship lower deck lanterns illuminate the main deck ceiling") {
    const std::array<ShipPart, 2> parts {{
        {ShipPartShape::Box, ShipMaterial::LightDeck, {0.0F, 3.0F, 0.0F}, {2.0F, 4.0F, 2.0F}},
        {ShipPartShape::Box, ShipMaterial::Lantern, {0.85F, 2.30F, 0.85F}, {1.15F, 2.65F, 1.15F}},
    }};
    const auto mesh = build_ship_mesh_data(parts);
    auto ceiling_vertices = std::size_t {0U};
    for (const auto& vertex : mesh.vertices) {
        if (vertex.ny < -0.90F && std::abs(vertex.y - 3.0F) < 0.001F) {
            ++ceiling_vertices;
            CHECK(vertex.block_light > 0.0F);
        }
    }
    CHECK(ceiling_vertices == 4U);
}

TEST_CASE("ship mesh repeats atlas detail across long structural faces") {
    const std::array<ShipPart, 1> parts {{
        {ShipPartShape::Box, ShipMaterial::CleanBeam, {0.0F, 0.0F, 0.0F}, {6.0F, 1.0F, 1.0F}},
    }};
    const auto mesh = build_ship_mesh_data(parts);

    CHECK(mesh.face_count == 14U);
    CHECK(mesh.vertices.size() == mesh.face_count * 4U);
    CHECK(mesh.indices.size() == mesh.face_count * 6U);
}

TEST_CASE("ship canvas panel turns a fore and aft sail into a supported triangle") {
    const std::array<ShipPart, 1> parts {{
        {
            ShipPartShape::Panel,
            ShipMaterial::CreamCanvas,
            {0.0F, 0.0F, 0.0F},
            {0.1F, 4.0F, 4.0F},
            {1.0F, 0.0F, 0.0F},
            0.08F,
        },
    }};
    const auto mesh = build_ship_mesh_data(parts);
    REQUIRE(mesh.face_count > 5U);
    CHECK(mesh.vertices.size() == mesh.face_count * 4U);
    CHECK(mesh.indices.size() == mesh.face_count * 6U);

    auto top_vertex_count = std::size_t {0U};
    auto bottom_reaches_forward_corner = false;
    for (const auto& vertex : mesh.vertices) {
        if (std::abs(vertex.y - 4.0F) <= 0.001F) {
            ++top_vertex_count;
            CHECK(vertex.z == doctest::Approx(0.0F));
        }
        if (std::abs(vertex.y) <= 0.001F && std::abs(vertex.z - 4.0F) <= 0.001F) {
            bottom_reaches_forward_corner = true;
        }
    }
    CHECK(top_vertex_count > 0U);
    CHECK(bottom_reaches_forward_corner);
}

TEST_CASE("ship canvas keeps repeated detail across a large trapezoidal sail") {
    const std::array<ShipPart, 1> parts {{
        {
            ShipPartShape::Panel,
            ShipMaterial::BlackCanvas,
            {-6.0F, 0.0F, 0.0F},
            {6.0F, 14.0F, 0.10F},
            {0.0F, 0.0F, 1.0F},
            0.08F,
        },
    }};
    const auto mesh = build_ship_mesh_data(parts);
    REQUIRE(mesh.face_count > 12U);

    auto canvas_tiles = std::size_t {0U};
    for (std::size_t face = 0; face < mesh.vertices.size(); face += 4U) {
        const auto& first = mesh.vertices[face];
        if (std::abs(first.nz) < 0.90F) {
            continue;
        }
        const auto point = [](const ChunkVertex& vertex) {
            return glm::vec3 {vertex.x, vertex.y, vertex.z};
        };
        const auto first_span = glm::length(point(mesh.vertices[face + 1U]) - point(first));
        const auto second_span = glm::length(point(mesh.vertices[face + 3U]) - point(first));
        CHECK(first_span <= 2.01F);
        CHECK(second_span <= 2.01F);
        ++canvas_tiles;
    }
    CHECK(canvas_tiles > 20U);
}

TEST_CASE("climbable ship net is an open rope grid with reinforced borders") {
    const std::array<ShipPart, 1> parts {{
        {
            ShipPartShape::ClimbableNet,
            ShipMaterial::Rope,
            {-8.92F, -1.30F, -9.0F},
            {-8.92F, 4.48F, -6.0F},
            {-1.0F, 0.0F, 0.0F},
            0.04F,
            false,
            false,
        },
    }};
    const auto mesh = build_ship_mesh_data(parts);

    // Je verrouille ici les sept montants et treize traverses produits par
    // l'espacement de 50 cm, y compris leurs subdivisions de texture.
    CHECK(mesh.face_count == 228U);
    CHECK(mesh.vertices.size() == mesh.face_count * 4U);
    CHECK(mesh.indices.size() == mesh.face_count * 6U);
    CHECK(mesh.water_vertices.empty());
    CHECK(mesh.water_indices.empty());

    const auto rope_uv = atlas_uv_rect(ship_atlas_tile(ShipAtlasMaterial::Rope));
    auto minimum = glm::vec3 {std::numeric_limits<float>::max()};
    auto maximum = glm::vec3 {std::numeric_limits<float>::lowest()};
    for (const auto& vertex : mesh.vertices) {
        CHECK(std::isfinite(vertex.x));
        CHECK(std::isfinite(vertex.y));
        CHECK(std::isfinite(vertex.z));
        CHECK(std::isfinite(vertex.nx));
        CHECK(std::isfinite(vertex.ny));
        CHECK(std::isfinite(vertex.nz));
        CHECK(vertex.material_class == doctest::Approx(static_cast<float>(BlockVisualMaterial::Wood)));
        CHECK(vertex.u >= rope_uv[0]);
        CHECK(vertex.u <= rope_uv[2]);
        CHECK(vertex.v >= rope_uv[1]);
        CHECK(vertex.v <= rope_uv[3]);
        minimum = glm::min(minimum, glm::vec3 {vertex.x, vertex.y, vertex.z});
        maximum = glm::max(maximum, glm::vec3 {vertex.x, vertex.y, vertex.z});
    }
    for (const auto index : mesh.indices) {
        CHECK(index < mesh.vertices.size());
    }

    constexpr auto reinforced_half_width = 0.04F * 1.65F * 0.5F;
    CHECK(minimum.x == doctest::Approx(-8.92F - reinforced_half_width));
    CHECK(maximum.x == doctest::Approx(-8.92F + reinforced_half_width));
    CHECK(minimum.y == doctest::Approx(-1.30F - reinforced_half_width));
    CHECK(maximum.y == doctest::Approx(4.48F + reinforced_half_width));
    CHECK(minimum.z == doctest::Approx(-9.0F - reinforced_half_width));
    CHECK(maximum.z == doctest::Approx(-6.0F + reinforced_half_width));

    for (std::size_t face = 0; face < mesh.vertices.size(); face += 4U) {
        auto face_minimum = glm::vec3 {std::numeric_limits<float>::max()};
        auto face_maximum = glm::vec3 {std::numeric_limits<float>::lowest()};
        for (std::size_t corner = 0; corner < 4U; ++corner) {
            const auto& vertex = mesh.vertices[face + corner];
            const auto point = glm::vec3 {vertex.x, vertex.y, vertex.z};
            face_minimum = glm::min(face_minimum, point);
            face_maximum = glm::max(face_maximum, point);
        }
        const auto extent = face_maximum - face_minimum;
        const auto broad_axes = static_cast<int>(extent.x > 0.10F) +
                                static_cast<int>(extent.y > 0.10F) +
                                static_cast<int>(extent.z > 0.10F);
        // Je refuse toute face qui remplirait deux dimensions du filet : une
        // telle face transformerait le treillis en panneau opaque.
        CHECK(broad_axes <= 1);
    }
}

TEST_CASE("climbable ship nets mirror across X and also support Z-facing planes") {
    const ShipPart port {
        ShipPartShape::ClimbableNet,
        ShipMaterial::Rope,
        {-8.92F, -1.30F, -9.0F},
        {-8.92F, 4.48F, -6.0F},
        {-1.0F, 0.0F, 0.0F},
        0.04F,
        false,
        false,
    };
    auto starboard = port;
    starboard.local_start.x = 8.92F;
    starboard.local_end.x = 8.92F;
    starboard.orientation = {1.0F, 0.0F, 0.0F};

    const auto port_mesh = build_ship_mesh_data(std::span<const ShipPart>(&port, 1U));
    const auto starboard_mesh = build_ship_mesh_data(std::span<const ShipPart>(&starboard, 1U));
    REQUIRE(port_mesh.face_count == starboard_mesh.face_count);
    REQUIRE(port_mesh.vertices.size() == starboard_mesh.vertices.size());

    auto port_min_x = std::numeric_limits<float>::max();
    auto port_max_x = std::numeric_limits<float>::lowest();
    auto starboard_min_x = std::numeric_limits<float>::max();
    auto starboard_max_x = std::numeric_limits<float>::lowest();
    for (std::size_t index = 0; index < port_mesh.vertices.size(); ++index) {
        const auto& port_vertex = port_mesh.vertices[index];
        const auto& starboard_vertex = starboard_mesh.vertices[index];
        port_min_x = std::min(port_min_x, port_vertex.x);
        port_max_x = std::max(port_max_x, port_vertex.x);
        starboard_min_x = std::min(starboard_min_x, starboard_vertex.x);
        starboard_max_x = std::max(starboard_max_x, starboard_vertex.x);
        CHECK(port_vertex.y == doctest::Approx(starboard_vertex.y));
        CHECK(port_vertex.z == doctest::Approx(starboard_vertex.z));
    }
    CHECK(port_min_x == doctest::Approx(-starboard_max_x));
    CHECK(port_max_x == doctest::Approx(-starboard_min_x));

    const std::array<ShipPart, 2> z_facing {{
        {
            ShipPartShape::ClimbableNet,
            ShipMaterial::Rope,
            {-1.5F, 0.0F, 2.0F},
            {1.5F, 2.0F, 2.0F},
            {0.0F, 0.0F, 1.0F},
            0.04F,
            false,
            false,
        },
        {
            ShipPartShape::ClimbableNet,
            ShipMaterial::Rope,
            {-1.5F, 0.0F, -2.0F},
            {1.5F, 2.0F, -2.0F},
            {0.0F, 0.0F, -1.0F},
            0.04F,
            false,
            false,
        },
    }};
    const auto positive_z_mesh = build_ship_mesh_data(std::span<const ShipPart>(&z_facing[0], 1U));
    const auto negative_z_mesh = build_ship_mesh_data(std::span<const ShipPart>(&z_facing[1], 1U));
    CHECK(positive_z_mesh.face_count == 92U);
    CHECK(positive_z_mesh.face_count == negative_z_mesh.face_count);
    CHECK(positive_z_mesh.face_count < 128U);
}

TEST_CASE("stern glyph bitmap follows the outward face orientation") {
    const std::array<ShipPart, 1> parts {{
        {
            ShipPartShape::Glyph,
            ShipMaterial::Brass,
            {0.0F, 0.0F, 0.0F},
            {5.0F, 7.0F, 0.10F},
            {0.0F, 0.0F, -1.0F},
            0.06F,
            false,
            false,
            U'L',
        },
    }};
    const auto mesh = build_ship_mesh_data(parts);
    auto top_min_x = std::numeric_limits<float>::max();
    auto top_max_x = std::numeric_limits<float>::lowest();
    for (const auto& vertex : mesh.vertices) {
        if (vertex.y <= 5.95F) {
            continue;
        }
        top_min_x = std::min(top_min_x, vertex.x);
        top_max_x = std::max(top_max_x, vertex.x);
    }
    CHECK(top_min_x == doctest::Approx(4.0F));
    CHECK(top_max_x == doctest::Approx(4.82F));
}

TEST_CASE("main mast engraving renders the lowercase a used by L'amelie") {
    const std::array<ShipPart, 1> parts {{
        {
            ShipPartShape::Glyph,
            ShipMaterial::Iron,
            {-0.18F, 0.0F, -0.232F},
            {0.18F, 0.52F, -0.205F},
            {0.0F, 0.0F, -1.0F},
            0.06F,
            false,
            false,
            U'a',
        },
    }};

    const auto mesh =
        build_ship_mesh_data(parts);

    REQUIRE_FALSE(mesh.empty());
    CHECK(mesh.face_count > 0U);

    for (const auto& vertex : mesh.vertices) {
        CHECK(
            vertex.material_class ==
            doctest::Approx(
                static_cast<float>(
                    BlockVisualMaterial::Metal)));
    }
}

TEST_CASE("L'Amelie mesh renders every maritime family with interior and lantern lighting") {
    const auto& blueprint = amelie_ship_blueprint();
    std::vector<ShipPart> climbable_nets;
    for (const auto& part : blueprint.parts) {
        if (part.shape == ShipPartShape::ClimbableNet) {
            climbable_nets.push_back(part);
        }
    }
    REQUIRE(climbable_nets.size() == 2U);
    const auto net_mesh = build_ship_mesh_data(climbable_nets);
    CHECK(net_mesh.face_count == 456U);
    CHECK(net_mesh.face_count < 512U);
    CHECK(net_mesh.vertices.size() == net_mesh.face_count * 4U);
    CHECK(net_mesh.indices.size() == net_mesh.face_count * 6U);

    const auto mesh = build_ship_mesh_data(blueprint.parts);

    REQUIRE_FALSE(mesh.empty());
    CHECK(mesh.face_count > blueprint.parts.size());
    CHECK(mesh.vertices.size() == mesh.face_count * 4U);
    CHECK(mesh.indices.size() == mesh.face_count * 6U);
    CHECK(mesh.water_vertices.empty());
    CHECK(mesh.water_indices.empty());
    CHECK(mesh.face_count < blueprint.parts.size() * 80U);
    for (const auto index : mesh.indices) {
        CHECK(index < mesh.vertices.size());
    }

    std::set<int> materials;
    auto minimum_sky = 1.0F;
    auto maximum_sky = 0.0F;
    auto maximum_block = 0.0F;
    for (const auto& vertex : mesh.vertices) {
        CHECK(std::isfinite(vertex.x));
        CHECK(std::isfinite(vertex.y));
        CHECK(std::isfinite(vertex.z));
        CHECK(vertex.x >= blueprint.bounds.min.x - 0.25F);
        CHECK(vertex.x <= blueprint.bounds.max.x + 0.25F);
        CHECK(vertex.y >= blueprint.bounds.min.y - 0.25F);
        CHECK(vertex.y <= blueprint.bounds.max.y + 0.25F);
        CHECK(vertex.z >= blueprint.bounds.min.z - 0.25F);
        CHECK(vertex.z <= blueprint.bounds.max.z + 0.25F);
        CHECK(vertex.ao >= 0.60F);
        CHECK(vertex.ao <= 1.0F);
        CHECK(vertex.sky_light >= 0.0F);
        CHECK(vertex.sky_light <= 1.0F);
        CHECK(vertex.block_light >= 0.0F);
        CHECK(vertex.block_light <= 1.0F);
        materials.insert(static_cast<int>(std::lround(vertex.material_class)));
        minimum_sky = std::min(minimum_sky, vertex.sky_light);
        maximum_sky = std::max(maximum_sky, vertex.sky_light);
        maximum_block = std::max(maximum_block, vertex.block_light);
    }

    CHECK(materials.contains(static_cast<int>(BlockVisualMaterial::Wood)));
    CHECK(materials.contains(static_cast<int>(BlockVisualMaterial::Fabric)));
    CHECK(materials.contains(static_cast<int>(BlockVisualMaterial::Metal)));
    CHECK(materials.contains(static_cast<int>(BlockVisualMaterial::Brass)));
    CHECK(materials.contains(static_cast<int>(BlockVisualMaterial::Emissive)));
    CHECK(materials.contains(static_cast<int>(BlockVisualMaterial::Glass)));
    CHECK(minimum_sky < 0.5F);
    CHECK(maximum_sky == doctest::Approx(1.0F));
    CHECK(maximum_block >= 11.0F / 15.0F);

    auto illuminated_cabin_ceiling_vertices = std::size_t {0};
    auto downward_cabin_vertices = std::size_t {0};
    auto lowest_cabin_ceiling = std::numeric_limits<float>::max();
    auto highest_cabin_ceiling = std::numeric_limits<float>::lowest();
    for (const auto& vertex : mesh.vertices) {
        if (vertex.ny < -0.90F && std::abs(vertex.x - 1.25F) < 2.0F && std::abs(vertex.z + 30.5F) < 2.0F) {
            ++downward_cabin_vertices;
            lowest_cabin_ceiling = std::min(lowest_cabin_ceiling, vertex.y);
            highest_cabin_ceiling = std::max(highest_cabin_ceiling, vertex.y);
        }
        if (vertex.ny < -0.90F && vertex.y >= 3.60F && vertex.y <= 3.70F &&
            std::abs(vertex.x - 1.25F) < 2.0F && std::abs(vertex.z + 30.5F) < 2.0F) {
            ++illuminated_cabin_ceiling_vertices;
            CHECK(vertex.block_light > 0.0F);
        }
    }
    CAPTURE(downward_cabin_vertices);
    CAPTURE(lowest_cabin_ceiling);
    CAPTURE(highest_cabin_ceiling);
    CHECK(illuminated_cabin_ceiling_vertices > 0U);
}

TEST_CASE("large ship visibility uses its nearest bounds instead of its center") {
    std::array<FrustumPlane, 6> permissive_frustum {};
    for (auto& plane : permissive_frustum) {
        plane.normal = {0.0F, 1.0F, 0.0F};
        plane.distance = 1'000.0F;
    }
    const ChunkBounds ship_bounds {
        {-7.2F, -2.0F, -30.5F},
        {7.2F, 24.5F, 38.5F},
        {0.0F, 11.25F, 4.0F},
    };
    const glm::vec3 eye {0.0F, 6.0F, -25.0F};
    const glm::vec3 looking_towards_stern {0.0F, 0.0F, -1.0F};
    ShadowPassContext shadow_context {};
    shadow_context.enabled = true;
    shadow_context.frustum = permissive_frustum;
    shadow_context.focus = eye;
    shadow_context.max_distance_sq = 4.0F;

    const auto generic_visibility = classify_chunk_visibility(
        ship_bounds,
        permissive_frustum,
        eye,
        looking_towards_stern,
        10'000.0F,
        100.0F,
        shadow_context,
        true);
    CHECK_FALSE(generic_visibility.camera);
    CHECK_FALSE(generic_visibility.shadow);

    const auto ship_visibility = classify_large_bounds_visibility(
        ship_bounds,
        permissive_frustum,
        eye,
        10'000.0F,
        shadow_context,
        true);
    CHECK(ship_visibility.camera);
    CHECK(ship_visibility.shadow);
    CHECK(ship_visibility.distance_squared == doctest::Approx(0.0F));
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

    drop.count = 1;
    drop.age_seconds = (std::numeric_limits<float>::max)();
    drop.spin_radians = (std::numeric_limits<float>::max)();
    const auto safe_instances = build_item_drop_gpu_instances(std::span<const ItemDropRenderInstance>(&drop, 1));
    const auto safe_vertices = build_item_drop_vertices(std::span<const ItemDropRenderInstance>(&drop, 1));
    REQUIRE(safe_instances.size() == 1);
    CHECK(std::isfinite(safe_instances.front().center.x));
    CHECK(std::isfinite(safe_instances.front().center.y));
    CHECK(std::isfinite(safe_instances.front().center.z));
    CHECK(std::isfinite(safe_instances.front().rotation));
    for (const auto& vertex : safe_vertices) {
        CHECK(std::isfinite(vertex.x));
        CHECK(std::isfinite(vertex.y));
        CHECK(std::isfinite(vertex.z));
    }
}

TEST_CASE("item drop atlas mapping covers tools and ignores corrupted block ids") {
    constexpr std::array<BlockVisualFace, 6> faces {{
        BlockVisualFace::PositiveX,
        BlockVisualFace::NegativeX,
        BlockVisualFace::PositiveY,
        BlockVisualFace::NegativeY,
        BlockVisualFace::PositiveZ,
        BlockVisualFace::NegativeZ,
    }};

    for (BlockId block_id = to_block_id(BlockType::Grass); block_id <= to_block_id(BlockType::Shovel); ++block_id) {
        const auto item_id = block_item_id(block_id);
        CAPTURE(static_cast<int>(block_id));
        REQUIRE(item_id != to_block_id(BlockType::Air));

        for (const auto face : faces) {
            CHECK(item_drop_atlas_tile(block_id, face) == block_atlas_tile(item_id, face));
        }

        ItemDropRenderInstance drop {};
        drop.block_id = block_id;
        drop.count = 1;
        const auto instances = build_item_drop_gpu_instances(std::span<const ItemDropRenderInstance>(&drop, 1));
        REQUIRE(instances.size() == 1);
        CHECK(instances.front().block_id == item_id);
    }

    struct ToolCase {
        BlockType type;
        BlockAtlasTile tile;
    };
    constexpr std::array<ToolCase, 3> tools {{
        {BlockType::Pickaxe, {5, 6}},
        {BlockType::Axe, {6, 6}},
        {BlockType::Shovel, {7, 6}},
    }};

    for (const auto& tool : tools) {
        const auto block_id = to_block_id(tool.type);
        CAPTURE(static_cast<int>(tool.type));
        CHECK(item_drop_atlas_tile(block_id, BlockVisualFace::PositiveX) == tool.tile);

        ItemDropRenderInstance drop {};
        drop.block_id = block_id;
        drop.count = 1;
        const auto instances = build_item_drop_gpu_instances(std::span<const ItemDropRenderInstance>(&drop, 1));
        REQUIRE(instances.size() == 1);
        CHECK(instances.front().face_tiles_0_1.x == doctest::Approx(static_cast<float>(tool.tile.x)));
        CHECK(instances.front().face_tiles_0_1.y == doctest::Approx(static_cast<float>(tool.tile.y)));
    }

    ItemDropRenderInstance corrupted {};
    corrupted.block_id = static_cast<BlockId>(255U);
    corrupted.count = 1;
    const auto corrupted_instances = build_item_drop_gpu_instances(std::span<const ItemDropRenderInstance>(&corrupted, 1));
    const auto corrupted_vertices = build_item_drop_vertices(std::span<const ItemDropRenderInstance>(&corrupted, 1));
    CHECK(corrupted_instances.empty());
    CHECK(corrupted_vertices.empty());
    CHECK(item_drop_atlas_tile(corrupted.block_id, BlockVisualFace::PositiveX) == BlockAtlasTile {});
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

TEST_CASE("equipment and tool items stay inventory-only and expose readable icons") {
    struct GearVisualCase {
        BlockType type;
        BlockAtlasTile tile;
        std::array<int, 2> sample;
        BlockVisualMaterial material;
    };

    const std::array<GearVisualCase, 9> gear_items {{
        {BlockType::Pastron, {2, 4}, {5, 7}, BlockVisualMaterial::Rock},
        {BlockType::RoundShield, {3, 4}, {7, 7}, BlockVisualMaterial::Rock},
        {BlockType::Sword, {4, 4}, {8, 7}, BlockVisualMaterial::Rock},
        {BlockType::Spear, {5, 4}, {8, 8}, BlockVisualMaterial::Rock},
        {BlockType::Shoes, {6, 4}, {4, 11}, BlockVisualMaterial::Wood},
        {BlockType::Pants, {7, 4}, {5, 8}, BlockVisualMaterial::Wood},
        {BlockType::Pickaxe, {5, 6}, {8, 4}, BlockVisualMaterial::Rock},
        {BlockType::Axe, {6, 6}, {11, 5}, BlockVisualMaterial::Wood},
        {BlockType::Shovel, {7, 6}, {7, 12}, BlockVisualMaterial::Rock},
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

TEST_CASE("L'Amelie ship atlas owns ten deterministic and distinct maritime materials") {
    const std::array<
        ShipAtlasMaterial,
        kShipAtlasMaterialCount
    > materials {{
        ShipAtlasMaterial::DarkHull,
        ShipAtlasMaterial::LightDeck,
        ShipAtlasMaterial::CleanBeam,
        ShipAtlasMaterial::CreamCanvas,
        ShipAtlasMaterial::Rope,
        ShipAtlasMaterial::Iron,
        ShipAtlasMaterial::Brass,
        ShipAtlasMaterial::Lantern,
        ShipAtlasMaterial::BlackCanvas,
        ShipAtlasMaterial::SolidGold,
    }};

    const auto first_pixels =
        build_block_atlas_pixels();

    const auto second_pixels =
        build_block_atlas_pixels();

    REQUIRE(first_pixels == second_pixels);

    std::array<
        std::uint64_t,
        kShipAtlasMaterialCount
    > checksums {};

    std::set<std::pair<int, int>>
        occupied_tiles;

    for (std::size_t material_index = 0;
         material_index < materials.size();
         ++material_index) {

        const auto tile =
            ship_atlas_tile(
                materials[material_index]);

        CHECK(tile.x >= 0);
        CHECK(
            tile.x <
            static_cast<int>(
                kBlockAtlasTilesPerAxis));

        CHECK(tile.y >= 0);
        CHECK(
            tile.y <
            static_cast<int>(
                kBlockAtlasTilesPerAxis));

        CHECK(
            occupied_tiles
                .emplace(tile.x, tile.y)
                .second);

        // Les huit matériaux historiques ne doivent jamais changer de case.
        if (material_index < 8U) {
            CHECK(
                tile.x ==
                static_cast<int>(
                    material_index));

            CHECK(
                tile.y ==
                kShipAtlasRow);
        }

        auto checksum =
            std::uint64_t {
                1469598103934665603ULL,
            };

        for (int y = 0;
             y < kBlockAtlasTileSize;
             ++y) {

            for (int x = 0;
                 x < kBlockAtlasTileSize;
                 ++x) {

                const auto pixel =
                    sample_block_atlas_pixel(
                        first_pixels,
                        tile.x,
                        tile.y,
                        x,
                        y);

                REQUIRE(pixel[3] == 255U);

                for (const auto channel : pixel) {
                    checksum ^=
                        static_cast<
                            std::uint64_t>(
                            channel);

                    checksum *=
                        1099511628211ULL;
                }
            }
        }

        checksums[material_index] =
            checksum;
    }

    for (std::size_t left = 0;
         left < checksums.size();
         ++left) {

        for (std::size_t right = left + 1U;
             right < checksums.size();
             ++right) {

            CHECK(
                checksums[left] !=
                checksums[right]);
        }
    }

    const auto black_tile =
        ship_atlas_tile(
            ShipAtlasMaterial::BlackCanvas);

    const auto gold_tile =
        ship_atlas_tile(
            ShipAtlasMaterial::SolidGold);

    const auto black_center =
        sample_block_atlas_pixel(
            first_pixels,
            black_tile.x,
            black_tile.y,
            8,
            8);

    const auto gold_center =
        sample_block_atlas_pixel(
            first_pixels,
            gold_tile.x,
            gold_tile.y,
            8,
            8);

    CHECK(black_center[0] < 80U);
    CHECK(black_center[1] < 80U);
    CHECK(black_center[2] < 90U);

    CHECK(gold_center[0] > 150U);
    CHECK(gold_center[0] > gold_center[1]);
    CHECK(gold_center[1] > gold_center[2]);

    CHECK(
        ship_visual_material(
            ShipAtlasMaterial::CreamCanvas) ==
        BlockVisualMaterial::Fabric);

    CHECK(
        ship_visual_material(
            ShipAtlasMaterial::BlackCanvas) ==
        BlockVisualMaterial::Fabric);

    CHECK(
        ship_visual_material(
            ShipAtlasMaterial::Iron) ==
        BlockVisualMaterial::Metal);

    CHECK(
        ship_visual_material(
            ShipAtlasMaterial::Brass) ==
        BlockVisualMaterial::Brass);

    CHECK(
        ship_visual_material(
            ShipAtlasMaterial::SolidGold) ==
        BlockVisualMaterial::Brass);

    CHECK(
        ship_visual_material(
            ShipAtlasMaterial::Lantern) ==
        BlockVisualMaterial::Emissive);
}

} // namespace valcraft
