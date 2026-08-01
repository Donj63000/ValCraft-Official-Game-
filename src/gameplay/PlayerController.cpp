#include "gameplay/PlayerController.h"

#include "gameplay/SeaAdventure.h"
#include "world/OceanAdventureLayout.h"
#include "world/OceanSimulation.h"
#include "world/World.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/trigonometric.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace valcraft {

namespace {

constexpr float kMoveSpeed = 5.6F;
constexpr float kSprintMoveSpeed = 7.2F;
constexpr float kFlySpeed = 10.0F;
constexpr float kJumpVelocity = 7.5F;
constexpr float kJumpCoyoteSeconds = 0.10F;
constexpr float kJumpBufferSeconds = 0.12F;
constexpr float kGravity = 24.0F;
constexpr float kWadeMoveSpeed = 4.0F;
constexpr float kWadeSprintMoveSpeed = 4.8F;
constexpr float kPoolroomsWadeSpeedMultiplier = 0.95F;
constexpr float kWadeGravity = 18.0F;
constexpr float kWadeJumpVelocity = 6.2F;
constexpr float kPoolroomsWadeJumpVelocity = 6.6F;
constexpr float kSwimMoveSpeed = 3.8F;
constexpr float kSwimGravity = 5.5F;
constexpr float kSwimBuoyancy = 7.0F;
constexpr float kSwimVerticalAcceleration = 20.0F;
constexpr float kSwimVerticalDamping = 6.0F;
constexpr float kSwimSinkSpeed = 3.8F;
constexpr float kSwimRiseSpeed = 4.6F;
constexpr float kSwimSurfaceJumpVelocity = 5.2F;
constexpr float kDynamicClimbSpeed = 3.2F;
constexpr float kDynamicClimbContactPadding = 0.08F;
constexpr float kDynamicClimbRetentionPadding = 0.14F;
constexpr float kDynamicClimbAlignmentEpsilon = 0.01F;
constexpr float kDynamicClimbInputThreshold = 1.0e-3F;
constexpr float kDynamicClimbOutwardDetachThreshold = 0.20F;
constexpr float kDynamicClimbExitTolerance = 0.02F;
constexpr float kDynamicClimbDeckSupportTolerance = 0.15F;
constexpr float kWaterFeetSampleHeight = 0.08F;
constexpr float kWaterBodySampleHeight = 1.05F;
constexpr float kMouseSensitivity = 0.08F;
constexpr float kCollisionEpsilon = 0.001F;
constexpr float kFallDamageThreshold = 3.25F;
constexpr float kVoidDamageThreshold = -6.0F;
constexpr float kInvulnerabilityDuration = 0.55F;
constexpr float kHurtFlashDuration = 0.35F;
constexpr float kRegenerationDelay = 6.0F;
constexpr float kRegenerationInterval = 2.5F;
constexpr float kDrowningDamageInterval = 1.0F;
constexpr float kMaxCollisionStep = 0.45F;
constexpr float kDynamicPlatformStepHeight = 0.55F;
constexpr float kDynamicPlatformContactTolerance = 0.04F;
constexpr float kDynamicOverlapRecoveryStep = 0.025F;
constexpr float kDynamicOverlapRecoveryDistance = 1.50F;
constexpr float kDynamicSupportClearanceStep = 0.025F;
constexpr float kDynamicSupportMaximumClearance = 0.60F;
constexpr float kBackroomsConnectorMaximumStepHeight = 1.05F;
constexpr float kBackroomsConnectorSupportTolerance = 1.10F;
constexpr float kBackroomsRampSupportProbeDepth = 2.0F;
constexpr float kBackroomsConnectorCameraStepThreshold = 0.30F;
constexpr float kBackroomsConnectorCameraMaximumLag = 1.10F;
constexpr float kBackroomsConnectorCameraRecoverySharpness = 13.0F;
constexpr float kBodyYawMoveThreshold = 0.15F;
constexpr float kBodyYawMoveTurnSpeed = 540.0F;
constexpr float kBodyYawIdleTurnSpeed = 360.0F;
constexpr float kPrimaryActionDuration = 0.22F;
constexpr float kSecondaryActionDuration = 0.16F;
constexpr float kLandingAnimationDuration = 0.20F;
constexpr float kStepPhaseGroundDistanceScale = 9.8F;
constexpr float kStepPhaseAirDistanceScale = 4.0F;
constexpr float kStepPhaseSwimDistanceScale = 7.4F;
constexpr float kTwoPi = 6.28318530717958647692F;
constexpr float kLookSwayInputScale = 0.045F;
constexpr float kLookSwayResponseSharpness = 18.0F;
constexpr float kLookSwayReturnSharpness = 24.0F;

auto normalized_horizontal(const glm::vec3& vector) -> glm::vec3 {
    const auto horizontal = glm::vec3 {vector.x, 0.0F, vector.z};
    const auto length = glm::length(horizontal);
    if (length <= 1.0e-5F) {
        return {};
    }
    return horizontal / length;
}

auto finite_or(float value, float fallback) noexcept -> float {
    return std::isfinite(value) ? value : fallback;
}

auto non_negative_finite_or(float value, float fallback) noexcept -> float {
    return std::isfinite(value) ? std::max(value, 0.0F) : fallback;
}

auto clamped_non_negative_finite_or(float value, float fallback, float maximum) noexcept -> float {
    return std::clamp(non_negative_finite_or(value, fallback), 0.0F, maximum);
}

auto non_negative_finite(float value) noexcept -> float {
    return non_negative_finite_or(value, 0.0F);
}

auto finite_input_axis(float value) noexcept -> float {
    return std::clamp(finite_or(value, 0.0F), -1.0F, 1.0F);
}

auto finite_vec3_or(const glm::vec3& value, const glm::vec3& fallback) noexcept -> glm::vec3 {
    return {
        finite_or(value.x, fallback.x),
        finite_or(value.y, fallback.y),
        finite_or(value.z, fallback.z),
    };
}

auto wrap_degrees(float angle) noexcept -> float {
    if (!std::isfinite(angle)) {
        return 0.0F;
    }
    angle = std::fmod(angle, 360.0F);
    if (angle <= -180.0F) {
        angle += 360.0F;
    }
    if (angle > 180.0F) {
        angle -= 360.0F;
    }
    return angle;
}

auto wrap_step_phase(float step_phase) noexcept -> float {
    if (!std::isfinite(step_phase)) {
        return 0.0F;
    }
    step_phase = std::remainder(step_phase, kTwoPi);
    if (step_phase < 0.0F) {
        step_phase += kTwoPi;
    }
    return step_phase;
}

auto rotate_towards_degrees(float current, float target, float max_delta) noexcept -> float {
    const auto delta = wrap_degrees(target - current);
    return wrap_degrees(current + std::clamp(delta, -max_delta, max_delta));
}

auto yaw_degrees_from_direction(const glm::vec2& direction) noexcept -> float {
    return static_cast<float>(glm::degrees(std::atan2(direction.y, direction.x)));
}

auto damp_towards(float current, float target, float sharpness, float dt) noexcept -> float {
    if (dt <= 1.0e-6F) {
        return target;
    }

    const auto blend = 1.0F - std::exp(-std::max(sharpness, 0.0F) * dt);
    return glm::mix(current, target, blend);
}

auto snap_small_sway(float value) noexcept -> float {
    return std::abs(value) < 1.0e-3F ? 0.0F : value;
}

auto block_break_crack_stage(float progress) noexcept -> std::uint8_t {
    const auto clamped_progress = std::clamp(progress, 0.0F, 1.0F);
    const auto scaled_stage = static_cast<int>(clamped_progress * static_cast<float>(kBlockBreakStageCount));
    return static_cast<std::uint8_t>(std::clamp(scaled_stage, 0, static_cast<int>(kBlockBreakStageCount) - 1));
}

auto player_physics_block(const World& world, int x, int y, int z) -> BlockId {
    // Je garde les collisions du joueur coherentes meme si le chunk voisin n'est
    // pas encore charge, sinon une frame de streaming ouvre un faux passage.
    return world.peek_block_or_generated(x, y, z);
}

} // namespace

PlayerController::PlayerController(glm::vec3 spawn_position) {
    state_.position = finite_vec3_or(spawn_position, state_.position);
    state_.fall_start_y = state_.position.y;
    state_.body_yaw_degrees = state_.yaw_degrees;
}

