#include "render/MarineDecor.h"

#include "world/OceanAdventureLayout.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include <optional>

namespace valcraft {
namespace {

constexpr int kPortDecorExclusionMargin = 3;
constexpr int kMinimumDecorDepth = 2;
constexpr int kMaximumDecorDepth = 36;
constexpr int kSparseDeepDecorDepth = 22;
constexpr int kMaximumAcceptedSlopeStep = 3;

struct RankedMarineDecor {
    MarineDecorInstance instance {};
    std::uint32_t selection_score = 0U;
    std::uint8_t grid_ordinal = 0U;
};

[[nodiscard]] constexpr auto is_supported_substrate(BlockId block) noexcept
    -> bool {
    return block == to_block_id(BlockType::Sand) ||
           block == to_block_id(BlockType::Gravel) ||
           block == to_block_id(BlockType::MossyStone);
}

[[nodiscard]] constexpr auto inside_expanded_rectangle(
    int world_x,
    int world_z,
    int min_x,
    int max_x,
    int min_z,
    int max_z,
    int margin) noexcept -> bool {
    return world_x >= min_x - margin && world_x <= max_x + margin &&
           world_z >= min_z - margin && world_z <= max_z + margin;
}

[[nodiscard]] constexpr auto is_excluded_column(
    int world_x,
    int world_z) noexcept -> bool {
    if (is_ocean_navigation_corridor_column(world_x, world_z)) {
        return true;
    }

    return inside_expanded_rectangle(
               world_x,
               world_z,
               kStartingPortMinX,
               kStartingPortMaxX,
               kStartingPortMinZ,
               kStartingPortMaxZ,
               kPortDecorExclusionMargin) ||
           inside_expanded_rectangle(
               world_x,
               world_z,
               kStartingPortBasinMinX,
               kStartingPortBasinMaxX,
               kStartingPortBasinMinZ,
               kStartingPortBasinMaxZ,
               kPortDecorExclusionMargin);
}

[[nodiscard]] constexpr auto is_valid_submerged_surface(
    const TerrainSurfaceSample& sample) noexcept -> bool {
    return sample.surface_height >= kWorldMinY &&
           sample.surface_height <= kWorldMaxY &&
           sample.water_level > sample.surface_height &&
           sample.water_level <= kWorldMaxY &&
           is_supported_substrate(sample.surface_block);
}

[[nodiscard]] constexpr auto unit_float(std::uint32_t value) noexcept -> float {
    return static_cast<float>(value & 0x00FFFFFFU) /
           static_cast<float>(0x01000000U);
}

[[nodiscard]] auto maximum_neighbor_step(
    int world_x,
    int world_z,
    int center_height,
    const MarineTerrainSurfaceSampler& sample_surface) -> std::optional<int> {
    constexpr std::array<std::array<int, 2>, 4> kNeighborOffsets {{
        {{-1, 0}},
        {{1, 0}},
        {{0, -1}},
        {{0, 1}},
    }};

    auto maximum_step = 0;
    for (const auto& offset : kNeighborOffsets) {
        const auto neighbor =
            sample_surface(world_x + offset[0], world_z + offset[1]);
        if (neighbor.surface_height < kWorldMinY ||
            neighbor.surface_height > kWorldMaxY) {
            return std::nullopt;
        }

        const auto difference =
            static_cast<std::int64_t>(neighbor.surface_height) -
            static_cast<std::int64_t>(center_height);
        const auto absolute_difference =
            difference < 0 ? -difference : difference;
        maximum_step = std::max(
            maximum_step,
            static_cast<int>(absolute_difference));
    }
    return maximum_step;
}

[[nodiscard]] auto choose_kind(
    int depth,
    int maximum_slope_step,
    std::uint32_t value) noexcept -> std::optional<MarineDecorKind> {
    std::array<MarineDecorKind, 9> eligible {};
    std::size_t count = 0U;

    const auto append = [&eligible, &count](MarineDecorKind kind) {
        eligible[count++] = kind;
    };

    if (depth >= 2 && depth <= 18 && maximum_slope_step <= 2) {
        append(MarineDecorKind::Seagrass);
        append(MarineDecorKind::Seagrass);
    }
    if (depth >= 5 && depth <= 34 && maximum_slope_step <= 2) {
        append(MarineDecorKind::Kelp);
        append(MarineDecorKind::Kelp);
    }
    if (depth >= 2 && depth <= 24 && maximum_slope_step <= 3) {
        append(MarineDecorKind::CoralFan);
    }
    if (depth >= 2 && depth <= 30 && maximum_slope_step <= 3) {
        append(MarineDecorKind::BranchCoralWarm);
        append(MarineDecorKind::BranchCoralLagoon);
    }
    if (depth >= 2 && depth <= 36 && maximum_slope_step <= 1) {
        append(MarineDecorKind::Shell);
    }

    if (count == 0U) {
        return std::nullopt;
    }
    return eligible[value % count];
}

[[nodiscard]] constexpr auto material_for_kind(
    MarineDecorKind kind) noexcept -> VisualMaterialId {
    switch (kind) {
    case MarineDecorKind::Seagrass:
        return VisualMaterialId::MarineSeagrass;
    case MarineDecorKind::Kelp:
        return VisualMaterialId::MarineKelp;
    case MarineDecorKind::CoralFan:
        return VisualMaterialId::CoralFan;
    case MarineDecorKind::BranchCoralWarm:
        return VisualMaterialId::CoralWarm;
    case MarineDecorKind::BranchCoralLagoon:
        return VisualMaterialId::CoralLagoon;
    case MarineDecorKind::Shell:
        return VisualMaterialId::MarineShell;
    }
    return VisualMaterialId::None;
}

void apply_scale(
    MarineDecorInstance& instance,
    int depth,
    std::uint32_t scale_hash) noexcept {
    const auto first = unit_float(scale_hash);
    const auto second = unit_float(scale_hash * 0x9E3779B9U + 0x85EBCA6BU);

    switch (instance.kind) {
    case MarineDecorKind::Seagrass:
        instance.scale_x = 0.58F + first * 0.38F;
        instance.scale_y = std::min(0.78F + second * 0.72F,
                                    static_cast<float>(depth) - 0.5F);
        instance.scale_z = instance.scale_x;
        break;
    case MarineDecorKind::Kelp: {
        const auto maximum_height =
            std::min(6.0F, static_cast<float>(depth) - 1.5F);
        const auto minimum_height = std::min(2.25F, maximum_height);
        instance.scale_x = 0.42F + first * 0.26F;
        instance.scale_y =
            minimum_height + (maximum_height - minimum_height) * second;
        instance.scale_z = instance.scale_x;
        break;
    }
    case MarineDecorKind::CoralFan:
        instance.scale_x = 0.62F + first * 0.58F;
        instance.scale_y = std::min(0.70F + second * 0.75F,
                                    static_cast<float>(depth) - 0.5F);
        instance.scale_z = 0.22F + first * 0.14F;
        break;
    case MarineDecorKind::BranchCoralWarm:
    case MarineDecorKind::BranchCoralLagoon: {
        const auto scale = 0.58F + first * 0.68F;
        instance.scale_x = scale;
        instance.scale_y =
            std::min(scale * (0.90F + second * 0.22F),
                     static_cast<float>(depth) - 0.5F);
        instance.scale_z = scale;
        break;
    }
    case MarineDecorKind::Shell:
        instance.scale_x = 0.24F + first * 0.34F;
        instance.scale_y = 0.18F + second * 0.24F;
        instance.scale_z = instance.scale_x * (0.72F + second * 0.18F);
        break;
    }
}

[[nodiscard]] auto valid_chunk_origin(
    ChunkCoord chunk_coord,
    int& origin_x,
    int& origin_z) noexcept -> bool {
    const auto wide_x =
        static_cast<std::int64_t>(chunk_coord.x) * kChunkSizeX;
    const auto wide_z =
        static_cast<std::int64_t>(chunk_coord.z) * kChunkSizeZ;
    constexpr auto kMinimumOrigin =
        static_cast<std::int64_t>(std::numeric_limits<int>::min()) + 1;
    constexpr auto kMaximumOrigin =
        static_cast<std::int64_t>(std::numeric_limits<int>::max()) -
        kChunkSizeX;
    if (wide_x < kMinimumOrigin || wide_x > kMaximumOrigin ||
        wide_z < kMinimumOrigin || wide_z > kMaximumOrigin) {
        return false;
    }

    origin_x = static_cast<int>(wide_x);
    origin_z = static_cast<int>(wide_z);
    return true;
}

} // namespace

auto build_marine_decor(
    ChunkCoord chunk_coord,
    WorldGenerationVersion generation_version,
    int world_seed,
    const MarineTerrainSurfaceSampler& sample_surface)
    -> std::vector<MarineDecorInstance> {
    std::vector<MarineDecorInstance> instances;
    const auto supports_marine_decor =
        generation_version == WorldGenerationVersion::SparseArchipelagoV2 ||
        generation_version == WorldGenerationVersion::LivingOceanV3;
    if (!supports_marine_decor ||
        !sample_surface) {
        return instances;
    }

    int origin_x = 0;
    int origin_z = 0;
    if (!valid_chunk_origin(chunk_coord, origin_x, origin_z)) {
        return instances;
    }

    std::vector<RankedMarineDecor> ranked_instances;
    ranked_instances.reserve(
        static_cast<std::size_t>(
            (kChunkSizeX / kMarineDecorGridStep) *
            (kChunkSizeZ / kMarineDecorGridStep)));
    for (int local_z = 0; local_z < kChunkSizeZ;
         local_z += kMarineDecorGridStep) {
        for (int local_x = 0; local_x < kChunkSizeX;
             local_x += kMarineDecorGridStep) {
            const auto grid_x = origin_x + local_x;
            const auto grid_z = origin_z + local_z;
            const auto candidate_hash = ocean_adventure_layout_hash(
                grid_x,
                grid_z,
                world_seed,
                0x4D415249U);
            if ((candidate_hash & 3U) == 0U) {
                continue;
            }

            const auto world_x =
                grid_x + static_cast<int>((candidate_hash >> 6U) & 1U);
            const auto world_z =
                grid_z + static_cast<int>((candidate_hash >> 7U) & 1U);
            if (is_excluded_column(world_x, world_z)) {
                continue;
            }

            const auto surface = sample_surface(world_x, world_z);
            if (!is_valid_submerged_surface(surface)) {
                continue;
            }

            const auto depth = surface.water_level - surface.surface_height;
            if (depth < kMinimumDecorDepth || depth > kMaximumDecorDepth) {
                continue;
            }
            if (depth > kSparseDeepDecorDepth &&
                ((candidate_hash >> 10U) & 3U) != 0U) {
                // Je garde les grands fonds vivants mais plus clairsemés : le
                // nombre de sommets reste inférieur au budget des hauts-fonds.
                continue;
            }

            const auto maximum_step = maximum_neighbor_step(
                world_x,
                world_z,
                surface.surface_height,
                sample_surface);
            if (!maximum_step.has_value() ||
                *maximum_step > kMaximumAcceptedSlopeStep) {
                continue;
            }

            const auto variation_hash = ocean_adventure_layout_hash(
                world_x,
                world_z,
                world_seed,
                0x4445434FU);
            const auto kind =
                choose_kind(depth, *maximum_step, variation_hash);
            if (!kind.has_value()) {
                continue;
            }

            MarineDecorInstance instance {};
            instance.position_x = static_cast<float>(world_x) + 0.5F;
            instance.position_y =
                static_cast<float>(surface.surface_height + 1);
            instance.position_z = static_cast<float>(world_z) + 0.5F;
            instance.yaw_radians =
                unit_float(variation_hash * 0x85EBCA6BU + 0xC2B2AE35U) *
                (2.0F * std::numbers::pi_v<float>);
            instance.phase =
                unit_float(variation_hash * 0x27D4EB2DU + 0x165667B1U) *
                (2.0F * std::numbers::pi_v<float>);
            instance.kind = *kind;
            instance.material = material_for_kind(*kind);
            apply_scale(
                instance,
                depth,
                variation_hash * 0x7FEB352DU + 0x846CA68BU);
            const auto grid_ordinal =
                static_cast<std::uint8_t>(
                    (local_z / kMarineDecorGridStep) *
                        (kChunkSizeX / kMarineDecorGridStep) +
                    (local_x / kMarineDecorGridStep));
            ranked_instances.push_back({
                instance,
                ocean_adventure_layout_hash(
                    world_x,
                    world_z,
                    world_seed,
                    0x53454C45U),
                grid_ordinal,
            });
        }
    }

    // Je sélectionne le budget par score spatial plutôt que par ordre de
    // parcours : aucun bord du chunk ne perd systématiquement sa végétation.
    const auto selection_order =
        [](const RankedMarineDecor& left,
           const RankedMarineDecor& right) noexcept {
            return left.selection_score < right.selection_score ||
                   (left.selection_score == right.selection_score &&
                    left.grid_ordinal < right.grid_ordinal);
        };
    std::sort(
        ranked_instances.begin(),
        ranked_instances.end(),
        selection_order);
    if (ranked_instances.size() >
        kMarineDecorMaxInstancesPerChunk) {
        ranked_instances.resize(
            kMarineDecorMaxInstancesPerChunk);
    }
    std::sort(
        ranked_instances.begin(),
        ranked_instances.end(),
        [](const RankedMarineDecor& left,
           const RankedMarineDecor& right) noexcept {
            return left.grid_ordinal < right.grid_ordinal;
        });

    instances.reserve(ranked_instances.size());
    for (const auto& ranked : ranked_instances) {
        instances.push_back(ranked.instance);
    }
    return instances;
}

} // namespace valcraft
