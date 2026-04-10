#pragma once

#include "app/SaveGame.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace valcraft {

enum class SaveSlotMenuMode : std::uint8_t {
    NewGame = 0,
    LoadGame = 1,
    SaveGame = 2,
};

enum class SaveSlotMenuParent : std::uint8_t {
    MainMenu = 0,
    PauseMenu = 1,
};

enum class SaveSlotPrimaryAction : std::uint8_t {
    None = 0,
    StartNewGame = 1,
    LoadGame = 2,
    SaveGame = 3,
    ConfirmOverwrite = 4,
    ConfirmLoad = 5,
};

struct SaveSlotMenuState {
    bool visible = false;
    SaveSlotMenuMode mode = SaveSlotMenuMode::NewGame;
    SaveSlotMenuParent parent = SaveSlotMenuParent::MainMenu;
    std::size_t selected_index = 0;
    float cursor_x = 0.0F;
    float cursor_y = 0.0F;
    std::array<SaveSlotMetadata, kSaveSlotCount> slots {};
    std::optional<std::size_t> active_slot {};

    auto operator==(const SaveSlotMenuState&) const -> bool = default;
};

struct SaveSlotCardLayout {
    std::size_t slot_index = 0;
    float x = 0.0F;
    float y = 0.0F;
    float width = 0.0F;
    float height = 0.0F;
    bool enabled = true;
    bool occupied = false;
    bool selected = false;
    bool hovered = false;
    bool active_slot = false;
    bool delete_visible = false;
    bool delete_hovered = false;
    float delete_x = 0.0F;
    float delete_y = 0.0F;
    float delete_size = 0.0F;
    SaveSlotMetadata metadata {};
};

struct SaveSlotButtonLayout {
    float x = 0.0F;
    float y = 0.0F;
    float width = 0.0F;
    float height = 0.0F;
    bool selected = false;
    bool hovered = false;
};

struct SaveSlotMenuLayout {
    float panel_x = 0.0F;
    float panel_y = 0.0F;
    float panel_width = 0.0F;
    float panel_height = 0.0F;
    float title_center_x = 0.0F;
    float title_y = 0.0F;
    float subtitle_center_x = 0.0F;
    float subtitle_y = 0.0F;
    std::array<SaveSlotCardLayout, kSaveSlotCount> cards {};
    SaveSlotButtonLayout back_button {};
};

inline constexpr auto save_slot_menu_mode_title(SaveSlotMenuMode mode) noexcept -> std::string_view {
    switch (mode) {
    case SaveSlotMenuMode::NewGame:
        return "CHOISIR UN SLOT";
    case SaveSlotMenuMode::LoadGame:
        return "CHARGER UNE PARTIE";
    case SaveSlotMenuMode::SaveGame:
        return "SAUVEGARDER";
    default:
        return "";
    }
}

inline constexpr auto save_slot_menu_mode_subtitle(SaveSlotMenuMode mode) noexcept -> std::string_view {
    switch (mode) {
    case SaveSlotMenuMode::NewGame:
        return "VIDE = NOUVELLE PARTIE, OCCUPE = OUVRIR";
    case SaveSlotMenuMode::LoadGame:
        return "SEULS LES SLOTS EXISTANTS SONT ACTIFS";
    case SaveSlotMenuMode::SaveGame:
        return "CHOISIS OU ECRIRE LA PARTIE";
    default:
        return "";
    }
}

inline constexpr auto save_slot_menu_slot_enabled(const SaveSlotMenuState& state, std::size_t slot_index) noexcept -> bool {
    if (slot_index >= kSaveSlotCount) {
        return false;
    }
    if (state.mode == SaveSlotMenuMode::LoadGame) {
        return state.slots[slot_index].exists;
    }
    return true;
}

