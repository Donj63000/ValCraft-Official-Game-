#include "render/BackroomsFlicker.h"

#include "world/BackroomsGenerator.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace valcraft {

namespace {

constexpr std::uint64_t kFixtureSelectionSalt = 0x4D9C7A31B682E5F9ULL;
constexpr std::uint64_t kFixtureTimingSalt = 0xA1F0D638C5724B9EULL;
constexpr int kMaximumFixtureSpan = 4;
constexpr auto kMinimumWorldCoordinate =
    static_cast<std::int64_t>(std::numeric_limits<int>::lowest());
constexpr auto kMaximumWorldCoordinate =
    static_cast<std::int64_t>(std::numeric_limits<int>::max());
// Je garde la marge maximale soustraite par le générateur lorsqu'il retrouve
// l'ancre d'une rampe. Les coordonnées int extrêmes restent ainsi sans UB.
constexpr auto kMinimumGeneratorCoordinate =
    kMinimumWorldCoordinate + (kMaximumFixtureSpan - 1);
constexpr auto kMaximumGeneratorCoordinate =
    kMaximumWorldCoordinate - (kMaximumFixtureSpan - 1);

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
  auto value =
      mix_bits(static_cast<std::uint32_t>(seed) ^ salt);
  value ^= mix_bits(
      static_cast<std::uint32_t>(world_x) +
      0x9E3779B97F4A7C15ULL);
  value ^= mix_bits(
      static_cast<std::uint32_t>(world_z) +
      0xD1B54A32D192ED03ULL);
  return mix_bits(value);
}

[[nodiscard]] constexpr auto level_flicker_seed(
    int seed,
    int logical_level) noexcept -> int {
  // Je sépare les rythmes de deux étages partageant les mêmes coordonnées.
  // Le cast non signé rend le mélange défini pour les profondeurs négatives.
  return static_cast<int>(
      static_cast<std::uint32_t>(seed) ^
      (static_cast<std::uint32_t>(logical_level) *
       UINT32_C(0x9E3779B9)));
}

[[nodiscard]] auto positive_remainder(float value, float period) noexcept
    -> float {
  const auto remainder = std::fmod(value, period);
  return remainder < 0.0F ? remainder + period : remainder;
}

struct RankedAnchor {
  BackroomsFlickerAnchor anchor{};
  std::int64_t distance_squared = 0;
};

struct CanonicalFixture {
  BackroomsFlickerAnchor anchor{};
  double center_x = 0.0;
  double center_z = 0.0;
};

[[nodiscard]] constexpr auto fixture_state_is_supported(
    BackroomsLightState state,
    bool include_emergency) noexcept -> bool {
  return state == BackroomsLightState::Active ||
         (include_emergency &&
          state == BackroomsLightState::Emergency);
}

[[nodiscard]] auto canonical_fixture_at(
    const BackroomsGenerator &generator,
    std::int64_t world_x_64,
    std::int64_t world_z_64,
    bool include_emergency) noexcept
    -> std::optional<CanonicalFixture> {
  if (world_x_64 < kMinimumGeneratorCoordinate ||
      world_x_64 > kMaximumGeneratorCoordinate ||
      world_z_64 < kMinimumGeneratorCoordinate ||
      world_z_64 > kMaximumGeneratorCoordinate) {
    return std::nullopt;
  }

  const auto world_x = static_cast<int>(world_x_64);
  const auto world_z = static_cast<int>(world_z_64);
  const auto sample =
      generator.sample_column(world_x, world_z);
  if (!fixture_state_is_supported(
          sample.light_state,
          include_emergency)) {
    return std::nullopt;
  }

  const auto descriptor =
      generator.descriptor_at(world_x, world_z);
  const auto step_x =
      descriptor.primary_axis_x ? 1 : 0;
  const auto step_z =
      descriptor.primary_axis_x ? 0 : 1;
  const auto previous_x_64 =
      world_x_64 - step_x;
  const auto previous_z_64 =
      world_z_64 - step_z;
  if (previous_x_64 >= kMinimumWorldCoordinate &&
      previous_x_64 <= kMaximumWorldCoordinate &&
      previous_z_64 >= kMinimumWorldCoordinate &&
      previous_z_64 <= kMaximumWorldCoordinate) {
    const auto previous_x =
        static_cast<int>(previous_x_64);
    const auto previous_z =
        static_cast<int>(previous_z_64);
    const auto previous =
        generator.sample_column(
            previous_x,
            previous_z);
    if (previous.light_state ==
        sample.light_state) {
      return std::nullopt;
    }
  }

  auto run_length = 1;
  for (auto offset = 1;
       offset < kMaximumFixtureSpan;
       ++offset) {
    const auto fixture_x_64 =
        world_x_64 +
        static_cast<std::int64_t>(
            step_x * offset);
    const auto fixture_z_64 =
        world_z_64 +
        static_cast<std::int64_t>(
            step_z * offset);
    if (fixture_x_64 < kMinimumWorldCoordinate ||
        fixture_x_64 > kMaximumWorldCoordinate ||
        fixture_z_64 < kMinimumWorldCoordinate ||
        fixture_z_64 > kMaximumWorldCoordinate) {
      break;
    }
    const auto fixture_x =
        static_cast<int>(fixture_x_64);
    const auto fixture_z =
        static_cast<int>(fixture_z_64);
    const auto fixture_sample =
        generator.sample_column(
            fixture_x,
            fixture_z);
    if (fixture_sample.light_state !=
        sample.light_state) {
      break;
    }
    ++run_length;
  }

  const auto half_span =
      0.5 *
      static_cast<double>(run_length - 1);
  const auto center_x =
      static_cast<double>(world_x) +
      0.5 +
      static_cast<double>(step_x) *
          half_span;
  const auto center_z =
      static_cast<double>(world_z) +
      0.5 +
      static_cast<double>(step_z) *
          half_span;
  return CanonicalFixture{
      {
          world_x,
          world_z,
          static_cast<float>(center_x),
          static_cast<float>(sample.ceiling_y),
          static_cast<float>(center_z),
      },
      center_x,
      center_z,
  };
}

} // namespace

