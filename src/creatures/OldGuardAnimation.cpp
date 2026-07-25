#include "creatures/OldGuardAnimation.h"

#include "creatures/CrewAnimation.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/vec4.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace valcraft {

namespace {

auto finite_or(float value, float fallback) noexcept -> float {
    return std::isfinite(value) ? value : fallback;
}

auto finite_vec3_or(const glm::vec3& value, const glm::vec3& fallback) noexcept
    -> glm::vec3 {
    return std::isfinite(value.x) &&
                   std::isfinite(value.y) &&
                   std::isfinite(value.z)
               ? value
               : fallback;
}

auto finite_orientation_or_identity(const glm::quat& value) noexcept -> glm::quat {
    const auto length_squared =
        value.w * value.w +
        value.x * value.x +
        value.y * value.y +
        value.z * value.z;
    if (!std::isfinite(length_squared) || length_squared <= 1.0e-8F) {
        return {1.0F, 0.0F, 0.0F, 0.0F};
    }
    return glm::normalize(value);
}

auto saturate(float value) noexcept -> float {
    return std::clamp(finite_or(value, 0.0F), 0.0F, 1.0F);
}

auto smoothstep01(float value) noexcept -> float {
    const auto amount = saturate(value);
    return amount * amount * (3.0F - 2.0F * amount);
}

auto safe_direction(const glm::vec3& value, const glm::vec3& fallback) noexcept
    -> glm::vec3 {
    const auto finite_value = finite_vec3_or(value, fallback);
    const auto length_squared = glm::dot(finite_value, finite_value);
    if (!std::isfinite(length_squared) || length_squared <= 1.0e-8F) {
        return fallback;
    }
    return finite_value / std::sqrt(length_squared);
}

auto transformed_point(const glm::mat4& transform, const glm::vec3& point) noexcept
    -> glm::vec3 {
    return glm::vec3 {transform * glm::vec4 {point, 1.0F}};
}

auto make_weapon_transform(const glm::vec3& origin,
                           const glm::vec3& forward,
                           const glm::vec3& preferred_up,
                           float scale) noexcept -> glm::mat4 {
    const auto axis_x = safe_direction(
        forward,
        glm::vec3 {1.0F, 0.0F, 0.0F});
    auto axis_z = glm::cross(
        axis_x,
        safe_direction(
            preferred_up,
            glm::vec3 {0.0F, 1.0F, 0.0F}));
    if (glm::dot(axis_z, axis_z) <= 1.0e-6F) {
        axis_z = glm::cross(axis_x, glm::vec3 {0.0F, 0.0F, 1.0F});
    }
    axis_z = safe_direction(
        axis_z,
        glm::vec3 {0.0F, 0.0F, 1.0F});
    const auto axis_y = safe_direction(
        glm::cross(axis_z, axis_x),
        glm::vec3 {0.0F, 1.0F, 0.0F});

    glm::mat4 transform {1.0F};
    transform[0] = glm::vec4 {axis_x * scale, 0.0F};
    transform[1] = glm::vec4 {axis_y * scale, 0.0F};
    transform[2] = glm::vec4 {axis_z * scale, 0.0F};
    transform[3] = glm::vec4 {origin, 1.0F};
    return transform;
}

auto reload_stage(float progress) noexcept -> OldGuardReloadStage {
    if (progress < 0.12F) return OldGuardReloadStage::RecoilAndHalfCock;
    if (progress < 0.24F) return OldGuardReloadStage::Cartridge;
    if (progress < 0.38F) return OldGuardReloadStage::Prime;
    if (progress < 0.52F) return OldGuardReloadStage::Powder;
    if (progress < 0.72F) return OldGuardReloadStage::Ramrod;
    if (progress < 0.86F) return OldGuardReloadStage::ReturnRamrod;
    return OldGuardReloadStage::Shoulder;
}

auto triangular(float value, float peak) noexcept -> float {
    const auto safe_peak = std::clamp(peak, 0.01F, 0.99F);
    if (value <= safe_peak) {
        return smoothstep01(value / safe_peak);
    }
    return 1.0F - smoothstep01((value - safe_peak) / (1.0F - safe_peak));
}

} // namespace

