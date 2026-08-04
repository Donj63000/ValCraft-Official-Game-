#include "render/BackroomsFlicker.h"

#include "world/World.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace valcraft {

namespace {

void load_fixture_test_chunks(
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

} // namespace

TEST_CASE("Jack retrouve exactement la rampe chargee la plus proche") {
  constexpr auto seed = 1337;
  constexpr auto radius = 48;
  World world {
      seed,
      5,
      WorldGenerationProfile::Backrooms,
      WorldGenerationVersion::BackroomsV4,
      VisualPipeline::LegacyVoxel,
      0,
  };
  const auto spawn = world.backrooms_anchor_spawn_block();
  load_fixture_test_chunks(world, spawn.x, spawn.z, radius);
  const auto query_x = static_cast<double>(spawn.x) + 0.37;
  const auto query_z = static_cast<double>(spawn.z) + 0.61;
  const auto candidates = world.query_backrooms_light_fixtures(
      query_x,
      query_z,
      radius,
      0,
      true);
  REQUIRE_FALSE(candidates.fixtures.empty());

  const auto expected = std::min_element(
      candidates.fixtures.begin(),
      candidates.fixtures.end(),
      [query_x, query_z](const auto &lhs, const auto &rhs) noexcept {
        const auto lhs_x =
            static_cast<double>(lhs.position_x) - query_x;
        const auto lhs_z =
            static_cast<double>(lhs.position_z) - query_z;
        const auto rhs_x =
            static_cast<double>(rhs.position_x) - query_x;
        const auto rhs_z =
            static_cast<double>(rhs.position_z) - query_z;
        const auto lhs_distance = lhs_x * lhs_x + lhs_z * lhs_z;
        const auto rhs_distance = rhs_x * rhs_x + rhs_z * rhs_z;
        if (lhs_distance != rhs_distance) {
          return lhs_distance < rhs_distance;
        }
        if (lhs.id.segment_anchor_z != rhs.id.segment_anchor_z) {
          return lhs.id.segment_anchor_z < rhs.id.segment_anchor_z;
        }
        return lhs.id.segment_anchor_x < rhs.id.segment_anchor_x;
      });
  const auto actual = find_nearest_backrooms_light_fixture(
      world,
      query_x,
      query_z,
      radius,
      0);

  REQUIRE(actual.has_value());
  CHECK(actual->fixture_id == expected->id);
  CHECK(actual->position_x == expected->position_x);
  CHECK(actual->position_y == expected->position_y);
  CHECK(actual->position_z == expected->position_z);
  CHECK(std::isfinite(actual->position_x));
  CHECK(std::isfinite(actual->position_y));
  CHECK(std::isfinite(actual->position_z));
}

TEST_CASE("Jack peut perturber une rampe de secours chargee") {
  World world {
      1337,
      6,
      WorldGenerationProfile::Backrooms,
      WorldGenerationVersion::BackroomsV4,
      VisualPipeline::LegacyVoxel,
      0,
  };
  load_fixture_test_chunks(world, 32, 32, 64);
  const auto fixtures =
      world.query_backrooms_light_fixtures(32.5, 32.5, 64, 0, true);
  const auto emergency = std::find_if(
      fixtures.fixtures.begin(),
      fixtures.fixtures.end(),
      [](const auto &fixture) noexcept {
        return fixture.id.state == BackroomsLightState::Emergency;
      });
  REQUIRE(emergency != fixtures.fixtures.end());

  const auto found = find_nearest_backrooms_light_fixture(
      world,
      emergency->position_x,
      emergency->position_z,
      0,
      0);
  REQUIRE(found.has_value());
  CHECK(found->fixture_id == emergency->id);
  CHECK(found->fixture_id.state == BackroomsLightState::Emergency);
}

TEST_CASE("la recherche de rampe borne son rayon et assainit les entrees") {
  World world {
      7331,
      6,
      WorldGenerationProfile::Backrooms,
      WorldGenerationVersion::BackroomsV4,
      VisualPipeline::LegacyVoxel,
      0,
  };
  const auto spawn = world.backrooms_anchor_spawn_block();
  load_fixture_test_chunks(world, spawn.x, spawn.z, 80);
  const auto position_x = static_cast<double>(spawn.x) + 0.5;
  const auto position_z = static_cast<double>(spawn.z) + 0.5;

  CHECK(find_nearest_backrooms_light_fixture(
            world,
            position_x,
            position_z,
            std::numeric_limits<int>::max(),
            0) ==
        find_nearest_backrooms_light_fixture(
            world,
            position_x,
            position_z,
            kMaximumBackroomsFixtureSearchRadius,
            0));
  CHECK(find_nearest_backrooms_light_fixture(
            world,
            position_x,
            position_z,
            -500,
            0) ==
        find_nearest_backrooms_light_fixture(
            world,
            position_x,
            position_z,
            0,
            0));
  CHECK_FALSE(find_nearest_backrooms_light_fixture(
                  world,
                  std::numeric_limits<double>::quiet_NaN(),
                  position_z,
                  32,
                  0)
                  .has_value());
  CHECK_FALSE(find_nearest_backrooms_light_fixture(
                  world,
                  position_x,
                  std::numeric_limits<double>::infinity(),
                  32,
                  0)
                  .has_value());
}

TEST_CASE("la recherche de rampe reste sure aux limites du monde signe") {
  World world {
      1337,
      1,
      WorldGenerationProfile::Backrooms,
      WorldGenerationVersion::BackroomsV4,
      VisualPipeline::LegacyVoxel,
      0,
  };
  CHECK_FALSE(find_nearest_backrooms_light_fixture(
                  world,
                  static_cast<double>(std::numeric_limits<int>::lowest()),
                  static_cast<double>(std::numeric_limits<int>::lowest()),
                  std::numeric_limits<int>::max(),
                  0)
                  .has_value());
  CHECK_FALSE(find_nearest_backrooms_light_fixture(
                  world,
                  static_cast<double>(std::numeric_limits<int>::max()),
                  static_cast<double>(std::numeric_limits<int>::max()),
                  std::numeric_limits<int>::max(),
                  0)
                  .has_value());
}

} // namespace valcraft
