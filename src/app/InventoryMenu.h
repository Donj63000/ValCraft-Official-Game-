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

struct InventoryKeycapLayout {
    float x = 0.0F;
    float y = 0.0F;
    float width = 0.0F;
    float height = 0.0F;
    std::uint8_t number = 0;
    bool selected = false;
};

struct InventoryMenuLayout {
    float panel_x = 0.0F;
    float panel_y = 0.0F;
    float panel_width = 0.0F;
    float panel_height = 0.0F;
    float header_panel_x = 0.0F;
    float header_panel_y = 0.0F;
    float header_panel_width = 0.0F;
    float header_panel_height = 0.0F;
    float body_x = 0.0F;
    float body_y = 0.0F;
    float body_width = 0.0F;
    float body_height = 0.0F;
    float footer_panel_x = 0.0F;
    float footer_panel_y = 0.0F;
    float footer_panel_width = 0.0F;
    float footer_panel_height = 0.0F;
    float title_center_x = 0.0F;
    float title_y = 0.0F;
    float subtitle_center_x = 0.0F;
    float subtitle_y = 0.0F;
    float preview_panel_x = 0.0F;
    float preview_panel_y = 0.0F;
    float preview_panel_width = 0.0F;
    float preview_panel_height = 0.0F;
    float detail_panel_x = 0.0F;
    float detail_panel_y = 0.0F;
    float detail_panel_width = 0.0F;
    float detail_panel_height = 0.0F;
    float storage_panel_x = 0.0F;
    float storage_panel_y = 0.0F;
    float storage_panel_width = 0.0F;
    float storage_panel_height = 0.0F;
    float hotbar_panel_x = 0.0F;
    float hotbar_panel_y = 0.0F;
    float hotbar_panel_width = 0.0F;
    float hotbar_panel_height = 0.0F;
    bool compact_detail = false;
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
    float detail_label_x = 0.0F;
    float detail_label_y = 0.0F;
    float footer_center_x = 0.0F;
    float footer_y = 0.0F;
    float slot_size = 0.0F;
    float slot_gap = 0.0F;
    float icon_inset = 0.0F;
    std::array<InventoryKeycapLayout, kHotbarSlotCount> hotbar_keycaps {};
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

    float slot_size = std::clamp(min_dimension * 0.067F, 30.0F, 50.0F);
    float slot_gap = 0.0F;
    float icon_inset = 0.0F;
    float panel_padding_x = 0.0F;
    float panel_padding_y = 0.0F;
    float header_height = 0.0F;
    float footer_height = 0.0F;
    float section_gap = 0.0F;
    float section_padding = 0.0F;
    float section_label_height = 0.0F;
    float keycap_band_height = 0.0F;
    float preview_column_width = 0.0F;
    float detail_width = 0.0F;
    float storage_slots_width = 0.0F;
    float storage_slots_height = 0.0F;
    float storage_panel_height = 0.0F;
    float hotbar_panel_height = 0.0F;
    float grid_column_height = 0.0F;
    float compact_preview_height = 0.0F;
    float compact_detail_height = 0.0F;
    float panel_width = 0.0F;
    float panel_height = 0.0F;
    bool compact_detail = false;

