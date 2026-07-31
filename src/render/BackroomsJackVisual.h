#pragma once

#include "creatures/CreatureGeometry.h"

#include <glm/vec3.hpp>

#include <cstddef>
#include <vector>

namespace valcraft {

inline constexpr std::size_t kBackroomsJackVisualPartBudget = 128U;
inline constexpr float kBackroomsJackVisualStandingHeight = 3.82F;
inline constexpr float kBackroomsJackVisualMaximumHunchRadians = 0.70F;

struct BackroomsJackVisualPose {
    glm::vec3 position {0.0F};
    float yaw_radians = 0.0F;
    float animation_time = 0.0F;
    float hunch_ratio = 0.0F;
    float head_scan_radians = 0.0F;
    float motion_amount = 0.0F;
    bool chasing = false;
    bool jumpscare = false;
    float sky_light = 0.0F;
    float block_light = 0.0F;
};

// Je convertis le lacet gameplay, dont l'avant vaut -Z, vers le repère du
// modèle de Jack dont le visage est construit vers +X.
[[nodiscard]] auto backrooms_jack_visual_body_yaw_radians(
    float gameplay_yaw_degrees) noexcept -> float;

[[nodiscard]] auto backrooms_jack_visual_head_yaw_radians(
    float gameplay_head_yaw_degrees) noexcept -> float;

// Je construis Jack dans son repere local, les pieds fixes sur position.y et
// le visage oriente vers +X avant application du lacet.
[[nodiscard]] auto build_backrooms_jack_visual_parts(
    const BackroomsJackVisualPose& pose)
    -> std::vector<CreaturePartInstance>;

} // namespace valcraft
