#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace valcraft {

struct MusketHudRect {
    float x = 0.0F;
    float y = 0.0F;
    float width = 0.0F;
    float height = 0.0F;

    auto operator==(const MusketHudRect&) const -> bool = default;
};

struct MusketHudLayout {
    std::array<MusketHudRect, 4U> branches {};
    float viewport_width = 0.0F;
    float viewport_height = 0.0F;
    float center_x = 0.0F;
    float center_y = 0.0F;
    float scale = 1.0F;
    float outline = 1.0F;
    float dot_outline_size = 5.0F;
    float dot_size = 2.0F;
    float text_y = 0.0F;
    bool valid = false;
};

// Je resous toute la geometrie en pixels pour que le pilote OpenGL, le ratio
// d'ecran et le facteur HiDPI ne puissent jamais deformer le reticule.
[[nodiscard]] inline auto resolve_musket_hud_layout(
    int width,
    int height,
    float raw_aim_ratio) noexcept -> MusketHudLayout {

    if (width <= 0 ||
        height <= 0) {
        return {};
    }

    MusketHudLayout layout {};
    layout.viewport_width =
        static_cast<float>(width);
    layout.viewport_height =
        static_cast<float>(height);
    layout.center_x =
        layout.viewport_width * 0.5F;
    layout.center_y =
        layout.viewport_height * 0.5F;
    layout.scale = std::clamp(
        std::min(
            layout.viewport_width,
            layout.viewport_height) /
            900.0F,
        0.75F,
        2.0F);
    const auto aim_ratio =
        std::isfinite(raw_aim_ratio)
            ? std::clamp(
                  raw_aim_ratio,
                  0.0F,
                  1.0F)
            : 0.0F;
    const auto gap =
        (3.0F +
         (1.0F - aim_ratio) *
             14.0F) *
        layout.scale;
    const auto length =
        7.0F * layout.scale;
    const auto thickness =
        std::max(
            1.0F,
            1.25F * layout.scale);
    layout.outline =
        std::max(
            1.0F,
            layout.scale);
    layout.dot_outline_size =
        std::max(
            5.0F,
            5.0F * layout.scale);
    layout.dot_size =
        2.5F * layout.scale;
    layout.text_y =
        layout.center_y +
        (38.0F +
         (1.0F - aim_ratio) *
             8.0F) *
            layout.scale;
    layout.branches = {{
        {
            layout.center_x - gap - length,
            layout.center_y - thickness * 0.5F,
            length,
            thickness,
        },
        {
            layout.center_x + gap,
            layout.center_y - thickness * 0.5F,
            length,
            thickness,
        },
        {
            layout.center_x - thickness * 0.5F,
            layout.center_y - gap - length,
            thickness,
            length,
        },
        {
            layout.center_x - thickness * 0.5F,
            layout.center_y + gap,
            thickness,
            length,
        },
    }};
    layout.valid = true;
    return layout;
}

} // namespace valcraft