    for (int pass = 0; pass < 10; ++pass) {
        slot_gap = std::max(4.0F, slot_size * 0.16F);
        icon_inset = std::max(4.0F, slot_size * 0.16F);
        panel_padding_x = std::max(18.0F, slot_size * 0.50F);
        panel_padding_y = std::max(16.0F, slot_size * 0.42F);
        section_gap = std::max(14.0F, slot_size * 0.34F);
        section_padding = std::max(10.0F, slot_size * 0.24F);
        section_label_height = std::max(10.0F, slot_size * 0.24F);
        keycap_band_height = std::max(16.0F, slot_size * 0.46F);
        header_height = std::clamp(slot_size * 1.55F, 64.0F, 84.0F);
        footer_height = std::clamp(slot_size * 0.98F, 40.0F, 56.0F);
        preview_column_width = std::clamp(slot_size * 4.50F, 150.0F, 224.0F);
        detail_width = std::clamp(slot_size * 3.35F, 150.0F, 184.0F);
        storage_slots_width =
            static_cast<float>(kInventoryColumns) * slot_size +
            static_cast<float>(kInventoryColumns - 1U) * slot_gap;
        storage_slots_height =
            static_cast<float>(kInventoryRows) * slot_size +
            static_cast<float>(kInventoryRows - 1U) * slot_gap;
        storage_panel_height = section_padding * 2.0F + section_label_height + storage_slots_height;
        hotbar_panel_height = section_padding * 2.0F + section_label_height + keycap_band_height + slot_size;
        grid_column_height = storage_panel_height + section_gap + hotbar_panel_height;

        const auto compact_preview_min = slot_size * 3.70F;
        const auto compact_detail_min = slot_size * 2.45F;
        compact_preview_height = std::clamp(
            grid_column_height * 0.56F,
            compact_preview_min,
            std::max(compact_preview_min, grid_column_height - section_gap - compact_detail_min));
        compact_detail_height = std::max(compact_detail_min, grid_column_height - compact_preview_height - section_gap);

        const auto header_gap = std::max(10.0F, slot_size * 0.26F);
        const auto footer_gap = std::max(10.0F, slot_size * 0.22F);
        const auto wide_width =
            panel_padding_x * 2.0F +
            preview_column_width +
            section_gap +
            storage_slots_width +
            section_padding * 2.0F +
            section_gap +
            detail_width;
        const auto compact_width =
            panel_padding_x * 2.0F +
            preview_column_width +
            section_gap +
            storage_slots_width +
            section_padding * 2.0F;

        compact_detail = wide_width > layout_width - 24.0F;
        panel_width = compact_detail ? compact_width : wide_width;
        panel_height =
            panel_padding_y * 2.0F +
            header_height +
            header_gap +
            grid_column_height +
            footer_gap +
            footer_height;

        if ((panel_width <= layout_width - 20.0F && panel_height <= layout_height - 20.0F) || slot_size <= 30.0F) {
            break;
        }
        slot_size = std::max(30.0F, slot_size - 2.0F);
    }

    const auto header_gap = std::max(10.0F, slot_size * 0.26F);
    const auto footer_gap = std::max(10.0F, slot_size * 0.22F);
    const auto panel_x = (layout_width - panel_width) * 0.5F;
    const auto panel_y = layout_height >= panel_height + 28.0F
                             ? std::max(14.0F, (layout_height - panel_height) * 0.5F)
                             : std::max(8.0F, (layout_height - panel_height) * 0.5F);
    const auto header_panel_x = panel_x + 8.0F;
    const auto header_panel_y = panel_y + 8.0F;
    const auto header_panel_width = std::max(0.0F, panel_width - 16.0F);
    const auto header_panel_height = header_height;
    const auto body_x = panel_x + panel_padding_x;
    const auto body_y = header_panel_y + header_panel_height + header_gap;
    const auto body_width = std::max(0.0F, panel_width - panel_padding_x * 2.0F);
    const auto body_height = grid_column_height;
    const auto footer_panel_x = panel_x + 10.0F;
    const auto footer_panel_y = body_y + body_height + footer_gap;
    const auto footer_panel_width = std::max(0.0F, panel_width - 20.0F);
    const auto footer_panel_height = footer_height;

    const auto preview_panel_x = body_x;
    const auto preview_panel_y = body_y;
    const auto preview_panel_width = preview_column_width;
    const auto preview_panel_height = compact_detail ? compact_preview_height : body_height;
    const auto storage_panel_x = body_x + preview_column_width + section_gap;
    const auto storage_panel_y = body_y;
    const auto storage_panel_width = storage_slots_width + section_padding * 2.0F;
    const auto hotbar_panel_x = storage_panel_x;
    const auto hotbar_panel_y = storage_panel_y + storage_panel_height + section_gap;
    const auto hotbar_panel_width = storage_panel_width;
    const auto detail_panel_x = compact_detail
                                    ? body_x
                                    : hotbar_panel_x + hotbar_panel_width + section_gap;
    const auto detail_panel_y = compact_detail ? preview_panel_y + preview_panel_height + section_gap : body_y;
    const auto detail_panel_width = compact_detail ? preview_column_width : detail_width;
    const auto detail_panel_height = compact_detail ? compact_detail_height : body_height;

    const auto grid_x = storage_panel_x + section_padding;
    const auto grid_y = storage_panel_y + section_padding + section_label_height;
    const auto hotbar_slots_x = hotbar_panel_x + section_padding;
    const auto hotbar_slots_y = hotbar_panel_y + section_padding + section_label_height + keycap_band_height;
    const auto keycap_width = std::clamp(slot_size * 0.38F, 14.0F, 20.0F);
    const auto keycap_height = std::clamp(slot_size * 0.30F, 10.0F, 16.0F);
    const auto keycap_y = hotbar_panel_y + section_padding + section_label_height + (keycap_band_height - keycap_height) * 0.5F;

