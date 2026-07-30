#pragma once

#include <algorithm>
#include <cstdint>

namespace valcraft {

struct ProgressionMenuRect {
    float x = 0.0F;
    float y = 0.0F;
    float width = 0.0F;
    float height = 0.0F;

    [[nodiscard]] constexpr auto right() const noexcept -> float {
        return x + width;
    }

    [[nodiscard]] constexpr auto bottom() const noexcept -> float {
        return y + height;
    }

    [[nodiscard]] constexpr auto empty() const noexcept -> bool {
        return width <= 0.0F ||
               height <= 0.0F;
    }

    auto operator==(const ProgressionMenuRect&) const -> bool = default;
};

enum class ProgressionMenuLayoutMode : std::uint8_t {
    TwoColumns = 0,
    CompactPages,
};

enum class ProgressionMenuPage : std::uint8_t {
    Ability = 0,
    Attributes,
    Slots,
    ConstructionPlan,
};

struct ProgressionMenuLayout {
    int viewport_width = 0;
    int viewport_height = 0;
    ProgressionMenuLayoutMode mode =
        ProgressionMenuLayoutMode::CompactPages;
    ProgressionMenuPage page =
        ProgressionMenuPage::Ability;
    ProgressionMenuRect panel {};
    ProgressionMenuRect title {};
    ProgressionMenuRect summary {};
    ProgressionMenuRect navigation {};
    ProgressionMenuRect primary_content {};
    ProgressionMenuRect secondary_content {};
    ProgressionMenuRect footer {};

    [[nodiscard]] constexpr auto valid() const noexcept -> bool {
        return viewport_width > 0 &&
               viewport_height > 0 &&
               !panel.empty() &&
               !primary_content.empty();
    }
};

[[nodiscard]] inline constexpr auto progression_menu_rect_contains(
    const ProgressionMenuRect& container,
    const ProgressionMenuRect& child) noexcept -> bool {
    if (container.empty() ||
        child.empty()) {
        return false;
    }
    return child.x >= container.x &&
           child.y >= container.y &&
           child.right() <=
               container.right() &&
           child.bottom() <=
               container.bottom();
}

[[nodiscard]] inline constexpr auto progression_menu_rects_overlap(
    const ProgressionMenuRect& lhs,
    const ProgressionMenuRect& rhs) noexcept -> bool {
    if (lhs.empty() ||
        rhs.empty()) {
        return false;
    }
    return lhs.x < rhs.right() &&
           lhs.right() > rhs.x &&
           lhs.y < rhs.bottom() &&
           lhs.bottom() > rhs.y;
}

[[nodiscard]] inline auto make_progression_menu_layout(
    int viewport_width,
    int viewport_height,
    ProgressionMenuPage page =
        ProgressionMenuPage::Ability) noexcept
    -> ProgressionMenuLayout {
    ProgressionMenuLayout layout {};
    layout.viewport_width =
        viewport_width;
    layout.viewport_height =
        viewport_height;
    layout.page =
        page;
    if (viewport_width <= 0 ||
        viewport_height <= 0) {
        return layout;
    }

    const auto width =
        static_cast<float>(
            viewport_width);
    const auto height =
        static_cast<float>(
            viewport_height);
    const auto compact =
        viewport_width < 900 ||
        viewport_height < 560;
    const auto outer_margin =
        compact ? 8.0F : 16.0F;
    const auto panel_width =
        std::max(
            0.0F,
            std::min(
                width -
                    outer_margin * 2.0F,
                1'120.0F));
    const auto panel_height =
        std::max(
            0.0F,
            std::min(
                height -
                    outer_margin * 2.0F,
                720.0F));
    layout.panel = {
        (width - panel_width) * 0.5F,
        (height - panel_height) * 0.5F,
        panel_width,
        panel_height,
    };

    if (compact) {
        layout.mode =
            ProgressionMenuLayoutMode::
                CompactPages;
        const auto inset = 12.0F;
        const auto content_width =
            std::max(
                0.0F,
                panel_width -
                    inset * 2.0F);
        layout.title = {
            layout.panel.x + inset,
            layout.panel.y + 8.0F,
            content_width,
            24.0F,
        };
        layout.summary = {
            layout.panel.x + inset,
            layout.panel.y + 36.0F,
            content_width,
            18.0F,
        };
        layout.navigation = {
            layout.panel.x + inset,
            layout.panel.y + 60.0F,
            content_width,
            26.0F,
        };
        layout.footer = {
            layout.panel.x + inset,
            layout.panel.bottom() -
                38.0F,
            content_width,
            28.0F,
        };
        const auto content_top =
            layout.navigation.bottom() +
            6.0F;
        layout.primary_content = {
            layout.panel.x + inset,
            content_top,
            content_width,
            std::max(
                0.0F,
                layout.footer.y -
                    6.0F -
                    content_top),
        };
        return layout;
    }

    layout.mode =
        ProgressionMenuLayoutMode::
            TwoColumns;
    const auto inset = 24.0F;
    const auto content_width =
        std::max(
            0.0F,
            panel_width -
                inset * 2.0F);
    layout.title = {
        layout.panel.x + inset,
        layout.panel.y + 16.0F,
        content_width,
        30.0F,
    };
    layout.summary = {
        layout.panel.x + inset,
        layout.panel.y + 52.0F,
        content_width,
        24.0F,
    };
    layout.navigation = {
        layout.panel.x + inset,
        layout.panel.y + 82.0F,
        content_width,
        28.0F,
    };
    layout.footer = {
        layout.panel.x + inset,
        layout.panel.bottom() -
            52.0F,
        content_width,
        36.0F,
    };

    const auto content_top =
        layout.navigation.bottom() +
        10.0F;
    const auto content_height =
        std::max(
            0.0F,
            layout.footer.y -
                10.0F -
                content_top);
    const auto column_gap = 20.0F;
    const auto available_columns_width =
        std::max(
            0.0F,
            content_width -
                column_gap);
    const auto primary_width =
        available_columns_width *
        0.52F;
    layout.primary_content = {
        layout.panel.x + inset,
        content_top,
        primary_width,
        content_height,
    };
    layout.secondary_content = {
        layout.primary_content.right() +
            column_gap,
        content_top,
        available_columns_width -
            primary_width,
        content_height,
    };
    return layout;
}

} // namespace valcraft
