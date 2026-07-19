#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace valcraft {

enum class OptionsMenuParent : std::uint8_t {
    MainMenu = 0,
    PauseMenu = 1,
};

enum class OptionsMenuAction : std::uint8_t {
    ToggleShadows = 0,
    TogglePostProcess = 1,
    Back = 2,
};

struct OptionsMenuState {
    bool visible = false;
    OptionsMenuParent parent = OptionsMenuParent::MainMenu;
    OptionsMenuAction selected_action = OptionsMenuAction::ToggleShadows;
    float cursor_x = 0.0F;
    float cursor_y = 0.0F;
    bool shadows_enabled = true;
    bool post_process_enabled = true;

    auto operator==(const OptionsMenuState&) const -> bool = default;
};

struct OptionsMenuButtonLayout {
    OptionsMenuAction action = OptionsMenuAction::ToggleShadows;
    std::string_view label {};
    float x = 0.0F;
    float y = 0.0F;
    float width = 0.0F;
    float height = 0.0F;
    bool selected = false;
    bool hovered = false;
};

constexpr std::size_t kOptionsMenuButtonCount = 3;

struct OptionsMenuLayout {
    float panel_x = 0.0F;
    float panel_y = 0.0F;
    float panel_width = 0.0F;
    float panel_height = 0.0F;
    float title_center_x = 0.0F;
    float title_y = 0.0F;
    float subtitle_center_x = 0.0F;
    float subtitle_y = 0.0F;
    std::array<OptionsMenuButtonLayout, kOptionsMenuButtonCount> buttons {};
};

inline constexpr auto options_menu_action_label(const OptionsMenuState& state, OptionsMenuAction action) noexcept -> std::string_view {
    switch (action) {
    case OptionsMenuAction::ToggleShadows:
        return state.shadows_enabled ? "OMBRES  ON" : "OMBRES  OFF";
    case OptionsMenuAction::TogglePostProcess:
        return state.post_process_enabled ? "POST PROCESS  ON" : "POST PROCESS  OFF";
    case OptionsMenuAction::Back:
        return "RETOUR";
    default:
        return "";
    }
}

inline constexpr auto options_menu_subtitle(OptionsMenuParent parent) noexcept -> std::string_view {
    return parent == OptionsMenuParent::PauseMenu ? "REGLAGES EN JEU" : "REGLAGES GENERAUX";
}

inline constexpr auto options_menu_action_index(OptionsMenuAction action) noexcept -> std::size_t {
    return static_cast<std::size_t>(action);
}

inline constexpr auto options_menu_action_from_index(std::size_t index) noexcept -> OptionsMenuAction {
    switch (index % kOptionsMenuButtonCount) {
    case 0:
        return OptionsMenuAction::ToggleShadows;
    case 1:
        return OptionsMenuAction::TogglePostProcess;
    case 2:
    default:
        return OptionsMenuAction::Back;
    }
}

inline constexpr auto next_options_menu_action(OptionsMenuAction current, int direction) noexcept -> OptionsMenuAction {
    const auto step = direction >= 0 ? 1 : -1;
    auto index = static_cast<int>(options_menu_action_index(current));
    index = (index + step + static_cast<int>(kOptionsMenuButtonCount)) % static_cast<int>(kOptionsMenuButtonCount);
    return options_menu_action_from_index(static_cast<std::size_t>(index));
}

inline auto build_options_menu_layout(int viewport_width, int viewport_height, const OptionsMenuState& state) -> OptionsMenuLayout {
    const auto layout_width = static_cast<float>(std::max(viewport_width, 1));
    const auto layout_height = static_cast<float>(std::max(viewport_height, 1));
    const auto edge_margin = std::clamp(std::min(layout_width, layout_height) * 0.035F, 4.0F, 24.0F);
    const auto available_panel_width = std::max(1.0F, layout_width - edge_margin * 2.0F);
    const auto available_panel_height = std::max(1.0F, layout_height - edge_margin * 2.0F);
    const auto panel_width = std::min(std::clamp(layout_width * 0.34F, 260.0F, 448.0F), available_panel_width);
    const auto button_inset = std::clamp(panel_width * 0.085F, 18.0F, 30.0F);
    const auto button_width = std::max(0.0F, panel_width - button_inset * 2.0F);
    const auto button_height = std::clamp(layout_height * 0.16F, 34.0F, 56.0F);
    const auto button_gap = std::clamp(layout_height * 0.025F, 6.0F, 18.0F);
    const auto top_chrome = std::clamp(layout_height * 0.34F, 76.0F, 120.0F);
    const auto bottom_padding = std::clamp(layout_height * 0.08F, 12.0F, 36.0F);
    const auto panel_height =
        std::min(
            button_height * static_cast<float>(kOptionsMenuButtonCount) +
                button_gap * static_cast<float>(kOptionsMenuButtonCount - 1U) +
                top_chrome +
                bottom_padding,
            available_panel_height);
    const auto panel_x = std::floor(std::clamp((layout_width - panel_width) * 0.5F, 0.0F, std::max(0.0F, layout_width - panel_width)));
    const auto panel_y = std::floor(std::clamp((layout_height - panel_height) * 0.5F, 0.0F, std::max(0.0F, layout_height - panel_height)));
    const auto button_x = std::floor(panel_x + (panel_width - button_width) * 0.5F);
    const auto button_start_y = panel_y + top_chrome;

    OptionsMenuLayout layout {};
    layout.panel_x = panel_x;
    layout.panel_y = panel_y;
    layout.panel_width = panel_width;
    layout.panel_height = panel_height;
    layout.title_center_x = panel_x + panel_width * 0.5F;
    layout.title_y = panel_y + std::clamp(top_chrome * 0.22F, 16.0F, 28.0F);
    layout.subtitle_center_x = layout.title_center_x;
    layout.subtitle_y = panel_y + std::clamp(top_chrome * 0.58F, 44.0F, 74.0F);

    for (std::size_t index = 0; index < kOptionsMenuButtonCount; ++index) {
        const auto action = options_menu_action_from_index(index);
        auto& button = layout.buttons[index];
        button.action = action;
        button.label = options_menu_action_label(state, action);
        button.x = button_x;
        button.y = button_start_y + static_cast<float>(index) * (button_height + button_gap);
        button.width = button_width;
        button.height = button_height;
        button.hovered =
            state.cursor_x >= button.x &&
            state.cursor_x <= button.x + button.width &&
            state.cursor_y >= button.y &&
            state.cursor_y <= button.y + button.height;
        button.selected = button.hovered || state.selected_action == action;
    }

    return layout;
}

inline auto options_menu_action_at(const OptionsMenuLayout& layout, float cursor_x, float cursor_y)
    -> std::optional<OptionsMenuAction> {
    for (const auto& button : layout.buttons) {
        if (cursor_x >= button.x &&
            cursor_x <= button.x + button.width &&
            cursor_y >= button.y &&
            cursor_y <= button.y + button.height) {
            return button.action;
        }
    }
    return std::nullopt;
}

} // namespace valcraft
