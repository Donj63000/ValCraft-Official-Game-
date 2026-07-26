#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace valcraft {

struct ModernHudRoundedRectMetrics {
    float radius = 0.0F;
    int corner_segments = 0;
    std::size_t vertex_count = 0U;
};

// Je centralise les limites géométriques du HUD moderne pour garder les mêmes
// courbes à toutes les résolutions et un budget CPU/GPU explicitement borné.
[[nodiscard]] inline auto modern_hud_rounded_rect_metrics(
    float width,
    float height,
    float preferred_radius) noexcept -> ModernHudRoundedRectMetrics {
    if (!std::isfinite(width) ||
        !std::isfinite(height) ||
        !std::isfinite(preferred_radius) ||
        width <= 0.0F ||
        height <= 0.0F) {
        return {};
    }

    const auto radius = std::clamp(
        preferred_radius,
        0.0F,
        std::min(width, height) * 0.5F);
    if (radius < 0.75F) {
        return ModernHudRoundedRectMetrics {0.0F, 0, 6U};
    }

    const auto corner_segments = std::clamp(
        static_cast<int>(std::ceil(radius * 0.50F)),
        4,
        10);
    auto vertex_count =
        static_cast<std::size_t>(corner_segments) * 4U * 3U;
    if (width - radius * 2.0F > 0.0F) {
        vertex_count += 6U;
    }
    if (height - radius * 2.0F > 0.0F) {
        vertex_count += 12U;
    }
    return ModernHudRoundedRectMetrics {
        radius,
        corner_segments,
        vertex_count,
    };
}

[[nodiscard]] inline auto modern_hud_panel_radius(
    float width,
    float height,
    float border_thickness) noexcept -> float {
    if (!std::isfinite(width) ||
        !std::isfinite(height) ||
        !std::isfinite(border_thickness) ||
        width <= 0.0F ||
        height <= 0.0F) {
        return 0.0F;
    }
    return modern_hud_rounded_rect_metrics(
               width,
               height,
               std::clamp(
                   std::min(width, height) * 0.22F +
                       std::max(border_thickness, 0.0F) * 0.35F,
                   5.0F,
                   18.0F))
        .radius;
}

} // namespace valcraft
