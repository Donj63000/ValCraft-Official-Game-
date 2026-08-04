#pragma once

#include "world/Block.h"

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <optional>
#include <string_view>

namespace valcraft {

class World;
class ShipEntity;
struct OceanState;

enum class PlayerWaterMovementProfile : std::uint8_t {
    Standard = 0,
    Poolrooms = 1,
};

enum class PlayerDeathCause : std::uint8_t {
    None = 0,
    Fall = 1,
    Drowning = 2,
    Void = 3,
    Zombie = 4,
    Starvation = 5,
    Thirst = 6,
    Stranded = 7,
    JackThePirate = 8,
    MarlowTheDrowned = 9,
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
    case PlayerDeathCause::Starvation:
        return "FAIM";
    case PlayerDeathCause::Thirst:
        return "SOIF";
    case PlayerDeathCause::Stranded:
        return "NAVIRE PERDU";
    case PlayerDeathCause::JackThePirate:
        return "JACK LE PIRATE";
    case PlayerDeathCause::MarlowTheDrowned:
        return "MARLOW LE NOYE";
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

struct PlayerWaterContactSnapshot {
    bool feet_in_water = false;
    bool body_in_water = false;
    bool head_in_water = false;
    bool swimming = false;

    [[nodiscard]] constexpr auto any_contact() const noexcept -> bool {
        return feet_in_water || body_in_water || head_in_water;
    }
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

struct PlayerDamageResult {
    float requested_damage = 0.0F;
    float damage_after_resistance = 0.0F;
    float health_damage = 0.0F;
    bool blocked_by_invulnerability = false;
    bool killed = false;

    [[nodiscard]] constexpr auto applied() const noexcept -> bool {
        return health_damage > 0.0F;
    }
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

    void update(
        const PlayerInput& input,
        float dt,
        const World& world,
        const ShipEntity* dynamic_obstacle = nullptr,
        const OceanState* dynamic_ocean = nullptr);

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
    [[nodiscard]] auto water_movement_profile() const noexcept
        -> PlayerWaterMovementProfile;
    [[nodiscard]] auto block_break_speed_multiplier() const noexcept -> float;
    [[nodiscard]] auto is_dead() const noexcept -> bool;
    [[nodiscard]] auto is_climbing_dynamic_obstacle() const noexcept -> bool;
    // Je rends l'etat d'eau physique consultable sans exposer ni modifier
    // l'etat interne du joueur. La position fournie represente toujours ses pieds.
    [[nodiscard]] auto sample_world_water_contact(
        const World& world,
        const glm::vec3& feet_position) const noexcept
        -> PlayerWaterContactSnapshot;

    void load_state(const PlayerState& state) noexcept;
    void set_position(const glm::vec3& position) noexcept;
    void translate_platform_delta(const glm::vec3& delta) noexcept;
    void resolve_dynamic_platform_support(float support_height) noexcept;
    void set_velocity(const glm::vec3& velocity) noexcept;
    void set_fly_mode_enabled(bool enabled) noexcept;
    void set_selected_block(BlockId block_id) noexcept;
    void set_max_health(float max_health) noexcept;
    void set_damage_resistance_percent(float percent) noexcept;
    void set_apnea_resistance_percent(float percent) noexcept;
    void set_fall_safety_multiplier(float multiplier) noexcept;
    void set_movement_speed_multiplier(float multiplier) noexcept;
    void set_water_movement_profile(
        PlayerWaterMovementProfile profile) noexcept;
    void set_block_break_speed_multiplier(float multiplier) noexcept;
    // Je purge uniquement l'intention de saut mise en tampon quand une
    // interface fige la simulation, sans modifier la position ni la velocite.
    void discard_buffered_jump() noexcept;
    void trigger_primary_action() noexcept;
    void trigger_secondary_action() noexcept;
    void respawn(const glm::vec3& position) noexcept;
    void apply_external_damage(float amount, PlayerDeathCause cause) noexcept;
    [[nodiscard]] auto apply_external_damage_report(
        float amount,
        PlayerDeathCause cause) noexcept -> PlayerDamageResult;

    // Les dangers continus possedent leur propre cadence et ne doivent pas etre
    // annules par la fenetre d'invulnerabilite d'une attaque precedente.
    void apply_environmental_damage(float amount, PlayerDeathCause cause) noexcept;

    // Cette transition represente un echec de scenario absolu. Elle ignore
    // volontairement l'armure et la fenetre d'invulnerabilite des degats ordinaires.
    void force_death(PlayerDeathCause cause) noexcept;