void PlayerController::update(
    const PlayerInput& input,
    float dt,
    const World& world,
    const ShipEntity* dynamic_obstacle,
    const OceanState* dynamic_ocean) {
    const auto clamped_dt = non_negative_finite(dt);
    const auto move_forward = finite_input_axis(input.move_forward);
    const auto move_right = finite_input_axis(input.move_right);
    const auto move_up = finite_input_axis(input.move_up);
    const auto look_delta_x = finite_or(input.look_delta_x, 0.0F);
    const auto look_delta_y = finite_or(input.look_delta_y, 0.0F);

    if (dynamic_obstacle == nullptr ||
        (climbed_dynamic_obstacle_ != nullptr && climbed_dynamic_obstacle_ != dynamic_obstacle)) {
        reset_dynamic_climb_state();
    }

    if (state_.dead) {
        state_.velocity = {};
        reset_jump_assist_state();
        reset_dynamic_climb_state();
        state_.hurt_timer = std::max(0.0F, state_.hurt_timer - clamped_dt);
        state_.landing_impact = std::max(0.0F, state_.landing_impact - clamped_dt / kLandingAnimationDuration);
        state_.look_sway_yaw = damp_towards(state_.look_sway_yaw, 0.0F, kLookSwayReturnSharpness, clamped_dt);
        state_.look_sway_pitch = damp_towards(state_.look_sway_pitch, 0.0F, kLookSwayReturnSharpness, clamped_dt);
        state_.look_sway_yaw = snap_small_sway(state_.look_sway_yaw);
        state_.look_sway_pitch = snap_small_sway(state_.look_sway_pitch);
        return;
    }

    if (dynamic_obstacle != nullptr) {
        // Je repare une penetration heritee du pas precedent avant de calculer
        // les appuis et les commandes. La physique ne doit jamais demarrer sa
        // dichotomie depuis une fraction zero deja invalide.
        (void)resolve_dynamic_obstacle_overlap(
            world,
            *dynamic_obstacle);
    }

    const auto was_on_ground = state_.on_ground;
    const auto water_contact_before_move =
        sample_water_contact(
            world,
            state_.position,
            dynamic_obstacle,
            dynamic_ocean);
    state_.animation_time += clamped_dt;
    state_.hurt_timer = std::max(0.0F, state_.hurt_timer - clamped_dt);
    state_.damage_cooldown = std::max(0.0F, state_.damage_cooldown - clamped_dt);
    state_.regen_delay = std::max(0.0F, state_.regen_delay - clamped_dt);
    state_.landing_impact = std::max(0.0F, state_.landing_impact - clamped_dt / kLandingAnimationDuration);
    if (std::abs(move_up) <= kDynamicClimbInputThreshold) {
        dynamic_climb_regrab_locked_ = false;
    }
    if (!input.jump) {
        dynamic_climb_jump_locked_ = false;
    }
    jump_buffer_timer_ = input.jump && !dynamic_climb_jump_locked_
                             ? kJumpBufferSeconds
                             : std::max(0.0F, jump_buffer_timer_ - clamped_dt);
    ground_coyote_timer_ = std::max(0.0F, ground_coyote_timer_ - clamped_dt);

    const auto advance_action_progress = [clamped_dt](float& progress, bool& active, float duration) {
        if (!active) {
            return;
        }

        progress = std::min(1.0F, progress + clamped_dt / std::max(duration, 1.0e-4F));
        if (progress >= 1.0F) {
            progress = 0.0F;
            active = false;
        }
    };
    advance_action_progress(state_.primary_action_progress, state_.primary_action_active, kPrimaryActionDuration);
    advance_action_progress(state_.secondary_action_progress, state_.secondary_action_active, kSecondaryActionDuration);

    state_.on_ground = false;

    if (input.toggle_fly) {
        state_.fly_mode = !state_.fly_mode;
        reset_jump_assist_state();
        reset_dynamic_climb_state();
        if (state_.fly_mode) {
            state_.velocity = {};
        }
    }

    if (state_.fly_mode) {
        reset_dynamic_climb_state();
    }

    state_.yaw_degrees = wrap_degrees(finite_or(state_.yaw_degrees, -90.0F) + look_delta_x * kMouseSensitivity);
    state_.pitch_degrees =
        std::clamp(finite_or(state_.pitch_degrees, -18.0F) - look_delta_y * kMouseSensitivity, -89.0F, 89.0F);

    const auto target_look_sway_yaw = glm::clamp(-look_delta_x * kLookSwayInputScale, -1.0F, 1.0F);
    const auto target_look_sway_pitch = glm::clamp(look_delta_y * kLookSwayInputScale, -1.0F, 1.0F);
    const auto look_input_active = std::abs(look_delta_x) > 1.0e-4F || std::abs(look_delta_y) > 1.0e-4F;
    const auto look_sway_sharpness = look_input_active ? kLookSwayResponseSharpness : kLookSwayReturnSharpness;
    state_.look_sway_yaw = damp_towards(state_.look_sway_yaw, target_look_sway_yaw, look_sway_sharpness, clamped_dt);
    state_.look_sway_pitch = damp_towards(state_.look_sway_pitch, target_look_sway_pitch, look_sway_sharpness, clamped_dt);
    state_.look_sway_yaw = snap_small_sway(state_.look_sway_yaw);
    state_.look_sway_pitch = snap_small_sway(state_.look_sway_pitch);

    auto forward = normalized_horizontal(look_direction());
    if (glm::dot(forward, forward) <= 1.0e-6F) {
        forward = {0.0F, 0.0F, -1.0F};
    }
    const auto right = glm::cross(forward, glm::vec3 {0.0F, 1.0F, 0.0F});
    auto wish = forward * move_forward + right * move_right;
    if (glm::dot(wish, wish) > 1.0e-5F) {
        wish = glm::normalize(wish);
    }

    const auto climb_contact_at = [dynamic_obstacle](const glm::vec3& feet_position, float padding)
        -> std::optional<ShipClimbContact> {
        if (dynamic_obstacle == nullptr) {
            return std::nullopt;
        }

        constexpr float half_width = kPlayerWidth * 0.5F;
        const auto horizontal_extent = half_width + padding;
        return dynamic_obstacle->climb_contact(
            {
                feet_position.x - horizontal_extent,
                feet_position.y - padding,
                feet_position.z - horizontal_extent,
            },
            {
                feet_position.x + horizontal_extent,
                feet_position.y + kPlayerHeight + padding,
                feet_position.z + horizontal_extent,
            });
    };

    const auto climb_normal = [](const ShipClimbContact& contact) noexcept {
        const auto normal =
            finite_vec3_or(
                contact.outward_normal,
                {});
        const auto length =
            glm::length(normal);
        return length > 1.0e-5F
                   ? normal / length
                   : glm::vec3 {0.0F};
    };

    const auto climb_up =
        [&climb_normal](
            const ShipClimbContact& contact) noexcept {
            const auto normal =
                climb_normal(contact);
            auto up =
                finite_vec3_or(
                    contact.up_direction,
                    {0.0F, 1.0F, 0.0F});
            // Je projette l'axe vertical du navire dans le plan du filet pour
            // neutraliser toute petite erreur numerique d'orthogonalite.
            up -= normal *
                  glm::dot(up, normal);
            const auto length =
                glm::length(up);
            return length > 1.0e-5F
                       ? up / length
                       : glm::vec3 {
                             0.0F,
                             1.0F,
                             0.0F,
                         };
        };

    const auto align_outside_climb_contact =
        [&climb_normal](
            glm::vec3& feet_position,
            const ShipClimbContact& contact) noexcept {
            const auto normal =
                climb_normal(contact);
            if (glm::dot(normal, normal) <=
                1.0e-5F) {
                return false;
            }

            constexpr auto half_width =
                kPlayerWidth * 0.5F;
            const glm::vec3 half_extents {
                half_width,
                kPlayerHeight * 0.5F,
                half_width,
            };
            const auto player_center =
                feet_position +
                glm::vec3 {
                    0.0F,
                    kPlayerHeight * 0.5F,
                    0.0F,
                };

            // Rayon de projection de l'AABB verticale du joueur sur la normale
            // du filet. Cette formule reste exacte meme avec roulis/tangage.
            const auto projected_radius =
                glm::dot(
                    glm::abs(normal),
                    half_extents);
            const auto target_projection =
                glm::dot(
                    finite_vec3_or(
                        contact.plane_point,
                        player_center),
                    normal) +
                projected_radius +
                kDynamicClimbAlignmentEpsilon;
            const auto correction =
                target_projection -
                glm::dot(
                    player_center,
                    normal);

            // Un contact valide ne requiert jamais un grand teleport. Cette
            // garde bloque une pose corrompue ou une normale incoherente.
            if (!std::isfinite(correction) ||
                std::abs(correction) > 1.0F) {
                return false;
            }

            feet_position +=
                normal * correction;
            return true;
        };

    auto active_climb_contact = std::optional<ShipClimbContact> {};
    if (climbed_dynamic_obstacle_ != nullptr) {
        active_climb_contact = climb_contact_at(state_.position, kDynamicClimbRetentionPadding);
        if (!active_climb_contact.has_value()) {
            climbed_dynamic_obstacle_ = nullptr;
            dynamic_climb_regrab_locked_ = true;
        }
    }

    if (climbed_dynamic_obstacle_ == nullptr &&
        !state_.fly_mode &&
        !dynamic_climb_regrab_locked_ &&
        std::abs(move_up) > kDynamicClimbInputThreshold) {
        active_climb_contact = climb_contact_at(state_.position, kDynamicClimbContactPadding);
        if (active_climb_contact.has_value() &&
            align_outside_climb_contact(state_.position, *active_climb_contact)) {
            // Je n'entre dans l'etat d'escalade qu'avec une intention verticale
            // explicite. Une collision passive avec le greement ne colle jamais.
            climbed_dynamic_obstacle_ = dynamic_obstacle;
            state_.velocity = {};
            state_.on_ground = false;
            state_.fall_start_y = state_.position.y;
            state_.airborne_time = 0.0F;
            state_.landing_impact = 0.0F;
            reset_jump_assist_state();
        } else {
            active_climb_contact.reset();
        }
    }

    if (climbed_dynamic_obstacle_ != nullptr && active_climb_contact.has_value()) {
        reset_jump_assist_state();
        const auto normal = climb_normal(*active_climb_contact);
        if (glm::dot(wish, normal) > kDynamicClimbOutwardDetachThreshold) {
            // Je laisse un mouvement volontaire vers la mer detacher le joueur
            // au lieu de le retenir artificiellement au filet.
            climbed_dynamic_obstacle_ = nullptr;
            dynamic_climb_regrab_locked_ = true;
            active_climb_contact.reset();
        }
    }

    const auto standing_on_dynamic_obstacle =
        [this, dynamic_obstacle](const glm::vec3& feet_position) noexcept {
        if (dynamic_obstacle == nullptr) {
            return false;
        }
        const auto support_height =
            dynamic_support_height_at(
                *dynamic_obstacle,
                feet_position,
                feet_position.y -
                    kDynamicSupportMaximumClearance -
                    kDynamicPlatformContactTolerance,
                feet_position.y +
                    kDynamicPlatformContactTolerance);
        return support_height.has_value() && std::abs(feet_position.y - *support_height) <= 0.01F;
    };
    const auto standing_on_solid = collides_at(world, state_.position + glm::vec3 {0.0F, -0.05F, 0.0F}) ||
                                   standing_on_dynamic_obstacle(state_.position);
    if (standing_on_solid) {
        ground_coyote_timer_ = kJumpCoyoteSeconds;
    }
    const auto has_buffered_jump = [&]() noexcept {
        return jump_buffer_timer_ > 0.0F;
    };
    const auto can_ground_jump = [&]() noexcept {
        return has_buffered_jump() && ground_coyote_timer_ > 0.0F;
    };
    const auto consume_jump_assist = [&]() noexcept {
        jump_buffer_timer_ = 0.0F;
        ground_coyote_timer_ = 0.0F;
    };
    const auto sprinting = input.sprint && move_forward > 0.0F && glm::dot(wish, wish) > 1.0e-5F;

    if (climbed_dynamic_obstacle_ != nullptr && active_climb_contact.has_value()) {
        const auto normal =
            climb_normal(
                *active_climb_contact);
        const auto up =
            climb_up(
                *active_climb_contact);
        auto side =
            glm::cross(up, normal);
        const auto side_length =
            glm::length(side);
        side = side_length > 1.0e-5F
                   ? side / side_length
                   : glm::vec3 {};

        const auto climb_speed =
            kDynamicClimbSpeed *
            movement_speed_multiplier_;
        const auto lateral_input =
            std::clamp(
                glm::dot(wish, side),
                -1.0F,
                1.0F);
        state_.velocity =
            (
                side * lateral_input +
                up * move_up
            ) *
            climb_speed;
        state_.on_ground = false;
        state_.fall_start_y = state_.position.y;
        state_.airborne_time = 0.0F;
        state_.landing_impact = 0.0F;
        reset_jump_assist_state();
    } else if (state_.fly_mode) {
        reset_jump_assist_state();
        auto fly_velocity = wish + glm::vec3 {0.0F, move_up, 0.0F};
        if (glm::dot(fly_velocity, fly_velocity) > 1.0e-5F) {
            fly_velocity = glm::normalize(fly_velocity) * kFlySpeed * movement_speed_multiplier_;
        }
        state_.velocity = fly_velocity;
    } else if (water_contact_before_move.swimming) {
        const auto move_speed = kSwimMoveSpeed * movement_speed_multiplier_;
        state_.velocity.x = wish.x * move_speed;
        state_.velocity.z = wish.z * move_speed;
        state_.velocity.y += (move_up * kSwimVerticalAcceleration - kSwimGravity) * clamped_dt;
        if (water_contact_before_move.head_in_water) {
            state_.velocity.y += kSwimBuoyancy * clamped_dt;
        }
        state_.velocity.y /= 1.0F + kSwimVerticalDamping * clamped_dt;
        state_.velocity.y = std::clamp(state_.velocity.y, -kSwimSinkSpeed, kSwimRiseSpeed);

        if (!water_contact_before_move.head_in_water && has_buffered_jump() && standing_on_solid) {
            state_.velocity.y = std::max(state_.velocity.y, kSwimSurfaceJumpVelocity);
            consume_jump_assist();
        }
    } else if (water_contact_before_move.feet_in_water) {
        // Dans les Poolrooms, je conserve presque toute la mobilité au lieu
        // d'appliquer la pénalité lourde prévue pour la mer et les rivières.
        const auto move_speed =
            (water_movement_profile_ ==
                     PlayerWaterMovementProfile::Poolrooms
                 ? (sprinting ? kSprintMoveSpeed : kMoveSpeed) *
                       kPoolroomsWadeSpeedMultiplier
                 : (sprinting
                        ? kWadeSprintMoveSpeed
                        : kWadeMoveSpeed)) *
            movement_speed_multiplier_;
        state_.velocity.x = wish.x * move_speed;
        state_.velocity.z = wish.z * move_speed;
        state_.velocity.y -= kWadeGravity * clamped_dt;
        if (can_ground_jump()) {
            // Dans les bassins encaisses, je garde une marge suffisante pour
            // franchir proprement la rive haute d'un voxel sans escalade ni
            // teleportation. Les autres eaux conservent leur saut historique.
            state_.velocity.y =
                water_movement_profile_ ==
                        PlayerWaterMovementProfile::Poolrooms
                    ? kPoolroomsWadeJumpVelocity
                    : kWadeJumpVelocity;
            consume_jump_assist();
        }
    } else {
        const auto move_speed = (sprinting ? kSprintMoveSpeed : kMoveSpeed) * movement_speed_multiplier_;
        state_.velocity.x = wish.x * move_speed;
        state_.velocity.z = wish.z * move_speed;
        state_.velocity.y -= kGravity * clamped_dt;
        if (can_ground_jump()) {
            state_.velocity.y = kJumpVelocity;
            consume_jump_assist();
        }
    }

    const auto move_axis_safely = [&](float delta, int axis) {
        auto remaining = delta;
        while (std::abs(remaining) > 1.0e-6F) {
            const auto step = std::clamp(remaining, -kMaxCollisionStep, kMaxCollisionStep);
            const auto before = state_.position[axis];
            move_axis(step, axis, world, dynamic_obstacle);

            const auto moved = state_.position[axis] - before;
            if (std::abs(moved) + 1.0e-5F < std::abs(step)) {
                break;
            }

            remaining -= step;
        }
    };

    const auto start_position = state_.position;
    auto exited_dynamic_climb_at_top = false;
    auto exited_dynamic_climb_at_bottom = false;

    const auto active_climb_local_position =
        climbed_dynamic_obstacle_ != nullptr &&
                active_climb_contact.has_value()
            ? dynamic_obstacle->world_to_local_point(
                  state_.position)
            : glm::vec3 {};
    const auto active_climb_local_velocity =
        climbed_dynamic_obstacle_ != nullptr &&
                active_climb_contact.has_value()
            ? dynamic_obstacle->world_to_local_direction(
                  state_.velocity)
            : glm::vec3 {};
    const auto deck_exit_local_y =
        climbed_dynamic_obstacle_ != nullptr &&
                active_climb_contact.has_value()
            ? dynamic_obstacle->world_to_local_point(
                  active_climb_contact->deck_exit)
                  .y
            : 0.0F;

    if (climbed_dynamic_obstacle_ != nullptr &&
        active_climb_contact.has_value() &&
        active_climb_local_velocity.y > 0.0F &&
        active_climb_local_position.y +
                active_climb_local_velocity.y *
                    clamped_dt >=
            deck_exit_local_y -
                kDynamicClimbExitTolerance) {
        constexpr float half_width =
            kPlayerWidth * 0.5F;
        const auto deck_exit =
            finite_vec3_or(
                active_climb_contact->deck_exit,
                state_.position);
        const auto deck_exit_min = glm::vec3 {
            deck_exit.x - half_width,
            deck_exit.y,
            deck_exit.z - half_width,
        };
        const auto deck_exit_max = glm::vec3 {
            deck_exit.x + half_width,
            deck_exit.y + kPlayerHeight,
            deck_exit.z + half_width,
        };
        const auto deck_support = dynamic_obstacle->support_height(deck_exit);
        const auto has_safe_deck_support =
            deck_support.has_value() &&
            std::abs(deck_exit.y - *deck_support) <= kDynamicClimbDeckSupportTolerance;
        const auto deck_exit_blocked =
            collides_at(world, deck_exit) ||
            dynamic_obstacle->intersects_aabb(deck_exit_min, deck_exit_max);

        if (has_safe_deck_support && !deck_exit_blocked) {
            // Je termine la montee uniquement apres avoir revalide le point
            // interieur : une future evolution du pont ne doit jamais faire
            // traverser une cloison ou deposer le joueur sans support.
            state_.position = deck_exit;
            state_.velocity = {};
            state_.on_ground = false;
            state_.fall_start_y = state_.position.y;
            state_.airborne_time = 0.0F;
            state_.landing_impact = 0.0F;
            climbed_dynamic_obstacle_ = nullptr;
            dynamic_climb_regrab_locked_ = true;
            dynamic_climb_jump_locked_ = input.jump;
            reset_jump_assist_state();
            exited_dynamic_climb_at_top = true;
        } else {
            // Si la sortie est obstruee, je reste au dernier barreau sans
            // accumuler de vitesse verticale ni de distance de chute.
            auto clamped_local_position =
                dynamic_obstacle->world_to_local_point(
                    state_.position);
            clamped_local_position.y =
                std::min(
                    clamped_local_position.y,
                    deck_exit_local_y -
                        kDynamicClimbExitTolerance);
            state_.position =
                dynamic_obstacle->local_to_world_point(
                    clamped_local_position);
            (void)align_outside_climb_contact(
                state_.position,
                *active_climb_contact);
            state_.velocity = {};
            state_.fall_start_y = state_.position.y;
            state_.airborne_time = 0.0F;
            reset_jump_assist_state();
        }
    } else {
        move_axis_safely(state_.velocity.x * clamped_dt, 0);
        move_axis_safely(state_.velocity.y * clamped_dt, 1);
        move_axis_safely(state_.velocity.z * clamped_dt, 2);

        if (climbed_dynamic_obstacle_ != nullptr && active_climb_contact.has_value()) {
            const auto local_position_after_move =
                dynamic_obstacle->world_to_local_point(
                    state_.position);
            const auto local_velocity_after_move =
                dynamic_obstacle->world_to_local_direction(
                    state_.velocity);
            if (local_velocity_after_move.y < 0.0F &&
                local_position_after_move.y <=
                    active_climb_contact->local_bounds.min.y +
                        kDynamicClimbExitTolerance) {
                // La limite basse est mesuree dans le repere du navire. Elle
                // reste donc correcte lorsque le filet monte d'un cote et
                // descend de l'autre pendant le roulis.
                auto released_local_position =
                    local_position_after_move;
                released_local_position.y =
                    std::min(
                        released_local_position.y,
                        active_climb_contact->local_bounds.min.y -
                            kCollisionEpsilon);
                state_.position =
                    dynamic_obstacle->local_to_world_point(
                        released_local_position);
                state_.velocity -=
                    active_climb_contact->up_direction *
                    glm::dot(
                        state_.velocity,
                        active_climb_contact->up_direction);
                state_.fall_start_y = state_.position.y;
                climbed_dynamic_obstacle_ = nullptr;
                dynamic_climb_regrab_locked_ = true;
                reset_jump_assist_state();
                exited_dynamic_climb_at_bottom = true;
            } else {
                auto contact_after_move = climb_contact_at(
                    state_.position,
                    kDynamicClimbRetentionPadding);
                if (!contact_after_move.has_value() ||
                    !align_outside_climb_contact(state_.position, *contact_after_move)) {
                    // Je ne borne pas artificiellement le deplacement le long du
                    // filet : depasser un bord constitue une vraie sortie laterale.
                    climbed_dynamic_obstacle_ = nullptr;
                    dynamic_climb_regrab_locked_ = true;
                    reset_jump_assist_state();
                } else {
                    active_climb_contact = contact_after_move;
                    state_.fall_start_y = state_.position.y;
                    state_.airborne_time = 0.0F;
                }
            }
        }
    }
    const auto supported_by_backrooms_connector =
        resolve_backrooms_connector_support(
            world,
            was_on_ground);
    const auto connector_vertical_delta =
        state_.position.y - start_position.y;
    if (world.generation_profile() ==
            WorldGenerationProfile::Backrooms &&
        was_on_ground &&
        supported_by_backrooms_connector &&
        std::abs(connector_vertical_delta) >
            kBackroomsConnectorCameraStepThreshold) {
        // Je laisse les pieds suivre immédiatement la vraie marche dans les
        // deux sens, puis je résorbe seulement le retard visuel de l'œil. La
        // garde au sol exclut les chutes et les réceptions accidentelles.
        connector_camera_offset_y_ =
            std::clamp(
                connector_camera_offset_y_ -
                    connector_vertical_delta,
                -kBackroomsConnectorCameraMaximumLag,
                kBackroomsConnectorCameraMaximumLag);
    }
    connector_camera_offset_y_ =
        damp_towards(
            connector_camera_offset_y_,
            0.0F,
            kBackroomsConnectorCameraRecoverySharpness,
            clamped_dt);
    const auto horizontal_displacement = glm::vec2 {
        state_.position.x - start_position.x,
        state_.position.z - start_position.z,
    };
    const auto horizontal_distance = glm::length(horizontal_displacement);
    const auto water_contact_after_move =
        sample_water_contact(
            world,
            state_.position,
            dynamic_obstacle,
            dynamic_ocean);
    auto landed_in_water = false;

    if (!state_.fly_mode) {
        state_.on_ground = supported_by_backrooms_connector ||
                           collides_at(world, state_.position + glm::vec3 {0.0F, -0.05F, 0.0F}) ||
                           standing_on_dynamic_obstacle(state_.position);
        if (state_.on_ground) {
            ground_coyote_timer_ = kJumpCoyoteSeconds;
        }
        if (state_.on_ground && !was_on_ground && !exited_dynamic_climb_at_top) {
            landed_in_water =
                water_contact_after_move.feet_in_water || water_contact_after_move.body_in_water || water_contact_after_move.head_in_water;
            const auto fall_distance = state_.fall_start_y - state_.position.y;
            const auto safe_fall_distance = kFallDamageThreshold * fall_safety_multiplier_;
            if (!landed_in_water && fall_distance > safe_fall_distance) {
                apply_damage(std::ceil(fall_distance - safe_fall_distance), PlayerDeathCause::Fall, true);
            }
            state_.landing_impact = landed_in_water ? 0.0F : 1.0F;
        } else if (exited_dynamic_climb_at_top) {
            state_.landing_impact = 0.0F;
        }
        if (state_.on_ground && state_.velocity.y < 0.0F) {
            state_.velocity.y = 0.0F;
        }
        if (state_.on_ground || water_contact_after_move.swimming ||
            climbed_dynamic_obstacle_ != nullptr || exited_dynamic_climb_at_bottom) {
            state_.fall_start_y = state_.position.y;
        } else {
            state_.fall_start_y = std::max(state_.fall_start_y, state_.position.y);
        }
        if (state_.on_ground && has_buffered_jump() && !water_contact_after_move.swimming &&
            !dynamic_climb_jump_locked_) {
            state_.velocity.y =
                water_contact_after_move.feet_in_water
                    ? (water_movement_profile_ ==
                               PlayerWaterMovementProfile::Poolrooms
                           ? kPoolroomsWadeJumpVelocity
                           : kWadeJumpVelocity)
                    : kJumpVelocity;
            state_.on_ground = false;
            state_.fall_start_y = state_.position.y;
            consume_jump_assist();
        }
    } else {
        state_.fall_start_y = state_.position.y;
    }

    const auto step_phase_scale = water_contact_after_move.swimming
                                      ? kStepPhaseSwimDistanceScale
                                      : (state_.on_ground ? kStepPhaseGroundDistanceScale : kStepPhaseAirDistanceScale);
    state_.step_phase += horizontal_distance * step_phase_scale;
    if (state_.step_phase >= kTwoPi || state_.step_phase <= -kTwoPi) {
        state_.step_phase = std::remainder(state_.step_phase, kTwoPi);
    }
    if (state_.step_phase < 0.0F) {
        state_.step_phase += kTwoPi;
    }

    if (!state_.fly_mode && !state_.on_ground && !water_contact_after_move.swimming &&
        climbed_dynamic_obstacle_ == nullptr) {
        state_.airborne_time += clamped_dt;
    } else {
        state_.airborne_time = 0.0F;
    }

    update_body_yaw(clamped_dt, horizontal_displacement);
    // Je reutilise l'echantillon post-deplacement pour la survie : refaire les
    // quinze sondes ici donnait exactement le meme etat pour cette frame.
    update_survival_state(clamped_dt, water_contact_after_move);
}

