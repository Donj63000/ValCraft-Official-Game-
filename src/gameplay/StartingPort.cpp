#include "gameplay/StartingPort.h"

#include "gameplay/SeaAdventure.h"
#include "world/OceanAdventureLayout.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace valcraft {

namespace {

constexpr int kPortHeadroom = 6;
constexpr int kBuildingWallHeight = 4;

auto hash_value(std::uint32_t value) noexcept -> std::uint32_t {
    value ^= value >> 16U;
    value *= 2246822519U;
    value ^= value >> 13U;
    value *= 3266489917U;
    return value ^ (value >> 16U);
}

auto hash_coords(int x, int z, int seed, int salt = 0) noexcept -> std::uint32_t {
    auto value = static_cast<std::uint32_t>(x) * 374761393U;
    value ^= static_cast<std::uint32_t>(z) * 668265263U;
    value ^= static_cast<std::uint32_t>(seed) * 2246822519U;
    value ^= static_cast<std::uint32_t>(salt) * 3266489917U;
    return hash_value(value);
}

auto valid_area(const StartingPortArea& area) noexcept -> bool {
    return area.min_x <= area.max_x && area.min_z <= area.max_z && is_world_y_valid(area.surface_y);
}

void set_block_if_needed(World& world, int x, int y, int z, BlockId block_id) {
    if (!is_world_y_valid(y)) {
        return;
    }

    const auto current_block = world.peek_block_or_generated(x, y, z);
    const auto current_water = world.peek_water_level_or_generated(x, y, z);
    if (block_id == to_block_id(BlockType::Air)) {
        if (current_block == block_id && current_water == 0U) {
            return;
        }
    } else if (current_block == block_id && current_water == 0U) {
        return;
    }
    world.set_block(x, y, z, block_id);
}

auto masonry_block(int x, int z, int seed, int salt) noexcept -> BlockId {
    const auto value = hash_coords(x, z, seed, salt) % 19U;
    if (value == 0U) {
        return to_block_id(BlockType::MossyStone);
    }
    if (value <= 4U) {
        return to_block_id(BlockType::Cobblestone);
    }
    return to_block_id(BlockType::Stone);
}

void clear_area_above(World& world, const StartingPortArea& area, int height) {
    if (!valid_area(area) || height <= 0) {
        return;
    }
    for (int z = area.min_z; z <= area.max_z; ++z) {
        for (int x = area.min_x; x <= area.max_x; ++x) {
            for (int y = area.surface_y + 1; y <= area.surface_y + height; ++y) {
                set_block_if_needed(world, x, y, z, to_block_id(BlockType::Air));
            }
        }
    }
}

auto find_support_ground(World& world, int x, int top_y, int z) -> int {
    for (int y = top_y; y >= kWorldMinY; --y) {
        const auto block_id = world.peek_block_or_generated(x, y, z);
        if (world.peek_water_level_or_generated(x, y, z) == 0U && is_block_collidable(block_id)) {
            return y;
        }
    }
    return kWorldMinY - 1;
}

void build_support_column(World& world, int x, int top_y, int z, BlockId block_id) {
    const auto ground_y = find_support_ground(world, x, top_y, z);
    for (int y = std::max(ground_y + 1, kWorldMinY); y <= top_y; ++y) {
        set_block_if_needed(world, x, y, z, block_id);
    }
}

void build_supported_surface(World& world,
                             const StartingPortArea& area,
                             int seed,
                             int salt,
                             bool wooden,
                             int support_spacing,
                             int headroom) {
    if (!valid_area(area)) {
        return;
    }

    clear_area_above(world, area, headroom);
    const auto support_block = wooden ? to_block_id(BlockType::PineWood) : to_block_id(BlockType::Stone);
    const auto spacing = std::max(support_spacing, 1);
    for (int z = area.min_z; z <= area.max_z; ++z) {
        for (int x = area.min_x; x <= area.max_x; ++x) {
            const auto x_grid = (x - area.min_x) % spacing == 0 || x == area.max_x;
            const auto z_grid = (z - area.min_z) % spacing == 0 || z == area.max_z;
            if (x_grid && z_grid) {
                build_support_column(world, x, area.surface_y - 1, z, support_block);
            }

            const auto floor_block = wooden
                ? to_block_id(BlockType::Planks)
                : masonry_block(x, z, seed, salt);
            set_block_if_needed(world, x, area.surface_y, z, floor_block);
        }
    }
}

auto make_building(StartingPortBuildingRole role,
                   int min_x,
                   int max_x,
                   int min_z,
                   int max_z,
                   int floor_y,
                   int seed,
                   int salt) -> StartingPortBuilding {
    StartingPortBuilding building {};
    building.role = role;
    building.footprint = {min_x, max_x, min_z, max_z, floor_y};
    building.door = {max_x, floor_y + 1, min_z + (max_z - min_z) / 2};
    building.interior = {
        min_x + (max_x - min_x) / 2,
        floor_y + 1,
        min_z + (max_z - min_z) / 2,
    };
    building.variant_seed = hash_coords(min_x, min_z, seed, salt);
    return building;
}

auto is_building_window(const StartingPortBuilding& building, int x, int y, int z) noexcept -> bool {
    if (y != building.footprint.surface_y + 2) {
        return false;
    }
    const auto width = building.footprint.max_x - building.footprint.min_x;
    const auto depth = building.footprint.max_z - building.footprint.min_z;
    const auto first_window_x = building.footprint.min_x + std::max(2, width / 3);
    const auto second_window_x = building.footprint.max_x - std::max(2, width / 3);
    const auto first_window_z = building.footprint.min_z + std::max(2, depth / 3);
    const auto second_window_z = building.footprint.max_z - std::max(2, depth / 3);
    const auto on_north_or_south =
        (z == building.footprint.min_z || z == building.footprint.max_z) &&
        (x == first_window_x || x == second_window_x);
    const auto on_west_or_east =
        (x == building.footprint.min_x || x == building.footprint.max_x) &&
        (z == first_window_z || z == second_window_z);
    return on_north_or_south || on_west_or_east;
}

void build_building_path(World& world, const StartingPortBuilding& building, const StartingPortLayout& layout) {
    const auto path_min_x = building.door.x + 1;
    const auto path_max_x = layout.stone_quay.min_x;
    if (path_min_x > path_max_x) {
        return;
    }
    for (int z = building.door.z - 1; z <= building.door.z + 1; ++z) {
        for (int x = path_min_x; x <= path_max_x; ++x) {
            set_block_if_needed(
                world,
                x,
                layout.quay_surface_y,
                z,
                masonry_block(x, z, layout.seed, 510));
            set_block_if_needed(world, x, layout.quay_surface_y + 1, z, to_block_id(BlockType::Air));
            set_block_if_needed(world, x, layout.quay_surface_y + 2, z, to_block_id(BlockType::Air));
        }
    }
}

void build_building(World& world, const StartingPortBuilding& building, const StartingPortLayout& layout) {
    const auto& area = building.footprint;
    if (!valid_area(area)) {
        return;
    }

    clear_area_above(world, area, kBuildingWallHeight + 2);
    for (int z = area.min_z; z <= area.max_z; ++z) {
        for (int x = area.min_x; x <= area.max_x; ++x) {
            // Je pose une dalle continue sous chaque batiment pour que l'eau ne
            // puisse jamais remonter dans les interieurs apres la simulation.
            set_block_if_needed(world, x, area.surface_y - 1, z, masonry_block(x, z, layout.seed, 601));
            const auto edge = x == area.min_x || x == area.max_x || z == area.min_z || z == area.max_z;
            const auto floor_block = edge
                ? masonry_block(x, z, layout.seed, 602)
                : to_block_id(BlockType::Planks);
            set_block_if_needed(world, x, area.surface_y, z, floor_block);

            if (!edge) {
                continue;
            }
            for (int y = area.surface_y + 1; y <= area.surface_y + kBuildingWallHeight; ++y) {
                const auto doorway = x == building.door.x && z == building.door.z && y <= building.door.y + 1;
                if (doorway) {
                    set_block_if_needed(world, x, y, z, to_block_id(BlockType::Air));
                    continue;
                }

                const auto corner = (x == area.min_x || x == area.max_x) &&
                                    (z == area.min_z || z == area.max_z);
                BlockId wall_block = masonry_block(x, z, layout.seed, 603 + y);
                if (corner || (building.role == StartingPortBuildingRole::Warehouse && y >= area.surface_y + 3)) {
                    wall_block = corner ? to_block_id(BlockType::Wood) : to_block_id(BlockType::Planks);
                } else if (is_building_window(building, x, y, z)) {
                    wall_block = to_block_id(BlockType::Glass);
                }
                set_block_if_needed(world, x, y, z, wall_block);
            }
        }
    }

    const auto roof_y = area.surface_y + kBuildingWallHeight + 1;
    for (int z = area.min_z - 1; z <= area.max_z + 1; ++z) {
        for (int x = area.min_x - 1; x <= area.max_x + 1; ++x) {
            set_block_if_needed(world, x, roof_y, z, to_block_id(BlockType::Planks));
        }
    }

    if (building.role == StartingPortBuildingRole::HarborMasterOffice) {
        const auto desk_x = area.min_x + 2;
        const auto desk_z = area.min_z + 3;
        set_block_if_needed(world, desk_x, area.surface_y + 1, desk_z, to_block_id(BlockType::Planks));
        set_block_if_needed(world, desk_x + 1, area.surface_y + 1, desk_z, to_block_id(BlockType::Planks));
    } else {
        for (int z = area.min_z + 2; z <= area.min_z + 5; z += 3) {
            set_block_if_needed(world, area.min_x + 2, area.surface_y + 1, z, to_block_id(BlockType::Planks));
            set_block_if_needed(world, area.min_x + 3, area.surface_y + 1, z, to_block_id(BlockType::Planks));
        }
    }

    set_block_if_needed(world, building.door.x, building.door.y, building.door.z, to_block_id(BlockType::Air));
    set_block_if_needed(world, building.door.x, building.door.y + 1, building.door.z, to_block_id(BlockType::Air));
    set_block_if_needed(world, building.interior.x, building.interior.y, building.interior.z, to_block_id(BlockType::Air));
    set_block_if_needed(world, building.interior.x, building.interior.y + 1, building.interior.z, to_block_id(BlockType::Air));
    build_building_path(world, building, layout);
}

void build_lighthouse(World& world, const StartingPortLayout& layout) {
    const auto center_x = layout.lighthouse_base.x;
    const auto center_z = layout.lighthouse_base.z;
    const auto base_y = layout.lighthouse_base.y;

    for (int z = center_z - 3; z <= center_z + 3; ++z) {
        for (int x = center_x - 3; x <= center_x + 3; ++x) {
            build_support_column(world, x, base_y - 1, z, to_block_id(BlockType::Stone));
            set_block_if_needed(world, x, base_y, z, masonry_block(x, z, layout.seed, 701));
        }
    }

    for (int y = base_y + 1; y <= base_y + 10; ++y) {
        const auto radius = y <= base_y + 3 ? 2 : 1;
        for (int z = center_z - radius; z <= center_z + radius; ++z) {
            for (int x = center_x - radius; x <= center_x + radius; ++x) {
                const auto edge = x == center_x - radius || x == center_x + radius ||
                                  z == center_z - radius || z == center_z + radius;
                set_block_if_needed(
                    world,
                    x,
                    y,
                    z,
                    edge ? masonry_block(x, z, layout.seed, 702 + y) : to_block_id(BlockType::Air));
            }
        }
    }

    const auto lantern_floor_y = base_y + 11;
    for (int z = center_z - 2; z <= center_z + 2; ++z) {
        for (int x = center_x - 2; x <= center_x + 2; ++x) {
            set_block_if_needed(world, x, lantern_floor_y, z, to_block_id(BlockType::Stone));
            set_block_if_needed(world, x, lantern_floor_y + 3, z, to_block_id(BlockType::Planks));
        }
    }
    for (int y = lantern_floor_y + 1; y <= lantern_floor_y + 2; ++y) {
        for (int z = center_z - 1; z <= center_z + 1; ++z) {
            for (int x = center_x - 1; x <= center_x + 1; ++x) {
                const auto edge = x == center_x - 1 || x == center_x + 1 ||
                                  z == center_z - 1 || z == center_z + 1;
                set_block_if_needed(
                    world,
                    x,
                    y,
                    z,
                    edge ? to_block_id(BlockType::Glass) : to_block_id(BlockType::Air));
            }
        }
    }

    set_block_if_needed(world, center_x, lantern_floor_y + 1, center_z, to_block_id(BlockType::Torch));
    const auto torch_y = lantern_floor_y + 1;
    set_block_if_needed(world, center_x - 2, torch_y, center_z, to_block_id(BlockType::Torch));
    set_block_if_needed(world, center_x + 2, torch_y, center_z, to_block_id(BlockType::Torch));
    set_block_if_needed(world, center_x, torch_y, center_z - 2, to_block_id(BlockType::Torch));
    set_block_if_needed(world, center_x, torch_y, center_z + 2, to_block_id(BlockType::Torch));
}

void build_crane(World& world, const StartingPortLayout& layout) {
    const auto base_x = layout.crane_base.x;
    const auto base_y = layout.crane_base.y;
    const auto base_z = layout.crane_base.z;
    const auto boom_y = base_y + 9;
    const auto boom_tip_x = layout.gangway.max_x;

    for (int y = base_y + 1; y <= boom_y; ++y) {
        set_block_if_needed(world, base_x, y, base_z, to_block_id(BlockType::Wood));
        set_block_if_needed(world, base_x + 1, y, base_z, to_block_id(BlockType::Wood));
    }
    for (int x = base_x - 2; x <= boom_tip_x; ++x) {
        set_block_if_needed(world, x, boom_y, base_z, to_block_id(BlockType::PineWood));
    }
    for (int offset = 1; offset <= 5; ++offset) {
        set_block_if_needed(world, base_x + offset, boom_y - offset, base_z, to_block_id(BlockType::Wood));
    }
    for (int y = base_y + 5; y < boom_y; ++y) {
        set_block_if_needed(world, boom_tip_x, y, base_z, to_block_id(BlockType::PineWood));
    }
    set_block_if_needed(world, boom_tip_x, base_y + 4, base_z, to_block_id(BlockType::Cobblestone));
}

void build_cargo_stack(World& world, const BlockCoord& anchor, int seed, int salt) {
    const auto height = 1 + static_cast<int>(hash_coords(anchor.x, anchor.z, seed, salt) % 3U);
    for (int y = anchor.y; y < anchor.y + height; ++y) {
        set_block_if_needed(world, anchor.x, y, anchor.z, to_block_id(BlockType::Planks));
        set_block_if_needed(world, anchor.x + 1, y, anchor.z, to_block_id(BlockType::Planks));
    }
}

void build_bollard(World& world, const BlockCoord& anchor) {
    set_block_if_needed(world, anchor.x, anchor.y, anchor.z, to_block_id(BlockType::Wood));
    set_block_if_needed(world, anchor.x, anchor.y + 1, anchor.z, to_block_id(BlockType::Wood));
}

void build_lantern_post(World& world, const BlockCoord& anchor) {
    set_block_if_needed(world, anchor.x, anchor.y, anchor.z, to_block_id(BlockType::Stone));
    set_block_if_needed(world, anchor.x, anchor.y + 1, anchor.z, to_block_id(BlockType::Wood));
    set_block_if_needed(world, anchor.x, anchor.y + 2, anchor.z, to_block_id(BlockType::Wood));
    set_block_if_needed(world, anchor.x, anchor.y + 3, anchor.z, to_block_id(BlockType::Torch));
}

} // namespace