    [[nodiscard]] auto current_target(const World& world, float max_distance = 8.0F) const -> RaycastHit;
    auto update_block_breaking(World& world,
                               float dt,
                               bool breaking_held,
                               float max_distance = 8.0F,
                               float tool_speed_multiplier = 1.0F)
        -> std::optional<BrokenBlockResult>;
    auto update_block_breaking(World& world,
                               float dt,
                               bool breaking_held,
                               const RaycastHit& target,
                               float tool_speed_multiplier = 1.0F)
        -> std::optional<BrokenBlockResult>;
    void cancel_block_breaking() noexcept;
    [[nodiscard]] auto block_break_progress() const noexcept -> const BlockBreakProgress&;
    auto try_break_block(World& world, float max_distance = 8.0F) const -> std::optional<BrokenBlockResult>;
    auto try_place_block(World& world, float max_distance = 8.0F) const -> std::optional<PlacedBlockResult>;
    [[nodiscard]] auto collides_at(const World& world, const glm::vec3& feet_position) const -> bool;
    [[nodiscard]] auto overlaps_dynamic_obstacle(const ShipEntity& obstacle) const noexcept -> bool;
    [[nodiscard]] auto dynamic_support_height(const ShipEntity& obstacle) const noexcept
        -> std::optional<float>;
    [[nodiscard]] auto resolve_dynamic_obstacle_overlap(const World& world,
                                                        const ShipEntity& obstacle) -> bool;

private:
    void update_survival_state(
        float dt,
        const PlayerWaterContactSnapshot& water_contact);
    void update_body_yaw(float dt, const glm::vec2& horizontal_displacement) noexcept;
    void move_axis(float delta, int axis, const World& world, const ShipEntity* dynamic_obstacle);
    [[nodiscard]] auto try_step_onto_backrooms_connector(
        const glm::vec3& horizontal_candidate,
        const World& world) -> bool;
    [[nodiscard]] auto resolve_backrooms_connector_support(
        const World& world,
        bool was_grounded) -> bool;
    auto apply_damage(
        float amount,
        PlayerDeathCause cause,
        bool bypass_cooldown = false) noexcept -> PlayerDamageResult;
    void enter_death_state(PlayerDeathCause cause) noexcept;
    void heal(float amount) noexcept;
    void reset_jump_assist_state() noexcept;
    void reset_dynamic_climb_state() noexcept;
    [[nodiscard]] auto block_overlaps_player(const BlockCoord& block_coord) const noexcept -> bool;
    [[nodiscard]] auto point_block(const World& world, const glm::vec3& point) const noexcept -> BlockId;
    [[nodiscard]] auto is_liquid_at(
        const World& world,
        const glm::vec3& point) const noexcept
        -> bool;
    [[nodiscard]] auto sample_water_contact(
        const World& world,
        const glm::vec3& feet_position,
        const ShipEntity* dynamic_obstacle,
        const OceanState* dynamic_ocean) const noexcept
        -> PlayerWaterContactSnapshot;
    [[nodiscard]] auto dynamic_support_height_at(const ShipEntity& obstacle,
                                                 const glm::vec3& feet_position,
                                                 float min_height,
                                                 float max_height) const noexcept
        -> std::optional<float>;

    PlayerState state_ {};
    BlockBreakProgress block_break_progress_ {};
    BlockId selected_block_ = to_block_id(BlockType::Grass);
    float max_health_ = 20.0F;
    float damage_resistance_percent_ = 0.0F;
    float apnea_resistance_percent_ = 0.0F;
    float fall_safety_multiplier_ = 1.0F;
    float movement_speed_multiplier_ = 1.0F;
    // Je garde ce réglage dans le runtime du contrôleur : le niveau courant
    // le réapplique et aucune donnée dérivée de l'eau ne pollue la sauvegarde.
    PlayerWaterMovementProfile water_movement_profile_ =
        PlayerWaterMovementProfile::Standard;
    float block_break_speed_multiplier_ = 1.0F;
    float ground_coyote_timer_ = 0.0F;
    float jump_buffer_timer_ = 0.0F;
    // Je garde l'accroche hors de PlayerState : elle depend d'une entite
    // runtime et ne doit jamais fuiter dans le format de sauvegarde.
    const ShipEntity* climbed_dynamic_obstacle_ = nullptr;
    bool dynamic_climb_regrab_locked_ = false;
    bool dynamic_climb_jump_locked_ = false;
    // Je lisse uniquement la vue après une contremarche parcourue au sol dans
    // les deux sens. Position, collisions et sauvegarde restent exactes.
    float connector_camera_offset_y_ = 0.0F;
    static constexpr float kPlayerWidth = 0.6F;
    static constexpr float kPlayerHeight = 1.8F;
    static constexpr float kEyeHeight = 1.62F;
    static constexpr float kMaxAirSeconds = 10.0F;
};

} // namespace valcraft