auto PlayerController::state() const noexcept -> const PlayerState& {
    return state_;
}

auto PlayerController::position() const noexcept -> const glm::vec3& {
    return state_.position;
}

auto PlayerController::eye_position() const noexcept -> glm::vec3 {
    return state_.position +
           glm::vec3 {
               0.0F,
               kEyeHeight + connector_camera_offset_y_,
               0.0F,
           };
}

auto PlayerController::look_direction() const noexcept -> glm::vec3 {
    const auto yaw = glm::radians(state_.yaw_degrees);
    const auto pitch = glm::radians(state_.pitch_degrees);
    return glm::normalize(glm::vec3 {
        std::cos(yaw) * std::cos(pitch),
        std::sin(pitch),
        std::sin(yaw) * std::cos(pitch),
    });
}

auto PlayerController::view_matrix() const -> glm::mat4 {
    return glm::lookAt(eye_position(), eye_position() + look_direction(), glm::vec3 {0.0F, 1.0F, 0.0F});
}

auto PlayerController::selected_block() const noexcept -> BlockId {
    return block_item_id(selected_block_);
}

auto PlayerController::max_health() const noexcept -> float {
    return max_health_;
}

auto PlayerController::max_air_seconds() const noexcept -> float {
    return kMaxAirSeconds;
}

