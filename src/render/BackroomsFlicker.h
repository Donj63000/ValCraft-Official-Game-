#pragma once

#include "world/WorldGenerator.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace valcraft {

class World;

inline constexpr std::size_t kMaximumBackroomsFlickerLights = 6U;
inline constexpr int kBackroomsFlickerSearchRadius = 80;
inline constexpr int kBackroomsFlickerCacheCellSize = 8;
inline constexpr std::uint32_t kBackroomsFlickerRatePerThousand = 14U;
inline constexpr int kMaximumBackroomsFixtureSearchRadius =
    kBackroomsFlickerSearchRadius;

struct BackroomsFlickerSchedule {
  float period_seconds = 24.0F;
  float burst_seconds = 0.7F;
  float phase_seconds = 0.0F;

  auto operator==(const BackroomsFlickerSchedule &) const -> bool = default;
};

struct BackroomsFlickerAnchor {
  BackroomsFixtureId fixture_id{};
  int world_x = 0;
  int world_z = 0;
  float position_x = 0.0F;
  float position_y = 0.0F;
  float position_z = 0.0F;

  auto operator==(const BackroomsFlickerAnchor &) const -> bool = default;
};

struct BackroomsFlickerField {
  std::array<BackroomsFlickerAnchor, kMaximumBackroomsFlickerLights> anchors{};
  std::size_t count = 0U;
};

struct BackroomsFlickerLight {
  float position_x = 0.0F;
  float position_y = 0.0F;
  float position_z = 0.0F;
  float intensity = 1.0F;

  auto operator==(const BackroomsFlickerLight &) const -> bool = default;
};

[[nodiscard]] auto backrooms_fixture_can_flicker(int seed, int world_x,
                                                 int world_z,
                                                 int logical_level = 0) noexcept -> bool;

[[nodiscard]] auto backrooms_flicker_schedule(int seed, int world_x,
                                              int world_z,
                                              int logical_level = 0) noexcept
    -> BackroomsFlickerSchedule;

[[nodiscard]] auto backrooms_flicker_intensity(int seed, int world_x,
                                               int world_z,
                                               float elapsed_seconds,
                                               int logical_level = 0) noexcept
    -> float;

[[nodiscard]] auto backrooms_fixture_can_flicker(
    const BackroomsFixtureId &fixture_id) noexcept -> bool;

[[nodiscard]] auto backrooms_flicker_schedule(
    const BackroomsFixtureId &fixture_id) noexcept
    -> BackroomsFlickerSchedule;

[[nodiscard]] auto backrooms_flicker_intensity(
    const BackroomsFixtureId &fixture_id,
    float elapsed_seconds) noexcept -> float;

[[nodiscard]] auto collect_backrooms_flicker_field(
    const World &world,
    int center_x,
    int center_z,
    int logical_level)
    -> BackroomsFlickerField;

// Je borne le rayon a kMaximumBackroomsFixtureSearchRadius et je renvoie un
// segment canonique dont le candidat intersecte l'index charge. Son empreinte
// est resolue depuis l'etat charge, surcharge ou genere du monde.
[[nodiscard]] auto find_nearest_backrooms_light_fixture(
    const World &world,
    double position_x,
    double position_z,
    int search_radius,
    int logical_level)
    -> std::optional<BackroomsFlickerAnchor>;

[[nodiscard]] auto sample_backrooms_flicker_lights(
    const BackroomsFlickerField &field,
    int seed,
    float elapsed_seconds,
    int logical_level = 0) noexcept
    -> std::array<BackroomsFlickerLight, kMaximumBackroomsFlickerLights>;

} // namespace valcraft
