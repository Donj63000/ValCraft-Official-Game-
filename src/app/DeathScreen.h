#pragma once

#include "gameplay/PlayerController.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace valcraft {

enum class DeathScreenAction : std::uint8_t {
    Respawn = 0,
    Quit = 1,
};

struct DeathScreenState {
    bool visible = false;
    DeathScreenAction selected_action = DeathScreenAction::Respawn;
    PlayerDeathCause cause = PlayerDeathCause::None;
    float cursor_x = 0.0F;
    float cursor_y = 0.0F;
};

struct DeathScreenButtonLayout {
    DeathScreenAction action = DeathScreenAction::Respawn;
    std::string_view label {};
    float x = 0.0F;
    float y = 0.0F;
    float width = 0.0F;
    float height = 0.0F;
    bool selected = false;
    bool hovered = false;
};

constexpr std::size_t kDeathScreenButtonCount = 2;

struct DeathScreenLayout {
    float panel_x = 0.0F;
    float panel_y = 0.0F;
    float panel_width = 0.0F;
    float panel_height = 0.0F;
    float title_center_x = 0.0F;
    float title_y = 0.0F;
    float subtitle_center_x = 0.0F;
    float subtitle_y = 0.0F;
    float cause_center_x = 0.0F;
    float cause_y = 0.0F;
    std::array<DeathScreenButtonLayout, kDeathScreenButtonCount> buttons {};
};

inline constexpr auto death_screen_action_label(DeathScreenAction action) noexcept -> std::string_view {
    switch (action) {
    case DeathScreenAction::Respawn:
        return "REAPPARAITRE";
    case DeathScreenAction::Quit:
    default:
        return "QUITTER";
    }
}

inline constexpr auto death_screen_action_index(DeathScreenAction action) noexcept -> std::size_t {
    return static_cast<std::size_t>(action);
}

inline constexpr auto death_screen_action_from_index(std::size_t index) noexcept -> DeathScreenAction {
    switch (index % kDeathScreenButtonCount) {
    case 0:
        return DeathScreenAction::Respawn;
    case 1:
    default:
        return DeathScreenAction::Quit;
    }
}

inline constexpr auto death_screen_cause_label(PlayerDeathCause cause) noexcept -> std::string_view {
    switch (cause) {
    case PlayerDeathCause::Fall:
        return "CAUSE CHUTE";
    case PlayerDeathCause::Drowning:
        return "CAUSE NOYADE";
    case PlayerDeathCause::Void:
        return "CAUSE ABYSSE";
    case PlayerDeathCause::None:
    default:
        return "CAUSE INCONNUE";
    }
}

inline auto next_death_screen_action(DeathScreenAction current, int direction) noexcept -> DeathScreenAction {
    auto index = static_cast<int>(death_screen_action_index(current));
    index += direction >= 0 ? 1 : -1;
    if (index < 0) {
        index = static_cast<int>(kDeathScreenButtonCount) - 1;
    } else if (index >= static_cast<int>(kDeathScreenButtonCount)) {
        index = 0;
    }
    return death_screen_action_from_index(static_cast<std::size_t>(index));
}

inline auto build_death_screen_layout(int viewport_width, int viewport_height, const DeathScreenState& state) -> DeathScreenLayout {
    const auto layout_width = static_cast<float>(std::max(viewport_width, 1));
    const auto layout_height = static_cast<float>(std::max(viewport_height, 1));
    const auto safe_width = static_cast<float>(std::max(viewport_width, 360));
    const auto safe_height = static_cast<float>(std::max(viewport_height, 260));

    const auto panel_width = std::clamp(safe_width * 0.38F, 380.0F, 520.0F);
    const auto button_width = panel_width - 76.0F;
    const auto button_height = std::clamp(safe_height * 0.072F, 48.0F, 58.0F);
    const auto button_gap = std::clamp(safe_height * 0.02F, 14.0F, 20.0F);
    const auto panel_height = 238.0F + button_height * static_cast<float>(kDeathScreenButtonCount) + button_gap;
    const auto panel_x = (layout_width - panel_width) * 0.5F;
    const auto panel_y = (layout_height - panel_height) * 0.42F;
    const auto button_x = panel_x + (panel_width - button_width) * 0.5F;
    const auto button_start_y = panel_y + 158.0F;

    DeathScreenLayout layout {};
    layout.panel_x = panel_x;
    layout.panel_y = panel_y;
    layout.panel_width = panel_width;
    layout.panel_height = panel_height;
    layout.title_center_x = panel_x + panel_width * 0.5F;
    layout.title_y = panel_y + 32.0F;
    layout.subtitle_center_x = layout.title_center_x;
    layout.subtitle_y = panel_y + 80.0F;
    layout.cause_center_x = layout.title_center_x;
    layout.cause_y = panel_y + 110.0F;

    for (std::size_t index = 0; index < kDeathScreenButtonCount; ++index) {
        auto& button = layout.buttons[index];
        button.action = death_screen_action_from_index(index);
        button.label = death_screen_action_label(button.action);
        button.x = button_x;
        button.y = button_start_y + static_cast<float>(index) * (button_height + button_gap);
        button.width = button_width;
        button.height = button_height;
        const auto hovered =
            state.cursor_x >= button.x &&
            state.cursor_x <= button.x + button.width &&
            state.cursor_y >= button.y &&
            state.cursor_y <= button.y + button.height;
        button.hovered = hovered;
        button.selected = button.hovered || state.selected_action == button.action;
    }

    return layout;
}

inline auto death_screen_action_at(const DeathScreenLayout& layout, float cursor_x, float cursor_y) -> std::optional<DeathScreenAction> {
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
