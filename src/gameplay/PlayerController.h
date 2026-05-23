#pragma once

#include "world/Block.h"

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <optional>
#include <string_view>

namespace valcraft {

class World;

enum class PlayerDeathCause : std::uint8_t {
    None = 0,
    Fall = 1,
    Drowning = 2,
    Void = 3,
    Zombie = 4,
};

inline constexpr auto player_death_cause_label(PlayerDeathCause cause) noexcept -> std::string_view {
    switch (cause) {
    case PlayerDeathCause::Fall:
        return "CHUTE";
    case PlayerDeathCause::Drowning:
        return "NOYADE";
    case PlayerDeathCause::Void:
        return "ABYSSE";
    case PlayerDeathCause::Zombie:
        return "ZOMBIE";
    case PlayerDeathCause::None:
    default:
        return "INCONNUE";
    }
}

struct PlayerInput {
    float move_forward = 0.0F;
    float move_right = 0.0F;
    float move_up = 0.0F;
    float look_delta_x = 0.0F;
    float look_delta_y = 0.0F;
    bool jump = false;
    bool sprint = false;
    bool toggle_fly = false;
};

struct PlayerState {
    glm::vec3 position {0.0F, 70.0F, 0.0F};
    glm::vec3 velocity {0.0F};
    float yaw_degrees = -90.0F;
    float pitch_degrees = -18.0F;
    float body_yaw_degrees = -90.0F;
    float animation_time = 0.0F;
    float step_phase = 0.0F;
    float health = 20.0F;
    float air_seconds = 10.0F;
    float hurt_timer = 0.0F;
    float damage_cooldown = 0.0F;
    float regen_delay = 0.0F;
    float regen_tick_timer = 0.0F;
    float drowning_tick_timer = 0.0F;
    float fall_start_y = 70.0F;
    float primary_action_progress = 0.0F;
    float secondary_action_progress = 0.0F;
    float landing_impact = 0.0F;
    float airborne_time = 0.0F;
    float look_sway_yaw = 0.0F;
    float look_sway_pitch = 0.0F;
    bool on_ground = false;
    bool fly_mode = false;
    bool head_underwater = false;
    bool swimming = false;
    bool primary_action_active = false;
    bool secondary_action_active = false;
    bool dead = false;
    PlayerDeathCause death_cause = PlayerDeathCause::None;
};

struct BrokenBlockResult {
    BlockCoord block {};
    BlockId block_id = to_block_id(BlockType::Air);
};

struct PlacedBlockResult {
    BlockCoord block {};
    BlockId block_id = to_block_id(BlockType::Air);
};

struct BlockBreakProgress {
    bool active = false;
    BlockCoord block {};
    BlockId block_id = to_block_id(BlockType::Air);
    float elapsed_seconds = 0.0F;
    float duration_seconds = 0.0F;
    float progress = 0.0F;
    std::uint8_t crack_stage = 0;
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
    [[nodiscard]] auto max_health() const noexcept -> float;
    [[nodiscard]] auto max_air_seconds() const noexcept -> float;
    [[nodiscard]] auto damage_resistance_percent() const noexcept -> float;
    [[nodiscard]] auto apnea_resistance_percent() const noexcept -> float;
    [[nodiscard]] auto fall_safety_multiplier() const noexcept -> float;
    [[nodiscard]] auto movement_speed_multiplier() const noexcept -> float;
    [[nodiscard]] auto block_break_speed_multiplier() const noexcept -> float;
    [[nodiscard]] auto is_dead() const noexcept -> bool;

    void load_state(const PlayerState& state) noexcept;
    void set_position(const glm::vec3& position) noexcept;
    void set_velocity(const glm::vec3& velocity) noexcept;
    void set_fly_mode_enabled(bool enabled) noexcept;
    void set_selected_block(BlockId block_id) noexcept;
    void set_damage_resistance_percent(float percent) noexcept;
    void set_apnea_resistance_percent(float percent) noexcept;
    void set_fall_safety_multiplier(float multiplier) noexcept;
    void set_movement_speed_multiplier(float multiplier) noexcept;
    void set_block_break_speed_multiplier(float multiplier) noexcept;
    void trigger_primary_action() noexcept;
    void trigger_secondary_action() noexcept;
    void respawn(const glm::vec3& position) noexcept;
    void apply_external_damage(float amount, PlayerDeathCause cause) noexcept;

    [[nodiscard]] auto current_target(const World& world, float max_distance = 8.0F) const -> RaycastHit;
    auto update_block_breaking(World& world, float dt, bool breaking_held, float max_distance = 8.0F)
        -> std::optional<BrokenBlockResult>;
    void cancel_block_breaking() noexcept;
    [[nodiscard]] auto block_break_progress() const noexcept -> const BlockBreakProgress&;
    auto try_break_block(World& world, float max_distance = 8.0F) const -> std::optional<BrokenBlockResult>;
    auto try_place_block(World& world, float max_distance = 8.0F) const -> std::optional<PlacedBlockResult>;
    [[nodiscard]] auto collides_at(const World& world, const glm::vec3& feet_position) const -> bool;

private:
    struct WaterContactState {
        bool feet_in_water = false;
        bool body_in_water = false;
        bool head_in_water = false;
        bool swimming = false;
    };

    void update_survival_state(float dt, const World& world);
    void update_body_yaw(float dt, const glm::vec2& horizontal_displacement) noexcept;
    void move_axis(float delta, int axis, const World& world);
    void apply_damage(float amount, PlayerDeathCause cause, bool bypass_cooldown = false) noexcept;
    void heal(float amount) noexcept;
    void reset_jump_assist_state() noexcept;
    [[nodiscard]] auto block_overlaps_player(const BlockCoord& block_coord) const noexcept -> bool;
    [[nodiscard]] auto point_block(const World& world, const glm::vec3& point) const noexcept -> BlockId;
    [[nodiscard]] auto is_liquid_at(const World& world, const glm::vec3& point) const noexcept -> bool;
    [[nodiscard]] auto sample_water_contact(const World& world, const glm::vec3& feet_position) const noexcept -> WaterContactState;

    PlayerState state_ {};
    BlockBreakProgress block_break_progress_ {};
    BlockId selected_block_ = to_block_id(BlockType::Grass);
    float damage_resistance_percent_ = 0.0F;
    float apnea_resistance_percent_ = 0.0F;
    float fall_safety_multiplier_ = 1.0F;
    float movement_speed_multiplier_ = 1.0F;
    float block_break_speed_multiplier_ = 1.0F;
    float ground_coyote_timer_ = 0.0F;
    float jump_buffer_timer_ = 0.0F;
    static constexpr float kPlayerWidth = 0.6F;
    static constexpr float kPlayerHeight = 1.8F;
    static constexpr float kEyeHeight = 1.62F;
    static constexpr float kMaxHealth = 20.0F;
    static constexpr float kMaxAirSeconds = 10.0F;
};

} // namespace valcraft