auto PlayerController::damage_resistance_percent() const noexcept -> float {
    return damage_resistance_percent_;
}

auto PlayerController::apnea_resistance_percent() const noexcept -> float {
    return apnea_resistance_percent_;
}

auto PlayerController::fall_safety_multiplier() const noexcept -> float {
    return fall_safety_multiplier_;
}

auto PlayerController::movement_speed_multiplier() const noexcept -> float {
    return movement_speed_multiplier_;
}

auto PlayerController::water_movement_profile() const noexcept
    -> PlayerWaterMovementProfile {
    return water_movement_profile_;
}

auto PlayerController::block_break_speed_multiplier() const noexcept -> float {
    return block_break_speed_multiplier_;
}

auto PlayerController::is_dead() const noexcept -> bool {
    return state_.dead;
}

auto PlayerController::is_climbing_dynamic_obstacle() const noexcept -> bool {
    return climbed_dynamic_obstacle_ != nullptr && !state_.dead && !state_.fly_mode;
}

void PlayerController::load_state(const PlayerState& state) noexcept {
    const PlayerState defaults {};
    state_ = state;
    block_break_progress_ = {};
    reset_jump_assist_state();
    reset_dynamic_climb_state();
    connector_camera_offset_y_ = 0.0F;
    state_.position = finite_vec3_or(state_.position, defaults.position);
    state_.velocity = finite_vec3_or(state_.velocity, {});
    state_.yaw_degrees = wrap_degrees(finite_or(state_.yaw_degrees, defaults.yaw_degrees));
    state_.pitch_degrees = std::clamp(finite_or(state_.pitch_degrees, defaults.pitch_degrees), -89.0F, 89.0F);
    state_.body_yaw_degrees = wrap_degrees(finite_or(state_.body_yaw_degrees, state_.yaw_degrees));
    state_.animation_time = clamped_non_negative_finite_or(state_.animation_time, 0.0F, 3600.0F);
    state_.step_phase = wrap_step_phase(state_.step_phase);
    state_.health = std::clamp(finite_or(state_.health, max_health_), 0.0F, max_health_);
    state_.air_seconds = std::clamp(finite_or(state_.air_seconds, kMaxAirSeconds), 0.0F, kMaxAirSeconds);
    state_.hurt_timer = clamped_non_negative_finite_or(state_.hurt_timer, 0.0F, kHurtFlashDuration);
    state_.damage_cooldown = clamped_non_negative_finite_or(state_.damage_cooldown, 0.0F, kInvulnerabilityDuration);
    state_.regen_delay = clamped_non_negative_finite_or(state_.regen_delay, 0.0F, kRegenerationDelay);
    state_.regen_tick_timer = clamped_non_negative_finite_or(state_.regen_tick_timer, 0.0F, kRegenerationInterval);
    state_.drowning_tick_timer = clamped_non_negative_finite_or(state_.drowning_tick_timer, 0.0F, kDrowningDamageInterval);
    state_.fall_start_y = finite_or(state_.fall_start_y, state_.position.y);
    if (std::abs(state_.fall_start_y - state_.position.y) > 128.0F) {
        state_.fall_start_y = state_.position.y;
    }
    state_.primary_action_progress = std::clamp(finite_or(state_.primary_action_progress, 0.0F), 0.0F, 1.0F);
    state_.secondary_action_progress = std::clamp(finite_or(state_.secondary_action_progress, 0.0F), 0.0F, 1.0F);
    state_.landing_impact = std::clamp(finite_or(state_.landing_impact, 0.0F), 0.0F, 1.0F);
    state_.airborne_time = clamped_non_negative_finite_or(state_.airborne_time, 0.0F, 60.0F);
    state_.look_sway_yaw = std::clamp(finite_or(state_.look_sway_yaw, 0.0F), -1.0F, 1.0F);
    state_.look_sway_pitch = std::clamp(finite_or(state_.look_sway_pitch, 0.0F), -1.0F, 1.0F);
    if (state_.dead) {
        state_.velocity = {};
    }
}

void PlayerController::set_position(const glm::vec3& position) noexcept {
    state_.position = finite_vec3_or(position, state_.position);
    state_.fall_start_y = state_.position.y;
    block_break_progress_ = {};
    reset_jump_assist_state();
    reset_dynamic_climb_state();
    connector_camera_offset_y_ = 0.0F;
}

void PlayerController::translate_platform_delta(const glm::vec3& delta) noexcept {
    const auto safe_delta = finite_vec3_or(delta, {});
    state_.position += safe_delta;
    if (std::abs(safe_delta.y) > 1.0e-6F) {
        state_.fall_start_y += safe_delta.y;
    }
}

void PlayerController::resolve_dynamic_platform_support(float support_height) noexcept {
    if (state_.dead || state_.fly_mode || !std::isfinite(support_height) || state_.velocity.y > 0.0F) {
        return;
    }

    constexpr float kPlatformSnapTolerance = 0.30F;
    if (std::abs(state_.position.y - support_height) > kPlatformSnapTolerance) {
        return;
    }
    if (state_.velocity.y >= 0.0F && state_.position.y > support_height + 0.01F) {
        return;
    }

    // Je termine la resolution verticale de la plateforme apres la physique du
    // monde afin que le joueur ne chute pas entre deux ticks du navire dynamique.
    state_.position.y = support_height;
    state_.velocity.y = 0.0F;
    state_.on_ground = true;
    state_.fall_start_y = support_height;
    state_.airborne_time = 0.0F;
    ground_coyote_timer_ = kJumpCoyoteSeconds;
}

void PlayerController::set_velocity(const glm::vec3& velocity) noexcept {
    state_.velocity = finite_vec3_or(velocity, {});
}

void PlayerController::set_fly_mode_enabled(bool enabled) noexcept {
    if (enabled) {
        reset_dynamic_climb_state();
    }
    if (state_.fly_mode == enabled) {
        return;
    }

    state_.fly_mode = enabled;
    state_.velocity = {};
    state_.fall_start_y = state_.position.y;
    reset_jump_assist_state();
    reset_dynamic_climb_state();
}

void PlayerController::set_selected_block(BlockId block_id) noexcept {
    selected_block_ = block_item_id(block_id);
}

void PlayerController::set_max_health(float max_health) noexcept {
    // Je garde la santé maximale hors du format historique du contrôleur :
    // elle reste entièrement dérivée du niveau et de la Robustesse du build.
    max_health_ = std::clamp(finite_or(max_health, 20.0F), 1.0F, 1000.0F);
    state_.health = std::clamp(state_.health, 0.0F, max_health_);
}

void PlayerController::set_damage_resistance_percent(float percent) noexcept {
    damage_resistance_percent_ = std::clamp(finite_or(percent, 0.0F), 0.0F, 99.0F);
}

void PlayerController::set_apnea_resistance_percent(float percent) noexcept {
    apnea_resistance_percent_ = std::clamp(finite_or(percent, 0.0F), 0.0F, 99.0F);
}

void PlayerController::set_fall_safety_multiplier(float multiplier) noexcept {
    fall_safety_multiplier_ = std::clamp(finite_or(multiplier, 1.0F), 0.25F, 3.0F);
}

void PlayerController::set_movement_speed_multiplier(float multiplier) noexcept {
    movement_speed_multiplier_ = std::clamp(finite_or(multiplier, 1.0F), 0.25F, 2.0F);
}

void PlayerController::set_water_movement_profile(
    PlayerWaterMovementProfile profile) noexcept {
    switch (profile) {
    case PlayerWaterMovementProfile::Poolrooms:
        water_movement_profile_ =
            PlayerWaterMovementProfile::Poolrooms;
        break;
    case PlayerWaterMovementProfile::Standard:
    default:
        water_movement_profile_ =
            PlayerWaterMovementProfile::Standard;
        break;
    }
}

void PlayerController::set_block_break_speed_multiplier(float multiplier) noexcept {
    block_break_speed_multiplier_ = std::clamp(finite_or(multiplier, 1.0F), 0.25F, 2.0F);
}

void PlayerController::trigger_primary_action() noexcept {
    if (state_.dead) {
        return;
    }

    state_.primary_action_active = true;
    state_.primary_action_progress = 0.0F;
}

void PlayerController::trigger_secondary_action() noexcept {
    if (state_.dead) {
        return;
    }

    state_.secondary_action_active = true;
    state_.secondary_action_progress = 0.0F;
}

void PlayerController::respawn(const glm::vec3& position) noexcept {
    state_ = {};
    block_break_progress_ = {};
    reset_jump_assist_state();
    reset_dynamic_climb_state();
    connector_camera_offset_y_ = 0.0F;
    state_.position = finite_vec3_or(position, state_.position);
    state_.health = max_health_;
    state_.fall_start_y = state_.position.y;
    state_.body_yaw_degrees = state_.yaw_degrees;
}

