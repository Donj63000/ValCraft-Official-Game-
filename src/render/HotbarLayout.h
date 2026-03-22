#pragma once

#include "app/Hotbar.h"

#include <algorithm>
#include <array>

namespace valcraft {

struct HotbarAtlasTile {
    int x = 0;
    int y = 0;

    auto operator==(const HotbarAtlasTile&) const -> bool = default;
};

struct HotbarSlotLayout {
    float x = 0.0F;
    float y = 0.0F;
    float size = 0.0F;
    bool is_selected = false;
    bool has_icon = false;
    HotbarSlot slot {};
    HotbarAtlasTile icon_tile {};
};

struct HotbarLayout {
    float bar_left = 0.0F;
    float bar_bottom = 0.0F;
    float bar_width = 0.0F;
    float bar_height = 0.0F;
    float slot_size = 0.0F;
    float slot_gap = 0.0F;
    float safe_margin = 0.0F;
    float icon_inset = 0.0F;
    std::array<HotbarSlotLayout, kHotbarSlotCount> slots {};
};

inline constexpr auto hotbar_slot_has_icon(const HotbarSlot& slot) noexcept -> bool {
    return !slot.is_empty_utility && slot.block_id != to_block_id(BlockType::Air);
}

inline constexpr auto hotbar_icon_tile(BlockId block_id) noexcept -> HotbarAtlasTile {
    switch (static_cast<BlockType>(block_id)) {
    case BlockType::Grass:
        return {1, 0};
    case BlockType::Dirt:
        return {2, 0};
    case BlockType::Stone:
        return {3, 0};
    case BlockType::Sand:
        return {0, 1};
    case BlockType::Wood:
        return {1, 1};
    case BlockType::Leaves:
        return {3, 1};
    case BlockType::Torch:
        return {0, 2};
    case BlockType::Air:
    default:
        return {0, 0};
    }
}

inline auto build_hotbar_layout(int viewport_width, int viewport_height, const HotbarState& state) -> HotbarLayout {
    constexpr float gap_ratio = 0.12F;
    constexpr float bar_padding_x_ratio = 0.18F;
    constexpr float bar_padding_y_ratio = 0.16F;
    constexpr float icon_inset_ratio = 0.18F;

    const auto width = static_cast<float>(std::max(viewport_width, 1));
    const auto height = static_cast<float>(std::max(viewport_height, 1));
    const auto min_dimension = std::min(width, height);
    const auto safe_margin = std::max(16.0F, min_dimension * 0.02F);
    const auto preferred_slot_size = std::clamp(height * 0.065F, 40.0F, 58.0F);
    const auto available_width = std::max(width - safe_margin * 2.0F, preferred_slot_size);
    const auto slot_size = std::max(
        24.0F,
        std::min(
            preferred_slot_size,
            available_width / (static_cast<float>(kHotbarSlotCount) +
                               static_cast<float>(kHotbarSlotCount - 1) * gap_ratio +
                               bar_padding_x_ratio * 2.0F)));
    const auto slot_gap = std::max(4.0F, slot_size * gap_ratio);
    const auto bar_padding_x = std::max(6.0F, slot_size * bar_padding_x_ratio);
    const auto bar_padding_y = std::max(6.0F, slot_size * bar_padding_y_ratio);
    const auto bar_width =
        static_cast<float>(kHotbarSlotCount) * slot_size +
        static_cast<float>(kHotbarSlotCount - 1) * slot_gap +
        bar_padding_x * 2.0F;
    const auto bar_height = slot_size + bar_padding_y * 2.0F;
    const auto bar_left = (width - bar_width) * 0.5F;
    const auto bar_bottom = safe_margin;
    const auto icon_inset = std::max(4.0F, slot_size * icon_inset_ratio);

    HotbarLayout layout {};
    layout.bar_left = bar_left;
    layout.bar_bottom = bar_bottom;
    layout.bar_width = bar_width;
    layout.bar_height = bar_height;
    layout.slot_size = slot_size;
    layout.slot_gap = slot_gap;
    layout.safe_margin = safe_margin;
    layout.icon_inset = icon_inset;

    for (std::size_t index = 0; index < kHotbarSlotCount; ++index) {
        auto& slot = layout.slots[index];
        slot.x = bar_left + bar_padding_x + static_cast<float>(index) * (slot_size + slot_gap);
        slot.y = bar_bottom + bar_padding_y;
        slot.size = slot_size;
        slot.is_selected = index == state.selected_index;
        slot.slot = state.slots[index];
        slot.has_icon = hotbar_slot_has_icon(slot.slot);
        slot.icon_tile = hotbar_icon_tile(slot.slot.block_id);
    }

    return layout;
}

} // namespace valcraft
