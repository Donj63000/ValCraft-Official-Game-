#pragma once

#include "world/Block.h"

#include <glm/vec3.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>
#include <vector>

namespace valcraft {

class World;

inline constexpr float kSeaHorizonTerrainOuterRadius = 512.0F;
// Je garde le pas logique de seize metres pour les raccords de chunks, mais
// j'echantillonne la silhouette des iles deux fois plus finement.
inline constexpr float kSeaHorizonGridStep = 16.0F;
inline constexpr float kSeaHorizonTerrainSampleStep = 8.0F;
inline constexpr float kSeaHorizonCacheHysteresis = 12.0F;
inline constexpr float kSeaHorizonTerrainHeightBias = -0.10F;
inline constexpr float kSeaHorizonDetailTransitionWidth = 24.0F;
inline constexpr float kSeaHorizonWaterBlendWidth = 24.0F;
inline constexpr float kSeaHorizonWaterBlendEndCap = 40.0F;
inline constexpr float kSeaHorizonWaterCoverageMargin = 8.0F;
inline constexpr int kSeaHorizonWaterCoverageScanRadius = 3;
inline constexpr float kSeaHorizonProjectionFarPlane = 576.0F;
inline constexpr std::size_t kSeaHorizonMaxTerrainVertices = 17689U;
inline constexpr std::size_t kSeaHorizonMaxTerrainTriangles = 34848U;

struct SeaHorizonSnappedCenter {
    int x = 0;
    int z = 0;

    auto operator==(const SeaHorizonSnappedCenter&) const -> bool = default;
};

// Je compacte la normale sur trois octets et je conserve le bloc procedural
// exact afin que le futur shader lointain puisse partager la palette moderne.
struct SeaHorizonTerrainVertex {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    std::int8_t nx = 0;
    std::int8_t ny = 127;
    std::int8_t nz = 0;
    BlockId block_id = to_block_id(BlockType::Air);

    auto operator==(const SeaHorizonTerrainVertex&) const -> bool = default;
};

static_assert(sizeof(SeaHorizonTerrainVertex) == 16U);
static_assert(alignof(SeaHorizonTerrainVertex) == alignof(float));
static_assert(std::is_standard_layout_v<SeaHorizonTerrainVertex>);
static_assert(std::is_trivially_copyable_v<SeaHorizonTerrainVertex>);

[[nodiscard]] constexpr auto pack_sea_horizon_snorm(float value) noexcept
    -> std::int8_t {
    const auto clamped = std::clamp(value, -1.0F, 1.0F);
    return static_cast<std::int8_t>(
        clamped >= 0.0F
            ? clamped * 127.0F + 0.5F
            : clamped * 127.0F - 0.5F);
}

[[nodiscard]] constexpr auto unpack_sea_horizon_snorm(
    std::int8_t value) noexcept -> float {
    return std::max(static_cast<float>(value) / 127.0F, -1.0F);
}

struct SeaHorizonTerrainMesh {
    std::vector<SeaHorizonTerrainVertex> vertices {};
    std::vector<std::uint32_t> indices {};
    SeaHorizonSnappedCenter snapped_center {};

    [[nodiscard]] auto empty() const noexcept -> bool {
        return indices.empty();
    }

    [[nodiscard]] auto triangle_count() const noexcept -> std::size_t {
        return indices.size() / 3U;
    }

    auto operator==(const SeaHorizonTerrainMesh&) const -> bool = default;
};

struct SeaHorizonFogRange {
    float start_distance = 384.0F;
    float end_distance = kSeaHorizonTerrainOuterRadius;

    auto operator==(const SeaHorizonFogRange&) const -> bool = default;
};

struct SeaHorizonDetailTransitionRange {
    float start_distance = 0.0F;
    float end_distance = 0.0F;

    [[nodiscard]] auto enabled() const noexcept -> bool {
        return end_distance > start_distance;
    }

    auto operator==(const SeaHorizonDetailTransitionRange&) const
        -> bool = default;
};

struct SeaHorizonWaterBlendRange {
    float start_distance = 0.0F;
    float end_distance = 0.0F;

    auto operator==(const SeaHorizonWaterBlendRange&) const
        -> bool = default;
};

[[nodiscard]] auto sea_horizon_snapped_center(
    const glm::vec3& camera_position) noexcept -> SeaHorizonSnappedCenter;

[[nodiscard]] auto sea_horizon_stable_center(
    const SeaHorizonSnappedCenter& current_center,
    const glm::vec3& camera_position) noexcept -> SeaHorizonSnappedCenter;

[[nodiscard]] auto build_sea_horizon_terrain_mesh(
    const World& world,
    const glm::vec3& camera_position) -> SeaHorizonTerrainMesh;

void filter_sea_horizon_terrain_indices(
    const SeaHorizonTerrainMesh& mesh,
    std::span<const ChunkCoord> detailed_chunks,
    std::vector<std::uint32_t>& proxy_indices,
    std::vector<std::uint32_t>& transition_indices);

[[nodiscard]] auto sea_horizon_detail_transition_range(
    float detailed_draw_distance) noexcept
    -> SeaHorizonDetailTransitionRange;

[[nodiscard]] auto sea_horizon_water_blend_range(
    float detailed_draw_distance) noexcept
    -> SeaHorizonWaterBlendRange;

[[nodiscard]] auto sea_horizon_water_blend_range(
    float detailed_draw_distance,
    float contiguous_chunk_coverage_distance) noexcept
    -> SeaHorizonWaterBlendRange;

[[nodiscard]] auto sea_horizon_contiguous_chunk_coverage_distance(
    const glm::vec3& camera_position,
    std::span<const ChunkCoord> uploaded_chunks,
    int maximum_radius) noexcept -> float;

[[nodiscard]] auto sea_horizon_fog_range(float storm) noexcept
    -> SeaHorizonFogRange;

[[nodiscard]] auto sea_horizon_predictive_streaming_focus(
    const glm::vec3& base,
    const glm::vec3& ship_position,
    const glm::vec3& velocity) noexcept -> glm::vec3;

} // namespace valcraft
