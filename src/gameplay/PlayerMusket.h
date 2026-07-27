#pragma once

#include <glm/vec3.hpp>

#include <cstdint>

namespace valcraft {

struct MusketConfig {
    float maximum_range = 50.0F;
    float base_damage = 20.0F;
    float reload_seconds = 5.0F;
    float ads_seconds = 0.18F;
    float recoil_seconds = 0.30F;
    float hip_spread_degrees = 2.5F;

    auto operator==(const MusketConfig&) const -> bool = default;
};

inline constexpr MusketConfig kDefaultMusketConfig {};

enum class PlayerMusketState : std::uint8_t {
    Loaded = 0,
    Empty = 1,
    Reloading = 2,
};

struct PlayerMusketInput {
    float damage_multiplier = 1.0F;
    bool active = false;
    bool aim_held = false;
    bool fire_held = false;
    bool fire_pressed = false;
    bool reload_held = false;
    bool reload_pressed = false;
    bool cancel_requested = false;
};

struct PlayerMusketView {
    PlayerMusketState state = PlayerMusketState::Loaded;
    bool active = false;
    bool aim_requested = false;
    float aim_ratio = 0.0F;
    float reload_progress = 0.0F;
    float recoil_ratio = 0.0F;
    std::uint8_t reload_stage = 0U;
    std::uint64_t shot_sequence = 0U;

    [[nodiscard]] auto loaded() const noexcept -> bool {
        return state == PlayerMusketState::Loaded;
    }

    [[nodiscard]] auto reloading() const noexcept -> bool {
        return state == PlayerMusketState::Reloading;
    }
};

struct PlayerMusketEvents {
    glm::vec3 shot_direction {0.0F, 0.0F, -1.0F};
    float maximum_distance = 0.0F;
    float damage = 0.0F;
    std::uint64_t shot_sequence = 0U;
    bool fired = false;
    bool dry_fired = false;
    bool reload_started = false;
    bool reload_completed = false;
    bool reload_cancelled = false;
    bool chamber_state_changed = false;
    bool loaded_after = true;
};

// Je garde ce calcul pur afin que le gameplay, les replays et les tests
// produisent exactement le meme rayon pour une sequence donnee.
[[nodiscard]] auto player_musket_shot_direction(
    const glm::vec3& forward,
    const glm::vec3& camera_up,
    bool aimed,
    std::uint64_t shot_sequence,
    const MusketConfig& config = kDefaultMusketConfig) noexcept -> glm::vec3;

[[nodiscard]] auto player_musket_world_fov(
    float aim_ratio) noexcept -> float;
[[nodiscard]] auto player_musket_viewmodel_fov(
    float base_fov,
    float aim_ratio) noexcept -> float;
[[nodiscard]] auto player_musket_look_scale(
    float aim_ratio) noexcept -> float;

class PlayerMusketController {
public:
    explicit PlayerMusketController(
        const MusketConfig& config = kDefaultMusketConfig) noexcept;

    [[nodiscard]] auto update(
        const PlayerMusketInput& input,
        float dt,
        const glm::vec3& forward,
        const glm::vec3& camera_up = glm::vec3 {0.0F, 1.0F, 0.0F}) noexcept
        -> const PlayerMusketEvents&;

    // Je synchronise ce point uniquement lors d'un changement d'exemplaire ou
    // d'un chargement de sauvegarde, jamais a chaque image.
    void synchronize_chamber(bool loaded) noexcept;
    void cancel_transient_actions() noexcept;
    void reset(
        bool loaded = true,
        std::uint64_t shot_sequence = 0U) noexcept;

    [[nodiscard]] auto config() const noexcept -> const MusketConfig&;
    [[nodiscard]] auto view() const noexcept -> const PlayerMusketView&;
    [[nodiscard]] auto events() const noexcept -> const PlayerMusketEvents&;
    [[nodiscard]] auto state() const noexcept -> PlayerMusketState;
    [[nodiscard]] auto loaded() const noexcept -> bool;

private:
    void cancel_runtime() noexcept;
    void refresh_view() noexcept;

    MusketConfig config_ {};
    PlayerMusketView view_ {};
    PlayerMusketEvents events_ {};
    PlayerMusketState state_ = PlayerMusketState::Loaded;
    float aim_ratio_ = 0.0F;
    double reload_elapsed_ = 0.0;
    float recoil_remaining_ = 0.0F;
    std::uint64_t shot_sequence_ = 0U;
    bool active_ = false;
    bool aim_held_ = false;
    bool previous_fire_held_ = false;
    bool previous_reload_held_ = false;
};

} // namespace valcraft