void PlayerController::apply_external_damage(float amount, PlayerDeathCause cause) noexcept {
    static_cast<void>(
        apply_external_damage_report(
            amount,
            cause));
}

auto PlayerController::apply_external_damage_report(
    float amount,
    PlayerDeathCause cause) noexcept -> PlayerDamageResult {
    return apply_damage(
        amount,
        cause,
        false);
}

void PlayerController::apply_environmental_damage(float amount, PlayerDeathCause cause) noexcept {
    // La resistance d'armure reste appliquee ; seule l'invulnerabilite commune
    // aux impacts est contournee, comme pour la chute, la noyade et le vide.
    static_cast<void>(
        apply_damage(
            amount,
            cause,
            true));
}

void PlayerController::force_death(PlayerDeathCause cause) noexcept {
    if (state_.dead) {
        return;
    }

    // Un echec de scenario n'est pas un coup classique : il doit rester fatal
    // meme pendant l'invulnerabilite temporaire ou avec 99 % de resistance.
    state_.hurt_timer = kHurtFlashDuration;
    state_.damage_cooldown = kInvulnerabilityDuration;
    state_.regen_delay = kRegenerationDelay;
    state_.regen_tick_timer = 0.0F;
    enter_death_state(cause);
}

auto PlayerController::current_target(const World& world, float max_distance) const -> RaycastHit {
    const auto clamped_distance = non_negative_finite(max_distance);
    if (clamped_distance <= 0.0F) {
        return {};
    }
    return world.raycast(eye_position(), look_direction(), clamped_distance);
}

auto PlayerController::update_block_breaking(World& world,
                                             float dt,
                                             bool breaking_held,
                                             float max_distance,
                                             float tool_speed_multiplier)
    -> std::optional<BrokenBlockResult> {
    if (state_.dead || !breaking_held) {
        cancel_block_breaking();
        return std::nullopt;
    }
    return update_block_breaking(
        world,
        dt,
        breaking_held,
        current_target(world, max_distance),
        tool_speed_multiplier);
}

auto PlayerController::update_block_breaking(World& world,
                                             float dt,
                                             bool breaking_held,
                                             const RaycastHit& hit,
                                             float tool_speed_multiplier)
    -> std::optional<BrokenBlockResult> {
    if (state_.dead || !breaking_held) {
        cancel_block_breaking();
        return std::nullopt;
    }

    if (!hit.hit || !is_block_breakable_at(hit.block, hit.block_id)) {
        cancel_block_breaking();
        return std::nullopt;
    }

    const auto tool_multiplier = std::clamp(finite_or(tool_speed_multiplier, 1.0F), 0.25F, 8.0F);
    const auto effective_break_speed = std::clamp(block_break_speed_multiplier_ * tool_multiplier, 0.25F, 8.0F);
    const auto duration_seconds = block_break_duration_seconds(hit.block_id) / effective_break_speed;
    if (!block_break_progress_.active ||
        block_break_progress_.block != hit.block ||
        block_break_progress_.block_id != hit.block_id) {
        block_break_progress_ = {
            true,
            hit.block,
            hit.block_id,
            0.0F,
            duration_seconds,
            0.0F,
            0,
        };
    }

    if (!state_.primary_action_active) {
        trigger_primary_action();
    }

    block_break_progress_.duration_seconds = duration_seconds;
    block_break_progress_.elapsed_seconds =
        std::min(block_break_progress_.elapsed_seconds + non_negative_finite(dt), duration_seconds);
    block_break_progress_.progress = duration_seconds <= 1.0e-4F
                                         ? 1.0F
                                         : block_break_progress_.elapsed_seconds / duration_seconds;
    block_break_progress_.crack_stage = block_break_crack_stage(block_break_progress_.progress);

    if (block_break_progress_.progress + 1.0e-4F < 1.0F) {
        return std::nullopt;
    }

    world.set_block(hit.block.x, hit.block.y, hit.block.z, to_block_id(BlockType::Air));
    const auto broken_block = BrokenBlockResult {hit.block, hit.block_id};
    cancel_block_breaking();
    return broken_block;
}

void PlayerController::cancel_block_breaking() noexcept {
    block_break_progress_ = {};
}

auto PlayerController::block_break_progress() const noexcept -> const BlockBreakProgress& {
    return block_break_progress_;
}

auto PlayerController::try_break_block(World& world, float max_distance) const -> std::optional<BrokenBlockResult> {
    const auto hit = current_target(world, max_distance);
    if (!hit.hit || !is_block_breakable_at(hit.block, hit.block_id)) {
        return std::nullopt;
    }

    world.set_block(hit.block.x, hit.block.y, hit.block.z, to_block_id(BlockType::Air));
    return BrokenBlockResult {hit.block, hit.block_id};
}

auto PlayerController::try_place_block(World& world, float max_distance) const -> std::optional<PlacedBlockResult> {
    const auto selected_block = block_item_id(selected_block_);
    if (!is_placeable_item(selected_block)) {
        return std::nullopt;
    }

    const auto hit = current_target(world, max_distance);
    if (!hit.hit) {
        return std::nullopt;
    }

    auto placement_coord = hit.adjacent;
    if (is_block_replaceable(hit.block_id)) {
        placement_coord = hit.block;
    }

    auto block_to_place = selected_block;
    if (selected_block == to_block_id(BlockType::Torch)) {
        auto torch_support_coord = hit.block;
        if (is_block_replaceable(hit.block_id)) {
            const BlockCoord support_offset {
                hit.block.x - hit.adjacent.x,
                hit.block.y - hit.adjacent.y,
                hit.block.z - hit.adjacent.z,
            };
            torch_support_coord = {
                placement_coord.x + support_offset.x,
                placement_coord.y + support_offset.y,
                placement_coord.z + support_offset.z,
            };
        }

        const auto resolved_torch_block = world.torch_block_to_place(placement_coord, torch_support_coord);
        if (!resolved_torch_block.has_value()) {
            return std::nullopt;
        }
        block_to_place = *resolved_torch_block;
    } else {
        if (!is_world_y_valid(placement_coord.y)) {
            return std::nullopt;
        }
        const auto current_block = world.get_block(placement_coord.x, placement_coord.y, placement_coord.z);
        if (current_block != to_block_id(BlockType::Air) && !is_block_replaceable(current_block)) {
            return std::nullopt;
        }
    }
    if (block_overlaps_player(placement_coord)) {
        return std::nullopt;
    }

    if (!world.set_player_block(
            placement_coord.x,
            placement_coord.y,
            placement_coord.z,
            block_to_place)) {
        return std::nullopt;
    }
    return PlacedBlockResult {placement_coord, block_to_place};
}

auto PlayerController::collides_at(const World& world, const glm::vec3& feet_position) const -> bool {
    constexpr float half_width = kPlayerWidth * 0.5F;
    const auto min_corner = glm::vec3 {feet_position.x - half_width, feet_position.y, feet_position.z - half_width};
    const auto max_corner = glm::vec3 {feet_position.x + half_width, feet_position.y + kPlayerHeight, feet_position.z + half_width};

    const auto min_x = static_cast<int>(std::floor(min_corner.x));
    const auto min_y = static_cast<int>(std::floor(min_corner.y));
    const auto min_z = static_cast<int>(std::floor(min_corner.z));
    const auto max_x = static_cast<int>(std::floor(max_corner.x - kCollisionEpsilon));
    const auto max_y = static_cast<int>(std::floor(max_corner.y - kCollisionEpsilon));
    const auto max_z = static_cast<int>(std::floor(max_corner.z - kCollisionEpsilon));

    for (int y = min_y; y <= max_y; ++y) {
        for (int z = min_z; z <= max_z; ++z) {
            for (int x = min_x; x <= max_x; ++x) {
                if (is_block_collidable(player_physics_block(world, x, y, z))) {
                    return true;
                }
            }
        }
    }

    return false;
}

auto PlayerController::overlaps_dynamic_obstacle(const ShipEntity& obstacle) const noexcept -> bool {
    constexpr float half_width = kPlayerWidth * 0.5F;
    const auto min_corner = glm::vec3 {
        state_.position.x - half_width,
        state_.position.y,
        state_.position.z - half_width,
    };
    const auto max_corner = glm::vec3 {
        state_.position.x + half_width,
        state_.position.y + kPlayerHeight,
        state_.position.z + half_width,
    };
    return obstacle.intersects_aabb(min_corner, max_corner);
}

auto PlayerController::dynamic_support_height_at(
    const ShipEntity& obstacle,
    const glm::vec3& feet_position,
    float min_height,
    float max_height) const noexcept
    -> std::optional<float> {

    const auto geometric_support =
        obstacle.support_height_in_range(
            feet_position,
            min_height,
            max_height);
    if (!geometric_support.has_value()) {
        return std::nullopt;
    }

    constexpr float half_width =
        kPlayerWidth *
        0.5F;
    const auto overlaps_at =
        [&](float feet_y) noexcept {
            return obstacle.intersects_aabb(
                {
                    feet_position.x -
                        half_width,
                    feet_y,
                    feet_position.z -
                        half_width,
                },
                {
                    feet_position.x +
                        half_width,
                    feet_y +
                        kPlayerHeight,
                    feet_position.z +
                        half_width,
                });
        };

    if (!overlaps_at(
            *geometric_support)) {
        return geometric_support;
    }

    auto colliding_height =
        *geometric_support;
    auto clear_height =
        std::optional<float> {};
    constexpr auto clearance_steps =
        static_cast<int>(
            kDynamicSupportMaximumClearance /
            kDynamicSupportClearanceStep);
    for (int step_index = 1;
         step_index <= clearance_steps;
         ++step_index) {
        const auto candidate_height =
            *geometric_support +
            static_cast<float>(step_index) *
                kDynamicSupportClearanceStep;
        if (!overlaps_at(
                candidate_height)) {
            clear_height =
                candidate_height;
            break;
        }
        colliding_height =
            candidate_height;
    }

    if (!clear_height.has_value()) {
        return std::nullopt;
    }

    // Je cherche le premier Y libre au dixieme de millimetre pres. Le joueur
    // reste vertical tandis que le dessus du navire s'incline : cette hauteur
    // de degagement est donc plus fiable qu'un simple echantillon au centre.
    for (int iteration = 0;
         iteration < 10;
         ++iteration) {
        const auto candidate_height =
            (colliding_height +
             *clear_height) *
            0.5F;
        if (overlaps_at(
                candidate_height)) {
            colliding_height =
                candidate_height;
        } else {
            clear_height =
                candidate_height;
        }
    }
    return clear_height;
}

auto PlayerController::dynamic_support_height(
    const ShipEntity& obstacle) const noexcept
    -> std::optional<float> {

    return dynamic_support_height_at(
        obstacle,
        state_.position,
        state_.position.y -
            kDynamicSupportMaximumClearance -
            kDynamicPlatformContactTolerance,
        state_.position.y +
            kDynamicPlatformContactTolerance);
}

