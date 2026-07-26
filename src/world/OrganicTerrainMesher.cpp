#include "world/OrganicTerrainMesher.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace valcraft {

namespace {

using Float3 = std::array<float, 3>;

constexpr std::size_t kMaximumCachedSampleCount = 16U * 1024U * 1024U;
constexpr float kMaximumSafeDisplacement = 0.459F;
constexpr float kNormalEpsilonSquared = 1.0e-12F;
constexpr float kTriangleEpsilonSquared = 1.0e-12F;
constexpr std::int32_t kMissingVertex = -1;
constexpr std::uint32_t kMissingRefinementVertex =
    std::numeric_limits<std::uint32_t>::max();
constexpr int kNormalSampleHalo = 2;

constexpr std::array<BlockCoord, 8> kCornerOffsets {{
    {0, 0, 0},
    {1, 0, 0},
    {0, 1, 0},
    {1, 1, 0},
    {0, 0, 1},
    {1, 0, 1},
    {0, 1, 1},
    {1, 1, 1},
}};

struct CellEdge {
    std::uint8_t first = 0;
    std::uint8_t second = 0;
};

constexpr std::array<CellEdge, 12> kCellEdges {{
    {0, 1},
    {2, 3},
    {4, 5},
    {6, 7},
    {0, 2},
    {1, 3},
    {4, 6},
    {5, 7},
    {0, 4},
    {1, 5},
    {2, 6},
    {3, 7},
}};

enum class SurfaceDirection : std::uint8_t {
    PositiveX,
    NegativeX,
    PositiveY,
    NegativeY,
    PositiveZ,
    NegativeZ,
};

constexpr std::array<SurfaceDirection, 6> kSurfaceDirections {{
    SurfaceDirection::PositiveX,
    SurfaceDirection::NegativeX,
    SurfaceDirection::PositiveY,
    SurfaceDirection::NegativeY,
    SurfaceDirection::PositiveZ,
    SurfaceDirection::NegativeZ,
}};

struct CachedSampleGrid {
    BlockCoord min {};
    BlockCoord max {};
    std::size_t size_x = 0;
    std::size_t size_y = 0;
    std::size_t size_z = 0;
    std::vector<OrganicTerrainCellSample> samples {};
    std::vector<std::int8_t> organic_density {};

    [[nodiscard]] auto index_of(int x, int y, int z) const noexcept -> std::size_t {
        const auto local_x = static_cast<std::size_t>(x - min.x);
        const auto local_y = static_cast<std::size_t>(y - min.y);
        const auto local_z = static_cast<std::size_t>(z - min.z);
        return (local_y * size_z + local_z) * size_x + local_x;
    }

    [[nodiscard]] auto at(int x, int y, int z) const noexcept -> const OrganicTerrainCellSample& {
        return samples[index_of(x, y, z)];
    }

    [[nodiscard]] auto density_at(int x, int y, int z) const noexcept -> float {
        return static_cast<float>(organic_density[index_of(x, y, z)]);
    }
};

struct ActiveCellCache {
    BlockCoord min {};
    std::size_t size_x = 0;
    std::size_t size_y = 0;
    std::size_t size_z = 0;
    std::vector<std::int32_t> vertex_indices {};

    [[nodiscard]] auto index_of(const BlockCoord& coord) const noexcept -> std::size_t {
        const auto local_x = static_cast<std::size_t>(coord.x - min.x);
        const auto local_y = static_cast<std::size_t>(coord.y - min.y);
        const auto local_z = static_cast<std::size_t>(coord.z - min.z);
        return (local_y * size_z + local_z) * size_x + local_x;
    }

    [[nodiscard]] auto vertex_index(const BlockCoord& coord) noexcept -> std::int32_t& {
        return vertex_indices[index_of(coord)];
    }
};

struct MaterialVote {
    BlockId block_id = to_block_id(BlockType::Air);
    std::uint8_t count = 0;
};

struct GeologicalMaterialPair {
    BlockId surface = to_block_id(BlockType::Grass);
    BlockId rock = to_block_id(BlockType::Stone);
    float source_bias = 0.0F;
};

[[nodiscard]] auto checked_volume(std::size_t size_x, std::size_t size_y, std::size_t size_z)
    -> std::size_t {
    if (size_x == 0 || size_y == 0 || size_z == 0 ||
        size_x > kMaximumCachedSampleCount / size_y ||
        size_x * size_y > kMaximumCachedSampleCount / size_z) {
        throw std::length_error("La section de terrain organique dépasse mon budget de travail");
    }
    return size_x * size_y * size_z;
}

[[nodiscard]] auto inclusive_extent(int min_value, int max_value) -> std::size_t {
    const auto extent = static_cast<std::int64_t>(max_value) -
                        static_cast<std::int64_t>(min_value) + 1;
    if (extent <= 0) {
        return 0;
    }
    return static_cast<std::size_t>(extent);
}

[[nodiscard]] auto add(const BlockCoord& lhs, const BlockCoord& rhs) noexcept -> BlockCoord {
    return {
        lhs.x + rhs.x,
        lhs.y + rhs.y,
        lhs.z + rhs.z,
    };
}

[[nodiscard]] auto subtract(const Float3& lhs, const Float3& rhs) noexcept -> Float3 {
    return {
        lhs[0] - rhs[0],
        lhs[1] - rhs[1],
        lhs[2] - rhs[2],
    };
}

[[nodiscard]] auto cross(const Float3& lhs, const Float3& rhs) noexcept -> Float3 {
    return {
        lhs[1] * rhs[2] - lhs[2] * rhs[1],
        lhs[2] * rhs[0] - lhs[0] * rhs[2],
        lhs[0] * rhs[1] - lhs[1] * rhs[0],
    };
}

[[nodiscard]] auto length_squared(const Float3& value) noexcept -> float {
    return value[0] * value[0] + value[1] * value[1] + value[2] * value[2];
}

[[nodiscard]] auto normalized_or(const Float3& value, const Float3& fallback) noexcept -> Float3 {
    auto candidate = value;
    auto candidate_length_squared = length_squared(candidate);
    if (!std::isfinite(candidate_length_squared) || candidate_length_squared <= kNormalEpsilonSquared) {
        candidate = fallback;
        candidate_length_squared = length_squared(candidate);
    }
    if (!std::isfinite(candidate_length_squared) || candidate_length_squared <= kNormalEpsilonSquared) {
        return {0.0F, 1.0F, 0.0F};
    }

    const auto inverse_length = 1.0F / std::sqrt(candidate_length_squared);
    return {
        candidate[0] * inverse_length,
        candidate[1] * inverse_length,
        candidate[2] * inverse_length,
    };
}

[[nodiscard]] auto triangle_area_squared(const Float3& a,
                                         const Float3& b,
                                         const Float3& c) noexcept -> float {
    return length_squared(cross(subtract(b, a), subtract(c, a)));
}

[[nodiscard]] auto sample_grid(const OrganicTerrainSection& section,
                               const OrganicTerrainSampler& sampler) -> CachedSampleGrid {
    CachedSampleGrid grid {};
    // Je garde la première cellule de halo pour la topologie et la seconde
    // uniquement pour calculer un gradient visuel continu. Aucun échantillon de
    // ce halo supplémentaire ne peut créer ou retirer un triangle.
    grid.min = {
        section.min.x - kNormalSampleHalo,
        section.min.y - kNormalSampleHalo,
        section.min.z - kNormalSampleHalo,
    };
    grid.max = {
        section.max.x + kNormalSampleHalo,
        section.max.y + kNormalSampleHalo,
        section.max.z + kNormalSampleHalo,
    };
    grid.size_x = inclusive_extent(grid.min.x, grid.max.x);
    grid.size_y = inclusive_extent(grid.min.y, grid.max.y);
    grid.size_z = inclusive_extent(grid.min.z, grid.max.z);
    grid.samples.resize(checked_volume(grid.size_x, grid.size_y, grid.size_z));
    grid.organic_density.resize(grid.samples.size());

    for (int y = grid.min.y; y <= grid.max.y; ++y) {
        for (int z = grid.min.z; z <= grid.max.z; ++z) {
            for (int x = grid.min.x; x <= grid.max.x; ++x) {
                auto sample = sampler(x, y, z);
                sample.sky_light = std::min<std::uint8_t>(sample.sky_light, 15);
                sample.block_light = std::min<std::uint8_t>(sample.block_light, 15);
                const auto index = grid.index_of(x, y, z);
                grid.samples[index] = sample;
                grid.organic_density[index] =
                    is_organic_terrain_block(sample.block_id)
                        ? std::int8_t {1}
                        : std::int8_t {-1};
            }
        }
    }
    return grid;
}

[[nodiscard]] auto make_active_cell_cache(const OrganicTerrainSection& section) -> ActiveCellCache {
    ActiveCellCache cache {};
    cache.min = {section.min.x - 1, section.min.y - 1, section.min.z - 1};
    const BlockCoord max {section.max.x, section.max.y, section.max.z};
    cache.size_x = inclusive_extent(cache.min.x, max.x);
    cache.size_y = inclusive_extent(cache.min.y, max.y);
    cache.size_z = inclusive_extent(cache.min.z, max.z);
    cache.vertex_indices.assign(
        checked_volume(cache.size_x, cache.size_y, cache.size_z),
        kMissingVertex);
    return cache;
}

struct CubicWeights {
    std::array<float, 4> value {};
    std::array<float, 4> derivative {};
};

struct SmoothedFieldSample {
    float density = 0.0F;
    Float3 gradient {0.0F, 0.0F, 0.0F};
};

struct ActiveVertexBuild {
    TerrainVertex vertex {};
    Float3 unrelaxed_position {};
};

struct PendingQuad {
    std::array<std::uint32_t, 4> vertices {};
    std::uint32_t material_owner = 0U;
};

using EdgeKey = std::pair<std::uint32_t, std::uint32_t>;

struct BinarySurfaceSample {
    bool active = false;
    std::uint8_t solid_count = 0U;
    Float3 position {};
    Float3 outward {0.0F, 1.0F, 0.0F};
};

struct BinarySurfaceCache {
    BlockCoord min {};
    std::size_t size_x = 0U;
    std::size_t size_y = 0U;
    std::size_t size_z = 0U;
    std::vector<BinarySurfaceSample> samples {};
    std::vector<std::uint8_t> initialized {};

