#include "render/BackroomsFlicker.h"

#include "world/World.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace valcraft {

namespace {

constexpr std::uint64_t kFixtureSelectionSalt = 0x4D9C7A31B682E5F9ULL;
constexpr std::uint64_t kFixtureTimingSalt = 0xA1F0D638C5724B9EULL;

[[nodiscard]] constexpr auto mix_bits(std::uint64_t value) noexcept
    -> std::uint64_t {
  value ^= value >> 30U;
  value *= 0xBF58476D1CE4E5B9ULL;
  value ^= value >> 27U;
  value *= 0x94D049BB133111EBULL;
  return value ^ (value >> 31U);
}

[[nodiscard]] constexpr auto fixture_hash(int seed, int world_x, int world_z,
                                          std::uint64_t salt) noexcept
    -> std::uint64_t {
  auto value = mix_bits(static_cast<std::uint32_t>(seed) ^ salt);
  value ^= mix_bits(static_cast<std::uint32_t>(world_x) +
                    0x9E3779B97F4A7C15ULL);
  value ^= mix_bits(static_cast<std::uint32_t>(world_z) +
                    0xD1B54A32D192ED03ULL);
  return mix_bits(value);
}

[[nodiscard]] constexpr auto combine_fixture_field(
    std::uint64_t hash,
    std::uint64_t field) noexcept -> std::uint64_t {
  return mix_bits(hash ^ mix_bits(field + 0x9E3779B97F4A7C15ULL));
}

[[nodiscard]] constexpr auto fixture_hash(
    const BackroomsFixtureId &fixture,
    std::uint64_t salt) noexcept -> std::uint64_t {
  auto hash = fixture_hash(
      fixture.seed,
      fixture.segment_anchor_x,
      fixture.segment_anchor_z,
      salt);
  hash = combine_fixture_field(
      hash, static_cast<std::uint32_t>(fixture.logical_level));
  hash = combine_fixture_field(
      hash, static_cast<std::uint32_t>(fixture.module_x));
  hash = combine_fixture_field(
      hash, static_cast<std::uint32_t>(fixture.module_z));
  hash = combine_fixture_field(
      hash, static_cast<std::uint32_t>(fixture.nominal_anchor_x));
  hash = combine_fixture_field(
      hash, static_cast<std::uint32_t>(fixture.nominal_anchor_z));
  hash = combine_fixture_field(
      hash, static_cast<std::uint32_t>(fixture.physical_ceiling_y));
  hash = combine_fixture_field(
      hash, static_cast<std::uint32_t>(fixture.connector_district_modules));
  hash = combine_fixture_field(
      hash, static_cast<std::uint32_t>(fixture.segment_length));
  hash = combine_fixture_field(
      hash, static_cast<std::uint32_t>(fixture.primary_axis_x));
  hash = combine_fixture_field(
      hash, static_cast<std::uint32_t>(fixture.theme));
  hash = combine_fixture_field(
      hash, static_cast<std::uint32_t>(fixture.archetype));
  hash = combine_fixture_field(
      hash, static_cast<std::uint32_t>(fixture.state));
  hash = combine_fixture_field(
      hash, static_cast<std::uint32_t>(fixture.pool_geometry_profile));
  hash = combine_fixture_field(
      hash, static_cast<std::uint32_t>(fixture.generation_version));
  return combine_fixture_field(hash, static_cast<std::uint32_t>(fixture.block));
}

[[nodiscard]] constexpr auto level_flicker_seed(
    int seed,
    int logical_level) noexcept -> int {
  // Je separe les rythmes de deux etages partageant les memes coordonnees.
  return static_cast<int>(
      static_cast<std::uint32_t>(seed) ^
      (static_cast<std::uint32_t>(logical_level) * UINT32_C(0x9E3779B9)));
}

[[nodiscard]] auto positive_remainder(float value, float period) noexcept
    -> float {
  const auto remainder = std::fmod(value, period);
  return remainder < 0.0F ? remainder + period : remainder;
}

[[nodiscard]] auto schedule_from_hash(std::uint64_t hash) noexcept
    -> BackroomsFlickerSchedule {
  const auto period_seconds =
      21.0F + static_cast<float>(hash % 1'300U) * 0.01F;
  const auto burst_seconds =
      0.55F + static_cast<float>((hash >> 12U) % 46U) * 0.01F;
  const auto normalized_phase =
      static_cast<float>((hash >> 24U) % 10'000U) / 10'000.0F;
  return {
      period_seconds,
      burst_seconds,
      normalized_phase * period_seconds,
  };
}

[[nodiscard]] auto intensity_from_schedule(
    BackroomsFlickerSchedule schedule,
    std::uint64_t pattern_offset,
    float elapsed_seconds) noexcept -> float {
  if (!std::isfinite(elapsed_seconds)) {
    elapsed_seconds = 0.0F;
  }
  const auto local_time = positive_remainder(
      std::max(elapsed_seconds, 0.0F) + schedule.phase_seconds,
      schedule.period_seconds);
  if (local_time >= schedule.burst_seconds) {
    return 1.0F;
  }

  // Je reproduis une amorce de ballast courte et irreguliere, mais chaque
  // segment canonique conserve son propre motif deterministe.
  constexpr std::array<float, 12U> kBallastPattern{{
      0.05F, 0.18F, 0.82F, 0.07F, 1.00F, 0.32F,
      0.09F, 0.68F, 0.14F, 0.92F, 0.06F, 0.44F,
  }};
  constexpr auto kFlickerTicksPerSecond = 22.0F;
  const auto tick = static_cast<std::uint64_t>(
      std::floor(local_time * kFlickerTicksPerSecond));
  return kBallastPattern[static_cast<std::size_t>(
      (tick + pattern_offset) % kBallastPattern.size())];
}

struct RankedAnchor {
  BackroomsFlickerAnchor anchor {};
  double distance_squared = 0.0;
};

[[nodiscard]] auto ranked_before(
    const RankedAnchor &lhs,
    const RankedAnchor &rhs) noexcept -> bool {
  if (lhs.distance_squared != rhs.distance_squared) {
    return lhs.distance_squared < rhs.distance_squared;
  }
  if (lhs.anchor.fixture_id.physical_ceiling_y !=
      rhs.anchor.fixture_id.physical_ceiling_y) {
    return lhs.anchor.fixture_id.physical_ceiling_y <
           rhs.anchor.fixture_id.physical_ceiling_y;
  }
  if (lhs.anchor.world_z != rhs.anchor.world_z) {
    return lhs.anchor.world_z < rhs.anchor.world_z;
  }
  return lhs.anchor.world_x < rhs.anchor.world_x;
}

void insert_bounded(
    std::array<RankedAnchor, kMaximumBackroomsFlickerLights> &ranked,
    std::size_t &count,
    RankedAnchor candidate) noexcept {
  if (count == ranked.size() && !ranked_before(candidate, ranked[count - 1U])) {
    return;
  }

  auto position = count < ranked.size() ? count : count - 1U;
  if (count < ranked.size()) {
    ++count;
  }
  while (position > 0U && ranked_before(candidate, ranked[position - 1U])) {
    if (position < ranked.size()) {
      ranked[position] = ranked[position - 1U];
    }
    --position;
  }
  ranked[position] = std::move(candidate);
}

[[nodiscard]] auto to_flicker_anchor(
    const BackroomsLightFixture &fixture) noexcept
    -> BackroomsFlickerAnchor {
  return {
      .fixture_id = fixture.id,
      .world_x = fixture.id.segment_anchor_x,
      .world_z = fixture.id.segment_anchor_z,
      .position_x = fixture.position_x,
      .position_y = fixture.position_y,
      .position_z = fixture.position_z,
  };
}

} // namespace

auto backrooms_fixture_can_flicker(int seed, int world_x, int world_z,
                                   int logical_level) noexcept -> bool {
  return fixture_hash(
             level_flicker_seed(seed, logical_level),
             world_x,
             world_z,
             kFixtureSelectionSalt) % 1000U <
         kBackroomsFlickerRatePerThousand;
}

auto backrooms_fixture_can_flicker(
    const BackroomsFixtureId &fixture_id) noexcept -> bool {
  return fixture_id.valid &&
         fixture_hash(fixture_id, kFixtureSelectionSalt) % 1000U <
             kBackroomsFlickerRatePerThousand;
}

auto backrooms_flicker_schedule(int seed, int world_x, int world_z,
                                int logical_level) noexcept
    -> BackroomsFlickerSchedule {
  return schedule_from_hash(
      fixture_hash(
          level_flicker_seed(seed, logical_level),
          world_x,
          world_z,
          kFixtureTimingSalt));
}

auto backrooms_flicker_schedule(
    const BackroomsFixtureId &fixture_id) noexcept
    -> BackroomsFlickerSchedule {
  return schedule_from_hash(fixture_hash(fixture_id, kFixtureTimingSalt));
}

auto backrooms_flicker_intensity(int seed, int world_x, int world_z,
                                 float elapsed_seconds,
                                 int logical_level) noexcept -> float {
  const auto pattern_offset =
      fixture_hash(
          level_flicker_seed(seed, logical_level),
          world_x,
          world_z,
          kFixtureTimingSalt ^ 0x7135U) % 12U;
  return intensity_from_schedule(
      backrooms_flicker_schedule(seed, world_x, world_z, logical_level),
      pattern_offset,
      elapsed_seconds);
}

auto backrooms_flicker_intensity(
    const BackroomsFixtureId &fixture_id,
    float elapsed_seconds) noexcept -> float {
  return intensity_from_schedule(
      backrooms_flicker_schedule(fixture_id),
      fixture_hash(fixture_id, kFixtureTimingSalt ^ 0x7135U) % 12U,
      elapsed_seconds);
}

auto collect_backrooms_flicker_field(
    const World &world,
    int center_x,
    int center_z,
    int logical_level) -> BackroomsFlickerField {
  const auto query = world.query_backrooms_light_fixtures(
      static_cast<double>(center_x) + 0.5,
      static_cast<double>(center_z) + 0.5,
      kBackroomsFlickerSearchRadius,
      logical_level,
      false);

  std::array<RankedAnchor, kMaximumBackroomsFlickerLights> ranked {};
  auto ranked_count = std::size_t {0U};
  for (const auto &fixture : query.fixtures) {
    if (!backrooms_fixture_can_flicker(fixture.id)) {
      continue;
    }
    const auto delta_x =
        static_cast<double>(fixture.position_x) -
        (static_cast<double>(center_x) + 0.5);
    const auto delta_z =
        static_cast<double>(fixture.position_z) -
        (static_cast<double>(center_z) + 0.5);
    insert_bounded(
        ranked,
        ranked_count,
        {to_flicker_anchor(fixture), delta_x * delta_x + delta_z * delta_z});
  }

  BackroomsFlickerField field {};
  field.count = ranked_count;
  for (std::size_t index = 0U; index < ranked_count; ++index) {
    field.anchors[index] = ranked[index].anchor;
  }
  return field;
}

auto find_nearest_backrooms_light_fixture(
    const World &world,
    double position_x,
    double position_z,
    int search_radius,
    int logical_level) -> std::optional<BackroomsFlickerAnchor> {
  if (!std::isfinite(position_x) || !std::isfinite(position_z)) {
    return std::nullopt;
  }

  const auto query = world.query_backrooms_light_fixtures(
      position_x,
      position_z,
      std::clamp(search_radius, 0, kMaximumBackroomsFixtureSearchRadius),
      logical_level,
      true);
  const BackroomsLightFixture *nearest = nullptr;
  auto nearest_distance_squared = std::numeric_limits<double>::infinity();
  for (const auto &fixture : query.fixtures) {
    const auto delta_x =
        static_cast<double>(fixture.position_x) - position_x;
    const auto delta_z =
        static_cast<double>(fixture.position_z) - position_z;
    const auto distance_squared = delta_x * delta_x + delta_z * delta_z;
    const auto tie_breaks_before =
        nearest != nullptr && distance_squared == nearest_distance_squared &&
        (fixture.id.segment_anchor_z < nearest->id.segment_anchor_z ||
         (fixture.id.segment_anchor_z == nearest->id.segment_anchor_z &&
          fixture.id.segment_anchor_x < nearest->id.segment_anchor_x));
    if (nearest == nullptr || distance_squared < nearest_distance_squared ||
        tie_breaks_before) {
      nearest = &fixture;
      nearest_distance_squared = distance_squared;
    }
  }
  return nearest != nullptr
             ? std::optional<BackroomsFlickerAnchor>(to_flicker_anchor(*nearest))
             : std::nullopt;
}

auto sample_backrooms_flicker_lights(const BackroomsFlickerField &field,
                                     int seed,
                                     float elapsed_seconds,
                                     int logical_level) noexcept
    -> std::array<BackroomsFlickerLight, kMaximumBackroomsFlickerLights> {
  std::array<BackroomsFlickerLight, kMaximumBackroomsFlickerLights> lights {};
  const auto count = std::min(field.count, kMaximumBackroomsFlickerLights);
  for (std::size_t index = 0U; index < count; ++index) {
    const auto &anchor = field.anchors[index];
    const auto intensity =
        anchor.fixture_id.valid
            ? backrooms_flicker_intensity(
                  anchor.fixture_id,
                  elapsed_seconds)
            : backrooms_flicker_intensity(
                  seed,
                  anchor.world_x,
                  anchor.world_z,
                  elapsed_seconds,
                  logical_level);
    lights[index] = {
        anchor.position_x,
        anchor.position_y,
        anchor.position_z,
        intensity,
    };
  }
  return lights;
}

} // namespace valcraft