auto PlayerController::resolve_dynamic_obstacle_overlap(
    const World& world,
    const ShipEntity& obstacle) -> bool {

    if (!overlaps_dynamic_obstacle(obstacle)) {
        return false;
    }

    const auto initial_position = state_.position;
    auto resolved_position = std::optional<glm::vec3> {};
    auto resolved_distance_squared =
        std::numeric_limits<float>::infinity();

    const auto is_clear = [&](const glm::vec3& candidate) {
        constexpr float half_width = kPlayerWidth * 0.5F;
        if (collides_at(world, candidate)) {
            return false;
        }
        return !obstacle.intersects_aabb(
            {
                candidate.x - half_width,
                candidate.y,
                candidate.z - half_width,
            },
            {
                candidate.x + half_width,
                candidate.y + kPlayerHeight,
                candidate.z + half_width,
            });
    };

    const auto consider_clear_candidate =
        [&](const glm::vec3& candidate) {
            const auto displacement =
                candidate -
                initial_position;
            const auto distance_squared =
                glm::dot(
                    displacement,
                    displacement);
            if (distance_squared >=
                    resolved_distance_squared ||
                !is_clear(candidate)) {
                return;
            }
            resolved_position =
                candidate;
            resolved_distance_squared =
                distance_squared;
        };

    // Je privilegie la face superieure qui vient de porter le joueur. Cette
    // correction exacte evite qu'un roulis transforme une rambarde inclinee en
    // coin dans l'AABB verticale du personnage.
    if (const auto support =
            dynamic_support_height_at(
                obstacle,
                initial_position,
                initial_position.y -
                    kDynamicPlatformStepHeight -
                    kDynamicSupportMaximumClearance,
                initial_position.y +
                    kDynamicOverlapRecoveryDistance);
        support.has_value()) {
        auto support_candidate =
            initial_position;
        support_candidate.y =
            *support +
            kCollisionEpsilon;
        consider_clear_candidate(
            support_candidate);
    }

    constexpr float diagonal =
        0.70710678118654752440F;
    constexpr float spatial_diagonal =
        0.57735026918962576451F;
    constexpr std::array<glm::vec3, 17>
        recovery_directions {{
            {0.0F, 1.0F, 0.0F},
            {1.0F, 0.0F, 0.0F},
            {-1.0F, 0.0F, 0.0F},
            {0.0F, 0.0F, 1.0F},
            {0.0F, 0.0F, -1.0F},
            {diagonal, diagonal, 0.0F},
            {-diagonal, diagonal, 0.0F},
            {0.0F, diagonal, diagonal},
            {0.0F, diagonal, -diagonal},
            {diagonal, 0.0F, diagonal},
            {-diagonal, 0.0F, diagonal},
            {diagonal, 0.0F, -diagonal},
            {-diagonal, 0.0F, -diagonal},
            {spatial_diagonal, spatial_diagonal, spatial_diagonal},
            {-spatial_diagonal, spatial_diagonal, spatial_diagonal},
            {spatial_diagonal, spatial_diagonal, -spatial_diagonal},
            {-spatial_diagonal, spatial_diagonal, -spatial_diagonal},
        }};
    constexpr auto recovery_steps =
        static_cast<int>(
            kDynamicOverlapRecoveryDistance /
            kDynamicOverlapRecoveryStep);

    // Je cherche la plus petite sortie libre dans un rayon borne. La montee
    // reste prioritaire a distance egale pour les obstacles bas ; les sorties
    // obliques liberent aussi les angles formes par une cloison et une poutre.
    for (int step_index = 1;
         step_index <= recovery_steps;
         ++step_index) {
        const auto distance =
            static_cast<float>(step_index) *
            kDynamicOverlapRecoveryStep;
        if (distance * distance >
            resolved_distance_squared) {
            break;
        }
        for (const auto& direction :
             recovery_directions) {
            consider_clear_candidate(
                initial_position +
                direction *
                    distance);
        }
    }

    if (!resolved_position.has_value()) {
        return false;
    }

    const auto correction =
        *resolved_position -
        initial_position;
    state_.position =
        *resolved_position;
    if (std::abs(correction.y) >
        1.0e-6F) {
        state_.fall_start_y +=
            correction.y;
    }

    const auto correction_length =
        glm::length(correction);
    if (correction_length > 1.0e-6F) {
        const auto outward_normal =
            correction /
            correction_length;
        const auto inward_speed =
            glm::dot(
                state_.velocity,
                outward_normal);
        if (inward_speed < 0.0F) {
            state_.velocity -=
                outward_normal *
                inward_speed;
        }
    }

    if (const auto support =
            dynamic_support_height(
                obstacle);
        support.has_value()) {
        auto supported_position =
            state_.position;
        supported_position.y =
            *support;
        if (is_clear(
                supported_position)) {
            resolve_dynamic_platform_support(
                *support);
        }
    }
    return true;
}

void PlayerController::update_body_yaw(float dt, const glm::vec2& horizontal_displacement) noexcept {
    if (state_.dead) {
        return;
    }

    const auto clamped_dt = non_negative_finite(dt);
    const auto horizontal_distance = glm::length(horizontal_displacement);
    const auto horizontal_speed = clamped_dt > 1.0e-5F ? horizontal_distance / clamped_dt : 0.0F;

    const auto moving = horizontal_speed > kBodyYawMoveThreshold;
    const auto target_yaw = moving ? yaw_degrees_from_direction(horizontal_displacement) : state_.yaw_degrees;
    const auto turn_speed = moving ? kBodyYawMoveTurnSpeed : kBodyYawIdleTurnSpeed;
    state_.body_yaw_degrees = rotate_towards_degrees(state_.body_yaw_degrees, target_yaw, turn_speed * clamped_dt);
}

void PlayerController::update_survival_state(float dt, const WaterContactState& water_contact) {
    if (state_.dead) {
        return;
    }

    dt = non_negative_finite(dt);
    state_.swimming = !state_.fly_mode && water_contact.swimming;
    state_.head_underwater = !state_.fly_mode && water_contact.head_in_water;

    if (state_.head_underwater) {
        const auto air_loss_multiplier = 1.0F - apnea_resistance_percent_ / 100.0F;
        state_.air_seconds = std::max(0.0F, state_.air_seconds - dt * air_loss_multiplier);
        if (state_.air_seconds <= 0.0F) {
            state_.drowning_tick_timer += dt;
            while (state_.drowning_tick_timer >= kDrowningDamageInterval && !state_.dead) {
                apply_damage(2.0F, PlayerDeathCause::Drowning, true);
                state_.drowning_tick_timer -= kDrowningDamageInterval;
            }
        }
    } else {
        state_.air_seconds = std::min(kMaxAirSeconds, state_.air_seconds + dt * 2.2F);
        state_.drowning_tick_timer = 0.0F;
    }

    if (state_.position.y < kVoidDamageThreshold && state_.damage_cooldown <= 0.0F) {
        apply_damage(4.0F, PlayerDeathCause::Void, true);
    }

    if (state_.dead) {
        return;
    }

    if (state_.health < max_health_ && state_.regen_delay <= 0.0F && !state_.head_underwater) {
        state_.regen_tick_timer += dt;
        while (state_.regen_tick_timer >= kRegenerationInterval && state_.health < max_health_) {
            heal(1.0F);
            state_.regen_tick_timer -= kRegenerationInterval;
        }
    } else if (state_.health >= max_health_ || state_.head_underwater) {
        state_.regen_tick_timer = 0.0F;
    }
}

auto PlayerController::try_step_onto_backrooms_connector(
    const glm::vec3& horizontal_candidate,
    const World& world) -> bool {
    // Je conserve la memoire d'appui du tick precedent : une rampe analytique
    // n'est volontairement pas une AABB collidable, mais elle doit autoriser
    // la derniere marche pleine exactement comme un plancher voxel.
    if (world.generation_profile() !=
            WorldGenerationProfile::Backrooms ||
        state_.fly_mode ||
        state_.velocity.y > 0.0F ||
        (!state_.on_ground &&
         ground_coyote_timer_ <= 0.0F &&
         !collides_at(
             world,
             state_.position +
                 glm::vec3 {0.0F, -0.08F, 0.0F}))) {
        return false;
    }

    constexpr auto half_width = kPlayerWidth * 0.5F;
    const auto minimum_x =
        static_cast<int>(
            std::floor(
                horizontal_candidate.x -
                half_width));
    const auto maximum_x =
        static_cast<int>(
            std::floor(
                horizontal_candidate.x +
                half_width -
                kCollisionEpsilon));
    const auto minimum_z =
        static_cast<int>(
            std::floor(
                horizontal_candidate.z -
                half_width));
    const auto maximum_z =
        static_cast<int>(
            std::floor(
                horizontal_candidate.z +
                half_width -
                kCollisionEpsilon));
    const auto minimum_y =
        static_cast<int>(
            std::floor(state_.position.y));
    const auto maximum_y =
        static_cast<int>(
            std::floor(
                state_.position.y +
                kBackroomsConnectorMaximumStepHeight));

    auto support_height =
        -std::numeric_limits<float>::infinity();
    for (int y = minimum_y; y <= maximum_y; ++y) {
        for (int z = minimum_z; z <= maximum_z; ++z) {
            for (int x = minimum_x; x <= maximum_x; ++x) {
                if (!is_backrooms_connector_step(
                        player_physics_block(
                            world,
                            x,
                            y,
                            z))) {
                    continue;
                }
                const auto top =
                    static_cast<float>(y + 1) +
                    kCollisionEpsilon;
                const auto rise =
                    top - state_.position.y;
                if (rise > kCollisionEpsilon &&
                    rise <=
                        kBackroomsConnectorMaximumStepHeight +
                            kCollisionEpsilon) {
                    support_height =
                        std::max(support_height, top);
                }
            }
        }
    }
    if (!std::isfinite(support_height)) {
        return false;
    }

    auto stepped_position = horizontal_candidate;
    stepped_position.y = support_height;
    if (collides_at(world, stepped_position)) {
        return false;
    }

    state_.position = stepped_position;
    state_.velocity.y = 0.0F;
    state_.on_ground = true;
    state_.fall_start_y = state_.position.y;
    state_.airborne_time = 0.0F;
    return true;
}

auto PlayerController::resolve_backrooms_connector_support(
    const World& world,
    bool was_grounded) -> bool {
    if (world.generation_profile() !=
            WorldGenerationProfile::Backrooms ||
        state_.fly_mode ||
        state_.velocity.y > 0.0F) {
        return false;
    }

    auto best_height =
        -std::numeric_limits<float>::infinity();
    constexpr auto half_width = kPlayerWidth * 0.5F;
    const auto minimum_x =
        static_cast<int>(
            std::floor(state_.position.x - half_width));
    const auto maximum_x =
        static_cast<int>(
            std::floor(
                state_.position.x + half_width -
                kCollisionEpsilon));
    const auto minimum_z =
        static_cast<int>(
            std::floor(state_.position.z - half_width));
    const auto maximum_z =
        static_cast<int>(
            std::floor(
                state_.position.z + half_width -
                kCollisionEpsilon));
    const auto minimum_y =
        std::max(
            kWorldMinY,
            static_cast<int>(
                std::floor(
                    state_.position.y -
                    kBackroomsRampSupportProbeDepth)));
    const auto maximum_y =
        std::min(
            kWorldMaxY,
            static_cast<int>(
                std::floor(
                    state_.position.y + 1.0F)));

    for (int y = minimum_y; y <= maximum_y; ++y) {
        for (int z = minimum_z; z <= maximum_z; ++z) {
            for (int x = minimum_x; x <= maximum_x; ++x) {
                const auto block =
                    player_physics_block(
                        world,
                        x,
                        y,
                        z);
                if (!is_backrooms_connector_step(block)) {
                    continue;
                }
                const auto height =
                    static_cast<float>(y + 1) +
                    kCollisionEpsilon;
                if (std::abs(
                        height - state_.position.y) <=
                    kBackroomsConnectorSupportTolerance) {
                    best_height =
                        std::max(best_height, height);
                }
            }
        }
    }

    const auto ramp_x =
        static_cast<int>(std::floor(state_.position.x));
    const auto ramp_z =
        static_cast<int>(std::floor(state_.position.z));
    for (int y = minimum_y; y <= maximum_y; ++y) {
        const auto block =
            player_physics_block(
                world,
                ramp_x,
                y,
                ramp_z);
        if (!is_backrooms_ramp(block)) {
            continue;
        }
        const auto local_x =
            state_.position.x -
            static_cast<float>(ramp_x);
        const auto local_z =
            state_.position.z -
            static_cast<float>(ramp_z);
        const auto height =
            static_cast<float>(y) +
            backrooms_ramp_surface_height(
                block,
                local_x,
                local_z) +
            kCollisionEpsilon;
        if (std::abs(
                height - state_.position.y) <=
            kBackroomsConnectorSupportTolerance) {
            best_height =
                std::max(best_height, height);
        }
    }

    if (!std::isfinite(best_height)) {
        return false;
    }
    const auto climbs_to_support =
        best_height >
        state_.position.y + kCollisionEpsilon;
    if (climbs_to_support && !was_grounded &&
        !state_.on_ground) {
        return false;
    }

    auto supported_position = state_.position;
    supported_position.y = best_height;
    if (collides_at(world, supported_position)) {
        return false;
    }
    state_.position = supported_position;
    state_.velocity.y = 0.0F;
    return true;
}

