#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace valcraft {

enum class ConfirmDialogIntent : std::uint8_t {
    None = 0,
    OverwriteSlot = 1,
    LoadSlot = 2,
    ReturnToMainMenu = 3,
    DeleteSlot = 4,
};

enum class ConfirmDialogChoice : std::uint8_t {
    Confirm = 0,
    Cancel = 1,
};

struct ConfirmDialogState {
    bool visible = false;
    ConfirmDialogIntent intent = ConfirmDialogIntent::None;
    ConfirmDialogChoice selected_choice = ConfirmDialogChoice::Confirm;
    float cursor_x = 0.0F;
    float cursor_y = 0.0F;

    auto operator==(const ConfirmDialogState&) const -> bool = default;
};

struct ConfirmDialogButtonLayout {
    ConfirmDialogChoice choice = ConfirmDialogChoice::Confirm;
    std::string_view label {};
    float x = 0.0F;
    float y = 0.0F;
    float width = 0.0F;
    float height = 0.0F;
    bool selected = false;
    bool hovered = false;
};

struct ConfirmDialogLayout {
    float panel_x = 0.0F;
    float panel_y = 0.0F;
    float panel_width = 0.0F;
    float panel_height = 0.0F;
    float title_center_x = 0.0F;
    float title_y = 0.0F;
    float subtitle_center_x = 0.0F;
    float subtitle_y = 0.0F;
    std::array<ConfirmDialogButtonLayout, 2> buttons {};
};

inline constexpr auto confirm_dialog_title(ConfirmDialogIntent intent) noexcept -> std::string_view {
    switch (intent) {
    case ConfirmDialogIntent::OverwriteSlot:
        return "ECRASER CE SLOT";
    case ConfirmDialogIntent::LoadSlot:
        return "CHARGER CETTE PARTIE";
    case ConfirmDialogIntent::ReturnToMainMenu:
        return "MENU PRINCIPAL";
    case ConfirmDialogIntent::DeleteSlot:
        return "VOULEZ VOUS SUPPRIMER CETTE PARTIE ?";
    case ConfirmDialogIntent::None:
    default:
        return "CONFIRMATION";
    }
}

inline constexpr auto confirm_dialog_subtitle(ConfirmDialogIntent intent) noexcept -> std::string_view {
    switch (intent) {
    case ConfirmDialogIntent::OverwriteSlot:
        return "LA SAUVEGARDE EXISTANTE SERA REMPLACEE";
    case ConfirmDialogIntent::LoadSlot:
        return "LA PROGRESSION NON SAUVEGARDEE SERA PERDUE";
    case ConfirmDialogIntent::ReturnToMainMenu:
        return "RETOUR SANS SAUVEGARDE";
    case ConfirmDialogIntent::DeleteSlot:
        return "CETTE ACTION EST DEFINITIVE";
    case ConfirmDialogIntent::None:
    default:
        return "VALIDE CETTE ACTION";
    }
}

inline constexpr auto confirm_dialog_choice_label(ConfirmDialogIntent intent, ConfirmDialogChoice choice) noexcept -> std::string_view {
    if (intent == ConfirmDialogIntent::DeleteSlot) {
        return choice == ConfirmDialogChoice::Confirm ? "OUI" : "NON";
    }

    switch (choice) {
    case ConfirmDialogChoice::Confirm:
        return "CONFIRMER";
    case ConfirmDialogChoice::Cancel:
        return "ANNULER";
    default:
        return "";
    }
}

inline constexpr auto next_confirm_dialog_choice(ConfirmDialogChoice choice, int direction) noexcept -> ConfirmDialogChoice {
    if (direction == 0) {
        return choice;
    }
    return choice == ConfirmDialogChoice::Confirm ? ConfirmDialogChoice::Cancel : ConfirmDialogChoice::Confirm;
}

inline auto build_confirm_dialog_layout(int viewport_width, int viewport_height, const ConfirmDialogState& state)
    -> ConfirmDialogLayout {
    const auto layout_width = static_cast<float>(std::max(viewport_width, 1));
    const auto layout_height = static_cast<float>(std::max(viewport_height, 1));
    const auto safe_width = static_cast<float>(std::max(viewport_width, 360));
    const auto safe_height = static_cast<float>(std::max(viewport_height, 240));

    const auto panel_width = std::clamp(safe_width * 0.34F, 320.0F, 460.0F);
    const auto button_width = std::clamp((panel_width - 56.0F) * 0.5F, 120.0F, 176.0F);
    const auto button_height = std::clamp(safe_height * 0.08F, 42.0F, 54.0F);
    const auto button_gap = std::clamp(safe_width * 0.02F, 12.0F, 20.0F);
    const auto panel_height = 178.0F + button_height;
    const auto panel_x = std::floor((layout_width - panel_width) * 0.5F);
    const auto panel_y = std::floor((layout_height - panel_height) * 0.5F);
    const auto buttons_y = panel_y + panel_height - button_height - 22.0F;
    const auto buttons_x = panel_x + (panel_width - (button_width * 2.0F + button_gap)) * 0.5F;

    ConfirmDialogLayout layout {};
    layout.panel_x = panel_x;
    layout.panel_y = panel_y;
    layout.panel_width = panel_width;
    layout.panel_height = panel_height;
    layout.title_center_x = panel_x + panel_width * 0.5F;
    layout.title_y = panel_y + 28.0F;
    layout.subtitle_center_x = layout.title_center_x;
    layout.subtitle_y = panel_y + 78.0F;

    for (std::size_t index = 0; index < layout.buttons.size(); ++index) {
        const auto choice = index == 0 ? ConfirmDialogChoice::Confirm : ConfirmDialogChoice::Cancel;
        auto& button = layout.buttons[index];
        button.choice = choice;
        button.label = confirm_dialog_choice_label(state.intent, choice);
        button.x = buttons_x + static_cast<float>(index) * (button_width + button_gap);
        button.y = buttons_y;
        button.width = button_width;
        button.height = button_height;
        button.hovered =
            state.cursor_x >= button.x &&
            state.cursor_x <= button.x + button.width &&
            state.cursor_y >= button.y &&
            state.cursor_y <= button.y + button.height;
        button.selected = button.hovered || state.selected_choice == choice;
    }

    return layout;
}

inline auto confirm_dialog_choice_at(const ConfirmDialogLayout& layout, float cursor_x, float cursor_y)
    -> std::optional<ConfirmDialogChoice> {
    for (const auto& button : layout.buttons) {
        if (cursor_x >= button.x &&
            cursor_x <= button.x + button.width &&
            cursor_y >= button.y &&
            cursor_y <= button.y + button.height) {
            return button.choice;
        }
    }
    return std::nullopt;
}

} // namespace valcraft
