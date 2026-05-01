#pragma once

#include <cstdint>

namespace valcraft {

struct SceneSamplerBindings {
    std::uint32_t color_texture = 0;
    std::uint32_t depth_texture = 0;

    constexpr auto operator==(const SceneSamplerBindings&) const noexcept -> bool = default;
};

[[nodiscard]] constexpr auto select_scene_sampler_bindings(bool use_scene_textures,
                                                           std::uint32_t fallback_color_texture,
                                                           std::uint32_t fallback_depth_texture,
                                                           std::uint32_t scene_color_texture,
                                                           std::uint32_t scene_depth_texture) noexcept
    -> SceneSamplerBindings {
    if (!use_scene_textures || scene_color_texture == 0 || scene_depth_texture == 0) {
        return {fallback_color_texture, fallback_depth_texture};
    }

    return {scene_color_texture, scene_depth_texture};
}

} // namespace valcraft