auto sample_old_guard_pose(const OldGuardRenderInstance& guard) noexcept
    -> OldGuardPose {
    OldGuardPose pose {};

    const auto seed_variation =
        static_cast<float>((guard.appearance_seed >> 8U) & 0xFFU) / 255.0F;
    pose.stature_scale = 0.96F + seed_variation * 0.075F;
    const auto orientation =
        finite_orientation_or_identity(guard.platform_orientation);
    const auto yaw = finite_or(guard.yaw_radians, 0.0F);

    pose.body_root =
        glm::translate(
            glm::mat4 {1.0F},
            finite_vec3_or(guard.position, glm::vec3 {0.0F})) *
        glm::mat4_cast(orientation);
    pose.body_root = glm::rotate(
        pose.body_root,
        yaw,
        glm::vec3 {0.0F, 1.0F, 0.0F});
    pose.body_root = glm::scale(
        pose.body_root,
        glm::vec3 {pose.stature_scale});

    const auto locomotion = sample_crew_locomotion(
        finite_or(guard.locomotion_phase, 0.0F),
        saturate(guard.motion_amount),
        CrewGaitStyle::Carry);
    const auto action_progress = saturate(guard.action_progress);
    const auto bayonet_lunge =
        guard.action == OldGuardAction::Bayonet
            ? triangular(
                  action_progress,
                  kOldGuardBayonetHitTime / kOldGuardBayonetSeconds)
            : 0.0F;
    const auto body_advance = bayonet_lunge * 0.24F;

    const auto local_pelvis =
        glm::vec3 {-0.02F + body_advance, 0.80F + locomotion.pelvis_offset_y, 0.0F};
    const auto local_chest =
        glm::vec3 {0.01F + body_advance, 1.25F + locomotion.pelvis_offset_y, 0.0F};
    const auto local_neck =
        glm::vec3 {0.02F + body_advance, 1.54F + locomotion.pelvis_offset_y, 0.0F};
    const auto local_head =
        glm::vec3 {0.03F + body_advance, 1.70F + locomotion.pelvis_offset_y, 0.0F};

    pose.pelvis = transformed_point(pose.body_root, local_pelvis);
    pose.chest = transformed_point(pose.body_root, local_chest);
    pose.neck = transformed_point(pose.body_root, local_neck);
    pose.head = transformed_point(pose.body_root, local_head);

    constexpr std::array<float, 2> kSides {{-1.0F, 1.0F}};
    for (std::size_t index = 0; index < kSides.size(); ++index) {
        const auto side = kSides[index];
        const auto& leg = locomotion.legs[index];
        pose.hips[index] = transformed_point(
            pose.body_root,
            glm::vec3 {
                leg.hip.x + body_advance,
                leg.hip.y,
                side * 0.095F,
            });
        pose.knees[index] = transformed_point(
            pose.body_root,
            glm::vec3 {
                leg.knee.x + body_advance,
                leg.knee.y,
                side * 0.100F,
            });
        pose.ankles[index] = transformed_point(
            pose.body_root,
            glm::vec3 {
                leg.ankle.x + body_advance,
                leg.ankle.y,
                side * 0.100F,
            });
        pose.feet[index] = transformed_point(
            pose.body_root,
            glm::vec3 {
                leg.foot_center.x + body_advance,
                leg.foot_center.y,
                side * 0.100F,
            });
        pose.shoulders[index] = transformed_point(
            pose.body_root,
            glm::vec3 {
                body_advance,
                1.37F + locomotion.pelvis_offset_y,
                side * 0.225F,
            });
    }

    const auto body_forward = safe_direction(
        transformed_point(
            pose.body_root,
            glm::vec3 {1.0F, 0.0F, 0.0F}) -
            transformed_point(
                pose.body_root,
                glm::vec3 {0.0F, 0.0F, 0.0F}),
        glm::vec3 {1.0F, 0.0F, 0.0F});
    const auto body_up = safe_direction(
        transformed_point(
            pose.body_root,
            glm::vec3 {0.0F, 1.0F, 0.0F}) -
            transformed_point(
                pose.body_root,
                glm::vec3 {0.0F, 0.0F, 0.0F}),
        glm::vec3 {0.0F, 1.0F, 0.0F});
    const auto body_right = safe_direction(
        transformed_point(
            pose.body_root,
            glm::vec3 {0.0F, 0.0F, 1.0F}) -
            transformed_point(
                pose.body_root,
                glm::vec3 {0.0F, 0.0F, 0.0F}),
        glm::vec3 {0.0F, 0.0F, 1.0F});
    const auto aimed_forward = safe_direction(guard.aim_direction, body_forward);

    const auto carry_origin = transformed_point(
        pose.body_root,
        glm::vec3 {0.12F + body_advance, 0.58F, -0.29F});
    const auto carry_forward =
        safe_direction(body_up + body_forward * 0.09F, body_up);
    auto aim_origin =
        pose.chest +
        body_up * 0.115F -
        body_right * 0.115F +
        aimed_forward * (0.05F + body_advance);
    auto aim_forward = aimed_forward;

    float shoulder_amount = 0.0F;
    switch (guard.action) {
    case OldGuardAction::RaiseMusket:
        shoulder_amount = smoothstep01(action_progress);
        break;
    case OldGuardAction::StabilizeAim:
    case OldGuardAction::Fire:
    case OldGuardAction::Bayonet:
        shoulder_amount = 1.0F;
        break;
    case OldGuardAction::Reload: {
        const auto reload_progress = saturate(
            1.0F - finite_or(guard.reload_remaining, kOldGuardReloadSeconds) /
                       kOldGuardReloadSeconds);
        pose.reload_stage = reload_stage(reload_progress);
        if (reload_progress < 0.12F) {
            shoulder_amount = 1.0F - 0.35F * smoothstep01(reload_progress / 0.12F);
        } else if (reload_progress < 0.72F) {
            shoulder_amount = 0.25F;
        } else {
            shoulder_amount =
                0.25F +
                0.75F * smoothstep01((reload_progress - 0.72F) / 0.28F);
        }

        const auto muzzle_pitch =
            reload_progress < 0.52F
                ? 0.35F + 0.55F * smoothstep01((reload_progress - 0.12F) / 0.40F)
                : 0.90F - 0.90F * smoothstep01((reload_progress - 0.52F) / 0.48F);
        aim_forward = safe_direction(
            aimed_forward * std::cos(muzzle_pitch) +
                body_up * std::sin(muzzle_pitch),
            aimed_forward);
        aim_origin -= body_up * (0.05F + 0.14F * (1.0F - shoulder_amount));

        if (pose.reload_stage == OldGuardReloadStage::Ramrod) {
            pose.ramrod_offset =
                0.52F *
                triangular((reload_progress - 0.52F) / 0.20F, 0.55F);
        } else if (pose.reload_stage == OldGuardReloadStage::ReturnRamrod) {
            pose.ramrod_offset =
                0.34F *
                (1.0F - smoothstep01((reload_progress - 0.72F) / 0.14F));
        }
        break;
    }
    case OldGuardAction::Patrol:
    case OldGuardAction::Watch:
    default:
        pose.reload_stage = OldGuardReloadStage::Shoulder;
        shoulder_amount = 0.0F;
        break;
    }

    if (guard.action == OldGuardAction::Fire) {
        pose.recoil =
            1.0F - smoothstep01(action_progress);
        aim_origin -= aim_forward * (0.115F * pose.recoil);
    }
    if (guard.action == OldGuardAction::Bayonet) {
        aim_origin += aim_forward * (0.28F * bayonet_lunge);
    }

    const auto weapon_origin =
        glm::mix(carry_origin, aim_origin, shoulder_amount);
    const auto weapon_forward =
        safe_direction(
            glm::mix(carry_forward, aim_forward, shoulder_amount),
            body_forward);
    pose.musket_transform = make_weapon_transform(
        weapon_origin,
        weapon_forward,
        body_up,
        pose.stature_scale);
    pose.muzzle_position = transformed_point(
        pose.musket_transform,
        glm::vec3 {1.24F, 0.0F, 0.0F});
    pose.muzzle_forward = safe_direction(
        transformed_point(
            pose.musket_transform,
            glm::vec3 {1.0F, 0.0F, 0.0F}) -
            transformed_point(
                pose.musket_transform,
                glm::vec3 {0.0F, 0.0F, 0.0F}),
        body_forward);
    pose.bayonet_base = transformed_point(
        pose.musket_transform,
        glm::vec3 {1.10F, 0.035F, 0.0F});
    pose.bayonet_tip = transformed_point(
        pose.musket_transform,
        glm::vec3 {1.76F, 0.035F, 0.0F});

    const auto rear_hand = transformed_point(
        pose.musket_transform,
        glm::vec3 {-0.12F, -0.04F, 0.04F});
    auto forward_hand = transformed_point(
        pose.musket_transform,
        glm::vec3 {0.48F, -0.035F, -0.035F});
    if (guard.action == OldGuardAction::Reload &&
        (pose.reload_stage == OldGuardReloadStage::Cartridge ||
         pose.reload_stage == OldGuardReloadStage::Prime ||
         pose.reload_stage == OldGuardReloadStage::Powder)) {
        forward_hand =
            pose.chest +
            body_up * (pose.reload_stage == OldGuardReloadStage::Powder ? 0.34F : 0.12F) +
            body_right * 0.08F;
    } else if (guard.action == OldGuardAction::Reload &&
               (pose.reload_stage == OldGuardReloadStage::Ramrod ||
                pose.reload_stage == OldGuardReloadStage::ReturnRamrod)) {
        forward_hand = transformed_point(
            pose.musket_transform,
            glm::vec3 {
                0.90F - pose.ramrod_offset,
                0.07F,
                -0.03F,
            });
    }

    pose.hands[0] = rear_hand;
    pose.hands[1] = forward_hand;
    for (std::size_t index = 0; index < pose.elbows.size(); ++index) {
        const auto side = kSides[index];
        const auto hand = pose.hands[index];
        const auto shoulder = pose.shoulders[index];
        pose.elbows[index] =
            glm::mix(shoulder, hand, 0.52F) -
            body_up * 0.10F +
            body_right * (side * 0.075F);
    }

    return pose;
}

} // namespace valcraft
