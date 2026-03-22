#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <optional>
#include <string_view>

namespace valcraft {

enum class PauseMenuAction : std::uint8_t {
    Resume = 0,
    Options = 1,
    Quit = 2,
};

struct PauseMenuState {
    bool visible = false;
    PauseMenuAction selected_action = PauseMenuAction::Resume;
    float cursor_x = 0.0F;
    float cursor_y = 0.0F;
};

struct PauseMenuButtonLayout {
    PauseMenuAction action = PauseMenuAction::Resume;
    std::string_view label {};
    float x = 0.0F;
    float y = 0.0F;
    float width = 0.0F;
    float height = 0.0F;
    bool enabled = true;
    bool selected = false;
    bool hovered = false;
};

constexpr std::size_t kPauseMenuButtonCount = 3;

struct PauseMenuLayout {
    float panel_x = 0.0F;
    float panel_y = 0.0F;
    float panel_width = 0.0F;
    float panel_height = 0.0F;
    float title_center_x = 0.0F;
    float title_y = 0.0F;
    float subtitle_center_x = 0.0F;
    float subtitle_y = 0.0F;
    std::array<PauseMenuButtonLayout, kPauseMenuButtonCount> buttons {};
};

inline constexpr auto pause_menu_action_label(PauseMenuAction action) noexcept -> std::string_view {
    switch (action) {
    case PauseMenuAction::Resume:
        return "REPRENDRE";
    case PauseMenuAction::Options:
        return "OPTIONS";
    case PauseMenuAction::Quit:
        return "QUITTER";
    default:
        return "";
    }
}

inline constexpr auto pause_menu_action_enabled(PauseMenuAction action) noexcept -> bool {
    return action != PauseMenuAction::Options;
}

inline constexpr auto pause_menu_action_index(PauseMenuAction action) noexcept -> std::size_t {
    return static_cast<std::size_t>(action);
}

inline constexpr auto pause_menu_action_from_index(std::size_t index) noexcept -> PauseMenuAction {
    switch (index % kPauseMenuButtonCount) {
    case 0:
        return PauseMenuAction::Resume;
    case 1:
        return PauseMenuAction::Options;
    case 2:
    default:
        return PauseMenuAction::Quit;
    }
}

inline auto next_pause_menu_action(PauseMenuAction current, int direction) noexcept -> PauseMenuAction {
    auto index = static_cast<int>(pause_menu_action_index(current));
    const auto step = direction >= 0 ? 1 : -1;

    for (std::size_t attempts = 0; attempts < kPauseMenuButtonCount; ++attempts) {
        index += step;
        if (index < 0) {
            index = static_cast<int>(kPauseMenuButtonCount) - 1;
        } else if (index >= static_cast<int>(kPauseMenuButtonCount)) {
            index = 0;
        }

        const auto action = pause_menu_action_from_index(static_cast<std::size_t>(index));
        if (pause_menu_action_enabled(action)) {
            return action;
        }
    }

    return current;
}

inline auto build_pause_menu_layout(int viewport_width, int viewport_height, const PauseMenuState& state) -> PauseMenuLayout {
    const auto layout_width = static_cast<float>(std::max(viewport_width, 1));
    const auto layout_height = static_cast<float>(std::max(viewport_height, 1));
    const auto safe_width = static_cast<float>(std::max(viewport_width, 320));
    const auto safe_height = static_cast<float>(std::max(viewport_height, 240));

    const auto panel_width = std::clamp(safe_width * 0.34F, 340.0F, 432.0F);
    const auto button_width = panel_width - 60.0F;
    const auto button_height = std::clamp(safe_height * 0.07F, 46.0F, 56.0F);
    const auto button_gap = std::clamp(safe_height * 0.018F, 12.0F, 18.0F);
    const auto panel_height = button_height * static_cast<float>(kPauseMenuButtonCount) + button_gap * 2.0F + 148.0F;
    const auto panel_x = (layout_width - panel_width) * 0.5F;
    const auto panel_y = layout_height >= panel_height + 64.0F
                             ? std::max(32.0F, (layout_height - panel_height) * 0.34F)
                             : (layout_height - panel_height) * 0.5F;
    const auto button_x = static_cast<float>(std::floor(panel_x + (panel_width - button_width) * 0.5F));
    const auto button_start_y = panel_y + 118.0F;

    PauseMenuLayout layout {};
    layout.panel_x = panel_x;
    layout.panel_y = panel_y;
    layout.panel_width = panel_width;
    layout.panel_height = panel_height;
    layout.title_center_x = panel_x + panel_width * 0.5F;
    layout.title_y = panel_y + 30.0F;
    layout.subtitle_center_x = layout.title_center_x;
    layout.subtitle_y = panel_y + 74.0F;

    for (std::size_t index = 0; index < kPauseMenuButtonCount; ++index) {
        const auto action = pause_menu_action_from_index(index);
        auto& button = layout.buttons[index];
        button.action = action;
        button.label = pause_menu_action_label(action);
        button.x = button_x;
        button.y = button_start_y + static_cast<float>(index) * (button_height + button_gap);
        button.width = button_width;
        button.height = button_height;
        button.enabled = pause_menu_action_enabled(action);

        const auto hovered =
            state.cursor_x >= button.x &&
            state.cursor_x <= button.x + button.width &&
            state.cursor_y >= button.y &&
            state.cursor_y <= button.y + button.height;
        button.hovered = button.enabled && hovered;
        button.selected = button.enabled && (state.selected_action == action || button.hovered);
    }

    return layout;
}

inline auto pause_menu_action_at(const PauseMenuLayout& layout, float cursor_x, float cursor_y) -> std::optional<PauseMenuAction> {
    for (const auto& button : layout.buttons) {
        if (!button.enabled) {
            continue;
        }
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
