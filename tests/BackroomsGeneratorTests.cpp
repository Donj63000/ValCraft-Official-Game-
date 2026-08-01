#include "world/BackroomsGenerator.h"
#include "world/Environment.h"
#include "world/WorldGenerator.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <queue>
#include <set>
#include <utility>
#include <vector>

namespace valcraft {

namespace {

constexpr auto local_index(int x, int z) noexcept -> std::size_t {
    return static_cast<std::size_t>(z * kBackroomsModuleSize + x);
}

auto finite_color(const glm::vec3& color) noexcept -> bool {
    return std::isfinite(color.r) &&
           std::isfinite(color.g) &&
           std::isfinite(color.b);
}

auto perceptual_luminance(const glm::vec3& color) noexcept -> float {
    return
        color.r * 0.2126F +
        color.g * 0.7152F +
        color.b * 0.0722F;
}

auto reachable_cells(
    const BackroomsGenerator& generator,
    int module_x,
    int module_z,
    int start_x,
    int start_z) -> std::array<bool, kBackroomsModuleSize * kBackroomsModuleSize> {

    std::array<bool, kBackroomsModuleSize * kBackroomsModuleSize> visited {};
    const auto world_x = module_x * kBackroomsModuleSize + start_x;
    const auto world_z = module_z * kBackroomsModuleSize + start_z;
    if (!generator.is_walkable(world_x, world_z)) {
        return visited;
    }

    std::queue<std::pair<int, int>> pending;
    pending.emplace(start_x, start_z);
    visited[local_index(start_x, start_z)] = true;

    constexpr std::array<std::pair<int, int>, 4> directions {{
        {1, 0},
        {-1, 0},
        {0, 1},
        {0, -1},
    }};
    while (!pending.empty()) {
        const auto [x, z] = pending.front();
        pending.pop();

        for (const auto& [dx, dz] : directions) {
            const auto next_x = x + dx;
            const auto next_z = z + dz;
            if (next_x < 0 ||
                next_x >= kBackroomsModuleSize ||
                next_z < 0 ||
                next_z >= kBackroomsModuleSize) {
                continue;
            }

            const auto index = local_index(next_x, next_z);
            if (visited[index]) {
                continue;
            }

            const auto next_world_x =
                module_x * kBackroomsModuleSize + next_x;
            const auto next_world_z =
                module_z * kBackroomsModuleSize + next_z;
            if (!generator.is_walkable(next_world_x, next_world_z)) {
                continue;
            }

            visited[index] = true;
            pending.emplace(next_x, next_z);
        }
    }
    return visited;
}

void check_portal_reachable(
    const std::array<bool, kBackroomsModuleSize * kBackroomsModuleSize>& visited,
    int local_x,
    int local_z) {

    for (int offset = -kBackroomsPortalHalfWidth;
         offset <= kBackroomsPortalHalfWidth;
         ++offset) {
        const auto x =
            local_x == 0 || local_x == kBackroomsModuleSize - 1
                ? local_x
                : local_x + offset;
        const auto z =
            local_z == 0 || local_z == kBackroomsModuleSize - 1
                ? local_z
                : local_z + offset;
        CHECK(visited[local_index(x, z)]);
    }
}

auto find_connector_in_district(
    const BackroomsGenerator& generator,
    BackroomsConnectorDirection direction,
    int district_x,
    int district_z) -> std::optional<BackroomsLevelConnector> {

    constexpr auto district_size =
        kBackroomsConnectorDistrictModules *
        kBackroomsModuleSize;
    const auto first_x = district_x * district_size;
    const auto first_z = district_z * district_size;
    for (int z = first_z; z < first_z + district_size; ++z) {
        for (int x = first_x; x < first_x + district_size; ++x) {
            const auto connector =
                generator.connector_near(
                    x,
                    kBackroomsFloorY + 1,
                    z,
                    0);
            if (connector.has_value() &&
                connector->direction == direction) {
                return connector;
            }
        }
    }
    return std::nullopt;
}

} // namespace

TEST_CASE("BackRooms module descriptors are deterministic") {
    const BackroomsGenerator first(42);
    const BackroomsGenerator second(42);
    const BackroomsGenerator different_seed(43);

    for (int z = -12; z <= 12; ++z) {
        for (int x = -12; x <= 12; ++x) {
            CHECK(
                first.module_descriptor(x, z) ==
                second.module_descriptor(x, z));
        }
    }

    auto at_least_one_difference = false;
    for (int z = -4; z <= 4; ++z) {
        for (int x = -4; x <= 4; ++x) {
            at_least_one_difference =
                at_least_one_difference ||
                first.module_descriptor(x, z) !=
                    different_seed.module_descriptor(x, z);
        }
    }
    CHECK(at_least_one_difference);
}

TEST_CASE("BackRooms logical levels select a deterministic theme identity") {
    const BackroomsGenerator level_zero(424242, 0);
    const BackroomsGenerator level_one(424242, 1);
    const BackroomsGenerator level_minus_one(424242, -1);
    const BackroomsGenerator poolrooms_a(424242, -2);
    const BackroomsGenerator poolrooms_b(424242, -2);
    const BackroomsGenerator deeper_poolrooms(424242, -3);

    CHECK(level_zero.logical_level() == 0);
    CHECK(level_zero.theme() == BackroomsTheme::Offices);
    CHECK_FALSE(level_zero.is_poolrooms());
    CHECK(level_minus_one.theme() == BackroomsTheme::Offices);
    CHECK_FALSE(level_minus_one.is_poolrooms());
    CHECK(poolrooms_a.logical_level() == -2);
    CHECK(poolrooms_a.theme() == BackroomsTheme::Poolrooms);
    CHECK(poolrooms_a.is_poolrooms());

    auto offices_differ = false;
    auto poolrooms_differ = false;
    for (int module_z = -4; module_z <= 4; ++module_z) {
        for (int module_x = -4; module_x <= 4; ++module_x) {
            CHECK(
                poolrooms_a.module_descriptor(module_x, module_z) ==
                poolrooms_b.module_descriptor(module_x, module_z));
            offices_differ =
                offices_differ ||
                level_zero.module_descriptor(module_x, module_z) !=
                    level_one.module_descriptor(module_x, module_z);
            poolrooms_differ =
                poolrooms_differ ||
                poolrooms_a.module_descriptor(module_x, module_z) !=
                    deeper_poolrooms.module_descriptor(
                        module_x,
                        module_z);
        }
    }
    CHECK(offices_differ);
    CHECK(poolrooms_differ);
}

TEST_CASE("BackRooms signed coordinates use floor division") {
    CHECK(BackroomsGenerator::module_coordinate(0) == 0);
    CHECK(BackroomsGenerator::module_coordinate(63) == 0);
    CHECK(BackroomsGenerator::module_coordinate(64) == 1);
    CHECK(BackroomsGenerator::module_coordinate(-1) == -1);
    CHECK(BackroomsGenerator::module_coordinate(-64) == -1);
    CHECK(BackroomsGenerator::module_coordinate(-65) == -2);

    CHECK(BackroomsGenerator::local_coordinate(0) == 0);
    CHECK(BackroomsGenerator::local_coordinate(63) == 63);
    CHECK(BackroomsGenerator::local_coordinate(64) == 0);
    CHECK(BackroomsGenerator::local_coordinate(-1) == 63);
    CHECK(BackroomsGenerator::local_coordinate(-64) == 0);
    CHECK(BackroomsGenerator::local_coordinate(-65) == 63);
}

TEST_CASE("BackRooms neighbouring modules share every portal") {
    const BackroomsGenerator generator(-9081);

    for (int module_z = -18; module_z <= 18; ++module_z) {
        for (int module_x = -18; module_x <= 18; ++module_x) {
            const auto current =
                generator.module_descriptor(module_x, module_z);
            const auto east =
                generator.module_descriptor(module_x + 1, module_z);
            const auto south =
                generator.module_descriptor(module_x, module_z + 1);

            CHECK(current.east_portal_z == east.west_portal_z);
            CHECK(current.south_portal_x == south.north_portal_x);

            for (int offset = -kBackroomsPortalHalfWidth;
                 offset <= kBackroomsPortalHalfWidth;
                 ++offset) {
                const auto east_world_z =
                    module_z * kBackroomsModuleSize +
                    current.east_portal_z + offset;
                const auto east_world_x =
                    module_x * kBackroomsModuleSize +
                    kBackroomsModuleSize - 1;
                CHECK(
                    generator.is_walkable(
                        east_world_x,
                        east_world_z));
                CHECK(
                    generator.is_walkable(
                        east_world_x + 1,
                        east_world_z));

                const auto south_world_x =
                    module_x * kBackroomsModuleSize +
                    current.south_portal_x + offset;
                const auto south_world_z =
                    module_z * kBackroomsModuleSize +
                    kBackroomsModuleSize - 1;
                CHECK(
                    generator.is_walkable(
                        south_world_x,
                        south_world_z));
                CHECK(
                    generator.is_walkable(
                        south_world_x,
                        south_world_z + 1));
            }
        }
    }
}

TEST_CASE("Poolrooms neighbouring modules keep dry shared portals") {
    constexpr std::array<int, 2> levels {{-2, -17}};
    for (const auto level : levels) {
        const BackroomsGenerator generator(-9081, level);
        for (int module_z = -5; module_z <= 5; ++module_z) {
            for (int module_x = -5; module_x <= 5; ++module_x) {
                const auto current =
                    generator.module_descriptor(module_x, module_z);
                const auto east =
                    generator.module_descriptor(
                        module_x + 1,
                        module_z);
                const auto south =
                    generator.module_descriptor(
                        module_x,
                        module_z + 1);
                CHECK(
                    current.east_portal_z ==
                    east.west_portal_z);
                CHECK(
                    current.south_portal_x ==
                    south.north_portal_x);

                for (int offset = -kBackroomsPortalHalfWidth;
                     offset <= kBackroomsPortalHalfWidth;
                     ++offset) {
                    const auto east_x =
                        module_x * kBackroomsModuleSize +
                        kBackroomsModuleSize - 1;
                    const auto east_z =
                        module_z * kBackroomsModuleSize +
                        current.east_portal_z + offset;
                    CHECK(generator.is_walkable(east_x, east_z));
                    CHECK(generator.is_walkable(east_x + 1, east_z));
                    CHECK(
                        generator.sample_water_state(
                            east_x,
                            kBackroomsFloorY + 1,
                            east_z) ==
                        0);
                    CHECK(
                        generator.sample_water_state(
                            east_x + 1,
                            kBackroomsFloorY + 1,
                            east_z) ==
                        0);
                    CHECK(
                        generator.sample_column(
                            east_x,
                            east_z).pool_surface !=
                        BackroomsPoolSurface::Water);
                    CHECK(
                        generator.sample_column(
                            east_x + 1,
                            east_z).pool_surface !=
                        BackroomsPoolSurface::Water);

                    const auto south_x =
                        module_x * kBackroomsModuleSize +
                        current.south_portal_x + offset;
                    const auto south_z =
                        module_z * kBackroomsModuleSize +
                        kBackroomsModuleSize - 1;
                    CHECK(generator.is_walkable(south_x, south_z));
                    CHECK(generator.is_walkable(south_x, south_z + 1));
                    CHECK(
                        generator.sample_water_state(
                            south_x,
                            kBackroomsFloorY + 1,
                            south_z) ==
                        0);
                    CHECK(
                        generator.sample_water_state(
                            south_x,
                            kBackroomsFloorY + 1,
                            south_z + 1) ==
                        0);
                    CHECK(
                        generator.sample_column(
                            south_x,
                            south_z).pool_surface !=
                        BackroomsPoolSurface::Water);
                    CHECK(
                        generator.sample_column(
                            south_x,
                            south_z + 1).pool_surface !=
                        BackroomsPoolSurface::Water);
                }
            }
        }
    }
}

TEST_CASE("BackRooms guaranteed skeleton keeps every exit reachable") {
    constexpr std::array<int, 3> seeds {{1337, -9081, 0x51A7}};
    for (const auto seed : seeds) {
        const BackroomsGenerator generator(seed);
        for (int module_z = -8; module_z <= 8; ++module_z) {
            for (int module_x = -8; module_x <= 8; ++module_x) {
                const auto descriptor =
                    generator.module_descriptor(module_x, module_z);
                const auto visited =
                    reachable_cells(
                        generator,
                        module_x,
                        module_z,
                        descriptor.hub_x,
                        descriptor.hub_z);

                CHECK(
                    visited[
                        local_index(
                            descriptor.hub_x,
                            descriptor.hub_z)]);
                check_portal_reachable(
                    visited,
                    descriptor.north_portal_x,
                    0);
                check_portal_reachable(
                    visited,
                    descriptor.south_portal_x,
                    kBackroomsModuleSize - 1);
                check_portal_reachable(
                    visited,
                    0,
                    descriptor.west_portal_z);
                check_portal_reachable(
                    visited,
                    kBackroomsModuleSize - 1,
                    descriptor.east_portal_z);
            }
        }
    }
}

TEST_CASE("Poolrooms mix safe shallow basins dry paths and voxel volumes") {
    const BackroomsGenerator generator(424242, -2);
    auto wet_columns = 0;
    auto dry_walkable_columns = 0;
    auto wall_columns = 0;
    auto overhead_columns = 0;
    auto balcony_columns = 0;
    auto arch_columns = 0;
    auto shore_columns = 0;
    auto found_readable_shore = false;
    constexpr std::array<std::pair<int, int>, 4> neighbours {{
        {1, 0},
        {-1, 0},
        {0, 1},
        {0, -1},
    }};

    for (int z = -96; z <= 96; ++z) {
        for (int x = -96; x <= 96; ++x) {
            const auto column =
                generator.sample_column(x, z);
            CHECK(column.floor_y == kBackroomsFloorY);
            CHECK(
                generator.sample_block(
                    x,
                    column.floor_y,
                    z) !=
                to_block_id(BlockType::Air));

            if (column.overhead_bottom_y <=
                column.overhead_top_y) {
                ++overhead_columns;
                CHECK(
                    column.overhead_bottom_y -
                        column.floor_y >=
                        5);
            }
            balcony_columns +=
                column.elevated_feature ==
                        BackroomsElevatedFeature::Balcony
                    ? 1
                    : 0;
            arch_columns +=
                column.elevated_feature ==
                        BackroomsElevatedFeature::Arch
                    ? 1
                    : 0;
            if (column.wall) {
                ++wall_columns;
            }
            shore_columns +=
                column.pool_surface ==
                        BackroomsPoolSurface::Shore
                    ? 1
                    : 0;

            const auto water =
                generator.sample_water_state(
                    x,
                    kBackroomsFloorY + 1,
                    z);
            if (water == 0) {
                if (!column.wall) {
                    ++dry_walkable_columns;
                }
                continue;
            }

            ++wet_columns;
            CHECK(
                column.pool_surface ==
                BackroomsPoolSurface::Water);
            CHECK(
                water_level_from_state(water) ==
                static_cast<std::uint8_t>(5));
            CHECK(water_state_is_source(water));
            CHECK(water_state_is_infinite(water));
            CHECK_FALSE(column.wall);
            CHECK(
                generator.sample_block(
                    x,
                    kBackroomsFloorY + 1,
                    z) ==
                to_block_id(BlockType::Air));
            CHECK(
                generator.sample_block(
                    x,
                    kBackroomsFloorY,
                    z) ==
                to_block_id(BlockType::PoolroomsWetTile));
            CHECK(
                generator.sample_water_state(
                    x,
                    kBackroomsFloorY,
                    z) ==
                0);

            for (const auto& [dx, dz] : neighbours) {
                const auto neighbour =
                    generator.sample_column(x + dx, z + dz);
                if (!neighbour.wall &&
                    generator.sample_water_state(
                        x + dx,
                        kBackroomsFloorY + 1,
                        z + dz) ==
                        0) {
                    CHECK(
                        neighbour.floor_y ==
                        column.floor_y);
                    CHECK(
                        neighbour.pool_surface ==
                        BackroomsPoolSurface::Shore);
                    CHECK((
                        neighbour.floor_block ==
                            to_block_id(
                                BlockType::PoolroomsMetal) ||
                        neighbour.floor_block ==
                            to_block_id(
                                BlockType::PoolroomsPlastic)));
                    found_readable_shore = true;
                }
            }
        }
    }

    CHECK(wet_columns > 1500);
    CHECK(dry_walkable_columns > 1500);
    CHECK(wall_columns > 500);
    CHECK(overhead_columns > 500);
    CHECK(balcony_columns > 100);
    CHECK(arch_columns > 100);
    CHECK(shore_columns > 1000);
    CHECK(found_readable_shore);
}

TEST_CASE("Poolrooms basins stay coherent and every water edge has a shore") {
    constexpr auto first = -128;
    constexpr auto last = 127;
    constexpr auto width = last - first + 1;
    const BackroomsGenerator generator(424242, -2);
    std::vector<BackroomsColumnSample> columns(
        static_cast<std::size_t>(width * width));
    std::vector<bool> visited(
        static_cast<std::size_t>(width * width),
        false);

    const auto index_of = [](int x, int z) {
        return static_cast<std::size_t>(
            (z - first) * width + (x - first));
    };
    for (int z = first; z <= last; ++z) {
        for (int x = first; x <= last; ++x) {
            columns[index_of(x, z)] =
                generator.sample_column(x, z);
        }
    }

    constexpr std::array<std::pair<int, int>, 4> neighbours {{
        {1, 0},
        {-1, 0},
        {0, 1},
        {0, -1},
    }};
    auto interior_components = 0;
    auto smallest_component = width * width;

    for (int z = first; z <= last; ++z) {
        for (int x = first; x <= last; ++x) {
            const auto index = index_of(x, z);
            const auto& column = columns[index];
            const auto water =
                column.water_state != WaterState {0};
            CHECK(
                water ==
                (column.pool_surface ==
                 BackroomsPoolSurface::Water));
            if (column.guaranteed_route) {
                CHECK_FALSE(water);
            }
            if (!water || visited[index]) {
                continue;
            }

            auto component_size = 0;
            auto touches_scan_edge = false;
            std::queue<std::pair<int, int>> pending;
            pending.emplace(x, z);
            visited[index] = true;
            while (!pending.empty()) {
                const auto [current_x, current_z] =
                    pending.front();
                pending.pop();
                ++component_size;
                touches_scan_edge =
                    touches_scan_edge ||
                    current_x == first ||
                    current_x == last ||
                    current_z == first ||
                    current_z == last;

                for (const auto& [dx, dz] : neighbours) {
                    const auto next_x = current_x + dx;
                    const auto next_z = current_z + dz;
                    if (next_x < first || next_x > last ||
                        next_z < first || next_z > last) {
                        continue;
                    }
                    const auto next_index =
                        index_of(next_x, next_z);
                    if (visited[next_index] ||
                        columns[next_index].water_state ==
                            WaterState {0}) {
                        continue;
                    }
                    visited[next_index] = true;
                    pending.emplace(next_x, next_z);
                }
            }

            if (!touches_scan_edge) {
                ++interior_components;
                smallest_component =
                    std::min(
                        smallest_component,
                        component_size);
                // Je refuse les flaques résiduelles créées par une route ou
                // un objet : chaque composante doit rester une vraie pièce.
                CHECK(component_size >= 32);
            }
        }
    }

    auto shoreline_transitions = 0;
    for (int z = first; z <= last; ++z) {
        for (int x = first; x <= last; ++x) {
            const auto& column =
                columns[index_of(x, z)];
            if (column.water_state == WaterState {0}) {
                continue;
            }
            CHECK_FALSE(column.wall);

            for (const auto& [dx, dz] : neighbours) {
                const auto next_x = x + dx;
                const auto next_z = z + dz;
                if (next_x < first || next_x > last ||
                    next_z < first || next_z > last) {
                    continue;
                }
                const auto& neighbour =
                    columns[index_of(next_x, next_z)];
                if (neighbour.water_state != WaterState {0}) {
                    continue;
                }

                ++shoreline_transitions;
                CHECK((
                    neighbour.wall ||
                    neighbour.pool_surface ==
                        BackroomsPoolSurface::Shore));
                if (!neighbour.wall) {
                    CHECK((
                        neighbour.floor_block ==
                            to_block_id(
                                BlockType::PoolroomsMetal) ||
                        neighbour.floor_block ==
                            to_block_id(
                                BlockType::PoolroomsPlastic)));
                }
            }
        }
    }

    auto checkerboards = 0;
    for (int z = first; z < last; ++z) {
        for (int x = first; x < last; ++x) {
            const auto upper_left =
                columns[index_of(x, z)].water_state !=
                WaterState {0};
            const auto upper_right =
                columns[index_of(x + 1, z)].water_state !=
                WaterState {0};
            const auto lower_left =
                columns[index_of(x, z + 1)].water_state !=
                WaterState {0};
            const auto lower_right =
                columns[index_of(x + 1, z + 1)].water_state !=
                WaterState {0};
            checkerboards +=
                upper_left == lower_right &&
                        upper_right == lower_left &&
                        upper_left != upper_right
                    ? 1
                    : 0;
        }
    }

    CHECK(interior_components >= 8);
    CHECK(smallest_component >= 32);
    CHECK(shoreline_transitions > 500);
    CHECK(checkerboards == 0);
}

TEST_CASE("Poolrooms legacy geometry stays unchanged when selected explicitly") {
    constexpr auto seed = 424242;
    const BackroomsGenerator implicit_legacy(seed, -2);
    const BackroomsGenerator explicit_legacy(
        seed,
        -2,
        kBackroomsConnectorDistrictModules,
        BackroomsPoolGeometryProfile::LegacyFlat);

    CHECK(
        implicit_legacy.pool_geometry_profile() ==
        BackroomsPoolGeometryProfile::LegacyFlat);
    CHECK(
        explicit_legacy.pool_geometry_profile() ==
        BackroomsPoolGeometryProfile::LegacyFlat);

    auto wet_columns = 0;
    for (int z = -96; z <= 96; ++z) {
        for (int x = -96; x <= 96; ++x) {
            const auto implicit_column =
                implicit_legacy.sample_column(x, z);
            const auto explicit_column =
                explicit_legacy.sample_column(x, z);
            CHECK(implicit_column == explicit_column);

            if (implicit_column.pool_surface !=
                BackroomsPoolSurface::Water) {
                continue;
            }

            ++wet_columns;
            CHECK(implicit_column.floor_y == kBackroomsFloorY);
            CHECK(
                implicit_column.water_y ==
                kBackroomsFloorY + 1);
            CHECK(
                water_level_from_state(
                    implicit_column.water_state) ==
                static_cast<std::uint8_t>(5));
        }
    }
    CHECK(wet_columns > 1500);
}

TEST_CASE("Poolrooms recessed basins contain water between two floor levels") {
    constexpr auto seed = 424242;
    const BackroomsGenerator generator(
        seed,
        -2,
        kBackroomsConnectorDistrictModules,
        BackroomsPoolGeometryProfile::RecessedOneBlock);
    CHECK(
        generator.pool_geometry_profile() ==
        BackroomsPoolGeometryProfile::RecessedOneBlock);

    constexpr std::array<std::pair<int, int>, 4> neighbours {{
        {1, 0},
        {-1, 0},
        {0, 1},
        {0, -1},
    }};
    constexpr auto basin_size = 32;
    auto wet_columns = 0;
    auto dry_columns = 0;
    auto shore_columns = 0;
    auto shallow_basins = 0;
    auto overflowing_basins = 0;
    auto shoreline_transitions = 0;

    for (int basin_z = -4; basin_z <= 4; ++basin_z) {
        for (int basin_x = -4; basin_x <= 4; ++basin_x) {
            std::optional<std::uint8_t> basin_water_level;

            for (int local_z = 0; local_z < basin_size; ++local_z) {
                for (int local_x = 0; local_x < basin_size; ++local_x) {
                    const auto world_x =
                        basin_x * basin_size + local_x;
                    const auto world_z =
                        basin_z * basin_size + local_z;
                    const auto column =
                        generator.sample_column(world_x, world_z);

                    if (column.pool_surface !=
                        BackroomsPoolSurface::Water) {
                        CHECK(column.floor_y == kBackroomsFloorY);
                        CHECK(column.water_state == WaterState {0});
                        CHECK(column.water_y == kWorldMinY - 1);
                        dry_columns +=
                            column.pool_surface ==
                                    BackroomsPoolSurface::Dry
                                ? 1
                                : 0;
                        shore_columns +=
                            column.pool_surface ==
                                    BackroomsPoolSurface::Shore
                                ? 1
                                : 0;
                        continue;
                    }

                    ++wet_columns;
                    const auto water_level =
                        water_level_from_state(
                            column.water_state);
                    CHECK(column.floor_y == kBackroomsFloorY - 1);
                    CHECK(column.water_y == kBackroomsFloorY);
                    CHECK(column.floor_block ==
                          to_block_id(BlockType::PoolroomsWetTile));
                    CHECK(water_state_is_source(column.water_state));
                    CHECK(water_state_is_infinite(column.water_state));
                    CHECK((water_level == 5U ||
                           water_level == kMaxWaterLevel));
                    CHECK(
                        generator.sample_block(
                            world_x,
                            kBackroomsFloorY - 1,
                            world_z) ==
                        to_block_id(BlockType::PoolroomsWetTile));
                    CHECK(
                        generator.sample_block(
                            world_x,
                            kBackroomsFloorY,
                            world_z) ==
                        to_block_id(BlockType::Air));
                    CHECK(
                        generator.sample_water_state(
                            world_x,
                            kBackroomsFloorY,
                            world_z) ==
                        column.water_state);
                    CHECK(
                        generator.sample_water_state(
                            world_x,
                            kBackroomsFloorY + 1,
                            world_z) ==
                        WaterState {0});

                    if (!basin_water_level.has_value()) {
                        basin_water_level = water_level;
                    } else {
                        // Je refuse une pente liquide au milieu d'une même
                        // cuve, y compris quand une route en découpe le bord.
                        CHECK(*basin_water_level == water_level);
                    }

                    for (const auto& [dx, dz] : neighbours) {
                        const auto neighbour =
                            generator.sample_column(
                                world_x + dx,
                                world_z + dz);
                        if (neighbour.pool_surface ==
                            BackroomsPoolSurface::Water) {
                            continue;
                        }

                        ++shoreline_transitions;
                        CHECK(neighbour.floor_y == kBackroomsFloorY);
                        CHECK(
                            is_block_opaque(
                                generator.sample_block(
                                    world_x + dx,
                                    kBackroomsFloorY,
                                    world_z + dz)));
                    }
                }
            }

            if (!basin_water_level.has_value()) {
                continue;
            }
            shallow_basins +=
                *basin_water_level == 5U ? 1 : 0;
            overflowing_basins +=
                *basin_water_level == kMaxWaterLevel ? 1 : 0;
        }
    }

    const auto basin_count =
        shallow_basins + overflowing_basins;
    // Je garde cette premiere fenetre dense pour valider chaque voxel sans
    // alourdir inutilement toute la suite. Les routes peuvent supprimer une
    // cuve entiere, donc je mesure separement la repartition sur une fenetre
    // beaucoup plus large avec un seul echantillon humide par bassin.
    REQUIRE(basin_count > 8);
    CHECK(wet_columns > 1000);
    CHECK(dry_columns > 1000);
    CHECK(shore_columns > 500);
    CHECK(shoreline_transitions > 250);
    auto distribution_shallow_basins = 0;
    auto distribution_overflowing_basins = 0;
    constexpr auto distribution_radius = 16;
    for (int basin_z = -distribution_radius;
         basin_z <= distribution_radius;
         ++basin_z) {
        for (int basin_x = -distribution_radius;
             basin_x <= distribution_radius;
             ++basin_x) {
            auto sampled_level = std::optional<std::uint8_t> {};
            for (int local_z = 0;
                 local_z < basin_size && !sampled_level.has_value();
                 ++local_z) {
                for (int local_x = 0;
                     local_x < basin_size;
                     ++local_x) {
                    const auto column = generator.sample_column(
                        basin_x * basin_size + local_x,
                        basin_z * basin_size + local_z);
                    if (column.pool_surface ==
                        BackroomsPoolSurface::Water) {
                        sampled_level = water_level_from_state(
                            column.water_state);
                        break;
                    }
                }
            }
            if (!sampled_level.has_value()) {
                continue;
            }
            distribution_shallow_basins +=
                *sampled_level == 5U ? 1 : 0;
            distribution_overflowing_basins +=
                *sampled_level == kMaxWaterLevel ? 1 : 0;
        }
    }
    const auto distribution_basin_count =
        distribution_shallow_basins +
        distribution_overflowing_basins;
    REQUIRE(distribution_basin_count > 100);
    CHECK(distribution_shallow_basins > 0);
    CHECK(distribution_overflowing_basins > 0);
    const auto shallow_ratio =
        static_cast<double>(distribution_shallow_basins) /
        static_cast<double>(distribution_basin_count);
    CHECK(shallow_ratio >= 0.60);
    CHECK(shallow_ratio <= 0.90);
}

TEST_CASE("BackRooms connectors pair levels and keep trigger landings free") {
    constexpr auto seed = 0x51A7;
    constexpr std::array<int, 2> levels {{0, -2}};
    constexpr std::array<BackroomsConnectorDirection, 2> directions {{
        BackroomsConnectorDirection::Up,
        BackroomsConnectorDirection::Down,
    }};

    for (const auto level : levels) {
        const BackroomsGenerator generator(seed, level);
        for (const auto direction : directions) {
            const auto connector =
                find_connector_in_district(
                    generator,
                    direction,
                    0,
                    0);
            REQUIRE(connector.has_value());
            CHECK(
                connector->destination_level ==
                level +
                    (direction ==
                             BackroomsConnectorDirection::Up
                         ? 1
                         : -1));
            CHECK(
                generator.connector_near(
                    connector->trigger_block.x,
                    connector->trigger_block.y,
                    connector->trigger_block.z,
                    0) ==
                connector);
            CHECK(
                generator.sample_block(
                    connector->trigger_block.x,
                    connector->trigger_block.y,
                    connector->trigger_block.z) ==
                to_block_id(BlockType::Air));
            CHECK(
                generator.sample_water_state(
                    connector->trigger_block.x,
                    connector->trigger_block.y,
                    connector->trigger_block.z) ==
                0);
            CHECK(
                generator.is_walkable(
                    connector->trigger_block.x,
                    connector->trigger_block.z));

            const BackroomsGenerator destination(
                seed,
                connector->destination_level);
            const auto paired_direction =
                direction == BackroomsConnectorDirection::Up
                    ? BackroomsConnectorDirection::Down
                    : BackroomsConnectorDirection::Up;
            const auto paired =
                destination.connector_near(
                    connector->trigger_block.x,
                    connector->trigger_block.y,
                    connector->trigger_block.z,
                    0);
            REQUIRE(paired.has_value());
            CHECK(paired->direction == paired_direction);
            CHECK(
                paired->trigger_block ==
                connector->trigger_block);
            CHECK(
                destination.sample_block(
                    connector->
                        destination_landing_block.x,
                    connector->
                        destination_landing_block.y,
                    connector->
                        destination_landing_block.z) ==
                to_block_id(BlockType::Air));
            CHECK(
                destination.sample_water_state(
                    connector->
                        destination_landing_block.x,
                    connector->
                        destination_landing_block.y,
                    connector->
                        destination_landing_block.z) ==
                0);
            CHECK(
                destination.is_walkable(
                    connector->
                        destination_landing_block.x,
                    connector->
                        destination_landing_block.z));

            auto visible_structure_blocks = 0;
            for (int z_offset = -4;
                 z_offset <= 4;
                 ++z_offset) {
                for (int x_offset = -4;
                     x_offset <= 4;
                     ++x_offset) {
                    for (int y = kBackroomsFloorY + 1;
                         y <= kBackroomsFloorY + 6;
                         ++y) {
                        const auto block =
                            generator.sample_block(
                                connector->trigger_block.x +
                                    x_offset,
                                y,
                                connector->trigger_block.z +
                                    z_offset);
                        visible_structure_blocks +=
                            block ==
                                    to_block_id(
                                        BlockType::
                                            PoolroomsMetal) ||
                                block ==
                                    to_block_id(
                                        BlockType::
                                            PoolroomsPlastic)
                                ? 1
                                : 0;
                    }
                }
            }
            CHECK(visible_structure_blocks >= 8);
        }
    }
}

TEST_CASE("Poolrooms connectors expose elevated stairs slides and balconies safely") {
    struct MotifCase {
        int level = -2;
        BackroomsConnectorDirection direction =
            BackroomsConnectorDirection::Up;
        BackroomsConnectorStyle style =
            BackroomsConnectorStyle::Stairs;
        BackroomsElevatedFeature feature =
            BackroomsElevatedFeature::StairFlight;
    };
    constexpr auto seed = 0x51A7;
    constexpr std::array<MotifCase, 2> cases {{
        {
            -2,
            BackroomsConnectorDirection::Up,
            BackroomsConnectorStyle::Stairs,
            BackroomsElevatedFeature::StairFlight,
        },
        {
            -12,
            BackroomsConnectorDirection::Down,
            BackroomsConnectorStyle::Slide,
            BackroomsElevatedFeature::SlideChute,
        },
    }};

    for (const auto& motif_case : cases) {
        const BackroomsGenerator generator(
            seed,
            motif_case.level);
        const BackroomsGenerator identical(
            seed,
            motif_case.level);
        const auto connector =
            find_connector_in_district(
                generator,
                motif_case.direction,
                0,
                0);
        REQUIRE(connector.has_value());
        REQUIRE(connector->style == motif_case.style);

        for (int z_offset = -3;
             z_offset <= 3;
             ++z_offset) {
            for (int x_offset = -3;
                 x_offset <= 3;
                 ++x_offset) {
                const auto world_x =
                    connector->trigger_block.x + x_offset;
                const auto world_z =
                    connector->trigger_block.z + z_offset;
                const auto column =
                    generator.sample_column(world_x, world_z);
                CHECK(column.guaranteed_route);
                CHECK_FALSE(column.wall);
                CHECK(
                    generator.sample_water_state(
                        world_x,
                        kBackroomsFloorY + 1,
                        world_z) ==
                    0);
                for (int y = kBackroomsFloorY + 1;
                     y <= kBackroomsFloorY + 4;
                     ++y) {
                    CHECK(
                        generator.sample_block(
                            world_x,
                            y,
                            world_z) ==
                        to_block_id(BlockType::Air));
                }
            }
        }

        auto flight_columns = 0;
        auto balcony_columns = 0;
        for (int z_offset = -12;
             z_offset <= 12;
             ++z_offset) {
            for (int x_offset = -12;
                 x_offset <= 12;
                 ++x_offset) {
                const auto world_x =
                    connector->trigger_block.x + x_offset;
                const auto world_z =
                    connector->trigger_block.z + z_offset;
                const auto column =
                    generator.sample_column(world_x, world_z);
                CHECK(
                    column ==
                    identical.sample_column(world_x, world_z));
                if (column.guaranteed_route) {
                    CHECK_FALSE(column.wall);
                }
                if (column.overhead_bottom_y <=
                    column.overhead_top_y) {
                    CHECK(
                        column.overhead_bottom_y -
                            column.floor_y >=
                        5);
                    CHECK(
                        column.overhead_top_y <
                        column.ceiling_y);
                }
                flight_columns +=
                    column.elevated_feature ==
                            motif_case.feature
                        ? 1
                        : 0;
                balcony_columns +=
                    column.elevated_feature ==
                            BackroomsElevatedFeature::Balcony
                        ? 1
                        : 0;
            }
        }
        CHECK(flight_columns >= 10);
        CHECK(balcony_columns >= 15);

        const auto& landing =
            connector->destination_landing_block;
        CHECK(
            generator.sample_block(
                connector->trigger_block.x,
                connector->trigger_block.y,
                connector->trigger_block.z) ==
            to_block_id(BlockType::Air));
        const BackroomsGenerator destination(
            seed,
            connector->destination_level);
        CHECK(
            destination.sample_block(
                landing.x,
                landing.y,
                landing.z) ==
            to_block_id(BlockType::Air));
    }
}

TEST_CASE("BackRooms distribution contains every spatial language") {
    const BackroomsGenerator generator(424242);
    std::set<BackroomsArchetype> archetypes;
    std::set<BackroomsPalette> palettes;
    std::set<BackroomsTension> tensions;
    std::set<BackroomsLightState> lights;
    auto minimum_ceiling_height = 100;
    auto maximum_ceiling_height = 0;

    for (int module_z = -24; module_z <= 24; ++module_z) {
        for (int module_x = -24; module_x <= 24; ++module_x) {
            const auto descriptor =
                generator.module_descriptor(module_x, module_z);
            archetypes.insert(descriptor.archetype);
            palettes.insert(descriptor.palette);
            tensions.insert(descriptor.tension);

            for (int local_z = 0;
                 local_z < kBackroomsModuleSize;
                 local_z += 7) {
                for (int local_x = 0;
                     local_x < kBackroomsModuleSize;
                     local_x += 7) {
                    const auto sample =
                        generator.sample_column(
                            module_x * kBackroomsModuleSize +
                                local_x,
                            module_z * kBackroomsModuleSize +
                                local_z);
                    minimum_ceiling_height =
                        std::min(
                            minimum_ceiling_height,
                            sample.ceiling_y -
                                sample.floor_y);
                    maximum_ceiling_height =
                        std::max(
                            maximum_ceiling_height,
                            sample.ceiling_y -
                                sample.floor_y);
                    lights.insert(sample.light_state);
                }
            }
        }
    }

    CHECK(archetypes.size() == 9U);
    CHECK(palettes.size() == 6U);
    CHECK(tensions.size() == 6U);
    CHECK(lights.contains(BackroomsLightState::Active));
    CHECK(lights.contains(BackroomsLightState::Failed));
    CHECK(lights.contains(BackroomsLightState::Emergency));
    CHECK(minimum_ceiling_height <= 6);
    CHECK(maximum_ceiling_height >= 20);
}

TEST_CASE("BackRooms generation profile emits enclosed office blocks") {
    const WorldGenerator generator(
        1337,
        WorldGenerationProfile::Backrooms,
        WorldGenerationVersion::BackroomsV1);

    CHECK(
        generator.profile() ==
        WorldGenerationProfile::Backrooms);
    CHECK(
        generator.generation_version() ==
        WorldGenerationVersion::BackroomsV1);

    const BackroomsGenerator layout(1337);
    const auto spawn = layout.spawn_block();
    CHECK(
        generator.sample_block(
            spawn.x,
            kBackroomsFloorY,
            spawn.z) ==
        to_block_id(BlockType::BackroomsCarpet));
    CHECK(
        generator.sample_block(
            spawn.x,
            spawn.y,
            spawn.z) ==
        to_block_id(BlockType::Air));

    const auto ceiling =
        layout.sample_column(spawn.x, spawn.z);
    CHECK(
        generator.sample_block(
            spawn.x,
            ceiling.ceiling_y,
            spawn.z) !=
        to_block_id(BlockType::Air));
    CHECK(
        generator.sample_water_state(
            spawn.x,
            spawn.y,
            spawn.z) ==
        static_cast<WaterState>(0));
}

TEST_CASE("BackRooms and Poolrooms ceilings seal one continuous underground mass") {
    constexpr std::array<int, 2U> logical_levels {{0, -2}};

    for (const auto logical_level : logical_levels) {
        CAPTURE(logical_level);
        const BackroomsGenerator generator(424242, logical_level);
        CHECK(
            generator.sample_column(0, 0).roof_block ==
            (logical_level <= -2
                 ? to_block_id(BlockType::PoolroomsTile)
                 : to_block_id(BlockType::BackroomsConcrete)));
        auto stepped_ceiling_edges = 0;
        auto active_fixtures = 0;

        for (int world_z = -70; world_z <= 70; world_z += 3) {
            for (int world_x = -70; world_x <= 70; world_x += 3) {
                const auto column =
                    generator.sample_column(world_x, world_z);
                REQUIRE(column.ceiling_y <= kBackroomsRoofY);

                const auto block_above_ceiling =
                    generator.sample_block(
                        world_x,
                        column.ceiling_y + 1,
                        world_z);
                if (column.ceiling_y < kBackroomsRoofY) {
                    CHECK(block_above_ceiling == column.roof_block);
                    CHECK(block_properties(block_above_ceiling).opaque);
                    // Je garde un seul panneau lumineux sous la masse du toit :
                    // le remplissage supérieur ne doit jamais devenir émissif.
                    CHECK(
                        block_properties(block_above_ceiling)
                            .emissive_level ==
                        0U);
                } else {
                    // Quand une salle atteint la hauteur maximale, son propre
                    // plafond constitue la face inférieure de la dalle commune.
                    CHECK(
                        block_above_ceiling ==
                        to_block_id(BlockType::Air));
                }
                // Je ferme toutes les colonnes sur une même dalle supérieure.
                // L'air au-dessus reste inaccessible et ne coûte aucun maillage.
                CHECK(
                    block_properties(
                        generator.sample_block(
                            world_x,
                            kBackroomsRoofY,
                            world_z))
                        .opaque);
                CHECK(
                    generator.sample_block(
                        world_x,
                        kBackroomsRoofY + 1,
                        world_z) ==
                    to_block_id(BlockType::Air));

                if (column.light_state == BackroomsLightState::Active) {
                    ++active_fixtures;
                }

                for (const auto& [offset_x, offset_z] :
                     std::array<std::pair<int, int>, 2U> {{{1, 0}, {0, 1}}}) {
                    const auto adjacent =
                        generator.sample_column(
                            world_x + offset_x,
                            world_z + offset_z);
                    if (adjacent.ceiling_y == column.ceiling_y) {
                        continue;
                    }

                    ++stepped_ceiling_edges;
                    const auto lower_is_current =
                        column.ceiling_y < adjacent.ceiling_y;
                    const auto lower_x =
                        lower_is_current
                            ? world_x
                            : world_x + offset_x;
                    const auto lower_z =
                        lower_is_current
                            ? world_z
                            : world_z + offset_z;
                    const auto lower_ceiling =
                        std::min(
                            column.ceiling_y,
                            adjacent.ceiling_y);
                    const auto upper_ceiling =
                        std::max(
                            column.ceiling_y,
                            adjacent.ceiling_y);
                    for (int y = lower_ceiling + 1;
                         y <= upper_ceiling;
                         ++y) {
                        CHECK(
                            block_properties(
                                generator.sample_block(
                                    lower_x,
                                    y,
                                    lower_z))
                                .opaque);
                    }
                }
            }
        }

        CHECK(stepped_ceiling_edges > 20);
        CHECK(active_fixtures > 0);
    }
}

TEST_CASE("BackRooms fluorescent ramps keep one coherent electrical state") {
    const BackroomsGenerator generator(424242);
    std::size_t checked_neighbours = 0U;

    for (int module_z = -3; module_z <= 3; ++module_z) {
        for (int module_x = -3; module_x <= 3; ++module_x) {
            const auto descriptor =
                generator.module_descriptor(module_x, module_z);
            for (int local_z = 0;
                 local_z < kBackroomsModuleSize;
                 ++local_z) {
                for (int local_x = 0;
                     local_x < kBackroomsModuleSize;
                     ++local_x) {
                    const auto next_local_x =
                        local_x +
                        (descriptor.primary_axis_x ? 1 : 0);
                    const auto next_local_z =
                        local_z +
                        (descriptor.primary_axis_x ? 0 : 1);
                    if (next_local_x >= kBackroomsModuleSize ||
                        next_local_z >= kBackroomsModuleSize) {
                        continue;
                    }

                    const auto world_x =
                        module_x * kBackroomsModuleSize + local_x;
                    const auto world_z =
                        module_z * kBackroomsModuleSize + local_z;
                    const auto next_world_x =
                        module_x * kBackroomsModuleSize + next_local_x;
                    const auto next_world_z =
                        module_z * kBackroomsModuleSize + next_local_z;
                    const auto current =
                        generator.sample_column(world_x, world_z);
                    const auto next =
                        generator.sample_column(
                            next_world_x,
                            next_world_z);
                    if (current.wall ||
                        next.wall ||
                        current.light_state ==
                            BackroomsLightState::None ||
                        next.light_state ==
                            BackroomsLightState::None) {
                        continue;
                    }

                    ++checked_neighbours;
                    CHECK(current.light_state == next.light_state);
                }
            }
        }
    }

    CHECK(checked_neighbours > 500U);
}

TEST_CASE("BackRooms light blocks expose only the intended emission levels") {
    CHECK(
        to_block_id(BlockType::BackroomsCeilingTile) ==
        static_cast<BlockId>(49));
    CHECK(
        to_block_id(BlockType::BackroomsFluorescentLight) ==
        static_cast<BlockId>(50));
    CHECK(
        to_block_id(BlockType::BackroomsFailedLight) ==
        static_cast<BlockId>(51));
    CHECK(
        to_block_id(BlockType::BackroomsEmergencyLight) ==
        static_cast<BlockId>(52));

    CHECK(
        block_emissive_level(
            to_block_id(BlockType::BackroomsCeilingTile)) == 0U);
    CHECK(
        block_emissive_level(
            to_block_id(BlockType::BackroomsFluorescentLight)) == 14U);
    CHECK(
        block_emissive_level(
            to_block_id(BlockType::BackroomsFailedLight)) == 0U);
    CHECK(
        block_emissive_level(
            to_block_id(BlockType::BackroomsEmergencyLight)) == 11U);
}

TEST_CASE("BackRooms grading preserves realistic bounded interior light") {
    constexpr auto seed = 424242;
    const BackroomsGenerator generator(seed);
    std::array<EnvironmentState, 6> tension_states {};
    std::array<bool, 6> found_tensions {};

    for (int module_z = -16; module_z <= 16; ++module_z) {
        for (int module_x = -16; module_x <= 16; ++module_x) {
            const auto world_x =
                module_x * kBackroomsModuleSize +
                kBackroomsModuleSize / 2;
            const auto world_z =
                module_z * kBackroomsModuleSize +
                kBackroomsModuleSize / 2;
            const auto descriptor =
                generator.module_descriptor(module_x, module_z);
            const auto state =
                make_backrooms_environment_state(
                    37.5F,
                    seed,
                    static_cast<float>(world_x),
                    static_cast<float>(world_z));

            CHECK(state.enclosed_interior);
            CHECK(state.suppress_gameplay_hud);
            CHECK(state.daylight_factor == doctest::Approx(0.0F));
            CHECK(state.sun_color == glm::vec3 {0.0F});
            CHECK(state.sun_direction == glm::vec3 {0.0F, -1.0F, 0.0F});
            CHECK(finite_color(state.ambient_color));
            CHECK(finite_color(state.block_light_color));
            CHECK(finite_color(state.fog_color));
            CHECK(perceptual_luminance(state.ambient_color) >= 0.072F);
            // Je garde l'ambiance pour colorer le rebond des lampes, sans
            // laisser la teinte nocturne relever les pixels noirs.
            CHECK(state.night_tint_color == glm::vec3 {0.0F});
            CHECK(state.exposure >= 0.66F);
            CHECK(state.exposure <= 1.02F);
            CHECK(state.height_fog_density >= 0.0045F);
            CHECK(state.height_fog_density <= 0.0120F);
            CHECK(state.vignette_strength >= 0.08F);
            CHECK(state.vignette_strength <= 0.23F);
            CHECK(state.contrast == doctest::Approx(1.01F));
            CHECK(state.saturation_boost == doctest::Approx(0.88F));
            CHECK(state.glow_threshold == doctest::Approx(0.78F));
            CHECK(state.glow_strength == doctest::Approx(0.13F));
            CHECK(state.block_light_color.r == doctest::Approx(0.94F));
            CHECK(state.block_light_color.g == doctest::Approx(1.00F));
            CHECK(state.block_light_color.b == doctest::Approx(0.82F));

            const auto tension_index =
                static_cast<std::size_t>(descriptor.tension);
            if (!found_tensions[tension_index]) {
                tension_states[tension_index] = state;
                found_tensions[tension_index] = true;
            }
        }
    }

    CHECK(std::ranges::all_of(found_tensions, [](bool found) {
        return found;
    }));
    const auto familiarity_index =
        static_cast<std::size_t>(BackroomsTension::Familiarity);
    const auto blackout_index =
        static_cast<std::size_t>(BackroomsTension::Blackout);
    CHECK(
        tension_states[familiarity_index].exposure >
        tension_states[blackout_index].exposure);
    CHECK(
        tension_states[familiarity_index].height_fog_density <
        tension_states[blackout_index].height_fog_density);
    CHECK(
        tension_states[familiarity_index].vignette_strength <
        tension_states[blackout_index].vignette_strength);
}

TEST_CASE("BackRooms lighting crosses module thresholds without one-frame jumps") {
    constexpr auto seed = 424242;
    constexpr auto elapsed_seconds = 19.75F;
    const BackroomsGenerator generator(seed);

    auto boundary_module_x = 0;
    auto boundary_module_z = 0;
    auto found_distinct_neighbours = false;
    for (int module_z = -8;
         module_z <= 8 && !found_distinct_neighbours;
         ++module_z) {
        for (int module_x = -8;
             module_x < 8;
             ++module_x) {
            const auto left =
                generator.module_descriptor(module_x, module_z);
            const auto right =
                generator.module_descriptor(module_x + 1, module_z);
            if (left.palette != right.palette ||
                left.tension != right.tension) {
                boundary_module_x = module_x;
                boundary_module_z = module_z;
                found_distinct_neighbours = true;
                break;
            }
        }
    }
    REQUIRE(found_distinct_neighbours);

    const auto boundary_x = static_cast<float>(
        (boundary_module_x + 1) * kBackroomsModuleSize);
    const auto sample_z = static_cast<float>(
        boundary_module_z * kBackroomsModuleSize +
        kBackroomsModuleSize / 2);
    const auto left_center =
        make_backrooms_environment_state(
            elapsed_seconds,
            seed,
            boundary_x - 8.0F,
            sample_z);
    const auto right_center =
        make_backrooms_environment_state(
            elapsed_seconds,
            seed,
            boundary_x + 8.0F,
            sample_z);
    const auto boundary =
        make_backrooms_environment_state(
            elapsed_seconds,
            seed,
            boundary_x,
            sample_z);
    const auto just_before =
        make_backrooms_environment_state(
            elapsed_seconds,
            seed,
            boundary_x - 0.001F,
            sample_z);
    const auto just_after =
        make_backrooms_environment_state(
            elapsed_seconds,
            seed,
            boundary_x + 0.001F,
            sample_z);

    // Je verrouille la moyenne exacte au seuil et une dérivée douce des deux
    // côtés : franchir une porte ne peut plus changer l'exposition en une frame.
    for (int channel = 0; channel < 3; ++channel) {
        CHECK(
            boundary.ambient_color[channel] ==
            doctest::Approx(
                (left_center.ambient_color[channel] +
                 right_center.ambient_color[channel]) *
                0.5F)
                .epsilon(0.00001));
        CHECK(
            boundary.fog_color[channel] ==
            doctest::Approx(
                (left_center.fog_color[channel] +
                 right_center.fog_color[channel]) *
                0.5F)
                .epsilon(0.00001));
        CHECK(
            std::abs(
                just_before.ambient_color[channel] -
                just_after.ambient_color[channel]) <
            0.00001F);
    }
    CHECK(
        boundary.exposure ==
        doctest::Approx(
            (left_center.exposure + right_center.exposure) * 0.5F)
            .epsilon(0.00001));
    CHECK(
        boundary.height_fog_density ==
        doctest::Approx(
            (left_center.height_fog_density +
             right_center.height_fog_density) *
            0.5F)
            .epsilon(0.00001));
    CHECK(
        boundary.vignette_strength ==
        doctest::Approx(
            (left_center.vignette_strength +
             right_center.vignette_strength) *
            0.5F)
            .epsilon(0.00001));
    CHECK(
        std::abs(just_before.exposure - just_after.exposure) <
        0.00001F);
    CHECK(
        std::abs(
            just_before.height_fog_density -
            just_after.height_fog_density) <
        0.00001F);
    CHECK(
        std::abs(
            just_before.vignette_strength -
            just_after.vignette_strength) <
        0.00001F);
}

} // namespace valcraft
