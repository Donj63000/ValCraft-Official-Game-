#include "render/SeaHorizon.h"

#include "world/World.h"

#include <glm/geometric.hpp>
#include <glm/vec2.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace valcraft {
namespace {

constexpr int kTerrainGridStep = 8;
constexpr int kTerrainVisibleOuterRadius = 512;
constexpr int kTerrainGeometryOuterRadius =
    kTerrainVisibleOuterRadius +
    static_cast<int>(kSeaHorizonGridStep);
constexpr int kTerrainGridRadius =
    kTerrainGeometryOuterRadius / kTerrainGridStep;
constexpr int kTerrainGridSide = kTerrainGridRadius * 2 + 1;
constexpr int kTerrainSampleBorder = 1;
constexpr int kTerrainSampleSide =
    kTerrainGridSide + kTerrainSampleBorder * 2;
constexpr std::size_t kTerrainGridSampleCount =
    static_cast<std::size_t>(kTerrainGridSide) *
    static_cast<std::size_t>(kTerrainGridSide);
constexpr std::size_t kTerrainExtendedSampleCount =
    static_cast<std::size_t>(kTerrainSampleSide) *
    static_cast<std::size_t>(kTerrainSampleSide);
constexpr std::uint32_t kInvalidVertexIndex =
    std::numeric_limits<std::uint32_t>::max();
constexpr int kSafeCoordinateMargin =
    kTerrainGridRadius * kTerrainGridStep +
    kTerrainSampleBorder * kTerrainGridStep;
constexpr int kSafeCenterMinimum =
    std::numeric_limits<int>::min() + kSafeCoordinateMargin;
constexpr int kSafeCenterMaximum =
    ((std::numeric_limits<int>::max() - kSafeCoordinateMargin) /
     kTerrainGridStep) *
    kTerrainGridStep;
constexpr float kPredictiveShipProximity = 64.0F;
constexpr float kPredictiveFocusDistance = 16.0F;
constexpr float kPredictiveMinimumSpeed = 0.25F;
constexpr float kPredictiveFullSpeed = 1.0F;
static_assert(
    static_cast<float>(kTerrainGridStep) ==
    kSeaHorizonTerrainSampleStep);
static_assert(
    static_cast<float>(kTerrainVisibleOuterRadius) ==
    kSeaHorizonTerrainOuterRadius);
static_assert(
    static_cast<float>(kTerrainGeometryOuterRadius) ==
    kSeaHorizonTerrainOuterRadius + kSeaHorizonGridStep);
static_assert(
    kTerrainGridSampleCount <= kSeaHorizonMaxTerrainVertices);
static_assert(
    static_cast<std::size_t>(
        (kTerrainGridSide - 1) *
        (kTerrainGridSide - 1) *
        2) <=
    kSeaHorizonMaxTerrainTriangles);

struct TerrainGridSample {
    TerrainSurfaceSample surface {};
    SeaHorizonTerrainVertex vertex {};
};

[[nodiscard]] auto grid_index(int x, int z) noexcept -> std::size_t {
    return static_cast<std::size_t>(z) *
               static_cast<std::size_t>(kTerrainGridSide) +
           static_cast<std::size_t>(x);
}

[[nodiscard]] auto extended_grid_index(int x, int z) noexcept
    -> std::size_t {
    return static_cast<std::size_t>(z) *
               static_cast<std::size_t>(kTerrainSampleSide) +
           static_cast<std::size_t>(x);
}

[[nodiscard]] auto snap_axis(float coordinate) noexcept -> int {
    if (!std::isfinite(coordinate)) {
        return 0;
    }

    const auto snapped = std::floor(
        static_cast<double>(coordinate) /
            static_cast<double>(kSeaHorizonGridStep) +
        0.5) *
        static_cast<double>(kSeaHorizonGridStep);
    const auto bounded = std::clamp(
        snapped,
        static_cast<double>(kSafeCenterMinimum),
        static_cast<double>(kSafeCenterMaximum));
    return static_cast<int>(bounded);
}

[[nodiscard]] auto finite_or_zero(float value) noexcept -> float {
    return std::isfinite(value) ? value : 0.0F;
}

[[nodiscard]] auto finite_vec3_or_zero(const glm::vec3& value) noexcept
    -> glm::vec3 {
    return {
        finite_or_zero(value.x),
        finite_or_zero(value.y),
        finite_or_zero(value.z),
    };
}

[[nodiscard]] auto is_vertex_in_terrain_annulus(
    int local_x,
    int local_z) noexcept -> bool {
    // Je conserve le bord extérieur carré de la grille : même avec
    // l'hystérésis, ses quatre côtés restent au-delà du brouillard terminal.
    // Le trou proche est découpé dans le shader et suit exactement la caméra.
    return local_x >= -kTerrainGeometryOuterRadius &&
           local_x <= kTerrainGeometryOuterRadius &&
           local_z >= -kTerrainGeometryOuterRadius &&
           local_z <= kTerrainGeometryOuterRadius;
}

[[nodiscard]] auto normalized_terrain_normal(
    int left_height,
    int right_height,
    int back_height,
    int front_height) noexcept -> glm::vec3 {
    const glm::vec3 unnormalized {
        static_cast<float>(left_height - right_height),
        kSeaHorizonTerrainSampleStep * 2.0F,
        static_cast<float>(back_height - front_height),
    };
    const auto length_squared = glm::dot(unnormalized, unnormalized);
    if (!std::isfinite(length_squared) ||
        length_squared <= std::numeric_limits<float>::epsilon()) {
        return {0.0F, 1.0F, 0.0F};
    }
    return unnormalized / std::sqrt(length_squared);
}

[[nodiscard]] auto make_terrain_vertex(
    int world_x,
    int world_z,
    const TerrainSurfaceSample& surface,
    const TerrainSurfaceSample& left,
    const TerrainSurfaceSample& right,
    const TerrainSurfaceSample& back,
    const TerrainSurfaceSample& front) noexcept
    -> SeaHorizonTerrainVertex {
    const auto normal = normalized_terrain_normal(
        left.surface_height,
        right.surface_height,
        back.surface_height,
        front.surface_height);

    return {
        static_cast<float>(world_x),
        static_cast<float>(surface.surface_height + 1) +
            kSeaHorizonTerrainHeightBias,
        static_cast<float>(world_z),
        pack_sea_horizon_snorm(normal.x),
        pack_sea_horizon_snorm(normal.y),
        pack_sea_horizon_snorm(normal.z),
        surface.surface_block,
    };
}

[[nodiscard]] auto append_terrain_vertex(
    SeaHorizonTerrainMesh& mesh,
    std::span<std::uint32_t> remap,
    std::span<const TerrainGridSample> samples,
    std::size_t sample_index) -> std::uint32_t {
    auto& mapped_index = remap[sample_index];
    if (mapped_index != kInvalidVertexIndex) {
        return mapped_index;
    }

    mapped_index = static_cast<std::uint32_t>(mesh.vertices.size());
    mesh.vertices.push_back(samples[sample_index].vertex);
    return mapped_index;
}

void append_terrain_triangle(
    SeaHorizonTerrainMesh& mesh,
    std::span<std::uint32_t> remap,
    std::span<const TerrainGridSample> samples,
    const std::array<std::size_t, 3>& triangle_samples,
    const std::array<std::array<int, 2>, 3>& local_positions) {
    for (const auto& position : local_positions) {
        if (!is_vertex_in_terrain_annulus(position[0], position[1])) {
            return;
        }
    }

    const auto entirely_submerged =
        std::all_of(
            triangle_samples.begin(),
            triangle_samples.end(),
            [&samples](std::size_t sample_index) noexcept {
                const auto& surface =
                    samples[sample_index].surface;
                return surface.water_level >
                       surface.surface_height;
            });
    if (entirely_submerged) {
        // Je réserve ce proxy aux silhouettes émergées. Le fond marin détaillé
        // se termine dans sa brume analytique au lieu de devenir une plaque.
        return;
    }

    for (const auto sample_index : triangle_samples) {
        mesh.indices.push_back(append_terrain_vertex(
            mesh,
            remap,
            samples,
            sample_index));
    }
}

} // namespace

