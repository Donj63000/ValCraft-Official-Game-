#include "gameplay/StartingVillage.h"

#include "world/WorldGenerator.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace valcraft {

namespace {

constexpr int kVillageSearchRadius = 80;
constexpr int kVillageSearchStep = 8;
constexpr int kVillageSampleRadius = 30;
constexpr int kVillageSampleStep = 4;
constexpr int kVillageMainRoadHalfWidth = 2;
constexpr int kVillageLoopRoadHalfWidth = 2;
constexpr int kVillageMainRoadReachX = 36;
constexpr int kVillageMainRoadReachZ = 32;
constexpr int kVillageLoopHalfExtentX = 24;
constexpr int kVillageLoopHalfExtentZ = 18;
constexpr int kVillagePlazaHalfExtent = 8;
constexpr int kVillageBoundsPadding = 8;
constexpr int kVillageCoreHalfExtentX = kVillageLoopHalfExtentX + 14;
constexpr int kVillageCoreHalfExtentZ = kVillageLoopHalfExtentZ + 14;
constexpr int kVillageRoadBlendFeather = 4;
constexpr int kVillageBuildingBlendFeather = 3;
constexpr int kVillagePlazaBlendFeather = 5;
constexpr float kVillageSpawnOffsetY = 1.001F;
constexpr float kResidentVillageRoamRadius = 21.0F;

struct GeneratedGroundSample {
    int surface_y = kWorldMinY;
    BlockId surface_block = to_block_id(BlockType::Grass);
    BiomeType biome = BiomeType::Meadow;
    bool valid_land = false;
    bool had_water = false;
};

struct VillageSiteCandidate {
    int center_x = 0;
    int center_z = 0;
    int base_y = 0;
    float score = std::numeric_limits<float>::lowest();
};

struct BuildingDimensions {
    int width = 9;
    int depth = 7;
    int wall_height = 3;
};

struct VillagePlanEntry {
    VillageBuildingRole role = VillageBuildingRole::House;
    VillageFacing facing = VillageFacing::North;
    int front_x = 0;
    int front_z = 0;
    int salt = 0;
};

auto floor_div_local(int value, int divisor) noexcept -> int {
    auto quotient = value / divisor;
    const auto remainder = value % divisor;
    if (remainder < 0) {
        --quotient;
    }
    return quotient;
}

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

auto is_overgrowth_block(BlockId block_id) noexcept -> bool {
    switch (static_cast<BlockType>(block_id)) {
    case BlockType::Wood:
    case BlockType::Leaves:
    case BlockType::PineWood:
    case BlockType::PineLeaves:
    case BlockType::TallGrass:
    case BlockType::RedFlower:
    case BlockType::YellowFlower:
    case BlockType::DeadShrub:
    case BlockType::Cactus:
        return true;
    default:
        return false;
    }
}

auto sample_generated_ground(const WorldGenerator& generator, int world_x, int world_z) -> GeneratedGroundSample {
    GeneratedGroundSample sample {};
    const auto surface = generator.sample_surface(world_x, world_z);
    sample.biome = surface.biome;
    sample.surface_y = surface.surface_height;
    sample.surface_block = surface.surface_block;
    sample.had_water = surface.water_level > surface.surface_height;
    sample.valid_land = !sample.had_water && !is_overgrowth_block(sample.surface_block);
    return sample;
}

auto choose_site(const WorldGenerator& generator) -> VillageSiteCandidate {
    VillageSiteCandidate best {};

    for (int center_z = -kVillageSearchRadius; center_z <= kVillageSearchRadius; center_z += kVillageSearchStep) {
        for (int center_x = -kVillageSearchRadius; center_x <= kVillageSearchRadius; center_x += kVillageSearchStep) {
            std::vector<int> heights {};
            int meadow_samples = 0;
            int forest_samples = 0;
            int taiga_samples = 0;
            int penalties = 0;
            int core_penalties = 0;

            for (int sample_z = center_z - kVillageSampleRadius; sample_z <= center_z + kVillageSampleRadius; sample_z += kVillageSampleStep) {
                for (int sample_x = center_x - kVillageSampleRadius; sample_x <= center_x + kVillageSampleRadius; sample_x += kVillageSampleStep) {
                    const auto ground = sample_generated_ground(generator, sample_x, sample_z);
                    const auto core_dx = std::abs(sample_x - center_x);
                    const auto core_dz = std::abs(sample_z - center_z);
                    const auto in_core = core_dx <= kVillageCoreHalfExtentX && core_dz <= kVillageCoreHalfExtentZ;
                    if (!ground.valid_land) {
                        penalties += in_core ? 18 : 7;
                        if (in_core) {
                            core_penalties += 18;
                        }
                        continue;
                    }
                    if (ground.surface_y < kSeaLevel + 2) {
                        penalties += in_core ? 6 : 3;
                        if (in_core) {
                            core_penalties += 6;
                        }
                    }
                    if (ground.surface_block == to_block_id(BlockType::Sand) || ground.surface_block == to_block_id(BlockType::Snow)) {
                        penalties += in_core ? 8 : 3;
                        if (in_core) {
                            core_penalties += 8;
                        }
                    }

                    heights.push_back(ground.surface_y);
                    switch (ground.biome) {
                    case BiomeType::Meadow:
                        ++meadow_samples;
                        break;
                    case BiomeType::Forest:
                        ++forest_samples;
                        break;
                    case BiomeType::Taiga:
                        ++taiga_samples;
                        penalties += 2;
                        if (in_core) {
                            core_penalties += 2;
                        }
                        break;
                    case BiomeType::Desert:
                    case BiomeType::RockyPeaks:
                        penalties += in_core ? 12 : 6;
                        if (in_core) {
                            core_penalties += 12;
                        }
                        break;
                    }
                }
            }

            if (heights.size() < 120U) {
                continue;
            }
            if (core_penalties > 36) {
                continue;
            }

            std::sort(heights.begin(), heights.end());
            const auto median_height = heights[heights.size() / 2U];
            const auto relief = heights.back() - heights.front();
            if (relief > 5) {
                continue;
            }

            int outlier_count = 0;
            for (const auto& height : heights) {
                if (std::abs(height - median_height) > 2) {
                    ++outlier_count;
                }
            }

            const auto distance = std::sqrt(static_cast<float>(center_x * center_x + center_z * center_z));
            const auto score =
                static_cast<float>(meadow_samples) * 2.8F +
                static_cast<float>(forest_samples) * 0.9F +
                static_cast<float>(taiga_samples) * 0.1F -
                static_cast<float>(relief) * 9.0F -
                static_cast<float>(outlier_count) * 1.8F -
                static_cast<float>(penalties) * 4.5F -
                static_cast<float>(core_penalties) * 3.5F -
                distance * 0.18F;

            if (score > best.score) {
                best = {center_x, center_z, median_height, score};
            }
        }
    }

    if (best.score == std::numeric_limits<float>::lowest()) {
        const auto fallback = sample_generated_ground(generator, 0, 0);
        best = {0, 0, std::max(fallback.surface_y, kSeaLevel + 2), 0.0F};
    }

    return best;
}

auto make_world_position(int world_x, int ground_y, int world_z) -> glm::vec3 {
    return {
        static_cast<float>(world_x) + 0.5F,
        static_cast<float>(ground_y) + kVillageSpawnOffsetY,
        static_cast<float>(world_z) + 0.5F,
    };
}

auto facing_forward(VillageFacing facing) noexcept -> std::pair<int, int> {
    switch (facing) {
    case VillageFacing::South:
        return {0, 1};
    case VillageFacing::North:
        return {0, -1};
    case VillageFacing::East:
        return {1, 0};
    case VillageFacing::West:
    default:
        return {-1, 0};
    }
}

auto facing_right(VillageFacing facing) noexcept -> std::pair<int, int> {
    switch (facing) {
    case VillageFacing::South:
        return {-1, 0};
    case VillageFacing::North:
        return {1, 0};
    case VillageFacing::East:
        return {0, 1};
    case VillageFacing::West:
    default:
        return {0, -1};
    }
}

auto choose_building_dimensions(VillageBuildingRole role, std::uint32_t seed) noexcept -> BuildingDimensions {
    BuildingDimensions dimensions {};
    switch (role) {
    case VillageBuildingRole::House:
        dimensions.width = (seed % 3U) == 0U ? 11 : 9;
        dimensions.depth = (seed % 5U) == 0U ? 9 : 7;
        dimensions.wall_height = (seed % 4U) == 0U ? 4 : 3;
        break;
    case VillageBuildingRole::Workshop:
        dimensions.width = (seed % 2U) == 0U ? 11 : 13;
        dimensions.depth = (seed % 3U) == 0U ? 9 : 8;
        dimensions.wall_height = 4;
        break;
    case VillageBuildingRole::Storehouse:
        dimensions.width = (seed % 2U) == 0U ? 11 : 13;
        dimensions.depth = 8;
        dimensions.wall_height = 4;
        break;
    case VillageBuildingRole::Lodge:
        dimensions.width = (seed % 2U) == 0U ? 13 : 15;
        dimensions.depth = (seed % 3U) == 0U ? 11 : 9;
        dimensions.wall_height = 4;
        break;
    }

    if ((dimensions.width & 1) == 0) {
        ++dimensions.width;
    }
    if ((dimensions.depth & 1) == 0) {
        ++dimensions.depth;
    }
    return dimensions;
}

auto make_building_from_front(int front_center_x,
                              int front_center_z,
                              int base_y,
                              VillageBuildingRole role,
                              VillageFacing facing,
                              int seed,
                              int salt) -> StartingVillageBuilding {
    const auto variant_seed = hash_coords(front_center_x, front_center_z, seed, salt);
    const auto dimensions = choose_building_dimensions(role, variant_seed);

    StartingVillageBuilding building {};
    building.role = role;
    building.facing = facing;
    building.base_y = base_y;
    building.variant_seed = variant_seed;

    switch (facing) {
    case VillageFacing::South:
        building.min_x = front_center_x - dimensions.width / 2;
        building.max_x = building.min_x + dimensions.width - 1;
        building.max_z = front_center_z;
        building.min_z = building.max_z - dimensions.depth + 1;
        building.door_x = front_center_x;
        building.door_z = building.max_z + 1;
        building.yard_x = building.door_x;
        building.yard_z = building.door_z + 2;
        building.interior_x = building.door_x;
        building.interior_z = building.max_z - 2;
        break;
    case VillageFacing::North:
        building.min_x = front_center_x - dimensions.width / 2;
        building.max_x = building.min_x + dimensions.width - 1;
        building.min_z = front_center_z;
        building.max_z = building.min_z + dimensions.depth - 1;
        building.door_x = front_center_x;
        building.door_z = building.min_z - 1;
        building.yard_x = building.door_x;
        building.yard_z = building.door_z - 2;
        building.interior_x = building.door_x;
        building.interior_z = building.min_z + 2;
        break;
    case VillageFacing::East:
        building.min_z = front_center_z - dimensions.width / 2;
        building.max_z = building.min_z + dimensions.width - 1;
        building.max_x = front_center_x;
        building.min_x = building.max_x - dimensions.depth + 1;
        building.door_x = building.max_x + 1;
        building.door_z = front_center_z;
        building.yard_x = building.door_x + 2;
        building.yard_z = building.door_z;
        building.interior_x = building.max_x - 2;
        building.interior_z = building.door_z;
        break;
    case VillageFacing::West:
        building.min_z = front_center_z - dimensions.width / 2;
        building.max_z = building.min_z + dimensions.width - 1;
        building.min_x = front_center_x;
        building.max_x = building.min_x + dimensions.depth - 1;
        building.door_x = building.min_x - 1;
        building.door_z = front_center_z;
        building.yard_x = building.door_x - 2;
        building.yard_z = building.door_z;
        building.interior_x = building.min_x + 2;
        building.interior_z = building.door_z;
        break;
    }

    return building;
}

void expand_layout_bounds(StartingVillageLayout& layout, const StartingVillageBuilding& building) {
    if (layout.buildings.empty()) {
        layout.min_x = building.min_x;
        layout.max_x = building.max_x;
        layout.min_z = building.min_z;
        layout.max_z = building.max_z;
        return;
    }

    layout.min_x = std::min(layout.min_x, building.min_x);
    layout.max_x = std::max(layout.max_x, building.max_x);
    layout.min_z = std::min(layout.min_z, building.min_z);
    layout.max_z = std::max(layout.max_z, building.max_z);
}

auto make_resident_anchor(const StartingVillageBuilding& building, const StartingVillageLayout& layout, int seed, int salt)
    -> CreatureSpawnAnchor {
    const auto variant_seed = hash_coords(building.yard_x, building.yard_z, seed, salt);
    const auto forward = facing_forward(building.facing);
    const auto right = facing_right(building.facing);
    const auto forward_x = forward.first;
    const auto forward_z = forward.second;
    const auto right_x = right.first;
    const auto right_z = right.second;

    CreatureSpawnAnchor anchor {};
    anchor.chunk = {floor_div_local(building.yard_x, kChunkSizeX), floor_div_local(building.yard_z, kChunkSizeZ)};
    anchor.ground_block = {building.yard_x, building.base_y, building.yard_z};
    anchor.spawn_position = make_world_position(building.yard_x, building.base_y, building.yard_z);
    anchor.species = CreatureSpecies::Villager;

    const auto social_x = layout.center_x + ((variant_seed & 1U) == 0U ? -6 : 6);
    const auto social_z = layout.center_z + (((variant_seed >> 1U) & 1U) == 0U ? -5 : 5);
    auto work_x = building.yard_x + right_x * 3;
    auto work_z = building.yard_z + right_z * 3;
    if (building.role == VillageBuildingRole::Workshop || building.role == VillageBuildingRole::Storehouse) {
        work_x = building.door_x - forward_x + right_x * ((variant_seed % 2U) == 0U ? 3 : -3);
        work_z = building.door_z - forward_z + right_z * ((variant_seed % 2U) == 0U ? 3 : -3);
    } else if (building.role == VillageBuildingRole::Lodge) {
        work_x = layout.center_x + right_x * 4;
        work_z = layout.center_z + right_z * 4;
    }

    anchor.patrol_points[0] = make_world_position(building.yard_x, building.base_y, building.yard_z);
    anchor.patrol_points[1] = make_world_position(building.interior_x, building.base_y + 1, building.interior_z);
    anchor.patrol_points[2] = make_world_position(social_x, layout.base_y, social_z);
    anchor.patrol_points[3] = make_world_position(work_x, building.base_y, work_z);
    anchor.patrol_point_count = static_cast<std::uint8_t>(anchor.patrol_points.size());

    float max_distance = 0.0F;
    for (std::size_t index = 0; index < anchor.patrol_point_count; ++index) {
        const auto dx = anchor.patrol_points[index].x - anchor.spawn_position.x;
        const auto dz = anchor.patrol_points[index].z - anchor.spawn_position.z;
        max_distance = std::max(max_distance, std::sqrt(dx * dx + dz * dz));
    }
    anchor.roam_radius = std::max(kResidentVillageRoamRadius, max_distance + 4.0F);
    return anchor;
}

void set_block_if_needed(World& world, int x, int y, int z, BlockId block_id) {
    if (!is_world_y_valid(y)) {
        return;
    }
    if (block_id == to_block_id(BlockType::Water)) {
        if (world.has_water(x, y, z)) {
            return;
        }
        // Je force l'eau des structures apres le nivellement, sinon le puits
        // ne peut pas remplacer les blocs solides poses pour la place.
        if (world.get_block(x, y, z) != to_block_id(BlockType::Air)) {
            world.set_block(x, y, z, to_block_id(BlockType::Air));
        }
        world.set_block(x, y, z, block_id);
        return;
    } else if (block_id == to_block_id(BlockType::Air)) {
        if (world.get_block(x, y, z) == to_block_id(BlockType::Air) && !world.has_water(x, y, z)) {
            return;
        }
    } else if (world.get_block(x, y, z) == block_id && !world.has_water(x, y, z)) {
        return;
    }
    world.set_block(x, y, z, block_id);
}

auto find_ground_height(World& world, int x, int z) -> int {
    for (int y = world.surface_height(x, z); y >= kWorldMinY; --y) {
        const auto block = world.get_block(x, y, z);
        if (block == to_block_id(BlockType::Air) || world.has_water(x, y, z) || is_overgrowth_block(block)) {
            continue;
        }
        return y;
    }
    return kWorldMinY;
}

void clear_column_above(World& world, int x, int z, int from_y, int to_y) {
    for (int y = from_y; y <= to_y; ++y) {
        if (is_world_y_valid(y) &&
            (world.get_block(x, y, z) != to_block_id(BlockType::Air) || world.has_water(x, y, z))) {
            set_block_if_needed(world, x, y, z, to_block_id(BlockType::Air));
        }
    }
}

auto smoothstep_unit(float value) noexcept -> float {
    const auto clamped = std::clamp(value, 0.0F, 1.0F);
    return clamped * clamped * (3.0F - 2.0F * clamped);
}

auto blended_height(int source_y, int target_y, float weight) noexcept -> int {
    const auto source = static_cast<double>(source_y);
    const auto delta = static_cast<double>(target_y - source_y);
    return static_cast<int>(std::lround(source + delta * static_cast<double>(weight)));
}

void grade_column(World& world, int x, int z, int target_y, BlockId top_block, BlockId filler_block) {
    const auto ground_y = find_ground_height(world, x, z);
    if (ground_y < target_y) {
        for (int y = ground_y + 1; y < target_y; ++y) {
            set_block_if_needed(world, x, y, z, filler_block);
        }
    }

    for (int y = std::max(target_y - 3, kWorldMinY); y < target_y; ++y) {
        const auto block = world.get_block(x, y, z);
        if (!is_block_collidable(block) || world.has_water(x, y, z) || is_block_replaceable(block)) {
            set_block_if_needed(world, x, y, z, filler_block);
        }
    }

    set_block_if_needed(world, x, target_y, z, top_block);
    clear_column_above(world, x, z, target_y + 1, std::min(target_y + 12, kWorldMaxY));
}

auto choose_road_block(std::uint32_t seed) noexcept -> BlockId {
    switch (seed % 12U) {
    case 0U:
    case 1U:
        return to_block_id(BlockType::Sand);
    case 2U:
    case 3U:
        return to_block_id(BlockType::MossyStone);
    case 4U:
    case 5U:
        return to_block_id(BlockType::Stone);
    default:
        return to_block_id(BlockType::Cobblestone);
    }
}

template <typename TopSelector>
void build_surface_patch(World& world, int min_x, int max_x, int min_z, int max_z, int base_y, int seed, int salt, BlockId filler_block, const TopSelector& top_selector) {
    for (int z = min_z; z <= max_z; ++z) {
        for (int x = min_x; x <= max_x; ++x) {
            grade_column(world, x, z, base_y, top_selector(hash_coords(x, z, seed, salt)), filler_block);
        }
    }
}

template <typename TopSelector>
void build_blended_ground_patch(World& world,
                                int min_x,
                                int max_x,
                                int min_z,
                                int max_z,
                                int target_y,
                                int feather,
                                int seed,
                                int salt,
                                BlockId filler_block,
                                const TopSelector& top_selector) {
    const auto blend_feather = std::max(feather, 0);
    for (int z = min_z - blend_feather; z <= max_z + blend_feather; ++z) {
        for (int x = min_x - blend_feather; x <= max_x + blend_feather; ++x) {
            const auto outside_x = x < min_x ? min_x - x : (x > max_x ? x - max_x : 0);
            const auto outside_z = z < min_z ? min_z - z : (z > max_z ? z - max_z : 0);
            const auto outside_distance = std::max(outside_x, outside_z);
            const auto current_ground_y = find_ground_height(world, x, z);
            const auto weight =
                outside_distance == 0 ?
                    1.0F :
                    smoothstep_unit(1.0F - static_cast<float>(outside_distance) / static_cast<float>(blend_feather + 1));
            const auto patch_y = blended_height(current_ground_y, target_y, weight);
            grade_column(world, x, z, patch_y, top_selector(hash_coords(x, z, seed, salt)), filler_block);
        }
    }
}

void build_path_patch(World& world, int min_x, int max_x, int min_z, int max_z, int base_y, int seed, int salt) {
    build_blended_ground_patch(
        world,
        min_x,
        max_x,
        min_z,
        max_z,
        base_y,
        kVillageRoadBlendFeather,
        seed,
        salt + 4000,
        to_block_id(BlockType::Dirt),
        [](std::uint32_t column_seed) {
            return (column_seed % 5U) == 0U ? to_block_id(BlockType::Dirt) : to_block_id(BlockType::Grass);
        });
    build_surface_patch(world, min_x, max_x, min_z, max_z, base_y, seed, salt, to_block_id(BlockType::Dirt), [](std::uint32_t column_seed) {
        return choose_road_block(column_seed);
    });
}

auto choose_garden_block(std::uint32_t seed) noexcept -> BlockId {
    switch (seed % 9U) {
    case 0U:
        return to_block_id(BlockType::YellowFlower);
    case 1U:
        return to_block_id(BlockType::DeadShrub);
    case 2U:
    case 3U:
    case 4U:
        return to_block_id(BlockType::TallGrass);
    case 5U:
        return to_block_id(BlockType::RedFlower);
    default:
        return to_block_id(BlockType::Air);
    }
}

void decorate_green_patch(World& world, int center_x, int center_z, int half_extent, int base_y, int seed, int salt) {
    for (int dz = -half_extent; dz <= half_extent; ++dz) {
        for (int dx = -half_extent; dx <= half_extent; ++dx) {
            const auto world_x = center_x + dx;
            const auto world_z = center_z + dz;
            grade_column(world, world_x, world_z, base_y, to_block_id(BlockType::Grass), to_block_id(BlockType::Dirt));
            const auto decoration = choose_garden_block(hash_coords(world_x, world_z, seed, salt));
            if (decoration != to_block_id(BlockType::Air) && world.get_block(world_x, base_y + 1, world_z) == to_block_id(BlockType::Air)) {
                set_block_if_needed(world, world_x, base_y + 1, world_z, decoration);
            }
        }
    }
}

void build_small_tree(World& world, int x, int z, int base_y, std::uint32_t seed, bool pine) {
    const auto trunk_block = pine ? to_block_id(BlockType::PineWood) : to_block_id(BlockType::Wood);
    const auto leaf_block = pine ? to_block_id(BlockType::PineLeaves) : to_block_id(BlockType::Leaves);
    const auto trunk_height = pine ? 5 + static_cast<int>(seed % 2U) : 3 + static_cast<int>(seed % 2U);

    grade_column(world, x, z, base_y, to_block_id(BlockType::Grass), to_block_id(BlockType::Dirt));
    for (int y = base_y + 1; y <= base_y + trunk_height; ++y) {
        set_block_if_needed(world, x, y, z, trunk_block);
    }

    if (pine) {
        for (int dy = -3; dy <= 2; ++dy) {
            const auto radius = dy <= -1 ? 1 : 0;
            const auto leaf_y = base_y + trunk_height + dy;
            for (int dz = -radius; dz <= radius; ++dz) {
                for (int dx = -radius; dx <= radius; ++dx) {
                    if (std::abs(dx) + std::abs(dz) > radius + 1) {
                        continue;
                    }
                    const auto leaf_x = x + dx;
                    const auto leaf_z = z + dz;
                    if (world.get_block(leaf_x, leaf_y, leaf_z) == to_block_id(BlockType::Air)) {
                        set_block_if_needed(world, leaf_x, leaf_y, leaf_z, leaf_block);
                    }
                }
            }
        }
        return;
    }

    const auto canopy_base_y = base_y + trunk_height - 1;
    for (int dz = -3; dz <= 3; ++dz) {
        for (int dx = -3; dx <= 3; ++dx) {
            for (int dy = 0; dy <= 2; ++dy) {
                const auto distance = std::abs(dx) + std::abs(dz);
                if (distance > 4 || (dy == 2 && distance > 2)) {
                    continue;
                }
                const auto leaf_x = x + dx;
                const auto leaf_y = canopy_base_y + dy;
                const auto leaf_z = z + dz;
                if (world.get_block(leaf_x, leaf_y, leaf_z) == to_block_id(BlockType::Air)) {
                    set_block_if_needed(world, leaf_x, leaf_y, leaf_z, leaf_block);
                }
            }
        }
    }
}

void build_small_tree_on_natural_ground(World& world, int x, int z, std::uint32_t seed, bool pine) {
    build_small_tree(world, x, z, find_ground_height(world, x, z), seed, pine);
}

void decorate_natural_green_patch(World& world, int center_x, int center_z, int half_extent, int seed, int salt) {
    for (int dz = -half_extent; dz <= half_extent; ++dz) {
        for (int dx = -half_extent; dx <= half_extent; ++dx) {
            const auto world_x = center_x + dx;
            const auto world_z = center_z + dz;
            const auto natural_y = find_ground_height(world, world_x, world_z);
            grade_column(world, world_x, world_z, natural_y, to_block_id(BlockType::Grass), to_block_id(BlockType::Dirt));
            const auto decoration = choose_garden_block(hash_coords(world_x, world_z, seed, salt));
            if (decoration != to_block_id(BlockType::Air) && world.get_block(world_x, natural_y + 1, world_z) == to_block_id(BlockType::Air)) {
                set_block_if_needed(world, world_x, natural_y + 1, world_z, decoration);
            }
        }
    }
}

void try_place_wall_torch(World& world, int x, int y, int z, const BlockCoord& support_offset) {
    if (!is_world_y_valid(y) || world.get_block(x, y, z) != to_block_id(BlockType::Air)) {
        return;
    }
    if (!is_block_collidable(world.get_block(x + support_offset.x, y + support_offset.y, z + support_offset.z))) {
        return;
    }
    const auto torch_block = torch_block_from_support_offset(support_offset);
    if (torch_block != to_block_id(BlockType::Air)) {
        set_block_if_needed(world, x, y, z, torch_block);
    }
}

auto doorway_world_cell(const StartingVillageBuilding& building) noexcept -> BlockCoord {
    switch (building.facing) {
    case VillageFacing::South:
        return {building.door_x, building.base_y + 1, building.max_z};
    case VillageFacing::North:
        return {building.door_x, building.base_y + 1, building.min_z};
    case VillageFacing::East:
        return {building.max_x, building.base_y + 1, building.door_z};
    case VillageFacing::West:
    default:
        return {building.min_x, building.base_y + 1, building.door_z};
    }
}

auto foundation_block_for_role(VillageBuildingRole role, std::uint32_t seed) noexcept -> BlockId {
    switch (role) {
    case VillageBuildingRole::Workshop:
        return (seed % 2U) == 0U ? to_block_id(BlockType::Stone) : to_block_id(BlockType::Cobblestone);
    case VillageBuildingRole::Storehouse:
        return to_block_id(BlockType::Cobblestone);
    case VillageBuildingRole::Lodge:
        return (seed % 3U) == 0U ? to_block_id(BlockType::MossyStone) : to_block_id(BlockType::Stone);
    case VillageBuildingRole::House:
    default:
        return (seed % 3U) == 0U ? to_block_id(BlockType::Cobblestone) : to_block_id(BlockType::Stone);
    }
}

auto floor_block_for_role(VillageBuildingRole role, std::uint32_t seed) noexcept -> BlockId {
    switch (role) {
    case VillageBuildingRole::Storehouse:
        return (seed % 2U) == 0U ? to_block_id(BlockType::Sand) : to_block_id(BlockType::Cobblestone);
    case VillageBuildingRole::Workshop:
        return (seed % 3U) == 0U ? to_block_id(BlockType::Cobblestone) : to_block_id(BlockType::Sand);
    case VillageBuildingRole::Lodge:
        return to_block_id(BlockType::Stone);
    case VillageBuildingRole::House:
    default:
        return to_block_id(BlockType::Sand);
    }
}

auto trim_block_for_role(VillageBuildingRole role, std::uint32_t seed) noexcept -> BlockId {
    switch (role) {
    case VillageBuildingRole::Lodge:
        return (seed % 2U) == 0U ? to_block_id(BlockType::Stone) : to_block_id(BlockType::MossyStone);
    case VillageBuildingRole::Workshop:
        return (seed % 3U) == 0U ? to_block_id(BlockType::Cobblestone) : to_block_id(BlockType::Stone);
    case VillageBuildingRole::Storehouse:
        return to_block_id(BlockType::Cobblestone);
    case VillageBuildingRole::House:
    default:
        return to_block_id(BlockType::Stone);
    }
}

auto wall_block_for_role(VillageBuildingRole role, std::uint32_t seed) noexcept -> BlockId {
    switch (role) {
    case VillageBuildingRole::Workshop:
        return (seed % 2U) == 0U ? to_block_id(BlockType::Sand) : to_block_id(BlockType::Stone);
    case VillageBuildingRole::Storehouse:
        return (seed % 3U) == 0U ? to_block_id(BlockType::Sand) : to_block_id(BlockType::Cobblestone);
    case VillageBuildingRole::Lodge:
        return (seed % 2U) == 0U ? to_block_id(BlockType::Sand) : to_block_id(BlockType::Stone);
    case VillageBuildingRole::House:
    default:
        return to_block_id(BlockType::Sand);
    }
}

auto roof_block_for_role(VillageBuildingRole role, std::uint32_t seed) noexcept -> BlockId {
    switch (role) {
    case VillageBuildingRole::Workshop:
        return (seed % 2U) == 0U ? to_block_id(BlockType::Planks) : to_block_id(BlockType::Wood);
    case VillageBuildingRole::Storehouse:
        return to_block_id(BlockType::Planks);
    case VillageBuildingRole::Lodge:
        return to_block_id(BlockType::Planks);
    case VillageBuildingRole::House:
    default:
        return (seed % 5U) == 0U ? to_block_id(BlockType::Wood) : to_block_id(BlockType::Planks);
    }
}

auto should_place_window(const StartingVillageBuilding& building, const BlockCoord& doorway, int x, int y, int z) noexcept -> bool {
    const auto dimensions = choose_building_dimensions(building.role, building.variant_seed);
    const auto is_window_band = y == building.base_y + 2 || (dimensions.wall_height >= 4 && building.role == VillageBuildingRole::Lodge && y == building.base_y + 3);
    if (!is_window_band) {
        return false;
    }

    if ((x == doorway.x && z == doorway.z) ||
        (std::abs(x - doorway.x) <= 1 && std::abs(z - doorway.z) <= 1)) {
        return false;
    }

    if (x == building.min_x || x == building.max_x) {
        if (z <= building.min_z + 1 || z >= building.max_z - 1) {
            return false;
        }
        const auto spacing = building.role == VillageBuildingRole::Storehouse ? 4 : 3;
        return ((z - building.min_z) % spacing) == 1;
    }
    if (z == building.min_z || z == building.max_z) {
        if (x <= building.min_x + 1 || x >= building.max_x - 1) {
            return false;
        }
        const auto spacing = building.role == VillageBuildingRole::Storehouse ? 4 : 3;
        return ((x - building.min_x) % spacing) == 1;
    }
    return false;
}

auto roof_base_y(const StartingVillageBuilding& building) noexcept -> int {
    const auto dimensions = choose_building_dimensions(building.role, building.variant_seed);
    return building.base_y + dimensions.wall_height + 1;
}

auto roof_surface_y(const StartingVillageBuilding& building, int x, int z) noexcept -> int {
    const auto eave_min_x = building.min_x - 1;
    const auto eave_max_x = building.max_x + 1;
    const auto eave_min_z = building.min_z - 1;
    const auto eave_max_z = building.max_z + 1;
    if (building.facing == VillageFacing::North || building.facing == VillageFacing::South) {
        return roof_base_y(building) + std::min(z - eave_min_z, eave_max_z - z);
    }
    return roof_base_y(building) + std::min(x - eave_min_x, eave_max_x - x);
}

auto is_gable_wall_cell(const StartingVillageBuilding& building, int x, int z) noexcept -> bool {
    if (building.facing == VillageFacing::North || building.facing == VillageFacing::South) {
        return x == building.min_x || x == building.max_x;
    }
    return z == building.min_z || z == building.max_z;
}

void build_upper_walls_and_gables(World& world,
                                  const StartingVillageBuilding& building,
                                  BlockId wall_block,
                                  BlockId trim_block) {
    const auto dimensions = choose_building_dimensions(building.role, building.variant_seed);
    const auto top_plate_y = building.base_y + dimensions.wall_height + 1;

    for (int z = building.min_z; z <= building.max_z; ++z) {
        for (int x = building.min_x; x <= building.max_x; ++x) {
            const auto edge = x == building.min_x || x == building.max_x || z == building.min_z || z == building.max_z;
            if (!edge) {
                continue;
            }
            set_block_if_needed(world, x, top_plate_y, z, trim_block);

            if (!is_gable_wall_cell(building, x, z)) {
                continue;
            }

            const auto wall_roof_y = roof_surface_y(building, x, z);
            for (int y = top_plate_y + 1; y < wall_roof_y; ++y) {
                const auto gable_block = (y + 1 == wall_roof_y) ? trim_block : wall_block;
                set_block_if_needed(world, x, y, z, gable_block);
            }
        }
    }
}

void build_lamp_post(World& world, int x, int z, int base_y, BlockId post_block) {
    set_block_if_needed(world, x, base_y, z, choose_road_block(hash_coords(x, z, base_y, 401)));
    clear_column_above(world, x, z, base_y + 1, std::min(base_y + 6, kWorldMaxY));
    for (int y = base_y + 1; y <= base_y + 3; ++y) {
        set_block_if_needed(world, x, y, z, post_block);
    }

    try_place_wall_torch(world, x + 1, base_y + 3, z, {-1, 0, 0});
    try_place_wall_torch(world, x - 1, base_y + 3, z, {1, 0, 0});
    try_place_wall_torch(world, x, base_y + 3, z + 1, {0, 0, -1});
    try_place_wall_torch(world, x, base_y + 3, z - 1, {0, 0, 1});
    set_block_if_needed(world, x, base_y + 4, z, to_block_id(BlockType::Torch));
}

void build_crate_stack(World& world, int x, int z, int base_y, BlockId base_block, BlockId top_block, int height) {
    set_block_if_needed(world, x, base_y, z, choose_road_block(hash_coords(x, z, base_y, 511)));
    clear_column_above(world, x, z, base_y + 1, std::min(base_y + 5, kWorldMaxY));
    for (int y = 1; y <= height; ++y) {
        set_block_if_needed(world, x, base_y + y, z, y == height ? top_block : base_block);
    }
}

void build_village_well(World& world, int center_x, int center_z, int base_y, int seed) {
    build_surface_patch(
        world,
        center_x - 3,
        center_x + 3,
        center_z - 3,
        center_z + 3,
        base_y,
        seed,
        701,
        to_block_id(BlockType::Dirt),
        [](std::uint32_t column_seed) {
            return (column_seed % 4U) == 0U ? to_block_id(BlockType::MossyStone) : to_block_id(BlockType::Stone);
        });

    for (int z = center_z - 1; z <= center_z + 1; ++z) {
        for (int x = center_x - 1; x <= center_x + 1; ++x) {
            set_block_if_needed(world, x, base_y, z, to_block_id(BlockType::Water));
            clear_column_above(world, x, z, base_y + 1, std::min(base_y + 3, kWorldMaxY));
        }
    }

    for (int z = center_z - 2; z <= center_z + 2; ++z) {
        for (int x = center_x - 2; x <= center_x + 2; ++x) {
            const auto outer_ring = x == center_x - 2 || x == center_x + 2 || z == center_z - 2 || z == center_z + 2;
            if (!outer_ring) {
                continue;
            }
            set_block_if_needed(world, x, base_y + 1, z, to_block_id(BlockType::Stone));
        }
    }

    constexpr std::array<std::pair<int, int>, 4> kWellPostOffsets {{
        {-2, -2},
        {2, -2},
        {-2, 2},
        {2, 2},
    }};
    for (const auto& post_offset : kWellPostOffsets) {
        const auto post_x = center_x + post_offset.first;
        const auto post_z = center_z + post_offset.second;
        for (int y = base_y + 2; y <= base_y + 4; ++y) {
            set_block_if_needed(world, post_x, y, post_z, to_block_id(BlockType::Stone));
        }
    }

    for (int z = center_z - 3; z <= center_z + 3; ++z) {
        for (int x = center_x - 3; x <= center_x + 3; ++x) {
            const auto edge = x == center_x - 3 || x == center_x + 3 || z == center_z - 3 || z == center_z + 3;
            if (edge) {
                set_block_if_needed(world, x, base_y + 5, z, to_block_id(BlockType::Cobblestone));
            }
        }
    }

    set_block_if_needed(world, center_x, base_y + 2, center_z, to_block_id(BlockType::Stone));
    set_block_if_needed(world, center_x, base_y + 3, center_z, to_block_id(BlockType::Stone));
    set_block_if_needed(world, center_x, base_y + 6, center_z, to_block_id(BlockType::Torch));
}

void decorate_frontage(World& world, const StartingVillageBuilding& building, int seed, int salt) {
    const auto forward = facing_forward(building.facing);
    const auto right = facing_right(building.facing);
    const auto forward_x = forward.first;
    const auto forward_z = forward.second;
    const auto right_x = right.first;
    const auto right_z = right.second;

    build_path_patch(
        world,
        std::min(building.door_x, building.yard_x) - 1,
        std::max(building.door_x, building.yard_x) + 1,
        std::min(building.door_z, building.yard_z) - 1,
        std::max(building.door_z, building.yard_z) + 1,
        building.base_y,
        seed,
        salt);

    switch (building.role) {
    case VillageBuildingRole::House:
    case VillageBuildingRole::Lodge:
        decorate_green_patch(world, building.yard_x + right_x * 3, building.yard_z + right_z * 3, 1, building.base_y, seed, salt + 1);
        decorate_green_patch(world, building.yard_x - right_x * 3, building.yard_z - right_z * 3, 1, building.base_y, seed, salt + 2);
        build_lamp_post(world, building.yard_x - forward_x * 2, building.yard_z - forward_z * 2, building.base_y, trim_block_for_role(building.role, building.variant_seed));
        break;
    case VillageBuildingRole::Workshop:
        build_crate_stack(
            world,
            building.yard_x + right_x * 2,
            building.yard_z + right_z * 2,
            building.base_y,
            to_block_id(BlockType::Cobblestone),
            to_block_id(BlockType::Planks),
            2);
        build_crate_stack(
            world,
            building.yard_x - right_x * 2,
            building.yard_z - right_z * 2,
            building.base_y,
            to_block_id(BlockType::Planks),
            to_block_id(BlockType::Wood),
            1);
        break;
    case VillageBuildingRole::Storehouse:
        build_crate_stack(
            world,
            building.yard_x + right_x * 2,
            building.yard_z + right_z * 2,
            building.base_y,
            to_block_id(BlockType::Wood),
            to_block_id(BlockType::Planks),
            2);
        build_crate_stack(
            world,
            building.yard_x - right_x * 2,
            building.yard_z - right_z * 2,
            building.base_y,
            to_block_id(BlockType::Cobblestone),
            to_block_id(BlockType::Wood),
            2);
        break;
    }
}

void build_roof(World& world, const StartingVillageBuilding& building) {
    const auto roof_block = roof_block_for_role(building.role, building.variant_seed);
    const auto ridge_block = trim_block_for_role(building.role, building.variant_seed);
    const auto eave_min_x = building.min_x - 1;
    const auto eave_max_x = building.max_x + 1;
    const auto eave_min_z = building.min_z - 1;
    const auto eave_max_z = building.max_z + 1;
    const auto roof_start_y = roof_base_y(building);

    if (building.facing == VillageFacing::North || building.facing == VillageFacing::South) {
        auto max_rise = 0;
        for (int z = eave_min_z; z <= eave_max_z; ++z) {
            const auto rise = std::min(z - eave_min_z, eave_max_z - z);
            max_rise = std::max(max_rise, rise);
            const auto roof_y = roof_start_y + rise;
            for (int x = eave_min_x; x <= eave_max_x; ++x) {
                set_block_if_needed(world, x, roof_y, z, roof_block);
            }
        }
        const auto ridge_min_z = eave_min_z + max_rise;
        const auto ridge_max_z = eave_max_z - max_rise;
        for (int z = ridge_min_z; z <= ridge_max_z; ++z) {
            for (int x = building.min_x; x <= building.max_x; ++x) {
                set_block_if_needed(world, x, roof_start_y + max_rise + 1, z, ridge_block);
            }
        }
        return;
    }

    auto max_rise = 0;
    for (int x = eave_min_x; x <= eave_max_x; ++x) {
        const auto rise = std::min(x - eave_min_x, eave_max_x - x);
        max_rise = std::max(max_rise, rise);
        const auto roof_y = roof_start_y + rise;
        for (int z = eave_min_z; z <= eave_max_z; ++z) {
            set_block_if_needed(world, x, roof_y, z, roof_block);
        }
    }
    const auto ridge_min_x = eave_min_x + max_rise;
    const auto ridge_max_x = eave_max_x - max_rise;
    for (int x = ridge_min_x; x <= ridge_max_x; ++x) {
        for (int z = building.min_z; z <= building.max_z; ++z) {
            set_block_if_needed(world, x, roof_start_y + max_rise + 1, z, ridge_block);
        }
    }
}

void build_roof_acroteria(World& world, const StartingVillageBuilding& building) {
    const auto trim_block = trim_block_for_role(building.role, building.variant_seed);
    const auto roof_top_y = roof_surface_y(building, building.door_x, building.facing == VillageFacing::North || building.facing == VillageFacing::South ? building.door_z - facing_forward(building.facing).second : building.door_z);
    const auto corner_y = roof_base_y(building) + 1;

    constexpr std::array<std::pair<int, int>, 4> kCornerOffsets {{
        {-1, -1},
        {1, -1},
        {-1, 1},
        {1, 1},
    }};
    for (const auto& corner : kCornerOffsets) {
        const auto ornament_x = corner.first < 0 ? building.min_x - 1 : building.max_x + 1;
        const auto ornament_z = corner.second < 0 ? building.min_z - 1 : building.max_z + 1;
        set_block_if_needed(world, ornament_x, corner_y, ornament_z, trim_block);
    }

    set_block_if_needed(world, building.door_x, std::min(roof_top_y + 1, kWorldMaxY), building.door_z, trim_block);
}

void build_antique_portico(World& world, const StartingVillageBuilding& building) {
    const auto dimensions = choose_building_dimensions(building.role, building.variant_seed);
    const auto forward = facing_forward(building.facing);
    const auto right = facing_right(building.facing);
    const auto forward_x = forward.first;
    const auto forward_z = forward.second;
    const auto right_x = right.first;
    const auto right_z = right.second;
    const auto column_block = trim_block_for_role(building.role, building.variant_seed);
    const auto roof_block = roof_block_for_role(building.role, building.variant_seed);
    const auto portico_y = building.base_y + dimensions.wall_height + 1;
    const auto half_span = std::min((building.facing == VillageFacing::North || building.facing == VillageFacing::South)
                                        ? (building.max_x - building.min_x) / 2
                                        : (building.max_z - building.min_z) / 2,
                                    building.role == VillageBuildingRole::Lodge ? 5 : 4);

    for (int offset = -half_span; offset <= half_span; offset += 2) {
        if (std::abs(offset) <= 1) {
            continue;
        }
        const auto column_x = building.door_x + right_x * offset;
        const auto column_z = building.door_z + right_z * offset;
        set_block_if_needed(world, column_x, building.base_y, column_z, choose_road_block(hash_coords(column_x, column_z, building.base_y, 617)));
        clear_column_above(world, column_x, column_z, building.base_y + 1, std::min(portico_y + 2, kWorldMaxY));
        for (int y = building.base_y + 1; y <= portico_y - 1; ++y) {
            set_block_if_needed(world, column_x, y, column_z, column_block);
        }
        set_block_if_needed(world, column_x, portico_y, column_z, to_block_id(BlockType::Cobblestone));
    }

    for (int offset = -half_span; offset <= half_span; ++offset) {
        const auto beam_x = building.door_x + right_x * offset;
        const auto beam_z = building.door_z + right_z * offset;
        set_block_if_needed(world, beam_x, portico_y, beam_z, column_block);
        if ((offset + half_span) % 2 == 0) {
            set_block_if_needed(world, beam_x, portico_y + 1, beam_z, roof_block);
        }
    }

    const auto step_x = building.door_x + forward_x;
    const auto step_z = building.door_z + forward_z;
    for (int offset = -1; offset <= 1; ++offset) {
        set_block_if_needed(
            world,
            step_x + right_x * offset,
            building.base_y,
            step_z + right_z * offset,
            to_block_id(BlockType::Stone));
    }
}

void build_building(World& world, const StartingVillageBuilding& building, int seed, int salt) {
    const auto dimensions = choose_building_dimensions(building.role, building.variant_seed);
    const auto foundation_block = foundation_block_for_role(building.role, building.variant_seed);
    const auto floor_block = floor_block_for_role(building.role, building.variant_seed);
    const auto wall_block = wall_block_for_role(building.role, building.variant_seed);
    const auto trim_block = trim_block_for_role(building.role, building.variant_seed);
    const auto glass_block = to_block_id(BlockType::Glass);
    const auto doorway = doorway_world_cell(building);

    build_blended_ground_patch(
        world,
        building.min_x - 1,
        building.max_x + 1,
        building.min_z - 1,
        building.max_z + 1,
        building.base_y,
        kVillageBuildingBlendFeather,
        seed,
        salt,
        to_block_id(BlockType::Dirt),
        [](std::uint32_t column_seed) {
            return (column_seed % 7U) == 0U ? to_block_id(BlockType::Dirt) : to_block_id(BlockType::Grass);
        });

    for (int z = building.min_z; z <= building.max_z; ++z) {
        for (int x = building.min_x; x <= building.max_x; ++x) {
            const auto edge = x == building.min_x || x == building.max_x || z == building.min_z || z == building.max_z;
            set_block_if_needed(world, x, building.base_y, z, edge ? foundation_block : floor_block);
            clear_column_above(world, x, z, building.base_y + 1, std::min(building.base_y + dimensions.wall_height + 8, kWorldMaxY));
        }
    }

    for (int z = building.min_z + 1; z <= building.max_z - 1; ++z) {
        for (int x = building.min_x + 1; x <= building.max_x - 1; ++x) {
            clear_column_above(world, x, z, building.base_y + 1, std::min(building.base_y + dimensions.wall_height + 7, kWorldMaxY));
        }
    }

    for (int y = building.base_y + 1; y <= building.base_y + dimensions.wall_height; ++y) {
        for (int z = building.min_z; z <= building.max_z; ++z) {
            for (int x = building.min_x; x <= building.max_x; ++x) {
                const auto edge = x == building.min_x || x == building.max_x || z == building.min_z || z == building.max_z;
                if (!edge) {
                    continue;
                }

                if (x == doorway.x && z == doorway.z && y <= building.base_y + 2) {
                    set_block_if_needed(world, x, y, z, to_block_id(BlockType::Air));
                    continue;
                }

                BlockId block = wall_block;
                if (x == doorway.x && z == doorway.z && y == building.base_y + 3) {
                    block = trim_block;
                } else if (should_place_window(building, doorway, x, y, z)) {
                    block = glass_block;
                } else if ((x == building.min_x || x == building.max_x) && (z == building.min_z || z == building.max_z)) {
                    block = trim_block;
                } else if ((building.role == VillageBuildingRole::Workshop || building.role == VillageBuildingRole::Storehouse) && y == building.base_y + 1) {
                    block = foundation_block;
                }

                set_block_if_needed(world, x, y, z, block);
            }
        }
    }

    build_upper_walls_and_gables(world, building, wall_block, trim_block);
    build_roof(world, building);
    build_roof_acroteria(world, building);
    build_antique_portico(world, building);

    const auto forward = facing_forward(building.facing);
    const auto forward_x = forward.first;
    const auto forward_z = forward.second;
    set_block_if_needed(world, doorway.x - forward_x, doorway.y, doorway.z - forward_z, to_block_id(BlockType::Air));
    set_block_if_needed(world, doorway.x - forward_x, doorway.y + 1, doorway.z - forward_z, to_block_id(BlockType::Air));
}

void build_plaza(World& world, const StartingVillageLayout& layout) {
    build_blended_ground_patch(
        world,
        layout.center_x - kVillagePlazaHalfExtent,
        layout.center_x + kVillagePlazaHalfExtent,
        layout.center_z - kVillagePlazaHalfExtent,
        layout.center_z + kVillagePlazaHalfExtent,
        layout.base_y,
        kVillagePlazaBlendFeather,
        layout.seed,
        801,
        to_block_id(BlockType::Dirt),
        [](std::uint32_t column_seed) {
            return (column_seed % 6U) == 0U ? to_block_id(BlockType::Dirt) : to_block_id(BlockType::Grass);
        });
    build_surface_patch(
        world,
        layout.center_x - kVillagePlazaHalfExtent,
        layout.center_x + kVillagePlazaHalfExtent,
        layout.center_z - kVillagePlazaHalfExtent,
        layout.center_z + kVillagePlazaHalfExtent,
        layout.base_y,
        layout.seed,
        811,
        to_block_id(BlockType::Dirt),
        [](std::uint32_t column_seed) {
            switch (column_seed % 5U) {
            case 0U:
                return to_block_id(BlockType::MossyStone);
            case 1U:
                return to_block_id(BlockType::Gravel);
            default:
                return to_block_id(BlockType::Cobblestone);
            }
        });

    decorate_green_patch(world, layout.center_x - 6, layout.center_z - 6, 1, layout.base_y, layout.seed, 821);
    decorate_green_patch(world, layout.center_x + 6, layout.center_z - 6, 1, layout.base_y, layout.seed, 822);
    decorate_green_patch(world, layout.center_x - 6, layout.center_z + 6, 1, layout.base_y, layout.seed, 823);
    decorate_green_patch(world, layout.center_x + 6, layout.center_z + 6, 1, layout.base_y, layout.seed, 824);

    build_village_well(world, layout.center_x, layout.center_z, layout.base_y, layout.seed);
    build_lamp_post(world, layout.center_x - 8, layout.center_z, layout.base_y, to_block_id(BlockType::Stone));
    build_lamp_post(world, layout.center_x + 8, layout.center_z, layout.base_y, to_block_id(BlockType::Stone));
    build_lamp_post(world, layout.center_x, layout.center_z - 8, layout.base_y, to_block_id(BlockType::Stone));
    build_lamp_post(world, layout.center_x, layout.center_z + 8, layout.base_y, to_block_id(BlockType::Stone));
}

void build_outer_landscape(World& world, const StartingVillageLayout& layout) {
    const auto west_tree_x = layout.center_x - kVillageLoopHalfExtentX - 12;
    const auto east_tree_x = layout.center_x + kVillageLoopHalfExtentX + 12;
    const auto north_tree_z = layout.center_z - kVillageLoopHalfExtentZ - 10;
    const auto south_tree_z = layout.center_z + kVillageLoopHalfExtentZ + 10;

    build_small_tree_on_natural_ground(world, west_tree_x, north_tree_z, hash_coords(west_tree_x, north_tree_z, layout.seed, 901), false);
    build_small_tree_on_natural_ground(world, east_tree_x, north_tree_z, hash_coords(east_tree_x, north_tree_z, layout.seed, 902), true);
    build_small_tree_on_natural_ground(world, west_tree_x, south_tree_z, hash_coords(west_tree_x, south_tree_z, layout.seed, 903), true);
    build_small_tree_on_natural_ground(world, east_tree_x, south_tree_z, hash_coords(east_tree_x, south_tree_z, layout.seed, 904), false);

    decorate_natural_green_patch(world, layout.center_x - 16, layout.center_z - 12, 2, layout.seed, 911);
    decorate_natural_green_patch(world, layout.center_x + 16, layout.center_z - 12, 2, layout.seed, 912);
    decorate_natural_green_patch(world, layout.center_x - 16, layout.center_z + 12, 2, layout.seed, 913);
    decorate_natural_green_patch(world, layout.center_x + 16, layout.center_z + 12, 2, layout.seed, 914);
}

} // namespace

