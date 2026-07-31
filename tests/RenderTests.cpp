#include "app/LoadingScreen.h"
#include "render/ItemDropGeometry.h"
#include "render/BackroomsVisibility.h"
#include "render/ModernHudStyle.h"
#include "render/ModernTerrainShaderSource.h"
#include "render/ModernWaterShaderSource.h"
#include "render/MusketVisualRecipe.h"
#include "render/Renderer.h"
#include "render/RendererQuality.h"
#include "render/SceneSamplerBindings.h"
#include "render/ShadowCulling.h"
#include "render/ShipMesh.h"
#include "render/ShipProtectionShaderSource.h"
#include "render/SkyShaderSource.h"
#include "render/VisualPipeline.h"
#include "world/BlockVisuals.h"

#include <doctest/doctest.h>

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
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
  std::array<int, 3> normal{};
  std::array<float, 4> uv_rect{};
};

auto atlas_uv_rect(const BlockAtlasTile &tile) -> std::array<float, 4> {
  const auto uv_step = 1.0F / kBlockAtlasTilesPerAxis;
  const auto u0 = static_cast<float>(tile.x) * uv_step;
  const auto v0 = static_cast<float>(tile.y) * uv_step;
  return {u0, v0, u0 + uv_step, v0 + uv_step};
}

auto rounded_normal(const ChunkVertex &vertex) -> std::array<int, 3> {
  return {
      static_cast<int>(std::lround(vertex.nx)),
      static_cast<int>(std::lround(vertex.ny)),
      static_cast<int>(std::lround(vertex.nz)),
  };
}

auto sample_block_atlas_pixel(const std::vector<std::uint8_t> &pixels,
                              int tile_x, int tile_y, int x, int y)
    -> std::array<std::uint8_t, 4> {
  const auto atlas_x = tile_x * kBlockAtlasTileSize + x;
  const auto atlas_y = tile_y * kBlockAtlasTileSize + y;
  const auto index =
      static_cast<std::size_t>((atlas_y * kBlockAtlasSize + atlas_x) * 4);
  return {pixels[index + 0], pixels[index + 1], pixels[index + 2],
          pixels[index + 3]};
}

auto sample_accent_atlas_pixel(const std::vector<std::uint8_t> &pixels,
                               int tile_x, int tile_y, int x, int y)
    -> std::array<std::uint8_t, 4> {
  const auto atlas_x = tile_x * kAccentAtlasTileSize + x;
  const auto atlas_y = tile_y * kAccentAtlasTileSize + y;
  const auto index =
      static_cast<std::size_t>((atlas_y * kAccentAtlasSize + atlas_x) * 4);
  return {pixels[index + 0], pixels[index + 1], pixels[index + 2],
          pixels[index + 3]};
}

