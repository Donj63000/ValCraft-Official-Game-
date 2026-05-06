#include "gameplay/PlayerController.h"

#include "world/World.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/trigonometric.hpp>

#include <algorithm>
#include <array>
#include <cmath>

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
constexpr float kWadeGravity = 18.0F;
constexpr float kWadeJumpVelocity = 6.2F;
constexpr float kSwimMoveSpeed = 3.8F;
constexpr float kSwimGravity = 5.5F;
constexpr float kSwimBuoyancy = 7.0F;
constexpr float kSwimVerticalAcceleration = 20.0F;
constexpr float kSwimVerticalDamping = 6.0F;
constexpr float kSwimSinkSpeed = 3.8F;
constexpr float kSwimRiseSpeed = 4.6F;
constexpr float kSwimSurfaceJumpVelocity = 5.2F;
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

auto wrap_degrees(float angle) noexcept -> float {
    while (angle <= -180.0F) {
        angle += 360.0F;
    }
    while (angle > 180.0F) {
        angle -= 360.0F;
    }
    return angle;
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
    state_.position = spawn_position;
    state_.fall_start_y = spawn_position.y;
    state_.body_yaw_degrees = state_.yaw_degrees;
}

void PlayerController::update(const PlayerInput& input, float dt, const World& world) {
    const auto clamped_dt = std::max(dt, 0.0F);

    if (state_.dead) {
        state_.velocity = {};
        reset_jump_assist_state();
        state_.hurt_timer = std::max(0.0F, state_.hurt_timer - clamped_dt);
        state_.landing_impact = std::max(0.0F, state_.landing_impact - clamped_dt / kLandingAnimationDuration);
        state_.look_sway_yaw = damp_towards(state_.look_sway_yaw, 0.0F, kLookSwayReturnSharpness, clamped_dt);
        state_.look_sway_pitch = damp_towards(state_.look_sway_pitch, 0.0F, kLookSwayReturnSharpness, clamped_dt);
        state_.look_sway_yaw = snap_small_sway(state_.look_sway_yaw);
        state_.look_sway_pitch = snap_small_sway(state_.look_sway_pitch);
        return;
    }

    const auto was_on_ground = state_.on_ground;
    const auto water_contact_before_move = sample_water_contact(world, state_.position);
    state_.animation_time += clamped_dt;
    state_.hurt_timer = std::max(0.0F, state_.hurt_timer - clamped_dt);
    state_.damage_cooldown = std::max(0.0F, state_.damage_cooldown - clamped_dt);
    state_.regen_delay = std::max(0.0F, state_.regen_delay - clamped_dt);
    state_.landing_impact = std::max(0.0F, state_.landing_impact - clamped_dt / kLandingAnimationDuration);
    jump_buffer_timer_ = input.jump ? kJumpBufferSeconds : std::max(0.0F, jump_buffer_timer_ - clamped_dt);
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
        if (state_.fly_mode) {
            state_.velocity = {};
        }
    }

    state_.yaw_degrees += input.look_delta_x * kMouseSensitivity;
    state_.pitch_degrees = std::clamp(state_.pitch_degrees - input.look_delta_y * kMouseSensitivity, -89.0F, 89.0F);

    const auto target_look_sway_yaw = glm::clamp(-input.look_delta_x * kLookSwayInputScale, -1.0F, 1.0F);
    const auto target_look_sway_pitch = glm::clamp(input.look_delta_y * kLookSwayInputScale, -1.0F, 1.0F);
    const auto look_input_active = std::abs(input.look_delta_x) > 1.0e-4F || std::abs(input.look_delta_y) > 1.0e-4F;
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
    auto wish = forward * input.move_forward + right * input.move_right;
    if (glm::dot(wish, wish) > 1.0e-5F) {
        wish = glm::normalize(wish);
    }

    const auto standing_on_solid = collides_at(world, state_.position + glm::vec3 {0.0F, -0.05F, 0.0F});
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
    const auto sprinting = input.sprint && input.move_forward > 0.0F && glm::dot(wish, wish) > 1.0e-5F;

    if (state_.fly_mode) {
        reset_jump_assist_state();
        auto fly_velocity = wish + glm::vec3 {0.0F, input.move_up, 0.0F};
        if (glm::dot(fly_velocity, fly_velocity) > 1.0e-5F) {
            fly_velocity = glm::normalize(fly_velocity) * kFlySpeed;
        }
        state_.velocity = fly_velocity;
    } else if (water_contact_before_move.swimming) {
        state_.velocity.x = wish.x * kSwimMoveSpeed;
        state_.velocity.z = wish.z * kSwimMoveSpeed;
        state_.velocity.y += (std::clamp(input.move_up, -1.0F, 1.0F) * kSwimVerticalAcceleration - kSwimGravity) * clamped_dt;
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
        const auto move_speed = sprinting ? kWadeSprintMoveSpeed : kWadeMoveSpeed;
        state_.velocity.x = wish.x * move_speed;
        state_.velocity.z = wish.z * move_speed;
        state_.velocity.y -= kWadeGravity * clamped_dt;
        if (can_ground_jump()) {
            state_.velocity.y = kWadeJumpVelocity;
            consume_jump_assist();
        }
    } else {
        const auto move_speed = sprinting ? kSprintMoveSpeed : kMoveSpeed;
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
            move_axis(step, axis, world);

            const auto moved = state_.position[axis] - before;
            if (std::abs(moved) + 1.0e-5F < std::abs(step)) {
                break;
            }

            remaining -= step;
        }
    };

    const auto start_position = state_.position;
    move_axis_safely(state_.velocity.x * clamped_dt, 0);
    move_axis_safely(state_.velocity.y * clamped_dt, 1);
    move_axis_safely(state_.velocity.z * clamped_dt, 2);
    const auto horizontal_displacement = glm::vec2 {
        state_.position.x - start_position.x,
        state_.position.z - start_position.z,
    };
    const auto horizontal_distance = glm::length(horizontal_displacement);
    const auto water_contact_after_move = sample_water_contact(world, state_.position);
    auto landed_in_water = false;

    if (!state_.fly_mode) {
        state_.on_ground = collides_at(world, state_.position + glm::vec3 {0.0F, -0.05F, 0.0F});
        if (state_.on_ground) {
            ground_coyote_timer_ = kJumpCoyoteSeconds;
        }
        if (state_.on_ground && !was_on_ground) {
            landed_in_water =
                water_contact_after_move.feet_in_water || water_contact_after_move.body_in_water || water_contact_after_move.head_in_water;
            const auto fall_distance = state_.fall_start_y - state_.position.y;
            if (!landed_in_water && fall_distance > kFallDamageThreshold) {
                apply_damage(std::ceil(fall_distance - 3.0F), PlayerDeathCause::Fall, true);
            }
            state_.landing_impact = landed_in_water ? 0.0F : 1.0F;
        }
        if (state_.on_ground && state_.velocity.y < 0.0F) {
            state_.velocity.y = 0.0F;
        }
        if (state_.on_ground || water_contact_after_move.swimming) {
            state_.fall_start_y = state_.position.y;
        } else {
            state_.fall_start_y = std::max(state_.fall_start_y, state_.position.y);
        }
        if (state_.on_ground && has_buffered_jump() && !water_contact_after_move.swimming) {
            state_.velocity.y = water_contact_after_move.feet_in_water ? kWadeJumpVelocity : kJumpVelocity;
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

    if (!state_.fly_mode && !state_.on_ground && !water_contact_after_move.swimming) {
        state_.airborne_time += clamped_dt;
    } else {
        state_.airborne_time = 0.0F;
    }

    update_body_yaw(clamped_dt, horizontal_displacement);
    update_survival_state(clamped_dt, world);
}

auto PlayerController::state() const noexcept -> const PlayerState& {
    return state_;
}

auto PlayerController::position() const noexcept -> const glm::vec3& {
    return state_.position;
}

auto PlayerController::eye_position() const noexcept -> glm::vec3 {
    return state_.position + glm::vec3 {0.0F, kEyeHeight, 0.0F};
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
    return kMaxHealth;
}

auto PlayerController::max_air_seconds() const noexcept -> float {
    return kMaxAirSeconds;
}

auto PlayerController::damage_resistance_percent() const noexcept -> float {
    return damage_resistance_percent_;
}

auto PlayerController::is_dead() const noexcept -> bool {
    return state_.dead;
}

void PlayerController::load_state(const PlayerState& state) noexcept {
    state_ = state;
    block_break_progress_ = {};
    reset_jump_assist_state();
    state_.health = std::clamp(state_.health, 0.0F, kMaxHealth);
    state_.air_seconds = std::clamp(state_.air_seconds, 0.0F, kMaxAirSeconds);
    state_.pitch_degrees = std::clamp(state_.pitch_degrees, -89.0F, 89.0F);
    if (state_.dead) {
        state_.velocity = {};
    }
}

void PlayerController::set_position(const glm::vec3& position) noexcept {
    state_.position = position;
    state_.fall_start_y = position.y;
    block_break_progress_ = {};
    reset_jump_assist_state();
}

void PlayerController::set_velocity(const glm::vec3& velocity) noexcept {
    state_.velocity = velocity;
}

void PlayerController::set_selected_block(BlockId block_id) noexcept {
    selected_block_ = block_item_id(block_id);
}

void PlayerController::set_damage_resistance_percent(float percent) noexcept {
    damage_resistance_percent_ = std::clamp(percent, 0.0F, 85.0F);
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
    state_.position = position;
    state_.fall_start_y = position.y;
    state_.body_yaw_degrees = state_.yaw_degrees;
}

void PlayerController::apply_external_damage(float amount, PlayerDeathCause cause) noexcept {
    const auto mitigated_amount = amount * (1.0F - damage_resistance_percent_ / 100.0F);
    apply_damage(mitigated_amount, cause, false);
}

auto PlayerController::current_target(const World& world, float max_distance) const -> RaycastHit {
    return world.raycast(eye_position(), look_direction(), max_distance);
}

auto PlayerController::update_block_breaking(World& world, float dt, bool breaking_held, float max_distance)
    -> std::optional<BrokenBlockResult> {
    if (state_.dead || !breaking_held) {
        cancel_block_breaking();
        return std::nullopt;
    }

    const auto hit = current_target(world, max_distance);
    if (!hit.hit || !is_block_breakable_at(hit.block, hit.block_id)) {
        cancel_block_breaking();
        return std::nullopt;
    }

    const auto duration_seconds = block_break_duration_seconds(hit.block_id);
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
        std::min(block_break_progress_.elapsed_seconds + std::max(dt, 0.0F), duration_seconds);
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
        if (block_overlaps_player(placement_coord)) {
            return std::nullopt;
        }
    }

    world.set_block(placement_coord.x, placement_coord.y, placement_coord.z, block_to_place);
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

void PlayerController::update_body_yaw(float dt, const glm::vec2& horizontal_displacement) noexcept {
    if (state_.dead) {
        return;
    }

    const auto clamped_dt = std::max(dt, 0.0F);
    const auto horizontal_distance = glm::length(horizontal_displacement);
    const auto horizontal_speed = clamped_dt > 1.0e-5F ? horizontal_distance / clamped_dt : 0.0F;

    const auto moving = horizontal_speed > kBodyYawMoveThreshold;
    const auto target_yaw = moving ? yaw_degrees_from_direction(horizontal_displacement) : state_.yaw_degrees;
    const auto turn_speed = moving ? kBodyYawMoveTurnSpeed : kBodyYawIdleTurnSpeed;
    state_.body_yaw_degrees = rotate_towards_degrees(state_.body_yaw_degrees, target_yaw, turn_speed * clamped_dt);
}

void PlayerController::update_survival_state(float dt, const World& world) {
    if (state_.dead) {
        return;
    }

    const auto water_contact = state_.fly_mode ? WaterContactState {} : sample_water_contact(world, state_.position);
    state_.swimming = !state_.fly_mode && water_contact.swimming;
    state_.head_underwater = !state_.fly_mode && water_contact.head_in_water;

    if (state_.head_underwater) {
        state_.air_seconds = std::max(0.0F, state_.air_seconds - dt);
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

    if (state_.health < kMaxHealth && state_.regen_delay <= 0.0F && !state_.head_underwater) {
        state_.regen_tick_timer += dt;
        while (state_.regen_tick_timer >= kRegenerationInterval && state_.health < kMaxHealth) {
            heal(1.0F);
            state_.regen_tick_timer -= kRegenerationInterval;
        }
    } else if (state_.health >= kMaxHealth || state_.head_underwater) {
        state_.regen_tick_timer = 0.0F;
    }
}

void PlayerController::move_axis(float delta, int axis, const World& world) {
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
                if (!is_block_collidable(player_physics_block(world, block_x, y, z))) {
                    continue;
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
                if (!is_block_collidable(player_physics_block(world, x, y, block_z))) {
                    continue;
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

void PlayerController::apply_damage(float amount, PlayerDeathCause cause, bool bypass_cooldown) noexcept {
    if (amount <= 0.0F || state_.dead) {
        return;
    }
    if (!bypass_cooldown && state_.damage_cooldown > 0.0F) {
        return;
    }

    state_.health = std::max(0.0F, state_.health - amount);
    state_.hurt_timer = kHurtFlashDuration;
    state_.damage_cooldown = kInvulnerabilityDuration;
    state_.regen_delay = kRegenerationDelay;
    state_.regen_tick_timer = 0.0F;
    if (state_.health <= 0.0F) {
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
    }
}

void PlayerController::heal(float amount) noexcept {
    if (amount <= 0.0F || state_.dead) {
        return;
    }
    state_.health = std::min(kMaxHealth, state_.health + amount);
}

void PlayerController::reset_jump_assist_state() noexcept {
    ground_coyote_timer_ = 0.0F;
    jump_buffer_timer_ = 0.0F;
}

auto PlayerController::is_liquid_at(const World& world, const glm::vec3& point) const noexcept -> bool {
    const auto block_x = static_cast<int>(std::floor(point.x));
    const auto block_y = static_cast<int>(std::floor(point.y));
    const auto block_z = static_cast<int>(std::floor(point.z));
    if (!is_world_y_valid(block_y)) {
        return false;
    }

    const auto level = world.peek_water_level_or_generated(block_x, block_y, block_z);
    if (level == 0) {
        return false;
    }

    const auto top_height = world.peek_water_level_or_generated(block_x, block_y + 1, block_z) > 0
                                ? 1.0F
                                : static_cast<float>(level) / static_cast<float>(kMaxWaterLevel);
    return point.y < static_cast<float>(block_y) + top_height;
}

auto PlayerController::sample_water_contact(const World& world, const glm::vec3& feet_position) const noexcept -> WaterContactState {
    WaterContactState water_contact {};
    if (state_.fly_mode) {
        return water_contact;
    }

    constexpr float sample_radius = kPlayerWidth * 0.35F;
    const std::array<glm::vec2, 5> horizontal_offsets {{
        {0.0F, 0.0F},
        {-sample_radius, -sample_radius},
        {-sample_radius, sample_radius},
        {sample_radius, -sample_radius},
        {sample_radius, sample_radius},
    }};

    const auto any_sample_at_height_is_liquid = [&](float height) {
        for (const auto& offset : horizontal_offsets) {
            if (is_liquid_at(world, feet_position + glm::vec3 {offset.x, height, offset.y})) {
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