StartingVillageGenerator::StartingVillageGenerator(int seed)
    : seed_(seed) {
}

auto StartingVillageGenerator::build_layout() const -> StartingVillageLayout {
    WorldGenerator generator(seed_);
    const auto site = choose_site(generator);

    StartingVillageLayout layout {};
    layout.seed = seed_;
    layout.center_x = site.center_x;
    layout.center_z = site.center_z;
    layout.base_y = std::max(site.base_y, kSeaLevel + 3);

    const auto north_front_z = layout.center_z - kVillageLoopHalfExtentZ - 4;
    const auto south_front_z = layout.center_z + kVillageLoopHalfExtentZ + 4;
    const auto west_front_x = layout.center_x - kVillageLoopHalfExtentX - 4;
    const auto east_front_x = layout.center_x + kVillageLoopHalfExtentX + 4;

    const std::array<VillagePlanEntry, 10> plan_entries {{
        {VillageBuildingRole::House,      VillageFacing::South, layout.center_x - 18, north_front_z, 11},
        {VillageBuildingRole::Workshop,   VillageFacing::South, layout.center_x,      north_front_z - 1, 12},
        {VillageBuildingRole::Lodge,      VillageFacing::South, layout.center_x + 18, north_front_z - 1, 13},
        {VillageBuildingRole::House,      VillageFacing::North, layout.center_x - 18, south_front_z, 21},
        {VillageBuildingRole::Storehouse, VillageFacing::North, layout.center_x,      south_front_z + 1, 22},
        {VillageBuildingRole::House,      VillageFacing::North, layout.center_x + 18, south_front_z, 23},
        {VillageBuildingRole::House,      VillageFacing::East,  west_front_x,         layout.center_z - 10, 31},
        {VillageBuildingRole::Workshop,   VillageFacing::East,  west_front_x - 1,     layout.center_z + 10, 32},
        {VillageBuildingRole::House,      VillageFacing::West,  east_front_x,         layout.center_z - 10, 41},
        {VillageBuildingRole::Storehouse, VillageFacing::West,  east_front_x + 1,     layout.center_z + 9, 42},
    }};

    for (const auto& plan_entry : plan_entries) {
        const auto building = make_building_from_front(
            plan_entry.front_x,
            plan_entry.front_z,
            layout.base_y,
            plan_entry.role,
            plan_entry.facing,
            seed_,
            plan_entry.salt);
        expand_layout_bounds(layout, building);
        layout.buildings.push_back(building);
    }

    layout.min_x = std::min(layout.min_x, layout.center_x - kVillageMainRoadReachX - kVillageBoundsPadding);
    layout.max_x = std::max(layout.max_x, layout.center_x + kVillageMainRoadReachX + kVillageBoundsPadding);
    layout.min_z = std::min(layout.min_z, layout.center_z - kVillageMainRoadReachZ - kVillageBoundsPadding);
    layout.max_z = std::max(layout.max_z, layout.center_z + kVillageMainRoadReachZ + kVillageBoundsPadding);
    layout.player_spawn = make_world_position(layout.center_x, layout.base_y, layout.center_z + kVillagePlazaHalfExtent + 6);

    layout.residents.reserve(layout.buildings.size());
    for (std::size_t index = 0; index < layout.buildings.size(); ++index) {
        layout.residents.push_back(make_resident_anchor(
            layout.buildings[index],
            layout,
            seed_,
            static_cast<int>(index) * 17 + 57));
    }

    return layout;
}

