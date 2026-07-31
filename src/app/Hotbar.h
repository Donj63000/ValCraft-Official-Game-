#pragma once

#include "world/Block.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace valcraft {

constexpr std::size_t kHotbarSlotCount = 9;
constexpr std::uint8_t kMaxItemStackCount = 64;
constexpr std::uint8_t kMusketLoadedInstanceState = static_cast<std::uint8_t>(1U << 0U);

inline constexpr auto max_item_stack_count(BlockId block_id) noexcept -> std::uint8_t {
    return is_inventory_only_item(block_id) ? static_cast<std::uint8_t>(1U) : kMaxItemStackCount;
}

struct HotbarSlot {
    BlockId block_id = to_block_id(BlockType::Air);
    std::uint8_t count = 0;
    std::uint8_t instance_state = 0;

    auto operator==(const HotbarSlot&) const -> bool = default;
};

struct HotbarState {
    std::array<HotbarSlot, kHotbarSlotCount> slots {};
    std::size_t selected_index = 0;

    auto operator==(const HotbarState&) const -> bool = default;

    [[nodiscard]] constexpr auto selected_slot() const noexcept -> const HotbarSlot& {
        return slots[selected_index < slots.size() ? selected_index : 0U];
    }
};

inline constexpr auto default_item_instance_state(BlockId block_id) noexcept -> std::uint8_t {
    return block_item_id(block_id) == to_block_id(BlockType::Musket)
               ? kMusketLoadedInstanceState
               : static_cast<std::uint8_t>(0U);
}

inline constexpr auto sanitized_item_instance_state(BlockId block_id,
                                                    std::uint8_t instance_state) noexcept -> std::uint8_t {
    return block_item_id(block_id) == to_block_id(BlockType::Musket)
               ? static_cast<std::uint8_t>(instance_state & kMusketLoadedInstanceState)
               : static_cast<std::uint8_t>(0U);
}

inline constexpr auto make_item_stack_with_state(BlockId block_id,
                                                 std::uint8_t count,
                                                 std::uint8_t instance_state) noexcept -> HotbarSlot {
    block_id = block_item_id(block_id);
    if (block_id == to_block_id(BlockType::Air) || count == 0) {
        return {};
    }
    const auto max_count = max_item_stack_count(block_id);
    return {
        block_id,
        static_cast<std::uint8_t>(count > max_count ? max_count : count),
        sanitized_item_instance_state(block_id, instance_state),
    };
}

inline constexpr auto make_item_stack(BlockId block_id, std::uint8_t count) noexcept -> HotbarSlot {
    return make_item_stack_with_state(
        block_id,
        count,
        default_item_instance_state(block_id));
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
    slot.block_id = block_item_id(slot.block_id);
    if (slot.block_id == to_block_id(BlockType::Air)) {
        slot = {};
        return;
    }
    const auto max_count = max_item_stack_count(slot.block_id);
    if (slot.count > max_count) {
        slot.count = max_count;
    }
    slot.instance_state =
        sanitized_item_instance_state(
            slot.block_id,
            slot.instance_state);
}

inline constexpr auto is_musket_item(const HotbarSlot& slot) noexcept -> bool {
    return hotbar_slot_has_item(slot) &&
           block_item_id(slot.block_id) == to_block_id(BlockType::Musket);
}

inline constexpr auto is_legendary_weapon_item(
    const HotbarSlot& slot) noexcept -> bool {
    return hotbar_slot_has_item(slot) &&
           is_legendary_weapon_item(slot.block_id);
}

inline constexpr auto item_stack_can_be_dropped(
    const HotbarSlot& slot) noexcept -> bool {
    return hotbar_slot_has_item(slot) &&
           !is_undroppable_item(slot.block_id);
}

inline constexpr auto is_musket_loaded(const HotbarSlot& slot) noexcept -> bool {
    return is_musket_item(slot) &&
           (slot.instance_state & kMusketLoadedInstanceState) != 0U;
}

inline constexpr void set_musket_loaded(HotbarSlot& slot, bool loaded) noexcept {
    normalize_item_stack(slot);
    if (!is_musket_item(slot)) {
        return;
    }
    slot.instance_state =
        loaded
            ? kMusketLoadedInstanceState
            : static_cast<std::uint8_t>(0U);
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
    if (!hotbar_slot_has_item(state.selected_slot())) {
        return to_block_id(BlockType::Air);
    }

    const auto selected_item = block_item_id(state.selected_slot().block_id);
    return is_placeable_item(selected_item) ? selected_item : to_block_id(BlockType::Air);
}

inline constexpr auto selected_hotbar_emits_local_light(const HotbarState& state) noexcept -> bool {
    return selected_hotbar_block(state) == to_block_id(BlockType::Torch);
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
