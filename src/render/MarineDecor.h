#pragma once

#include "render/VisualMaterials.h"
#include "world/WorldGenerator.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <type_traits>
#include <vector>

namespace valcraft {

inline constexpr int kMarineDecorGridStep = 2;
inline constexpr std::size_t kMarineDecorMaxInstancesPerChunk = 48U;

enum class MarineDecorKind : std::uint8_t {
    Seagrass = 0,
    Kelp = 1,
    CoralFan = 2,
    BranchCoralWarm = 3,
    BranchCoralLagoon = 4,
    Shell = 5,
};

// Je garde une instance POD compacte pour pouvoir la mettre en cache par
// chunk sans imposer de representation GPU au generateur.
struct MarineDecorInstance {
    float position_x = 0.0F;
    float position_y = 0.0F;
    float position_z = 0.0F;
    float scale_x = 1.0F;
    float scale_y = 1.0F;
    float scale_z = 1.0F;
    float yaw_radians = 0.0F;
    float phase = 0.0F;
    MarineDecorKind kind = MarineDecorKind::Seagrass;
    std::uint8_t reserved = 0U;
    VisualMaterialId material = VisualMaterialId::MarineSeagrass;

    auto operator==(const MarineDecorInstance&) const -> bool = default;
};

static_assert(sizeof(MarineDecorInstance) == 36U);
static_assert(std::is_standard_layout_v<MarineDecorInstance>);
static_assert(std::is_trivially_copyable_v<MarineDecorInstance>);

using MarineTerrainSurfaceSampler =
    std::function<TerrainSurfaceSample(int world_x, int world_z)>;

// Je derive seulement des instances visuelles du terrain procedural : cette
// fonction ne modifie ni les chunks, ni l'eau, ni les collisions.
[[nodiscard]] auto build_marine_decor(
    ChunkCoord chunk_coord,
    WorldGenerationVersion generation_version,
    int world_seed,
    const MarineTerrainSurfaceSampler& sample_surface)
    -> std::vector<MarineDecorInstance>;

} // namespace valcraft
