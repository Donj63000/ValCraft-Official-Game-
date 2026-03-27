#pragma once

#include "app/Hotbar.h"
#include "world/BlockVisuals.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace valcraft {

constexpr std::size_t kInventoryColumns = 9;
constexpr std::size_t kInventoryRows = 3;
constexpr std::size_t kInventoryStorageSlotCount = kInventoryColumns * kInventoryRows;
constexpr std::size_t kInventoryVisibleSlotCount = kInventoryStorageSlotCount + kHotbarSlotCount;

enum class InventorySlotGroup : std::uint8_t {
    Storage = 0,
    Hotbar = 1,
};

struct InventorySlotRef {
    InventorySlotGroup group = InventorySlotGroup::Storage;
    std::size_t index = 0;

    auto operator==(const InventorySlotRef&) const -> bool = default;
};

struct InventoryMenuState {
    bool visible = false;
    float cursor_x = 0.0F;
    float cursor_y = 0.0F;
    std::array<HotbarSlot, kInventoryStorageSlotCount> storage_slots {};
    HotbarSlot carried_slot {};
    bool carrying_item = false;
    std::optional<InventorySlotRef> hovered_slot {};

    auto operator==(const InventoryMenuState&) const -> bool = default;
};

struct InventorySlotLayout {
    InventorySlotRef ref {};
    HotbarSlot slot {};
    BlockAtlasTile icon_tile {};
    float x = 0.0F;
    float y = 0.0F;
    float size = 0.0F;
    bool hovered = false;
    bool is_hotbar = false;
    bool is_selected_hotbar = false;
    bool has_icon = false;
};

struct InventoryMenuLayout {
    float panel_x = 0.0F;
    float panel_y = 0.0F;
    float panel_width = 0.0F;
    float panel_height = 0.0F;
    float title_center_x = 0.0F;
    float title_y = 0.0F;
    float subtitle_center_x = 0.0F;
    float subtitle_y = 0.0F;
    float preview_x = 0.0F;
    float preview_y = 0.0F;
    float preview_width = 0.0F;
    float preview_height = 0.0F;
    float preview_center_x = 0.0F;
    float preview_base_y = 0.0F;
    float silhouette_scale = 0.0F;
    float grid_x = 0.0F;
    float grid_y = 0.0F;
    float grid_width = 0.0F;
    float grid_height = 0.0F;
    float storage_label_x = 0.0F;
    float storage_label_y = 0.0F;
    float hotbar_label_x = 0.0F;
    float hotbar_label_y = 0.0F;
    float footer_center_x = 0.0F;
    float footer_y = 0.0F;
    float slot_size = 0.0F;
    float slot_gap = 0.0F;
    float icon_inset = 0.0F;
    std::array<InventorySlotLayout, kInventoryVisibleSlotCount> slots {};
};

inline constexpr auto inventory_make_slot(BlockId block_id, std::uint8_t count = 1) noexcept -> HotbarSlot {
    return make_item_stack(block_id, count);
}

inline constexpr auto inventory_empty_slot() noexcept -> HotbarSlot {
    return empty_item_stack();
}

inline constexpr auto inventory_slot_has_item(const HotbarSlot& slot) noexcept -> bool {
    return hotbar_slot_has_item(slot);
}

inline constexpr auto inventory_can_merge(const HotbarSlot& slot, BlockId block_id) noexcept -> bool {
    return inventory_slot_has_item(slot) &&
           slot.block_id == block_id &&
           slot.count < kMaxItemStackCount;
}

inline constexpr auto inventory_same_item(const HotbarSlot& lhs, const HotbarSlot& rhs) noexcept -> bool {
    return inventory_slot_has_item(lhs) &&
           inventory_slot_has_item(rhs) &&
           lhs.block_id == rhs.block_id;
}