StartingPortGenerator::StartingPortGenerator(int seed)
    : seed_(seed) {
}

auto StartingPortGenerator::build_layout() const -> StartingPortLayout {
    const auto& ship_bounds = amelie_ship_blueprint().bounds;

    StartingPortLayout layout {};
    layout.seed = seed_;
    layout.quay_surface_y = kStartingPortSurfaceY;
    layout.ship_sweep_min_x = static_cast<int>(std::floor(ship_bounds.min.x));
    layout.ship_sweep_max_x = static_cast<int>(std::ceil(ship_bounds.max.x));

    const auto fixed_structure_max_x = layout.ship_sweep_min_x - 1;
    layout.stone_quay = {
        kStartingPortQuayMinX,
        kStartingPortQuayMaxX,
        kStartingPortQuayMinZ,
        kStartingPortQuayMaxZ,
        kStartingPortSurfaceY,
    };
    layout.gangway = {
        kStartingPortBasinMinX,
        fixed_structure_max_x,
        -9,
        -7,
        kStartingPortSurfaceY,
    };
    layout.wooden_pier = {
        kStartingPortBasinMinX,
        std::min(kStartingPortBasinMinX + 9, fixed_structure_max_x),
        kStartingPortBasinMinZ + 5,
        kStartingPortBasinMinZ + 10,
        kStartingPortSurfaceY,
    };
    layout.breakwater_west = {
        kStartingPortMinX + 4,
        kStartingPortMaxX,
        kStartingPortMinZ + 4,
        kStartingPortMinZ + 8,
        kStartingPortSurfaceY,
    };
    layout.breakwater_east = {
        kStartingPortMaxX - 4,
        kStartingPortMaxX,
        kStartingPortMinZ + 9,
        kStartingPortQuayMinZ,
        kStartingPortSurfaceY,
    };

    layout.lighthouse_base = {kStartingPortMinX + 9, kStartingPortSurfaceY, kStartingPortMinZ + 6};
    layout.crane_base = {kStartingPortQuayMaxX - 3, kStartingPortSurfaceY, 10};

    layout.buildings.push_back(make_building(
        StartingPortBuildingRole::HarborMasterOffice,
        kStartingPortMinX + 18,
        kStartingPortQuayMinX - 2,
        -26,
        -12,
        kStartingPortSurfaceY,
        seed_,
        101));
    layout.buildings.push_back(make_building(
        StartingPortBuildingRole::Warehouse,
        kStartingPortMinX + 3,
        kStartingPortMinX + 16,
        7,
        27,
        kStartingPortSurfaceY,
        seed_,
        102));

    layout.cargo_anchors = {
        {kStartingPortQuayMinX + 3, kStartingPortSurfaceY + 1, 2},
        {kStartingPortQuayMinX + 7, kStartingPortSurfaceY + 1, 7},
        {kStartingPortQuayMinX + 11, kStartingPortSurfaceY + 1, 13},
        {kStartingPortQuayMinX + 4, kStartingPortSurfaceY + 1, 16},
    };
    layout.bollards = {
        {kStartingPortQuayMaxX, kStartingPortSurfaceY + 1, -46},
        {kStartingPortQuayMaxX, kStartingPortSurfaceY + 1, -30},
        {kStartingPortQuayMaxX, kStartingPortSurfaceY + 1, -14},
        {kStartingPortQuayMaxX, kStartingPortSurfaceY + 1, 2},
        {kStartingPortQuayMaxX, kStartingPortSurfaceY + 1, 16},
    };
    layout.lantern_posts = {
        {kStartingPortQuayMinX + 2, kStartingPortSurfaceY + 1, -47},
        {kStartingPortQuayMinX + 2, kStartingPortSurfaceY + 1, -31},
        {kStartingPortQuayMinX + 2, kStartingPortSurfaceY + 1, -15},
        {kStartingPortQuayMinX + 2, kStartingPortSurfaceY + 1, 1},
        {kStartingPortQuayMinX + 2, kStartingPortSurfaceY + 1, 17},
    };

    layout.min_x = kStartingPortMinX;
    layout.max_x = std::max(kStartingPortMaxX, fixed_structure_max_x);
    layout.min_z = kStartingPortMinZ;
    layout.max_z = kStartingPortMaxZ;
    return layout;
}