void StartingVillageGenerator::apply(World& world, const StartingVillageLayout& layout) const {
    if (layout.buildings.empty()) {
        return;
    }

    build_path_patch(
        world,
        layout.center_x - kVillageMainRoadReachX,
        layout.center_x + kVillageMainRoadReachX,
        layout.center_z - kVillageMainRoadHalfWidth,
        layout.center_z + kVillageMainRoadHalfWidth,
        layout.base_y,
        layout.seed,
        1011);
    build_path_patch(
        world,
        layout.center_x - kVillageMainRoadHalfWidth,
        layout.center_x + kVillageMainRoadHalfWidth,
        layout.center_z - kVillageMainRoadReachZ,
        layout.center_z + kVillageMainRoadReachZ,
        layout.base_y,
        layout.seed,
        1012);

    const auto west_loop_x = layout.center_x - kVillageLoopHalfExtentX;
    const auto east_loop_x = layout.center_x + kVillageLoopHalfExtentX;
    const auto north_loop_z = layout.center_z - kVillageLoopHalfExtentZ;
    const auto south_loop_z = layout.center_z + kVillageLoopHalfExtentZ;

    build_path_patch(
        world,
        west_loop_x - 1,
        east_loop_x + 1,
        north_loop_z - kVillageLoopRoadHalfWidth,
        north_loop_z + kVillageLoopRoadHalfWidth,
        layout.base_y,
        layout.seed,
        1021);
    build_path_patch(
        world,
        west_loop_x - 1,
        east_loop_x + 1,
        south_loop_z - kVillageLoopRoadHalfWidth,
        south_loop_z + kVillageLoopRoadHalfWidth,
        layout.base_y,
        layout.seed,
        1022);
    build_path_patch(
        world,
        west_loop_x - kVillageLoopRoadHalfWidth,
        west_loop_x + kVillageLoopRoadHalfWidth,
        north_loop_z - 1,
        south_loop_z + 1,
        layout.base_y,
        layout.seed,
        1023);
    build_path_patch(
        world,
        east_loop_x - kVillageLoopRoadHalfWidth,
        east_loop_x + kVillageLoopRoadHalfWidth,
        north_loop_z - 1,
        south_loop_z + 1,
        layout.base_y,
        layout.seed,
        1024);

    build_plaza(world, layout);
    build_outer_landscape(world, layout);

    for (std::size_t index = 0; index < layout.buildings.size(); ++index) {
        decorate_frontage(world, layout.buildings[index], layout.seed, 1100 + static_cast<int>(index) * 7);
    }
    for (std::size_t index = 0; index < layout.buildings.size(); ++index) {
        build_building(world, layout.buildings[index], layout.seed, 1200 + static_cast<int>(index) * 11);
    }
}

} // namespace valcraft