inline constexpr auto inventory_item_label(BlockId block_id) noexcept -> std::string_view {
    switch (static_cast<BlockType>(block_id)) {
    case BlockType::Grass:
        return "HERBE";
    case BlockType::Dirt:
        return "TERRE";
    case BlockType::Stone:
        return "PIERRE";
    case BlockType::Sand:
        return "SABLE";
    case BlockType::Wood:
        return "BOIS";
    case BlockType::Leaves:
        return "FEUILLES";
    case BlockType::Torch:
        return "TORCHE";
    case BlockType::Cobblestone:
        return "PIERRE TAILLEE";
    case BlockType::Planks:
        return "PLANCHES";
    case BlockType::Gravel:
        return "GRAVIER";
    case BlockType::MossyStone:
        return "PIERRE MOUSSE";
    case BlockType::Snow:
        return "NEIGE";
    case BlockType::PineWood:
        return "BOIS DE PIN";
    case BlockType::PineLeaves:
        return "AIGUILLES";
    case BlockType::TallGrass:
        return "HERBE HAUTE";
    case BlockType::RedFlower:
        return "FLEUR ROUGE";
    case BlockType::YellowFlower:
        return "FLEUR JAUNE";
    case BlockType::DeadShrub:
        return "BUISSON SEC";
    case BlockType::Cactus:
        return "CACTUS";
    case BlockType::Water:
        return "EAU";
    case BlockType::Air:
    default:
        return "";
    }
}

inline constexpr auto inventory_slot_icon_tile(BlockId block_id) noexcept -> BlockAtlasTile {
    return block_hotbar_tile(block_id);
}

inline constexpr void normalize_inventory_storage_slot(HotbarSlot& slot) noexcept {
    normalize_item_stack(slot);
}

inline constexpr void normalize_inventory_hotbar_slot(HotbarState& hotbar, std::size_t index) noexcept {
    if (index >= kHotbarSlotCount) {
        return;
    }
    normalize_item_stack(hotbar.slots[index]);
}

inline constexpr void normalize_inventory_state(InventoryMenuState& inventory, HotbarState& hotbar) noexcept {
    for (auto& slot : inventory.storage_slots) {
        normalize_inventory_storage_slot(slot);
    }
    for (std::size_t index = 0; index < kHotbarSlotCount; ++index) {
        normalize_inventory_hotbar_slot(hotbar, index);
    }

    normalize_item_stack(inventory.carried_slot);
    inventory.carrying_item = inventory_slot_has_item(inventory.carried_slot);
}

inline constexpr auto inventory_slot_ptr(InventoryMenuState& inventory,
                                         HotbarState& hotbar,
                                         InventorySlotRef ref) noexcept -> HotbarSlot* {
    switch (ref.group) {
    case InventorySlotGroup::Storage:
        if (ref.index < inventory.storage_slots.size()) {
            return &inventory.storage_slots[ref.index];
        }
        return nullptr;
    case InventorySlotGroup::Hotbar:
        if (ref.index < hotbar.slots.size()) {
            return &hotbar.slots[ref.index];
        }
        return nullptr;
    default:
        return nullptr;
    }
}

inline constexpr auto inventory_slot_ptr(const InventoryMenuState& inventory,
                                         const HotbarState& hotbar,
                                         InventorySlotRef ref) noexcept -> const HotbarSlot* {
    switch (ref.group) {
    case InventorySlotGroup::Storage:
        if (ref.index < inventory.storage_slots.size()) {
            return &inventory.storage_slots[ref.index];
        }
        return nullptr;
    case InventorySlotGroup::Hotbar:
        if (ref.index < hotbar.slots.size()) {
            return &hotbar.slots[ref.index];
        }
        return nullptr;
    default:
        return nullptr;
    }
}

inline constexpr auto inventory_take_from_slot(HotbarSlot& slot, std::uint8_t count) noexcept -> HotbarSlot {
    normalize_item_stack(slot);
    if (!inventory_slot_has_item(slot) || count == 0) {
        return inventory_empty_slot();
    }

    const auto removed_count = static_cast<std::uint8_t>(std::min<std::uint8_t>(slot.count, count));
    HotbarSlot removed {slot.block_id, removed_count};
    slot.count = static_cast<std::uint8_t>(slot.count - removed_count);
    normalize_item_stack(slot);
    return removed;
}

inline constexpr auto inventory_take_from_ref(InventoryMenuState& inventory,
                                              HotbarState& hotbar,
                                              InventorySlotRef ref,
                                              std::uint8_t count) noexcept -> HotbarSlot {
    auto* slot = inventory_slot_ptr(inventory, hotbar, ref);
    if (slot == nullptr) {
        return inventory_empty_slot();
    }
    auto removed = inventory_take_from_slot(*slot, count);
    normalize_inventory_state(inventory, hotbar);
    return removed;
}