auto sea_horizon_snapped_center(
    const glm::vec3& camera_position) noexcept -> SeaHorizonSnappedCenter {
    return {
        snap_axis(camera_position.x),
        snap_axis(camera_position.z),
    };
}

auto sea_horizon_stable_center(
    const SeaHorizonSnappedCenter& current_center,
    const glm::vec3& camera_position) noexcept -> SeaHorizonSnappedCenter {
    if (!std::isfinite(camera_position.x) ||
        !std::isfinite(camera_position.z)) {
        return current_center;
    }

    // Je conserve le maillage dans une zone plus large que la demi-cellule :
    // le roulis du navire ne peut donc pas provoquer deux reconstructions GPU
    // alternées lorsque la caméra oscille exactement sur une frontière.
    if (std::abs(
            camera_position.x -
            static_cast<float>(current_center.x)) <=
            kSeaHorizonCacheHysteresis &&
        std::abs(
            camera_position.z -
            static_cast<float>(current_center.z)) <=
            kSeaHorizonCacheHysteresis) {
        return current_center;
    }

    return sea_horizon_snapped_center(
        camera_position);
}

auto build_sea_horizon_terrain_mesh(
    const World& world,
    const glm::vec3& camera_position) -> SeaHorizonTerrainMesh {
    SeaHorizonTerrainMesh mesh {};
    mesh.snapped_center =
        sea_horizon_snapped_center(camera_position);
    if (world.generation_profile() !=
        WorldGenerationProfile::OceanAdventure) {
        return mesh;
    }

    // Je prélève une seule bordure supplémentaire pour calculer toutes les
    // normales, sans multiplier les appels de bruit pour chaque sommet.
    // Je place ces grilles raffinées sur le tas : la pile Windows reste
    // largement sous sa limite même avec les échantillons tous les huit mètres.
    std::vector<TerrainSurfaceSample> extended_samples(
        kTerrainExtendedSampleCount);
    for (int sample_z = 0;
         sample_z < kTerrainSampleSide;
         ++sample_z) {
        const auto local_z =
            (sample_z -
             kTerrainGridRadius -
             kTerrainSampleBorder) *
            kTerrainGridStep;
        const auto world_z = mesh.snapped_center.z + local_z;
        for (int sample_x = 0;
             sample_x < kTerrainSampleSide;
             ++sample_x) {
            const auto local_x =
                (sample_x -
                 kTerrainGridRadius -
                 kTerrainSampleBorder) *
                kTerrainGridStep;
            const auto world_x =
                mesh.snapped_center.x + local_x;
            extended_samples[
                extended_grid_index(sample_x, sample_z)] =
                world.sample_generated_surface(world_x, world_z);
        }
    }

    std::vector<TerrainGridSample> samples(
        kTerrainGridSampleCount);
    for (int grid_z = 0; grid_z < kTerrainGridSide; ++grid_z) {
        const auto local_z =
            (grid_z - kTerrainGridRadius) * kTerrainGridStep;
        const auto world_z = mesh.snapped_center.z + local_z;
        for (int grid_x = 0; grid_x < kTerrainGridSide; ++grid_x) {
            const auto local_x =
                (grid_x - kTerrainGridRadius) * kTerrainGridStep;
            const auto world_x = mesh.snapped_center.x + local_x;
            const auto sample_x = grid_x + kTerrainSampleBorder;
            const auto sample_z = grid_z + kTerrainSampleBorder;
            auto& sample = samples[grid_index(grid_x, grid_z)];
            sample.surface =
                extended_samples[
                    extended_grid_index(sample_x, sample_z)];
            sample.vertex = make_terrain_vertex(
                world_x,
                world_z,
                sample.surface,
                extended_samples[
                    extended_grid_index(sample_x - 1, sample_z)],
                extended_samples[
                    extended_grid_index(sample_x + 1, sample_z)],
                extended_samples[
                    extended_grid_index(sample_x, sample_z - 1)],
                extended_samples[
                    extended_grid_index(sample_x, sample_z + 1)]);
        }
    }

    mesh.vertices.reserve(kSeaHorizonMaxTerrainVertices);
    mesh.indices.reserve(kSeaHorizonMaxTerrainTriangles * 3U);
    std::vector<std::uint32_t> remap(
        kTerrainGridSampleCount,
        kInvalidVertexIndex);

    for (int grid_z = 0; grid_z < kTerrainGridSide - 1; ++grid_z) {
        const auto local_z0 =
            (grid_z - kTerrainGridRadius) * kTerrainGridStep;
        const auto local_z1 = local_z0 + kTerrainGridStep;
        for (int grid_x = 0; grid_x < kTerrainGridSide - 1; ++grid_x) {
            const auto local_x0 =
                (grid_x - kTerrainGridRadius) * kTerrainGridStep;
            const auto local_x1 =
                local_x0 + kTerrainGridStep;
            const auto index0 = grid_index(grid_x, grid_z);
            const auto index1 = grid_index(grid_x + 1, grid_z);
            const auto index2 = grid_index(grid_x + 1, grid_z + 1);
            const auto index3 = grid_index(grid_x, grid_z + 1);

            // Je garde un enroulement cohérent pour les normales géométriques ;
            // le renderer affiche volontairement les deux faces à cette distance.
            append_terrain_triangle(
                mesh,
                remap,
                samples,
                {index0, index3, index2},
                {{{local_x0, local_z0},
                  {local_x0, local_z1},
                  {local_x1, local_z1}}});
            append_terrain_triangle(
                mesh,
                remap,
                samples,
                {index0, index2, index1},
                {{{local_x0, local_z0},
                  {local_x1, local_z1},
                  {local_x1, local_z0}}});
        }
    }

    return mesh;
}