    [[nodiscard]] auto index_of(const BlockCoord& coord) const noexcept
        -> std::size_t {
        const auto local_x =
            static_cast<std::size_t>(coord.x - min.x);
        const auto local_y =
            static_cast<std::size_t>(coord.y - min.y);
        const auto local_z =
            static_cast<std::size_t>(coord.z - min.z);
        return (local_y * size_z + local_z) * size_x + local_x;
    }
};

[[nodiscard]] auto cubic_bspline_weights(float parameter) noexcept
    -> CubicWeights {
    const auto t = std::clamp(parameter, 0.0F, 1.0F);
    const auto one_minus_t = 1.0F - t;
    const auto t_squared = t * t;
    const auto one_minus_t_squared = one_minus_t * one_minus_t;
    return {
        {
            one_minus_t_squared * one_minus_t / 6.0F,
            (3.0F * t_squared * t -
             6.0F * t_squared +
             4.0F) /
                6.0F,
            (-3.0F * t_squared * t +
             3.0F * t_squared +
             3.0F * t +
             1.0F) /
                6.0F,
            t_squared * t / 6.0F,
        },
        {
            -0.5F * one_minus_t_squared,
            1.5F * t_squared - 2.0F * t,
            -1.5F * t_squared + t + 0.5F,
            0.5F * t_squared,
        },
    };
}

[[nodiscard]] auto dot(const Float3& lhs, const Float3& rhs) noexcept -> float {
    return lhs[0] * rhs[0] + lhs[1] * rhs[1] + lhs[2] * rhs[2];
}

[[nodiscard]] auto sample_smoothed_field(const CachedSampleGrid& grid,
                                         const BlockCoord& cell,
                                         const Float3& position) noexcept
    -> SmoothedFieldSample {
    const std::array<CubicWeights, 3> weights {{
        cubic_bspline_weights(
            position[0] - (static_cast<float>(cell.x) + 0.5F)),
        cubic_bspline_weights(
            position[1] - (static_cast<float>(cell.y) + 0.5F)),
        cubic_bspline_weights(
            position[2] - (static_cast<float>(cell.z) + 0.5F)),
    }};

    // Je dérive un champ B-spline cubique C2 depuis les densités déjà
    // préclassées du cache. Je conserve ainsi la continuité visuelle validée
    // sans répéter le switch BlockId dans chaque évaluation.
    SmoothedFieldSample result {};
    std::array<std::array<float, 4>, 4> density_along_x {};
    std::array<std::array<float, 4>, 4> derivative_along_x {};
    for (std::size_t y = 0U; y < 4U; ++y) {
        for (std::size_t z = 0U; z < 4U; ++z) {
            const auto first_index = grid.index_of(
                cell.x - 1,
                cell.y - 1 + static_cast<int>(y),
                cell.z - 1 + static_cast<int>(z));
            const auto* densities =
                grid.organic_density.data() + first_index;
            for (std::size_t x = 0U; x < 4U; ++x) {
                const auto density =
                    static_cast<float>(densities[x]);
                density_along_x[y][z] +=
                    density * weights[0].value[x];
                derivative_along_x[y][z] +=
                    density * weights[0].derivative[x];
            }
        }
    }

    // Je termine la convolution en deux passes 1D. Les mêmes 64 octets
    // alimentent tous les résultats, mais je ne recalcule plus les produits
    // Y/Z pour chaque échantillon individuel.
    for (std::size_t y = 0U; y < 4U; ++y) {
        auto density_along_xz = 0.0F;
        auto derivative_x_along_xz = 0.0F;
        auto derivative_z_along_xz = 0.0F;
        for (std::size_t z = 0U; z < 4U; ++z) {
            density_along_xz +=
                density_along_x[y][z] * weights[2].value[z];
            derivative_x_along_xz +=
                derivative_along_x[y][z] * weights[2].value[z];
            derivative_z_along_xz +=
                density_along_x[y][z] * weights[2].derivative[z];
        }
        result.density +=
            density_along_xz * weights[1].value[y];
        result.gradient[0] +=
            derivative_x_along_xz * weights[1].value[y];
        result.gradient[1] +=
            density_along_xz * weights[1].derivative[y];
        result.gradient[2] +=
            derivative_z_along_xz * weights[1].value[y];
    }
    return result;
}

[[nodiscard]] auto smoothed_outward_normal(
    const SmoothedFieldSample& field,
    const Float3& fallback) noexcept -> Float3 {
    const auto local_fallback = normalized_or(fallback, {0.0F, 1.0F, 0.0F});
    const Float3 outward {
        -field.gradient[0],
        -field.gradient[1],
        -field.gradient[2],
    };
    if (length_squared(outward) <= kNormalEpsilonSquared) {
        return local_fallback;
    }

    const auto smoothed = normalized_or(outward, local_fallback);
    // Dans un cas topologiquement ambigu (selle ou paroi d'une cellule), je
    // refuse qu'un voisin éloigné retourne l'éclairage de la surface locale.
    return dot(smoothed, local_fallback) > 0.25F
               ? smoothed
               : local_fallback;
}

[[nodiscard]] auto has_exposed_vertical_clearance(
    const CachedSampleGrid& grid,
    const BlockCoord& cell) noexcept -> bool {
    for (int z = 0; z <= 1; ++z) {
        for (int x = 0; x <= 1; ++x) {
            if (grid.density_at(
                    cell.x + x,
                    cell.y + 2,
                    cell.z + z) > 0.0F) {
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] auto binary_surface_sample(
    const std::array<bool, 8>& solid,
    const BlockCoord& cell,
    float maximum_displacement) noexcept -> BinarySurfaceSample {
    Float3 accumulated_position {0.0F, 0.0F, 0.0F};
    Float3 accumulated_outward {0.0F, 0.0F, 0.0F};
    Float3 first_outward {0.0F, 1.0F, 0.0F};
    auto solid_count = std::uint8_t {0U};
    auto intersection_count = std::size_t {0U};
    for (const auto is_solid : solid) {
        solid_count = static_cast<std::uint8_t>(
            solid_count + static_cast<std::uint8_t>(is_solid));
    }

    for (const auto& edge : kCellEdges) {
        const auto first_index = static_cast<std::size_t>(edge.first);
        const auto second_index = static_cast<std::size_t>(edge.second);
        if (solid[first_index] == solid[second_index]) {
            continue;
        }

        const auto& first_offset = kCornerOffsets[first_index];
        const auto& second_offset = kCornerOffsets[second_index];
        const Float3 first_position {
            static_cast<float>(cell.x + first_offset.x) + 0.5F,
            static_cast<float>(cell.y + first_offset.y) + 0.5F,
            static_cast<float>(cell.z + first_offset.z) + 0.5F,
        };
        const Float3 second_position {
            static_cast<float>(cell.x + second_offset.x) + 0.5F,
            static_cast<float>(cell.y + second_offset.y) + 0.5F,
            static_cast<float>(cell.z + second_offset.z) + 0.5F,
        };
        for (std::size_t axis = 0U; axis < 3U; ++axis) {
            accumulated_position[axis] +=
                (first_position[axis] + second_position[axis]) * 0.5F;
        }

        const auto outward = solid[first_index]
                                  ? subtract(second_position, first_position)
                                  : subtract(first_position, second_position);
        for (std::size_t axis = 0U; axis < 3U; ++axis) {
            accumulated_outward[axis] += outward[axis];
        }
        if (intersection_count == 0U) {
            first_outward = outward;
        }
        ++intersection_count;
    }

    if (intersection_count == 0U) {
        return {};
    }
    const auto inverse_count =
        1.0F / static_cast<float>(intersection_count);
    Float3 position {
        accumulated_position[0] * inverse_count,
        accumulated_position[1] * inverse_count,
        accumulated_position[2] * inverse_count,
    };
    const Float3 center {
        static_cast<float>(cell.x) + 1.0F,
        static_cast<float>(cell.y) + 1.0F,
        static_cast<float>(cell.z) + 1.0F,
    };
    for (std::size_t axis = 0U; axis < position.size(); ++axis) {
        position[axis] = std::clamp(
            position[axis],
            center[axis] - maximum_displacement,
            center[axis] + maximum_displacement);
    }
    return {
        true,
        solid_count,
        position,
        normalized_or(accumulated_outward, first_outward),
    };
}

[[nodiscard]] auto binary_surface_sample(
    const CachedSampleGrid& grid,
    const BlockCoord& cell,
    float maximum_displacement) noexcept -> BinarySurfaceSample {
    std::array<bool, 8> solid {};
    for (std::size_t corner = 0U; corner < kCornerOffsets.size(); ++corner) {
        const auto coordinate = add(cell, kCornerOffsets[corner]);
        solid[corner] =
            grid.density_at(coordinate.x, coordinate.y, coordinate.z) > 0.0F;
    }
    return binary_surface_sample(
        solid,
        cell,
        maximum_displacement);
}

[[nodiscard]] auto make_binary_surface_cache(
    const OrganicTerrainSection& section) -> BinarySurfaceCache {
    BinarySurfaceCache cache {};
    cache.min = {
        section.min.x - 2,
        section.min.y - 2,
        section.min.z - 2,
    };
    const BlockCoord max {
        section.max.x + 1,
        section.max.y + 1,
        section.max.z + 1,
    };
    cache.size_x = inclusive_extent(cache.min.x, max.x);
    cache.size_y = inclusive_extent(cache.min.y, max.y);
    cache.size_z = inclusive_extent(cache.min.z, max.z);
    const auto volume =
        checked_volume(cache.size_x, cache.size_y, cache.size_z);
    cache.samples.resize(volume);
    cache.initialized.assign(volume, 0U);
    return cache;
}

[[nodiscard]] auto cached_binary_surface_sample(
    const CachedSampleGrid& grid,
    BinarySurfaceCache& cache,
    const BlockCoord& cell,
    float maximum_displacement) noexcept
    -> const BinarySurfaceSample& {
    const auto index = cache.index_of(cell);
    if (cache.initialized[index] == 0U) {
        cache.samples[index] =
            binary_surface_sample(grid, cell, maximum_displacement);
        cache.initialized[index] = 1U;
    }
    return cache.samples[index];
}

[[nodiscard]] auto exposed_laplacian_correction(
    const CachedSampleGrid& grid,
    BinarySurfaceCache& cache,
    const BlockCoord& cell,
    const Float3& position,
    const Float3& local_outward,
    float maximum_displacement) noexcept -> Float3 {
    constexpr std::array<BlockCoord, 6> kNeighborOffsets {{
        {-1, 0, 0},
        {1, 0, 0},
        {0, -1, 0},
        {0, 1, 0},
        {0, 0, -1},
        {0, 0, 1},
    }};
    constexpr float kRelaxationStrength = 0.75F;
    constexpr float kMaximumNeighborDistanceSquared = 2.6F;

    Float3 accumulated_position {0.0F, 0.0F, 0.0F};
    auto neighbor_count = std::size_t {0U};
    for (const auto& offset : kNeighborOffsets) {
        const auto neighbor_cell = add(cell, offset);
        const auto& neighbor = cached_binary_surface_sample(
            grid,
            cache,
            neighbor_cell,
            maximum_displacement);
        if (!neighbor.active ||
            neighbor.outward[1] <= 0.05F ||
            dot(neighbor.outward, local_outward) <= 0.35F ||
            length_squared(subtract(neighbor.position, position)) >
                kMaximumNeighborDistanceSquared) {
            continue;
        }
        for (std::size_t axis = 0U; axis < 3U; ++axis) {
            accumulated_position[axis] += neighbor.position[axis];
        }
        ++neighbor_count;
    }
    if (neighbor_count < 2U) {
        return {};
    }

    const auto inverse_count =
        1.0F / static_cast<float>(neighbor_count);
    return {
        (accumulated_position[0] * inverse_count - position[0]) *
            kRelaxationStrength,
        (accumulated_position[1] * inverse_count - position[1]) *
            kRelaxationStrength,
        (accumulated_position[2] * inverse_count - position[2]) *
            kRelaxationStrength,
    };
}

[[nodiscard]] auto exposed_horizontal_ring_correction(
    const CachedSampleGrid& grid,
    BinarySurfaceCache& cache,
    const BlockCoord& cell,
    const Float3& position,
    const Float3& local_outward,
    float maximum_displacement) noexcept -> Float3 {
    constexpr std::array<BlockCoord, 8> kHorizontalRing {{
        {-1, 0, -1},
        {0, 0, -1},
        {1, 0, -1},
        {-1, 0, 0},
        {1, 0, 0},
        {-1, 0, 1},
        {0, 0, 1},
        {1, 0, 1},
    }};
    constexpr float kMaximumNeighborDistanceSquared = 3.6F;

    const auto horizontal_length_squared =
        local_outward[0] * local_outward[0] +
        local_outward[2] * local_outward[2];
    if (horizontal_length_squared <= 0.0625F) {
        return {};
    }
    const auto inverse_horizontal_length =
        1.0F / std::sqrt(horizontal_length_squared);
    const Float3 horizontal_outward {
        local_outward[0] * inverse_horizontal_length,
        0.0F,
        local_outward[2] * inverse_horizontal_length,
    };
    const Float3 horizontal_tangent {
        -horizontal_outward[2],
        0.0F,
        horizontal_outward[0],
    };
    std::array<bool, 2> has_side {};
    std::array<float, 2> best_radial_projection {{
        -std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity(),
    }};
    std::array<Float3, 2> side_position {};
    for (const auto& offset : kHorizontalRing) {
        const auto tangent_projection =
            static_cast<float>(offset.x) * horizontal_tangent[0] +
            static_cast<float>(offset.z) * horizontal_tangent[2];
        if (std::abs(tangent_projection) <= 0.35F) {
            continue;
        }
        const auto neighbor_cell = add(cell, offset);
        const auto& neighbor = cached_binary_surface_sample(
            grid,
            cache,
            neighbor_cell,
            maximum_displacement);
        if (!neighbor.active ||
            neighbor.outward[1] <= 0.05F ||
            dot(neighbor.outward, local_outward) <= 0.20F ||
            length_squared(subtract(neighbor.position, position)) >
                kMaximumNeighborDistanceSquared) {
            continue;
        }
        const auto side = tangent_projection < 0.0F ? 0U : 1U;
        const auto radial_projection =
            neighbor.position[0] * horizontal_outward[0] +
            neighbor.position[2] * horizontal_outward[2];
        if (!has_side[side] ||
            radial_projection > best_radial_projection[side]) {
            has_side[side] = true;
            best_radial_projection[side] = radial_projection;
            side_position[side] = neighbor.position;
        }
    }
    if (!has_side[0] || !has_side[1]) {
        return {};
    }

    const Float3 contour_center {
        (side_position[0][0] + side_position[1][0]) * 0.5F,
        (side_position[0][1] + side_position[1][1]) * 0.5F,
        (side_position[0][2] + side_position[1][2]) * 0.5F,
    };
    // Je moyenne les deux voisins situés le long de la tangente de la lèvre.
    // La recherche retient le candidat le plus extérieur de chaque côté :
    // contrairement à une moyenne isotrope, elle ne tire jamais la falaise
    // vers ses cellules intérieures.
    return {
        (contour_center[0] - position[0]) * 0.75F,
        0.0F,
        (contour_center[2] - position[2]) * 0.75F,
    };
}

[[nodiscard]] auto relaxed_exposed_position(
    const CachedSampleGrid& grid,
    BinarySurfaceCache& cache,
    const BlockCoord& cell,
    const Float3& original_position,
    const Float3& center,
    const SmoothedFieldSample& field,
    const Float3& local_outward,
    const Float3& smoothed_outward,
    std::uint8_t solid_count,
    std::uint8_t sky_light,
    bool has_vertical_clearance,
    float maximum_displacement,
    float maximum_relaxation) noexcept -> Float3 {
    if (maximum_relaxation <= 0.0F ||
        solid_count < 2U ||
        solid_count > 6U ||
        sky_light < 12U ||
        !has_vertical_clearance ||
        local_outward[1] <= 0.05F ||
        dot(smoothed_outward, local_outward) <= 0.25F) {
        return original_position;
    }

    // Je fais une seule étape de projection vers l'iso-surface B-spline. Elle
    // étale visuellement une marche sur ses cellules voisines, sans relire le
    // sampler ni ajouter une couche de halo.
    auto correction = exposed_laplacian_correction(
        grid,
        cache,
        cell,
        original_position,
        local_outward,
        maximum_displacement);
    const auto ring_correction = exposed_horizontal_ring_correction(
        grid,
        cache,
        cell,
        original_position,
        local_outward,
        maximum_displacement);
    correction[0] += ring_correction[0];
    correction[1] += ring_correction[1];
    correction[2] += ring_correction[2];
    const auto gradient_length_squared = length_squared(field.gradient);
    if (std::isfinite(field.density) &&
        std::isfinite(gradient_length_squared) &&
        gradient_length_squared > kNormalEpsilonSquared) {
        constexpr float kIsoSurfaceProjectionStrength = 2.0F;
        correction[0] -=
            kIsoSurfaceProjectionStrength *
            field.density * field.gradient[0] / gradient_length_squared;
        correction[1] -=
            kIsoSurfaceProjectionStrength *
            field.density * field.gradient[1] / gradient_length_squared;
        correction[2] -=
            kIsoSurfaceProjectionStrength *
            field.density * field.gradient[2] / gradient_length_squared;
    }
    const auto correction_length_squared = length_squared(correction);
    if (!std::isfinite(correction_length_squared)) {
        return original_position;
    }
    if (correction_length_squared >
        maximum_relaxation * maximum_relaxation) {
        const auto scale =
            maximum_relaxation / std::sqrt(correction_length_squared);
        correction[0] *= scale;
        correction[1] *= scale;
        correction[2] *= scale;
    }

    auto result = original_position;
    for (std::size_t axis = 0U; axis < result.size(); ++axis) {
        result[axis] = std::clamp(
            result[axis] + correction[axis],
            center[axis] - maximum_displacement,
            center[axis] + maximum_displacement);
    }
    return result;
}

[[nodiscard]] auto select_materials(const std::array<OrganicTerrainCellSample, 8>& samples)
    -> std::pair<MaterialVote, MaterialVote> {
    std::array<MaterialVote, 8> votes {};
    std::size_t vote_count = 0;

    for (const auto& sample : samples) {
        if (!is_organic_terrain_block(sample.block_id)) {
            continue;
        }

        auto vote_iterator = std::find_if(
            votes.begin(),
            votes.begin() + static_cast<std::ptrdiff_t>(vote_count),
            [&](const MaterialVote& vote) {
                return vote.block_id == sample.block_id;
            });
        if (vote_iterator == votes.begin() + static_cast<std::ptrdiff_t>(vote_count)) {
            if (vote_count < votes.size()) {
                votes[vote_count] = {sample.block_id, 1};
                ++vote_count;
            }
        } else {
            ++vote_iterator->count;
        }
    }

    const auto ordered_before = [](const MaterialVote& lhs,
                                   const MaterialVote& rhs) noexcept {
        if (lhs.count != rhs.count) {
            return lhs.count > rhs.count;
        }
        return lhs.block_id < rhs.block_id;
    };
    // Je trie seulement la plage réellement remplie. Cette insertion bornée
    // évite aussi le faux positif array-bounds de GCC sur std::sort(array).
    for (std::size_t index = 1U; index < vote_count; ++index) {
        const auto candidate = votes[index];
        auto insertion = index;
        while (insertion > 0U &&
               ordered_before(candidate, votes[insertion - 1U])) {
            votes[insertion] = votes[insertion - 1U];
            --insertion;
        }
        votes[insertion] = candidate;
    }

    const auto primary = vote_count > 0 ? votes[0] : MaterialVote {};
    const auto secondary = vote_count > 1 ? votes[1] : MaterialVote {};
    return {primary, secondary};
}

[[nodiscard]] constexpr auto is_mineral(BlockId block_id) noexcept -> bool {
    return block_id >= to_block_id(BlockType::CoalOre) &&
           block_id <= to_block_id(BlockType::MetallicAlloyOre);
}

[[nodiscard]] auto geological_material_pair(
    const std::array<OrganicTerrainCellSample, 8>& samples,
    const MaterialVote& primary) noexcept -> GeologicalMaterialPair {
    std::array<std::uint8_t, 40> counts {};
    for (const auto& sample : samples) {
        if (sample.block_id < counts.size() &&
            is_organic_terrain_block(sample.block_id)) {
            ++counts[sample.block_id];
        }
    }

    // Je conserve les familles qui portent réellement l'identité d'un biome,
    // mais je canonicalise le socle terre/herbe/pierre. Ainsi une falaise ne
    // change plus de paire de textures à chaque strate logique.
    auto mineral = to_block_id(BlockType::Air);
    auto mineral_votes = std::uint8_t {0U};
    for (auto block_id = to_block_id(BlockType::CoalOre);
         block_id <= to_block_id(BlockType::MetallicAlloyOre);
         ++block_id) {
        const auto votes = counts[block_id];
        if (votes > mineral_votes ||
            (votes == mineral_votes &&
             votes > 0U &&
             (mineral == to_block_id(BlockType::Air) ||
              block_id < mineral))) {
            mineral = block_id;
            mineral_votes = votes;
        }
    }

    auto surface = to_block_id(BlockType::Grass);
    if (mineral_votes > 0U) {
        // Je donne la priorité au filon dès qu'il contribue à la cellule duale.
        // Son blend borné le raccorde ensuite à la pierre sur plusieurs sommets
        // au lieu de le faire disparaître lorsqu'il est localement minoritaire.
        surface = mineral;
    } else if (primary.block_id == to_block_id(BlockType::Snow)) {
        surface = to_block_id(BlockType::Snow);
    } else if (primary.block_id == to_block_id(BlockType::Sand)) {
        surface = to_block_id(BlockType::Sand);
    } else if (primary.block_id == to_block_id(BlockType::Gravel)) {
        surface = to_block_id(BlockType::Gravel);
    } else if (primary.block_id == to_block_id(BlockType::MossyStone)) {
        surface = to_block_id(BlockType::MossyStone);
    } else if (is_mineral(primary.block_id)) {
        surface = primary.block_id;
    }

    const auto solid_votes = std::max<unsigned int>(
        1U,
        static_cast<unsigned int>(
            std::count_if(
                samples.begin(),
                samples.end(),
                [](const OrganicTerrainCellSample& sample) {
                    return is_organic_terrain_block(sample.block_id);
                })));
    const auto stone_votes =
        static_cast<unsigned int>(counts[to_block_id(BlockType::Stone)]);
    const auto dirt_votes =
        static_cast<unsigned int>(counts[to_block_id(BlockType::Dirt)]);
    auto source_bias =
        (static_cast<float>(stone_votes) * 0.18F +
         static_cast<float>(dirt_votes) * 0.08F) /
        static_cast<float>(solid_votes);
    if (is_mineral(surface)) {
        // Je garde les filons identifiables tout en les raccordant à la roche.
        source_bias = -0.22F;
    }
    return {
        surface,
        to_block_id(BlockType::Stone),
        source_bias,
    };
}

[[nodiscard]] constexpr auto smooth_unit(float value) noexcept -> float {
    const auto bounded = std::clamp(value, 0.0F, 1.0F);
    return bounded * bounded * (3.0F - 2.0F * bounded);
}

[[nodiscard]] constexpr auto floor_divide(int value, int divisor) noexcept -> int {
    const auto quotient = value / divisor;
    const auto remainder = value % divisor;
    return remainder < 0 ? quotient - 1 : quotient;
}

[[nodiscard]] constexpr auto positive_modulo(int value, int divisor) noexcept -> int {
    const auto remainder = value % divisor;
    return remainder < 0 ? remainder + divisor : remainder;
}

[[nodiscard]] constexpr auto geological_hash(int x, int z) noexcept -> std::uint32_t {
    auto hash = static_cast<std::uint32_t>(x) * 0x8DA6B343U;
    hash ^= static_cast<std::uint32_t>(z) * 0xD8163841U;
    hash ^= hash >> 13U;
    hash *= 0x85EBCA6BU;
    hash ^= hash >> 16U;
    return hash & 0xFFFFU;
}

[[nodiscard]] auto geological_macro_variation(const BlockCoord& cell) noexcept -> float {
    constexpr int kMacroPeriod = 12;
    const auto lattice_x = floor_divide(cell.x, kMacroPeriod);
    const auto lattice_z = floor_divide(cell.z, kMacroPeriod);
    const auto local_x =
        smooth_unit(
            static_cast<float>(positive_modulo(cell.x, kMacroPeriod)) /
            static_cast<float>(kMacroPeriod));
    const auto local_z =
        smooth_unit(
            static_cast<float>(positive_modulo(cell.z, kMacroPeriod)) /
            static_cast<float>(kMacroPeriod));
    const auto sample = [](int x, int z) noexcept {
        return static_cast<float>(geological_hash(x, z)) / 65535.0F;
    };
    const auto west = std::lerp(
        sample(lattice_x, lattice_z),
        sample(lattice_x, lattice_z + 1),
        local_z);
    const auto east = std::lerp(
        sample(lattice_x + 1, lattice_z),
        sample(lattice_x + 1, lattice_z + 1),
        local_z);
    return std::lerp(west, east, local_x) * 2.0F - 1.0F;
}

[[nodiscard]] auto geological_rock_blend(
    const Float3& normal,
    std::uint8_t sky_light,
    const BlockCoord& cell,
    float source_bias,
    bool mineral_surface) noexcept -> std::uint8_t {
    const auto upward = std::clamp(normal[1], 0.0F, 1.0F);
    const auto slope = 1.0F - upward;
    const auto cliff = smooth_unit((slope - 0.20F) / 0.62F);
    const auto shelter =
        1.0F - static_cast<float>(sky_light) / 15.0F;
    const auto macro = geological_macro_variation(cell);

    auto rock = 0.055F +
                cliff * 0.82F +
                shelter * 0.24F +
                macro * 0.075F +
                source_bias;
    if (mineral_surface) {
        rock = std::min(rock, 0.42F);
    }
    rock = std::clamp(rock, 0.0F, 1.0F);
    return static_cast<std::uint8_t>(
        std::lround(rock * 255.0F));
}

[[nodiscard]] auto build_active_vertex(const CachedSampleGrid& grid,
    BinarySurfaceCache& binary_cache,
                                       const BlockCoord& cell,
                                       float maximum_displacement,
                                       float maximum_relaxation) -> ActiveVertexBuild {
    std::array<OrganicTerrainCellSample, 8> samples {};
    std::uint8_t sky_light = 0;
    std::uint8_t block_light = 0;

    for (std::size_t corner_index = 0; corner_index < kCornerOffsets.size(); ++corner_index) {
        const auto corner = add(cell, kCornerOffsets[corner_index]);
        samples[corner_index] = grid.at(corner.x, corner.y, corner.z);
        sky_light = std::max(sky_light, samples[corner_index].sky_light);
        block_light = std::max(block_light, samples[corner_index].block_light);
    }

    const auto& binary_surface = cached_binary_surface_sample(
        grid,
        binary_cache,
        cell,
        maximum_displacement);
    const auto solid_count = binary_surface.solid_count;
    auto position = binary_surface.position;
    const Float3 center {
        static_cast<float>(cell.x) + 1.0F,
        static_cast<float>(cell.y) + 1.0F,
        static_cast<float>(cell.z) + 1.0F,
    };
    const auto unrelaxed_position = position;
    const auto local_outward = binary_surface.outward;
    const auto initial_smoothed_field =
        sample_smoothed_field(grid, cell, unrelaxed_position);
    const auto initial_normal =
        smoothed_outward_normal(
            initial_smoothed_field,
            local_outward);
    position = relaxed_exposed_position(
        grid,
        binary_cache,
        cell,
        unrelaxed_position,
        center,
        initial_smoothed_field,
        local_outward,
        initial_normal,
        solid_count,
        sky_light,
        has_exposed_vertical_clearance(grid, cell),
        maximum_displacement,
        maximum_relaxation);

    // La relaxation change la position géométrique. Je rééchantillonne donc le
    // champ au point final : conserver la normale de l'ancien point décalait
    // visuellement les reflets, l'éclairage et le mélange roche/terre.
    const auto final_smoothed_field =
        sample_smoothed_field(grid, cell, position);
    const auto normal =
        smoothed_outward_normal(
            final_smoothed_field,
            local_outward);
    const auto [primary, secondary] = select_materials(samples);
    static_cast<void>(secondary);
    const auto geological_pair =
        geological_material_pair(samples, primary);
    const auto material_blend = geological_rock_blend(
        normal,
        sky_light,
        cell,
        geological_pair.source_bias,
        is_mineral(geological_pair.surface));
    const auto occlusion_penalty = static_cast<unsigned int>(
        solid_count > 0 ? solid_count - 1U : 0U) * 15U;
    const auto ambient_occlusion = static_cast<std::uint8_t>(255U - std::min(occlusion_penalty, 255U));

    return {
        {
            position[0],
            position[1],
            position[2],
            normal[0],
            normal[1],
            normal[2],
            geological_pair.surface,
            geological_pair.rock,
            material_blend,
            ambient_occlusion,
            sky_light,
            block_light,
            kTerrainSurfaceFlagGeologicalBlend,
        },
        unrelaxed_position,
    };
}

[[nodiscard]] auto active_cells_for_surface(const BlockCoord& solid_cell,
                                            SurfaceDirection direction)
    -> std::array<BlockCoord, 4> {
    switch (direction) {
    case SurfaceDirection::PositiveX: {
        const auto x = solid_cell.x;
        return {{
            {x, solid_cell.y - 1, solid_cell.z - 1},
            {x, solid_cell.y, solid_cell.z - 1},
            {x, solid_cell.y, solid_cell.z},
            {x, solid_cell.y - 1, solid_cell.z},
        }};
    }
    case SurfaceDirection::NegativeX: {
        const auto x = solid_cell.x - 1;
        return {{
            {x, solid_cell.y - 1, solid_cell.z},
            {x, solid_cell.y, solid_cell.z},
            {x, solid_cell.y, solid_cell.z - 1},
            {x, solid_cell.y - 1, solid_cell.z - 1},
        }};
    }
    case SurfaceDirection::PositiveY: {
        const auto y = solid_cell.y;
        return {{
            {solid_cell.x - 1, y, solid_cell.z - 1},
            {solid_cell.x - 1, y, solid_cell.z},
            {solid_cell.x, y, solid_cell.z},
            {solid_cell.x, y, solid_cell.z - 1},
        }};
    }
    case SurfaceDirection::NegativeY: {
        const auto y = solid_cell.y - 1;
        return {{
            {solid_cell.x, y, solid_cell.z - 1},
            {solid_cell.x, y, solid_cell.z},
            {solid_cell.x - 1, y, solid_cell.z},
            {solid_cell.x - 1, y, solid_cell.z - 1},
        }};
    }
    case SurfaceDirection::PositiveZ: {
        const auto z = solid_cell.z;
        return {{
            {solid_cell.x - 1, solid_cell.y - 1, z},
            {solid_cell.x, solid_cell.y - 1, z},
            {solid_cell.x, solid_cell.y, z},
            {solid_cell.x - 1, solid_cell.y, z},
        }};
    }
    case SurfaceDirection::NegativeZ:
    default: {
        const auto z = solid_cell.z - 1;
        return {{
            {solid_cell.x - 1, solid_cell.y, z},
            {solid_cell.x, solid_cell.y, z},
            {solid_cell.x, solid_cell.y - 1, z},
            {solid_cell.x - 1, solid_cell.y - 1, z},
        }};
    }
    }
}

[[nodiscard]] auto neighbor_for_surface(const BlockCoord& cell,
                                        SurfaceDirection direction) noexcept -> BlockCoord {
    switch (direction) {
    case SurfaceDirection::PositiveX:
        return {cell.x + 1, cell.y, cell.z};
    case SurfaceDirection::NegativeX:
        return {cell.x - 1, cell.y, cell.z};
    case SurfaceDirection::PositiveY:
        return {cell.x, cell.y + 1, cell.z};
    case SurfaceDirection::NegativeY:
        return {cell.x, cell.y - 1, cell.z};
    case SurfaceDirection::PositiveZ:
        return {cell.x, cell.y, cell.z + 1};
    case SurfaceDirection::NegativeZ:
    default:
        return {cell.x, cell.y, cell.z - 1};
    }
}

[[nodiscard]] auto get_or_create_vertex(const CachedSampleGrid& grid,
                                        BinarySurfaceCache& binary_cache,
                                        ActiveCellCache& cache,
                                        OrganicTerrainMesh& mesh,
                                        std::vector<Float3>& unrelaxed_positions,
                                        std::vector<BlockCoord>& active_cells,
                                        const BlockCoord& active_cell,
                                        float maximum_displacement,
                                        float maximum_relaxation) -> std::uint32_t {
    auto& cached_index = cache.vertex_index(active_cell);
    if (cached_index != kMissingVertex) {
        return static_cast<std::uint32_t>(cached_index);
    }
    if (mesh.vertices.size() >= static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
        throw std::length_error("Le maillage organique dépasse la plage de ses indices");
    }

    const auto vertex_index = static_cast<std::uint32_t>(mesh.vertices.size());
    const auto built_vertex = build_active_vertex(
        grid,
        binary_cache,
        active_cell,
        maximum_displacement,
        maximum_relaxation);
    mesh.vertices.push_back(built_vertex.vertex);
    unrelaxed_positions.push_back(built_vertex.unrelaxed_position);
    active_cells.push_back(active_cell);
    cached_index = static_cast<std::int32_t>(vertex_index);
    return vertex_index;
}

void append_pending_quad(
    OrganicTerrainMesh& mesh,
    std::vector<PendingQuad>& pending_quads,
    const std::vector<Float3>& unrelaxed_positions,
    const std::array<std::uint32_t, 4>& quad) {
    const auto& v0 = unrelaxed_positions[quad[0]];
    const auto& v1 = unrelaxed_positions[quad[1]];
    const auto& v2 = unrelaxed_positions[quad[2]];
    const auto& v3 = unrelaxed_positions[quad[3]];
    const auto quality_diagonal_02 = std::min(
        triangle_area_squared(v0, v1, v2),
        triangle_area_squared(v0, v2, v3));
    const auto quality_diagonal_13 = std::min(
        triangle_area_squared(v0, v1, v3),
        triangle_area_squared(v1, v2, v3));

    // Je choisis la diagonale sur les positions Surface Nets non relaxées. Le
    // lissage visuel ne peut donc jamais changer la topologie ni les indices.
    if (quality_diagonal_02 >= quality_diagonal_13 &&
        quality_diagonal_02 > kTriangleEpsilonSquared) {
        pending_quads.push_back({quad, quad[2]});
    } else if (quality_diagonal_13 > kTriangleEpsilonSquared) {
        pending_quads.push_back({quad, quad[3]});
    } else {
        return;
    }
    ++mesh.quad_count;
}

[[nodiscard]] auto edge_key(std::uint32_t first,
                            std::uint32_t second) noexcept -> EdgeKey {
    return first < second
               ? EdgeKey {first, second}
               : EdgeKey {second, first};
}

[[nodiscard]] auto position_of(const TerrainVertex& vertex) noexcept -> Float3 {
    return {vertex.x, vertex.y, vertex.z};
}

[[nodiscard]] auto block_coord_before(const BlockCoord& lhs,
                                      const BlockCoord& rhs) noexcept -> bool {
    if (lhs.x != rhs.x) {
        return lhs.x < rhs.x;
    }
    if (lhs.y != rhs.y) {
        return lhs.y < rhs.y;
    }
    return lhs.z < rhs.z;
}

[[nodiscard]] auto canonical_cell(const BlockCoord& first,
                                  const BlockCoord& second) noexcept
    -> BlockCoord {
    return block_coord_before(first, second) ? first : second;
}

[[nodiscard]] auto refinement_vertex_is_eligible(
    const CachedSampleGrid& grid,
    BinarySurfaceCache& binary_cache,
    const std::vector<BlockCoord>& active_cells,
    const OrganicTerrainMesh& mesh,
    std::uint32_t index,
    float maximum_displacement) noexcept -> bool {
    const auto& vertex = mesh.vertices[index];
    if (vertex.sky_light < 12U ||
        vertex.ny <= 0.08F ||
        vertex.ny >= 0.90F) {
        return false;
    }
    const auto horizontal_length_squared =
        vertex.nx * vertex.nx + vertex.nz * vertex.nz;
    if (horizontal_length_squared <= 0.20F) {
        return false;
    }

    const auto& cell = active_cells[index];
    const auto& binary = cached_binary_surface_sample(
        grid,
        binary_cache,
        cell,
        maximum_displacement);
    return binary.active &&
           binary.solid_count >= 2U &&
           binary.solid_count <= 6U &&
           has_exposed_vertical_clearance(grid, cell);
}

[[nodiscard]] auto should_refine_lip_edge(
    const std::vector<std::uint8_t>& eligible_vertices,
    const std::vector<Float3>& unrelaxed_positions,
    const OrganicTerrainMesh& mesh,
    std::uint32_t first,
    std::uint32_t second) noexcept -> bool {
    if (eligible_vertices[first] == 0U ||
        eligible_vertices[second] == 0U) {
        return false;
    }

    const auto edge = subtract(
        unrelaxed_positions[second],
        unrelaxed_positions[first]);
    const auto edge_length_squared = length_squared(edge);
    if (edge_length_squared <= 0.30F) {
        return false;
    }
    const auto inverse_edge_length = 1.0F / std::sqrt(edge_length_squared);
    const Float3 direction {
        edge[0] * inverse_edge_length,
        edge[1] * inverse_edge_length,
        edge[2] * inverse_edge_length,
    };
    if (std::abs(direction[1]) >= 0.45F) {
        return false;
    }

    const auto& first_vertex = mesh.vertices[first];
    const auto& second_vertex = mesh.vertices[second];
    auto mean_normal = normalized_or(
        {
            first_vertex.nx + second_vertex.nx,
            first_vertex.ny + second_vertex.ny,
            first_vertex.nz + second_vertex.nz,
        },
        {0.0F, 1.0F, 0.0F});
    const auto horizontal_length = std::sqrt(
        mean_normal[0] * mean_normal[0] +
        mean_normal[2] * mean_normal[2]);
    if (horizontal_length <= 0.48F ||
        mean_normal[1] <= 0.18F ||
        mean_normal[1] >= 0.88F) {
        return false;
    }
    mean_normal[0] /= horizontal_length;
    mean_normal[2] /= horizontal_length;
    const auto contour_alignment = std::abs(
        direction[0] * mean_normal[0] +
        direction[2] * mean_normal[2]);
    return contour_alignment < 0.45F;
}

[[nodiscard]] auto interpolated_vertex(const TerrainVertex& first,
                                       const TerrainVertex& second) noexcept
    -> TerrainVertex {
    const auto rounded_average = [](std::uint8_t lhs,
                                    std::uint8_t rhs) noexcept {
        return static_cast<std::uint8_t>(
            (static_cast<unsigned int>(lhs) +
             static_cast<unsigned int>(rhs) +
             1U) /
            2U);
    };
    return {
        (first.x + second.x) * 0.5F,
        (first.y + second.y) * 0.5F,
        (first.z + second.z) * 0.5F,
        (first.nx + second.nx) * 0.5F,
        (first.ny + second.ny) * 0.5F,
        (first.nz + second.nz) * 0.5F,
        first.primary_block_id,
        first.secondary_block_id,
        rounded_average(first.material_blend, second.material_blend),
        rounded_average(first.ambient_occlusion, second.ambient_occlusion),
        rounded_average(first.sky_light, second.sky_light),
        rounded_average(first.block_light, second.block_light),
        static_cast<std::uint16_t>(
            first.surface_flags | second.surface_flags),
    };
}

void project_refined_vertex(const CachedSampleGrid& grid,
                            const BlockCoord& field_cell,
                            const Float3& logical_anchor,
                            float maximum_projection,
                            TerrainVertex& vertex) noexcept {
    const auto starting_position = position_of(vertex);
    const auto field =
        sample_smoothed_field(grid, field_cell, starting_position);
    const auto gradient_length_squared = length_squared(field.gradient);
    if (std::isfinite(field.density) &&
        std::isfinite(gradient_length_squared) &&
        gradient_length_squared > kNormalEpsilonSquared) {
        auto correction = Float3 {
            -field.density * field.gradient[0] / gradient_length_squared,
            -field.density * field.gradient[1] / gradient_length_squared,
            -field.density * field.gradient[2] / gradient_length_squared,
        };
        const auto correction_length_squared = length_squared(correction);
        if (std::isfinite(correction_length_squared) &&
            correction_length_squared >
                maximum_projection * maximum_projection) {
            const auto scale =
                maximum_projection / std::sqrt(correction_length_squared);
            correction[0] *= scale;
            correction[1] *= scale;
            correction[2] *= scale;
        }
        vertex.x += correction[0];
        vertex.y += correction[1];
        vertex.z += correction[2];
    }

    constexpr float kLogicalEnvelope = 0.459F;
    vertex.x = std::clamp(
        vertex.x,
        logical_anchor[0] - kLogicalEnvelope,
        logical_anchor[0] + kLogicalEnvelope);
    vertex.y = std::clamp(
        vertex.y,
        logical_anchor[1] - kLogicalEnvelope,
        logical_anchor[1] + kLogicalEnvelope);
    vertex.z = std::clamp(
        vertex.z,
        logical_anchor[2] - kLogicalEnvelope,
        logical_anchor[2] + kLogicalEnvelope);

    const auto projected_field = sample_smoothed_field(
        grid,
        field_cell,
        position_of(vertex));
    const auto normal = smoothed_outward_normal(
        projected_field,
        {vertex.nx, vertex.ny, vertex.nz});
    vertex.nx = normal[0];
    vertex.ny = normal[1];
    vertex.nz = normal[2];
}

[[nodiscard]] auto append_edge_midpoint(
    const CachedSampleGrid& grid,
    OrganicTerrainMesh& mesh,
    const std::vector<Float3>& unrelaxed_positions,
    const std::vector<BlockCoord>& active_cells,
    std::uint32_t& cached_midpoint,
    const EdgeKey& edge) -> std::uint32_t {
    if (cached_midpoint != kMissingRefinementVertex) {
        return cached_midpoint;
    }
    if (mesh.vertices.size() >=
        static_cast<std::size_t>(
            std::numeric_limits<std::uint32_t>::max())) {
        throw std::length_error(
            "Le raffinement organique dépasse la plage de ses indices");
    }

    auto midpoint = interpolated_vertex(
        mesh.vertices[edge.first],
        mesh.vertices[edge.second]);
    const Float3 logical_anchor {
        (unrelaxed_positions[edge.first][0] +
         unrelaxed_positions[edge.second][0]) *
            0.5F,
        (unrelaxed_positions[edge.first][1] +
         unrelaxed_positions[edge.second][1]) *
            0.5F,
        (unrelaxed_positions[edge.first][2] +
         unrelaxed_positions[edge.second][2]) *
            0.5F,
    };
    project_refined_vertex(
        grid,
        canonical_cell(
            active_cells[edge.first],
            active_cells[edge.second]),
        logical_anchor,
        0.16F,
        midpoint);

    const auto index = static_cast<std::uint32_t>(mesh.vertices.size());
    mesh.vertices.push_back(midpoint);
    cached_midpoint = index;
    return index;
}

[[nodiscard]] auto append_quad_center(
    const CachedSampleGrid& grid,
    OrganicTerrainMesh& mesh,
    const std::vector<Float3>& unrelaxed_positions,
    const std::vector<BlockCoord>& active_cells,
    const PendingQuad& pending) -> std::uint32_t {
    if (mesh.vertices.size() >=
        static_cast<std::size_t>(
            std::numeric_limits<std::uint32_t>::max())) {
        throw std::length_error(
            "Le raffinement organique dépasse la plage de ses indices");
    }
    auto center = mesh.vertices[pending.material_owner];
    Float3 logical_anchor {};
    Float3 visual_position {};
    auto field_cell = active_cells[pending.vertices[0]];
    for (const auto index : pending.vertices) {
        const auto position = position_of(mesh.vertices[index]);
        for (std::size_t axis = 0U; axis < 3U; ++axis) {
            visual_position[axis] += position[axis] * 0.25F;
            logical_anchor[axis] +=
                unrelaxed_positions[index][axis] * 0.25F;
        }
        field_cell = canonical_cell(field_cell, active_cells[index]);
    }
    center.x = visual_position[0];
    center.y = visual_position[1];
    center.z = visual_position[2];
    project_refined_vertex(
        grid,
        field_cell,
        logical_anchor,
        0.10F,
        center);

    const auto index = static_cast<std::uint32_t>(mesh.vertices.size());
    mesh.vertices.push_back(center);
    return index;
}

void append_unrefined_quad(OrganicTerrainMesh& mesh,
                           const PendingQuad& pending) {
    const auto& quad = pending.vertices;
    if (pending.material_owner == quad[2]) {
        mesh.indices.insert(mesh.indices.end(), {
            quad[0], quad[1], quad[2],
            quad[3], quad[0], quad[2],
        });
    } else {
        mesh.indices.insert(mesh.indices.end(), {
            quad[0], quad[1], quad[3],
            quad[1], quad[2], quad[3],
        });
    }
}

void finalize_pending_quads(
    const CachedSampleGrid& grid,
    BinarySurfaceCache& binary_cache,
    OrganicTerrainMesh& mesh,
    const std::vector<Float3>& unrelaxed_positions,
    const std::vector<BlockCoord>& active_cells,
    const std::vector<PendingQuad>& pending_quads,
    float maximum_displacement,
    bool adaptive_refinement) {
    auto split_edges = std::vector<EdgeKey> {};
    if (adaptive_refinement) {
        const auto original_vertex_count = mesh.vertices.size();
        auto eligible_vertices =
            std::vector<std::uint8_t>(original_vertex_count, 0U);
        for (std::size_t index = 0U;
             index < original_vertex_count;
             ++index) {
            eligible_vertices[index] =
                refinement_vertex_is_eligible(
                    grid,
                    binary_cache,
                    active_cells,
                    mesh,
                    static_cast<std::uint32_t>(index),
                    maximum_displacement)
                    ? 1U
                    : 0U;
        }

        for (const auto& pending : pending_quads) {
            for (std::size_t edge = 0U; edge < 4U; ++edge) {
                const auto key = edge_key(
                    pending.vertices[edge],
                    pending.vertices[(edge + 1U) % 4U]);
                if (should_refine_lip_edge(
                        eligible_vertices,
                        unrelaxed_positions,
                        mesh,
                        key.first,
                        key.second)) {
                    split_edges.push_back(key);
                }
            }
        }
    }
    std::sort(split_edges.begin(), split_edges.end());
    split_edges.erase(
        std::unique(split_edges.begin(), split_edges.end()),
        split_edges.end());

    auto midpoint_indices = std::vector<std::uint32_t>(
        split_edges.size(),
        kMissingRefinementVertex);
    for (const auto& pending : pending_quads) {
        std::array<bool, 4> split {};
        std::array<std::size_t, 4> split_indices {};
        auto split_count = std::size_t {0U};
        for (std::size_t edge = 0U; edge < 4U; ++edge) {
            const auto key = edge_key(
                pending.vertices[edge],
                pending.vertices[(edge + 1U) % 4U]);
            const auto found =
                std::lower_bound(
                    split_edges.begin(),
                    split_edges.end(),
                    key);
            split[edge] =
                found != split_edges.end() && *found == key;
            split_indices[edge] = static_cast<std::size_t>(
                found - split_edges.begin());
            split_count += static_cast<std::size_t>(split[edge]);
        }
        if (split_count == 0U) {
            append_unrefined_quad(mesh, pending);
            continue;
        }

        const auto center = append_quad_center(
            grid,
            mesh,
            unrelaxed_positions,
            active_cells,
            pending);
        for (std::size_t edge = 0U; edge < 4U; ++edge) {
            const auto first = pending.vertices[edge];
            const auto second =
                pending.vertices[(edge + 1U) % 4U];
            if (split[edge]) {
                const auto midpoint = append_edge_midpoint(
                    grid,
                    mesh,
                    unrelaxed_positions,
                    active_cells,
                    midpoint_indices[split_indices[edge]],
                    edge_key(first, second));
                mesh.indices.insert(mesh.indices.end(), {
                    first, midpoint, center,
                    midpoint, second, center,
                });
            } else {
                mesh.indices.insert(mesh.indices.end(), {
                    first, second, center,
                });
            }
        }
    }
}

} // namespace

auto OrganicTerrainSection::valid() const noexcept -> bool {
    return min.x <= max.x &&
           min.y <= max.y &&
           min.z <= max.z &&
           min.x >= std::numeric_limits<int>::min() + kNormalSampleHalo &&
           min.y >= std::numeric_limits<int>::min() + kNormalSampleHalo &&
           min.z >= std::numeric_limits<int>::min() + kNormalSampleHalo &&
           max.x <= std::numeric_limits<int>::max() - kNormalSampleHalo &&
           max.y <= std::numeric_limits<int>::max() - kNormalSampleHalo &&
           max.z <= std::numeric_limits<int>::max() - kNormalSampleHalo;
}

auto OrganicTerrainSection::contains(const BlockCoord& coord) const noexcept -> bool {
    return valid() &&
           coord.x >= min.x && coord.x <= max.x &&
           coord.y >= min.y && coord.y <= max.y &&
           coord.z >= min.z && coord.z <= max.z;
}

OrganicTerrainMesher::OrganicTerrainMesher(OrganicTerrainMesherSettings settings) noexcept
    : settings_(settings) {
    if (!std::isfinite(settings_.maximum_vertex_displacement)) {
        settings_.maximum_vertex_displacement = OrganicTerrainMesherSettings {}.maximum_vertex_displacement;
    }
    settings_.maximum_vertex_displacement = std::clamp(
        settings_.maximum_vertex_displacement,
        0.0F,
        kMaximumSafeDisplacement);
    if (!std::isfinite(settings_.exposed_surface_relaxation)) {
        settings_.exposed_surface_relaxation =
            OrganicTerrainMesherSettings {}.exposed_surface_relaxation;
    }
    settings_.exposed_surface_relaxation = std::clamp(
        settings_.exposed_surface_relaxation,
        0.0F,
        settings_.maximum_vertex_displacement);
}

auto OrganicTerrainMesher::build_mesh(const OrganicTerrainSection& section,
                                      const OrganicTerrainSampler& sampler,
                                      std::size_t vertex_reserve_hint,
                                      std::size_t index_reserve_hint) const -> OrganicTerrainMesh {
    OrganicTerrainMesh mesh {};
    if (!section.valid() || !sampler) {
        return mesh;
    }

    const auto grid = sample_grid(section, sampler);
    auto active_cell_cache = make_active_cell_cache(section);
    auto binary_surface_cache = make_binary_surface_cache(section);
    auto unrelaxed_positions = std::vector<Float3> {};
    auto vertex_active_cells = std::vector<BlockCoord> {};
    auto pending_quads = std::vector<PendingQuad> {};
    mesh.vertices.reserve(vertex_reserve_hint > 0U ? vertex_reserve_hint : 1024U);
    mesh.indices.reserve(index_reserve_hint > 0U ? index_reserve_hint : 1536U);
    unrelaxed_positions.reserve(
        vertex_reserve_hint > 0U ? vertex_reserve_hint : 1024U);
    vertex_active_cells.reserve(
        vertex_reserve_hint > 0U ? vertex_reserve_hint : 1024U);
    pending_quads.reserve(
        index_reserve_hint > 0U ? index_reserve_hint / 6U : 256U);

    for (int y = section.min.y; y <= section.max.y; ++y) {
        for (int z = section.min.z; z <= section.max.z; ++z) {
            for (int x = section.min.x; x <= section.max.x; ++x) {
                const BlockCoord cell {x, y, z};
                if (grid.density_at(x, y, z) <= 0.0F) {
                    continue;
                }

                for (const auto direction : kSurfaceDirections) {
                    const auto neighbor = neighbor_for_surface(cell, direction);
                    if (grid.density_at(
                            neighbor.x,
                            neighbor.y,
                            neighbor.z) > 0.0F) {
                        continue;
                    }

                    const auto surface_active_cells =
                        active_cells_for_surface(cell, direction);
                    std::array<std::uint32_t, 4> quad {};
                    for (std::size_t index = 0;
                         index < surface_active_cells.size();
                         ++index) {
                        quad[index] = get_or_create_vertex(
                            grid,
                            binary_surface_cache,
                            active_cell_cache,
                            mesh,
                            unrelaxed_positions,
                            vertex_active_cells,
                            surface_active_cells[index],
                            settings_.maximum_vertex_displacement,
                            settings_.exposed_surface_relaxation);
                    }
                    append_pending_quad(
                        mesh,
                        pending_quads,
                        unrelaxed_positions,
                        quad);
                }
            }
        }
    }

    finalize_pending_quads(
        grid,
        binary_surface_cache,
        mesh,
        unrelaxed_positions,
        vertex_active_cells,
        pending_quads,
        settings_.maximum_vertex_displacement,
        settings_.adaptive_lip_refinement);
    return mesh;
}

auto OrganicTerrainMesher::settings() const noexcept -> const OrganicTerrainMesherSettings& {
    return settings_;
}

} // namespace valcraft
