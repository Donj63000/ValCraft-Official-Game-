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
    const auto safe_width = static_cast<float>(std::max(viewport_width, 360));
    const auto safe_height = static_cast<float>(std::max(viewport_height, 260));

    const auto panel_width = std::clamp(safe_width * 0.34F, 340.0F, 448.0F);
    const auto button_width = panel_width - 60.0F;
    const auto button_height = std::clamp(safe_height * 0.075F, 46.0F, 56.0F);
    const auto button_gap = std::clamp(safe_height * 0.02F, 12.0F, 18.0F);
    const auto panel_height = button_height * static_cast<float>(kOptionsMenuButtonCount) + button_gap * 2.0F + 156.0F;
    const auto panel_x = std::floor((layout_width - panel_width) * 0.5F);
    const auto panel_y = std::floor((layout_height - panel_height) * 0.5F);
    const auto button_x = std::floor(panel_x + (panel_width - button_width) * 0.5F);
    const auto button_start_y = panel_y + 120.0F;

    OptionsMenuLayout layout {};
    layout.panel_x = panel_x;
    layout.panel_y = panel_y;
    layout.panel_width = panel_width;
    layout.panel_height = panel_height;
    layout.title_center_x = panel_x + panel_width * 0.5F;
    layout.title_y = panel_y + 28.0F;
    layout.subtitle_center_x = layout.title_center_x;
    layout.subtitle_y = panel_y + 74.0F;

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