inline constexpr void inventory_merge_into_slot(HotbarSlot& target, HotbarSlot& source) noexcept {
    normalize_item_stack(target);
    normalize_item_stack(source);
    if (!inventory_slot_has_item(source)) {
        return;
    }

    if (!inventory_slot_has_item(target)) {
        const auto moved = static_cast<std::uint8_t>(std::min<std::uint8_t>(source.count, kMaxItemStackCount));
        target = {source.block_id, moved};
        source.count = static_cast<std::uint8_t>(source.count - moved);
        normalize_item_stack(source);
        return;
    }

    if (target.block_id != source.block_id || target.count >= kMaxItemStackCount) {
        return;
    }

    const auto space_left = static_cast<std::uint8_t>(kMaxItemStackCount - target.count);
    const auto moved = static_cast<std::uint8_t>(std::min<std::uint8_t>(space_left, source.count));
    target.count = static_cast<std::uint8_t>(target.count + moved);
    source.count = static_cast<std::uint8_t>(source.count - moved);
    normalize_item_stack(source);
}

inline void inventory_try_store_stack_in_slots(std::span<HotbarSlot> slots,
                                               HotbarSlot& stack,
                                               bool matching_only) noexcept {
    if (!inventory_slot_has_item(stack)) {
        return;
    }

    for (auto& slot : slots) {
        normalize_item_stack(slot);
        if (matching_only) {
            if (!inventory_can_merge(slot, stack.block_id)) {
                continue;
            }
        } else if (inventory_slot_has_item(slot)) {
            continue;
        }

        inventory_merge_into_slot(slot, stack);
        if (!inventory_slot_has_item(stack)) {
            return;
        }
    }
}

inline auto inventory_try_store_stack(InventoryMenuState& inventory,
                                      HotbarState& hotbar,
                                      HotbarSlot stack) noexcept -> HotbarSlot {
    normalize_inventory_state(inventory, hotbar);
    normalize_item_stack(stack);
    if (!inventory_slot_has_item(stack)) {
        return inventory_empty_slot();
    }

    inventory_try_store_stack_in_slots(std::span<HotbarSlot>(hotbar.slots), stack, true);
    inventory_try_store_stack_in_slots(std::span<HotbarSlot>(inventory.storage_slots), stack, true);
    inventory_try_store_stack_in_slots(std::span<HotbarSlot>(hotbar.slots), stack, false);
    inventory_try_store_stack_in_slots(std::span<HotbarSlot>(inventory.storage_slots), stack, false);

    normalize_inventory_state(inventory, hotbar);
    normalize_item_stack(stack);
    return stack;
}

inline constexpr void inventory_primary_click(InventoryMenuState& inventory,
                                              HotbarState& hotbar,
                                              InventorySlotRef ref) noexcept {
    auto* slot = inventory_slot_ptr(inventory, hotbar, ref);
    if (slot == nullptr) {
        return;
    }

    normalize_inventory_state(inventory, hotbar);
    if (!inventory.carrying_item && !inventory_slot_has_item(*slot)) {
        return;
    }

    if (!inventory.carrying_item) {
        inventory.carried_slot = *slot;
        *slot = inventory_empty_slot();
    } else if (!inventory_slot_has_item(*slot)) {
        *slot = inventory.carried_slot;
        inventory.carried_slot = inventory_empty_slot();
    } else if (inventory_same_item(*slot, inventory.carried_slot)) {
        inventory_merge_into_slot(*slot, inventory.carried_slot);
    } else {
        std::swap(inventory.carried_slot, *slot);
    }

    normalize_inventory_state(inventory, hotbar);
}

inline constexpr void inventory_pick_or_swap(InventoryMenuState& inventory,
                                             HotbarState& hotbar,
                                             InventorySlotRef ref) noexcept {
    inventory_primary_click(inventory, hotbar, ref);
}

inline constexpr void inventory_secondary_click(InventoryMenuState& inventory,
                                                HotbarState& hotbar,
                                                InventorySlotRef ref) noexcept {
    auto* slot = inventory_slot_ptr(inventory, hotbar, ref);
    if (slot == nullptr) {
        return;
    }

    normalize_inventory_state(inventory, hotbar);
    if (!inventory.carrying_item) {
        if (!inventory_slot_has_item(*slot)) {
            return;
        }

        const auto pickup_count = static_cast<std::uint8_t>((slot->count + 1U) / 2U);
        inventory.carried_slot = inventory_take_from_slot(*slot, pickup_count);
    } else if (!inventory_slot_has_item(*slot)) {
        *slot = inventory_take_from_slot(inventory.carried_slot, 1);
    } else if (inventory_same_item(*slot, inventory.carried_slot) && slot->count < kMaxItemStackCount) {
        ++slot->count;
        inventory.carried_slot.count = static_cast<std::uint8_t>(inventory.carried_slot.count - 1U);
    }

    normalize_inventory_state(inventory, hotbar);
}

