#pragma once

#include "world/Block.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace valcraft {

constexpr std::size_t kHotbarSlotCount = 9;
constexpr std::uint8_t kMaxItemStackCount = 64;

struct HotbarSlot {
    BlockId block_id = to_block_id(BlockType::Air);
    std::uint8_t count = 0;

    auto operator==(const HotbarSlot&) const -> bool = default;
};

struct HotbarState {
    std::array<HotbarSlot, kHotbarSlotCount> slots {};
    std::size_t selected_index = 0;

    auto operator==(const HotbarState&) const -> bool = default;

    [[nodiscard]] constexpr auto selected_slot() const noexcept -> const HotbarSlot& {
        return slots[selected_index];
    }
};

inline constexpr auto make_item_stack(BlockId block_id, std::uint8_t count) noexcept -> HotbarSlot {
    if (block_id == to_block_id(BlockType::Air) || count == 0) {
        return {};
    }
    return {block_id, static_cast<std::uint8_t>(count > kMaxItemStackCount ? kMaxItemStackCount : count)};
}

inline constexpr auto empty_item_stack() noexcept -> HotbarSlot {
    return {};
}

inline constexpr auto hotbar_slot_has_item(const HotbarSlot& slot) noexcept -> bool {
    return slot.block_id != to_block_id(BlockType::Air) && slot.count > 0;
}

inline constexpr void normalize_item_stack(HotbarSlot& slot) noexcept {
    if (!hotbar_slot_has_item(slot)) {
        slot = {};
        return;
    }
    if (slot.count > kMaxItemStackCount) {
        slot.count = kMaxItemStackCount;
    }
}

inline auto make_default_hotbar_state() noexcept -> HotbarState {
    HotbarState hotbar {};
    hotbar.slots = {{
        make_item_stack(to_block_id(BlockType::Grass), 32),
        make_item_stack(to_block_id(BlockType::Dirt), 32),
        make_item_stack(to_block_id(BlockType::Stone), 32),
        make_item_stack(to_block_id(BlockType::Cobblestone), 32),
        make_item_stack(to_block_id(BlockType::Sand), 32),
        make_item_stack(to_block_id(BlockType::Planks), 32),
        make_item_stack(to_block_id(BlockType::Torch), 16),
        make_item_stack(to_block_id(BlockType::Water), 8),
        empty_item_stack(),
    }};
    hotbar.selected_index = 0;
    return hotbar;
}

inline constexpr auto selected_hotbar_block(const HotbarState& state) noexcept -> BlockId {
    return hotbar_slot_has_item(state.selected_slot())
               ? state.selected_slot().block_id
               : to_block_id(BlockType::Air);
}

inline constexpr auto hotbar_index_from_number_key(int number_key) noexcept -> std::optional<std::size_t> {
    if (number_key < 1 || number_key > static_cast<int>(kHotbarSlotCount)) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(number_key - 1);
}

inline constexpr auto normalize_hotbar_index(std::size_t index) noexcept -> std::size_t {
    return index < kHotbarSlotCount ? index : 0;
}

inline constexpr void select_hotbar_index(HotbarState& state, std::size_t index) noexcept {
    state.selected_index = normalize_hotbar_index(index);
}

inline constexpr auto cycle_hotbar_index(std::size_t current_index, int delta) noexcept -> std::size_t {
    constexpr auto slot_count = static_cast<int>(kHotbarSlotCount);
    const auto current = static_cast<int>(normalize_hotbar_index(current_index));
    const auto wrapped = (current + (delta % slot_count) + slot_count) % slot_count;
    return static_cast<std::size_t>(wrapped);
}

inline constexpr void cycle_hotbar_selection(HotbarState& state, int delta) noexcept {
    state.selected_index = cycle_hotbar_index(state.selected_index, delta);
}

} // namespace valcraft
