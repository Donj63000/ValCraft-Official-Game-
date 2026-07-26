#pragma once

#include "render/StylizedPrimitives.h"
#include "render/VisualMesh.h"
#include "render/VisualVegetation.h"

#include <cstdint>
#include <functional>
#include <vector>

namespace valcraft {

struct VisualVegetationLighting {
    std::uint8_t sky_light = 15U;
    std::uint8_t block_light = 0U;
};

using VisualVegetationLightingSampler =
    std::function<VisualVegetationLighting(int world_x, int world_y, int world_z)>;

// Je transforme les recettes déterministes en géométrie GPU sans modifier les
// cellules sources. Le même batch peut donc être reconstruit ou remplacé par
// un autre LOD sans aucune incidence sur le monde logique.
[[nodiscard]] auto build_visual_vegetation_mesh(
    const VisualVegetationBuild& build,
    VisualVegetationLod lod,
    StylizedPrimitiveLod primitive_lod,
    const VisualVegetationLightingSampler& lighting_sampler)
    -> OrganicTerrainMesh;

// Je distribue chaque triangle dans une seule section verticale d'apres son
// centroide. Je conserve ainsi toute la primitive lorsqu'un tronc, un cactus
// ou une canopee traverse une frontiere, sans dupliquer ni perdre de triangle.
[[nodiscard]] auto partition_visual_vegetation_mesh(
    const OrganicTerrainMesh& mesh,
    int minimum_y,
    int section_height,
    std::size_t section_count) -> std::vector<OrganicTerrainMesh>;

} // namespace valcraft