    const auto preview_inner_padding = std::max(12.0F, slot_size * 0.28F);
    const auto preview_inner_height = std::max(0.0F, preview_panel_height - preview_inner_padding * 2.0F);
    const auto preview_inner_y = preview_panel_y + preview_inner_padding;

    InventoryMenuLayout layout {};
    layout.panel_x = panel_x;
    layout.panel_y = panel_y;
    layout.panel_width = panel_width;
    layout.panel_height = panel_height;
    layout.header_panel_x = header_panel_x;
    layout.header_panel_y = header_panel_y;
    layout.header_panel_width = header_panel_width;
    layout.header_panel_height = header_panel_height;
    layout.body_x = body_x;
    layout.body_y = body_y;
    layout.body_width = body_width;
    layout.body_height = body_height;
    layout.footer_panel_x = footer_panel_x;
    layout.footer_panel_y = footer_panel_y;
    layout.footer_panel_width = footer_panel_width;
    layout.footer_panel_height = footer_panel_height;
    layout.title_center_x = panel_x + panel_width * 0.5F;
    layout.title_y = header_panel_y + 16.0F;
    layout.subtitle_center_x = layout.title_center_x;
    layout.subtitle_y = header_panel_y + header_panel_height - 24.0F;
    layout.preview_panel_x = preview_panel_x;
    layout.preview_panel_y = preview_panel_y;
    layout.preview_panel_width = preview_panel_width;
    layout.preview_panel_height = preview_panel_height;
    layout.detail_panel_x = detail_panel_x;
    layout.detail_panel_y = detail_panel_y;
    layout.detail_panel_width = detail_panel_width;
    layout.detail_panel_height = detail_panel_height;
    layout.storage_panel_x = storage_panel_x;
    layout.storage_panel_y = storage_panel_y;
    layout.storage_panel_width = storage_panel_width;
    layout.storage_panel_height = storage_panel_height;
    layout.hotbar_panel_x = hotbar_panel_x;
    layout.hotbar_panel_y = hotbar_panel_y;
    layout.hotbar_panel_width = hotbar_panel_width;
    layout.hotbar_panel_height = hotbar_panel_height;
    layout.compact_detail = compact_detail;
    layout.preview_x = preview_panel_x;
    layout.preview_y = preview_panel_y;
    layout.preview_width = preview_panel_width;
    layout.preview_height = preview_panel_height;
    layout.preview_center_x = preview_panel_x + preview_panel_width * 0.5F;
    layout.preview_base_y = preview_inner_y + preview_inner_height - std::max(18.0F, slot_size * 0.52F);
    layout.silhouette_scale = std::clamp(preview_panel_width * 0.078F, 9.0F, 15.0F);
    layout.grid_x = grid_x;
    layout.grid_y = grid_y;
    layout.grid_width = storage_slots_width;
    layout.grid_height = body_height;
    layout.storage_label_x = storage_panel_x + section_padding;
    layout.storage_label_y = storage_panel_y + 10.0F;
    layout.hotbar_label_x = hotbar_panel_x + section_padding;
    layout.hotbar_label_y = hotbar_panel_y + 10.0F;
    layout.detail_label_x = detail_panel_x + std::max(10.0F, slot_size * 0.22F);
    layout.detail_label_y = detail_panel_y + 10.0F;
    layout.footer_center_x = panel_x + panel_width * 0.5F;
    layout.footer_y = footer_panel_y + std::max(8.0F, footer_panel_height * 0.5F - 7.0F);
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
        slot.x = hotbar_slots_x + static_cast<float>(column) * (slot_size + slot_gap);
        slot.y = hotbar_slots_y;
        slot.size = slot_size;
        slot.hovered =
            inventory.cursor_x >= slot.x &&
            inventory.cursor_x <= slot.x + slot.size &&
            inventory.cursor_y >= slot.y &&
            inventory.cursor_y <= slot.y + slot.size;
        slot.is_hotbar = true;
        slot.is_selected_hotbar = column == hotbar.selected_index;
        slot.has_icon = inventory_slot_has_item(slot.slot);

        auto& keycap = layout.hotbar_keycaps[column];
        keycap.x = slot.x + (slot_size - keycap_width) * 0.5F;
        keycap.y = keycap_y;
        keycap.width = keycap_width;
        keycap.height = keycap_height;
        keycap.number = static_cast<std::uint8_t>(column + 1U);
        keycap.selected = slot.is_selected_hotbar;
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
