#include "render/BackroomsFlicker.h"

#include "world/World.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <set>
#include <tuple>
#include <utility>

namespace valcraft {

namespace {

void load_backrooms_chunks(
    World &world,
    int center_x,
    int center_z,
    int radius) {
  const auto minimum =
      world.world_to_chunk(center_x - radius, center_z - radius);
  const auto maximum =
      world.world_to_chunk(center_x + radius, center_z + radius);
  for (auto chunk_z = minimum.z; chunk_z <= maximum.z; ++chunk_z) {
    for (auto chunk_x = minimum.x; chunk_x <= maximum.x; ++chunk_x) {
      world.ensure_chunk_loaded({chunk_x, chunk_z});
    }
  }
}

[[nodiscard]] auto expected_pool_profile(
    WorldGenerationVersion version) noexcept
    -> BackroomsPoolGeometryProfile {
  return version == WorldGenerationVersion::BackroomsV4
             ? BackroomsPoolGeometryProfile::FloodedDistrictsV4
             : version == WorldGenerationVersion::BackroomsV3
                   ? BackroomsPoolGeometryProfile::RecessedOneBlock
                   : BackroomsPoolGeometryProfile::LegacyFlat;
}

[[nodiscard]] auto has_same_nominal_fixture(
    const BackroomsLightFixture &fixture,
    const BackroomsFixtureId &identity) noexcept -> bool {
  return fixture.id.seed == identity.seed &&
         fixture.id.logical_level == identity.logical_level &&
         fixture.id.module_x == identity.module_x &&
         fixture.id.module_z == identity.module_z &&
         fixture.id.nominal_anchor_x == identity.nominal_anchor_x &&
         fixture.id.nominal_anchor_z == identity.nominal_anchor_z &&
         fixture.id.physical_ceiling_y == identity.physical_ceiling_y &&
         fixture.id.primary_axis_x == identity.primary_axis_x &&
         fixture.id.generation_version == identity.generation_version;
}

[[nodiscard]] auto find_nominal_fixture(
    const BackroomsLightFixtureQuery &query,
    const BackroomsFixtureId &identity)
    -> std::vector<BackroomsLightFixture>::const_iterator {
  return std::find_if(
      query.fixtures.begin(),
      query.fixtures.end(),
      [&identity](const auto &fixture) noexcept {
        return has_same_nominal_fixture(fixture, identity);
      });
}

struct IndexedCrossChunkFixture {
  BackroomsLightFixture fixture {};
  ChunkCoord indexed_chunk {};
  ChunkCoord neighbor_chunk {};
};

[[nodiscard]] auto find_indexed_cross_chunk_fixture(
    int seed,
    bool require_segment_crossing)
    -> std::optional<IndexedCrossChunkFixture> {
  World discovery {
      seed,
      3,
      WorldGenerationProfile::Backrooms,
      WorldGenerationVersion::BackroomsV4,
      VisualPipeline::LegacyVoxel,
      0,
  };
  for (auto chunk_z = -2; chunk_z <= 2; ++chunk_z) {
    for (auto chunk_x = -2; chunk_x <= 2; ++chunk_x) {
      const ChunkCoord candidate_chunk {chunk_x, chunk_z};
      discovery.ensure_chunk_loaded(candidate_chunk);
      const auto center_x = chunk_x * kChunkSizeX + kChunkSizeX / 2;
      const auto center_z = chunk_z * kChunkSizeZ + kChunkSizeZ / 2;
      const auto query = discovery.query_backrooms_light_fixtures(
          static_cast<double>(center_x) + 0.5,
          static_cast<double>(center_z) + 0.5,
          kChunkSizeX,
          0,
          true);
      for (const auto &fixture : query.fixtures) {
        const auto nominal_end_x =
            fixture.id.nominal_anchor_x +
            (fixture.id.primary_axis_x
                 ? kBackroomsLightFixtureNominalLength - 1
                 : 0);
        const auto nominal_end_z =
            fixture.id.nominal_anchor_z +
            (fixture.id.primary_axis_x
                 ? 0
                 : kBackroomsLightFixtureNominalLength - 1);
        const auto nominal_anchor_chunk = discovery.world_to_chunk(
            fixture.id.nominal_anchor_x,
            fixture.id.nominal_anchor_z);
        const auto nominal_end_chunk =
            discovery.world_to_chunk(nominal_end_x, nominal_end_z);
        const auto segment_end_x =
            fixture.id.segment_anchor_x +
            (fixture.id.primary_axis_x
                 ? fixture.id.segment_length - 1
                 : 0);
        const auto segment_end_z =
            fixture.id.segment_anchor_z +
            (fixture.id.primary_axis_x
                 ? 0
                 : fixture.id.segment_length - 1);
        const auto segment_anchor_chunk = discovery.world_to_chunk(
            fixture.id.segment_anchor_x,
            fixture.id.segment_anchor_z);
        const auto segment_end_chunk =
            discovery.world_to_chunk(segment_end_x, segment_end_z);
        if (nominal_anchor_chunk == nominal_end_chunk ||
            (nominal_anchor_chunk != candidate_chunk &&
             nominal_end_chunk != candidate_chunk) ||
            (segment_anchor_chunk != candidate_chunk &&
             segment_end_chunk != candidate_chunk) ||
            (require_segment_crossing &&
             segment_anchor_chunk == segment_end_chunk)) {
          continue;
        }
        return IndexedCrossChunkFixture {
            fixture,
            candidate_chunk,
            require_segment_crossing
                ? (segment_anchor_chunk == candidate_chunk
                       ? segment_end_chunk
                       : segment_anchor_chunk)
                : (nominal_anchor_chunk == candidate_chunk
                       ? nominal_end_chunk
                       : nominal_anchor_chunk),
        };
      }
    }
  }
  return std::nullopt;
}

} // namespace

TEST_CASE("Backrooms flicker selection stays deterministic and genuinely rare") {
  constexpr auto kSampleSide = 400;
  auto selected = std::size_t {0U};
  auto changed_with_seed = std::size_t {0U};
  for (auto z = 0; z < kSampleSide; ++z) {
    for (auto x = 0; x < kSampleSide; ++x) {
      const auto first = backrooms_fixture_can_flicker(1337, x * 4, z * 4);
      const auto repeated = backrooms_fixture_can_flicker(1337, x * 4, z * 4);
      const auto other_seed = backrooms_fixture_can_flicker(7331, x * 4, z * 4);
      CHECK(first == repeated);
      selected += first ? 1U : 0U;
      changed_with_seed += first != other_seed ? 1U : 0U;
    }
  }

  const auto sample_count =
      static_cast<std::size_t>(kSampleSide * kSampleSide);
  const auto rate =
      static_cast<double>(selected) / static_cast<double>(sample_count);
  CHECK(rate > 0.010);
  CHECK(rate < 0.018);
  CHECK(changed_with_seed > 3'000U);
}

TEST_CASE("Backrooms ballast bursts are bounded irregular and reproducible") {
  constexpr auto kSeed = 1337;
  constexpr auto kAnchorX = -37;
  constexpr auto kAnchorZ = 91;
  const auto schedule =
      backrooms_flicker_schedule(kSeed, kAnchorX, kAnchorZ);

  CHECK(schedule.period_seconds >= 21.0F);
  CHECK(schedule.period_seconds < 34.0F);
  CHECK(schedule.burst_seconds >= 0.55F);
  CHECK(schedule.burst_seconds <= 1.0F);
  CHECK(schedule.phase_seconds >= 0.0F);
  CHECK(schedule.phase_seconds < schedule.period_seconds);

  const auto burst_start = std::fmod(
      schedule.period_seconds - schedule.phase_seconds,
      schedule.period_seconds);
  auto minimum = 1.0F;
  auto maximum = 0.0F;
  for (auto sample_index = 0; sample_index < 60; ++sample_index) {
    const auto time =
        burst_start + schedule.burst_seconds *
                          static_cast<float>(sample_index) / 60.0F;
    const auto intensity =
        backrooms_flicker_intensity(kSeed, kAnchorX, kAnchorZ, time);
    CHECK(std::isfinite(intensity));
    CHECK(intensity >= 0.05F);
    CHECK(intensity <= 1.0F);
    CHECK(intensity ==
          backrooms_flicker_intensity(kSeed, kAnchorX, kAnchorZ, time));
    minimum = std::min(minimum, intensity);
    maximum = std::max(maximum, intensity);
  }
  CHECK(minimum <= 0.07F);
  CHECK(maximum >= 0.92F);
  CHECK(backrooms_flicker_intensity(
            kSeed,
            kAnchorX,
            kAnchorZ,
            burst_start + schedule.burst_seconds + 0.02F) ==
        doctest::Approx(1.0F));

  const auto invalid = backrooms_flicker_intensity(
      kSeed,
      kAnchorX,
      kAnchorZ,
      std::numeric_limits<float>::quiet_NaN());
  CHECK(std::isfinite(invalid));
  CHECK(invalid >= 0.05F);
  CHECK(invalid <= 1.0F);
}

TEST_CASE("Backrooms world index exposes versioned real fixture segments") {
  constexpr auto kSeed = 1337;
  World world {
      kSeed,
      6,
      WorldGenerationProfile::Backrooms,
      WorldGenerationVersion::BackroomsV4,
      VisualPipeline::LegacyVoxel,
      0,
  };
  const auto spawn = world.backrooms_anchor_spawn_block();
  load_backrooms_chunks(world, spawn.x, spawn.z, 48);

  const auto query = world.query_backrooms_light_fixtures(
      static_cast<double>(spawn.x) + 0.5,
      static_cast<double>(spawn.z) + 0.5,
      48,
      0,
      true);
  REQUIRE_FALSE(query.fixtures.empty());
  CHECK(query.inspected_fixture_candidates > 0U);
  CHECK(query.inspected_fixture_candidates < 12'961U);
  CHECK(query.inspected_fixture_footprint_cells <=
        query.inspected_fixture_candidates *
            static_cast<std::size_t>(kBackroomsLightFixtureNominalLength));
  CHECK(query.inspected_emissive_cells == 0U);

  std::set<std::tuple<int, int, int, int, int>> unique_segments {};
  for (const auto &fixture : query.fixtures) {
    CAPTURE(fixture.id.segment_anchor_x);
    CAPTURE(fixture.id.segment_anchor_z);
    CHECK(fixture.id.valid);
    CHECK(fixture.id.seed == kSeed);
    CHECK(fixture.id.logical_level == 0);
    CHECK(fixture.id.connector_district_modules ==
          kBackroomsSpatialConnectorDistrictModules);
    CHECK(fixture.id.pool_geometry_profile ==
          BackroomsPoolGeometryProfile::FloodedDistrictsV4);
    CHECK(fixture.id.generation_version ==
          WorldGenerationVersion::BackroomsV4);
    CHECK(fixture.id.segment_length >= 1);
    CHECK(fixture.id.segment_length <= 4);
    CHECK(world.get_block(
              fixture.id.segment_anchor_x,
              fixture.id.physical_ceiling_y,
              fixture.id.segment_anchor_z) == fixture.id.block);
    CHECK(unique_segments
              .insert({
                  fixture.id.logical_level,
                  fixture.id.physical_ceiling_y,
                  fixture.id.segment_anchor_x,
                  fixture.id.segment_anchor_z,
                  fixture.id.segment_length,
              })
              .second);
  }
}

TEST_CASE("Backrooms fixture index tracks all stack profiles and levels") {
  constexpr std::array versions {
      WorldGenerationVersion::BackroomsV2,
      WorldGenerationVersion::BackroomsV3,
      WorldGenerationVersion::BackroomsV4,
  };
  for (const auto version : versions) {
    CAPTURE(static_cast<std::uint32_t>(version));
    World world {
        424242,
        4,
        WorldGenerationProfile::Backrooms,
        version,
        VisualPipeline::LegacyVoxel,
        0,
    };
    load_backrooms_chunks(world, 32, 32, 32);

    for (auto logical_level = -2; logical_level <= 2; ++logical_level) {
      CAPTURE(logical_level);
      const auto context =
          world.backrooms_generation_context(logical_level);
      REQUIRE(context.has_value());
      CHECK(context->connector_district_modules ==
            kBackroomsSpatialConnectorDistrictModules);
      CHECK(context->pool_geometry_profile == expected_pool_profile(version));
      CHECK(context->generation_version == version);

      const auto query = world.query_backrooms_light_fixtures(
          32.5,
          32.5,
          32,
          logical_level,
          true);
      REQUIRE_FALSE(query.fixtures.empty());
      // Je fixe un budget structurel dix fois inferieur au balayage 161x161.
      // Les probes internes restent visibles et ne sont pas masques par le
      // compteur des seuls candidats retenus.
      CHECK(query.inspected_fixture_candidates > 0U);
      CHECK(query.inspected_fixture_candidates < 2'592U);
      CHECK(query.inspected_fixture_footprint_cells >=
            query.fixtures.size());
      CHECK(query.inspected_fixture_footprint_cells <=
            query.inspected_fixture_candidates *
                static_cast<std::size_t>(
                    kBackroomsLightFixtureNominalLength));
      CHECK(query.inspected_emissive_cells == 0U);
      for (const auto &fixture : query.fixtures) {
        CHECK(fixture.id.logical_level == logical_level);
        CHECK(fixture.id.pool_geometry_profile ==
              context->pool_geometry_profile);
        CHECK(fixture.id.generation_version == version);
        CHECK(fixture.id.physical_ceiling_y >= context->physical_floor_y + 4);
      }
    }
  }

  World legacy {
      424242,
      4,
      WorldGenerationProfile::Backrooms,
      WorldGenerationVersion::BackroomsV1,
      VisualPipeline::LegacyVoxel,
      0,
  };
  load_backrooms_chunks(legacy, 32, 32, 32);
  const auto legacy_context = legacy.backrooms_generation_context(0);
  REQUIRE(legacy_context.has_value());
  CHECK(legacy_context->connector_district_modules ==
        kBackroomsConnectorDistrictModules);
  CHECK(legacy_context->pool_geometry_profile ==
        BackroomsPoolGeometryProfile::LegacyFlat);
  const auto legacy_query = legacy.query_backrooms_light_fixtures(
      32.5,
      32.5,
      32,
      0,
      true);
  REQUIRE_FALSE(legacy_query.fixtures.empty());
  CHECK(legacy_query.inspected_fixture_candidates < 2'592U);
  CHECK(legacy_query.inspected_fixture_footprint_cells <=
        legacy_query.inspected_fixture_candidates *
            static_cast<std::size_t>(
                kBackroomsLightFixtureNominalLength));
  CHECK(legacy_query.inspected_emissive_cells == 0U);
  for (const auto &fixture : legacy_query.fixtures) {
    CHECK(fixture.id.logical_level == 0);
    CHECK(fixture.id.generation_version ==
          WorldGenerationVersion::BackroomsV1);
    CHECK(fixture.id.pool_geometry_profile ==
          BackroomsPoolGeometryProfile::LegacyFlat);
  }
}

TEST_CASE("Backrooms fixture identity center and schedule ignore neighbor loading") {
  constexpr auto kSeed = 5'903;
  const auto crossing_fixture =
      find_indexed_cross_chunk_fixture(kSeed, false);
  REQUIRE(crossing_fixture.has_value());

  World world {
      kSeed,
      3,
      WorldGenerationProfile::Backrooms,
      WorldGenerationVersion::BackroomsV4,
      VisualPipeline::LegacyVoxel,
      0,
  };
  world.ensure_chunk_loaded(crossing_fixture->indexed_chunk);
  const auto before_query = world.query_backrooms_light_fixtures(
      crossing_fixture->fixture.position_x,
      crossing_fixture->fixture.position_z,
      8,
      0,
      true);
  const auto before =
      find_nominal_fixture(before_query, crossing_fixture->fixture.id);
  REQUIRE(before != before_query.fixtures.end());
  const auto schedule_before = backrooms_flicker_schedule(before->id);

  const auto revision_before = world.backrooms_light_fixture_revision();
  world.ensure_chunk_loaded(crossing_fixture->neighbor_chunk);
  CHECK(world.backrooms_light_fixture_revision() > revision_before);
  const auto after_query = world.query_backrooms_light_fixtures(
      crossing_fixture->fixture.position_x,
      crossing_fixture->fixture.position_z,
      8,
      0,
      true);
  const auto after = find_nominal_fixture(after_query, before->id);
  REQUIRE(after != after_query.fixtures.end());
  CHECK(after->id == before->id);
  CHECK(after->position_x == before->position_x);
  CHECK(after->position_y == before->position_y);
  CHECK(after->position_z == before->position_z);
  CHECK(backrooms_flicker_schedule(after->id) == schedule_before);
  CHECK(std::count_if(
            after_query.fixtures.begin(),
            after_query.fixtures.end(),
            [&before](const auto &fixture) noexcept {
              return fixture.id == before->id;
            }) == 1);
  CHECK(after_query.inspected_emissive_cells == 0U);
}

TEST_CASE("Backrooms unloaded override restoration invalidates the fixture cache once") {
  constexpr auto kSeed = 5'903;
  const auto crossing = find_indexed_cross_chunk_fixture(kSeed, true);
  REQUIRE(crossing.has_value());

  const auto step_x = crossing->fixture.id.primary_axis_x ? 1 : 0;
  const auto step_z = crossing->fixture.id.primary_axis_x ? 0 : 1;
  auto removed_cell = BlockCoord {};
  auto found_neighbor_cell = false;
  World coordinate_world {
      kSeed,
      3,
      WorldGenerationProfile::Backrooms,
      WorldGenerationVersion::BackroomsV4,
      VisualPipeline::LegacyVoxel,
      0,
  };
  for (auto offset = 0;
       offset < crossing->fixture.id.segment_length;
       ++offset) {
    const BlockCoord candidate {
        crossing->fixture.id.segment_anchor_x + step_x * offset,
        crossing->fixture.id.physical_ceiling_y,
        crossing->fixture.id.segment_anchor_z + step_z * offset,
    };
    if (coordinate_world.world_to_chunk(candidate.x, candidate.z) ==
        crossing->neighbor_chunk) {
      removed_cell = candidate;
      found_neighbor_cell = true;
      break;
    }
  }
  REQUIRE(found_neighbor_cell);

  World source {
      kSeed,
      3,
      WorldGenerationProfile::Backrooms,
      WorldGenerationVersion::BackroomsV4,
      VisualPipeline::LegacyVoxel,
      0,
  };
  source.ensure_chunk_loaded(crossing->indexed_chunk);
  source.ensure_chunk_loaded(crossing->neighbor_chunk);
  source.set_block(
      removed_cell.x,
      removed_cell.y,
      removed_cell.z,
      to_block_id(BlockType::Air));
  const auto save_plan = source.capture_save_plan();
  const auto snapshots = source.modified_chunk_snapshots();
  REQUIRE(save_plan.chunks.size() == 1U);
  REQUIRE(snapshots.size() == 1U);

  World restored {
      kSeed,
      3,
      WorldGenerationProfile::Backrooms,
      WorldGenerationVersion::BackroomsV4,
      VisualPipeline::LegacyVoxel,
      0,
  };
  restored.ensure_chunk_loaded(crossing->indexed_chunk);
  REQUIRE_FALSE(restored.chunk_records().contains(crossing->neighbor_chunk));
  const auto original_query = restored.query_backrooms_light_fixtures(
      crossing->fixture.position_x,
      crossing->fixture.position_z,
      8,
      0,
      true);
  REQUIRE(std::find_if(
              original_query.fixtures.begin(),
              original_query.fixtures.end(),
              [&crossing](const auto &fixture) noexcept {
                return fixture.id == crossing->fixture.id;
              }) != original_query.fixtures.end());

  const auto revision_before_restore =
      restored.backrooms_light_fixture_revision();
  restored.begin_restore_save_plan(save_plan);
  CHECK(restored.backrooms_light_fixture_revision() ==
        revision_before_restore);
  while (restored.has_pending_save_restore()) {
    (void)restored.process_save_restore(
        kChunkVolume,
        std::numeric_limits<double>::infinity());
  }
  CHECK(restored.backrooms_light_fixture_revision() ==
        revision_before_restore + 1U);
  const auto overridden_query = restored.query_backrooms_light_fixtures(
      crossing->fixture.position_x,
      crossing->fixture.position_z,
      8,
      0,
      true);
  CHECK(std::none_of(
      overridden_query.fixtures.begin(),
      overridden_query.fixtures.end(),
      [&crossing](const auto &fixture) noexcept {
        return fixture.id == crossing->fixture.id;
      }));

  const auto revision_before_identical_replace =
      restored.backrooms_light_fixture_revision();
  restored.replace_chunk_snapshots(snapshots);
  CHECK(restored.backrooms_light_fixture_revision() ==
        revision_before_identical_replace);

  World pristine {
      kSeed,
      0,
      WorldGenerationProfile::Backrooms,
      WorldGenerationVersion::BackroomsV4,
      VisualPipeline::LegacyVoxel,
      0,
  };
  const auto revision_before_clear =
      restored.backrooms_light_fixture_revision();
  restored.begin_restore_save_plan(pristine.capture_save_plan());
  CHECK(restored.backrooms_light_fixture_revision() ==
        revision_before_clear + 1U);

  restored.begin_restore_save_plan(save_plan);
  while (restored.has_pending_save_restore()) {
    (void)restored.process_save_restore(
        kChunkVolume,
        std::numeric_limits<double>::infinity());
  }
  const auto revision_before_unloaded_restore =
      restored.backrooms_light_fixture_revision();
  REQUIRE(restored.restore_generated_cell(
      removed_cell.x,
      removed_cell.y,
      removed_cell.z));
  CHECK(restored.backrooms_light_fixture_revision() ==
        revision_before_unloaded_restore + 1U);

  restored.begin_restore_save_plan(save_plan);
  while (restored.has_pending_save_restore()) {
    (void)restored.process_save_restore(
        kChunkVolume,
        std::numeric_limits<double>::infinity());
  }
  const auto revision_before_install =
      restored.backrooms_light_fixture_revision();
  restored.ensure_chunk_loaded(crossing->neighbor_chunk);
  CHECK(restored.backrooms_light_fixture_revision() ==
        revision_before_install + 1U);
}

TEST_CASE("Backrooms real gaps split one nominal ramp into distinct segments") {
  World world {
      7193,
      5,
      WorldGenerationProfile::Backrooms,
      WorldGenerationVersion::BackroomsV4,
      VisualPipeline::LegacyVoxel,
      0,
  };
  load_backrooms_chunks(world, 32, 32, 48);
  const auto before =
      world.query_backrooms_light_fixtures(32.5, 32.5, 48, 0, true);
  const auto candidate = std::find_if(
      before.fixtures.begin(),
      before.fixtures.end(),
      [](const auto &fixture) noexcept {
        return fixture.id.segment_length >= 3;
      });
  REQUIRE(candidate != before.fixtures.end());

  const auto step_x = candidate->id.primary_axis_x ? 1 : 0;
  const auto step_z = candidate->id.primary_axis_x ? 0 : 1;
  const auto removed_x = candidate->id.segment_anchor_x + step_x;
  const auto removed_z = candidate->id.segment_anchor_z + step_z;
  const auto revision_before = world.backrooms_light_fixture_revision();
  world.set_block(
      removed_x,
      candidate->id.physical_ceiling_y,
      removed_z,
      to_block_id(BlockType::Air));
  CHECK(world.backrooms_light_fixture_revision() > revision_before);

  const auto after =
      world.query_backrooms_light_fixtures(32.5, 32.5, 48, 0, true);
  auto matching_segments = std::size_t {0U};
  std::set<std::pair<int, int>> segment_anchors {};
  for (const auto &fixture : after.fixtures) {
    if (fixture.id.module_x != candidate->id.module_x ||
        fixture.id.module_z != candidate->id.module_z ||
        fixture.id.nominal_anchor_x != candidate->id.nominal_anchor_x ||
        fixture.id.nominal_anchor_z != candidate->id.nominal_anchor_z) {
      continue;
    }
    ++matching_segments;
    segment_anchors.insert({
        fixture.id.segment_anchor_x,
        fixture.id.segment_anchor_z,
    });
  }
  CHECK(matching_segments == 2U);
  CHECK(segment_anchors.size() == 2U);
}

TEST_CASE("Backrooms flicker field consumes loaded canonical fixtures") {
  constexpr auto kSeed = 1337;
  World world {
      kSeed,
      6,
      WorldGenerationProfile::Backrooms,
      WorldGenerationVersion::BackroomsV4,
      VisualPipeline::LegacyVoxel,
      0,
  };
  const auto spawn = world.backrooms_anchor_spawn_block();
  load_backrooms_chunks(world, spawn.x, spawn.z, 64);
  const auto field =
      collect_backrooms_flicker_field(world, spawn.x, spawn.z, 0);

  CHECK(field.count > 0U);
  CHECK(field.count <= kMaximumBackroomsFlickerLights);
  auto previous_distance = std::numeric_limits<double>::lowest();
  for (std::size_t index = 0U; index < field.count; ++index) {
    const auto &anchor = field.anchors[index];
    CHECK(anchor.fixture_id.valid);
    CHECK(anchor.fixture_id.state == BackroomsLightState::Active);
    CHECK(backrooms_fixture_can_flicker(anchor.fixture_id));
    CHECK(world.get_block(
              anchor.world_x,
              anchor.fixture_id.physical_ceiling_y,
              anchor.world_z) == anchor.fixture_id.block);
    const auto delta_x =
        static_cast<double>(anchor.position_x) -
        (static_cast<double>(spawn.x) + 0.5);
    const auto delta_z =
        static_cast<double>(anchor.position_z) -
        (static_cast<double>(spawn.z) + 0.5);
    const auto distance = delta_x * delta_x + delta_z * delta_z;
    CHECK(distance >= previous_distance);
    previous_distance = distance;
  }

  CHECK(sample_backrooms_flicker_lights(field, kSeed, 42.25F, 0) ==
        sample_backrooms_flicker_lights(field, kSeed, 42.25F, 0));
}

TEST_CASE("Backrooms loaded fixture queries reject signed world limits safely") {
  World world {
      1337,
      1,
      WorldGenerationProfile::Backrooms,
      WorldGenerationVersion::BackroomsV4,
      VisualPipeline::LegacyVoxel,
      0,
  };
  CHECK(world
            .query_backrooms_light_fixtures(
                static_cast<double>(std::numeric_limits<int>::lowest()),
                static_cast<double>(std::numeric_limits<int>::lowest()),
                std::numeric_limits<int>::max(),
                0,
                true)
            .fixtures.empty());
  CHECK(world
            .query_backrooms_light_fixtures(
                static_cast<double>(std::numeric_limits<int>::max()),
                static_cast<double>(std::numeric_limits<int>::max()),
                std::numeric_limits<int>::max(),
                0,
                true)
            .fixtures.empty());
}

} // namespace valcraft
