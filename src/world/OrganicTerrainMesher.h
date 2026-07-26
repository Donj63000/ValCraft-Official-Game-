#pragma once

#include "render/VisualMesh.h"
#include "world/Block.h"
#include "world/Chunk.h"

#include <cstdint>
#include <functional>

namespace valcraft {

// Je borne explicitement le support du champ C2. Les états organiques sont
// préclassés une seule fois dans le cache de section : ces 4³ lectures ne
// réexécutent donc jamais la classification BlockId pendant le remeshing.
inline constexpr std::size_t kOrganicTerrainVisualFieldSampleCount = 64U;
inline constexpr std::size_t
    kOrganicTerrainVisualFieldBlockClassificationCount = 0U;

// Je réserve ce bit au contrat visuel géologique. Il signale au shader que
// `primary_block_id` et `secondary_block_id` forment une paire de couches
// canonique dont `material_blend` peut être interpolé sans révéler la grille.
inline constexpr std::uint16_t kTerrainSurfaceFlagGeologicalBlend = 1U << 4U;

struct OrganicTerrainCellSample {
    BlockId block_id = to_block_id(BlockType::Air);
    std::uint8_t sky_light = 15;
    std::uint8_t block_light = 0;
};

using OrganicTerrainSampler = std::function<OrganicTerrainCellSample(int world_x, int world_y, int world_z)>;

struct OrganicTerrainSection {
    // Je rends les deux bornes inclusives pour correspondre aux sections de ChunkMesher.
    BlockCoord min {};
    BlockCoord max {
        kChunkSizeX - 1,
        kChunkSectionHeight - 1,
        kChunkSizeZ - 1,
    };

    [[nodiscard]] auto valid() const noexcept -> bool;
    [[nodiscard]] auto contains(const BlockCoord& coord) const noexcept -> bool;
};

struct OrganicTerrainMesherSettings {
    // Je limite le déplacement autour du centre de la cellule duale afin de
    // lisser la silhouette sans éloigner visuellement la surface de la collision.
    float maximum_vertex_displacement = 0.42F;

    // Je projette légèrement les surfaces praticables exposées vers le champ
    // visuel lissé. Cette distance reste incluse dans la contrainte ci-dessus
    // et ne modifie ni les blocs, ni la collision, ni la topologie.
    float exposed_surface_relaxation = 0.26F;

    // Je raffine uniquement les arêtes de lèvres naturelles fortement courbes.
    // Ce réglage reste purement visuel et n'entre jamais dans une sauvegarde.
    bool adaptive_lip_refinement = false;
};

[[nodiscard]] constexpr auto is_organic_terrain_block(BlockId block_id) noexcept -> bool {
    switch (static_cast<BlockType>(block_id)) {
    case BlockType::Grass:
    case BlockType::Dirt:
    case BlockType::Stone:
    case BlockType::Sand:
    case BlockType::Gravel:
    case BlockType::MossyStone:
    case BlockType::Snow:
    case BlockType::CoalOre:
    case BlockType::IronOre:
    case BlockType::GoldOre:
    case BlockType::DiamondOre:
    case BlockType::MetallicAlloyOre:
        return true;
    case BlockType::Air:
    case BlockType::Wood:
    case BlockType::Leaves:
    case BlockType::Torch:
    case BlockType::Cobblestone:
    case BlockType::Planks:
    case BlockType::PineWood:
    case BlockType::PineLeaves:
    case BlockType::TallGrass:
    case BlockType::RedFlower:
    case BlockType::YellowFlower:
    case BlockType::DeadShrub:
    case BlockType::Cactus:
    case BlockType::Water:
    case BlockType::TorchWallPositiveX:
    case BlockType::TorchWallNegativeX:
    case BlockType::TorchWallPositiveZ:
    case BlockType::TorchWallNegativeZ:
    case BlockType::Glass:
    case BlockType::Pastron:
    case BlockType::RoundShield:
    case BlockType::Sword:
    case BlockType::Spear:
    case BlockType::Shoes:
    case BlockType::Pants:
    case BlockType::Pickaxe:
    case BlockType::Axe:
    case BlockType::Shovel:
    default:
        return false;
    }
}

class OrganicTerrainMesher {
public:
    explicit OrganicTerrainMesher(OrganicTerrainMesherSettings settings = {}) noexcept;

    [[nodiscard]] auto build_mesh(const OrganicTerrainSection& section,
                                  const OrganicTerrainSampler& sampler,
                                  std::size_t vertex_reserve_hint = 0,
                                  std::size_t index_reserve_hint = 0) const -> OrganicTerrainMesh;

    [[nodiscard]] auto settings() const noexcept -> const OrganicTerrainMesherSettings&;

private:
    OrganicTerrainMesherSettings settings_ {};
};

} // namespace valcraft