void filter_sea_horizon_terrain_indices(
    const SeaHorizonTerrainMesh& mesh,
    std::span<const ChunkCoord> detailed_chunks,
    std::vector<std::uint32_t>& proxy_indices,
    std::vector<std::uint32_t>& transition_indices) {
    proxy_indices.clear();
    transition_indices.clear();
    proxy_indices.reserve(mesh.indices.size());
    transition_indices.reserve(mesh.indices.size());

    const auto chunk_less =
        [](const ChunkCoord& left,
           const ChunkCoord& right) noexcept {
            return left.x < right.x ||
                   (left.x == right.x &&
                    left.z < right.z);
        };

    for (std::size_t offset = 0U;
         offset + 2U < mesh.indices.size();
         offset += 3U) {
        const auto first_index = mesh.indices[offset];
        const auto second_index = mesh.indices[offset + 1U];
        const auto third_index = mesh.indices[offset + 2U];
        if (first_index >= mesh.vertices.size() ||
            second_index >= mesh.vertices.size() ||
            third_index >= mesh.vertices.size()) {
            continue;
        }

        const auto& first = mesh.vertices[first_index];
        const auto& second = mesh.vertices[second_index];
        const auto& third = mesh.vertices[third_index];
        const auto center_x =
            (first.x + second.x + third.x) /
            3.0F;
        const auto center_z =
            (first.z + second.z + third.z) /
            3.0F;
        const ChunkCoord owner {
            static_cast<int>(
                std::floor(
                    static_cast<double>(center_x) /
                    static_cast<double>(kChunkSizeX))),
            static_cast<int>(
                std::floor(
                    static_cast<double>(center_z) /
                    static_cast<double>(kChunkSizeZ))),
        };
        const auto detailed =
            std::binary_search(
                detailed_chunks.begin(),
                detailed_chunks.end(),
                owner,
                chunk_less);
        auto& destination =
            detailed
                ? transition_indices
                : proxy_indices;
        destination.insert(
            destination.end(),
            {
                first_index,
                second_index,
                third_index,
            });
    }
}

