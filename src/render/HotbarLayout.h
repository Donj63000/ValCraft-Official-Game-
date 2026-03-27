#pragma once

#include "app/Hotbar.h"
#include "world/BlockVisuals.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace valcraft {

using HotbarAtlasTile = BlockAtlasTile;
inline constexpr std::size_t kHudVitalGlyphCount = 10U;

enum class HudGlyphFill : std::uint8_t {
    Empty = 0,
    Half = 1,
    Full = 2,
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

struct GameplayHudSlotLayout {
    float x = 0.0F;
    float bottom = 0.0F;
    float size = 0.0F;
    float icon_x = 0.0F;
    float icon_bottom = 0.0F;
    float icon_size = 0.0F;
    float count_right_x = 0.0F;
    float count_bottom = 0.0F;
    bool is_selected = false;
    bool has_icon = false;
    bool show_stack_count = false;
    HotbarSlot slot {};
    HotbarAtlasTile icon_tile {};
};

struct VitalGlyphLayout {
    float x = 0.0F;
    float bottom = 0.0F;
    float size = 0.0F;
    HudGlyphFill fill = HudGlyphFill::Empty;
};

struct GameplayHudLabelLayout {
    float center_x = 0.0F;
    float bottom = 0.0F;
    float pixel_size = 0.0F;
    float height = 0.0F;
};

// Gameplay HUD layout intentionally keeps the same bottom-origin convention as
// build_hotbar_layout(). The renderer converts to top-left explicitly only when
// a helper requires it.
struct GameplayHudLayout {
    HotbarLayout hotbar {};
    float safe_margin = 0.0F;
    float hotbar_top = 0.0F;
    float hotbar_panel_x = 0.0F;
    float hotbar_panel_bottom = 0.0F;
    float hotbar_panel_width = 0.0F;
    float hotbar_panel_height = 0.0F;
    float vitals_bottom = 0.0F;
    float cluster_bottom = 0.0F;
    float cluster_top = 0.0F;
    bool air_visible = false;
    GameplayHudLabelLayout label {};
    std::array<GameplayHudSlotLayout, kHotbarSlotCount> slots {};
    std::array<VitalGlyphLayout, kHudVitalGlyphCount> hearts {};
    std::array<VitalGlyphLayout, kHudVitalGlyphCount> bubbles {};
};

inline constexpr auto hotbar_slot_has_icon(const HotbarSlot& slot) noexcept -> bool {
    return hotbar_slot_has_item(slot);
}

inline constexpr auto hotbar_icon_tile(BlockId block_id) noexcept -> HotbarAtlasTile {
    return block_hotbar_tile(block_id);
}

inline constexpr auto gameplay_hud_stack_count_visible(const HotbarSlot& slot) noexcept -> bool {
    return hotbar_slot_has_item(slot) && slot.count > 1;
}

inline constexpr auto gameplay_hud_slot_top(const GameplayHudSlotLayout& slot) noexcept -> float {
    return slot.bottom + slot.size;
}

inline constexpr auto gameplay_hud_vital_top(const VitalGlyphLayout& glyph) noexcept -> float {
    return glyph.bottom + glyph.size;
}

inline constexpr auto gameplay_hud_label_top(const GameplayHudLabelLayout& label) noexcept -> float {
    return label.bottom + label.height;
}

template <std::size_t GlyphCount>
inline auto build_vital_glyph_fills(float value, float max_value) -> std::array<HudGlyphFill, GlyphCount> {
    std::array<HudGlyphFill, GlyphCount> fills {};
    if (max_value <= 0.0F) {
        fills.fill(HudGlyphFill::Empty);
        return fills;
    }

    const auto normalized = std::clamp(value / max_value, 0.0F, 1.0F);
    const auto filled_units = normalized * static_cast<float>(GlyphCount) * 2.0F;
    for (std::size_t index = 0; index < GlyphCount; ++index) {
        const auto units = std::clamp(filled_units - static_cast<float>(index) * 2.0F, 0.0F, 2.0F);
        fills[index] = units >= 1.5F ? HudGlyphFill::Full : (units >= 0.5F ? HudGlyphFill::Half : HudGlyphFill::Empty);
    }
    return fills;
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

inline auto build_gameplay_hud_layout(int viewport_width,
                                      int viewport_height,
                                      const HotbarState& state,
                                      float health,
                                      float max_health,
                                      float air_seconds,
                                      float max_air_seconds,
                                      bool air_visible) -> GameplayHudLayout {
    const auto hotbar = build_hotbar_layout(viewport_width, viewport_height, state);
    const auto slot_raise = std::max(2.0F, static_cast<float>(std::floor(hotbar.slot_size * 0.08F)));
    const auto panel_padding_x = std::max(5.0F, static_cast<float>(std::floor(hotbar.slot_size * 0.12F)));
    const auto panel_padding_y = std::max(4.0F, static_cast<float>(std::floor(hotbar.slot_size * 0.10F)));
    const auto hotbar_top = hotbar.bar_bottom + hotbar.bar_height;
    const auto panel_x = hotbar.bar_left - panel_padding_x;
    const auto panel_bottom = hotbar.bar_bottom - panel_padding_y;
    const auto panel_width = hotbar.bar_width + panel_padding_x * 2.0F;
    const auto panel_height = hotbar.bar_height + panel_padding_y * 2.0F;
    const auto panel_top = panel_bottom + panel_height;

    const auto vital_size = std::clamp(static_cast<float>(std::floor(hotbar.slot_size * 0.30F)), 8.0F, 16.0F);
    const auto vital_gap = std::max(1.0F, static_cast<float>(std::floor(vital_size * 0.18F)));
    const auto vital_row_width =
        static_cast<float>(kHudVitalGlyphCount) * vital_size + static_cast<float>(kHudVitalGlyphCount - 1U) * vital_gap;
    const auto vitals_gap = std::max(8.0F, static_cast<float>(std::floor(vital_size * 0.85F)));
    const auto vitals_bottom = panel_top + vitals_gap;
    const auto left_cluster_center = hotbar.bar_left + hotbar.bar_width * 0.25F;
    const auto right_cluster_center = hotbar.bar_left + hotbar.bar_width * 0.75F;
    const auto hearts_x = left_cluster_center - vital_row_width * 0.5F;
    const auto bubbles_x = right_cluster_center - vital_row_width * 0.5F;

    constexpr float label_pixel_size = 2.0F;
    constexpr float label_height = label_pixel_size * 7.0F;
    const auto label_gap = std::max(6.0F, static_cast<float>(std::floor(vital_size * 0.50F)));
    const auto label_bottom = vitals_bottom + vital_size + label_gap;

    GameplayHudLayout layout {};
    layout.hotbar = hotbar;
    layout.safe_margin = hotbar.safe_margin;
    layout.hotbar_top = hotbar_top;
    layout.hotbar_panel_x = panel_x;
    layout.hotbar_panel_bottom = panel_bottom;
    layout.hotbar_panel_width = panel_width;
    layout.hotbar_panel_height = panel_height;
    layout.vitals_bottom = vitals_bottom;
    layout.cluster_bottom = panel_bottom;
    layout.cluster_top = label_bottom + label_height;
    layout.air_visible = air_visible;
    layout.label.center_x = hotbar.bar_left + hotbar.bar_width * 0.5F;
    layout.label.bottom = label_bottom;
    layout.label.pixel_size = label_pixel_size;
    layout.label.height = label_height;

    const auto heart_fills = build_vital_glyph_fills<kHudVitalGlyphCount>(health, max_health);
    const auto bubble_fills = build_vital_glyph_fills<kHudVitalGlyphCount>(air_seconds, max_air_seconds);

    for (std::size_t index = 0; index < kHotbarSlotCount; ++index) {
        const auto& slot = hotbar.slots[index];
        auto& gameplay_slot = layout.slots[index];
        const auto slot_bottom = slot.y + (slot.is_selected ? slot_raise : 0.0F);
        const auto icon_size = std::max(8.0F, slot.size - hotbar.icon_inset * 2.0F);
        const auto icon_offset = (slot.size - icon_size) * 0.5F;

        gameplay_slot.x = slot.x;
        gameplay_slot.bottom = slot_bottom;
        gameplay_slot.size = slot.size;
        gameplay_slot.icon_x = slot.x + icon_offset;
        gameplay_slot.icon_bottom = slot_bottom + icon_offset;
        gameplay_slot.icon_size = icon_size;
        gameplay_slot.count_right_x = slot.x + slot.size - 5.0F;
        gameplay_slot.count_bottom = slot_bottom + slot.size - 4.0F;
        gameplay_slot.is_selected = slot.is_selected;
        gameplay_slot.has_icon = slot.has_icon;
        gameplay_slot.show_stack_count = gameplay_hud_stack_count_visible(slot.slot);
        gameplay_slot.slot = slot.slot;
        gameplay_slot.icon_tile = slot.icon_tile;
    }

    for (std::size_t index = 0; index < kHudVitalGlyphCount; ++index) {
        auto& heart = layout.hearts[index];
        heart.x = hearts_x + static_cast<float>(index) * (vital_size + vital_gap);
        heart.bottom = vitals_bottom;
        heart.size = vital_size;
        heart.fill = heart_fills[index];

        auto& bubble = layout.bubbles[index];
        bubble.x = bubbles_x + static_cast<float>(index) * (vital_size + vital_gap);
        bubble.bottom = vitals_bottom;
        bubble.size = vital_size;
        bubble.fill = bubble_fills[index];
    }

    return layout;
}

} // namespace valcraft
