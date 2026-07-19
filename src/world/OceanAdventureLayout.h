#pragma once

#include <cstdint>

namespace valcraft {

// Je centralise ici les dimensions partagees par le terrain oceanique, le port
// et le navire pour que leurs volumes ne puissent pas diverger silencieusement.
inline constexpr int kSeaLevel = 48;
inline constexpr int kOceanNavigationCorridorCenterX = 0;
inline constexpr int kOceanNavigationCorridorHalfWidth = 16;
inline constexpr int kOceanNaturalLandExclusionHalfWidth = 24;
inline constexpr int kOceanNavigationTransitionOuterHalfWidth = 40;
inline constexpr int kOceanNavigationCorridorStartZ = -48;
inline constexpr int kOceanNavigationCorridorMaxSeabedY = kSeaLevel - 6;

inline constexpr int kOceanRouteMacroSectorLength = 600;
inline constexpr int kOceanRouteFirstIslandWindowMinZ = 500;
inline constexpr int kOceanRouteIslandWindowJitter = 100;
inline constexpr int kOceanRouteIslandWindowLength = 96;
inline constexpr int kOceanRouteIslandCenterMinAbsX = 50;
inline constexpr int kOceanRouteIslandCenterMaxAbsX = 70;
inline constexpr int kOceanRouteReservedHalfWidth = 144;

inline constexpr int kStartingPortMinX = -76;
inline constexpr int kStartingPortMaxX = -18;
inline constexpr int kStartingPortMinZ = -72;
inline constexpr int kStartingPortMaxZ = 30;
inline constexpr int kStartingPortSurfaceY = 52;
inline constexpr int kStartingPortMainlandMaxX = -40;
inline constexpr int kStartingPortQuayMinX = -48;
inline constexpr int kStartingPortQuayMaxX = -32;
inline constexpr int kStartingPortQuayMinZ = -52;
inline constexpr int kStartingPortQuayMaxZ = 18;
inline constexpr int kStartingPortBasinMinX = -31;
inline constexpr int kStartingPortBasinMaxX = -17;
inline constexpr int kStartingPortBasinMinZ = -52;
inline constexpr int kStartingPortBasinMaxZ = 24;

[[nodiscard]] inline constexpr auto ocean_adventure_layout_hash(
    int x,
    int z,
    int seed,
    std::uint32_t salt) noexcept -> std::uint32_t {
    auto value = static_cast<std::uint32_t>(x) * 0x9e3779b9U;
    value ^= static_cast<std::uint32_t>(z) * 0x85ebca6bU;
    value ^= static_cast<std::uint32_t>(seed) * 0xc2b2ae35U;
    value ^= salt * 0x27d4eb2dU;
    value ^= value >> 16U;
    value *= 0x7feb352dU;
    value ^= value >> 15U;
    value *= 0x846ca68bU;
    return value ^ (value >> 16U);
}

[[nodiscard]] inline constexpr auto ocean_route_island_window_start_z(
    int seed,
    int macro_sector) noexcept -> std::int64_t {
    const auto jitter = ocean_adventure_layout_hash(macro_sector, 0, seed, 101U) %
                        static_cast<std::uint32_t>(kOceanRouteIslandWindowJitter + 1);
    return static_cast<std::int64_t>(macro_sector) * kOceanRouteMacroSectorLength +
           kOceanRouteFirstIslandWindowMinZ + static_cast<std::int64_t>(jitter);
}

[[nodiscard]] inline constexpr auto is_ocean_navigation_corridor_column(int world_x, int world_z) noexcept -> bool {
    return world_z >= kOceanNavigationCorridorStartZ &&
           world_x >= kOceanNavigationCorridorCenterX - kOceanNavigationCorridorHalfWidth &&
           world_x <= kOceanNavigationCorridorCenterX + kOceanNavigationCorridorHalfWidth;
}

[[nodiscard]] inline constexpr auto is_starting_port_bounds_column(int world_x, int world_z) noexcept -> bool {
    return world_x >= kStartingPortMinX && world_x <= kStartingPortMaxX &&
           world_z >= kStartingPortMinZ && world_z <= kStartingPortMaxZ;
}

[[nodiscard]] inline constexpr auto is_starting_port_quay_foundation_column(int world_x, int world_z) noexcept
    -> bool {
    return world_x >= kStartingPortQuayMinX && world_x <= kStartingPortQuayMaxX &&
           world_z >= kStartingPortQuayMinZ && world_z <= kStartingPortQuayMaxZ;
}

[[nodiscard]] inline constexpr auto is_starting_port_mainland_plateau_column(int world_x, int world_z) noexcept
    -> bool {
    // Je garde un terre-plein continu sous les deux batiments et le phare ;
    // les ouvrages maritimes sculptent ensuite la facade est du port.
    return is_starting_port_bounds_column(world_x, world_z) &&
           world_x <= kStartingPortMainlandMaxX;
}

[[nodiscard]] inline constexpr auto is_starting_port_terrain_foundation_column(int world_x, int world_z) noexcept
    -> bool {
    return is_starting_port_mainland_plateau_column(world_x, world_z) ||
           is_starting_port_quay_foundation_column(world_x, world_z);
}

[[nodiscard]] inline constexpr auto is_starting_port_basin_column(int world_x, int world_z) noexcept -> bool {
    return world_x >= kStartingPortBasinMinX && world_x <= kStartingPortBasinMaxX &&
           world_z >= kStartingPortBasinMinZ && world_z <= kStartingPortBasinMaxZ;
}

} // namespace valcraft