inline constexpr auto resolve_save_slot_primary_action(const SaveSlotMenuState& state,
                                                       std::size_t slot_index,
                                                       bool session_dirty) noexcept -> SaveSlotPrimaryAction {
    if (!save_slot_menu_slot_enabled(state, slot_index)) {
        return SaveSlotPrimaryAction::None;
    }

    switch (state.mode) {
    case SaveSlotMenuMode::NewGame:
        return state.slots[slot_index].exists ? SaveSlotPrimaryAction::LoadGame : SaveSlotPrimaryAction::StartNewGame;
    case SaveSlotMenuMode::LoadGame:
        if (state.parent == SaveSlotMenuParent::PauseMenu && session_dirty) {
            return SaveSlotPrimaryAction::ConfirmLoad;
        }
        return SaveSlotPrimaryAction::LoadGame;
    case SaveSlotMenuMode::SaveGame:
        return state.slots[slot_index].exists ? SaveSlotPrimaryAction::ConfirmOverwrite : SaveSlotPrimaryAction::SaveGame;
    default:
        return SaveSlotPrimaryAction::None;
    }
}

inline auto first_save_slot_menu_index(const SaveSlotMenuState& state) noexcept -> std::size_t {
    for (std::size_t index = 0; index < kSaveSlotCount; ++index) {
        if (save_slot_menu_slot_enabled(state, index)) {
            return index;
        }
    }
    return kSaveSlotCount;
}

inline auto next_save_slot_menu_index(const SaveSlotMenuState& state, int direction) noexcept -> std::size_t {
    constexpr auto selectable_count = static_cast<int>(kSaveSlotCount + 1U);
    auto index = static_cast<int>(std::min<std::size_t>(state.selected_index, kSaveSlotCount));
    const auto step = direction >= 0 ? 1 : -1;

    for (int attempts = 0; attempts < selectable_count; ++attempts) {
        index = (index + step + selectable_count) % selectable_count;
        if (index == static_cast<int>(kSaveSlotCount)) {
            return static_cast<std::size_t>(index);
        }
        if (save_slot_menu_slot_enabled(state, static_cast<std::size_t>(index))) {
            return static_cast<std::size_t>(index);
        }
    }

    return index < 0 ? 0U : static_cast<std::size_t>(index);
}

