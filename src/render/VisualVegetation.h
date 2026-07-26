#pragma once

#include "world/Block.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace valcraft {

// Je classe uniquement l'apparence d'un ensemble de blocs. Cette information
// ne remplace jamais les BlockId et ne doit donc pas etre serialisee.
enum class VisualVegetationSourceKind : std::uint8_t {
    BroadleafTree = 0,
    PineTree,
    WoodStructure,
    PineStructure,
    Cactus,
    TallGrass,
    RedFlower,
    YellowFlower,
};

enum class VisualVegetationPrimitive : std::uint8_t {
    TaperedTrunk = 0,
    EllipsoidCanopy,
    ConicalCanopy,
    CactusStem,
    CactusArm,
    GrassBlade,
    FlowerStem,
    FlowerPetals,
    SimplifiedBouquet,
    Impostor,
    LeafSpray,
};

enum class VisualVegetationLod : std::uint8_t {
    Near = 0,
    Medium,
    Far,
    Count,
};

inline constexpr auto kVisualVegetationLodCount =
    static_cast<std::size_t>(VisualVegetationLod::Count);

struct VisualVegetationBounds {
    float min_x = 0.0F;
    float min_y = 0.0F;
    float min_z = 0.0F;
    float max_x = 0.0F;
    float max_y = 0.0F;
    float max_z = 0.0F;
    bool valid = false;

    auto operator==(const VisualVegetationBounds&) const -> bool = default;
};

struct VisualVegetationSection {
    BlockCoord min {};
    BlockCoord max {};
    int halo = 1;

    auto operator==(const VisualVegetationSection&) const -> bool = default;
};

// Je conserve une entree de classification meme lorsque sa racine appartient
// au halo. Le proprietaire est alors la section voisine, ce qui evite les
// doublons de rendu sans perdre l'information de classification locale.
struct VisualVegetationSource {
    VisualVegetationSourceKind kind = VisualVegetationSourceKind::TallGrass;
    BlockId source_block = to_block_id(BlockType::Air);
    bool owns_instances = false;
    BlockCoord anchor {};
    std::uint32_t source_cell_count = 0;
    std::uint32_t foliage_cell_count = 0;
    std::uint32_t seed = 0;
    // Je distingue le volume porteur du feuillage afin de dimensionner le
    // tronc et la couronne sur leurs cellules reelles, sans toucher au monde.
    VisualVegetationBounds source_bounds {};
    VisualVegetationBounds foliage_bounds {};
    VisualVegetationBounds logical_bounds {};

    auto operator==(const VisualVegetationSource&) const -> bool = default;
};

// Je fournis directement les bornes monde de chaque primitive afin que le
// renderer puisse faire son culling sans reconstruire une matrice.
struct VisualVegetationInstance {
    float position_x = 0.0F;
    float position_y = 0.0F;
    float position_z = 0.0F;
    float scale_x = 1.0F;
    float scale_y = 1.0F;
    float scale_z = 1.0F;
    float yaw_radians = 0.0F;
    float wind_phase = 0.0F;
    VisualVegetationBounds bounds {};
    std::uint32_t seed = 0;
    VisualVegetationPrimitive primitive = VisualVegetationPrimitive::Impostor;
    VisualVegetationSourceKind source_kind = VisualVegetationSourceKind::TallGrass;
    BlockId material_block = to_block_id(BlockType::Air);

    auto operator==(const VisualVegetationInstance&) const -> bool = default;
};

struct VisualVegetationLodBatch {
    VisualVegetationLod lod = VisualVegetationLod::Near;
    std::vector<VisualVegetationInstance> instances;
    VisualVegetationBounds bounds {};

    [[nodiscard]] auto instance_count() const noexcept -> std::size_t {
        return instances.size();
    }
};

struct VisualVegetationBuild {
    std::vector<VisualVegetationSource> sources;
    std::array<VisualVegetationLodBatch, kVisualVegetationLodCount> lods {};
    VisualVegetationBounds bounds {};
};

using VisualVegetationSampler = std::function<BlockId(int world_x, int world_y, int world_z)>;

[[nodiscard]] auto visual_vegetation_lod_index(VisualVegetationLod lod) noexcept -> std::size_t;

// Je derive toutes les variations de la graine du monde et des coordonnees
// entieres. Un meme monde produit ainsi exactement les memes instances.
[[nodiscard]] auto visual_vegetation_seed(
    std::uint32_t world_seed,
    BlockCoord coordinate,
    std::uint32_t salt = 0U) noexcept -> std::uint32_t;

[[nodiscard]] auto build_visual_vegetation(
    const VisualVegetationSection& section,
    const VisualVegetationSampler& sampler,
    std::uint32_t world_seed) -> VisualVegetationBuild;

// Je calcule un condensat canonique champ par champ : il ne depend ni du
// padding des structures ni de l'implementation de la STL.
[[nodiscard]] auto visual_vegetation_deterministic_hash(
    const VisualVegetationBuild& build) noexcept -> std::uint64_t;

} // namespace valcraft