void StartingPortGenerator::apply(World& world, const StartingPortLayout& layout) const {
    if (layout.buildings.empty() || !valid_area(layout.stone_quay) || !valid_area(layout.gangway)) {
        return;
    }

    build_supported_surface(world, layout.stone_quay, layout.seed, 201, false, 5, kPortHeadroom);
    build_supported_surface(world, layout.gangway, layout.seed, 202, true, 3, 3);
    build_supported_surface(world, layout.wooden_pier, layout.seed, 203, true, 3, 3);
    build_supported_surface(world, layout.breakwater_west, layout.seed, 204, false, 4, 2);
    build_supported_surface(world, layout.breakwater_east, layout.seed, 205, false, 4, 2);

    for (const auto& building : layout.buildings) {
        build_building(world, building, layout);
    }
    build_lighthouse(world, layout);
    build_crane(world, layout);

    for (std::size_t index = 0; index < layout.cargo_anchors.size(); ++index) {
        build_cargo_stack(world, layout.cargo_anchors[index], layout.seed, 801 + static_cast<int>(index));
    }
    for (const auto& bollard : layout.bollards) {
        build_bollard(world, bollard);
    }
    for (const auto& lantern : layout.lantern_posts) {
        build_lantern_post(world, lantern);
    }
}

} // namespace valcraft
