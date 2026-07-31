#pragma once

#include "app/Hotbar.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace valcraft {

enum class EquipmentSlot : std::uint8_t {
    Chest = 0,
    Shield = 1,
    Weapon = 2,
    Legs = 3,
    Feet = 4,
};

constexpr std::size_t kEquipmentSlotCount = 5;

struct WeaponStats {
    float damage = 0.0F;
    float range = 0.0F;
};

inline constexpr auto equipment_slot_index(EquipmentSlot slot) noexcept -> std::size_t {
    return static_cast<std::size_t>(slot);
}

inline constexpr auto equipment_slot_label(EquipmentSlot slot) noexcept -> std::string_view {
    switch (slot) {
    case EquipmentSlot::Chest:
        return "PASTRON";
    case EquipmentSlot::Shield:
        return "BOUCLIER";
    case EquipmentSlot::Weapon:
        return "ARME";
    case EquipmentSlot::Legs:
        return "JAMBES";
    case EquipmentSlot::Feet:
    default:
        return "PIEDS";
    }
}

inline constexpr auto equipment_slot_from_index(std::size_t index) noexcept -> EquipmentSlot {
    return index < kEquipmentSlotCount ? static_cast<EquipmentSlot>(index) : EquipmentSlot::Chest;
}

inline constexpr auto item_equipment_slot(BlockId block_id) noexcept -> std::optional<EquipmentSlot> {
    block_id = block_item_id(block_id);
    switch (static_cast<BlockType>(block_id)) {
    case BlockType::Pastron:
        return EquipmentSlot::Chest;
    case BlockType::RoundShield:
        return EquipmentSlot::Shield;
    case BlockType::Sword:
    case BlockType::Spear:
    case BlockType::LeviathanSpine:
        return EquipmentSlot::Weapon;
    case BlockType::Pants:
        return EquipmentSlot::Legs;
    case BlockType::Shoes:
        return EquipmentSlot::Feet;
    case BlockType::Air:
    case BlockType::Grass:
    case BlockType::Dirt:
    case BlockType::Stone:
    case BlockType::Sand:
    case BlockType::Wood:
    case BlockType::Leaves:
    case BlockType::Torch:
    case BlockType::Cobblestone:
    case BlockType::Planks:
    case BlockType::Gravel:
    case BlockType::MossyStone:
    case BlockType::Snow:
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
    case BlockType::CoalOre:
    case BlockType::IronOre:
    case BlockType::GoldOre:
    case BlockType::DiamondOre:
    case BlockType::MetallicAlloyOre:
    case BlockType::Pickaxe:
    case BlockType::Axe:
    case BlockType::Shovel:
    case BlockType::Musket:
    default:
        return std::nullopt;
    }
}

inline constexpr auto equipment_slot_accepts_item(EquipmentSlot slot, BlockId block_id) noexcept -> bool {
    const auto item_slot = item_equipment_slot(block_id);
    return item_slot.has_value() && *item_slot == slot;
}

inline constexpr auto equipment_ref_accepts_item(std::size_t slot_index, BlockId block_id) noexcept -> bool {
    return slot_index < kEquipmentSlotCount &&
           equipment_slot_accepts_item(equipment_slot_from_index(slot_index), block_id);
}

inline constexpr auto armor_resistance_percent(BlockId block_id) noexcept -> float {
    switch (static_cast<BlockType>(block_item_id(block_id))) {
    case BlockType::Pastron:
        return 18.0F;
    case BlockType::RoundShield:
        return 12.0F;
    case BlockType::Pants:
        return 8.0F;
    case BlockType::Shoes:
        return 4.0F;
    case BlockType::Air:
    case BlockType::Grass:
    case BlockType::Dirt:
    case BlockType::Stone:
    case BlockType::Sand:
    case BlockType::Wood:
    case BlockType::Leaves:
    case BlockType::Torch:
    case BlockType::Cobblestone:
    case BlockType::Planks:
    case BlockType::Gravel:
    case BlockType::MossyStone:
    case BlockType::Snow:
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
    case BlockType::CoalOre:
    case BlockType::IronOre:
    case BlockType::GoldOre:
    case BlockType::DiamondOre:
    case BlockType::MetallicAlloyOre:
    case BlockType::Sword:
    case BlockType::Spear:
    case BlockType::Pickaxe:
    case BlockType::Axe:
    case BlockType::Shovel:
    case BlockType::Musket:
    case BlockType::LeviathanSpine:
    default:
        return 0.0F;
    }
}

inline constexpr auto weapon_stats(BlockId block_id) noexcept -> std::optional<WeaponStats> {
    switch (static_cast<BlockType>(block_item_id(block_id))) {
    case BlockType::Sword:
        return WeaponStats {6.0F, 3.1F};
    case BlockType::Spear:
        return WeaponStats {5.0F, 4.4F};
    case BlockType::Air:
    case BlockType::Grass:
    case BlockType::Dirt:
    case BlockType::Stone:
    case BlockType::Sand:
    case BlockType::Wood:
    case BlockType::Leaves:
    case BlockType::Torch:
    case BlockType::Cobblestone:
    case BlockType::Planks:
    case BlockType::Gravel:
    case BlockType::MossyStone:
    case BlockType::Snow:
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
    case BlockType::CoalOre:
    case BlockType::IronOre:
    case BlockType::GoldOre:
    case BlockType::DiamondOre:
    case BlockType::MetallicAlloyOre:
    case BlockType::Pastron:
    case BlockType::RoundShield:
    case BlockType::Shoes:
    case BlockType::Pants:
    case BlockType::Pickaxe:
    case BlockType::Axe:
    case BlockType::Shovel:
    case BlockType::Musket:
    case BlockType::LeviathanSpine:
    default:
        return std::nullopt;
    }
}

inline constexpr auto equipped_resistance_percent(
    const std::array<HotbarSlot, kEquipmentSlotCount>& equipment_slots,
    bool suppress_shield = false) noexcept -> float {
    auto total = 0.0F;
    for (std::size_t index = 0U;
         index < equipment_slots.size();
         ++index) {
        const auto& slot = equipment_slots[index];
        if (!hotbar_slot_has_item(slot)) {
            continue;
        }
        if (suppress_shield &&
            index ==
                equipment_slot_index(
                    EquipmentSlot::Shield)) {
            continue;
        }
        total += armor_resistance_percent(slot.block_id);
    }
    return std::clamp(total, 0.0F, 85.0F);
}

inline constexpr auto equipped_weapon_stats(const std::array<HotbarSlot, kEquipmentSlotCount>& equipment_slots,
                                            const HotbarState& hotbar) noexcept -> std::optional<WeaponStats> {
    const auto& equipped_weapon = equipment_slots[equipment_slot_index(EquipmentSlot::Weapon)];
    if (hotbar_slot_has_item(equipped_weapon)) {
        // Je ne rabats pas une arme legendaire sur les statistiques de l'objet
        // selectionne : son systeme colossal doit etre l'unique source d'attaque.
        return weapon_stats(equipped_weapon.block_id);
    }

    if (hotbar_slot_has_item(hotbar.selected_slot())) {
        return weapon_stats(hotbar.selected_slot().block_id);
    }
    return std::nullopt;
}

} // namespace valcraft