inline constexpr void inventory_swap_with_hotbar(InventoryMenuState& inventory,
                                                 HotbarState& hotbar,
                                                 InventorySlotRef ref,
                                                 std::size_t hotbar_index) noexcept {
    if (hotbar_index >= kHotbarSlotCount) {
        return;
    }
    if (ref.group == InventorySlotGroup::Hotbar && ref.index == hotbar_index) {
        return;
    }

    auto* slot = inventory_slot_ptr(inventory, hotbar, ref);
    if (slot == nullptr) {
        return;
    }

    normalize_inventory_state(inventory, hotbar);
    std::swap(*slot, hotbar.slots[hotbar_index]);
    normalize_inventory_state(inventory, hotbar);
}

inline auto make_default_inventory_menu_state() noexcept -> InventoryMenuState {
    InventoryMenuState state {};
    const std::array<HotbarSlot, 18> starter_stacks {{
        inventory_make_slot(to_block_id(BlockType::Wood), 16),
        inventory_make_slot(to_block_id(BlockType::Leaves), 16),
        inventory_make_slot(to_block_id(BlockType::Gravel), 32),
        inventory_make_slot(to_block_id(BlockType::MossyStone), 16),
        inventory_make_slot(to_block_id(BlockType::Snow), 16),
        inventory_make_slot(to_block_id(BlockType::PineWood), 16),
        inventory_make_slot(to_block_id(BlockType::PineLeaves), 16),
        inventory_make_slot(to_block_id(BlockType::TallGrass), 8),
        inventory_make_slot(to_block_id(BlockType::RedFlower), 8),
        inventory_make_slot(to_block_id(BlockType::YellowFlower), 8),
        inventory_make_slot(to_block_id(BlockType::DeadShrub), 8),
        inventory_make_slot(to_block_id(BlockType::Cactus), 16),
        inventory_make_slot(to_block_id(BlockType::Water), 8),
        inventory_make_slot(to_block_id(BlockType::Torch), 16),
        inventory_make_slot(to_block_id(BlockType::Cobblestone), 32),
        inventory_make_slot(to_block_id(BlockType::Planks), 32),
        inventory_make_slot(to_block_id(BlockType::Sand), 32),
        inventory_make_slot(to_block_id(BlockType::Stone), 32),
    }};

    for (std::size_t index = 0; index < starter_stacks.size(); ++index) {
        state.storage_slots[index] = starter_stacks[index];
    }
    for (std::size_t index = starter_stacks.size(); index < state.storage_slots.size(); ++index) {
        state.storage_slots[index] = inventory_empty_slot();
    }
    state.carried_slot = inventory_empty_slot();
    return state;
}