inline auto build_save_slot_menu_layout(int viewport_width, int viewport_height, const SaveSlotMenuState& state)
    -> SaveSlotMenuLayout {
    const auto layout_width = static_cast<float>(std::max(viewport_width, 1));
    const auto layout_height = static_cast<float>(std::max(viewport_height, 1));
    const auto safe_width = static_cast<float>(std::max(viewport_width, 540));
    const auto safe_height = static_cast<float>(std::max(viewport_height, 360));

    const auto panel_width = std::clamp(safe_width * 0.60F, 520.0F, 860.0F);
    const auto panel_height = std::clamp(safe_height * 0.72F, 360.0F, 620.0F);
    const auto panel_x = std::floor((layout_width - panel_width) * 0.5F);
    const auto panel_y = std::floor((layout_height - panel_height) * 0.5F);
    const auto inner_padding = std::clamp(panel_width * 0.04F, 18.0F, 28.0F);
    const auto top_offset = 104.0F;
    const auto column_gap = std::clamp(panel_width * 0.04F, 14.0F, 26.0F);
    const auto row_gap = std::clamp(panel_height * 0.028F, 12.0F, 18.0F);
    const auto card_width = (panel_width - inner_padding * 2.0F - column_gap) * 0.5F;
    const auto card_height = std::clamp((panel_height - top_offset - inner_padding - row_gap * 3.0F - 58.0F) / 4.0F, 54.0F, 88.0F);
    const auto card_start_x = panel_x + inner_padding;
    const auto card_start_y = panel_y + top_offset;
    const auto delete_padding = 10.0F;

    SaveSlotMenuLayout layout {};
    layout.panel_x = panel_x;
    layout.panel_y = panel_y;
    layout.panel_width = panel_width;
    layout.panel_height = panel_height;
    layout.title_center_x = panel_x + panel_width * 0.5F;
    layout.title_y = panel_y + 24.0F;
    layout.subtitle_center_x = layout.title_center_x;
    layout.subtitle_y = panel_y + 66.0F;

    for (std::size_t slot_index = 0; slot_index < kSaveSlotCount; ++slot_index) {
        const auto row = static_cast<float>(slot_index / 2U);
        const auto column = static_cast<float>(slot_index % 2U);
        auto& card = layout.cards[slot_index];
        card.slot_index = slot_index;
        card.x = card_start_x + column * (card_width + column_gap);
        card.y = card_start_y + row * (card_height + row_gap);
        card.width = card_width;
        card.height = card_height;
        card.metadata = state.slots[slot_index];
        card.enabled = save_slot_menu_slot_enabled(state, slot_index);
        card.occupied = state.slots[slot_index].exists;
        card.active_slot = state.active_slot.has_value() && *state.active_slot == slot_index;
        card.delete_visible = card.occupied;
        card.delete_size = std::clamp(card.height * 0.28F, 18.0F, 24.0F);
        card.delete_x = card.x + card.width - delete_padding - card.delete_size;
        card.delete_y = card.y + 8.0F;
        const auto delete_hovered =
            card.delete_visible &&
            state.cursor_x >= card.delete_x &&
            state.cursor_x <= card.delete_x + card.delete_size &&
            state.cursor_y >= card.delete_y &&
            state.cursor_y <= card.delete_y + card.delete_size;
        card.delete_hovered = delete_hovered;
        const auto card_hovered =
            state.cursor_x >= card.x &&
            state.cursor_x <= card.x + card.width &&
            state.cursor_y >= card.y &&
            state.cursor_y <= card.y + card.height;
        card.hovered = card_hovered && !delete_hovered;
        card.selected = (state.selected_index == slot_index && card.enabled) || ((card.hovered || card.delete_hovered) && card.enabled);
    }

    layout.back_button.width = std::clamp(panel_width * 0.24F, 140.0F, 220.0F);
    layout.back_button.height = 42.0F;
    layout.back_button.x = panel_x + panel_width - inner_padding - layout.back_button.width;
    layout.back_button.y = panel_y + panel_height - inner_padding - layout.back_button.height;
    layout.back_button.hovered =
        state.cursor_x >= layout.back_button.x &&
        state.cursor_x <= layout.back_button.x + layout.back_button.width &&
        state.cursor_y >= layout.back_button.y &&
        state.cursor_y <= layout.back_button.y + layout.back_button.height;
    layout.back_button.selected = state.selected_index == kSaveSlotCount || layout.back_button.hovered;

    return layout;
}

inline auto save_slot_card_at(const SaveSlotMenuLayout& layout, float cursor_x, float cursor_y) -> std::optional<std::size_t> {
    for (const auto& card : layout.cards) {
        if (!card.enabled) {
            continue;
        }
        if (card.delete_visible &&
            cursor_x >= card.delete_x &&
            cursor_x <= card.delete_x + card.delete_size &&
            cursor_y >= card.delete_y &&
            cursor_y <= card.delete_y + card.delete_size) {
            continue;
        }
        if (cursor_x >= card.x &&
            cursor_x <= card.x + card.width &&
            cursor_y >= card.y &&
            cursor_y <= card.y + card.height) {
            return card.slot_index;
        }
    }
    return std::nullopt;
}

inline auto save_slot_delete_at(const SaveSlotMenuLayout& layout, float cursor_x, float cursor_y) -> std::optional<std::size_t> {
    for (const auto& card : layout.cards) {
        if (!card.delete_visible) {
            continue;
        }
        if (cursor_x >= card.delete_x &&
            cursor_x <= card.delete_x + card.delete_size &&
            cursor_y >= card.delete_y &&
            cursor_y <= card.delete_y + card.delete_size) {
            return card.slot_index;
        }
    }
    return std::nullopt;
}

inline auto save_slot_back_hovered(const SaveSlotMenuLayout& layout, float cursor_x, float cursor_y) noexcept -> bool {
    return cursor_x >= layout.back_button.x &&
           cursor_x <= layout.back_button.x + layout.back_button.width &&
           cursor_y >= layout.back_button.y &&
           cursor_y <= layout.back_button.y + layout.back_button.height;
}

} // namespace valcraft
