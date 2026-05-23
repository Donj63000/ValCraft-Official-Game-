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
    Save = 1,
    Load = 2,
    Options = 3,
    ReturnToMainMenu = 4,
};

struct PauseMenuState {
    bool visible = false;
    PauseMenuAction selected_action = PauseMenuAction::Resume;
    float cursor_x = 0.0F;
    float cursor_y = 0.0F;

    auto operator==(const PauseMenuState&) const -> bool = default;
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

constexpr std::size_t kPauseMenuButtonCount = 5;

struct PauseMenuLayout {
    float panel_x = 0.0F;
    float panel_y = 0.0F;
    float panel_width = 0.0F;
    float panel_height = 0.0F;
    float header_panel_x = 0.0F;
    float header_panel_y = 0.0F;
    float header_panel_width = 0.0F;
    float header_panel_height = 0.0F;
    float button_stack_x = 0.0F;
    float button_stack_y = 0.0F;
    float button_stack_width = 0.0F;
    float button_stack_height = 0.0F;
    float footer_panel_x = 0.0F;
    float footer_panel_y = 0.0F;
    float footer_panel_width = 0.0F;
    float footer_panel_height = 0.0F;
    float brand_center_x = 0.0F;
    float brand_y = 0.0F;
    float title_center_x = 0.0F;
    float title_y = 0.0F;
    float subtitle_center_x = 0.0F;
    float subtitle_y = 0.0F;
    float footer_center_x = 0.0F;
    float footer_y = 0.0F;
    float accent_rail_x = 0.0F;
    float accent_rail_y = 0.0F;
    float accent_rail_width = 0.0F;
    float accent_rail_height = 0.0F;
    std::array<PauseMenuButtonLayout, kPauseMenuButtonCount> buttons {};
};

inline constexpr auto pause_menu_action_label(PauseMenuAction action) noexcept -> std::string_view {
    switch (action) {
    case PauseMenuAction::Resume:
        return "REPRENDRE";
    case PauseMenuAction::Save:
        return "SAUVEGARDER";
    case PauseMenuAction::Load:
        return "CHARGER";
    case PauseMenuAction::Options:
        return "OPTIONS";
    case PauseMenuAction::ReturnToMainMenu:
        return "MENU PRINCIPAL";
    default:
        return "";
    }
}

inline constexpr auto pause_menu_action_enabled(PauseMenuAction action) noexcept -> bool {
    (void)action;
    return true;
}

inline constexpr auto pause_menu_action_index(PauseMenuAction action) noexcept -> std::size_t {
    return static_cast<std::size_t>(action);
}

inline constexpr auto pause_menu_action_from_index(std::size_t index) noexcept -> PauseMenuAction {
    switch (index % kPauseMenuButtonCount) {
    case 0:
        return PauseMenuAction::Resume;
    case 1:
        return PauseMenuAction::Save;
    case 2:
        return PauseMenuAction::Load;
    case 3:
        return PauseMenuAction::Options;
    case 4:
    default:
        return PauseMenuAction::ReturnToMainMenu;
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

    const auto outer_margin = std::clamp(std::min(layout_width, layout_height) * 0.035F, 8.0F, 32.0F);
    const auto max_panel_width = std::max(220.0F, layout_width - outer_margin * 2.0F);
    const auto panel_width = std::min(std::clamp(safe_width * 0.38F, 360.0F, 500.0F), max_panel_width);
    const auto button_width = std::max(0.0F, panel_width - std::clamp(panel_width * 0.15F, 44.0F, 72.0F));
    const auto available_panel_height = std::max(180.0F, layout_height - outer_margin * 2.0F);

    auto button_height = std::clamp(safe_height * 0.070F, 42.0F, 56.0F);
    auto button_gap = std::clamp(safe_height * 0.016F, 10.0F, 17.0F);
    auto header_height = std::clamp(safe_height * 0.150F, 70.0F, 106.0F);
    auto footer_height = std::clamp(safe_height * 0.066F, 34.0F, 48.0F);
    auto inner_gap = std::clamp(safe_height * 0.020F, 10.0F, 18.0F);
    auto panel_padding_y = std::clamp(safe_height * 0.024F, 10.0F, 22.0F);
    auto button_stack_height =
        button_height * static_cast<float>(kPauseMenuButtonCount) +
        button_gap * static_cast<float>(kPauseMenuButtonCount - 1U);
    auto panel_height = panel_padding_y * 2.0F + header_height + inner_gap + button_stack_height + inner_gap + footer_height;

    for (int pass = 0; pass < 18 && panel_height > available_panel_height; ++pass) {
        button_height = std::max(24.0F, button_height - 2.0F);
        button_gap = std::max(3.0F, button_gap - 1.0F);
        header_height = std::max(36.0F, header_height - 4.0F);
        footer_height = std::max(20.0F, footer_height - 2.0F);
        inner_gap = std::max(3.0F, inner_gap - 1.0F);
        panel_padding_y = std::max(3.0F, panel_padding_y - 1.0F);
        button_stack_height =
            button_height * static_cast<float>(kPauseMenuButtonCount) +
            button_gap * static_cast<float>(kPauseMenuButtonCount - 1U);
        panel_height = panel_padding_y * 2.0F + header_height + inner_gap + button_stack_height + inner_gap + footer_height;
    }

    const auto panel_x = (layout_width - panel_width) * 0.5F;
    const auto panel_y = layout_height >= panel_height + outer_margin * 2.0F
                             ? std::max(outer_margin, (layout_height - panel_height) * 0.42F)
                             : (layout_height - panel_height) * 0.5F;
    const auto header_panel_x = panel_x + 10.0F;
    const auto header_panel_y = panel_y + panel_padding_y;
    const auto header_panel_width = std::max(0.0F, panel_width - 20.0F);
    const auto header_panel_height = header_height;
    const auto button_x = static_cast<float>(std::floor(panel_x + (panel_width - button_width) * 0.5F));
    const auto button_start_y = header_panel_y + header_panel_height + inner_gap;
    const auto footer_panel_x = panel_x + 12.0F;
    const auto footer_panel_y = button_start_y + button_stack_height + inner_gap;
    const auto footer_panel_width = std::max(0.0F, panel_width - 24.0F);
    const auto footer_panel_height = footer_height;

    PauseMenuLayout layout {};
    layout.panel_x = panel_x;
    layout.panel_y = panel_y;
    layout.panel_width = panel_width;
    layout.panel_height = panel_height;
    layout.header_panel_x = header_panel_x;
    layout.header_panel_y = header_panel_y;
    layout.header_panel_width = header_panel_width;
    layout.header_panel_height = header_panel_height;
    layout.button_stack_x = button_x;
    layout.button_stack_y = button_start_y;
    layout.button_stack_width = button_width;
    layout.button_stack_height = button_stack_height;
    layout.footer_panel_x = footer_panel_x;
    layout.footer_panel_y = footer_panel_y;
    layout.footer_panel_width = footer_panel_width;
    layout.footer_panel_height = footer_panel_height;
    layout.brand_center_x = panel_x + panel_width * 0.5F;
    layout.brand_y = header_panel_y + std::clamp(header_panel_height * 0.10F, 5.0F, 10.0F);
    layout.title_center_x = panel_x + panel_width * 0.5F;
    layout.title_y = header_panel_y + std::clamp(header_panel_height * 0.34F, 18.0F, 36.0F);
    layout.subtitle_center_x = layout.title_center_x;
    layout.subtitle_y = header_panel_y + std::clamp(header_panel_height * 0.72F, 32.0F, 78.0F);
    layout.footer_center_x = panel_x + panel_width * 0.5F;
    layout.footer_y = footer_panel_y + std::max(6.0F, footer_panel_height * 0.5F - 7.0F);
    layout.accent_rail_x = panel_x + 8.0F;
    layout.accent_rail_y = header_panel_y + 6.0F;
    layout.accent_rail_width = std::max(3.0F, panel_width * 0.012F);
    layout.accent_rail_height = std::max(0.0F, panel_height - panel_padding_y * 2.0F - 12.0F);

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