inline auto build_inventory_menu_layout(int viewport_width,
                                        int viewport_height,
                                        const InventoryMenuState& inventory,
                                        const HotbarState& hotbar) -> InventoryMenuLayout {
    const auto layout_width = static_cast<float>(std::max(viewport_width, 1));
    const auto layout_height = static_cast<float>(std::max(viewport_height, 1));
    const auto safe_width = static_cast<float>(std::max(viewport_width, 640));
    const auto safe_height = static_cast<float>(std::max(viewport_height, 360));
    const auto min_dimension = std::min(safe_width, safe_height);

    const auto slot_size = std::clamp(min_dimension * 0.073F, 38.0F, 52.0F);
    const auto slot_gap = std::max(4.0F, slot_size * 0.18F);
    const auto icon_inset = std::max(4.0F, slot_size * 0.18F);
    const auto preview_width = std::clamp(slot_size * 4.1F, 158.0F, 204.0F);
    const auto panel_width = std::clamp(preview_width + slot_size * 9.0F + slot_gap * 10.0F + 96.0F, 620.0F, 820.0F);
    const auto panel_height = std::clamp(slot_size * 4.9F + slot_gap * 8.0F + 150.0F, 430.0F, 580.0F);
    const auto panel_x = (layout_width - panel_width) * 0.5F;
    const auto panel_y = layout_height >= panel_height + 44.0F
                             ? std::max(22.0F, (layout_height - panel_height) * 0.5F)
                             : (layout_height - panel_height) * 0.5F;
    const auto preview_x = panel_x + 24.0F;
    const auto preview_y = panel_y + 78.0F;
    const auto preview_height = panel_height - 132.0F;
    const auto grid_x = preview_x + preview_width + 24.0F;
    const auto grid_y = preview_y + 28.0F;
    const auto grid_width = panel_x + panel_width - 24.0F - grid_x;
    const auto storage_label_y = preview_y - 8.0F;
    const auto hotbar_y = grid_y + static_cast<float>(kInventoryRows) * (slot_size + slot_gap) + 28.0F;
    const auto hotbar_label_y = hotbar_y - 24.0F;

    InventoryMenuLayout layout {};
    layout.panel_x = panel_x;
    layout.panel_y = panel_y;
    layout.panel_width = panel_width;
    layout.panel_height = panel_height;
    layout.title_center_x = panel_x + panel_width * 0.5F;
    layout.title_y = panel_y + 22.0F;
    layout.subtitle_center_x = layout.title_center_x;
    layout.subtitle_y = panel_y + 54.0F;
    layout.preview_x = preview_x;
    layout.preview_y = preview_y;
    layout.preview_width = preview_width;
    layout.preview_height = preview_height;
    layout.preview_center_x = preview_x + preview_width * 0.5F;
    layout.preview_base_y = preview_y + preview_height - 34.0F;
    layout.silhouette_scale = std::clamp(preview_width * 0.10F, 11.0F, 16.0F);
    layout.grid_x = grid_x;
    layout.grid_y = grid_y;
    layout.grid_width = grid_width;
    layout.grid_height = hotbar_y + slot_size - grid_y;
    layout.storage_label_x = grid_x;
    layout.storage_label_y = storage_label_y;
    layout.hotbar_label_x = grid_x;
    layout.hotbar_label_y = hotbar_label_y;
    layout.footer_center_x = layout.title_center_x;
    layout.footer_y = panel_y + panel_height - 30.0F;
    layout.slot_size = slot_size;
    layout.slot_gap = slot_gap;
    layout.icon_inset = icon_inset;

    for (std::size_t row = 0; row < kInventoryRows; ++row) {
        for (std::size_t column = 0; column < kInventoryColumns; ++column) {
            const auto index = row * kInventoryColumns + column;
            auto& slot = layout.slots[index];
            slot.ref = {InventorySlotGroup::Storage, index};
            slot.slot = inventory.storage_slots[index];
            slot.icon_tile = inventory_slot_icon_tile(slot.slot.block_id);
            slot.x = grid_x + static_cast<float>(column) * (slot_size + slot_gap);
            slot.y = grid_y + static_cast<float>(row) * (slot_size + slot_gap);
            slot.size = slot_size;
            slot.hovered =
                inventory.cursor_x >= slot.x &&
                inventory.cursor_x <= slot.x + slot.size &&
                inventory.cursor_y >= slot.y &&
                inventory.cursor_y <= slot.y + slot.size;
            slot.has_icon = inventory_slot_has_item(slot.slot);
        }
    }

    for (std::size_t column = 0; column < kHotbarSlotCount; ++column) {
        const auto layout_index = kInventoryStorageSlotCount + column;
        auto& slot = layout.slots[layout_index];
        slot.ref = {InventorySlotGroup::Hotbar, column};
        slot.slot = hotbar.slots[column];
        slot.icon_tile = inventory_slot_icon_tile(slot.slot.block_id);
        slot.x = grid_x + static_cast<float>(column) * (slot_size + slot_gap);
        slot.y = hotbar_y;
        slot.size = slot_size;
        slot.hovered =
            inventory.cursor_x >= slot.x &&
            inventory.cursor_x <= slot.x + slot.size &&
            inventory.cursor_y >= slot.y &&
            inventory.cursor_y <= slot.y + slot.size;
        slot.is_hotbar = true;
        slot.is_selected_hotbar = column == hotbar.selected_index;
        slot.has_icon = inventory_slot_has_item(slot.slot);
    }

    return layout;
}

inline auto inventory_slot_at(const InventoryMenuLayout& layout, float cursor_x, float cursor_y) -> std::optional<InventorySlotRef> {
    for (const auto& slot : layout.slots) {
        if (cursor_x >= slot.x &&
            cursor_x <= slot.x + slot.size &&
            cursor_y >= slot.y &&
            cursor_y <= slot.y + slot.size) {
            return slot.ref;
        }
    }
    return std::nullopt;
}

} // namespace valcraft