auto sea_horizon_detail_transition_range(
    float detailed_draw_distance) noexcept
    -> SeaHorizonDetailTransitionRange {
    const auto safe_draw_distance =
        std::isfinite(detailed_draw_distance)
            ? std::max(
                  detailed_draw_distance,
                  0.0F)
            : 0.0F;
    // Je termine la sous-couche huit mètres avant le bord détaillé et je garde
    // un raccord compact de vingt-quatre mètres. Le relief proche reste ainsi
    // pleinement détaillé au lieu de se dissoudre dès quelques chunks.
    const auto end_distance =
        std::max(
            safe_draw_distance -
                kSeaHorizonGridStep *
                    0.5F,
            0.0F);
    return {
        std::max(
            end_distance -
                kSeaHorizonDetailTransitionWidth,
            0.0F),
        end_distance,
    };
}

auto sea_horizon_water_blend_range(
    float detailed_draw_distance) noexcept
    -> SeaHorizonWaterBlendRange {
    const auto safe_draw_distance =
        std::isfinite(detailed_draw_distance)
            ? std::max(
                  detailed_draw_distance,
                  0.0F)
            : 0.0F;
    // Je retire trois chunks et demi au rayon théorique : un chunk absorbe
    // l'anticipation du navire, un demi-chunk protège les angles du carré de
    // streaming et deux chunks couvrent une publication GPU tardive. Je
    // plafonne aussi le raccord validé à quarante mètres afin qu'un bord d'eau
    // réel soit déjà identique à l'océan analytique lorsqu'il apparaît.
    const auto end_distance =
        std::min(
            std::max(
                safe_draw_distance -
                    kSeaHorizonGridStep *
                        3.5F,
                0.0F),
            kSeaHorizonWaterBlendEndCap);
    return {
        std::max(
            end_distance -
                kSeaHorizonWaterBlendWidth,
            0.0F),
        end_distance,
    };
}