auto backrooms_fixture_can_flicker(int seed, int world_x,
                                   int world_z,
                                   int logical_level) noexcept -> bool {
  return fixture_hash(
             level_flicker_seed(seed, logical_level),
             world_x,
             world_z,
             kFixtureSelectionSalt) % 1000U <
         kBackroomsFlickerRatePerThousand;
}

auto backrooms_flicker_schedule(
    int seed,
    int world_x,
    int world_z,
    int logical_level) noexcept
    -> BackroomsFlickerSchedule {
  const auto hash =
      fixture_hash(
          level_flicker_seed(seed, logical_level),
          world_x,
          world_z,
          kFixtureTimingSalt);
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

auto backrooms_flicker_intensity(int seed, int world_x, int world_z,
                                 float elapsed_seconds,
                                 int logical_level) noexcept -> float {
  if (!std::isfinite(elapsed_seconds)) {
    elapsed_seconds = 0.0F;
  }
  const auto schedule =
      backrooms_flicker_schedule(
          seed,
          world_x,
          world_z,
          logical_level);
  const auto local_time = positive_remainder(
      std::max(elapsed_seconds, 0.0F) + schedule.phase_seconds,
      schedule.period_seconds);
  if (local_time >= schedule.burst_seconds) {
    return 1.0F;
  }

  // Je reproduis une amorce de ballast courte et irreguliere. Le motif reste
  // deterministe pour qu'une sauvegarde et les tests rejouent la meme tension.
  constexpr std::array<float, 12U> kBallastPattern{{
      0.05F, 0.18F, 0.82F, 0.07F, 1.00F, 0.32F,
      0.09F, 0.68F, 0.14F, 0.92F, 0.06F, 0.44F,
  }};
  constexpr auto kFlickerTicksPerSecond = 22.0F;
  const auto tick =
      static_cast<std::uint64_t>(std::floor(local_time *
                                            kFlickerTicksPerSecond));
  const auto pattern_offset =
      fixture_hash(
          level_flicker_seed(seed, logical_level),
          world_x,
          world_z,
          kFixtureTimingSalt ^ 0x7135U) %
      kBallastPattern.size();
  return kBallastPattern[static_cast<std::size_t>(
      (tick + pattern_offset) % kBallastPattern.size())];
}

auto collect_backrooms_flicker_field(
    int seed,
    int center_x,
    int center_z,
    int logical_level)
    -> BackroomsFlickerField {
  const BackroomsGenerator generator{
      seed,
      logical_level,
  };
  std::vector<RankedAnchor> candidates{};
  candidates.reserve(16U);

  const auto radius =
      static_cast<std::int64_t>(kBackroomsFlickerSearchRadius);
  const auto radius_squared = radius * radius;
  const auto center_x_64 = static_cast<std::int64_t>(center_x);
  const auto center_z_64 = static_cast<std::int64_t>(center_z);

  for (auto z_64 = center_z_64 - radius; z_64 <= center_z_64 + radius;
       ++z_64) {
    if (z_64 < kMinimumWorldCoordinate ||
        z_64 > kMaximumWorldCoordinate) {
      continue;
    }
    for (auto x_64 = center_x_64 - radius; x_64 <= center_x_64 + radius;
         ++x_64) {
      if (x_64 < kMinimumWorldCoordinate ||
          x_64 > kMaximumWorldCoordinate) {
        continue;
      }
      const auto delta_x = x_64 - center_x_64;
      const auto delta_z = z_64 - center_z_64;
      const auto distance_squared =
          delta_x * delta_x + delta_z * delta_z;
      if (distance_squared > radius_squared) {
        continue;
      }

      const auto fixture =
          canonical_fixture_at(
              generator,
              x_64,
              z_64,
              false);
      if (!fixture.has_value()) {
        continue;
      }
      if (!backrooms_fixture_can_flicker(
              seed,
              fixture->anchor.world_x,
              fixture->anchor.world_z,
              logical_level)) {
        continue;
      }

      candidates.push_back({
          fixture->anchor,
          distance_squared,
      });
    }
  }

  std::sort(candidates.begin(), candidates.end(),
            [](const RankedAnchor &lhs, const RankedAnchor &rhs) {
              if (lhs.distance_squared != rhs.distance_squared) {
                return lhs.distance_squared < rhs.distance_squared;
              }
              if (lhs.anchor.world_z != rhs.anchor.world_z) {
                return lhs.anchor.world_z < rhs.anchor.world_z;
              }
              return lhs.anchor.world_x < rhs.anchor.world_x;
            });

  BackroomsFlickerField field{};
  field.count =
      std::min(candidates.size(), kMaximumBackroomsFlickerLights);
  for (std::size_t index = 0U; index < field.count; ++index) {
    field.anchors[index] = candidates[index].anchor;
  }
  return field;
}

auto find_nearest_backrooms_light_fixture(
    int seed,
    double position_x,
    double position_z,
    int search_radius,
    int logical_level) noexcept
    -> std::optional<BackroomsFlickerAnchor> {
  if (!std::isfinite(position_x) ||
      !std::isfinite(position_z)) {
    return std::nullopt;
  }

  constexpr auto minimum_position =
      static_cast<double>(
          std::numeric_limits<int>::lowest());
  constexpr auto maximum_position_exclusive =
      static_cast<double>(
          std::numeric_limits<int>::max()) +
      1.0;
  if (position_x < minimum_position ||
      position_x >= maximum_position_exclusive ||
      position_z < minimum_position ||
      position_z >= maximum_position_exclusive) {
    return std::nullopt;
  }

  const auto radius =
      std::clamp(
          search_radius,
          0,
          kMaximumBackroomsFixtureSearchRadius);
  const auto radius_as_double =
      static_cast<double>(radius);
  const auto radius_squared =
      radius_as_double * radius_as_double;
  const auto center_x_64 =
      static_cast<std::int64_t>(
          std::floor(position_x));
  const auto center_z_64 =
      static_cast<std::int64_t>(
          std::floor(position_z));

  // Je depasse le disque de trois cellules pendant la collecte : le debut
  // d'une rampe de quatre cases peut se trouver hors du rayon alors que son
  // centre, seul point compare au monstre, se trouve encore dedans.
  const auto scan_radius =
      static_cast<std::int64_t>(
          radius +
          (kMaximumFixtureSpan - 1));
  const BackroomsGenerator generator{
      seed,
      logical_level,
  };
  auto nearest =
      std::optional<BackroomsFlickerAnchor>{};
  auto nearest_distance_squared =
      std::numeric_limits<double>::infinity();

  for (auto z_64 =
           center_z_64 - scan_radius;
       z_64 <= center_z_64 + scan_radius;
       ++z_64) {
    if (z_64 < kMinimumWorldCoordinate ||
        z_64 > kMaximumWorldCoordinate) {
      continue;
    }
    for (auto x_64 =
             center_x_64 - scan_radius;
         x_64 <= center_x_64 + scan_radius;
         ++x_64) {
      if (x_64 < kMinimumWorldCoordinate ||
          x_64 > kMaximumWorldCoordinate) {
        continue;
      }

      const auto fixture =
          canonical_fixture_at(
              generator,
              x_64,
              z_64,
              true);
      if (!fixture.has_value()) {
        continue;
      }

      const auto delta_x =
          fixture->center_x -
          position_x;
      const auto delta_z =
          fixture->center_z -
          position_z;
      const auto distance_squared =
          delta_x * delta_x +
          delta_z * delta_z;
      if (!std::isfinite(distance_squared) ||
          distance_squared > radius_squared) {
        continue;
      }

      const auto tie_breaks_before =
          nearest.has_value() &&
          distance_squared ==
              nearest_distance_squared &&
          (
              fixture->anchor.world_z <
                  nearest->world_z ||
              (
                  fixture->anchor.world_z ==
                      nearest->world_z &&
                  fixture->anchor.world_x <
                      nearest->world_x
              )
          );
      if (!nearest.has_value() ||
          distance_squared <
              nearest_distance_squared ||
          tie_breaks_before) {
        nearest =
            fixture->anchor;
        nearest_distance_squared =
            distance_squared;
      }
    }
  }

  return nearest;
}

auto sample_backrooms_flicker_lights(const BackroomsFlickerField &field,
                                     int seed,
                                     float elapsed_seconds,
                                     int logical_level) noexcept
    -> std::array<BackroomsFlickerLight, kMaximumBackroomsFlickerLights> {
  std::array<BackroomsFlickerLight, kMaximumBackroomsFlickerLights> lights{};
  const auto count =
      std::min(field.count, kMaximumBackroomsFlickerLights);
  for (std::size_t index = 0U; index < count; ++index) {
    const auto &anchor = field.anchors[index];
    lights[index] = {
        anchor.position_x,
        anchor.position_y,
        anchor.position_z,
        backrooms_flicker_intensity(seed, anchor.world_x, anchor.world_z,
                                    elapsed_seconds, logical_level),
    };
  }
  return lights;
}

} // namespace valcraft
