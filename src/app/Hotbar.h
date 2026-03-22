#pragma once

#include "world/Block.h"

#include <array>
#include <cstddef>
#include <optional>

namespace valcraft {

constexpr std::size_t kHotbarSlotCount = 9;

struct HotbarSlot {
    BlockId block_id = to_block_id(BlockType::Air);
    bool is_empty_utility = false;
};

struct HotbarState {
    std::array<HotbarSlot, kHotbarSlotCount> slots {};
    std::size_t selected_index = 0;

    [[nodiscard]] constexpr auto selected_slot() const noexcept -> const HotbarSlot& {
        return slots[selected_index];
    }
};

inline auto make_default_hotbar_state() noexcept -> HotbarState {
    HotbarState hotbar {};
    hotbar.slots = {{
        {to_block_id(BlockType::Grass), false},
        {to_block_id(BlockType::Dirt), false},
        {to_block_id(BlockType::Stone), false},
        {to_block_id(BlockType::Sand), false},
        {to_block_id(BlockType::Wood), false},
        {to_block_id(BlockType::Leaves), false},
        {to_block_id(BlockType::Torch), false},
        {to_block_id(BlockType::Air), true},
        {to_block_id(BlockType::Air), true},
    }};
    hotbar.selected_index = 0;
    return hotbar;
}

inline constexpr auto selected_hotbar_block(const HotbarState& state) noexcept -> BlockId {
    return state.selected_slot().block_id;
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