void PlayerController::move_axis(float delta, int axis, const World& world, const ShipEntity* dynamic_obstacle) {
    if (std::abs(delta) <= 1.0e-6F) {
        return;
    }

    constexpr float half_width = kPlayerWidth * 0.5F;
    auto next_position = state_.position;
    next_position[axis] += delta;

    const auto min_corner = glm::vec3 {next_position.x - half_width, next_position.y, next_position.z - half_width};
    const auto max_corner = glm::vec3 {next_position.x + half_width, next_position.y + kPlayerHeight, next_position.z + half_width};

    if (axis == 0) {
        const auto block_x = delta > 0.0F
                                 ? static_cast<int>(std::floor(max_corner.x - kCollisionEpsilon))
                                 : static_cast<int>(std::floor(min_corner.x + kCollisionEpsilon));
        const auto min_y = static_cast<int>(std::floor(min_corner.y));
        const auto max_y = static_cast<int>(std::floor(max_corner.y - kCollisionEpsilon));
        const auto min_z = static_cast<int>(std::floor(min_corner.z));
        const auto max_z = static_cast<int>(std::floor(max_corner.z - kCollisionEpsilon));

        for (int y = min_y; y <= max_y; ++y) {
            for (int z = min_z; z <= max_z; ++z) {
                const auto physics_block =
                    player_physics_block(
                        world,
                        block_x,
                        y,
                        z);
                if (!is_block_collidable(physics_block)) {
                    continue;
                }
                if (is_backrooms_connector_step(physics_block) &&
                    try_step_onto_backrooms_connector(
                        next_position,
                        world)) {
                    return;
                }
                next_position.x = delta > 0.0F
                                      ? static_cast<float>(block_x) - half_width - kCollisionEpsilon
                                      : static_cast<float>(block_x + 1) + half_width + kCollisionEpsilon;
                state_.velocity.x = 0.0F;
                state_.position = next_position;
                return;
            }
        }
    } else if (axis == 1) {
        if (delta < 0.0F &&
            world.generation_profile() ==
                WorldGenerationProfile::Backrooms) {
            const auto ramp_x =
                static_cast<int>(std::floor(next_position.x));
            const auto ramp_z =
                static_cast<int>(std::floor(next_position.z));
            const auto minimum_ramp_y =
                std::max(
                    kWorldMinY,
                    static_cast<int>(
                        std::floor(next_position.y)) -
                        1);
            const auto maximum_ramp_y =
                std::min(
                    kWorldMaxY,
                    static_cast<int>(
                        std::floor(state_.position.y)));
            auto crossed_surface =
                -std::numeric_limits<float>::infinity();
            for (int ramp_y = minimum_ramp_y;
                 ramp_y <= maximum_ramp_y;
                 ++ramp_y) {
                const auto ramp_block =
                    player_physics_block(
                        world,
                        ramp_x,
                        ramp_y,
                        ramp_z);
                if (!is_backrooms_ramp(ramp_block)) {
                    continue;
                }
                const auto surface_height =
                    static_cast<float>(ramp_y) +
                    backrooms_ramp_surface_height(
                        ramp_block,
                        next_position.x -
                            static_cast<float>(ramp_x),
                        next_position.z -
                            static_cast<float>(ramp_z));
                if (state_.position.y +
                            kCollisionEpsilon >=
                        surface_height &&
                    next_position.y <=
                        surface_height +
                            kCollisionEpsilon) {
                    crossed_surface =
                        std::max(
                            crossed_surface,
                            surface_height);
                }
            }
            if (std::isfinite(crossed_surface)) {
                auto ramp_supported_position =
                    next_position;
                ramp_supported_position.y =
                    crossed_surface +
                    kCollisionEpsilon;
                if (!collides_at(
                        world,
                        ramp_supported_position)) {
                    // Je resous le croisement pendant le balayage vertical :
                    // meme une chute rapide ne peut traverser la pente entre
                    // deux resolutions de support en fin de tick.
                    state_.position =
                        ramp_supported_position;
                    state_.velocity.y = 0.0F;
                    state_.on_ground = true;
                    return;
                }
            }
        }

        const auto block_y = delta > 0.0F
                                 ? static_cast<int>(std::floor(max_corner.y - kCollisionEpsilon))
                                 : static_cast<int>(std::floor(min_corner.y + kCollisionEpsilon));
        const auto min_x = static_cast<int>(std::floor(min_corner.x));
        const auto max_x = static_cast<int>(std::floor(max_corner.x - kCollisionEpsilon));
        const auto min_z = static_cast<int>(std::floor(min_corner.z));
        const auto max_z = static_cast<int>(std::floor(max_corner.z - kCollisionEpsilon));

        for (int z = min_z; z <= max_z; ++z) {
            for (int x = min_x; x <= max_x; ++x) {
                if (!is_block_collidable(player_physics_block(world, x, block_y, z))) {
                    continue;
                }
                if (delta > 0.0F) {
                    next_position.y = static_cast<float>(block_y) - kPlayerHeight - kCollisionEpsilon;
                } else {
                    next_position.y = static_cast<float>(block_y + 1) + kCollisionEpsilon;
                    state_.on_ground = true;
                }
                state_.velocity.y = 0.0F;
                state_.position = next_position;
                return;
            }
        }
    } else {
        const auto block_z = delta > 0.0F
                                 ? static_cast<int>(std::floor(max_corner.z - kCollisionEpsilon))
                                 : static_cast<int>(std::floor(min_corner.z + kCollisionEpsilon));
        const auto min_x = static_cast<int>(std::floor(min_corner.x));
        const auto max_x = static_cast<int>(std::floor(max_corner.x - kCollisionEpsilon));
        const auto min_y = static_cast<int>(std::floor(min_corner.y));
        const auto max_y = static_cast<int>(std::floor(max_corner.y - kCollisionEpsilon));

        for (int y = min_y; y <= max_y; ++y) {
            for (int x = min_x; x <= max_x; ++x) {
                const auto physics_block =
                    player_physics_block(
                        world,
                        x,
                        y,
                        block_z);
                if (!is_block_collidable(physics_block)) {
                    continue;
                }
                if (is_backrooms_connector_step(physics_block) &&
                    try_step_onto_backrooms_connector(
                        next_position,
                        world)) {
                    return;
                }
                next_position.z = delta > 0.0F
                                      ? static_cast<float>(block_z) - half_width - kCollisionEpsilon
                                      : static_cast<float>(block_z + 1) + half_width + kCollisionEpsilon;
                state_.velocity.z = 0.0F;
                state_.position = next_position;
                return;
            }
        }
    }

    if (dynamic_obstacle != nullptr) {
        const auto intersects_dynamic_obstacle = [&](const glm::vec3& feet_position) noexcept {
            const auto obstacle_min = glm::vec3 {
                feet_position.x - half_width,
                feet_position.y,
                feet_position.z - half_width,
            };
            const auto obstacle_max = glm::vec3 {
                feet_position.x + half_width,
                feet_position.y + kPlayerHeight,
                feet_position.z + half_width,
            };
            return dynamic_obstacle->intersects_aabb(obstacle_min, obstacle_max);
        };

        // Je resous d'abord l'atterrissage sur le pont pour conserver une hauteur
        // exacte, puis je borne la recherche de contact lateral a huit iterations.
        // Le volume complet doit tenir au-dessus de l'appui : un meuble sous un
        // plafond bas ne peut jamais devenir une position de joueur valide.
        if (axis == 1 && delta < 0.0F) {
            const auto support_height =
                dynamic_support_height_at(
                    *dynamic_obstacle,
                    next_position,
                    next_position.y -
                        kDynamicPlatformContactTolerance,
                    state_.position.y +
                        kDynamicPlatformContactTolerance);
            if (support_height.has_value() && state_.position.y >= *support_height - kCollisionEpsilon &&
                next_position.y <= *support_height + kCollisionEpsilon) {
                auto supported_position = next_position;
                supported_position.y = *support_height + kCollisionEpsilon;
                if (!collides_at(world, supported_position) &&
                    !intersects_dynamic_obstacle(supported_position)) {
                    state_.velocity.y = 0.0F;
                    state_.on_ground = true;
                    state_.position = supported_position;
                    return;
                }
            }
        }

        const auto current_support =
            dynamic_support_height_at(
                *dynamic_obstacle,
                state_.position,
                state_.position.y -
                    kDynamicSupportMaximumClearance -
                    kDynamicPlatformContactTolerance,
                state_.position.y +
                    kDynamicPlatformContactTolerance);
        const auto can_follow_dynamic_steps =
            axis != 1 && !state_.fly_mode && state_.velocity.y <= 0.0F &&
            (state_.on_ground ||
             (current_support.has_value() &&
              std::abs(
                  state_.position.y -
                  *current_support) <=
                  kDynamicPlatformContactTolerance));

        if (intersects_dynamic_obstacle(next_position)) {
            if (can_follow_dynamic_steps) {
                const auto step_support = dynamic_obstacle->step_support_height_in_range(
                    next_position,
                    state_.position.y - kCollisionEpsilon,
                    state_.position.y + kDynamicPlatformStepHeight);
                if (step_support.has_value()) {
                    auto stepped_position = next_position;
                    stepped_position.y = *step_support + kCollisionEpsilon;
                    const auto step_height = stepped_position.y - state_.position.y;
                    if (step_height >= -kCollisionEpsilon &&
                        step_height <= kDynamicPlatformStepHeight + kCollisionEpsilon &&
                        !collides_at(world, stepped_position) &&
                        !intersects_dynamic_obstacle(stepped_position)) {
                        // Je franchis les demi-marches du navire sans transformer
                        // le joueur en projectile ni autoriser l'escalade de la coque.
                        state_.position = stepped_position;
                        state_.velocity.y = 0.0F;
                        state_.on_ground = true;
                        return;
                    }
                }
            }

            auto safe_fraction = 0.0F;
            auto colliding_fraction = 1.0F;
            for (int iteration = 0; iteration < 8; ++iteration) {
                const auto candidate_fraction = (safe_fraction + colliding_fraction) * 0.5F;
                auto candidate_position = state_.position;
                candidate_position[axis] += delta * candidate_fraction;
                if (intersects_dynamic_obstacle(candidate_position)) {
                    colliding_fraction = candidate_fraction;
                } else {
                    safe_fraction = candidate_fraction;
                }
            }
            next_position = state_.position;
            next_position[axis] += delta * safe_fraction;
            state_.velocity[axis] = 0.0F;
            if (axis == 1 && delta < 0.0F) {
                state_.on_ground = true;
            }
        } else if (can_follow_dynamic_steps) {
            const auto lower_support =
                dynamic_support_height_at(
                    *dynamic_obstacle,
                    next_position,
                    state_.position.y -
                        kDynamicPlatformStepHeight -
                        kDynamicSupportMaximumClearance,
                    state_.position.y +
                        kDynamicPlatformContactTolerance);
            if (lower_support.has_value()) {
                auto snapped_position = next_position;
                snapped_position.y = *lower_support + kCollisionEpsilon;
                const auto drop_height = state_.position.y - snapped_position.y;
                if (drop_height >= -kDynamicPlatformContactTolerance &&
                    drop_height <= kDynamicPlatformStepHeight + kCollisionEpsilon &&
                    !collides_at(world, snapped_position) &&
                    !intersects_dynamic_obstacle(snapped_position)) {
                    // Je garde les pieds au contact des dalles descendantes pour
                    // eviter une micro-chute a chaque marche de l'escalier.
                    next_position = snapped_position;
                    state_.velocity.y = 0.0F;
                    state_.on_ground = true;
                }
            }
        }
    }

    state_.position = next_position;
}

