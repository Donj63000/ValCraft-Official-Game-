#pragma once

#include "creatures/CreatureGeometry.h"

#include <glm/vec3.hpp>

#include <cstddef>
#include <vector>

namespace valcraft {

inline constexpr std::size_t kBackroomsMarlowVisualPartBudget = 96U;
inline constexpr std::size_t kBackroomsMarlowBuoyVisualPartBudget = 20U;
inline constexpr float kBackroomsMarlowVisualStandingHeight = 3.85F;
inline constexpr float kBackroomsMarlowMaximumSubmersion = 3.35F;

struct BackroomsMarlowVisualPose {
    glm::vec3 position {0.0F};
    float yaw_radians = 0.0F;
    float animation_time = 0.0F;
    float motion_amount = 0.0F;
    float submersion_ratio = 0.0F;
    // Je signe cette valeur : -1 et +1 font sortir Marlow des deux cotes
    // possibles d'un angle sans dupliquer le rig.
    float peek_amount = 0.0F;
    float head_scan_radians = 0.0F;
    float reach_amount = 0.0F;
    bool jumpscare = false;
    float sky_light = 0.0F;
    float block_light = 0.0F;
};

struct BackroomsMarlowBuoyVisualPose {
    // La coordonnee Y est la surface reelle de l'eau, pas le fond du bassin.
    glm::vec3 water_surface_position {0.0F};
    float yaw_radians = 0.0F;
    float animation_time = 0.0F;
    float disturbance = 0.0F;
    float sky_light = 0.0F;
    float block_light = 0.0F;
};

// Je garde la meme convention que Jack : le gameplay regarde vers -Z, tandis
// que le visage procedural est modele vers +X.
[[nodiscard]] auto backrooms_marlow_visual_body_yaw_radians(
    float gameplay_yaw_degrees) noexcept -> float;

[[nodiscard]] auto backrooms_marlow_visual_head_yaw_radians(
    float gameplay_head_yaw_degrees) noexcept -> float;

// Je transforme la progression d'apparition en immersion reelle : a zero,
// Marlow reste sous la surface ; a un, il rejoint son immersion de phase.
[[nodiscard]] auto backrooms_marlow_visual_submersion_ratio(
    float phase_immersion_ratio,
    float reveal_amount) noexcept -> float;

[[nodiscard]] auto build_backrooms_marlow_visual_parts(
    const BackroomsMarlowVisualPose& pose)
    -> std::vector<CreaturePartInstance>;

[[nodiscard]] auto build_backrooms_marlow_buoy_visual_parts(
    const BackroomsMarlowBuoyVisualPose& pose)
    -> std::vector<CreaturePartInstance>;

} // namespace valcraft
