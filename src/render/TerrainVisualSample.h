#pragma once

#include "render/VisualMaterials.h"
#include "render/VisualMesh.h"

#include <glm/vec3.hpp>

#include <cstdint>
#include <optional>

namespace valcraft {

// Cette requête appartient exclusivement au rendu. Je ne l'utilise jamais
// pour les collisions, le raycast DDA, la navigation ou une règle de jeu.
struct TerrainVisualQuery {
    glm::vec3 world_position {0.0F};
    float maximum_distance = 1.5F;
    float minimum_normal_y = -1.0F;
};

struct TerrainVisualSample {
    glm::vec3 position {0.0F};
    glm::vec3 normal {0.0F, 1.0F, 0.0F};
    VisualMaterialId primary_material = VisualMaterialId::None;
    VisualMaterialId secondary_material = VisualMaterialId::None;
    float material_blend = 0.0F;
    float ambient_occlusion = 1.0F;
    float sky_light = 1.0F;
    float block_light = 0.0F;
    float distance_squared = 0.0F;
    std::uint64_t mesh_revision = 0U;

    auto operator==(const TerrainVisualSample&) const -> bool = default;
};

[[nodiscard]] auto sample_terrain_visual_mesh(
    const OrganicTerrainMesh& mesh,
    const TerrainVisualQuery& query,
    std::uint64_t mesh_revision = 0U) noexcept
    -> std::optional<TerrainVisualSample>;

} // namespace valcraft