auto sea_horizon_water_blend_range(
    float detailed_draw_distance,
    float contiguous_chunk_coverage_distance) noexcept
    -> SeaHorizonWaterBlendRange {
    const auto target =
        sea_horizon_water_blend_range(
            detailed_draw_distance);
    const auto safe_coverage =
        std::isfinite(
            contiguous_chunk_coverage_distance)
            ? std::max(
                  contiguous_chunk_coverage_distance -
                      kSeaHorizonWaterCoverageMargin,
                  0.0F)
            : 0.0F;
    const auto end_distance =
        std::min(
            target.end_distance,
            safe_coverage);
    return {
        std::max(
            end_distance -
                kSeaHorizonWaterBlendWidth,
            0.0F),
        end_distance,
    };
}

auto sea_horizon_contiguous_chunk_coverage_distance(
    const glm::vec3& camera_position,
    std::span<const ChunkCoord> uploaded_chunks,
    int maximum_radius) noexcept -> float {
    if (!std::isfinite(camera_position.x) ||
        !std::isfinite(camera_position.z) ||
        uploaded_chunks.empty()) {
        return 0.0F;
    }

    const auto chunk_x =
        std::floor(
            camera_position.x /
            static_cast<float>(
                kChunkSizeX));
    const auto chunk_z =
        std::floor(
            camera_position.z /
            static_cast<float>(
                kChunkSizeZ));
    const auto minimum_chunk =
        static_cast<double>(
            std::numeric_limits<int>::lowest() +
            kSeaHorizonWaterCoverageScanRadius);
    const auto maximum_chunk =
        static_cast<double>(
            std::numeric_limits<int>::max() -
            kSeaHorizonWaterCoverageScanRadius);
    if (chunk_x < minimum_chunk ||
        chunk_x > maximum_chunk ||
        chunk_z < minimum_chunk ||
        chunk_z > maximum_chunk) {
        return 0.0F;
    }

    const ChunkCoord center {
        static_cast<int>(chunk_x),
        static_cast<int>(chunk_z),
    };
    const auto chunk_is_uploaded =
        [uploaded_chunks](
            const ChunkCoord& candidate) noexcept {
            return std::find(
                       uploaded_chunks.begin(),
                       uploaded_chunks.end(),
                       candidate) !=
                   uploaded_chunks.end();
        };
    const auto radius_limit =
        std::clamp(
            maximum_radius,
            0,
            kSeaHorizonWaterCoverageScanRadius);
    auto complete_radius = -1;
    for (auto radius = 0;
         radius <= radius_limit;
         ++radius) {
        auto ring_complete = true;
        for (auto dz = -radius;
             dz <= radius &&
             ring_complete;
             ++dz) {
            for (auto dx = -radius;
                 dx <= radius;
                 ++dx) {
                if (radius > 0 &&
                    std::abs(dx) != radius &&
                    std::abs(dz) != radius) {
                    continue;
                }
                if (!chunk_is_uploaded({
                        center.x + dx,
                        center.z + dz,
                    })) {
                    ring_complete = false;
                    break;
                }
            }
        }
        if (!ring_complete) {
            break;
        }
        complete_radius = radius;
    }
    if (complete_radius < 0) {
        return 0.0F;
    }

    const auto minimum_x =
        static_cast<float>(
            (static_cast<double>(
                 center.x) -
             static_cast<double>(
                 complete_radius)) *
            static_cast<double>(
                kChunkSizeX));
    const auto maximum_x =
        static_cast<float>(
            (static_cast<double>(
                 center.x) +
             static_cast<double>(
                 complete_radius) +
             1.0) *
            static_cast<double>(
                kChunkSizeX));
    const auto minimum_z =
        static_cast<float>(
            (static_cast<double>(
                 center.z) -
             static_cast<double>(
                 complete_radius)) *
            static_cast<double>(
                kChunkSizeZ));
    const auto maximum_z =
        static_cast<float>(
            (static_cast<double>(
                 center.z) +
             static_cast<double>(
                 complete_radius) +
             1.0) *
            static_cast<double>(
                kChunkSizeZ));
    return std::max(
        std::min({
            camera_position.x - minimum_x,
            maximum_x - camera_position.x,
            camera_position.z - minimum_z,
            maximum_z - camera_position.z,
        }),
        0.0F);
}

