#include "gameplay/PlayerController.h"

#include "world/World.h"

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace valcraft {

namespace {

constexpr float kMoveSpeed = 5.6F;
constexpr float kFlySpeed = 10.0F;
constexpr float kJumpVelocity = 7.5F;
constexpr float kGravity = 24.0F;
constexpr float kMouseSensitivity = 0.08F;
constexpr float kCollisionEpsilon = 0.001F;

auto normalized_horizontal(const glm::vec3& vector) -> glm::vec3 {
    const auto horizontal = glm::vec3 {vector.x, 0.0F, vector.z};
    const auto length = glm::length(horizontal);
    if (length <= 1.0e-5F) {
        return {};
    }
    return horizontal / length;
}

} // namespace

PlayerController::PlayerController(glm::vec3 spawn_position) {
    state_.position = spawn_position;
}

void PlayerController::update(const PlayerInput& input, float dt, const World& world) {
    state_.on_ground = false;

    if (input.toggle_fly) {
        state_.fly_mode = !state_.fly_mode;
        if (state_.fly_mode) {
            state_.velocity = {};
        }
    }

    state_.yaw_degrees += input.look_delta_x * kMouseSensitivity;
    state_.pitch_degrees = std::clamp(state_.pitch_degrees - input.look_delta_y * kMouseSensitivity, -89.0F, 89.0F);

    const auto forward = normalized_horizontal(look_direction());
    const auto right = glm::normalize(glm::cross(forward, glm::vec3 {0.0F, 1.0F, 0.0F}));
    auto wish = forward * input.move_forward + right * input.move_right;
    if (glm::dot(wish, wish) > 1.0e-5F) {
        wish = glm::normalize(wish);
    }

    if (state_.fly_mode) {
        auto fly_velocity = wish + glm::vec3 {0.0F, input.move_up, 0.0F};
        if (glm::dot(fly_velocity, fly_velocity) > 1.0e-5F) {
            fly_velocity = glm::normalize(fly_velocity) * kFlySpeed;
        }
        state_.velocity = fly_velocity;
    } else {
        state_.velocity.x = wish.x * kMoveSpeed;
        state_.velocity.z = wish.z * kMoveSpeed;
        state_.velocity.y -= kGravity * dt;
        if (input.jump && collides_at(world, state_.position + glm::vec3 {0.0F, -0.05F, 0.0F})) {
            state_.velocity.y = kJumpVelocity;
        }
    }

    move_axis(state_.velocity.x * dt, 0, world);
    move_axis(state_.velocity.y * dt, 1, world);
    move_axis(state_.velocity.z * dt, 2, world);

    if (!state_.fly_mode) {
        state_.on_ground = collides_at(world, state_.position + glm::vec3 {0.0F, -0.05F, 0.0F});
        if (state_.on_ground && state_.velocity.y < 0.0F) {
            state_.velocity.y = 0.0F;
        }
    }
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
    return selected_block_;
}

void PlayerController::set_position(const glm::vec3& position) noexcept {
    state_.position = position;
}

void PlayerController::set_velocity(const glm::vec3& velocity) noexcept {
    state_.velocity = velocity;
}

void PlayerController::set_selected_block(BlockId block_id) noexcept {
    selected_block_ = block_id;
}

auto PlayerController::current_target(const World& world, float max_distance) const -> RaycastHit {
    return world.raycast(eye_position(), look_direction(), max_distance);
}

auto PlayerController::try_break_block(World& world, float max_distance) const -> bool {
    const auto hit = current_target(world, max_distance);
    if (!hit.hit || !is_block_solid(hit.block_id)) {
        return false;
    }

    world.set_block(hit.block.x, hit.block.y, hit.block.z, to_block_id(BlockType::Air));
    return true;
}

auto PlayerController::try_place_block(World& world, float max_distance) const -> bool {
    if (selected_block_ == to_block_id(BlockType::Air)) {
        return false;
    }

    const auto hit = current_target(world, max_distance);
    if (!hit.hit) {
        return false;
    }
    if (!is_world_y_valid(hit.adjacent.y)) {
        return false;
    }
    if (is_block_solid(world.get_block(hit.adjacent.x, hit.adjacent.y, hit.adjacent.z))) {
        return false;
    }
    if (block_overlaps_player(hit.adjacent)) {
        return false;
    }

    world.set_block(hit.adjacent.x, hit.adjacent.y, hit.adjacent.z, selected_block_);
    return true;
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
                if (is_block_solid(world.get_block(x, y, z))) {
                    return true;
                }
            }
        }
    }

    return false;
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
                if (!is_block_solid(world.get_block(block_x, y, z))) {
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
                if (!is_block_solid(world.get_block(x, block_y, z))) {
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
                if (!is_block_solid(world.get_block(x, y, block_z))) {
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

} // namespace valcraft
