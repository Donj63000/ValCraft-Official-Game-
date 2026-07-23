#include "creatures/CrewAnimation.h"

#include <algorithm>
#include <cmath>

namespace valcraft {

namespace {

constexpr float kPi = 3.14159265358979323846F;
constexpr float kTwoPi = 2.0F * kPi;
constexpr float kThighLength = 0.37F;
constexpr float kShinLength = 0.36F;
constexpr float kNeutralHipX = -0.020F;
constexpr float kNeutralHipY = 0.785F;
constexpr float kSoleRestHeight = 0.005F;
constexpr glm::vec2 kAnkleOffset {-0.045F, kCrewFootHalfHeight};

auto saturate(float value) noexcept -> float {
    return std::clamp(std::isfinite(value) ? value : 0.0F, 0.0F, 1.0F);
}

auto smoothstep01(float value) noexcept -> float {
    const auto clamped = saturate(value);
    return clamped * clamped * (3.0F - 2.0F * clamped);
}

auto wrapped_phase(float phase) noexcept -> float {
    if (!std::isfinite(phase)) {
        return 0.0F;
    }

    const auto wrapped = phase - std::floor(phase);
    return wrapped >= 0.0F && wrapped < 1.0F ? wrapped : 0.0F;
}

auto mix(float first, float second, float amount) noexcept -> float {
    return first + (second - first) * amount;
}

auto foot_pitch(float leg_phase) noexcept -> float {
    if (leg_phase < 0.5F) {
        const auto support_phase = leg_phase * 2.0F;
        if (support_phase < 0.20F) {
            // Je pose d'abord le talon, puis je ramene rapidement la semelle a plat.
            return mix(0.10F, 0.0F, smoothstep01(support_phase / 0.20F));
        }
        if (support_phase < 0.70F) {
            return 0.0F;
        }

        // Je termine l'appui sur la pointe avant de degager le pied.
        return mix(0.0F, -0.14F, smoothstep01((support_phase - 0.70F) / 0.30F));
    }

    const auto swing_phase = (leg_phase - 0.5F) * 2.0F;
    return mix(-0.14F, 0.10F, smoothstep01(swing_phase));
}

auto rotate(const glm::vec2& value, float angle) noexcept -> glm::vec2 {
    const auto cosine = std::cos(angle);
    const auto sine = std::sin(angle);
    return {
        cosine * value.x - sine * value.y,
        sine * value.x + cosine * value.y,
    };
}

auto solve_knee(const glm::vec2& hip, const glm::vec2& ankle) noexcept -> glm::vec2 {
    const auto delta = ankle - hip;
    const auto distance_squared = delta.x * delta.x + delta.y * delta.y;
    const auto distance = std::sqrt(std::max(distance_squared, 1.0e-8F));
    const auto maximum_reach = kThighLength + kShinLength - 1.0e-5F;
    const auto minimum_reach = std::abs(kThighLength - kShinLength) + 1.0e-5F;
    const auto solved_distance = std::clamp(distance, minimum_reach, maximum_reach);
    const auto direction = delta / distance;
    const auto along =
        (kThighLength * kThighLength - kShinLength * kShinLength + solved_distance * solved_distance) /
        (2.0F * solved_distance);
    const auto forward = std::sqrt(std::max(kThighLength * kThighLength - along * along, 0.0F));
    const auto center = hip + direction * along;

    // Je choisis l'intersection orientee vers +X pour garder le genou devant le marin.
    return center + glm::vec2 {-direction.y, direction.x} * forward;
}

auto sample_leg(
    float phase,
    float motion,
    float pelvis_offset_y,
    float swing_height,
    float hip_lowering) noexcept -> CrewLegPose {
    const auto leg_phase = wrapped_phase(phase);
    const auto supporting = leg_phase < 0.5F;
    float trajectory_x = 0.0F;
    float lift = 0.0F;

    if (supporting) {
        const auto support_phase = leg_phase * 2.0F;
        // Je recule le pied de 0,48 m pendant que la racine avance exactement
        // de la meme distance : le point d'appui reste verrouille sur le pont.
        trajectory_x = 0.5F * kCrewLocomotionCycleDistance * (0.5F - support_phase);
    } else {
        const auto swing_phase = (leg_phase - 0.5F) * 2.0F;
        const auto eased = smoothstep01(swing_phase);
        trajectory_x = 0.5F * kCrewLocomotionCycleDistance * (eased - 0.5F);
        lift = swing_height * std::sin(kPi * eased);
    }

    CrewLegPose pose {};
    pose.foot_pitch_radians = foot_pitch(leg_phase) * motion;
    pose.sole_height = kSoleRestHeight + lift * motion;
    pose.supporting = supporting || motion <= 1.0e-5F;
    if (pose.foot_pitch_radians > 1.0e-6F) {
        pose.foot_pivot = {-kCrewFootHalfLength, -kCrewFootHalfHeight};
    } else if (pose.foot_pitch_radians < -1.0e-6F) {
        pose.foot_pivot = {kCrewFootHalfLength, -kCrewFootHalfHeight};
    } else {
        pose.foot_pivot = {0.0F, -kCrewFootHalfHeight};
    }
    pose.foot_center = {
        // Je conserve la compensation spatiale complete des que la phase
        // avance. Seule la hauteur se fond avec motion : le pied d'appui ne
        // glisse donc pas pendant l'acceleration ou la deceleration.
        trajectory_x,
        pose.sole_height + kCrewFootHalfHeight,
    };
    pose.ankle =
        pose.foot_center +
        pose.foot_pivot +
        rotate(
            kAnkleOffset - pose.foot_pivot,
            pose.foot_pitch_radians);
    pose.hip = {
        kNeutralHipX,
        kNeutralHipY + pelvis_offset_y - hip_lowering,
    };
    pose.knee = solve_knee(pose.hip, pose.ankle);
    return pose;
}

} // namespace

auto sample_crew_locomotion(
    float phase,
    float motion,
    CrewGaitStyle style,
    float hip_lowering) noexcept -> CrewLocomotionPose {
    const auto safe_phase = wrapped_phase(phase);
    const auto motion_weight = smoothstep01(motion);
    const auto carry = style == CrewGaitStyle::Carry;
    const auto safe_hip_lowering =
        std::clamp(
            std::isfinite(hip_lowering) ? hip_lowering : 0.0F,
            0.0F,
            0.25F);
    const auto pelvis_amplitude = carry ? 0.014F : 0.024F;
    const auto sway_amplitude = carry ? 0.015F : 0.030F;
    const auto swing_height = carry ? 0.10F : 0.13F;
    const auto step_rise = 0.5F - 0.5F * std::cos(safe_phase * 2.0F * kTwoPi);
    const auto lateral_cycle = std::sin(safe_phase * kTwoPi);

    CrewLocomotionPose pose {};
    pose.pelvis_offset_y = pelvis_amplitude * step_rise * motion_weight;
    pose.torso_sway_radians = sway_amplitude * lateral_cycle * motion_weight;
    pose.head_stabilization_radians = -0.65F * pose.torso_sway_radians;
    pose.legs[0] = sample_leg(
        safe_phase,
        motion_weight,
        pose.pelvis_offset_y,
        swing_height,
        safe_hip_lowering);
    pose.legs[1] = sample_leg(
        safe_phase + 0.5F,
        motion_weight,
        pose.pelvis_offset_y,
        swing_height,
        safe_hip_lowering);

    if (!carry) {
        const auto half_stride = 0.25F * kCrewLocomotionCycleDistance;
        pose.arm_swing =
            -0.18F *
            (pose.legs[0].foot_center.x / half_stride) *
            motion_weight;
    }
    return pose;
}

} // namespace valcraft
