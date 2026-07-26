#pragma once

#include "render/ArchitecturalMesher.h"
#include "render/StylizedPrimitives.h"

#include <cstddef>

namespace valcraft {

// Je conserve les fixtures comme description logique et j'ajoute leur
// geometrie apres les surfaces architecturales deja presentes. La valeur
// retournee est la position du premier index ajoute dans mesh.indices.
[[nodiscard]] auto append_architectural_fixture_geometry(
    ArchitecturalMesh& mesh,
    StylizedPrimitiveLod lod) -> std::size_t;

} // namespace valcraft
