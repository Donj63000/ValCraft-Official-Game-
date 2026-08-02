#pragma once

#include "render/ArchitecturalMesher.h"
#include "render/StylizedPrimitives.h"

#include <cstddef>

namespace valcraft {

// Je route uniquement les accessoires solides dont la silhouette peut etre
// reconstruite proprement dans la passe hard-surface. Les plantes et bouees
// restent dans leur passe cutout tant qu'une passe transparente dediee ne les
// prend pas en charge.
[[nodiscard]] constexpr auto is_modern_backrooms_hard_surface_prop(
    BlockId block_id) noexcept -> bool {
    switch (static_cast<BlockType>(block_id)) {
    case BlockType::BackroomsDesk:
    case BlockType::BackroomsChair:
    case BlockType::BackroomsRampPositiveX:
    case BlockType::BackroomsRampNegativeX:
    case BlockType::BackroomsRampPositiveZ:
    case BlockType::BackroomsRampNegativeZ:
        return true;
    default:
        return false;
    }
}

// Je complete un maillage architectural deja construit. La valeur retournee
// designe le premier index ajoute et permet aux tests de verifier qu'une
// instance n'est emise qu'une fois.
[[nodiscard]] auto append_modern_backrooms_prop_geometry(
    ArchitecturalMesh& mesh,
    const ArchitecturalSection& section,
    const ArchitecturalSampler& sampler,
    StylizedPrimitiveLod lod = StylizedPrimitiveLod::Medium) -> std::size_t;

} // namespace valcraft