auto PlayerController::block_overlaps_player(const BlockCoord& block_coord) const noexcept -> bool {
    constexpr float half_width = kPlayerWidth * 0.5F;
    const auto player_min = glm::vec3 {state_.position.x - half_width, state_.position.y, state_.position.z - half_width};
    const auto player_max = glm::vec3 {state_.position.x + half_width, state_.position.y + kPlayerHeight, state_.position.z + half_width};
    const auto block_min = glm::vec3 {static_cast<float>(block_coord.x), static_cast<float>(block_coord.y), static_cast<float>(block_coord.z)};
    const auto block_max = block_min + glm::vec3 {1.0F};

    return player_min.x < block_max.x && player_max.x > block_min.x &&
           player_min.y < block_max.y && player_max.y > block_min.y &&
           player_min.z < block_max.z && player_max.z > block_min.z;
}

auto PlayerController::apply_damage(
    float amount,
    PlayerDeathCause cause,
    bool bypass_cooldown) noexcept -> PlayerDamageResult {

    PlayerDamageResult result {};
    result.requested_damage =
        std::isfinite(amount) &&
                amount > 0.0F
            ? amount
            : 0.0F;

    if (!std::isfinite(amount) || amount <= 0.0F || state_.dead) {
        return result;
    }

    if (!bypass_cooldown && state_.damage_cooldown > 0.0F) {
        result.blocked_by_invulnerability = true;
        return result;
    }

    const auto mitigated_amount =
        amount * (1.0F - damage_resistance_percent_ / 100.0F);
    result.damage_after_resistance =
        std::max(
            mitigated_amount,
            0.0F);

    if (mitigated_amount <= 0.0F) {
        return result;
    }

    const auto health_before =
        state_.health;
    state_.health = std::max(0.0F, state_.health - mitigated_amount);
    result.health_damage =
        std::max(
            health_before -
                state_.health,
            0.0F);
    state_.hurt_timer = kHurtFlashDuration;
    state_.damage_cooldown = kInvulnerabilityDuration;
    state_.regen_delay = kRegenerationDelay;
    state_.regen_tick_timer = 0.0F;

    if (state_.health <= 0.0F) {
        enter_death_state(cause);
    }
    result.killed =
        state_.dead;
    return result;
}

void PlayerController::enter_death_state(PlayerDeathCause cause) noexcept {
    // Je centralise tous les invariants de mort afin que les degats ordinaires
    // et les echecs de scenario ne divergent jamais au fil des evolutions.
    state_.health = 0.0F;
    state_.dead = true;
    state_.death_cause = cause;
    state_.velocity = {};
    block_break_progress_ = {};
    state_.head_underwater = false;
    state_.swimming = false;
    state_.primary_action_active = false;
    state_.secondary_action_active = false;
    state_.primary_action_progress = 0.0F;
    state_.secondary_action_progress = 0.0F;
    state_.landing_impact = 0.0F;
    reset_jump_assist_state();
    reset_dynamic_climb_state();
}

void PlayerController::heal(float amount) noexcept {
    if (!std::isfinite(amount) || amount <= 0.0F || state_.dead) {
        return;
    }
    state_.health = std::min(max_health_, state_.health + amount);
}

void PlayerController::reset_jump_assist_state() noexcept {
    ground_coyote_timer_ = 0.0F;
    jump_buffer_timer_ = 0.0F;
}

void PlayerController::reset_dynamic_climb_state() noexcept {
    climbed_dynamic_obstacle_ = nullptr;
    dynamic_climb_regrab_locked_ = false;
    dynamic_climb_jump_locked_ = false;
}

auto PlayerController::is_liquid_at(
    const World& world,
    const glm::vec3& point) const noexcept
    -> bool {

    const auto block_x = static_cast<int>(std::floor(point.x));
    const auto block_y = static_cast<int>(std::floor(point.y));
    const auto block_z = static_cast<int>(std::floor(point.z));
    if (!is_world_y_valid(block_y)) {
        return false;
    }

    const auto level =
        world.peek_water_level_or_generated(
            block_x,
            block_y,
            block_z);
    if (level == 0) {
        return false;
    }

    const auto top_height =
        world.peek_water_level_or_generated(
            block_x,
            block_y + 1,
            block_z) > 0
            ? 1.0F
            : static_cast<float>(level) /
                  static_cast<float>(kMaxWaterLevel);
    return point.y <
           static_cast<float>(block_y) +
               top_height;
}

auto PlayerController::sample_water_contact(
    const World& world,
    const glm::vec3& feet_position,
    const ShipEntity* dynamic_obstacle,
    const OceanState* dynamic_ocean) const noexcept
    -> WaterContactState {

    WaterContactState water_contact {};
    if (state_.fly_mode) {
        return water_contact;
    }

    constexpr float sample_radius = kPlayerWidth * 0.35F;
    constexpr std::size_t kWaterContactSampleCount = 5U;
    const std::array<glm::vec2, kWaterContactSampleCount> horizontal_offsets {{
        {0.0F, 0.0F},
        {-sample_radius, -sample_radius},
        {-sample_radius, sample_radius},
        {sample_radius, -sample_radius},
        {sample_radius, sample_radius},
    }};

    struct DynamicOceanContact {
        bool valid = false;
        float surface_height = 0.0F;
    };
    std::array<
        DynamicOceanContact,
        kWaterContactSampleCount>
        dynamic_contacts {};
    constexpr float kOceanSurfaceAtRest =
        static_cast<float>(kSeaLevel + 1);

    if (dynamic_ocean != nullptr) {
        // Je calcule chaque hauteur horizontale une seule fois, puis je la
        // réutilise pour les pieds, le torse et la tête.
        for (std::size_t index = 0;
             index < horizontal_offsets.size();
             ++index) {
            const auto& offset =
                horizontal_offsets[index];
            const glm::vec2 point_xz {
                feet_position.x + offset.x,
                feet_position.z + offset.y,
            };
            const auto block_x =
                static_cast<int>(
                    std::floor(point_xz.x));
            const auto block_z =
                static_cast<int>(
                    std::floor(point_xz.y));

            if (world.peek_water_level_or_generated(
                    block_x,
                    kSeaLevel,
                    block_z) == 0) {
                continue;
            }

            // Je suis les trois longues composantes physiques, comme la
            // flottabilite du navire et le rendu en qualite basse. Les petites
            // rides restent un detail visuel et ne font pas varier la nage
            // lorsque la qualite adaptative change.
            const auto surface_height =
                kOceanSurfaceAtRest +
                OceanSimulation::sample(
                    *dynamic_ocean,
                    point_xz,
                    kOceanBuoyancyWaveCount)
                    .height;
            if (std::isfinite(surface_height)) {
                dynamic_contacts[index] = {
                    true,
                    surface_height,
                };
            }
        }
    }

    const auto any_sample_at_height_is_liquid = [&](float height) {
        std::array<BlockCoord, kWaterContactSampleCount> sampled_blocks {};
        std::size_t sampled_block_count = 0;
        for (std::size_t index = 0;
             index < horizontal_offsets.size();
             ++index) {
            const auto& offset =
                horizontal_offsets[index];
            const auto point = feet_position + glm::vec3 {offset.x, height, offset.y};
            if (dynamic_obstacle != nullptr &&
                dynamic_obstacle->excludes_ocean_at(
                    point)) {
                // Je supprime chaque contact liquide dans le volume etanche,
                // aux pieds comme au torse et a la tete, sans assécher la mer
                // immédiatement voisine de la coque.
                continue;
            }
            const BlockCoord block {
                static_cast<int>(std::floor(point.x)),
                static_cast<int>(std::floor(point.y)),
                static_cast<int>(std::floor(point.z)),
            };
            const auto duplicate = std::find(
                sampled_blocks.begin(),
                sampled_blocks.begin() + static_cast<std::ptrdiff_t>(sampled_block_count),
                block);
            // Je mutualise les cellules de l'eau statique. Pour une vague, je
            // garde les cinq positions car sa hauteur varie à l'intérieur du
            // même voxel et un bord du joueur peut être immergé avant le centre.
            if (dynamic_ocean == nullptr &&
                duplicate !=
                    sampled_blocks.begin() +
                        static_cast<std::ptrdiff_t>(
                            sampled_block_count)) {
                continue;
            }
            sampled_blocks[sampled_block_count++] = block;

            const auto& dynamic_contact =
                dynamic_contacts[index];
            if (dynamic_contact.valid) {
                // Je retire l'eau voxel au-dessus d'un creux et je prolonge
                // son volume sous une crête. Sous le niveau au repos, la
                // colonne voxel protège toujours les grottes et le terrain.
                if (point.y >=
                    dynamic_contact.surface_height) {
                    continue;
                }
                if (point.y >=
                    kOceanSurfaceAtRest) {
                    return true;
                }
            }

            if (is_liquid_at(world, point)) {
                return true;
            }
        }
        return false;
    };

    water_contact.feet_in_water = any_sample_at_height_is_liquid(kWaterFeetSampleHeight);
    water_contact.body_in_water = any_sample_at_height_is_liquid(kWaterBodySampleHeight);
    water_contact.head_in_water = any_sample_at_height_is_liquid(kEyeHeight);
    water_contact.swimming = water_contact.body_in_water || water_contact.head_in_water;
    return water_contact;
}

auto PlayerController::point_block(const World& world, const glm::vec3& point) const noexcept -> BlockId {
    const auto block_y = static_cast<int>(std::floor(point.y));
    if (!is_world_y_valid(block_y)) {
        return to_block_id(BlockType::Air);
    }
    return player_physics_block(
        world,
        static_cast<int>(std::floor(point.x)),
        block_y,
        static_cast<int>(std::floor(point.z)));
}

} // namespace valcraft
