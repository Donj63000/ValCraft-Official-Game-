#pragma once

#include <algorithm>
#include <cmath>

namespace valcraft {

struct MaritimeSubmersionState {
    bool active = false;
    float depth = 0.0F;
    float blend = 0.0F;

    auto operator==(const MaritimeSubmersionState&) const -> bool = default;
};

[[nodiscard]] constexpr auto water_detail_sample_count(
    float water_surface_detail) noexcept -> int {
    if (!(water_surface_detail >= 0.0F)) {
        return 0;
    }
    if (water_surface_detail >= 0.85F) {
        return 2;
    }
    return water_surface_detail >= 0.45F ? 1 : 0;
}

[[nodiscard]] constexpr auto water_wake_enabled(
    float water_surface_detail) noexcept -> bool {
    return water_surface_detail >= 0.45F;
}

[[nodiscard]] inline auto sanitized_ship_speed(
    float ship_speed) noexcept -> float {
    return std::isfinite(ship_speed)
               ? std::clamp(ship_speed, 0.0F, 24.0F)
               : 0.0F;
}

[[nodiscard]] inline auto resolve_maritime_submersion_state(
    bool modern_pipeline,
    bool ocean_adventure,
    bool head_underwater,
    float eye_height,
    float ocean_surface_height) noexcept
    -> MaritimeSubmersionState {
    if (!modern_pipeline ||
        !ocean_adventure ||
        !head_underwater ||
        !std::isfinite(eye_height) ||
        !std::isfinite(ocean_surface_height) ||
        ocean_surface_height <= eye_height + 0.005F) {
        return {};
    }

    const auto depth =
        std::clamp(
            ocean_surface_height - eye_height,
            0.0F,
            64.0F);
    const auto normalized =
        std::clamp(
            depth / 2.5F,
            0.0F,
            1.0F);
    const auto smooth =
        normalized * normalized *
        (3.0F - 2.0F * normalized);

    // Je garde un minimum visible dès que les yeux franchissent la surface,
    // puis je renforce progressivement l'absorption avec la profondeur.
    return {
        true,
        depth,
        0.35F + smooth * 0.65F,
    };
}

} // namespace valcraft
