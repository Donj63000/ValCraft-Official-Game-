#pragma once

#include "world/Block.h"

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

namespace valcraft {

class World;

struct PlayerInput {
    float move_forward = 0.0F;
    float move_right = 0.0F;
    float move_up = 0.0F;
    float look_delta_x = 0.0F;
    float look_delta_y = 0.0F;
    bool jump = false;
    bool toggle_fly = false;
};

struct PlayerState {
    glm::vec3 position {0.0F, 70.0F, 0.0F};
    glm::vec3 velocity {0.0F};
    float yaw_degrees = -90.0F;
    float pitch_degrees = -18.0F;
    bool on_ground = false;
    bool fly_mode = false;
};

class PlayerController {
public:
    explicit PlayerController(glm::vec3 spawn_position = {0.0F, 70.0F, 0.0F});

    void update(const PlayerInput& input, float dt, const World& world);

    [[nodiscard]] auto state() const noexcept -> const PlayerState&;
    [[nodiscard]] auto position() const noexcept -> const glm::vec3&;
    [[nodiscard]] auto eye_position() const noexcept -> glm::vec3;
    [[nodiscard]] auto look_direction() const noexcept -> glm::vec3;
    [[nodiscard]] auto view_matrix() const -> glm::mat4;
    [[nodiscard]] auto selected_block() const noexcept -> BlockId;

    void set_position(const glm::vec3& position) noexcept;
    void set_velocity(const glm::vec3& velocity) noexcept;
    void set_selected_block(BlockId block_id) noexcept;

    [[nodiscard]] auto current_target(const World& world, float max_distance = 8.0F) const -> RaycastHit;
    auto try_break_block(World& world, float max_distance = 8.0F) const -> bool;
    auto try_place_block(World& world, float max_distance = 8.0F) const -> bool;
    [[nodiscard]] auto collides_at(const World& world, const glm::vec3& feet_position) const -> bool;

private:
    void move_axis(float delta, int axis, const World& world);
    [[nodiscard]] auto block_overlaps_player(const BlockCoord& block_coord) const noexcept -> bool;

    PlayerState state_ {};
    BlockId selected_block_ = to_block_id(BlockType::Grass);
    static constexpr float kPlayerWidth = 0.6F;
    static constexpr float kPlayerHeight = 1.8F;
    static constexpr float kEyeHeight = 1.62F;
};

} // namespace valcraft
