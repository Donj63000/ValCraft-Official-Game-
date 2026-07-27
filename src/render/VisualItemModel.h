#pragma once

#include "render/StylizedPrimitives.h"
#include "render/VisualMaterials.h"
#include "world/Block.h"

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace valcraft {

enum class VisualItemModelClass : std::uint8_t {
    OrganicBlock = 0,
    ArchitecturalBlock,
    Vegetation,
    Fixture,
    Liquid,
    Armor,
    Shield,
    Weapon,
    Apparel,
    Ore,
    Tool,
};

struct VisualItemPrimitive {
    StylizedPrimitiveType primitive = StylizedPrimitiveType::RoundedBox;
    glm::mat4 transform {1.0F};
    VisualMaterialId material = VisualMaterialId::None;
    glm::vec4 albedo_tint {1.0F};
    bool two_sided = false;
};

struct VisualItemModel {
    BlockId item_id = to_block_id(BlockType::Air);
    VisualItemModelClass model_class = VisualItemModelClass::OrganicBlock;
    std::vector<VisualItemPrimitive> primitives {};
    StylizedPrimitiveBounds bounds {};
    std::uint64_t geometry_checksum = 0U;

    [[nodiscard]] auto empty() const noexcept -> bool {
        return primitives.empty();
    }
};

// Je conserve une couche par objet canonique. Les quatre orientations de
// torche murale pointent vers la couche de la torche et ne dupliquent rien.
inline constexpr std::array<BlockId, 36> kVisualItemCanonicalIds {{
    to_block_id(BlockType::Grass),
    to_block_id(BlockType::Dirt),
    to_block_id(BlockType::Stone),
    to_block_id(BlockType::Sand),
    to_block_id(BlockType::Wood),
    to_block_id(BlockType::Leaves),
    to_block_id(BlockType::Torch),
    to_block_id(BlockType::Cobblestone),
    to_block_id(BlockType::Planks),
    to_block_id(BlockType::Gravel),
    to_block_id(BlockType::MossyStone),
    to_block_id(BlockType::Snow),
    to_block_id(BlockType::PineWood),
    to_block_id(BlockType::PineLeaves),
    to_block_id(BlockType::TallGrass),
    to_block_id(BlockType::RedFlower),
    to_block_id(BlockType::YellowFlower),
    to_block_id(BlockType::DeadShrub),
    to_block_id(BlockType::Cactus),
    to_block_id(BlockType::Water),
    to_block_id(BlockType::Glass),
    to_block_id(BlockType::Pastron),
    to_block_id(BlockType::RoundShield),
    to_block_id(BlockType::Sword),
    to_block_id(BlockType::Spear),
    to_block_id(BlockType::Shoes),
    to_block_id(BlockType::Pants),
    to_block_id(BlockType::CoalOre),
    to_block_id(BlockType::IronOre),
    to_block_id(BlockType::GoldOre),
    to_block_id(BlockType::DiamondOre),
    to_block_id(BlockType::MetallicAlloyOre),
    to_block_id(BlockType::Pickaxe),
    to_block_id(BlockType::Axe),
    to_block_id(BlockType::Shovel),
    to_block_id(BlockType::Musket),
}};

inline constexpr std::size_t kVisualItemModelCount =
    kVisualItemCanonicalIds.size();

[[nodiscard]] constexpr auto canonical_visual_item_id(
    BlockId block_id) noexcept -> BlockId {
    return block_item_id(block_id);
}

[[nodiscard]] constexpr auto is_visual_item_displayable(
    BlockId block_id) noexcept -> bool {
    return canonical_visual_item_id(block_id) !=
               to_block_id(BlockType::Air) &&
           is_known_block_id(block_id);
}

[[nodiscard]] constexpr auto visual_item_layer_index(
    BlockId block_id) noexcept -> std::size_t {
    const auto canonical_id = canonical_visual_item_id(block_id);
    for (std::size_t index = 0U;
         index < kVisualItemCanonicalIds.size();
         ++index) {
        if (kVisualItemCanonicalIds[index] == canonical_id) {
            return index;
        }
    }
    return kVisualItemModelCount;
}

[[nodiscard]] constexpr auto visual_item_model_class_for(
    BlockId block_id) noexcept -> VisualItemModelClass {
    switch (static_cast<BlockType>(canonical_visual_item_id(block_id))) {
    case BlockType::Grass:
    case BlockType::Dirt:
    case BlockType::Stone:
    case BlockType::Sand:
    case BlockType::Gravel:
    case BlockType::MossyStone:
    case BlockType::Snow:
        return VisualItemModelClass::OrganicBlock;
    case BlockType::Cobblestone:
    case BlockType::Planks:
    case BlockType::Glass:
        return VisualItemModelClass::ArchitecturalBlock;
    case BlockType::Wood:
    case BlockType::Leaves:
    case BlockType::PineWood:
    case BlockType::PineLeaves:
    case BlockType::TallGrass:
    case BlockType::RedFlower:
    case BlockType::YellowFlower:
    case BlockType::DeadShrub:
    case BlockType::Cactus:
        return VisualItemModelClass::Vegetation;
    case BlockType::Torch:
        return VisualItemModelClass::Fixture;
    case BlockType::Water:
        return VisualItemModelClass::Liquid;
    case BlockType::Pastron:
        return VisualItemModelClass::Armor;
    case BlockType::RoundShield:
        return VisualItemModelClass::Shield;
    case BlockType::Sword:
    case BlockType::Spear:
    case BlockType::Musket:
        return VisualItemModelClass::Weapon;
    case BlockType::Shoes:
    case BlockType::Pants:
        return VisualItemModelClass::Apparel;
    case BlockType::CoalOre:
    case BlockType::IronOre:
    case BlockType::GoldOre:
    case BlockType::DiamondOre:
    case BlockType::MetallicAlloyOre:
        return VisualItemModelClass::Ore;
    case BlockType::Pickaxe:
    case BlockType::Axe:
    case BlockType::Shovel:
        return VisualItemModelClass::Tool;
    case BlockType::Air:
    case BlockType::TorchWallPositiveX:
    case BlockType::TorchWallNegativeX:
    case BlockType::TorchWallPositiveZ:
    case BlockType::TorchWallNegativeZ:
    default:
        return VisualItemModelClass::OrganicBlock;
    }
}

[[nodiscard]] auto build_visual_item_model(
    BlockId block_id) -> VisualItemModel;

[[nodiscard]] auto build_all_visual_item_models()
    -> std::array<VisualItemModel, kVisualItemModelCount>;

[[nodiscard]] auto visual_item_model_fingerprint(
    const VisualItemModel& model) noexcept -> std::uint64_t;

} // namespace valcraft
