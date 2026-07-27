#include "gameplay/PlayerMusket.h"

#include "render/MusketVisualRecipe.h"

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace valcraft {

namespace {

constexpr float kPi = 3.14159265358979323846F;
constexpr float kDirectionEpsilonSquared = 1.0e-12F;

[[nodiscard]] auto positive_or(float value, float fallback) noexcept -> float {
    return std::isfinite(value) && value > 0.0F ? value : fallback;
}

[[nodiscard]] auto non_negative_or(float value, float fallback) noexcept -> float {
    return std::isfinite(value) && value >= 0.0F ? value : fallback;
}

[[nodiscard]] auto finite_vec3(const glm::vec3& value) noexcept -> bool {
    return std::isfinite(value.x) &&
           std::isfinite(value.y) &&
           std::isfinite(value.z);
}

[[nodiscard]] auto safe_direction(
    const glm::vec3& value,
    const glm::vec3& fallback) noexcept -> glm::vec3 {
    const auto length_squared = glm::dot(value, value);
    if (!finite_vec3(value) ||
        !std::isfinite(length_squared) ||
        length_squared <= kDirectionEpsilonSquared) {
        return fallback;
    }
    return value / std::sqrt(length_squared);
}

[[nodiscard]] auto sanitize_config(const MusketConfig& config) noexcept
    -> MusketConfig {
    auto sanitized = config;
    sanitized.maximum_range = positive_or(
        config.maximum_range,
        kDefaultMusketConfig.maximum_range);
    sanitized.base_damage = non_negative_or(
        config.base_damage,
        kDefaultMusketConfig.base_damage);
    sanitized.reload_seconds = positive_or(
        config.reload_seconds,
        kDefaultMusketConfig.reload_seconds);
    sanitized.ads_seconds = positive_or(
        config.ads_seconds,
        kDefaultMusketConfig.ads_seconds);
    sanitized.recoil_seconds = positive_or(
        config.recoil_seconds,
        kDefaultMusketConfig.recoil_seconds);
    sanitized.hip_spread_degrees = std::clamp(
        non_negative_or(
            config.hip_spread_degrees,
            kDefaultMusketConfig.hip_spread_degrees),
        0.0F,
        45.0F);
    return sanitized;
}

[[nodiscard]] auto splitmix64(std::uint64_t value) noexcept -> std::uint64_t {
    value += 0x9E3779B97F4A7C15ULL;
    value = (value ^ (value >> 30U)) * 0xBF58476D1CE4E5B9ULL;
    value = (value ^ (value >> 27U)) * 0x94D049BB133111EBULL;
    return value ^ (value >> 31U);
}

[[nodiscard]] auto hash_unit_float(std::uint64_t value) noexcept -> float {
    const auto high_bits = static_cast<std::uint32_t>(splitmix64(value) >> 40U);
    return static_cast<float>(high_bits) * (1.0F / 16777216.0F);
}

[[nodiscard]] auto move_towards(
    float value,
    float target,
    float maximum_delta) noexcept -> float {
    if (value < target) {
        return std::min(value + maximum_delta, target);
    }
    return std::max(value - maximum_delta, target);
}

[[nodiscard]] auto sanitized_aim_ratio(
    float aim_ratio) noexcept -> float {
    return std::isfinite(aim_ratio)
        ? std::clamp(aim_ratio, 0.0F, 1.0F)
        : 0.0F;
}

} // namespace

auto player_musket_shot_direction(
    const glm::vec3& forward,
    const glm::vec3& camera_up,
    bool aimed,
    std::uint64_t shot_sequence,
    const MusketConfig& config) noexcept -> glm::vec3 {
    const auto safe_forward = safe_direction(
        forward,
        glm::vec3 {0.0F, 0.0F, -1.0F});
    if (aimed) {
        return safe_forward;
    }

    const auto sanitized = sanitize_config(config);
    if (sanitized.hip_spread_degrees <=
        std::numeric_limits<float>::epsilon()) {
        return safe_forward;
    }

    auto safe_up = safe_direction(
        camera_up,
        glm::vec3 {0.0F, 1.0F, 0.0F});
    auto right = glm::cross(safe_forward, safe_up);
    if (!finite_vec3(right) ||
        glm::dot(right, right) <= kDirectionEpsilonSquared) {
        safe_up =
            std::abs(safe_forward.y) < 0.99F
                ? glm::vec3 {0.0F, 1.0F, 0.0F}
                : glm::vec3 {1.0F, 0.0F, 0.0F};
        right = glm::cross(safe_forward, safe_up);
    }
    right = safe_direction(right, glm::vec3 {1.0F, 0.0F, 0.0F});
    const auto corrected_up = safe_direction(
        glm::cross(right, safe_forward),
        glm::vec3 {0.0F, 1.0F, 0.0F});

    // Je distribue les impacts uniformement sur le disque angulaire au lieu de
    // les concentrer artificiellement au centre du reticule.
    const auto radial_sample = std::sqrt(hash_unit_float(shot_sequence));
    const auto angular_sample =
        hash_unit_float(shot_sequence ^ 0xD1B54A32D192ED03ULL) *
        (2.0F * kPi);
    const auto maximum_tangent = std::tan(
        sanitized.hip_spread_degrees * (kPi / 180.0F));
    const auto radius = radial_sample * maximum_tangent;
    const auto offset =
        right * (std::cos(angular_sample) * radius) +
        corrected_up * (std::sin(angular_sample) * radius);
    return safe_direction(
        safe_forward + offset,
        safe_forward);
}

auto player_musket_world_fov(
    float aim_ratio) noexcept -> float {
    constexpr auto hip_fov = 75.0F;
    constexpr auto aimed_fov = 58.0F;
    const auto ratio =
        sanitized_aim_ratio(aim_ratio);
    return hip_fov +
           (aimed_fov - hip_fov) * ratio;
}

auto player_musket_viewmodel_fov(
    float base_fov,
    float aim_ratio) noexcept -> float {
    constexpr auto default_base_fov = 62.0F;
    constexpr auto maximum_aimed_fov = 50.0F;
    const auto sanitized_base =
        std::isfinite(base_fov)
            ? std::clamp(base_fov, 35.0F, 100.0F)
            : default_base_fov;
    const auto aimed_fov =
        std::min(
            sanitized_base,
            maximum_aimed_fov);
    const auto ratio =
        sanitized_aim_ratio(aim_ratio);
    return sanitized_base +
           (aimed_fov - sanitized_base) * ratio;
}

auto player_musket_look_scale(
    float aim_ratio) noexcept -> float {
    constexpr auto aimed_scale = 0.65F;
    const auto ratio =
        sanitized_aim_ratio(aim_ratio);
    return 1.0F +
           (aimed_scale - 1.0F) * ratio;
}

PlayerMusketController::PlayerMusketController(
    const MusketConfig& config) noexcept
    : config_(sanitize_config(config)) {
    refresh_view();
}

auto PlayerMusketController::update(
    const PlayerMusketInput& input,
    float dt,
    const glm::vec3& forward,
    const glm::vec3& camera_up) noexcept -> const PlayerMusketEvents& {
    events_ = {};
    events_.loaded_after = state_ == PlayerMusketState::Loaded;
    events_.shot_sequence = shot_sequence_;

    const auto fire_pressed =
        input.active &&
        (input.fire_pressed ||
         (input.fire_held &&
          !previous_fire_held_));
    const auto reload_pressed =
        input.active &&
        (input.reload_pressed ||
         (input.reload_held &&
          !previous_reload_held_));
    previous_fire_held_ = input.fire_held;
    previous_reload_held_ = input.reload_held;

    if (!input.active || input.cancel_requested) {
        events_.reload_cancelled =
            state_ == PlayerMusketState::Reloading;
        cancel_runtime();
        events_.loaded_after = state_ == PlayerMusketState::Loaded;
        events_.shot_sequence = shot_sequence_;
        refresh_view();
        return events_;
    }

    active_ = true;
    aim_held_ = input.aim_held;

    // Je resous toujours le tir avant R : les deux fronts dans la meme image
    // consomment donc la balle puis lancent correctement le rechargement.
    if (fire_pressed) {
        if (state_ == PlayerMusketState::Loaded) {
            ++shot_sequence_;
            events_.fired = true;
            events_.chamber_state_changed = true;
            events_.shot_sequence = shot_sequence_;
            events_.maximum_distance = config_.maximum_range;
            const auto requested_damage_multiplier =
                std::isfinite(input.damage_multiplier)
                    ? std::max(input.damage_multiplier, 0.0F)
                    : 1.0F;
            const auto maximum_damage_multiplier =
                std::numeric_limits<float>::max() /
                std::max(config_.base_damage, 1.0F);
            const auto damage_multiplier = std::min(
                requested_damage_multiplier,
                maximum_damage_multiplier);
            events_.damage = config_.base_damage * damage_multiplier;
            events_.shot_direction = player_musket_shot_direction(
                forward,
                camera_up,
                input.aim_held,
                shot_sequence_,
                config_);
            state_ = PlayerMusketState::Empty;
            recoil_remaining_ = config_.recoil_seconds;
        } else {
            events_.dry_fired = true;
        }
    }

    if (reload_pressed && state_ == PlayerMusketState::Empty) {
        state_ = PlayerMusketState::Reloading;
        reload_elapsed_ = 0.0F;
        events_.reload_started = true;
    }

    const auto safe_dt =
        std::isfinite(dt) && dt > 0.0F ? dt : 0.0F;
    if (state_ == PlayerMusketState::Reloading) {
        const auto reload_seconds =
            static_cast<double>(
                config_.reload_seconds);
        reload_elapsed_ = std::min(
            reload_elapsed_ +
                static_cast<double>(safe_dt),
            reload_seconds);
        if (reload_elapsed_ >= reload_seconds) {
            state_ = PlayerMusketState::Loaded;
            reload_elapsed_ = 0.0;
            events_.reload_completed = true;
            events_.chamber_state_changed = true;
        }
    }

    recoil_remaining_ = std::max(
        recoil_remaining_ - safe_dt,
        0.0F);
    const auto aim_target =
        input.aim_held && state_ != PlayerMusketState::Reloading
            ? 1.0F
            : 0.0F;
    aim_ratio_ = move_towards(
        aim_ratio_,
        aim_target,
        safe_dt / config_.ads_seconds);

    events_.loaded_after = state_ == PlayerMusketState::Loaded;
    events_.shot_sequence = shot_sequence_;
    refresh_view();
    return events_;
}

void PlayerMusketController::synchronize_chamber(bool loaded) noexcept {
    state_ = loaded
        ? PlayerMusketState::Loaded
        : PlayerMusketState::Empty;
    active_ = false;
    aim_held_ = false;
    reload_elapsed_ = 0.0;
    recoil_remaining_ = 0.0F;
    aim_ratio_ = 0.0F;
    events_ = {};
    events_.loaded_after = loaded;
    events_.shot_sequence = shot_sequence_;
    refresh_view();
}

void PlayerMusketController::cancel_transient_actions() noexcept {
    cancel_runtime();
    events_ = {};
    events_.loaded_after = state_ == PlayerMusketState::Loaded;
    events_.shot_sequence = shot_sequence_;
    refresh_view();
}

void PlayerMusketController::cancel_runtime() noexcept {
    if (state_ == PlayerMusketState::Reloading) {
        state_ = PlayerMusketState::Empty;
    }
    active_ = false;
    aim_held_ = false;
    aim_ratio_ = 0.0F;
    reload_elapsed_ = 0.0;
    recoil_remaining_ = 0.0F;
}

void PlayerMusketController::reset(
    bool loaded,
    std::uint64_t shot_sequence) noexcept {
    state_ = loaded
        ? PlayerMusketState::Loaded
        : PlayerMusketState::Empty;
    aim_ratio_ = 0.0F;
    reload_elapsed_ = 0.0;
    recoil_remaining_ = 0.0F;
    shot_sequence_ = shot_sequence;
    active_ = false;
    aim_held_ = false;
    previous_fire_held_ = false;
    previous_reload_held_ = false;
    events_ = {};
    events_.loaded_after = loaded;
    refresh_view();
}

auto PlayerMusketController::config() const noexcept
    -> const MusketConfig& {
    return config_;
}

auto PlayerMusketController::view() const noexcept
    -> const PlayerMusketView& {
    return view_;
}

auto PlayerMusketController::events() const noexcept
    -> const PlayerMusketEvents& {
    return events_;
}

auto PlayerMusketController::state() const noexcept
    -> PlayerMusketState {
    return state_;
}

auto PlayerMusketController::loaded() const noexcept -> bool {
    return state_ == PlayerMusketState::Loaded;
}

void PlayerMusketController::refresh_view() noexcept {
    view_.state = state_;
    view_.active = active_;
    view_.aim_requested =
        active_ &&
        aim_held_ &&
        state_ != PlayerMusketState::Reloading;
    view_.aim_ratio = std::clamp(aim_ratio_, 0.0F, 1.0F);
    view_.reload_progress =
        state_ == PlayerMusketState::Reloading
            ? static_cast<float>(
                  std::clamp(
                      reload_elapsed_ /
                          static_cast<double>(
                              config_.reload_seconds),
                      0.0,
                      1.0))
            : 0.0F;
    view_.recoil_ratio = std::clamp(
        recoil_remaining_ / config_.recoil_seconds,
        0.0F,
        1.0F);
    view_.reload_stage =
        state_ == PlayerMusketState::Reloading
            ? musket_reload_stage(
                  view_.reload_progress)
            : 0U;
    view_.shot_sequence = shot_sequence_;
}

} // namespace valcraft
