#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace valcraft {

enum class MainMenuAction : std::uint8_t {
    Play = 0,
    Load = 1,
    Options = 2,
};

struct MainMenuState {
    bool visible = false;
    MainMenuAction selected_action = MainMenuAction::Play;
    float cursor_x = 0.0F;
    float cursor_y = 0.0F;

    auto operator==(const MainMenuState&) const -> bool = default;
};

struct MainMenuButtonLayout {
    MainMenuAction action = MainMenuAction::Play;
    std::string_view label {};
    float x = 0.0F;
    float y = 0.0F;
    float width = 0.0F;
    float height = 0.0F;
    bool selected = false;
    bool hovered = false;
};

constexpr std::size_t kMainMenuButtonCount = 3;

struct MainMenuLayout {
    float hero_center_x = 0.0F;
    float hero_y = 0.0F;
    float tagline_center_x = 0.0F;
    float tagline_y = 0.0F;
    float button_stack_x = 0.0F;
    float button_stack_y = 0.0F;
    float button_stack_width = 0.0F;
    float button_stack_height = 0.0F;
    std::array<MainMenuButtonLayout, kMainMenuButtonCount> buttons {};
};

inline constexpr auto main_menu_action_label(MainMenuAction action) noexcept -> std::string_view {
    switch (action) {
    case MainMenuAction::Play:
        return "JOUER";
    case MainMenuAction::Load:
        return "CHARGER";
    case MainMenuAction::Options:
        return "OPTIONS";
    default:
        return "";
    }
}

inline constexpr auto main_menu_action_index(MainMenuAction action) noexcept -> std::size_t {
    return static_cast<std::size_t>(action);
}

inline constexpr auto main_menu_action_from_index(std::size_t index) noexcept -> MainMenuAction {
    switch (index % kMainMenuButtonCount) {
    case 0:
        return MainMenuAction::Play;
    case 1:
        return MainMenuAction::Load;
    case 2:
    default:
        return MainMenuAction::Options;
    }
}

inline constexpr auto next_main_menu_action(MainMenuAction current, int direction) noexcept -> MainMenuAction {
    const auto step = direction >= 0 ? 1 : -1;
    auto index = static_cast<int>(main_menu_action_index(current));
    index = (index + step + static_cast<int>(kMainMenuButtonCount)) % static_cast<int>(kMainMenuButtonCount);
    return main_menu_action_from_index(static_cast<std::size_t>(index));
}

inline auto build_main_menu_layout(int viewport_width, int viewport_height, const MainMenuState& state) -> MainMenuLayout {
    const auto layout_width = static_cast<float>(std::max(viewport_width, 1));
    const auto layout_height = static_cast<float>(std::max(viewport_height, 1));
    const auto safe_width = static_cast<float>(std::max(viewport_width, 360));
    const auto safe_height = static_cast<float>(std::max(viewport_height, 300));

    const auto button_width = std::clamp(safe_width * 0.22F, 240.0F, 340.0F);
    const auto button_height = std::clamp(safe_height * 0.072F, 44.0F, 58.0F);
    const auto button_gap = std::clamp(safe_height * 0.022F, 12.0F, 20.0F);
    const auto stack_height =
        button_height * static_cast<float>(kMainMenuButtonCount) +
        button_gap * static_cast<float>(kMainMenuButtonCount - 1U);
    const auto hero_center_x = layout_width * 0.5F;
    const auto hero_y = std::clamp(layout_height * 0.18F, 48.0F, layout_height * 0.32F);
    const auto tagline_y = hero_y + std::clamp(safe_height * 0.16F, 58.0F, 92.0F);
    const auto button_stack_x = std::floor((layout_width - button_width) * 0.5F);
    const auto button_stack_y = std::min(
        layout_height - stack_height - std::max(28.0F, safe_height * 0.10F),
        tagline_y + std::clamp(safe_height * 0.12F, 56.0F, 94.0F));

    MainMenuLayout layout {};
    layout.hero_center_x = hero_center_x;
    layout.hero_y = hero_y;
    layout.tagline_center_x = hero_center_x;
    layout.tagline_y = tagline_y;
    layout.button_stack_x = button_stack_x;
    layout.button_stack_y = button_stack_y;
    layout.button_stack_width = button_width;
    layout.button_stack_height = stack_height;

    for (std::size_t index = 0; index < kMainMenuButtonCount; ++index) {
        const auto action = main_menu_action_from_index(index);
        auto& button = layout.buttons[index];
        button.action = action;
        button.label = main_menu_action_label(action);
        button.x = button_stack_x;
        button.y = button_stack_y + static_cast<float>(index) * (button_height + button_gap);
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

inline auto main_menu_action_at(const MainMenuLayout& layout, float cursor_x, float cursor_y) -> std::optional<MainMenuAction> {
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
