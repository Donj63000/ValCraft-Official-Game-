#pragma once

#include <glm/vec2.hpp>

#include <array>

namespace valcraft {

// Je centralise ici la cadence et l'enveloppe des bottes afin que le gameplay,
// l'IK, la geometrie et leurs tests ne puissent jamais diverger.
inline constexpr float kCrewLocomotionCycleDistance = 0.96F;
inline constexpr float kCrewFootHalfLength = 0.085F;
inline constexpr float kCrewFootHalfHeight = 0.065F;
inline constexpr float kCrewFootToeHalfHeight = 0.052F;

enum class CrewGaitStyle {
    Walk,
    Carry,
};

struct CrewLegPose {
    glm::vec2 hip {0.0F};
    glm::vec2 knee {0.0F};
    glm::vec2 ankle {0.0F};
    // Je fournis le repere non tourne de la botte et son pivot de contact ; la
    // geometrie peut ainsi composer un vrai appui talon ou pointe.
    glm::vec2 foot_center {0.0F};
    glm::vec2 foot_pivot {0.0F};
    float foot_pitch_radians = 0.0F;
    float sole_height = 0.0F;
    bool supporting = true;
};

struct CrewLocomotionPose {
    std::array<CrewLegPose, 2> legs {};
    float pelvis_offset_y = 0.0F;
    float torso_sway_radians = 0.0F;
    float head_stabilization_radians = 0.0F;
    float arm_swing = 0.0F;
};

[[nodiscard]] auto sample_crew_locomotion(
    float phase,
    float motion,
    CrewGaitStyle style,
    float hip_lowering = 0.0F) noexcept -> CrewLocomotionPose;

} // namespace valcraft
