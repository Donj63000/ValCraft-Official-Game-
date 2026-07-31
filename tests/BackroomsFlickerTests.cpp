#include "render/BackroomsFlicker.h"

#include "world/BackroomsGenerator.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <set>
#include <utility>

namespace valcraft {

TEST_CASE("Backrooms flicker selection stays deterministic and genuinely rare") {
  constexpr auto kSampleSide = 400;
  auto selected = std::size_t{0U};
  auto changed_with_seed = std::size_t{0U};
  for (auto z = 0; z < kSampleSide; ++z) {
    for (auto x = 0; x < kSampleSide; ++x) {
      const auto first =
          backrooms_fixture_can_flicker(1337, x * 4, z * 4);
      const auto repeated =
          backrooms_fixture_can_flicker(1337, x * 4, z * 4);
      const auto other_seed =
          backrooms_fixture_can_flicker(7331, x * 4, z * 4);
      CHECK(first == repeated);
      selected += first ? 1U : 0U;
      changed_with_seed += first != other_seed ? 1U : 0U;
    }
  }

  const auto sample_count =
      static_cast<std::size_t>(kSampleSide * kSampleSide);
  const auto rate =
      static_cast<double>(selected) /
      static_cast<double>(sample_count);
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

  const auto burst_start =
      std::fmod(
          schedule.period_seconds -
              schedule.phase_seconds,
          schedule.period_seconds);
  auto minimum = 1.0F;
  auto maximum = 0.0F;
  for (auto sample_index = 0; sample_index < 60; ++sample_index) {
    const auto time =
        burst_start +
        schedule.burst_seconds *
            static_cast<float>(sample_index) /
            60.0F;
    const auto intensity =
        backrooms_flicker_intensity(
            kSeed,
            kAnchorX,
            kAnchorZ,
            time);
    CHECK(std::isfinite(intensity));
    CHECK(intensity >= 0.05F);
    CHECK(intensity <= 1.0F);
    CHECK(
        intensity ==
        backrooms_flicker_intensity(
            kSeed,
            kAnchorX,
            kAnchorZ,
            time));
    minimum = std::min(minimum, intensity);
    maximum = std::max(maximum, intensity);
  }
  CHECK(minimum <= 0.07F);
  CHECK(maximum >= 0.92F);
  CHECK(
      backrooms_flicker_intensity(
          kSeed,
          kAnchorX,
          kAnchorZ,
          burst_start +
              schedule.burst_seconds +
              0.02F) ==
      doctest::Approx(1.0F));

  const auto invalid =
      backrooms_flicker_intensity(
          kSeed,
          kAnchorX,
          kAnchorZ,
          std::numeric_limits<float>::quiet_NaN());
  CHECK(std::isfinite(invalid));
  CHECK(invalid >= 0.05F);
  CHECK(invalid <= 1.0F);
  CHECK(
      backrooms_flicker_intensity(
          kSeed,
          kAnchorX,
          kAnchorZ,
          -100.0F) ==
      backrooms_flicker_intensity(
          kSeed,
          kAnchorX,
          kAnchorZ,
          0.0F));
}

TEST_CASE("Backrooms flicker field contains only canonical active fixtures") {
  constexpr auto kSeed = 1337;
  const BackroomsGenerator generator{kSeed};
  const auto spawn = generator.spawn_block();
  const auto field =
      collect_backrooms_flicker_field(
          kSeed,
          spawn.x,
          spawn.z);

  CHECK(field.count > 0U);
  CHECK(field.count <= kMaximumBackroomsFlickerLights);
  std::set<std::pair<int, int>> unique_anchors{};
  auto previous_distance = std::numeric_limits<double>::lowest();
  for (std::size_t index = 0U; index < field.count; ++index) {
    const auto &anchor = field.anchors[index];
    const auto sample =
        generator.sample_column(
            anchor.world_x,
            anchor.world_z);
    const auto descriptor =
        generator.descriptor_at(
            anchor.world_x,
            anchor.world_z);
    const auto previous_x =
        anchor.world_x -
        (descriptor.primary_axis_x ? 1 : 0);
    const auto previous_z =
        anchor.world_z -
        (descriptor.primary_axis_x ? 0 : 1);

    CHECK(sample.light_state == BackroomsLightState::Active);
    CHECK(
        generator
            .sample_column(previous_x, previous_z)
            .light_state !=
        BackroomsLightState::Active);
    CHECK(
        backrooms_fixture_can_flicker(
            kSeed,
            anchor.world_x,
            anchor.world_z));
    CHECK(std::isfinite(anchor.position_x));
    CHECK(std::isfinite(anchor.position_y));
    CHECK(std::isfinite(anchor.position_z));
    CHECK(
        unique_anchors
            .insert({
                anchor.world_x,
                anchor.world_z,
            })
            .second);

    const auto delta_x =
        static_cast<double>(
            anchor.world_x -
            spawn.x);
    const auto delta_z =
        static_cast<double>(
            anchor.world_z -
            spawn.z);
    const auto distance =
        delta_x * delta_x +
        delta_z * delta_z;
    CHECK(distance >= previous_distance);
    previous_distance = distance;
  }

  const auto first_sample =
      sample_backrooms_flicker_lights(
          field,
          kSeed,
          42.25F);
  const auto repeated_sample =
      sample_backrooms_flicker_lights(
          field,
          kSeed,
          42.25F);
  for (std::size_t index = 0U; index < field.count; ++index) {
    CHECK(
        first_sample[index].position_x ==
        repeated_sample[index].position_x);
    CHECK(
        first_sample[index].position_y ==
        repeated_sample[index].position_y);
    CHECK(
        first_sample[index].position_z ==
        repeated_sample[index].position_z);
    CHECK(
        first_sample[index].intensity ==
        repeated_sample[index].intensity);
  }
}

TEST_CASE("Backrooms flicker scan is safe at signed world limits") {
  const auto minimum_field =
      collect_backrooms_flicker_field(
          1337,
          std::numeric_limits<int>::lowest(),
          std::numeric_limits<int>::lowest());
  const auto maximum_field =
      collect_backrooms_flicker_field(
          1337,
          std::numeric_limits<int>::max(),
          std::numeric_limits<int>::max());
  CHECK(minimum_field.count <= kMaximumBackroomsFlickerLights);
  CHECK(maximum_field.count <= kMaximumBackroomsFlickerLights);
}

} // namespace valcraft