auto collect_face_samples(std::span<const ChunkVertex> vertices)
    -> std::vector<FaceSample> {
  std::vector<FaceSample> samples;
  samples.reserve(vertices.size() / 6U);

  for (std::size_t face_begin = 0; face_begin + 5 < vertices.size();
       face_begin += 6U) {
    float min_u = vertices[face_begin].u;
    float min_v = vertices[face_begin].v;
    float max_u = vertices[face_begin].u;
    float max_v = vertices[face_begin].v;

    for (std::size_t offset = 0; offset < 6U; ++offset) {
      const auto &vertex = vertices[face_begin + offset];
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

void check_face_sample(const FaceSample &sample,
                       const std::array<int, 3> &expected_normal,
                       const std::array<float, 4> &expected_uv_rect) {
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
  CHECK(tracker.update(LoadingPhase::Preparation, 1.0F) ==
        doctest::Approx(0.05F));
  CHECK(tracker.update(LoadingPhase::Generation, 0.5F) ==
        doctest::Approx(0.39F));
  CHECK(tracker.update(LoadingPhase::SaveRead, 1.0F) == doctest::Approx(0.39F));
  CHECK(tracker.update_absolute(0.80F) == doctest::Approx(0.80F));
  CHECK(tracker.update_absolute(-1.0F) == doctest::Approx(0.80F));
  CHECK(tracker.update_absolute(std::numeric_limits<float>::quiet_NaN()) ==
        doctest::Approx(0.80F));
  CHECK(tracker.update_absolute(std::numeric_limits<float>::infinity()) ==
        doctest::Approx(0.80F));
  CHECK(tracker.update(LoadingPhase::Finalization, 1.5F) ==
        doctest::Approx(0.999F));
  CHECK_FALSE(tracker.completed());
  CHECK(tracker.complete() == doctest::Approx(1.0F));
  CHECK(tracker.completed());
  CHECK(tracker.phase() == LoadingPhase::Complete);
}

TEST_CASE("maritime loading quotes rotate deterministically without "
          "allocations in their views") {
  const auto quotes = maritime_loading_quotes();
  REQUIRE(quotes.size() >= 6U);

  const auto initial = make_maritime_loading_quote_view(1337U, 0.0);
  const auto repeated = make_maritime_loading_quote_view(1337U, 0.0);
  CHECK(initial.current == repeated.current);
  CHECK(initial.current_index == repeated.current_index);
  CHECK(initial.blend == doctest::Approx(0.0F));
  CHECK(make_maritime_loading_quote_view(1337U, -8.0).cycle == 0U);
  CHECK(make_maritime_loading_quote_view(
            1337U, std::numeric_limits<double>::quiet_NaN())
            .cycle == 0U);

  std::set<std::size_t> visited;
  for (std::size_t cycle = 0; cycle < quotes.size(); ++cycle) {
    const auto selection = make_maritime_loading_quote_view(
        1337U, static_cast<double>(cycle) * 5.0);
    CHECK(selection.current_index < quotes.size());
    CHECK(selection.current != selection.next);
    visited.insert(selection.current_index);
  }
  CHECK(visited.size() == quotes.size());
  CHECK(make_maritime_loading_quote_view(1337U, 4.7).blend ==
        doctest::Approx(0.5F));

  for (const auto &quote : quotes) {
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

TEST_CASE(
    "loading screen layout remains inside compact desktop and 4k viewports") {
  for (const auto dimensions : std::array<std::array<int, 2>, 3>{
           {{640, 360}, {1600, 900}, {3840, 2160}}}) {
    const auto layout = make_loading_screen_layout(
        LoadingScreenTheme::Maritime, dimensions[0], dimensions[1]);
    CHECK(layout.content_x >= 0.0F);
    CHECK(layout.content_x + layout.content_width <= layout.viewport_width);
    CHECK(layout.panel_y >= 0.0F);
    CHECK(layout.panel_y + layout.panel_height <= layout.viewport_height);
    CHECK(layout.track_x >= layout.content_x);
    CHECK(layout.track_x + layout.track_width <=
          layout.content_x + layout.content_width);
    CHECK(layout.track_y >= layout.panel_y);
    CHECK(layout.track_y + layout.track_height <=
          layout.panel_y + layout.panel_height);
    CHECK(layout.quote_y >= layout.panel_y);
    CHECK(layout.author_y + layout.quote_pixel_size * 7.0F <=
          layout.panel_y + layout.panel_height);
    CHECK(layout.title_y >= 0.0F);
    CHECK(layout.detail_y < layout.horizon_y);
  }
}

TEST_CASE("renderer world resource reset progress handles initial empty and "
          "incremental states") {
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

TEST_CASE(
    "visual pipeline contract keeps the legacy image as the renderer default") {
  constexpr RendererOptions default_options{};
  constexpr RendererFrameStats default_stats{};

  CHECK(default_options.visual_pipeline == VisualPipeline::LegacyVoxel);
  CHECK(default_stats.visual_pipeline == VisualPipeline::LegacyVoxel);
  CHECK(visual_pipeline_name(VisualPipeline::LegacyVoxel) ==
        std::string_view{"legacy"});
  CHECK(visual_pipeline_name(VisualPipeline::ModernStylized) ==
        std::string_view{"modern"});
  CHECK_FALSE(is_modern_visual_pipeline(VisualPipeline::LegacyVoxel));
  CHECK(is_modern_visual_pipeline(VisualPipeline::ModernStylized));
  CHECK(parse_visual_pipeline("legacy") == VisualPipeline::LegacyVoxel);
  CHECK(parse_visual_pipeline("modern") == VisualPipeline::ModernStylized);
  CHECK_FALSE(parse_visual_pipeline("").has_value());
  CHECK_FALSE(parse_visual_pipeline("stylized").has_value());

  CHECK(visual_pipeline_post_contrast(VisualPipeline::LegacyVoxel, 1.12F) ==
        doctest::Approx(1.12F));
  CHECK(visual_pipeline_post_contrast(VisualPipeline::ModernStylized, 1.12F) ==
        doctest::Approx(1.04F));
  CHECK(visual_pipeline_glow_threshold(VisualPipeline::LegacyVoxel, 0.78F) ==
        doctest::Approx(0.78F));
  CHECK(visual_pipeline_glow_threshold(VisualPipeline::ModernStylized, 0.78F) ==
        doctest::Approx(0.90F));
  CHECK(visual_pipeline_glow_threshold(VisualPipeline::ModernStylized, 0.96F) ==
        doctest::Approx(1.0F));
  CHECK(visual_pipeline_glow_strength(VisualPipeline::LegacyVoxel, 0.20F) ==
        doctest::Approx(0.20F));
  CHECK(visual_pipeline_glow_strength(VisualPipeline::ModernStylized, 0.20F) ==
        doctest::Approx(0.11F));

  auto modern_options = default_options;
  modern_options.visual_pipeline = VisualPipeline::ModernStylized;
  CHECK(modern_options != default_options);
}

TEST_CASE("modern HUD rounded geometry stays finite and bounded") {
  const auto compact = modern_hud_rounded_rect_metrics(48.0F, 48.0F, 9.0F);
  CHECK(compact.radius == doctest::Approx(9.0F));
  CHECK(compact.corner_segments >= 4);
  CHECK(compact.corner_segments <= 10);
  CHECK(compact.vertex_count >= 54U);
  CHECK(compact.vertex_count <= 138U);

  const auto ultrawide =
      modern_hud_rounded_rect_metrics(3840.0F, 120.0F, 64.0F);
  CHECK(ultrawide.radius == doctest::Approx(60.0F));
  CHECK(ultrawide.corner_segments == 10);
  CHECK(ultrawide.vertex_count <= 138U);

  const auto clamped_radius = modern_hud_panel_radius(40.0F, 18.0F, 12.0F);
  CHECK(clamped_radius > 0.0F);
  CHECK(clamped_radius <= 9.0F);

  CHECK(modern_hud_rounded_rect_metrics(std::numeric_limits<float>::quiet_NaN(),
                                        20.0F, 4.0F)
            .vertex_count == 0U);
  CHECK(modern_hud_rounded_rect_metrics(20.0F, -1.0F, 4.0F).corner_segments ==
        0);
}

TEST_CASE("renderer ship mesh cache remains not ready until revision renderer "
          "and gpu agree") {
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

TEST_CASE("renderer ship mesh cache retains both modern lod variants") {
  RendererShipMeshCacheState cache;
  constexpr std::uint64_t geometry_revision = 0xA6E11EULL;
  constexpr std::size_t part_count = 3'291U;
  constexpr std::uint8_t near_variant = 1U;
  constexpr std::uint8_t far_variant = 2U;

  CHECK(stylized_ship_lod_index(StylizedShipLod::Near) !=
        stylized_ship_lod_index(StylizedShipLod::Far));
  CHECK(stylized_ship_lod_index(StylizedShipLod::Near) < kStylizedShipLodCount);
  CHECK(stylized_ship_lod_index(StylizedShipLod::Far) < kStylizedShipLodCount);

  cache.remember(geometry_revision, part_count, near_variant);
  CHECK(cache.matches(geometry_revision, part_count, near_variant));
  CHECK_FALSE(cache.matches(geometry_revision, part_count, far_variant));

  cache.remember(geometry_revision, part_count, far_variant);
  CHECK(cache.ready(geometry_revision, part_count, true, true, near_variant));
  CHECK(cache.ready(geometry_revision, part_count, true, true, far_variant));

  // Je simule une régénération Far : la résidence Near doit rester intacte.
  cache.remember(geometry_revision + 1U, part_count, far_variant);
  CHECK(cache.matches(geometry_revision, part_count, near_variant));
  CHECK_FALSE(cache.matches(geometry_revision, part_count, far_variant));

  cache.reset();
  CHECK_FALSE(cache.matches(geometry_revision, part_count, near_variant));
  CHECK_FALSE(cache.matches(geometry_revision + 1U, part_count, far_variant));
}

static_assert(
    std::is_same_v<decltype(std::declval<Renderer &>().prepare_ship_mesh(
                       std::declval<const ShipRenderState &>())),
                   bool>);
static_assert(std::is_same_v<
              decltype(std::declval<Renderer &>().process_world_resource_reset(
                  std::size_t{}, 0.0)),
              bool>);

using RenderFrameWithCrew = void (Renderer::*)(
    World &, const PlayerController &, const PlayerMusketView &,
    const HotbarState &, const InventoryMenuState &, const DeathScreenState &,
    const PauseMenuState &, const MainMenuState &, const SaveSlotMenuState &,
    const OptionsMenuState &, const ConfirmDialogState &,
    std::span<const CreatureRenderInstance>,
    std::span<const CrewRenderInstance>,
    std::span<const OldGuardRenderInstance>,
    std::span<const OldGuardMuzzleFlashInstance>,
    std::span<const OldGuardSmokeInstance>,
    std::span<const OldGuardMuzzleFlashInstance>,
    std::span<const OldGuardSmokeInstance>,
    std::span<const ItemDropRenderInstance>, const ShipRenderState &,
    const PlayerProgressionState &, bool,
    const BackroomsFlashlightHudView &,
    const GameplayHudAnnouncementView &, const MaritimeHudView &,
    const CommandConsoleView &,
    const EnvironmentState &, int, int);

static_assert(std::is_same_v<decltype(static_cast<RenderFrameWithCrew>(
                                 &Renderer::render_frame)),
                             RenderFrameWithCrew>);

TEST_CASE("crew renderer owns six additional long range slots and packed local "
          "lighting") {
  CHECK(kCrewVisualRenderCapacity == 6U);
  CHECK(kCrewVisualPartBudget == 80U);
  CHECK(kCrewVisualDrawDistance == doctest::Approx(96.0F));

  // Je garde ces quatre flottants contigus car ils partagent l'attribut GPU 15.
  CHECK(offsetof(CreaturePartInstance, sky_light) ==
        offsetof(CreaturePartInstance, emissive_strength) + sizeof(float));
  CHECK(offsetof(CreaturePartInstance, block_light) ==
        offsetof(CreaturePartInstance, sky_light) + sizeof(float));
  CHECK(offsetof(CreaturePartInstance, precipitation_exposure) ==
        offsetof(CreaturePartInstance, block_light) + sizeof(float));
}

TEST_CASE(
    "creature shader preserves each uniform color under local ship lighting") {
  const auto renderer_path =
      std::filesystem::path{
          __FILE__,
      }
          .parent_path()
          .parent_path() /
      "src" / "render" / "Renderer.cpp";
  std::ifstream input{
      renderer_path,
      std::ios::binary,
  };
  REQUIRE(input.good());
  const std::string source{
      std::istreambuf_iterator<char>{
          input,
      },
      std::istreambuf_iterator<char>{},
  };
  const auto fragment_begin =
      source.find("creature_fragment_shader_part1");
  REQUIRE(fragment_begin != std::string::npos);
  const auto fragment_end =
      source.find(
          "const std::string creature_fragment_shader",
          fragment_begin);
  REQUIRE(fragment_end != std::string::npos);
  const auto fragment =
      std::string_view{
          source,
      }
          .substr(fragment_begin, fragment_end - fragment_begin);

  CHECK(fragment.find("vec3 modern_local_albedo") != std::string_view::npos);
  CHECK(fragment.find("mix(albedo, sqrt(max(albedo, vec3(0.0))), 0.32)") !=
        std::string_view::npos);
  CHECK(fragment.find("u_modern_pipeline != 0") != std::string_view::npos);
  CHECK(fragment.find("uniform vec3 u_local_light_radiance;") !=
        std::string_view::npos);
  CHECK(fragment.find("u_local_light_radiance *") != std::string_view::npos);
  CHECK(fragment.find("if (instance_block_light > 0.0001)") !=
        std::string_view::npos);
  CHECK(fragment.find("local_light_facing *") != std::string_view::npos);
  CHECK(fragment.find("local_light_albedo;") != std::string_view::npos);
}

TEST_CASE("sky shader avoids reserved GLSL noise identifiers") {
  const std::string_view shader_source{kSkyFragmentShaderSource};
  const std::string_view vertex_source{kSkyVertexShaderSource};

  CHECK(shader_source.find("float noise3(") == std::string_view::npos);
  CHECK(shader_source.find("float value_noise3(") != std::string_view::npos);
  CHECK(shader_source.find("value_noise3(p)") != std::string_view::npos);
  CHECK(shader_source.find("u_overcast_intensity") != std::string_view::npos);
  CHECK(shader_source.find("u_precipitation_intensity") !=
        std::string_view::npos);
  CHECK(shader_source.find("uniform float u_violent_storm_intensity") !=
        std::string_view::npos);
  CHECK(shader_source.find("uniform float u_lightning_intensity") !=
        std::string_view::npos);
  CHECK(shader_source.find("uniform float u_lightning_bolt_intensity") !=
        std::string_view::npos);
  CHECK(shader_source.find("uniform vec3 u_lightning_direction") !=
        std::string_view::npos);
  CHECK(shader_source.find("uniform float u_lightning_shape_seed") !=
        std::string_view::npos);
  CHECK(shader_source.find("float lightning_bolt_mask(vec3 view_direction)") !=
        std::string_view::npos);
  CHECK(shader_source.find("u_lightning_direction.xz") !=
        std::string_view::npos);
  CHECK(shader_source.find("u_lightning_direction.y") !=
        std::string_view::npos);
  CHECK(shader_source.find("lightning_bolt_mask(direction)") !=
        std::string_view::npos);
  CHECK(shader_source.find("volumetric_cloud_minimum") !=
        std::string_view::npos);
  CHECK(shader_source.find(
            "max(cloud_factor, overcast_factor) > volumetric_cloud_minimum") !=
        std::string_view::npos);
  CHECK(shader_source.find("star_spawn") != std::string_view::npos);
  CHECK(shader_source.find("star_weather_visibility") !=
        std::string_view::npos);
  CHECK(shader_source.find("uniform int u_cloud_steps") !=
        std::string_view::npos);
  CHECK(shader_source.find("uniform float u_cloud_detail") !=
        std::string_view::npos);
  CHECK(shader_source.find("step >= cloud_steps") != std::string_view::npos);
  CHECK(shader_source.find("if (layer <= 0.0)") != std::string_view::npos);
  CHECK(shader_source.find("smoothstep(0.12, 0.24, disk_visibility)") !=
        std::string_view::npos);
  CHECK(shader_source.find("directional_cloud_light > 0.001") !=
        std::string_view::npos);
  CHECK(shader_source.find("if (shape + weather_bias <= coverage)") !=
        std::string_view::npos);
  CHECK(vertex_source.find("vec4(clip, 1.0, 1.0)") != std::string_view::npos);
}

TEST_CASE(
    "maritime sky seals the distant ocean with an analytic fogged plane") {
  const std::string_view shader_source{kSkyFragmentShaderSource};

  CHECK(shader_source.find("uniform vec3 u_maritime_camera_position") !=
        std::string_view::npos);
  CHECK(shader_source.find("uniform float u_maritime_sea_level") !=
        std::string_view::npos);
  CHECK(shader_source.find("uniform int u_maritime_submersion_active") !=
        std::string_view::npos);
  CHECK(shader_source.find("uniform vec2 u_maritime_far_fog_range") !=
        std::string_view::npos);
  CHECK(shader_source.find("float plane_distance") != std::string_view::npos);
  CHECK(shader_source.find("u_maritime_camera_position.y") !=
        std::string_view::npos);
  CHECK(shader_source.find("bool maritime_underwater_camera") !=
        std::string_view::npos);
  CHECK(shader_source.find("u_maritime_submersion_active != 0") !=
        std::string_view::npos);
  CHECK(shader_source.find("vec3 deep_water") != std::string_view::npos);
  CHECK(shader_source.find("float terminal_fog") != std::string_view::npos);
  const auto terminal_mix_color = shader_source.rfind("u_distant_fog_color");
  REQUIRE(terminal_mix_color != std::string_view::npos);
  const auto terminal_mix_factor =
      shader_source.find("terminal_fog", terminal_mix_color);
  REQUIRE(terminal_mix_factor != std::string_view::npos);
  CHECK(terminal_mix_factor - terminal_mix_color < 96U);
}

TEST_CASE("maritime detail stays opaque above a fogged proxy underlay") {
  const auto terrain_shader = kModernTerrainFragmentShaderSource;
  CHECK(terrain_shader.find("uniform int u_maritime_horizon_enabled;") !=
        std::string_view::npos);
  CHECK(
      terrain_shader.find("uniform vec2 u_maritime_detail_transition_range;") !=
      std::string_view::npos);
  CHECK(terrain_shader.find("uniform float u_maritime_sea_level;") !=
        std::string_view::npos);
  CHECK(terrain_shader.find("uniform int u_maritime_submersion_active;") !=
        std::string_view::npos);
  CHECK(terrain_shader.find("float maritime_horizon_haze = 0.0;") !=
        std::string_view::npos);
  CHECK(terrain_shader.find("v_world_position.xz -\n"
                            "                u_camera_position.xz") !=
        std::string_view::npos);
  CHECK(terrain_shader.find("if (proxy_coverage > dither_threshold)") ==
        std::string_view::npos);
  CHECK(terrain_shader.find("bool underwater_volume") !=
        std::string_view::npos);
  CHECK(terrain_shader.find("u_maritime_submersion_active != 0") !=
        std::string_view::npos);
  CHECK(terrain_shader.find("float underwater_terminal_fog") !=
        std::string_view::npos);

  const auto renderer_path =
      std::filesystem::path{
          __FILE__,
      }
          .parent_path()
          .parent_path() /
      "src" / "render" / "Renderer.cpp";
  std::ifstream input(renderer_path, std::ios::binary);
  REQUIRE(input.is_open());
  const std::string renderer_source{
      std::istreambuf_iterator<char>{
          input,
      },
      std::istreambuf_iterator<char>{},
  };
  CHECK(renderer_source.find("fog_distance =\n"
                             "            distance(") != std::string::npos);
  CHECK(renderer_source.find("maritime_plane_distance)\n"
                             "                : 1.0;") != std::string::npos);
  CHECK(renderer_source.find("sea_horizon_uniforms_.sea_level,\n"
                             "        static_cast<float>(\n"
                             "            kSeaLevel + 1));") !=
        std::string::npos);
  CHECK(renderer_source.find("ordered_transition_threshold(\n"
                             "                gl_FragCoord.xy)") !=
        std::string::npos);
  CHECK(renderer_source.find("v_world_position.y <\n"
                             "            u_sea_level - 0.25") !=
        std::string::npos);
  CHECK(renderer_source.find("if (submerged) {\n"
                             "        discard;") != std::string::npos);
  CHECK(renderer_source.find("if (u_camera_position.y <\n"
                             "        u_sea_level) {\n"
                             "        discard;") != std::string::npos);
  CHECK(renderer_source.find("underwater_camera") == std::string::npos);
  const auto legacy_underwater_guard =
      renderer_source.find("options_.visual_pipeline ==\n"
                           "                VisualPipeline::LegacyVoxel");
  REQUIRE(legacy_underwater_guard != std::string::npos);
  const auto legacy_overlay =
      renderer_source.find("const auto overlay_edge", legacy_underwater_guard);
  REQUIRE(legacy_overlay != std::string::npos);
  CHECK(legacy_overlay - legacy_underwater_guard < 256U);
  CHECK(renderer_source.find("u_camera_position.y >=\n"
                             "            u_maritime_sea_level") !=
        std::string::npos);
  CHECK(renderer_source.find("u_maritime_far_fog_range") != std::string::npos);
  CHECK(renderer_source.find("uniform float u_maritime_sea_level;") !=
        std::string::npos);
  CHECK(renderer_source.find("float maritime_plane_distance") !=
        std::string::npos);
  CHECK(renderer_source.find("-maritime_plane_distance *\n"
                             "                maritime_plane_distance") !=
        std::string::npos);
  CHECK(renderer_source.find("u_projection_far_distance") != std::string::npos);
  CHECK(renderer_source.find("? kSeaHorizonProjectionFarPlane") !=
        std::string::npos);
  CHECK(renderer_source.find("const float far_plane = 320.0;") ==
        std::string::npos);
}

TEST_CASE(
    "Backrooms rendering seals every GPU streaming frontier before display") {
  const auto terrain_shader = kModernTerrainFragmentShaderSource;
  const auto occurrence_count =
      [](std::string_view source,
         std::string_view needle) noexcept {
        auto count = std::size_t {0U};
        auto position = std::size_t {0U};
        while ((position = source.find(needle, position)) !=
               std::string_view::npos) {
          ++count;
          position += needle.size();
        }
        return count;
      };
  CHECK(terrain_shader.find("uniform vec2 u_interior_fog_range;") !=
        std::string_view::npos);
  CHECK(terrain_shader.find("uniform int u_enclosed_interior;") !=
        std::string_view::npos);
  CHECK(
      terrain_shader.find(
          "uniform float u_backrooms_flashlight_intensity;") !=
      std::string_view::npos);
  CHECK(
      terrain_shader.find(
          "uniform int u_backrooms_flicker_count;") !=
      std::string_view::npos);
  CHECK(
      terrain_shader.find(
          "uniform vec4 u_backrooms_flicker_lights[6];") !=
      std::string_view::npos);
  CHECK(
      terrain_shader.find(
          "vec2 backrooms_flicker_scales") !=
      std::string_view::npos);
  CHECK(
      terrain_shader.find(
          "saturate(v_block_light) *\n"
          "        backrooms_flicker.x") !=
      std::string_view::npos);
  CHECK(
      terrain_shader.find(
          "backrooms_flicker.y,\n"
          "            enclosed_interior") !=
      std::string_view::npos);
  CHECK(
      terrain_shader.find(
          "float backrooms_flashlight_irradiance") !=
      std::string_view::npos);
  CHECK(
      terrain_shader.find(
          "float backrooms_darkness_visibility") !=
      std::string_view::npos);
  CHECK(
      occurrence_count(
          terrain_shader,
          "backrooms_darkness_visibility(") ==
      2U);
  CHECK(
      kBackroomsDarknessBlockLightFullVisibilityThreshold ==
      doctest::Approx(0.62F));
  CHECK(
      kBackroomsDarknessFlashlightFullVisibilityThreshold ==
      doctest::Approx(0.18F));
  CHECK(
      terrain_shader.find(
          "0.000,\n"
          "            0.620") !=
      std::string_view::npos);
  CHECK(
      terrain_shader.find(
          "0.000,\n"
          "            0.180") !=
      std::string_view::npos);
  CHECK(
      terrain_shader.find(
          "(1.0 - fixture_visibility) *\n"
          "            (1.0 - flashlight_visibility)") !=
      std::string_view::npos);
  CHECK(
      terrain_shader.find(
          "(isnan(local_light) ||\n"
          "         isinf(local_light))") !=
      std::string_view::npos);
  CHECK(
      terrain_shader.find(
          "(isnan(flashlight_energy) ||\n"
          "         isinf(flashlight_energy))") !=
      std::string_view::npos);
  CHECK(
      terrain_shader.find(
          "backrooms_darkness_visibility(\n"
          "            local_light,\n"
          "            flashlight_energy)") !=
      std::string_view::npos);
  CHECK(
      terrain_shader.find(
          "const float outer_cone_cosine = 0.913545") !=
      std::string_view::npos);
  CHECK(
      terrain_shader.find(
          "const float penumbra_cone_cosine = 0.887011") !=
      std::string_view::npos);
  CHECK(
      terrain_shader.find(
          "const float hotspot_cosine = 0.994522") !=
      std::string_view::npos);
  CHECK(
      terrain_shader.find(
          "vec3(1.00, 0.92, 0.76)") !=
      std::string_view::npos);
  CHECK(terrain_shader.find("u_interior_fog_range.x >= 0.0") !=
        std::string_view::npos);
  CHECK(terrain_shader.find("float interior_terminal_fog") !=
        std::string_view::npos);
  CHECK(terrain_shader.find(": 1.0;") != std::string_view::npos);
  CHECK(
      terrain_shader.find(
          "v_primary_block == 50u || v_primary_block == 52u") !=
      std::string_view::npos);
  CHECK(
      terrain_shader.find(
          "v_primary_block == 49u || v_primary_block == 51u") ==
      std::string_view::npos);
  CHECK(
      terrain_shader.find("if (v_primary_block == 50u)") !=
      std::string_view::npos);
  CHECK(
      terrain_shader.find("else if (v_primary_block == 52u)") !=
      std::string_view::npos);
  const auto emission_override_begin =
      terrain_shader.find("float emission =");
  const auto emission_override_end =
      terrain_shader.find("vec3 view_direction", emission_override_begin);
  REQUIRE(emission_override_begin != std::string_view::npos);
  REQUIRE(emission_override_end != std::string_view::npos);
  const auto emission_override =
      terrain_shader.substr(
          emission_override_begin,
          emission_override_end - emission_override_begin);
  CHECK(emission_override.find("49u") == std::string_view::npos);
  CHECK(emission_override.find("51u") == std::string_view::npos);

  const auto emission_color_begin =
      terrain_shader.find("vec3 emission_color");
  const auto emission_color_end =
      terrain_shader.find(
          "color +=\n"
          "        emission_color",
          emission_color_begin);
  REQUIRE(emission_color_begin != std::string_view::npos);
  REQUIRE(emission_color_end != std::string_view::npos);
  const auto emission_color =
      terrain_shader.substr(
          emission_color_begin,
          emission_color_end - emission_color_begin);
  CHECK(emission_color.find("49u") == std::string_view::npos);
  CHECK(emission_color.find("51u") == std::string_view::npos);
  CHECK(
      terrain_shader.find("float softened_local_light") !=
      std::string_view::npos);
  CHECK(
      terrain_shader.find("float smooth_local_light") !=
      std::string_view::npos);
  CHECK(
      terrain_shader.find(
          "local_light * local_light * (3.0 - 2.0 * local_light)") !=
      std::string_view::npos);
  CHECK(
      terrain_shader.find(
          "smoothstep(0.52, 0.82, primary_source_peak)") !=
      std::string_view::npos);
  CHECK(
      terrain_shader.find("(0.045 + 0.055 * softened_local_light)") ==
      std::string_view::npos);
  CHECK(
      terrain_shader.find("vec3 interior_bounce") !=
      std::string_view::npos);

  const auto renderer_path =
      std::filesystem::path{__FILE__}.parent_path().parent_path() / "src" /
      "render" / "Renderer.cpp";
  std::ifstream input(renderer_path, std::ios::binary);
  REQUIRE(input.is_open());
  const std::string renderer_source{
      std::istreambuf_iterator<char>{input},
      std::istreambuf_iterator<char>{},
  };
  CHECK(renderer_source.find("uniform vec2 u_interior_fog_range;") !=
        std::string::npos);
  CHECK(renderer_source.find("uniform int u_enclosed_interior;") !=
        std::string::npos);
  CHECK(renderer_source.find(
            "backrooms_contiguous_chunk_coverage_distance(") !=
        std::string::npos);
  CHECK(renderer_source.find("backrooms_terminal_fog_range(") !=
        std::string::npos);
  CHECK(renderer_source.find("backrooms_advance_terminal_fog_range(") !=
        std::string::npos);
  const auto coverage_scan_begin =
      renderer_source.find("auto backrooms_fog_target =");
  const auto coverage_scan_end =
      renderer_source.find(
          "auto backrooms_fog_range =",
          coverage_scan_begin);
  REQUIRE(coverage_scan_begin != std::string::npos);
  REQUIRE(coverage_scan_end != std::string::npos);
  const auto coverage_scan =
      std::string_view(renderer_source).substr(
          coverage_scan_begin,
          coverage_scan_end - coverage_scan_begin);
  CHECK(coverage_scan.find("mesh->second.revision == 0U") !=
        std::string_view::npos);
  CHECK(coverage_scan.find("chunk->is_dirty()") ==
        std::string_view::npos);
  CHECK(coverage_scan.find("chunk->is_lighting_dirty()") ==
        std::string_view::npos);
  CHECK(coverage_scan.find("cpu_revision") ==
        std::string_view::npos);
  CHECK(renderer_source.find("kBackroomsStreamingSafetyChunks") !=
        std::string::npos);
  CHECK(renderer_source.find(
            "glUniform2f(world_uniforms_.interior_fog_range") !=
        std::string::npos);
  CHECK(renderer_source.find(
            "glUniform2f(modern_terrain_uniforms_.interior_fog_range") !=
        std::string::npos);
  CHECK(renderer_source.find(
            "glUniform1i(world_uniforms_.enclosed_interior") !=
        std::string::npos);
  CHECK(renderer_source.find(
            "glUniform1i(modern_terrain_uniforms_.enclosed_interior") !=
        std::string::npos);
  CHECK(renderer_source.find(
            "glUniform1i(uniforms.enclosed_interior") !=
        std::string::npos);
  CHECK(renderer_source.find(
            "\"u_enclosed_interior\"") !=
        std::string::npos);
  CHECK(renderer_source.find(
            "const std::array<GLint, 44> "
            "modern_terrain_uniform_locations") !=
        std::string::npos);
  CHECK(renderer_source.find(
            "const std::array<GLint, 41> "
            "modern_architecture_uniform_locations") !=
        std::string::npos);
  CHECK(renderer_source.find(
            "modern_terrain_uniforms_.block_light_color,\n"
            "      modern_terrain_uniforms_.enclosed_interior,\n"
            "      modern_terrain_uniforms_.backrooms_flicker_count,\n"
            "      modern_terrain_uniforms_.backrooms_flicker_lights,\n"
            "      modern_terrain_uniforms_.backrooms_flashlight_intensity") !=
        std::string::npos);
  CHECK(renderer_source.find(
            "modern_architecture_uniforms_.block_light_color,\n"
            "      modern_architecture_uniforms_.enclosed_interior,\n"
            "      modern_architecture_uniforms_.backrooms_flicker_count,\n"
            "      modern_architecture_uniforms_.backrooms_flicker_lights,\n"
            "      modern_architecture_uniforms_.backrooms_flashlight_intensity") !=
        std::string::npos);
  CHECK(renderer_source.find(
            "glUniform1f(uniforms.backrooms_flashlight_intensity") !=
        std::string::npos);
  CHECK(renderer_source.find(
            "item_drop_uniforms_.backrooms_flicker_count") !=
        std::string::npos);
  CHECK(renderer_source.find(
            "item_drop_uniforms_.backrooms_flicker_lights") !=
        std::string::npos);
  CHECK(renderer_source.find(
            "item_drop_uniforms_.backrooms_flashlight_intensity") !=
        std::string::npos);
  CHECK(renderer_source.find(
            "const std::array<GLint, 6>\n"
            "      item_drop_backrooms_uniform_locations") !=
        std::string::npos);
  CHECK(renderer_source.find(
            "glUniform1i(item_drop_uniforms_.enclosed_interior") !=
        std::string::npos);
  CHECK(renderer_source.find(
            "glUniform2f(\n"
            "      item_drop_uniforms_.interior_fog_range") !=
        std::string::npos);
  CHECK(renderer_source.find(
            "modern_terrain_uniforms_.backrooms_flashlight_intensity,\n"
            "        backrooms_flashlight_strength") !=
        std::string::npos);
  CHECK(renderer_source.find(
            "\"u_backrooms_flashlight_intensity\"") !=
        std::string::npos);
  CHECK(renderer_source.find(
            "const std::array<GLint, 7> "
            "world_interior_lighting_uniform_locations") !=
        std::string::npos);
  CHECK(renderer_source.find(
            "world_uniforms_.enclosed_interior,\n"
            "      world_uniforms_.backrooms_flicker_count,\n"
            "      world_uniforms_.backrooms_flicker_lights,\n"
            "      world_uniforms_.backrooms_flashlight_intensity,\n"
            "      world_uniforms_.interior_fog_range") !=
        std::string::npos);
  CHECK(renderer_source.find("float softened_block_light") !=
        std::string::npos);
  CHECK(renderer_source.find("float smooth_block_light") !=
        std::string::npos);
  CHECK(renderer_source.find("float deepened_block_light") !=
        std::string::npos);
  CHECK(renderer_source.find(
            "block_light * block_light * (3.0 - 2.0 * block_light)") !=
        std::string::npos);
  CHECK(renderer_source.find("vec3 legacy_emission") !=
        std::string::npos);
  CHECK(renderer_source.find("vec3 backrooms_emission") !=
        std::string::npos);
  CHECK(renderer_source.find("u_backrooms_flicker_count") !=
        std::string::npos);
  CHECK(renderer_source.find("u_backrooms_flicker_lights[6]") !=
        std::string::npos);
  CHECK(renderer_source.find("float backrooms_light_scale") !=
        std::string::npos);
  CHECK(renderer_source.find("float backrooms_source_scale") !=
        std::string::npos);
  CHECK(renderer_source.find("float interior_shadow_reveal") !=
        std::string::npos);
  CHECK(renderer_source.find(
            "uniform float u_backrooms_flashlight_intensity;") !=
        std::string::npos);
  CHECK(renderer_source.find(
            "float backrooms_flashlight_irradiance") !=
        std::string::npos);
  CHECK(renderer_source.find(
            "float backrooms_darkness_visibility") !=
        std::string::npos);
  CHECK(renderer_source.find(
            "float backrooms_flicker_light_scale") !=
        std::string::npos);
  CHECK(renderer_source.find(
            "backrooms_flicker_light_scale(\n"
            "            v_world_position)") !=
        std::string::npos);
  CHECK(renderer_source.find(
            "0.000,\n"
            "            0.620") !=
        std::string::npos);
  CHECK(renderer_source.find(
            "0.000,\n"
            "            0.180") !=
        std::string::npos);
  CHECK(renderer_source.find(
            "backrooms_darkness_visibility(\n"
            "            block_light,\n"
            "            flashlight_energy)") !=
        std::string::npos);
  CHECK(renderer_source.find(
            "backrooms_darkness_visibility(\n"
            "            instance_block_light,\n"
            "            flashlight_energy)") !=
        std::string::npos);
  const auto legacy_darkness_begin =
      renderer_source.find(
          "float backrooms_darkness_visibility");
  const auto creature_darkness_begin =
      renderer_source.find(
          "float backrooms_darkness_visibility",
          legacy_darkness_begin + 1U);
  REQUIRE(
      legacy_darkness_begin !=
      std::string::npos);
  REQUIRE(
      creature_darkness_begin !=
      std::string::npos);
  CHECK(
      renderer_source.find(
          "float backrooms_darkness_visibility",
          creature_darkness_begin + 1U) ==
      std::string::npos);
  const auto legacy_darkness_source =
      std::string_view(renderer_source).substr(
          legacy_darkness_begin,
          creature_darkness_begin -
              legacy_darkness_begin);
  const auto creature_darkness_source =
      std::string_view(renderer_source).substr(
          creature_darkness_begin);
  for (const auto shader_source :
       {legacy_darkness_source,
        creature_darkness_source}) {
    CAPTURE(shader_source.size());
    CHECK(
        shader_source.find(
            "if (u_enclosed_interior == 0)") !=
        std::string_view::npos);
    CHECK(
        shader_source.find(
            "(isnan(local_light) ||\n"
            "         isinf(local_light))") !=
        std::string_view::npos);
    CHECK(
        shader_source.find(
            "(isnan(flashlight_energy) ||\n"
            "         isinf(flashlight_energy))") !=
        std::string_view::npos);
    CHECK(
        shader_source.find(
            "0.000,\n"
            "            0.620") !=
        std::string_view::npos);
    CHECK(
        shader_source.find(
            "0.000,\n"
            "            0.180") !=
        std::string_view::npos);
    CHECK(
        shader_source.find(
            "(1.0 - fixture_visibility) *\n"
            "            (1.0 - flashlight_visibility)") !=
        std::string_view::npos);
  }
  CHECK(
      legacy_darkness_source.find(
          "final_color *=\n"
          "        backrooms_darkness_visibility(\n"
          "            block_light,\n"
          "            flashlight_energy);") !=
      std::string_view::npos);
  CHECK(
      creature_darkness_source.find(
          "final_color *=\n"
          "        backrooms_darkness_visibility(\n"
          "            instance_block_light,\n"
          "            flashlight_energy);") !=
      std::string_view::npos);
  CHECK(renderer_source.find(
            "const float outer_cone_cosine = 0.913545") !=
        std::string::npos);
  CHECK(renderer_source.find(
            "const float penumbra_cone_cosine = 0.887011") !=
        std::string::npos);
  CHECK(renderer_source.find(
            "const float hotspot_cosine = 0.994522") !=
        std::string::npos);
  CHECK(renderer_source.find(
            "vec3(1.00, 0.92, 0.76)") !=
        std::string::npos);
  CHECK(renderer_source.find(
            "glUniform1f(\n"
            "      world_uniforms_.backrooms_flashlight_intensity") !=
        std::string::npos);
  CHECK(renderer_source.find(
            "mix(0.72, 1.04, clamp(v_face_shade, 0.0, 1.0))") !=
        std::string::npos);
  CHECK(renderer_source.find(
            "mix(0.60, 1.00, clamp(v_ao, 0.0, 1.0))") !=
        std::string::npos);
  CHECK(renderer_source.find(
            "mix(0.62, 0.88, hemisphere)") !=
        std::string::npos);
  CHECK(renderer_source.find(
            "mix(0.36, 1.0, interior_shadow_reveal)") !=
        std::string::npos);
  const auto darkest_meshed_face =
      (0.72F + (1.04F - 0.72F) * 0.65F) *
      (0.60F + (1.00F - 0.60F) * 0.55F);
  CHECK(darkest_meshed_face == doctest::Approx(0.76096F));
  CHECK(darkest_meshed_face > 0.70F);
  CHECK(darkest_meshed_face < 0.77F);
  CHECK(renderer_source.find(
            "vec3(1.24, 0.68, 0.24) * emissive_mask") !=
        std::string::npos);
  CHECK(renderer_source.find(
            "smoothstep(0.52, 0.82, source_peak)") !=
        std::string::npos);
  CHECK(renderer_source.find(
            "(0.045 + 0.055 * softened_block_light)") ==
        std::string::npos);
  CHECK(renderer_source.find("float interior_haze") !=
        std::string::npos);
  CHECK(renderer_source.find(
            "backrooms_interior\n"
            "          ? glm::vec3 {0.0F}") !=
        std::string::npos);
  CHECK(renderer_source.find(
            "creature_uniforms_.backrooms_flicker_count,\n"
            "      0)") !=
        std::string::npos);
  CHECK(renderer_source.find(
            "creature_uniforms_.interior_fog_range,\n"
            "      -1.0F,\n"
            "      -1.0F)") !=
        std::string::npos);
  CHECK(renderer_source.find(
            "creature_uniforms_.backrooms_flashlight_intensity,\n"
            "      0.0F)") !=
        std::string::npos);
  CHECK(renderer_source.find("if (!backrooms_interior)") !=
        std::string::npos);
}

TEST_CASE(
    "Poolrooms block ids atlas and physical properties stay append only") {
  CHECK(to_block_id(BlockType::PoolroomsTile) == 53U);
  CHECK(to_block_id(BlockType::PoolroomsWetTile) == 54U);
  CHECK(to_block_id(BlockType::PoolroomsDarkTile) == 55U);
  CHECK(to_block_id(BlockType::PoolroomsMetal) == 56U);
  CHECK(to_block_id(BlockType::PoolroomsPlastic) == 57U);
  CHECK(to_block_id(BlockType::PoolroomsLight) == 58U);
  CHECK(to_block_id(BlockType::PoolroomsFailedLight) == 59U);
  CHECK(to_block_id(BlockType::BackroomsDesk) == 60U);
  CHECK(to_block_id(BlockType::BackroomsChair) == 61U);
  CHECK(to_block_id(BlockType::BackroomsPlant) == 62U);
  CHECK(to_block_id(BlockType::PoolroomsFloat) == 63U);
  CHECK(is_known_block_id(63U));
  CHECK_FALSE(is_known_block_id(64U));

  const auto light =
      block_properties(to_block_id(BlockType::PoolroomsLight));
  const auto failed =
      block_properties(to_block_id(BlockType::PoolroomsFailedLight));
  const auto plant =
      block_properties(to_block_id(BlockType::BackroomsPlant));
  const auto pool_float =
      block_properties(to_block_id(BlockType::PoolroomsFloat));
  CHECK(light.emissive_level == 14U);
  CHECK(light.opaque);
  CHECK(light.collidable);
  CHECK(failed.emissive_level == 0U);
  CHECK(plant.mesh_type == BlockMeshType::Cross);
  CHECK_FALSE(plant.collidable);
  CHECK(pool_float.mesh_type == BlockMeshType::Cross);
  CHECK_FALSE(pool_float.collidable);

  CHECK((block_atlas_tile(
             to_block_id(BlockType::PoolroomsTile),
             BlockVisualFace::PositiveY) ==
         BlockAtlasTile{0, kPoolroomsAtlasRow}));
  CHECK((block_atlas_tile(
             to_block_id(BlockType::PoolroomsFloat),
             BlockVisualFace::PositiveX) ==
         BlockAtlasTile{10, kPoolroomsAtlasRow}));
  CHECK(block_visual_material(
            to_block_id(BlockType::PoolroomsMetal)) ==
        BlockVisualMaterial::Metal);
  CHECK(block_visual_material(
            to_block_id(BlockType::PoolroomsLight)) ==
        BlockVisualMaterial::Emissive);

  const auto atlas = build_block_atlas_pixels();
  REQUIRE(atlas.size() ==
          static_cast<std::size_t>(
              kBlockAtlasSize * kBlockAtlasSize * 4));
  const auto tile_grout =
      sample_block_atlas_pixel(
          atlas, 0, kPoolroomsAtlasRow, 0, 0);
  const auto tile_face =
      sample_block_atlas_pixel(
          atlas, 0, kPoolroomsAtlasRow, 2, 2);
  const auto wet_face =
      sample_block_atlas_pixel(
          atlas, 1, kPoolroomsAtlasRow, 2, 2);
  const auto active_light =
      sample_block_atlas_pixel(
          atlas, 5, kPoolroomsAtlasRow, 7, 7);
  const auto float_center =
      sample_block_atlas_pixel(
          atlas, 10, kPoolroomsAtlasRow, 7, 7);
  const auto float_ring =
      sample_block_atlas_pixel(
          atlas, 10, kPoolroomsAtlasRow, 1, 7);
  CHECK(tile_face[0] > tile_grout[0]);
  CHECK(tile_face[1] > tile_grout[1]);
  CHECK(wet_face[2] > wet_face[0]);
  CHECK(active_light[1] >= 250U);
  CHECK(float_center[3] == 0U);
  CHECK(float_ring[3] == 255U);
}

TEST_CASE(
    "Poolrooms environment stays cold enclosed and darker than offices") {
  const auto offices =
      make_backrooms_environment_state(
          19.0F, 481, 12.0F, -7.0F);
  const auto poolrooms =
      make_backrooms_environment_state(
          19.0F, 481, 12.0F, -7.0F, true);

  CHECK(offices.enclosed_interior);
  CHECK_FALSE(offices.poolrooms);
  CHECK(poolrooms.enclosed_interior);
  CHECK(poolrooms.poolrooms);
  CHECK(poolrooms.suppress_gameplay_hud);
  CHECK(poolrooms.daylight_factor == doctest::Approx(0.0F));
  CHECK(poolrooms.night_tint_color == glm::vec3{0.0F});
  CHECK(poolrooms.block_light_color.b >
        poolrooms.block_light_color.r);
  CHECK(poolrooms.ambient_color.g >
        poolrooms.ambient_color.r);
  CHECK(poolrooms.exposure < offices.exposure);
  CHECK(poolrooms.saturation_boost == doctest::Approx(0.96F));
  CHECK(poolrooms.glow_strength > offices.glow_strength);
}

TEST_CASE(
    "Poolrooms PBR terrain keeps ceramic joints wetness and cold emission") {
  const auto shader = kModernTerrainFragmentShaderSource;
  CHECK(shader.find("block_id >= 42u && block_id <= 63u") !=
        std::string_view::npos);
  CHECK(shader.find("vec3 poolrooms_ceramic_color") !=
        std::string_view::npos);
  CHECK(shader.find("poolrooms_face_coordinate() * 2.0") !=
        std::string_view::npos);
  CHECK(shader.find("poolrooms_face_coordinate() * 4.0") ==
        std::string_view::npos);
  CHECK(shader.find("max(fwidth(tile_position)") !=
        std::string_view::npos);
  CHECK(shader.find("0.486 - antialias_width") !=
        std::string_view::npos);
  CHECK(shader.find("0.496 + antialias_width") !=
        std::string_view::npos);
  CHECK(shader.find("float atlas_modulation") !=
        std::string_view::npos);
  CHECK(shader.find("mix(\n                0.985,\n                1.015,") !=
        std::string_view::npos);
  CHECK(shader.find(
            "if (block_id >= 53u && block_id <= 55u) return 0.008") !=
        std::string_view::npos);
  CHECK(shader.find(
            "if (block_id >= 53u && block_id <= 55u) return 45.0") !=
        std::string_view::npos);
  CHECK(shader.find("v_primary_block == 54u ? 1.0 : 0.0") !=
        std::string_view::npos);
  CHECK(shader.find("if (block_id == 54u) return 0.095") !=
        std::string_view::npos);
  CHECK(shader.find("if (block_id == 56u) return 0.86") !=
        std::string_view::npos);
  CHECK(shader.find("else if (v_primary_block == 58u)") !=
        std::string_view::npos);
  CHECK(shader.find("vec3(0.54, 0.98, 1.04)") !=
        std::string_view::npos);
}

TEST_CASE(
    "Poolrooms modern water is calm indoor turquoise and darkness aware") {
  const auto vertex_shader = kModernWaterVertexShaderSource;
  const auto &fragment_shader =
      modern_water_fragment_shader_source();
  CHECK(vertex_shader.find("uniform int u_poolrooms_interior;") !=
        std::string_view::npos);
  CHECK(vertex_shader.find(
            "wave_weight > 0.0 &&\n"
            "        u_poolrooms_interior == 0") !=
        std::string_view::npos);

  for (const auto uniform :
       std::array<std::string_view, 8>{
           "uniform int u_enclosed_interior;",
           "uniform int u_poolrooms_interior;",
           "uniform vec3 u_block_light_color;",
           "uniform vec3 u_camera_forward;",
           "uniform int u_backrooms_flicker_count;",
           "uniform vec4 u_backrooms_flicker_lights[6];",
           "uniform float u_backrooms_flashlight_intensity;",
           "uniform vec2 u_interior_fog_range;",
       }) {
    CAPTURE(uniform);
    CHECK(fragment_shader.find(uniform) != std::string_view::npos);
  }
  CHECK(fragment_shader.find("vec3 poolrooms_volume_color") !=
        std::string_view::npos);
  CHECK(fragment_shader.find("vec3(0.018, 0.520, 0.460)") !=
        std::string_view::npos);
  CHECK(fragment_shader.find("vec2 poolrooms_ripple_gradient") !=
        std::string_view::npos);
  CHECK(fragment_shader.find("poolrooms_ripple_gradient(\n"
                             "                v_world_position.xz)") !=
        std::string_view::npos);
  CHECK(fragment_shader.find("(poolrooms_interior ? 0.12 : 1.0)") !=
        std::string_view::npos);
  CHECK(fragment_shader.find("float refraction_strength") !=
        std::string_view::npos);
  CHECK(fragment_shader.find("mix(0.0085, 0.0022, fresnel)") !=
        std::string_view::npos);
  CHECK(fragment_shader.find(
            "poolrooms_interior\n"
            "            ? exp(") !=
        std::string_view::npos);
  CHECK(fragment_shader.find(
            "-vec3(0.52, 0.110, 0.075)") !=
        std::string_view::npos);
  CHECK(fragment_shader.find("float poolrooms_light_response") !=
        std::string_view::npos);
  CHECK(fragment_shader.find(
            "flashlight_energy * 1.35") !=
        std::string_view::npos);
  CHECK(fragment_shader.find("float poolrooms_shallow_factor") !=
        std::string_view::npos);
  CHECK(fragment_shader.find("float poolrooms_edge_band") !=
        std::string_view::npos);
  CHECK(fragment_shader.find("float poolrooms_surface_fresnel") !=
        std::string_view::npos);
  CHECK(fragment_shader.find("0.310)") !=
        std::string_view::npos);
  CHECK(fragment_shader.find("poolrooms_edge_band * 0.160") !=
        std::string_view::npos);
  CHECK(fragment_shader.find("vec3(0.10, 0.78, 0.72)") !=
        std::string_view::npos);
  CHECK(fragment_shader.find(
            "const float penumbra_cone_cosine = 0.887011") !=
        std::string_view::npos);
  CHECK(fragment_shader.find(
            "if (!poolrooms_interior) {") !=
        std::string_view::npos);
  CHECK(fragment_shader.find(
            "if (foam_detail >= 0.45)") !=
        std::string_view::npos);
  CHECK(fragment_shader.find(
            "poolrooms_interior\n"
            "            ? 0.0\n"
            "            : ship_wake_mask") !=
        std::string_view::npos);
  CHECK(fragment_shader.find("float backrooms_flashlight_irradiance") !=
        std::string_view::npos);
  CHECK(fragment_shader.find("float backrooms_flicker_light_scale") !=
        std::string_view::npos);
  CHECK(fragment_shader.find("float backrooms_darkness_visibility") !=
        std::string_view::npos);
  CHECK(fragment_shader.find(
            "color *=\n"
            "        backrooms_darkness_visibility(") !=
        std::string_view::npos);
}

TEST_CASE(
    "Renderer enables modern water only for sea or explicit Poolrooms") {
  const auto renderer_path =
      std::filesystem::path{__FILE__}.parent_path().parent_path() /
      "src" / "render" / "Renderer.cpp";
  std::ifstream input(renderer_path, std::ios::binary);
  REQUIRE(input.is_open());
  const std::string source{
      std::istreambuf_iterator<char>{input},
      std::istreambuf_iterator<char>{},
  };

  CHECK(source.find(
            "(maritime_horizon_enabled || poolrooms_interior)") !=
        std::string::npos);
  CHECK(source.find(
            "environment.enclosed_interior &&\n"
            "      world.generation_profile() ==") !=
        std::string::npos);
  CHECK(source.find(
            "environment.poolrooms") !=
        std::string::npos);
  CHECK(source.find(
            "modern_water_uniforms_.poolrooms_interior") !=
        std::string::npos);
  CHECK(source.find(
            "modern_water_uniforms_.backrooms_flicker_lights") !=
        std::string::npos);
  CHECK(source.find(
            "modern_water_uniforms_.backrooms_flashlight_intensity") !=
        std::string::npos);
  CHECK(source.find(
            "modern_water_uniforms_.interior_fog_range") !=
        std::string::npos);
  CHECK(source.find(
            "poolrooms_interior\n"
            "                      ? 0") !=
        std::string::npos);
}

TEST_CASE(
    "ship protection GLSL keeps water and weather masks on one contract") {
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
      std::filesystem::path{
          __FILE__,
      }
          .parent_path()
          .parent_path() /
      "src" / "render" / "Renderer.cpp";
  std::ifstream input(renderer_path, std::ios::binary);
  REQUIRE(input.is_open());
  const std::string source{
      std::istreambuf_iterator<char>{
          input,
      },
      std::istreambuf_iterator<char>{},
  };

  CHECK(source.find("rain_streak_layer") == std::string::npos);
  CHECK(source.find("glDrawArraysInstanced") != std::string::npos);
  CHECK(source.find("ScopedPrecipitationGlState") != std::string::npos);
  CHECK(source.find("GL_DEPTH_WRITEMASK") != std::string::npos);
  CHECK(source.find("glBlendFuncSeparate") != std::string::npos);
  CHECK(source.find("precipitation_uniforms_.ship_protection_enabled") !=
        std::string::npos);
  CHECK(source.find("post_process_uniforms_.weather_exposure") !=
        std::string::npos);
  CHECK(source.find("ship_excludes_ocean(v_world_position)") !=
        std::string::npos);
}

TEST_CASE(
    "modern ship shader keeps the exterior lantern and wet deck contracts") {
  const auto renderer_path =
      std::filesystem::path{
          __FILE__,
      }
          .parent_path()
          .parent_path() /
      "src" / "render" / "Renderer.cpp";
  std::ifstream input(renderer_path, std::ios::binary);
  REQUIRE(input.is_open());
  const std::string source{
      std::istreambuf_iterator<char>{
          input,
      },
      std::istreambuf_iterator<char>{},
  };

  CHECK(source.find("in float v_block_light;") != std::string::npos);
  CHECK(source.find("uniform float u_exterior_light_activation;") !=
        std::string::npos);
  CHECK(source.find("modern_ship_uniforms_.exterior_light_activation") !=
        std::string::npos);
  CHECK(source.find("ship_exterior_light_activation(") != std::string::npos);
  CHECK(source.find("material_id != 7\n"
                    "            ? a_block_light *") != std::string::npos);
  CHECK(source.find("float exterior_orientation =") != std::string::npos);
  CHECK(source.find("u_exterior_light_activation *\n"
                    "                  exterior_orientation") !=
        std::string::npos);
  CHECK(source.find("v_block_light") != std::string::npos);
  CHECK(source.find("u_exterior_light_activation") != std::string::npos);
  CHECK(source.find("float exterior_light =\n"
                    "        v_block_light;") != std::string::npos);
  CHECK(source.find("uniform vec3 u_exterior_light_radiance;") !=
        std::string::npos);
  CHECK(source.find("modern_ship_uniforms_.exterior_light_radiance") !=
        std::string::npos);
  CHECK(source.find("exterior_lantern_radiance(") != std::string::npos);
  CHECK(source.find("u_exterior_light_radiance *") != std::string::npos);
  CHECK(source.find("grazing_base *\n"
                    "                grazing_base *\n"
                    "                grazing_base") != std::string::npos);
  CHECK(source.find("if (exposed_deck > 0.0001 &&\n"
                    "            wetness > 0.0001)") != std::string::npos);

  CHECK(source.find("float exposed_deck =") != std::string::npos);
  CHECK(source.find("material_id == 1") != std::string::npos);
  CHECK(source.find("min(\n"
                    "                roughness,\n"
                    "                0.50)") != std::string::npos);
  CHECK(source.find("wetness *\n"
                    "                exposed_deck") != std::string::npos);
  CHECK(source.find("vec3(0.026, 0.036, 0.052)") != std::string::npos);
}

TEST_CASE("renderer quality profiles bound expensive passes predictably") {
  const auto high =
      resolve_renderer_quality_settings(RendererQuality::High, 3840, 2160);
  const auto medium =
      resolve_renderer_quality_settings(RendererQuality::Medium, 1920, 1080);
  const auto low =
      resolve_renderer_quality_settings(RendererQuality::Low, 1280, 720);

  CHECK(high.resolved_quality == RendererQuality::High);
  CHECK(high.cloud_steps == 7);
  CHECK(high.cloud_detail == doctest::Approx(1.0F));
  CHECK(high.glow_downsample == 2);
  CHECK(high.high_precision_hdr);
  CHECK(high.ocean_wave_count == 6);
  CHECK(high.ocean_detail_scale == doctest::Approx(1.0F));
  CHECK(high.water_surface_detail == doctest::Approx(1.0F));

  CHECK(medium.cloud_steps == 4);
  CHECK(medium.cloud_detail < high.cloud_detail);
  CHECK(medium.glow_downsample == 3);
  CHECK_FALSE(medium.high_precision_hdr);
  CHECK(medium.ocean_wave_count == 4);
  CHECK(medium.ocean_detail_scale == doctest::Approx(0.70F));
  CHECK(medium.water_surface_detail == doctest::Approx(0.70F));

  CHECK(low.cloud_steps == 2);
  CHECK(low.cloud_detail == doctest::Approx(0.0F));
  CHECK(low.post_detail_scale == doctest::Approx(0.0F));
  CHECK(low.glow_downsample == 4);
  CHECK(low.ocean_wave_count == 3);
  CHECK(low.ocean_detail_scale == doctest::Approx(0.0F));
  CHECK(low.water_surface_detail == doctest::Approx(0.30F));
  CHECK(high.water_surface_detail > medium.water_surface_detail);
  CHECK(medium.water_surface_detail > low.water_surface_detail);

  CHECK(resolve_renderer_quality_settings(RendererQuality::Dynamic, 1920, 1080)
            .resolved_quality == RendererQuality::High);
  CHECK(resolve_renderer_quality_settings(RendererQuality::Dynamic, 2560, 1440)
            .resolved_quality == RendererQuality::Medium);
  CHECK(resolve_renderer_quality_settings(RendererQuality::Dynamic, 3840, 2160)
            .resolved_quality == RendererQuality::Low);
}

TEST_CASE("gpu timer conversion keeps asynchronous query units explicit") {
  CHECK(gpu_elapsed_nanoseconds_to_milliseconds(0U) == doctest::Approx(0.0));
  CHECK(gpu_elapsed_nanoseconds_to_milliseconds(1'000'000U) ==
        doctest::Approx(1.0));
  CHECK(gpu_elapsed_nanoseconds_to_milliseconds(16'666'667U) ==
        doctest::Approx(16.666667));
}

TEST_CASE("adaptive frame timing follows the slowest valid CPU or GPU sample") {
  const auto cpu_bound =
      resolve_adaptive_frame_time_sample(4.0, true, 24.0, true);
  REQUIRE(cpu_bound.valid);
  CHECK(cpu_bound.frame_time_ms == doctest::Approx(24.0));

  const auto gpu_bound =
      resolve_adaptive_frame_time_sample(31.0, true, 7.0, true);
  REQUIRE(gpu_bound.valid);
  CHECK(gpu_bound.frame_time_ms == doctest::Approx(31.0));

  const auto cpu_only =
      resolve_adaptive_frame_time_sample(0.0, false, 18.5, true);
  REQUIRE(cpu_only.valid);
  CHECK(cpu_only.frame_time_ms == doctest::Approx(18.5));

  const auto invalid = resolve_adaptive_frame_time_sample(
      std::numeric_limits<double>::quiet_NaN(), true, -1.0, true);
  CHECK_FALSE(invalid.valid);
}

TEST_CASE("adaptive stream radius sheds world work progressively") {
  CHECK(resolve_adaptive_stream_radius(5, RendererQuality::High) == 5);
  CHECK(resolve_adaptive_stream_radius(5, RendererQuality::Medium) == 4);
  CHECK(resolve_adaptive_stream_radius(5, RendererQuality::Low) == 3);
  CHECK(resolve_adaptive_stream_radius(1, RendererQuality::Low) == 0);
  CHECK(resolve_adaptive_stream_radius(-4, RendererQuality::Medium) == 0);
}

TEST_CASE("dynamic renderer quality downgrades quickly and upgrades with "
          "hysteresis") {
  RendererAdaptiveQualityController controller;
  CHECK(controller.settings(RendererQuality::Dynamic, 1920, 1080)
            .resolved_quality == RendererQuality::High);

  static_cast<void>(
      controller.update(RendererQuality::Dynamic, 1920, 1080, 35.0));
  const auto downgraded =
      controller.update(RendererQuality::Dynamic, 1920, 1080, 35.0);
  CHECK(downgraded.resolved_quality == RendererQuality::Medium);

  for (std::size_t sample = 0; sample < 280U; ++sample) {
    static_cast<void>(
        controller.update(RendererQuality::Dynamic, 1920, 1080, 8.0));
  }
  CHECK(controller.state().resolved_quality == RendererQuality::High);
  CHECK(controller.state().frame_time_ema_ms == doctest::Approx(8.0));
  CHECK(controller.state().frame_time_p95_ms == doctest::Approx(8.0));
}

TEST_CASE("fixed renderer quality ignores adaptive timing samples") {
  RendererAdaptiveQualityController controller;
  for (std::size_t sample = 0; sample < 32U; ++sample) {
    static_cast<void>(
        controller.update(RendererQuality::High, 3840, 2160, 80.0));
  }
  CHECK(controller.state().resolved_quality == RendererQuality::High);
}

TEST_CASE("ship mesh removes joined box faces and keeps local coordinates") {
  const std::array<ShipPart, 2> parts{{
      {ShipPartShape::Box,
       ShipMaterial::LightDeck,
       {0.0F, 0.0F, 0.0F},
       {1.0F, 1.0F, 1.0F}},
      {ShipPartShape::Box,
       ShipMaterial::LightDeck,
       {1.0F, 0.0F, 0.0F},
       {2.0F, 1.0F, 1.0F}},
  }};
  const auto mesh = build_ship_mesh_data(parts);

  CHECK(mesh.face_count == 10U);
  CHECK(mesh.vertices.size() == 40U);
  CHECK(mesh.indices.size() == 60U);
  const auto max_x = std::max_element(
      mesh.vertices.begin(), mesh.vertices.end(),
      [](const auto &lhs, const auto &rhs) { return lhs.x < rhs.x; });
  REQUIRE(max_x != mesh.vertices.end());
  CHECK(max_x->x == doctest::Approx(2.0F));
}

TEST_CASE("legacy ship mesh ignores modern decorative furniture shapes") {
  const std::array<ShipPart, 2> decorative_parts{{
      {
          ShipPartShape::ChamferedBox,
          ShipMaterial::Linen,
          {-1.0F, 0.0F, -1.0F},
          {1.0F, 0.4F, 1.0F},
      },
      {
          ShipPartShape::DrapedPanel,
          ShipMaterial::BurgundyTextile,
          {-1.0F, 0.5F, -1.0F},
          {1.0F, 0.5F, 1.0F},
          {0.0F, 1.0F, 0.0F},
          0.06F,
      },
  }};
  const auto decorative_mesh = build_ship_mesh_data(decorative_parts);
  CHECK(decorative_mesh.face_count == 0U);
  CHECK(decorative_mesh.vertices.empty());
  CHECK(decorative_mesh.indices.empty());

  const std::array<ShipPart, 1> physical_core{{
      {
          ShipPartShape::Box,
          ShipMaterial::OiledOak,
          {-1.0F, 0.0F, -1.0F},
          {1.0F, 0.4F, 1.0F},
      },
  }};
  const auto core_mesh = build_ship_mesh_data(physical_core);
  CHECK_FALSE(core_mesh.empty());
}

TEST_CASE("ship deck underside blocks its own sky lighting") {
  const std::array<ShipPart, 1> parts{{
      {ShipPartShape::Box,
       ShipMaterial::LightDeck,
       {0.0F, 0.0F, 0.0F},
       {2.0F, 1.0F, 2.0F}},
  }};
  const auto mesh = build_ship_mesh_data(parts);
  auto underside_vertices = std::size_t{0U};
  auto top_vertices = std::size_t{0U};

  for (const auto &vertex : mesh.vertices) {
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
  const std::array<ShipPart, 2> parts{{
      {ShipPartShape::Box,
       ShipMaterial::LightDeck,
       {0.0F, 3.0F, 0.0F},
       {2.0F, 4.0F, 2.0F}},
      {ShipPartShape::Box,
       ShipMaterial::Lantern,
       {0.85F, 2.30F, 0.85F},
       {1.15F, 2.65F, 1.15F}},
  }};
  const auto mesh = build_ship_mesh_data(parts);
  auto ceiling_vertices = std::size_t{0U};
  for (const auto &vertex : mesh.vertices) {
    if (vertex.ny < -0.90F && std::abs(vertex.y - 3.0F) < 0.001F) {
      ++ceiling_vertices;
      CHECK(vertex.block_light > 0.0F);
    }
  }
  CHECK(ceiling_vertices == 4U);
}

TEST_CASE("ship mesh repeats atlas detail across long structural faces") {
  const std::array<ShipPart, 1> parts{{
      {ShipPartShape::Box,
       ShipMaterial::CleanBeam,
       {0.0F, 0.0F, 0.0F},
       {6.0F, 1.0F, 1.0F}},
  }};
  const auto mesh = build_ship_mesh_data(parts);

  CHECK(mesh.face_count == 14U);
  CHECK(mesh.vertices.size() == mesh.face_count * 4U);
  CHECK(mesh.indices.size() == mesh.face_count * 6U);
}

TEST_CASE(
    "ship canvas panel turns a fore and aft sail into a supported triangle") {
  const std::array<ShipPart, 1> parts{{
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

  auto top_vertex_count = std::size_t{0U};
  auto bottom_reaches_forward_corner = false;
  for (const auto &vertex : mesh.vertices) {
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
  const std::array<ShipPart, 1> parts{{
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

  auto canvas_tiles = std::size_t{0U};
  for (std::size_t face = 0; face < mesh.vertices.size(); face += 4U) {
    const auto &first = mesh.vertices[face];
    if (std::abs(first.nz) < 0.90F) {
      continue;
    }
    const auto point = [](const ChunkVertex &vertex) {
      return glm::vec3{vertex.x, vertex.y, vertex.z};
    };
    const auto first_span =
        glm::length(point(mesh.vertices[face + 1U]) - point(first));
    const auto second_span =
        glm::length(point(mesh.vertices[face + 3U]) - point(first));
    CHECK(first_span <= 2.01F);
    CHECK(second_span <= 2.01F);
    ++canvas_tiles;
  }
  CHECK(canvas_tiles > 20U);
}

TEST_CASE("climbable ship net is an open rope grid with reinforced borders") {
  const std::array<ShipPart, 1> parts{{
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
  auto minimum = glm::vec3{std::numeric_limits<float>::max()};
  auto maximum = glm::vec3{std::numeric_limits<float>::lowest()};
  for (const auto &vertex : mesh.vertices) {
    CHECK(std::isfinite(vertex.x));
    CHECK(std::isfinite(vertex.y));
    CHECK(std::isfinite(vertex.z));
    CHECK(std::isfinite(vertex.nx));
    CHECK(std::isfinite(vertex.ny));
    CHECK(std::isfinite(vertex.nz));
    CHECK(vertex.material_class ==
          doctest::Approx(static_cast<float>(BlockVisualMaterial::Wood)));
    CHECK(vertex.u >= rope_uv[0]);
    CHECK(vertex.u <= rope_uv[2]);
    CHECK(vertex.v >= rope_uv[1]);
    CHECK(vertex.v <= rope_uv[3]);
    minimum = glm::min(minimum, glm::vec3{vertex.x, vertex.y, vertex.z});
    maximum = glm::max(maximum, glm::vec3{vertex.x, vertex.y, vertex.z});
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
    auto face_minimum = glm::vec3{std::numeric_limits<float>::max()};
    auto face_maximum = glm::vec3{std::numeric_limits<float>::lowest()};
    for (std::size_t corner = 0; corner < 4U; ++corner) {
      const auto &vertex = mesh.vertices[face + corner];
      const auto point = glm::vec3{vertex.x, vertex.y, vertex.z};
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

TEST_CASE(
    "climbable ship nets mirror across X and also support Z-facing planes") {
  const ShipPart port{
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

  const auto port_mesh =
      build_ship_mesh_data(std::span<const ShipPart>(&port, 1U));
  const auto starboard_mesh =
      build_ship_mesh_data(std::span<const ShipPart>(&starboard, 1U));
  REQUIRE(port_mesh.face_count == starboard_mesh.face_count);
  REQUIRE(port_mesh.vertices.size() == starboard_mesh.vertices.size());

  auto port_min_x = std::numeric_limits<float>::max();
  auto port_max_x = std::numeric_limits<float>::lowest();
  auto starboard_min_x = std::numeric_limits<float>::max();
  auto starboard_max_x = std::numeric_limits<float>::lowest();
  for (std::size_t index = 0; index < port_mesh.vertices.size(); ++index) {
    const auto &port_vertex = port_mesh.vertices[index];
    const auto &starboard_vertex = starboard_mesh.vertices[index];
    port_min_x = std::min(port_min_x, port_vertex.x);
    port_max_x = std::max(port_max_x, port_vertex.x);
    starboard_min_x = std::min(starboard_min_x, starboard_vertex.x);
    starboard_max_x = std::max(starboard_max_x, starboard_vertex.x);
    CHECK(port_vertex.y == doctest::Approx(starboard_vertex.y));
    CHECK(port_vertex.z == doctest::Approx(starboard_vertex.z));
  }
  CHECK(port_min_x == doctest::Approx(-starboard_max_x));
  CHECK(port_max_x == doctest::Approx(-starboard_min_x));

  const std::array<ShipPart, 2> z_facing{{
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
  const auto positive_z_mesh =
      build_ship_mesh_data(std::span<const ShipPart>(&z_facing[0], 1U));
  const auto negative_z_mesh =
      build_ship_mesh_data(std::span<const ShipPart>(&z_facing[1], 1U));
  CHECK(positive_z_mesh.face_count == 92U);
  CHECK(positive_z_mesh.face_count == negative_z_mesh.face_count);
  CHECK(positive_z_mesh.face_count < 128U);
}

TEST_CASE("stern glyph bitmap follows the outward face orientation") {
  const std::array<ShipPart, 1> parts{{
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
  for (const auto &vertex : mesh.vertices) {
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
  const std::array<ShipPart, 1> parts{{
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

  const auto mesh = build_ship_mesh_data(parts);

  REQUIRE_FALSE(mesh.empty());
  CHECK(mesh.face_count > 0U);

  for (const auto &vertex : mesh.vertices) {
    CHECK(vertex.material_class ==
          doctest::Approx(static_cast<float>(BlockVisualMaterial::Metal)));
  }
}

TEST_CASE("L'Amelie mesh renders every maritime family with interior and "
          "lantern lighting") {
  const auto &blueprint = amelie_ship_blueprint();
  std::vector<ShipPart> climbable_nets;
  for (const auto &part : blueprint.parts) {
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
  for (const auto &vertex : mesh.vertices) {
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

  auto illuminated_cabin_ceiling_vertices = std::size_t{0};
  auto downward_cabin_vertices = std::size_t{0};
  auto lowest_cabin_ceiling = std::numeric_limits<float>::max();
  auto highest_cabin_ceiling = std::numeric_limits<float>::lowest();
  for (const auto &vertex : mesh.vertices) {
    if (vertex.ny < -0.90F && std::abs(vertex.x - 1.25F) < 2.0F &&
        std::abs(vertex.z + 30.5F) < 2.0F) {
      ++downward_cabin_vertices;
      lowest_cabin_ceiling = std::min(lowest_cabin_ceiling, vertex.y);
      highest_cabin_ceiling = std::max(highest_cabin_ceiling, vertex.y);
    }
    if (vertex.ny < -0.90F && vertex.y >= 3.60F && vertex.y <= 3.70F &&
        std::abs(vertex.x - 1.25F) < 2.0F &&
        std::abs(vertex.z + 30.5F) < 2.0F) {
      ++illuminated_cabin_ceiling_vertices;
      CHECK(vertex.block_light > 0.0F);
    }
  }
  CAPTURE(downward_cabin_vertices);
  CAPTURE(lowest_cabin_ceiling);
  CAPTURE(highest_cabin_ceiling);
  CHECK(illuminated_cabin_ceiling_vertices > 0U);
}

TEST_CASE(
    "large ship visibility uses its nearest bounds instead of its center") {
  std::array<FrustumPlane, 6> permissive_frustum{};
  for (auto &plane : permissive_frustum) {
    plane.normal = {0.0F, 1.0F, 0.0F};
    plane.distance = 1'000.0F;
  }
  const ChunkBounds ship_bounds{
      {-7.2F, -2.0F, -30.5F},
      {7.2F, 24.5F, 38.5F},
      {0.0F, 11.25F, 4.0F},
  };
  const glm::vec3 eye{0.0F, 6.0F, -25.0F};
  const glm::vec3 looking_towards_stern{0.0F, 0.0F, -1.0F};
  ShadowPassContext shadow_context{};
  shadow_context.enabled = true;
  shadow_context.frustum = permissive_frustum;
  shadow_context.focus = eye;
  shadow_context.max_distance_sq = 4.0F;

  const auto generic_visibility = classify_chunk_visibility(
      ship_bounds, permissive_frustum, eye, looking_towards_stern, 10'000.0F,
      100.0F, shadow_context, true);
  CHECK_FALSE(generic_visibility.camera);
  CHECK_FALSE(generic_visibility.shadow);

  const auto ship_visibility = classify_large_bounds_visibility(
      ship_bounds, permissive_frustum, eye, 10'000.0F, shadow_context, true);
  CHECK(ship_visibility.camera);
  CHECK(ship_visibility.shadow);
  CHECK(ship_visibility.distance_squared == doctest::Approx(0.0F));
}

TEST_CASE("accent atlas keeps celestial sprites bright and readable") {
  const auto atlas_pixels = build_accent_atlas_pixels();
  REQUIRE(atlas_pixels.size() ==
          static_cast<std::size_t>(kAccentAtlasSize * kAccentAtlasSize * 4));

  const auto sun_tile = accent_atlas_tile(AccentAtlasSprite::Sun);
  const auto moon_tile = accent_atlas_tile(AccentAtlasSprite::Moon);
  const auto star_tile = accent_atlas_tile(AccentAtlasSprite::Star);
  const auto sun_core =
      sample_accent_atlas_pixel(atlas_pixels, sun_tile.x, sun_tile.y, 7, 7);
  const auto moon_core =
      sample_accent_atlas_pixel(atlas_pixels, moon_tile.x, moon_tile.y, 7, 7);
  const auto star_core =
      sample_accent_atlas_pixel(atlas_pixels, star_tile.x, star_tile.y, 7, 7);

  CHECK(sun_core[0] > 230);
  CHECK(sun_core[1] > 210);
  CHECK(moon_core[2] > moon_core[0]);
  CHECK(moon_core[3] > 200);
  CHECK(star_core[3] > 220);
  CHECK(star_core[0] > 220);
}

TEST_CASE("scene sampler bindings keep neutral fallback textures outside "
          "refraction passes") {
  constexpr std::uint32_t kFallbackColor = 11U;
  constexpr std::uint32_t kFallbackDepth = 12U;
  constexpr std::uint32_t kSceneColor = 21U;
  constexpr std::uint32_t kSceneDepth = 22U;
  const SceneSamplerBindings fallback_bindings{kFallbackColor, kFallbackDepth};
  const SceneSamplerBindings scene_bindings{kSceneColor, kSceneDepth};

  CHECK(select_scene_sampler_bindings(false, kFallbackColor, kFallbackDepth,
                                      kSceneColor,
                                      kSceneDepth) == fallback_bindings);
  CHECK(select_scene_sampler_bindings(true, kFallbackColor, kFallbackDepth,
                                      kSceneColor,
                                      kSceneDepth) == scene_bindings);
  CHECK(select_scene_sampler_bindings(true, kFallbackColor, kFallbackDepth,
                                      kSceneColor, 0U) == fallback_bindings);
}

TEST_CASE(
    "camera culling can reject a chunk that still belongs in the shadow pass") {
  constexpr float shadow_distance = 96.0F;
  constexpr float back_cull_start_distance = 20.0F;

  const glm::vec3 eye{8.0F, 36.0F, 8.0F};
  const glm::vec3 forward{0.0F, 0.0F, -1.0F};
  const auto camera_projection =
      glm::perspective(glm::radians(75.0F), 16.0F / 9.0F, 0.1F, 320.0F);
  const auto camera_view =
      glm::lookAt(eye, eye + forward, glm::vec3{0.0F, 1.0F, 0.0F});
  const auto camera_frustum =
      extract_frustum_planes(camera_projection * camera_view);

  const glm::vec3 focus{8.0F, 18.0F, 8.0F};
  const auto sun_direction = glm::normalize(glm::vec3{0.25F, 0.9F, 0.35F});
  const auto light_position = focus + sun_direction * (shadow_distance * 0.85F);
  const auto light_view =
      glm::lookAt(light_position, focus, glm::vec3{0.0F, 1.0F, 0.0F});
  const auto light_projection =
      glm::ortho(-shadow_distance, shadow_distance, -shadow_distance,
                 shadow_distance, 1.0F, shadow_distance * 3.0F);
  const auto light_frustum =
      extract_frustum_planes(light_projection * light_view);

  const auto bounds = make_chunk_bounds({0, 2});
  const auto draw_distance_sq = 160.0F * 160.0F;
  const auto max_shadow_distance =
      shadow_distance + static_cast<float>(kChunkSizeX);
  const auto max_shadow_distance_sq = max_shadow_distance * max_shadow_distance;

  CHECK_FALSE(should_render_chunk_in_camera_pass(
      bounds, camera_frustum, eye, forward, draw_distance_sq,
      back_cull_start_distance * back_cull_start_distance));
  CHECK(should_render_chunk_in_shadow_pass(bounds, light_frustum, focus,
                                           max_shadow_distance_sq));
}

TEST_CASE(
    "combined chunk visibility keeps shadow-only chunks in the shadow cache") {
  constexpr float shadow_distance = 96.0F;
  constexpr float back_cull_start_distance = 20.0F;

  const glm::vec3 eye{8.0F, 36.0F, 8.0F};
  const glm::vec3 forward{0.0F, 0.0F, -1.0F};
  const auto camera_projection =
      glm::perspective(glm::radians(75.0F), 16.0F / 9.0F, 0.1F, 320.0F);
  const auto camera_view =
      glm::lookAt(eye, eye + forward, glm::vec3{0.0F, 1.0F, 0.0F});
  const auto camera_frustum =
      extract_frustum_planes(camera_projection * camera_view);

  const glm::vec3 focus{8.0F, 18.0F, 8.0F};
  const auto sun_direction = glm::normalize(glm::vec3{0.25F, 0.9F, 0.35F});
  const auto light_position = focus + sun_direction * (shadow_distance * 0.85F);
  const auto light_view =
      glm::lookAt(light_position, focus, glm::vec3{0.0F, 1.0F, 0.0F});
  const auto light_projection =
      glm::ortho(-shadow_distance, shadow_distance, -shadow_distance,
                 shadow_distance, 1.0F, shadow_distance * 3.0F);

  ShadowPassContext shadow_context{};
  shadow_context.frustum =
      extract_frustum_planes(light_projection * light_view);
  shadow_context.focus = focus;
  const auto max_shadow_distance =
      shadow_distance + static_cast<float>(kChunkSizeX);
  shadow_context.max_distance_sq = max_shadow_distance * max_shadow_distance;
  shadow_context.enabled = true;

  const auto visibility = classify_chunk_visibility(
      make_chunk_bounds({0, 2}), camera_frustum, eye, forward, 160.0F * 160.0F,
      back_cull_start_distance * back_cull_start_distance, shadow_context,
      true);

  CHECK_FALSE(visibility.camera);
  CHECK(visibility.shadow);
}

TEST_CASE("shadow culling rejects chunks outside the light coverage volume") {
  constexpr float shadow_distance = 96.0F;

  const glm::vec3 focus{8.0F, 18.0F, 8.0F};
  const auto sun_direction = glm::normalize(glm::vec3{0.25F, 0.9F, 0.35F});
  const auto light_position = focus + sun_direction * (shadow_distance * 0.85F);
  const auto light_view =
      glm::lookAt(light_position, focus, glm::vec3{0.0F, 1.0F, 0.0F});
  const auto light_projection =
      glm::ortho(-shadow_distance, shadow_distance, -shadow_distance,
                 shadow_distance, 1.0F, shadow_distance * 3.0F);
  const auto light_frustum =
      extract_frustum_planes(light_projection * light_view);

  const auto far_bounds = make_chunk_bounds({16, 16});
  const auto max_shadow_distance =
      shadow_distance + static_cast<float>(kChunkSizeX);
  const auto max_shadow_distance_sq = max_shadow_distance * max_shadow_distance;

  CHECK_FALSE(should_render_chunk_in_shadow_pass(
      far_bounds, light_frustum, focus, max_shadow_distance_sq));
}

TEST_CASE("item drop geometry keeps cube layers and atlas faces aligned with "
          "gpu instances") {
  ItemDropRenderInstance drop{};
  drop.position = {2.0F, 4.0F, 6.0F};
  drop.block_id = to_block_id(BlockType::Grass);
  drop.count = 1;
  drop.sky_light = 1.0F;

  const auto single_instances = build_item_drop_gpu_instances(
      std::span<const ItemDropRenderInstance>(&drop, 1));
  const auto single_vertices = build_item_drop_vertices(
      std::span<const ItemDropRenderInstance>(&drop, 1));
  REQUIRE(single_instances.size() == 1);
  CHECK(single_vertices.size() == 36);

  std::set<std::array<int, 3>> normals;
  for (const auto &vertex : single_vertices) {
    normals.insert(rounded_normal(vertex));
  }

  CHECK(normals.size() == 6);
  CHECK(normals.find(std::array<int, 3>{1, 0, 0}) != normals.end());
  CHECK(normals.find(std::array<int, 3>{-1, 0, 0}) != normals.end());
  CHECK(normals.find(std::array<int, 3>{0, 1, 0}) != normals.end());
  CHECK(normals.find(std::array<int, 3>{0, -1, 0}) != normals.end());

  const auto single_faces = collect_face_samples(single_vertices);
  REQUIRE(single_faces.size() == 6);
  const auto item_block = block_item_id(drop.block_id);
  check_face_sample(
      single_faces[0], {1, 0, 0},
      atlas_uv_rect(block_atlas_tile(item_block, BlockVisualFace::PositiveX)));
  check_face_sample(
      single_faces[1], {-1, 0, 0},
      atlas_uv_rect(block_atlas_tile(item_block, BlockVisualFace::NegativeX)));
  check_face_sample(
      single_faces[2], {0, 1, 0},
      atlas_uv_rect(block_atlas_tile(item_block, BlockVisualFace::PositiveY)));
  check_face_sample(
      single_faces[3], {0, -1, 0},
      atlas_uv_rect(block_atlas_tile(item_block, BlockVisualFace::NegativeY)));
  check_face_sample(
      single_faces[4], {0, 0, 1},
      atlas_uv_rect(block_atlas_tile(item_block, BlockVisualFace::PositiveZ)));
  check_face_sample(
      single_faces[5], {0, 0, -1},
      atlas_uv_rect(block_atlas_tile(item_block, BlockVisualFace::NegativeZ)));

  drop.count = 2;
  const auto double_instances = build_item_drop_gpu_instances(
      std::span<const ItemDropRenderInstance>(&drop, 1));
  const auto double_vertices = build_item_drop_vertices(
      std::span<const ItemDropRenderInstance>(&drop, 1));
  CHECK(double_instances.size() == 2);
  CHECK(double_vertices.size() == 72);

  drop.count = 32;
  const auto stacked_instances = build_item_drop_gpu_instances(
      std::span<const ItemDropRenderInstance>(&drop, 1));
  const auto stacked_vertices = build_item_drop_vertices(
      std::span<const ItemDropRenderInstance>(&drop, 1));
  CHECK(stacked_instances.size() == 3);
  CHECK(stacked_vertices.size() == 108);

  drop.count = 1;
  drop.age_seconds = (std::numeric_limits<float>::max)();
  drop.spin_radians = (std::numeric_limits<float>::max)();
  const auto safe_instances = build_item_drop_gpu_instances(
      std::span<const ItemDropRenderInstance>(&drop, 1));
  const auto safe_vertices = build_item_drop_vertices(
      std::span<const ItemDropRenderInstance>(&drop, 1));
  REQUIRE(safe_instances.size() == 1);
  for (int column = 0; column < 4; ++column) {
    for (int row = 0; row < 4; ++row) {
      CHECK(std::isfinite(safe_instances.front().transform[column][row]));
    }
  }
  for (const auto &vertex : safe_vertices) {
    CHECK(std::isfinite(vertex.x));
    CHECK(std::isfinite(vertex.y));
    CHECK(std::isfinite(vertex.z));
  }
}

TEST_CASE("musket ground drop preserves the canonical recipe in modern and "
          "legacy pipelines") {
  CHECK_FALSE(item_drop_uses_rounded_template(VisualPipeline::LegacyVoxel));
  CHECK(item_drop_uses_rounded_template(VisualPipeline::ModernStylized));

  ItemDropRenderInstance drop{};
  drop.position = {2.0F, 4.0F, 6.0F};
  drop.block_id = to_block_id(BlockType::Musket);
  drop.count = 1;
  drop.sky_light = 0.75F;
  drop.block_light = 0.25F;

  const auto instances = build_item_drop_gpu_instances(
      std::span<const ItemDropRenderInstance>(&drop, 1));
  const auto vertices = build_item_drop_vertices(
      std::span<const ItemDropRenderInstance>(&drop, 1));
  const auto recipe = musket_visual_parts();
  REQUIRE(recipe.size() == 30U);
  REQUIRE(instances.size() == recipe.size());
  REQUIRE(vertices.size() == recipe.size() * 36U);

  bool found_walnut = false;
  bool found_steel = false;
  bool found_brass = false;
  bool found_flint = false;
  bool found_dark_bore = false;
  for (std::size_t index = 0U; index < instances.size(); ++index) {
    const auto &instance = instances[index];
    const auto &part = recipe[index];
    CHECK(instance.block_id == to_block_id(BlockType::Musket));
    CHECK(instance.sky_light == doctest::Approx(drop.sky_light));
    CHECK(instance.block_light == doctest::Approx(drop.block_light));

    BlockId expected_texture = to_block_id(BlockType::IronOre);
    switch (part.material) {
    case MusketVisualMaterial::Walnut:
      expected_texture = to_block_id(BlockType::Planks);
      found_walnut = true;
      break;
    case MusketVisualMaterial::Brass:
      expected_texture = to_block_id(BlockType::GoldOre);
      found_brass = true;
      break;
    case MusketVisualMaterial::DarkBore:
      expected_texture = to_block_id(BlockType::CoalOre);
      found_dark_bore = part.kind == MusketVisualPartKind::Muzzle;
      break;
    case MusketVisualMaterial::Flint:
      expected_texture = to_block_id(BlockType::CoalOre);
      found_flint = true;
      break;
    case MusketVisualMaterial::PatinatedSteel:
    default:
      found_steel = true;
      break;
    }
    CHECK(instance.texture_id == expected_texture);
    const auto expected_tile =
        block_atlas_tile(expected_texture, BlockVisualFace::PositiveX);
    CHECK(instance.face_tiles_0_1.x ==
          doctest::Approx(static_cast<float>(expected_tile.x)));
    CHECK(instance.face_tiles_0_1.y ==
          doctest::Approx(static_cast<float>(expected_tile.y)));

    CHECK(glm::length(glm::vec3{instance.transform[0]}) ==
          doctest::Approx(part.half_extent.x * 2.0F * kMusketGroundDropScale));
    CHECK(glm::length(glm::vec3{instance.transform[1]}) ==
          doctest::Approx(part.half_extent.y * 2.0F * kMusketGroundDropScale));
    CHECK(glm::length(glm::vec3{instance.transform[2]}) ==
          doctest::Approx(part.half_extent.z * 2.0F * kMusketGroundDropScale));

    for (int column = 0; column < 4; ++column) {
      for (int row = 0; row < 4; ++row) {
        CHECK(std::isfinite(instance.transform[column][row]));
      }
    }
  }
  CHECK(found_walnut);
  CHECK(found_steel);
  CHECK(found_brass);
  CHECK(found_flint);
  CHECK(found_dark_bore);

  auto minimum = glm::vec3{(std::numeric_limits<float>::max)()};
  auto maximum = glm::vec3{(std::numeric_limits<float>::lowest)()};
  for (const auto &vertex : vertices) {
    minimum = glm::min(minimum, glm::vec3{vertex.x, vertex.y, vertex.z});
    maximum = glm::max(maximum, glm::vec3{vertex.x, vertex.y, vertex.z});
  }
  const auto extent = maximum - minimum;
  CHECK(extent.x > 1.35F);
  CHECK(extent.x > extent.z * 4.0F);
  CHECK(minimum.y > drop.position.y);

  // Je refuse de dupliquer visuellement une arme mono-exemplaire meme si
  // une instance de rendu corrompue annonce une pile impossible.
  drop.count = 32;
  CHECK(build_item_drop_gpu_instances(
            std::span<const ItemDropRenderInstance>(&drop, 1))
            .size() == recipe.size());

  drop.count = 1;
  drop.age_seconds = (std::numeric_limits<float>::max)();
  drop.spin_radians = (std::numeric_limits<float>::max)();
  const auto sanitized_instances = build_item_drop_gpu_instances(
      std::span<const ItemDropRenderInstance>(&drop, 1));
  REQUIRE(sanitized_instances.size() == recipe.size());
  for (const auto &instance : sanitized_instances) {
    for (int column = 0; column < 4; ++column) {
      for (int row = 0; row < 4; ++row) {
        CHECK(std::isfinite(instance.transform[column][row]));
      }
    }
  }
}

TEST_CASE(
    "item drop atlas mapping covers tools and ignores corrupted block ids") {
  constexpr std::array<BlockVisualFace, 6> faces{{
      BlockVisualFace::PositiveX,
      BlockVisualFace::NegativeX,
      BlockVisualFace::PositiveY,
      BlockVisualFace::NegativeY,
      BlockVisualFace::PositiveZ,
      BlockVisualFace::NegativeZ,
  }};

  for (BlockId block_id = to_block_id(BlockType::Grass);
       block_id <= to_block_id(BlockType::Shovel); ++block_id) {
    const auto item_id = block_item_id(block_id);
    CAPTURE(static_cast<int>(block_id));
    REQUIRE(item_id != to_block_id(BlockType::Air));

    for (const auto face : faces) {
      CHECK(item_drop_atlas_tile(block_id, face) ==
            block_atlas_tile(item_id, face));
    }

    ItemDropRenderInstance drop{};
    drop.block_id = block_id;
    drop.count = 1;
    const auto instances = build_item_drop_gpu_instances(
        std::span<const ItemDropRenderInstance>(&drop, 1));
    REQUIRE(instances.size() == 1);
    CHECK(instances.front().block_id == item_id);
  }

  struct ToolCase {
    BlockType type;
    BlockAtlasTile tile;
  };
  constexpr std::array<ToolCase, 3> tools{{
      {BlockType::Pickaxe, {5, 6}},
      {BlockType::Axe, {6, 6}},
      {BlockType::Shovel, {7, 6}},
  }};

  for (const auto &tool : tools) {
    const auto block_id = to_block_id(tool.type);
    CAPTURE(static_cast<int>(tool.type));
    CHECK(item_drop_atlas_tile(block_id, BlockVisualFace::PositiveX) ==
          tool.tile);

    ItemDropRenderInstance drop{};
    drop.block_id = block_id;
    drop.count = 1;
    const auto instances = build_item_drop_gpu_instances(
        std::span<const ItemDropRenderInstance>(&drop, 1));
    REQUIRE(instances.size() == 1);
    CHECK(instances.front().face_tiles_0_1.x ==
          doctest::Approx(static_cast<float>(tool.tile.x)));
    CHECK(instances.front().face_tiles_0_1.y ==
          doctest::Approx(static_cast<float>(tool.tile.y)));
  }

  ItemDropRenderInstance corrupted{};
  corrupted.block_id = static_cast<BlockId>(255U);
  corrupted.count = 1;
  const auto corrupted_instances = build_item_drop_gpu_instances(
      std::span<const ItemDropRenderInstance>(&corrupted, 1));
  const auto corrupted_vertices = build_item_drop_vertices(
      std::span<const ItemDropRenderInstance>(&corrupted, 1));
  CHECK(corrupted_instances.empty());
  CHECK(corrupted_vertices.empty());
  CHECK(item_drop_atlas_tile(corrupted.block_id, BlockVisualFace::PositiveX) ==
        BlockAtlasTile{});
}

TEST_CASE(
    "glass block exposes a readable window tile for village construction") {
  const auto glass_id = to_block_id(BlockType::Glass);
  const auto glass_properties = block_properties(glass_id);

  CHECK_FALSE(glass_properties.opaque);
  CHECK(glass_properties.collidable);
  CHECK_FALSE(glass_properties.surface_support);
  CHECK_FALSE(glass_properties.replaceable);
  CHECK(glass_properties.mesh_type == BlockMeshType::FullCube);
  CHECK(block_atlas_tile(glass_id, BlockVisualFace::PositiveX) ==
        BlockAtlasTile{1, 4});
  CHECK(block_hotbar_tile(glass_id) == BlockAtlasTile{1, 4});
  CHECK(block_visual_material(glass_id) == BlockVisualMaterial::Glass);

  const auto atlas_pixels = build_block_atlas_pixels();
  REQUIRE(atlas_pixels.size() ==
          static_cast<std::size_t>(kBlockAtlasSize * kBlockAtlasSize * 4));

  const auto frame_pixel = sample_block_atlas_pixel(atlas_pixels, 1, 4, 0, 0);
  const auto pane_reflection_pixel =
      sample_block_atlas_pixel(atlas_pixels, 1, 4, 2, 2);
  const auto mullion_pixel = sample_block_atlas_pixel(atlas_pixels, 1, 4, 7, 7);
  const auto transparent_pane =
      sample_block_atlas_pixel(atlas_pixels, 1, 4, 5, 5);

  CHECK(frame_pixel[3] == 255);
  CHECK(mullion_pixel[3] == 255);
  CHECK(transparent_pane[3] == 0);
  CHECK(frame_pixel[0] > frame_pixel[2]);
  CHECK(pane_reflection_pixel[2] > pane_reflection_pixel[0]);
}

TEST_CASE(
    "equipment and tool items stay inventory-only and expose readable icons") {
  struct GearVisualCase {
    BlockType type;
    BlockAtlasTile tile;
    std::array<int, 2> sample;
    BlockVisualMaterial material;
  };

  const std::array<GearVisualCase, 9> gear_items{{
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
  REQUIRE(atlas_pixels.size() ==
          static_cast<std::size_t>(kBlockAtlasSize * kBlockAtlasSize * 4));

  for (const auto &gear : gear_items) {
    const auto block_id = to_block_id(gear.type);
    const auto properties = block_properties(block_id);

    CHECK(is_inventory_only_item(block_id));
    CHECK_FALSE(is_placeable_item(block_id));
    CHECK_FALSE(has_block_mesh(block_id));
    CHECK_FALSE(is_block_breakable(block_id));
    CHECK_FALSE(properties.collidable);
    CHECK(block_hotbar_tile(block_id) == gear.tile);
    CHECK(block_visual_material(block_id) == gear.material);

    const auto icon_pixel = sample_block_atlas_pixel(
        atlas_pixels, gear.tile.x, gear.tile.y, gear.sample[0], gear.sample[1]);
    CHECK(icon_pixel[3] == 255);
  }
}

TEST_CASE(
    "resource ore blocks expose distinct atlas tiles and item-drop materials") {
  struct OreVisualCase {
    BlockType type;
    BlockAtlasTile tile;
  };

  const std::array<OreVisualCase, 5> ore_items{{
      {BlockType::CoalOre, {0, 6}},
      {BlockType::IronOre, {1, 6}},
      {BlockType::GoldOre, {2, 6}},
      {BlockType::DiamondOre, {3, 6}},
      {BlockType::MetallicAlloyOre, {4, 6}},
  }};

  const auto atlas_pixels = build_block_atlas_pixels();
  REQUIRE(atlas_pixels.size() ==
          static_cast<std::size_t>(kBlockAtlasSize * kBlockAtlasSize * 4));
  const auto stone_tile = block_atlas_tile(to_block_id(BlockType::Stone),
                                           BlockVisualFace::PositiveX);

  for (const auto &ore : ore_items) {
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
        const auto ore_pixel = sample_block_atlas_pixel(
            atlas_pixels, ore.tile.x, ore.tile.y, x, y);
        const auto stone_pixel = sample_block_atlas_pixel(
            atlas_pixels, stone_tile.x, stone_tile.y, x, y);
        opaque_pixels += ore_pixel[3] == 255 ? 1 : 0;

        const auto red_delta = std::abs(static_cast<int>(ore_pixel[0]) -
                                        static_cast<int>(stone_pixel[0]));
        const auto green_delta = std::abs(static_cast<int>(ore_pixel[1]) -
                                          static_cast<int>(stone_pixel[1]));
        const auto blue_delta = std::abs(static_cast<int>(ore_pixel[2]) -
                                         static_cast<int>(stone_pixel[2]));
        differentiated_pixels +=
            red_delta + green_delta + blue_delta > 42 ? 1 : 0;
      }
    }

    CAPTURE(static_cast<int>(ore.type));
    CHECK(opaque_pixels == kBlockAtlasTileSize * kBlockAtlasTileSize);
    CHECK(differentiated_pixels > 64);
  }
}

TEST_CASE("block atlas supports the antique Greek village material palette") {
  const auto atlas_pixels = build_block_atlas_pixels();
  REQUIRE(atlas_pixels.size() ==
          static_cast<std::size_t>(kBlockAtlasSize * kBlockAtlasSize * 4));

  const auto stone_tile = block_atlas_tile(to_block_id(BlockType::Stone),
                                           BlockVisualFace::PositiveX);
  const auto sand_tile = block_atlas_tile(to_block_id(BlockType::Sand),
                                          BlockVisualFace::PositiveX);
  const auto roof_tile = block_atlas_tile(to_block_id(BlockType::Planks),
                                          BlockVisualFace::PositiveX);
  const auto stone_center =
      sample_block_atlas_pixel(atlas_pixels, stone_tile.x, stone_tile.y, 8, 8);
  const auto sand_center =
      sample_block_atlas_pixel(atlas_pixels, sand_tile.x, sand_tile.y, 8, 8);
  const auto roof_center =
      sample_block_atlas_pixel(atlas_pixels, roof_tile.x, roof_tile.y, 8, 10);
  const auto roof_seam =
      sample_block_atlas_pixel(atlas_pixels, roof_tile.x, roof_tile.y, 8, 4);

  CHECK(stone_center[0] > stone_center[2] + 20);
  CHECK(stone_center[1] > stone_center[2] + 12);
  CHECK(sand_center[0] > sand_center[2] + 40);
  CHECK(sand_center[1] > sand_center[2] + 24);
  CHECK(roof_center[0] > roof_center[1] + 45);
  CHECK(roof_center[1] > roof_center[2] + 24);
  CHECK(roof_seam[0] < roof_center[0]);
}

TEST_CASE("L'Amelie ship atlas owns ten deterministic and distinct maritime "
          "materials") {
  const std::array<ShipAtlasMaterial, kShipAtlasMaterialCount> materials{{
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

  const auto first_pixels = build_block_atlas_pixels();

  const auto second_pixels = build_block_atlas_pixels();

  REQUIRE(first_pixels == second_pixels);

  std::array<std::uint64_t, kShipAtlasMaterialCount> checksums{};

  std::set<std::pair<int, int>> occupied_tiles;

  for (std::size_t material_index = 0; material_index < materials.size();
       ++material_index) {

    const auto tile = ship_atlas_tile(materials[material_index]);

    CHECK(tile.x >= 0);
    CHECK(tile.x < static_cast<int>(kBlockAtlasTilesPerAxis));

    CHECK(tile.y >= 0);
    CHECK(tile.y < static_cast<int>(kBlockAtlasTilesPerAxis));

    CHECK(occupied_tiles.emplace(tile.x, tile.y).second);

    // Les huit matériaux historiques ne doivent jamais changer de case.
    if (material_index < 8U) {
      CHECK(tile.x == static_cast<int>(material_index));

      CHECK(tile.y == kShipAtlasRow);
    }

    auto checksum = std::uint64_t{
        1469598103934665603ULL,
    };

    for (int y = 0; y < kBlockAtlasTileSize; ++y) {

      for (int x = 0; x < kBlockAtlasTileSize; ++x) {

        const auto pixel =
            sample_block_atlas_pixel(first_pixels, tile.x, tile.y, x, y);

        REQUIRE(pixel[3] == 255U);

        for (const auto channel : pixel) {
          checksum ^= static_cast<std::uint64_t>(channel);

          checksum *= 1099511628211ULL;
        }
      }
    }

    checksums[material_index] = checksum;
  }

  for (std::size_t left = 0; left < checksums.size(); ++left) {

    for (std::size_t right = left + 1U; right < checksums.size(); ++right) {

      CHECK(checksums[left] != checksums[right]);
    }
  }

  const auto black_tile = ship_atlas_tile(ShipAtlasMaterial::BlackCanvas);

  const auto gold_tile = ship_atlas_tile(ShipAtlasMaterial::SolidGold);

  const auto black_center =
      sample_block_atlas_pixel(first_pixels, black_tile.x, black_tile.y, 8, 8);

  const auto gold_center =
      sample_block_atlas_pixel(first_pixels, gold_tile.x, gold_tile.y, 8, 8);

  CHECK(black_center[0] < 80U);
  CHECK(black_center[1] < 80U);
  CHECK(black_center[2] < 90U);

  CHECK(gold_center[0] > 150U);
  CHECK(gold_center[0] > gold_center[1]);
  CHECK(gold_center[1] > gold_center[2]);

  CHECK(ship_visual_material(ShipAtlasMaterial::CreamCanvas) ==
        BlockVisualMaterial::Fabric);

  CHECK(ship_visual_material(ShipAtlasMaterial::BlackCanvas) ==
        BlockVisualMaterial::Fabric);

  CHECK(ship_visual_material(ShipAtlasMaterial::Iron) ==
        BlockVisualMaterial::Metal);

  CHECK(ship_visual_material(ShipAtlasMaterial::Brass) ==
        BlockVisualMaterial::Brass);

  CHECK(ship_visual_material(ShipAtlasMaterial::SolidGold) ==
        BlockVisualMaterial::Brass);

  CHECK(ship_visual_material(ShipAtlasMaterial::Lantern) ==
        BlockVisualMaterial::Emissive);
}

TEST_CASE("progression HUD resolves exact experience and maximum level") {
  const auto level = 99U;
  const auto threshold = player_experience_for_next_level(level);
  const auto progression = PlayerProgressionState{
      level,
      threshold / 2ULL,
  };
  const auto snapshot =
      make_progression_experience_hud_snapshot(progression, 275ULL);

  CHECK(snapshot.level == level);
  CHECK(snapshot.current_experience == threshold / 2ULL);
  CHECK(snapshot.next_level_experience == threshold);
  CHECK(snapshot.aggregated_experience_gain == 275ULL);
  CHECK_FALSE(snapshot.maximum_level);
  CHECK(snapshot.progress_ratio ==
        doctest::Approx(static_cast<float>(threshold / 2ULL) /
                        static_cast<float>(threshold)));

  const auto maximum = make_progression_experience_hud_snapshot(
      {
          kPlayerProgressionMaxLevel,
          std::numeric_limits<std::uint64_t>::max(),
      },
      10ULL);
  CHECK(maximum.maximum_level);
  CHECK(maximum.next_level_experience == 0ULL);
  CHECK(maximum.progress_ratio == doctest::Approx(1.0F));
}

TEST_CASE("ability HUD exposes costs timers charges effects and point state") {
  PlayerBuildState build{};
  const auto ability = AbilityId::KnightVanguardStrike;
  const auto index = ability_index(ability);
  build.ability_ranks[index] = 1U;
  build.equipped_abilities[0U] = ability;
  build.val_energy = 0.0F;
  build.global_cooldown_remaining = 0.15F;
  build.cooldowns_remaining[index] = 4.25F;
  build.charges[index] = 1U;

  ProgressionRuntimeHudView runtime{};
  runtime.focused_ability = ability;
  runtime.active_duration_remaining = 3.5F;
  runtime.wind_blade_armed = true;
  runtime.wind_dodge_ready = true;
  runtime.iron_guard_active = true;
  runtime.active_footmen = 25U;
  const auto snapshot = make_progression_ability_hud_snapshot(build, runtime);

  CHECK(snapshot.visible);
  CHECK(snapshot.ability == ability);
  CHECK_FALSE(snapshot.display_name.empty());
  CHECK(snapshot.energy_cost > 0.0F);
  CHECK(snapshot.energy_insufficient);
  CHECK(snapshot.global_cooldown_remaining == doctest::Approx(0.15F));
  CHECK(snapshot.cooldown_remaining == doctest::Approx(4.25F));
  CHECK(snapshot.charges == 1U);
  CHECK(snapshot.maximum_charges >= snapshot.charges);
  CHECK(snapshot.active_duration_remaining == doctest::Approx(3.5F));
  CHECK(snapshot.wind_blade_armed);
  CHECK(snapshot.wind_dodge_ready);
  CHECK(snapshot.iron_guard_active);
  CHECK(snapshot.active_footmen == 8U);
  CHECK_FALSE(snapshot.feedback_assets.visual_id.empty());
  CHECK_FALSE(snapshot.feedback_assets.sfx_id.empty());
}

TEST_CASE("ability feedback identifiers have deterministic generic fallbacks") {
  const auto fallback =
      resolve_ability_feedback_assets(AbilityId::None, {}, {});
  CHECK(fallback.visual_id == kGenericAbilityVisualId);
  CHECK(fallback.sfx_id == kGenericAbilitySfxId);

  const auto explicit_assets = resolve_ability_feedback_assets(
      AbilityId::KnightIronGuard, "VisualTest", "SfxTest");
  CHECK(explicit_assets.visual_id == "VisualTest");
  CHECK(explicit_assets.sfx_id == "SfxTest");
}

TEST_CASE(
    "ability HUD layout stays bounded and disjoint at supported resolutions") {
  constexpr std::array<std::array<int, 2U>, 3U> viewports{{
      {640, 360},
      {1280, 720},
      {1600, 900},
  }};

  for (const auto &viewport_size : viewports) {
    const auto layout = make_progression_ability_hud_layout(viewport_size[0U],
                                                            viewport_size[1U]);
    CAPTURE(viewport_size[0U], viewport_size[1U]);
    REQUIRE(layout.valid());
    const auto viewport = ProgressionMenuRect{
        0.0F,
        0.0F,
        static_cast<float>(viewport_size[0U]),
        static_cast<float>(viewport_size[1U]),
    };
    CHECK(progression_menu_rect_contains(viewport, layout.panel));
    CHECK_FALSE(progression_menu_rects_overlap(layout.energy, layout.ability));
    CHECK_FALSE(progression_menu_rects_overlap(layout.ability, layout.timers));
    CHECK_FALSE(progression_menu_rects_overlap(layout.timers, layout.effects));
  }
}

TEST_CASE("legendary renderer bridge owns bounded copies of every presentation "
          "family") {
  std::vector<LeviathanWeaponPartInstance> weapon_parts(
      kRendererMaximumLeviathanWeaponParts + 3U);
  std::vector<ChainedColossusPartInstance> colossus_parts(1U);
  std::vector<ColossusBloodTrace> blood_traces(
      kRendererMaximumColossusBloodTraces + 2U);
  for (std::size_t index = 0U; index < blood_traces.size(); ++index) {
    blood_traces[index].position = {
        static_cast<float>(index) * 0.01F,
        72.0F,
        0.0F,
    };
    blood_traces[index].normal = {0.0F, 1.0F, 0.0F};
    blood_traces[index].radius = 0.08F;
    blood_traces[index].opacity = 0.82F;
  }

  std::vector<IssouCrowdInstance> crowd(kRendererMaximumIssouCrowdInstances +
                                        4U);
  for (auto &member : crowd) {
    // Je garde presque toute la foule en LOD culled pour tester le contrat
    // d'entrée sans alourdir inutilement ce test unitaire.
    member.lod = IssouCrowdLod::Culled;
  }
  crowd.front().lod = IssouCrowdLod::Full;

  std::vector<IssouArenaDecorInstance> decor(
      kRendererMaximumIssouDecorInstances + 1U);
  std::vector<LegendaryEnemyRenderSnapshot> enemies(
      kRendererMaximumLegendaryEnemySnapshots + 2U);
  enemies.front().alive = true;
  enemies.front().position = {2.0F, 72.0F, 3.0F};

  SeaLeviathanRenderSnapshot sea{};
  sea.active = true;
  sea.body_anchor_world = {0.0F, 62.0F, 8.0F};
  sea.core_world = {0.0F, 63.0F, 7.5F};
  sea.telegraph_world = {0.0F, 64.0F, 5.0F};
  for (std::size_t index = 0U; index < sea.tentacles.size(); ++index) {
    sea.tentacles[index].anchor_world = {
        index % 2U == 0U ? -4.0F : 4.0F,
        63.0F,
        4.0F + static_cast<float>(index),
    };
    sea.tentacles[index].resistance_ratio = 1.0F;
  }

  RendererLegendaryPresentationFrame frame{};
  frame.first_person_weapon_parts = {
      weapon_parts.data(),
      weapon_parts.size(),
  };
  frame.chained_colossus_parts = {
      colossus_parts.data(),
      colossus_parts.size(),
  };
  frame.colossus_blood_traces = {
      blood_traces.data(),
      blood_traces.size(),
  };
  frame.issou_crowd = {
      crowd.data(),
      crowd.size(),
  };
  frame.issou_decor = {
      decor.data(),
      decor.size(),
  };
  frame.legendary_enemies = {
      enemies.data(),
      enemies.size(),
  };
  frame.sea_leviathan = sea;

  Renderer renderer{};
  renderer.set_legendary_presentation(frame);
  const auto submitted = renderer.legendary_presentation_stats();
  CHECK(submitted.accepted_weapon_parts ==
        kRendererMaximumLeviathanWeaponParts);
  CHECK(submitted.accepted_colossus_parts == 1U);
  CHECK(submitted.accepted_blood_traces == kRendererMaximumColossusBloodTraces);
  CHECK(submitted.accepted_crowd_instances ==
        kRendererMaximumIssouCrowdInstances);
  CHECK(submitted.accepted_decor_instances ==
        kRendererMaximumIssouDecorInstances);
  CHECK(submitted.accepted_enemy_snapshots ==
        kRendererMaximumLegendaryEnemySnapshots);
  CHECK(submitted.accepted_sea_leviathans == 1U);
  CHECK(submitted.staged_viewmodel_parts <=
        kRendererMaximumLeviathanWeaponParts);
  CHECK(submitted.staged_world_parts <= kRendererMaximumLegendaryWorldParts);
  CHECK(submitted.dropped_submissions >= 12U);
  CHECK(submitted.pending);

  // Je vérifie que les spans de l'appelant ne restent pas empruntés.
  weapon_parts.front().transform[0][0] =
      std::numeric_limits<float>::quiet_NaN();
  CHECK(renderer.legendary_presentation_stats().accepted_weapon_parts ==
        kRendererMaximumLeviathanWeaponParts);

  renderer.clear_legendary_presentation();
  const auto cleared = renderer.legendary_presentation_stats();
  CHECK_FALSE(cleared.pending);
  CHECK(cleared.staged_viewmodel_parts == 0U);
  CHECK(cleared.staged_world_parts == 0U);
}

TEST_CASE("legendary renderer bridge rejects non finite GPU input and consumes "
          "unrendered frames") {
  LeviathanWeaponPartInstance weapon{};
  weapon.transform[0][0] = std::numeric_limits<float>::quiet_NaN();
  ChainedColossusPartInstance colossus{};
  colossus.geometry.transform[1][1] = std::numeric_limits<float>::infinity();
  ColossusBloodTrace blood{};
  blood.radius = std::numeric_limits<float>::quiet_NaN();
  blood.opacity = 1.0F;
  IssouCrowdInstance crowd{};
  crowd.position.x = std::numeric_limits<float>::infinity();
  IssouArenaDecorInstance decor{};
  decor.transform[2][2] = std::numeric_limits<float>::quiet_NaN();
  LegendaryEnemyRenderSnapshot enemy{};
  enemy.alive = true;
  enemy.position.z = std::numeric_limits<float>::infinity();
  SeaLeviathanRenderSnapshot sea{};
  sea.active = true;
  sea.body_anchor_world.x = std::numeric_limits<float>::quiet_NaN();

  RendererLegendaryPresentationFrame malformed{};
  malformed.first_person_weapon_parts = {&weapon, 1U};
  malformed.chained_colossus_parts = {&colossus, 1U};
  malformed.colossus_blood_traces = {&blood, 1U};
  malformed.issou_crowd = {&crowd, 1U};
  malformed.issou_decor = {&decor, 1U};
  malformed.legendary_enemies = {&enemy, 1U};
  malformed.sea_leviathan = sea;

  Renderer renderer{};
  renderer.set_legendary_presentation(malformed);
  const auto rejected = renderer.legendary_presentation_stats();
  CHECK(rejected.accepted_weapon_parts == 0U);
  CHECK(rejected.accepted_colossus_parts == 0U);
  CHECK(rejected.accepted_blood_traces == 0U);
  CHECK(rejected.accepted_crowd_instances == 0U);
  CHECK(rejected.accepted_decor_instances == 0U);
  CHECK(rejected.accepted_enemy_snapshots == 0U);
  CHECK(rejected.accepted_sea_leviathans == 0U);
  CHECK(rejected.staged_viewmodel_parts == 0U);
  CHECK(rejected.staged_world_parts == 0U);
  CHECK(rejected.dropped_submissions >= 7U);
  CHECK_FALSE(rejected.pending);

  LeviathanWeaponPartInstance valid_weapon{};
  RendererLegendaryPresentationFrame valid{};
  valid.first_person_weapon_parts = {&valid_weapon, 1U};
  renderer.set_legendary_presentation(valid);
  REQUIRE(renderer.legendary_presentation_stats().pending);

  // Le renderer non initialisé suit le même contrat mono-frame que le
  // renderer OpenGL actif, sans tenter le moindre appel GPU.
  World world{7331, 1};
  PlayerController player{};
  renderer.render_frame(
      world, player, PlayerMusketView{}, HotbarState{}, InventoryMenuState{},
      DeathScreenState{}, PauseMenuState{}, MainMenuState{},
      SaveSlotMenuState{}, OptionsMenuState{}, ConfirmDialogState{},
      std::span<const CreatureRenderInstance>{},
      std::span<const ItemDropRenderInstance>{}, ShipRenderState{},
      PlayerProgressionState{}, false, BackroomsFlashlightHudView{},
      GameplayHudAnnouncementView{}, MaritimeHudView{},
      CommandConsoleView{}, EnvironmentState{}, 1280, 720);
  CHECK_FALSE(renderer.legendary_presentation_stats().pending);
  CHECK(renderer.legendary_presentation_stats().staged_viewmodel_parts == 0U);
}

TEST_CASE("leviathan visual events own and render every geometric family") {
  LeviathanCombatVisualRequest request{};
  request.origin = {12.0F, 74.0F, -8.0F};
  request.direction = {0.25F, 0.08F, -1.0F};
  request.attack = ColossalAttackKind::ChargedExecution;
  request.attack_progress = 0.88F;
  request.landed = true;
  request.sectioned = true;
  request.weight = LeviathanImpactWeight::BossOrSection;
  request.awakening = LegendaryWeaponAwakening::Awakened;
  auto events = build_leviathan_visual_events(request);
  REQUIRE(events.size() == 5U);

  RendererLegendaryPresentationFrame frame{};
  frame.leviathan_visual_events = events;
  Renderer renderer{};
  renderer.set_legendary_presentation(frame);

  const auto stats = renderer.legendary_presentation_stats();
  CHECK(stats.accepted_visual_events == 5U);
  CHECK(stats.rendered_visual_events == 3U);
  CHECK(stats.accepted_camera_impulses == 1U);
  CHECK(stats.accepted_visual_hit_stops == 1U);
  CHECK(stats.staged_visual_events == 5U);
  CHECK(stats.staged_visual_event_parts >= 20U);
  CHECK(stats.staged_world_parts == stats.staged_visual_event_parts);
  CHECK(stats.pending);

  const auto &owned = renderer.leviathan_visual_event_snapshot();
  REQUIRE(owned.event_view().size() == 5U);
  const auto camera =
      owned.first(LeviathanVisualEventKind::CameraImpulse);
  const auto hit_stop =
      owned.first(LeviathanVisualEventKind::VisualHitStop);
  REQUIRE(camera.has_value());
  REQUIRE(hit_stop.has_value());
  CHECK(camera->intensity > 0.0F);
  CHECK(hit_stop->duration_seconds >= 0.070F);
  CHECK(std::all_of(
      owned.event_view().begin(), owned.event_view().end(),
      [](const LeviathanVisualEvent &event) {
        return event.visual_only && std::isfinite(event.position.x) &&
               std::isfinite(event.position.y) &&
               std::isfinite(event.position.z) &&
               glm::length(event.direction) == doctest::Approx(1.0F);
      }));

  // Je supprime le vecteur appelant pour vérifier que les impulsions de
  // caméra et le hit-stop restent des valeurs possédées, jamais des vues.
  events.front().position.x = std::numeric_limits<float>::quiet_NaN();
  events.clear();
  CHECK(owned.event_view().size() == 5U);
  CHECK(owned.first(LeviathanVisualEventKind::VisualHitStop).has_value());

  renderer.clear_legendary_presentation();
  CHECK(renderer.leviathan_visual_event_snapshot().event_view().empty());
  CHECK(renderer.legendary_presentation_stats().staged_visual_events == 0U);
  CHECK(renderer.legendary_presentation_stats().staged_visual_event_parts ==
        0U);
}

TEST_CASE("leviathan visual event bridge is bounded and rejects simulation") {
  LeviathanVisualEvent valid{};
  valid.kind = LeviathanVisualEventKind::Trail;
  valid.position = {3.0F, 72.0F, 4.0F};
  valid.direction = {0.0F, 0.0F, -1.0F};
  valid.color = {0.4F, 0.7F, 1.0F, 0.8F};
  valid.radius = 1.5F;
  valid.duration_seconds = 0.11F;
  valid.intensity = 0.8F;
  valid.visual_only = true;
  std::vector<LeviathanVisualEvent> events(
      kRendererMaximumLeviathanVisualEvents + 4U, valid);

  events[0].kind = static_cast<LeviathanVisualEventKind>(255U);
  events[1].position.x = std::numeric_limits<float>::infinity();
  events[2].visual_only = false;
  events[3].direction = {0.0F, 0.0F, 0.0F};
  events[3].color = {-2.0F, 3.0F, 0.5F, 4.0F};
  events[3].radius = 99.0F;
  events[3].duration_seconds = 9.0F;
  events[3].intensity = 9.0F;
  events[3].particle_count = 65'000U;

  RendererLegendaryPresentationFrame frame{};
  frame.leviathan_visual_events = events;
  Renderer renderer{};
  renderer.set_legendary_presentation(frame);

  const auto stats = renderer.legendary_presentation_stats();
  CHECK(stats.accepted_visual_events ==
        kRendererMaximumLeviathanVisualEvents - 3U);
  CHECK(stats.rendered_visual_events ==
        kRendererMaximumLeviathanVisualEvents - 3U);
  CHECK(stats.dropped_submissions >= 7U);
  const auto &owned = renderer.leviathan_visual_event_snapshot();
  REQUIRE(owned.event_view().size() ==
          kRendererMaximumLeviathanVisualEvents - 3U);
  const auto &sanitized = owned.event_view().front();
  CHECK(sanitized.direction.x == doctest::Approx(0.0F));
  CHECK(sanitized.direction.y == doctest::Approx(1.0F));
  CHECK(sanitized.direction.z == doctest::Approx(0.0F));
  CHECK(sanitized.color.r == doctest::Approx(0.0F));
  CHECK(sanitized.color.g == doctest::Approx(1.0F));
  CHECK(sanitized.color.a == doctest::Approx(1.0F));
  CHECK(sanitized.radius == doctest::Approx(8.0F));
  CHECK(sanitized.duration_seconds == doctest::Approx(2.0F));
  CHECK(sanitized.intensity == doctest::Approx(2.0F));
  CHECK(sanitized.particle_count == 0U);
}

TEST_CASE("legendary renderer reserves append only astral boss presentation") {
  LegendaryEnemyRenderSnapshot boss{};
  boss.archetype = static_cast<LegendaryEnemyArchetype>(6U);
  boss.behavior = LegendaryEnemyBehavior::Telegraph;
  boss.alive = true;
  boss.position = {4.0F, 72.0F, -9.0F};
  boss.health_ratio = 0.72F;
  boss.stagger_ratio = 0.35F;
  boss.astral_intangible = true;

  RendererLegendaryPresentationFrame frame{};
  frame.legendary_enemies = {&boss, 1U};
  Renderer renderer{};
  renderer.set_legendary_presentation(frame);

  const auto stats = renderer.legendary_presentation_stats();
  CHECK(stats.accepted_enemy_snapshots == 1U);
  CHECK(stats.staged_world_parts >= 10U);
  CHECK(stats.dropped_submissions == 0U);
  CHECK(stats.pending);
}

TEST_CASE("issou renderer owns bounded hud and result copies without OpenGL") {
  IssouArenaHudInput input{};
  input.phase = IssouArenaPhase::Combat;
  input.viewport_width = 1600.0F;
  input.viewport_height = 900.0F;
  input.boss_health_ratio = 0.65F;
  input.boss_stagger_ratio = 0.42F;
  input.weapon_stability_ratio = 0.74F;
  input.charge_ratio = 0.81F;
  input.momentum = 3U;
  input.accessibility.interface_scale = 1.45F;
  input.accessibility.opacity = 0.61F;
  input.accessibility.high_contrast = true;
  auto hud = build_issou_arena_hud(input);
  REQUIRE(hud.size() == kRendererMaximumIssouHudElements);

  IssouArenaCombatStatistics combat{};
  combat.combat_seconds = 42.5F;
  combat.damage_dealt = 730.0F;
  combat.limbs_severed = 3U;
  combat.perfect_guards = 4U;
  combat.missed_attacks = 2U;
  combat.maximum_momentum = 3U;
  combat.maximum_targets_hit = 6U;
  combat.executed = true;
  auto results = build_issou_results(combat, true);
  REQUIRE(results.lines.size() == kRendererMaximumIssouResultLines);

  RendererLegendaryPresentationFrame frame{};
  frame.issou_hud = hud;
  frame.issou_results = results;
  Renderer renderer{};
  renderer.set_legendary_presentation(frame);

  const auto submitted = renderer.legendary_presentation_stats();
  CHECK(submitted.accepted_hud_elements ==
        kRendererMaximumIssouHudElements);
  CHECK(submitted.accepted_result_lines ==
        kRendererMaximumIssouResultLines);
  CHECK(submitted.accepted_result_presentations == 1U);
  CHECK(submitted.staged_hud_elements == kRendererMaximumIssouHudElements);
  CHECK(submitted.staged_result_lines == kRendererMaximumIssouResultLines);
  CHECK(submitted.pending);

  const auto &owned = renderer.issou_hud_snapshot();
  REQUIRE(owned.element_view().size() == kRendererMaximumIssouHudElements);
  REQUIRE(owned.result_line_view().size() ==
          kRendererMaximumIssouResultLines);
  CHECK(owned.results_visible);
  CHECK(owned.victory);
  CHECK(owned.executed);
  CHECK(owned.element_view().front().value == doctest::Approx(0.65F));
  CHECK(owned.element_view().front().background.a ==
        doctest::Approx(0.61F));
  CHECK(owned.result_line_view()[1].value == doctest::Approx(730.0F));

  // Je détruis les sources pour prouver que la soumission n'en garde aucune
  // vue et que les résultats restent entièrement possédés par Renderer.
  hud.front().value = std::numeric_limits<float>::quiet_NaN();
  hud.clear();
  results.lines.clear();
  frame.issou_results->lines.clear();
  CHECK(owned.element_view().front().value == doctest::Approx(0.65F));
  CHECK(owned.result_line_view()[1].value == doctest::Approx(730.0F));

  const auto geometry =
      build_renderer_issou_hud_geometry(owned, 1600, 900);
  REQUIRE_FALSE(geometry.empty());
  CHECK(geometry.size() % 3U == 0U);
  CHECK(std::all_of(
      geometry.begin(), geometry.end(), [](const HudVertex &vertex) {
        return std::isfinite(vertex.x) && std::isfinite(vertex.y) &&
               std::isfinite(vertex.u) && std::isfinite(vertex.v) &&
               std::isfinite(vertex.r) && std::isfinite(vertex.g) &&
               std::isfinite(vertex.b) && std::isfinite(vertex.a) &&
               vertex.x >= -1.10F && vertex.x <= 1.10F &&
               vertex.y >= -1.10F && vertex.y <= 1.10F &&
               vertex.r >= 0.0F && vertex.r <= 1.0F &&
               vertex.g >= 0.0F && vertex.g <= 1.0F &&
               vertex.b >= 0.0F && vertex.b <= 1.0F &&
               vertex.a >= 0.0F && vertex.a <= 1.0F;
      }));

  renderer.clear_legendary_presentation();
  CHECK(renderer.issou_hud_snapshot().element_view().empty());
  CHECK(renderer.issou_hud_snapshot().result_line_view().empty());
  CHECK_FALSE(renderer.issou_hud_snapshot().results_visible);
  CHECK(renderer.legendary_presentation_stats().staged_hud_elements == 0U);
  CHECK(renderer.legendary_presentation_stats().staged_result_lines == 0U);
  CHECK_FALSE(renderer.legendary_presentation_stats().pending);
}

TEST_CASE("issou renderer sanitizes duplicate malformed and oversized hud") {
  auto hud = build_issou_arena_hud(IssouArenaHudInput{
      .phase = IssouArenaPhase::Countdown,
      .viewport_width = 1280.0F,
      .viewport_height = 720.0F,
      .countdown_seconds = 7.25F,
  });
  REQUIRE(hud.size() == kRendererMaximumIssouHudElements);
  hud.front().value = 4.0F;
  hud.front().foreground = {-2.0F, 3.0F, 0.5F, 2.0F};
  hud[1].kind = hud.front().kind;
  hud[2].kind = static_cast<IssouHudElementKind>(255U);
  hud[3].rect.width = std::numeric_limits<float>::quiet_NaN();

  IssouResultsPresentation results{};
  results.victory = false;
  results.lines = {
      {IssouResultMetric::CombatSeconds, 12.25F, false},
      {IssouResultMetric::CombatSeconds, 99.0F, true},
      {static_cast<IssouResultMetric>(255U), 4.0F, false},
      {IssouResultMetric::DamageDealt,
       std::numeric_limits<float>::infinity(), false},
  };

  RendererLegendaryPresentationFrame frame{};
  frame.issou_hud = hud;
  frame.issou_results = results;
  Renderer renderer{};
  renderer.set_legendary_presentation(frame);

  const auto stats = renderer.legendary_presentation_stats();
  CHECK(stats.accepted_hud_elements == 3U);
  CHECK(stats.accepted_result_lines == 1U);
  CHECK(stats.accepted_result_presentations == 1U);
  CHECK(stats.dropped_submissions >= 6U);
  const auto &owned = renderer.issou_hud_snapshot();
  REQUIRE(owned.element_view().size() == 3U);
  CHECK(owned.element_view().front().value == doctest::Approx(1.0F));
  CHECK(owned.element_view().front().foreground.r == doctest::Approx(0.0F));
  CHECK(owned.element_view().front().foreground.g == doctest::Approx(1.0F));
  CHECK(owned.element_view().front().foreground.a == doctest::Approx(1.0F));
  REQUIRE(owned.result_line_view().size() == 1U);
  CHECK(owned.result_line_view().front().value == doctest::Approx(12.25F));

  const auto result_geometry =
      build_renderer_issou_hud_geometry(owned, 1280, 720);
  CHECK_FALSE(result_geometry.empty());

  const auto countdown_hud = build_issou_arena_hud(IssouArenaHudInput{
      .phase = IssouArenaPhase::Countdown,
      .viewport_width = 1280.0F,
      .viewport_height = 720.0F,
      .countdown_seconds = 7.25F,
  });
  const auto countdown = std::find_if(
      countdown_hud.begin(), countdown_hud.end(),
      [](const IssouHudElement &element) {
        return element.kind == IssouHudElementKind::Countdown;
      });
  REQUIRE(countdown != countdown_hud.end());
  auto countdown_only = RendererIssouHudSnapshot{};
  countdown_only.elements[0] = *countdown;
  countdown_only.element_count = 1U;
  CHECK_FALSE(
      build_renderer_issou_hud_geometry(countdown_only, 1280, 720).empty());
}

TEST_CASE("leviathan viewmodel fallback never renders the weapon as a cube") {
  const auto air = to_block_id(BlockType::Air);
  const auto stone = to_block_id(BlockType::Stone);
  const auto leviathan = to_block_id(BlockType::LeviathanSpine);

  CHECK(resolve_renderer_viewmodel_item(air, false) == air);
  CHECK(resolve_renderer_viewmodel_item(stone, false) == stone);
  CHECK(resolve_renderer_viewmodel_item(leviathan, false) == air);
  CHECK(resolve_renderer_viewmodel_item(leviathan, true) == air);
  CHECK(resolve_renderer_viewmodel_item(stone, true) == air);
}

} // namespace valcraft