auto sea_horizon_fog_range(float storm) noexcept -> SeaHorizonFogRange {
    const auto sanitized_storm =
        std::isfinite(storm) ? std::clamp(storm, 0.0F, 1.0F) : 0.0F;
    return {
        384.0F - sanitized_storm * 128.0F,
        kSeaHorizonTerrainOuterRadius - sanitized_storm * 96.0F,
    };
}

auto sea_horizon_predictive_streaming_focus(
    const glm::vec3& base,
    const glm::vec3& ship_position,
    const glm::vec3& velocity) noexcept -> glm::vec3 {
    const auto safe_base = finite_vec3_or_zero(base);
    if (!std::isfinite(base.x) ||
        !std::isfinite(base.y) ||
        !std::isfinite(base.z) ||
        !std::isfinite(ship_position.x) ||
        !std::isfinite(ship_position.z) ||
        !std::isfinite(velocity.x) ||
        !std::isfinite(velocity.z)) {
        return safe_base;
    }

    const glm::vec2 ship_delta {
        safe_base.x - ship_position.x,
        safe_base.z - ship_position.z,
    };
    if (glm::dot(ship_delta, ship_delta) >
        kPredictiveShipProximity * kPredictiveShipProximity) {
        return safe_base;
    }

    const glm::vec2 horizontal_velocity {velocity.x, velocity.z};
    const auto speed_squared =
        glm::dot(horizontal_velocity, horizontal_velocity);
    if (!std::isfinite(speed_squared) ||
        speed_squared <=
            kPredictiveMinimumSpeed *
                kPredictiveMinimumSpeed) {
        return safe_base;
    }

    const auto speed =
        std::sqrt(speed_squared);
    const auto forward =
        horizontal_velocity / speed;
    const auto prediction_ratio =
        std::clamp(
            (speed - kPredictiveMinimumSpeed) /
                (kPredictiveFullSpeed -
                 kPredictiveMinimumSpeed),
            0.0F,
            1.0F);
    const auto smooth_prediction_ratio =
        prediction_ratio *
        prediction_ratio *
        (3.0F -
         2.0F * prediction_ratio);
    // Je n'avance pleinement le streaming qu'à la vitesse de croisière :
    // les minuscules vitesses dues au roulis ne déplacent plus l'anneau.
    const auto prediction_distance =
        kPredictiveFocusDistance *
        smooth_prediction_ratio;
    return {
        safe_base.x + forward.x * prediction_distance,
        safe_base.y,
        safe_base.z + forward.y * prediction_distance,
    };
}

} // namespace valcraft
