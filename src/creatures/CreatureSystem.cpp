#include "creatures/CreatureSystem.h"

#include <array>
#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/vec2.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>

namespace valcraft {

namespace {

constexpr float kGroundSnapOffset = 1.001F;
constexpr float kPi = 3.14159265358979323846F;
constexpr float kTwoPi = 2.0F * kPi;
constexpr int kSpawnCandidateCount = 4;
constexpr float kPlayerShyDistance = 3.25F;
constexpr float kNightDetectionDistance = 11.5F;
constexpr float kNightAttackDistance = 1.65F;
constexpr float kNightStrikeCooldown = 0.9F;
constexpr float kZombieDamage = 3.0F;
constexpr float kMaxStepHeight = 1.4F;
constexpr float kDawnAttackVisualCap = 0.42F;
constexpr float kNightAttackSampleHeight = 0.9F;
constexpr float kNightChasePersistenceSeconds = 1.35F;
constexpr float kResidentGreetingDistance = 5.0F;
constexpr float kResidentPersonalSpace = 1.65F;
constexpr float kResidentHomeSnapThreshold = 1.75F;

struct SpeciesTuning {
    float day_speed = 1.0F;
    float flee_speed = 2.0F;
    float lurk_speed = 0.6F;
    float chase_speed = 1.6F;
    float day_roam_radius = 4.5F;
    float night_roam_radius = 5.5F;
    float chase_radius = 10.5F;
};

struct SpawnCandidate {
    ChunkCoord coord {};
    float distance_squared = 0.0F;
};

struct SteeringMoveResult {
    bool moved = false;
    bool diverted = false;
    float heading = 0.0F;
};

enum class ResidentRoutinePhase : std::uint8_t {
    Home = 0,
    Morning = 1,
    Work = 2,
    Social = 3,
    Evening = 4,
    Night = 5,
};

auto is_morph_visible(const CreatureCycleState& cycle) noexcept -> bool;
auto is_hostile_night(const CreatureCycleState& cycle) noexcept -> bool;
auto is_spawn_column_clear(const World& world, int world_x, int ground_y, int world_z) -> bool;
auto tuning_for(CreatureSpecies species) noexcept -> SpeciesTuning;
auto horizontal_distance_squared(const glm::vec3& lhs, const glm::vec3& rhs) noexcept -> float;
auto smoothing_factor(float dt, float response_rate) noexcept -> float;
auto try_move_grounded(CreatureInstance& creature,
                       const World& world,
                       const glm::vec2& desired_delta,
                       float roam_radius,
                       std::optional<int> preferred_floor_y = std::nullopt) -> bool;
auto try_move_grounded_with_steering(CreatureInstance& creature,
                                     const World& world,
                                     float base_heading,
                                     float step_distance,
                                     float roam_radius,
                                     bool aggressive,
                                     std::optional<int> preferred_floor_y = std::nullopt) -> SteeringMoveResult;

auto hash_coords(int x, int z, std::uint32_t seed) noexcept -> std::uint32_t {
    auto value = static_cast<std::uint32_t>(x) * 374761393U;
    value ^= static_cast<std::uint32_t>(z) * 668265263U;
    value ^= seed * 2246822519U;
    value = (value ^ (value >> 13U)) * 1274126177U;
    return value ^ (value >> 16U);
}

auto advance_seed(std::uint32_t& state) noexcept -> std::uint32_t {
    state = state * 1664525U + 1013904223U;
    return state;
}

auto next_unit(std::uint32_t& state) noexcept -> float {
    const auto value = advance_seed(state) >> 8U;
    return static_cast<float>(value & 0x00FFFFFFU) / static_cast<float>(0x01000000U);
}

auto next_signed_unit(std::uint32_t& state) noexcept -> float {
    return next_unit(state) * 2.0F - 1.0F;
}

auto wrap_angle(float angle) noexcept -> float {
    while (angle <= -kPi) {
        angle += kTwoPi;
    }
    while (angle > kPi) {
        angle -= kTwoPi;
    }
    return angle;
}

auto rotate_towards(float current, float target, float max_delta) noexcept -> float {
    const auto delta = wrap_angle(target - current);
    return wrap_angle(current + std::clamp(delta, -max_delta, max_delta));
}

auto yaw_from_direction(const glm::vec2& direction) noexcept -> float {
    return std::atan2(direction.y, direction.x);
}

auto direction_from_yaw(float yaw_radians) noexcept -> glm::vec2 {
    return {std::cos(yaw_radians), std::sin(yaw_radians)};
}

auto perpendicular_left(const glm::vec2& value) noexcept -> glm::vec2 {
    return {-value.y, value.x};
}

auto settle_yaw_from_seed(std::uint32_t seed) noexcept -> float {
    return static_cast<float>((seed % 6283U)) / 1000.0F - kPi;
}

auto normalize_or_cardinal(const glm::vec2& value, std::uint32_t seed) noexcept -> glm::vec2 {
    if (glm::dot(value, value) > 1.0e-6F) {
        return glm::normalize(value);
    }

    switch (seed % 4U) {
    case 0U:
        return {1.0F, 0.0F};
    case 1U:
        return {0.0F, 1.0F};
    case 2U:
        return {-1.0F, 0.0F};
    default:
        return {0.0F, -1.0F};
    }
}

auto choose_resident_role(std::uint32_t seed) noexcept -> CreatureResidentRole {
    switch (seed % 4U) {
    case 0U:
        return CreatureResidentRole::Gardener;
    case 1U:
        return CreatureResidentRole::Merchant;
    case 2U:
        return CreatureResidentRole::Artisan;
    default:
        return CreatureResidentRole::Elder;
    }
}

auto resident_routine_phase(float time_of_day) noexcept -> ResidentRoutinePhase {
    const auto normalized = std::fmod(time_of_day, 24.0F);
    if (normalized < 0.0F) {
        return ResidentRoutinePhase::Night;
    }
    if (normalized < 5.5F || normalized >= 20.0F) {
        return ResidentRoutinePhase::Night;
    }
    if (normalized < 8.5F) {
        return ResidentRoutinePhase::Morning;
    }
    if (normalized < 12.5F) {
        return ResidentRoutinePhase::Work;
    }
    if (normalized < 16.5F) {
        return ResidentRoutinePhase::Social;
    }
    if (normalized < 19.5F) {
        return ResidentRoutinePhase::Evening;
    }
    return ResidentRoutinePhase::Home;
}

auto resident_floor_position(const CreatureSpawnAnchor& anchor) noexcept -> glm::vec3 {
    return {
        anchor.spawn_position.x,
        static_cast<float>(anchor.ground_block.y) + kGroundSnapOffset,
        anchor.spawn_position.z,
    };
}

auto resolve_resident_floor_y(const World& world, const CreatureSpawnAnchor& anchor) -> std::optional<int> {
    const auto x = anchor.ground_block.x;
    const auto z = anchor.ground_block.z;
    const auto preferred = anchor.ground_block.y;

    const auto try_y = [&](int y) -> std::optional<int> {
        if (!is_world_y_valid(y) || y > kWorldMaxY - 2) {
            return std::nullopt;
        }
        if (!is_spawn_column_clear(world, x, y, z)) {
            return std::nullopt;
        }
        return y;
    };

    if (const auto exact = try_y(preferred); exact.has_value()) {
        return exact;
    }

    for (int delta = 1; delta <= 4; ++delta) {
        if (const auto lower = try_y(preferred - delta); lower.has_value()) {
            return lower;
        }
    }
    for (int delta = 1; delta <= 2; ++delta) {
        if (const auto higher = try_y(preferred + delta); higher.has_value()) {
            return higher;
        }
    }

    return std::nullopt;
}

auto settle_resident_profile(const CreatureSpawnAnchor& anchor,
                             const glm::vec3& settlement_center,
                             std::uint32_t routine_seed) -> ResidentProfile {
    ResidentProfile profile {};
    profile.anchor = anchor;
    profile.role = choose_resident_role(routine_seed);
    profile.routine_seed = routine_seed;
    profile.home_position = resident_floor_position(anchor);
    profile.interior_position = anchor.patrol_point_count > 1 ? anchor.patrol_points[1] : profile.home_position;
    profile.walk_radius = 2.35F + static_cast<float>(routine_seed % 4U) * 0.35F;
    profile.home_radius = 1.70F;
    profile.roam_radius = std::max(anchor.roam_radius, 8.0F);

    const auto home_xy = glm::vec2 {profile.home_position.x, profile.home_position.z};
    const auto center_xy = glm::vec2 {settlement_center.x, settlement_center.z};
    const auto to_center = normalize_or_cardinal(center_xy - home_xy, routine_seed);
    const auto away_from_center = normalize_or_cardinal(home_xy - center_xy, routine_seed + 13U);
    const auto lateral = perpendicular_left(to_center);
    const auto garden_lateral = perpendicular_left(away_from_center);
    const auto social_distance = 1.8F + static_cast<float>((routine_seed >> 3U) % 4U) * 0.6F;
    const auto work_distance = 1.5F + static_cast<float>((routine_seed >> 5U) % 3U) * 0.55F;
    const auto garden_distance = 1.2F + static_cast<float>((routine_seed >> 7U) % 3U) * 0.45F;

    profile.social_position =
        anchor.patrol_point_count > 2 ?
            anchor.patrol_points[2] :
            profile.home_position + glm::vec3 {to_center.x * social_distance, 0.0F, to_center.y * social_distance};
    profile.work_position =
        anchor.patrol_point_count > 3 ?
            anchor.patrol_points[3] :
            profile.home_position + glm::vec3 {away_from_center.x * work_distance, 0.0F, away_from_center.y * work_distance};
    profile.garden_position =
        anchor.patrol_point_count > 0 ?
            anchor.patrol_points[0] :
            profile.home_position +
                glm::vec3 {lateral.x * garden_distance + garden_lateral.x * 0.4F, 0.0F, lateral.y * garden_distance + garden_lateral.y * 0.4F};

    profile.interior_position.y = profile.home_position.y;
    profile.social_position.y = profile.home_position.y;
    profile.work_position.y = profile.home_position.y;
    profile.garden_position.y = profile.home_position.y;
    return profile;
}

auto resident_target_for_phase(const ResidentProfile& profile, ResidentRoutinePhase phase) noexcept -> glm::vec3 {
    switch (profile.role) {
    case CreatureResidentRole::Gardener:
        switch (phase) {
        case ResidentRoutinePhase::Morning:
            return profile.garden_position;
        case ResidentRoutinePhase::Work:
            return profile.garden_position;
        case ResidentRoutinePhase::Social:
            return profile.social_position;
        case ResidentRoutinePhase::Evening:
            return profile.home_position;
        case ResidentRoutinePhase::Home:
        case ResidentRoutinePhase::Night:
        default:
            return profile.interior_position;
        }
    case CreatureResidentRole::Merchant:
        switch (phase) {
        case ResidentRoutinePhase::Morning:
        case ResidentRoutinePhase::Work:
            return profile.work_position;
        case ResidentRoutinePhase::Social:
            return profile.social_position;
        case ResidentRoutinePhase::Evening:
            return profile.home_position;
        case ResidentRoutinePhase::Home:
        case ResidentRoutinePhase::Night:
        default:
            return profile.interior_position;
        }
    case CreatureResidentRole::Elder:
        switch (phase) {
        case ResidentRoutinePhase::Morning:
        case ResidentRoutinePhase::Work:
            return profile.home_position;
        case ResidentRoutinePhase::Social:
            return profile.social_position;
        case ResidentRoutinePhase::Evening:
            return profile.home_position;
        case ResidentRoutinePhase::Home:
        case ResidentRoutinePhase::Night:
        default:
            return profile.interior_position;
        }
    case CreatureResidentRole::Artisan:
    default:
        switch (phase) {
        case ResidentRoutinePhase::Morning:
            return profile.work_position;
        case ResidentRoutinePhase::Work:
            return profile.work_position;
        case ResidentRoutinePhase::Social:
            return profile.social_position;
        case ResidentRoutinePhase::Evening:
            return profile.home_position;
        case ResidentRoutinePhase::Home:
        case ResidentRoutinePhase::Night:
        default:
            return profile.interior_position;
        }
    }
}

auto resident_speed_factor(CreatureResidentRole role) noexcept -> float {
    switch (role) {
    case CreatureResidentRole::Gardener:
        return 0.84F;
    case CreatureResidentRole::Merchant:
        return 0.96F;
    case CreatureResidentRole::Elder:
        return 0.68F;
    case CreatureResidentRole::Artisan:
    default:
        return 0.90F;
    }
}

auto resident_behavior_bias(CreatureResidentRole role) noexcept -> float {
    switch (role) {
    case CreatureResidentRole::Gardener:
        return 0.70F;
    case CreatureResidentRole::Merchant:
        return 0.88F;
    case CreatureResidentRole::Elder:
        return 0.60F;
    case CreatureResidentRole::Artisan:
    default:
        return 0.78F;
    }
}

auto resolve_grounded_target_y(const World& world,
                               int world_x,
                               int world_z,
                               float current_y,
                               std::optional<int> preferred_floor_y = std::nullopt) -> std::optional<float> {
    const auto try_floor = [&](int floor_y) -> std::optional<float> {
        if (!is_world_y_valid(floor_y) || floor_y > kWorldMaxY - 2) {
            return std::nullopt;
        }
        if (!is_spawn_column_clear(world, world_x, floor_y, world_z)) {
            return std::nullopt;
        }

        const auto target_y = static_cast<float>(floor_y) + kGroundSnapOffset;
        if (std::abs(target_y - current_y) > kMaxStepHeight) {
            return std::nullopt;
        }
        return target_y;
    };

    if (preferred_floor_y.has_value()) {
        if (const auto exact = try_floor(*preferred_floor_y); exact.has_value()) {
            return exact;
        }
        for (int delta = 1; delta <= 2; ++delta) {
            if (const auto lower = try_floor(*preferred_floor_y - delta); lower.has_value()) {
                return lower;
            }
            if (const auto upper = try_floor(*preferred_floor_y + delta); upper.has_value()) {
                return upper;
            }
        }
        return std::nullopt;
    }

    const auto surface_y = world.loaded_surface_height(world_x, world_z);
    if (!surface_y.has_value()) {
        return std::nullopt;
    }
    return try_floor(*surface_y);
}

void update_resident_creature(CreatureInstance& creature,
                              const ResidentProfile& profile,
                              float dt,
                              const World& world,
                              const glm::vec3& player_position,
                              const EnvironmentState& environment) {
    const auto tuning = tuning_for(creature.anchor.species);
    const auto home_position = resident_floor_position(creature.anchor);
    const auto preferred_floor_y = creature.anchor.ground_block.y;
    if (std::abs(creature.position.y - home_position.y) > kResidentHomeSnapThreshold ||
        horizontal_distance_squared(creature.position, home_position) > profile.roam_radius * profile.roam_radius) {
        creature.position = home_position;
        creature.yaw_radians = settle_yaw_from_seed(profile.routine_seed);
        creature.wander_heading = creature.yaw_radians;
    }

    creature.phase = CreaturePhase::Day;
    creature.morph_factor = 0.0F;
    creature.animation_time += std::max(dt, 0.0F);
    creature.behavior_timer = std::max(0.0F, creature.behavior_timer - dt);
    creature.attack_cooldown = 0.0F;

    const auto to_player = glm::vec2 {
        player_position.x - creature.position.x,
        player_position.z - creature.position.z,
    };
    const auto player_distance_sq = glm::dot(to_player, to_player);
    const auto player_distance = std::sqrt(std::max(player_distance_sq, 0.0F));
    const auto player_close = player_distance < kResidentPersonalSpace && player_distance_sq > 1.0e-6F;
    const auto phase = resident_routine_phase(environment.time_of_day);
    const auto target_position = resident_target_for_phase(profile, phase);
    const auto to_target = glm::vec2 {
        target_position.x - creature.position.x,
        target_position.z - creature.position.z,
    };
    const auto target_distance = std::sqrt(std::max(glm::dot(to_target, to_target), 0.0F));
    const auto target_direction = normalize_or_cardinal(to_target, profile.routine_seed);
    const auto orbit_direction = perpendicular_left(target_direction) * ((profile.routine_seed & 1U) == 0U ? 1.0F : -1.0F);
    const auto resident_speed = tuning.day_speed * resident_speed_factor(profile.role);
    const auto bias = resident_behavior_bias(profile.role);

    glm::vec2 desired_move {0.0F};
    auto desired_yaw = creature.yaw_radians;
    float target_motion_amount = 0.08F;
    float target_gaze_weight = 0.26F;

    if (player_close) {
        const auto give_space_direction = -glm::normalize(to_player);
        creature.behavior_state = CreatureBehaviorState::Wander;
        creature.behavior_timer = 0.34F;
        desired_move = give_space_direction * resident_speed * dt * (0.36F + bias * 0.16F);
        desired_yaw = yaw_from_direction(give_space_direction);
        target_motion_amount = 0.38F + bias * 0.12F;
        target_gaze_weight = 0.20F;
    } else if (target_distance > profile.walk_radius * 0.60F) {
        creature.behavior_state = CreatureBehaviorState::Wander;
        creature.behavior_timer = 0.75F + bias * 0.80F;
        desired_move = target_direction * resident_speed * dt * (0.58F + bias * 0.18F);
        desired_yaw = yaw_from_direction(target_direction);
        target_motion_amount = 0.34F + bias * 0.26F;
        target_gaze_weight = 0.18F + bias * 0.18F;
    } else {
        const auto choice = next_unit(creature.behavior_seed);
        switch (phase) {
        case ResidentRoutinePhase::Night:
        case ResidentRoutinePhase::Home:
            if (choice < 0.55F) {
                creature.behavior_state = CreatureBehaviorState::Idle;
                creature.behavior_timer = 0.90F + choice * 0.80F;
                desired_yaw = creature.wander_heading;
            } else {
                creature.behavior_state = CreatureBehaviorState::Stare;
                creature.behavior_timer = 0.50F + choice * 0.70F;
                if (player_distance_sq > 1.0e-6F) {
                    desired_yaw = yaw_from_direction(glm::normalize(to_player));
                }
                target_gaze_weight = 0.72F;
            }
            target_motion_amount = 0.05F + bias * 0.04F;
            break;
        case ResidentRoutinePhase::Morning:
        case ResidentRoutinePhase::Work:
            if (choice < 0.38F) {
                creature.behavior_state = CreatureBehaviorState::Sniff;
                creature.behavior_timer = 0.65F + choice * 0.55F;
                desired_move = orbit_direction * resident_speed * dt * (0.10F + bias * 0.06F);
                desired_yaw = yaw_from_direction(orbit_direction);
                target_motion_amount = 0.22F + bias * 0.16F;
                target_gaze_weight = 0.32F;
            } else if (choice < 0.72F) {
                creature.behavior_state = CreatureBehaviorState::Wander;
                creature.behavior_timer = 0.75F + choice * 0.90F;
                desired_move = target_direction * resident_speed * dt * (0.18F + bias * 0.08F);
                desired_yaw = yaw_from_direction(target_direction);
                target_motion_amount = 0.30F + bias * 0.20F;
                target_gaze_weight = 0.22F;
            } else {
                creature.behavior_state = CreatureBehaviorState::Idle;
                creature.behavior_timer = 0.70F + choice * 0.60F;
                desired_yaw = creature.wander_heading;
                target_motion_amount = 0.12F + bias * 0.08F;
                target_gaze_weight = 0.24F;
            }
            break;
        case ResidentRoutinePhase::Social:
        case ResidentRoutinePhase::Evening:
        default:
            if (choice < 0.42F) {
                creature.behavior_state = CreatureBehaviorState::Stare;
                creature.behavior_timer = 0.60F + choice * 0.60F;
                if (player_distance_sq > 1.0e-6F) {
                    desired_yaw = yaw_from_direction(glm::normalize(to_player));
                }
                target_motion_amount = 0.10F + bias * 0.06F;
                target_gaze_weight = 0.80F;
            } else if (choice < 0.78F) {
                creature.behavior_state = CreatureBehaviorState::Sniff;
                creature.behavior_timer = 0.55F + choice * 0.50F;
                desired_move = orbit_direction * resident_speed * dt * (0.08F + bias * 0.04F);
                desired_yaw = yaw_from_direction(orbit_direction);
                target_motion_amount = 0.18F + bias * 0.10F;
                target_gaze_weight = 0.34F;
            } else {
                creature.behavior_state = CreatureBehaviorState::Idle;
                creature.behavior_timer = 0.80F + choice * 0.60F;
                desired_yaw = creature.wander_heading;
                target_motion_amount = 0.12F + bias * 0.08F;
                target_gaze_weight = 0.26F;
            }
            break;
        }
    }

    const auto desired_distance = glm::length(desired_move);
    bool attempted_move = false;
    bool moved = false;
    bool steering_diverted = false;
    auto resolved_yaw = desired_yaw;
    if (desired_distance > 1.0e-6F) {
        attempted_move = true;
        const auto steering_result = try_move_grounded_with_steering(
            creature,
            world,
            resolved_yaw,
            desired_distance,
            profile.roam_radius,
            false,
            preferred_floor_y);
        moved = steering_result.moved;
        steering_diverted = steering_result.diverted;
        resolved_yaw = steering_result.heading;
    }

    const glm::vec2 travelled_horizontal {
        creature.position.x - home_position.x,
        creature.position.z - home_position.z,
    };
    if (glm::dot(travelled_horizontal, travelled_horizontal) > 1.0e-6F) {
        resolved_yaw = yaw_from_direction(glm::normalize(travelled_horizontal));
        if (creature.behavior_state == CreatureBehaviorState::Wander ||
            creature.behavior_state == CreatureBehaviorState::Sniff) {
            creature.wander_heading = resolved_yaw;
        }
    } else if (attempted_move && !moved) {
        creature.wander_heading = wrap_angle(creature.wander_heading + kPi * 0.35F);
    }
    if (steering_diverted && creature.behavior_state == CreatureBehaviorState::Wander) {
        creature.wander_heading = resolved_yaw;
    }
    creature.yaw_radians = resolved_yaw;

    if (dt > 0.0F) {
        const auto reference_distance = std::max(resident_speed * dt, 0.001F);
        const auto realised_motion = glm::clamp(glm::length(desired_move) / reference_distance, 0.0F, 1.0F);
        target_motion_amount = std::max(target_motion_amount, realised_motion * 0.88F);
    }

    const auto response = smoothing_factor(dt, 8.2F);
    creature.motion_amount = glm::mix(creature.motion_amount, glm::clamp(target_motion_amount, 0.0F, 1.0F), response);
    creature.gaze_weight = glm::mix(creature.gaze_weight, glm::clamp(target_gaze_weight, 0.0F, 1.0F), response);
    creature.attack_amount = glm::mix(creature.attack_amount, 0.0F, response);
}

auto is_chunk_within_radius(const ChunkCoord& center, const ChunkCoord& coord, int radius) noexcept -> bool {
    return std::abs(coord.x - center.x) <= radius && std::abs(coord.z - center.z) <= radius;
}

auto chunk_distance_squared_to_player(const ChunkCoord& coord, const glm::vec3& player_position) noexcept -> float {
    const auto center_x = static_cast<float>(coord.x * kChunkSizeX) + static_cast<float>(kChunkSizeX) * 0.5F;
    const auto center_z = static_cast<float>(coord.z * kChunkSizeZ) + static_cast<float>(kChunkSizeZ) * 0.5F;
    const auto dx = center_x - player_position.x;
    const auto dz = center_z - player_position.z;
    return dx * dx + dz * dz;
}

auto horizontal_distance_squared(const glm::vec3& lhs, const glm::vec3& rhs) noexcept -> float {
    const auto dx = lhs.x - rhs.x;
    const auto dz = lhs.z - rhs.z;
    return dx * dx + dz * dz;
}

auto melee_height_layer(const glm::vec3& position) noexcept -> int {
    return static_cast<int>(std::floor(position.y));
}

auto shares_melee_height_layer(const glm::vec3& lhs, const glm::vec3& rhs) noexcept -> bool {
    return melee_height_layer(lhs) == melee_height_layer(rhs);
}

auto has_clear_melee_path(const World& world, const glm::vec3& origin, const glm::vec3& target) -> bool {
    const auto delta = target - origin;
    const auto max_distance = glm::length(delta);
    if (max_distance <= 1.0e-6F) {
        return true;
    }

    const auto direction = delta / max_distance;
    BlockCoord current {
        static_cast<int>(std::floor(origin.x)),
        static_cast<int>(std::floor(origin.y)),
        static_cast<int>(std::floor(origin.z)),
    };
    const BlockCoord target_cell {
        static_cast<int>(std::floor(target.x)),
        static_cast<int>(std::floor(target.y)),
        static_cast<int>(std::floor(target.z)),
    };
    if (current == target_cell) {
        return true;
    }

    const auto compute_step = [](float component) noexcept -> int {
        if (component > 0.0F) {
            return 1;
        }
        if (component < 0.0F) {
            return -1;
        }
        return 0;
    };

    const auto step_x = compute_step(direction.x);
    const auto step_y = compute_step(direction.y);
    const auto step_z = compute_step(direction.z);
    const auto infinity = std::numeric_limits<float>::infinity();
    const auto next_boundary = [](float origin_component, int current_cell, int step) noexcept -> float {
        if (step > 0) {
            return static_cast<float>(current_cell + 1) - origin_component;
        }
        return origin_component - static_cast<float>(current_cell);
    };

    float t_max_x = step_x == 0 ? infinity : next_boundary(origin.x, current.x, step_x) / std::abs(direction.x);
    float t_max_y = step_y == 0 ? infinity : next_boundary(origin.y, current.y, step_y) / std::abs(direction.y);
    float t_max_z = step_z == 0 ? infinity : next_boundary(origin.z, current.z, step_z) / std::abs(direction.z);

    const auto t_delta_x = step_x == 0 ? infinity : 1.0F / std::abs(direction.x);
    const auto t_delta_y = step_y == 0 ? infinity : 1.0F / std::abs(direction.y);
    const auto t_delta_z = step_z == 0 ? infinity : 1.0F / std::abs(direction.z);

    float travelled = 0.0F;
    while (travelled <= max_distance) {
        if (t_max_x <= t_max_y && t_max_x <= t_max_z) {
            current.x += step_x;
            travelled = t_max_x;
            t_max_x += t_delta_x;
        } else if (t_max_y <= t_max_z) {
            current.y += step_y;
            travelled = t_max_y;
            t_max_y += t_delta_y;
        } else {
            current.z += step_z;
            travelled = t_max_z;
            t_max_z += t_delta_z;
        }

        if (travelled > max_distance) {
            break;
        }
        if (current == target_cell) {
            return true;
        }
        if (is_block_collidable(world.get_block(current.x, current.y, current.z))) {
            return false;
        }
    }

    return true;
}

auto can_strike_player(const CreatureInstance& creature,
                       const World& world,
                       const glm::vec3& player_position,
                       float horizontal_distance,
                       float horizontal_distance_sq) -> bool {
    if (horizontal_distance_sq <= 1.0e-6F || horizontal_distance > kNightAttackDistance) {
        return false;
    }
    if (!shares_melee_height_layer(creature.position, player_position)) {
        return false;
    }

    // Je n'autorise le coup de melee que si le monstre et le joueur partagent
    // la meme couche verticale et qu'aucun bloc solide ne coupe la ligne directe.
    const auto attack_origin = creature.position + glm::vec3 {0.0F, kNightAttackSampleHeight, 0.0F};
    const auto player_target = player_position + glm::vec3 {0.0F, kNightAttackSampleHeight, 0.0F};
    return has_clear_melee_path(world, attack_origin, player_target);
}

auto tuning_for(CreatureSpecies species) noexcept -> SpeciesTuning {
    switch (species) {
    case CreatureSpecies::Pig:
        return {1.05F, 2.30F, 0.58F, 1.65F, 4.25F, 5.20F, 10.25F};
    case CreatureSpecies::Cow:
        return {0.92F, 1.95F, 0.52F, 1.45F, 4.80F, 5.80F, 11.25F};
    case CreatureSpecies::Villager:
        return {1.02F, 1.28F, 0.44F, 0.0F, 6.40F, 3.40F, 6.60F};
    case CreatureSpecies::Sheep:
    default:
        return {0.88F, 2.05F, 0.54F, 1.52F, 4.40F, 5.50F, 10.75F};
    }
}

auto is_resident_species(CreatureSpecies species) noexcept -> bool {
    return species == CreatureSpecies::Villager;
}

auto make_spawned_creature(const CreatureSpawnAnchor& anchor,
                           std::uint32_t seed,
                           const CreatureCycleState& cycle) -> CreatureInstance {
    CreatureInstance creature {};
    creature.anchor = anchor;
    creature.position = anchor.spawn_position;
    creature.yaw_radians = next_unit(seed) * kTwoPi - kPi;
    creature.wander_heading = creature.yaw_radians;
    creature.behavior_timer = 0.0F;
    creature.animation_time = next_unit(seed) * 5.0F;
    creature.nervous_intensity = 0.0F;
    creature.behavior_seed = hash_coords(anchor.ground_block.x * 3, anchor.ground_block.z * 7, seed ^ 0xA53C9E1BU);
    creature.appearance_seed = hash_coords(anchor.ground_block.x * 11, anchor.ground_block.z * 5, seed ^ 0x6C8E9CF5U);
    creature.attack_amount = 0.0F;

    if (is_resident_species(anchor.species)) {
        creature.phase = CreaturePhase::Day;
        creature.morph_factor = 0.0F;
        creature.behavior_state = CreatureBehaviorState::Idle;
        creature.motion_amount = 0.10F;
        creature.gaze_weight = 0.34F;
        creature.attack_cooldown = 0.0F;
    } else {
        creature.phase = cycle.phase;
        creature.morph_factor = cycle.morph_factor;
        creature.behavior_state = is_hostile_night(cycle) ? CreatureBehaviorState::Lurk : CreatureBehaviorState::Idle;
        creature.motion_amount = is_morph_visible(cycle) ? 0.18F : 0.10F;
        creature.gaze_weight = is_morph_visible(cycle) ? 0.48F : 0.16F;
        creature.attack_cooldown = next_unit(seed) * 0.35F;
    }

    return creature;
}

auto make_spawned_resident(const ResidentProfile& profile, const CreatureCycleState& cycle) -> CreatureInstance {
    const auto seed = profile.routine_seed ^ static_cast<std::uint32_t>(profile.role) * 2654435761U;
    auto creature = make_spawned_creature(profile.anchor, seed, cycle);
    creature.anchor = profile.anchor;
    creature.position = profile.home_position;
    creature.yaw_radians = settle_yaw_from_seed(seed);
    creature.wander_heading = creature.yaw_radians;
    creature.behavior_seed = hash_coords(
        profile.anchor.ground_block.x * 3,
        profile.anchor.ground_block.z * 7,
        seed ^ 0xA53C9E1BU);
    creature.appearance_seed = hash_coords(
        profile.anchor.ground_block.x * 11,
        profile.anchor.ground_block.z * 5,
        seed ^ 0x6C8E9CF5U);
    creature.phase = CreaturePhase::Day;
    creature.morph_factor = 0.0F;
    creature.behavior_state = CreatureBehaviorState::Idle;
    creature.motion_amount = 0.08F;
    creature.gaze_weight = resident_behavior_bias(profile.role) * 0.45F;
    creature.attack_cooldown = 0.0F;
    creature.nervous_intensity = 0.0F;
    creature.attack_amount = 0.0F;
    return creature;
}

auto is_spawn_column_clear(const World& world, int world_x, int ground_y, int world_z) -> bool {
    if (ground_y < kWorldMinY || ground_y > kWorldMaxY - 2) {
        return false;
    }
    if (!is_block_collidable(world.get_block(world_x, ground_y, world_z))) {
        return false;
    }
    return world.get_block(world_x, ground_y + 1, world_z) == to_block_id(BlockType::Air) &&
           world.get_block(world_x, ground_y + 2, world_z) == to_block_id(BlockType::Air);
}

auto count_tree_columns_nearby(const World& world, int world_x, int world_y, int world_z, int radius) -> int {
    int tree_columns = 0;
    for (int dz = -radius; dz <= radius; ++dz) {
        for (int dx = -radius; dx <= radius; ++dx) {
            bool found_tree = false;
            for (int dy = 0; dy <= 6; ++dy) {
                const auto block = world.get_block(world_x + dx, world_y + dy, world_z + dz);
                if (block == to_block_id(BlockType::Wood) || block == to_block_id(BlockType::Leaves)) {
                    found_tree = true;
                    break;
                }
            }
            tree_columns += found_tree ? 1 : 0;
        }
    }
    return tree_columns;
}

auto local_relief_range(const World& world, int world_x, int world_z, int radius) -> int {
    auto min_height = kWorldMaxY;
    auto max_height = kWorldMinY;
    bool found_any = false;

    for (int dz = -radius; dz <= radius; ++dz) {
        for (int dx = -radius; dx <= radius; ++dx) {
            const auto height = world.loaded_surface_height(world_x + dx, world_z + dz);
            if (!height.has_value()) {
                continue;
            }
            min_height = std::min(min_height, *height);
            max_height = std::max(max_height, *height);
            found_any = true;
        }
    }

    return found_any ? max_height - min_height : 0;
}

auto classify_spawn_species(const World& world, int world_x, int ground_y, int world_z) -> std::optional<CreatureSpecies> {
    const auto surface_block = world.get_block(world_x, ground_y, world_z);
    if (surface_block != to_block_id(BlockType::Grass)) {
        return std::nullopt;
    }

    const auto nearby_trees = count_tree_columns_nearby(world, world_x, ground_y + 1, world_z, 4);
    if (nearby_trees >= 3) {
        return CreatureSpecies::Pig;
    }

    const auto relief = local_relief_range(world, world_x, world_z, 2);
    if (ground_y >= 48 && relief >= 2) {
        return CreatureSpecies::Sheep;
    }

    return CreatureSpecies::Cow;
}

void pick_day_behavior(CreatureInstance& creature) {
    const auto choice = next_unit(creature.behavior_seed);
    if (choice < 0.32F) {
        creature.behavior_state = CreatureBehaviorState::Idle;
        creature.behavior_timer = 0.85F + next_unit(creature.behavior_seed) * 1.10F;
    } else if (choice < 0.76F) {
        creature.behavior_state = CreatureBehaviorState::Wander;
        creature.behavior_timer = 1.10F + next_unit(creature.behavior_seed) * 1.55F;
        creature.wander_heading = wrap_angle(creature.wander_heading + next_signed_unit(creature.behavior_seed) * 1.05F);
    } else {
        creature.behavior_state = CreatureBehaviorState::Sniff;
        creature.behavior_timer = 0.65F + next_unit(creature.behavior_seed) * 0.80F;
        creature.wander_heading = wrap_angle(creature.wander_heading + next_signed_unit(creature.behavior_seed) * 0.35F);
    }
}

void pick_resident_behavior(CreatureInstance& creature, float player_distance) {
    const auto choice = next_unit(creature.behavior_seed);
    if (player_distance < kResidentGreetingDistance && choice < 0.34F) {
        creature.behavior_state = CreatureBehaviorState::Stare;
        creature.behavior_timer = 0.55F + next_unit(creature.behavior_seed) * 1.10F;
    } else if (choice < 0.30F) {
        creature.behavior_state = CreatureBehaviorState::Idle;
        creature.behavior_timer = 0.90F + next_unit(creature.behavior_seed) * 1.25F;
    } else if (choice < 0.78F) {
        creature.behavior_state = CreatureBehaviorState::Wander;
        creature.behavior_timer = 1.20F + next_unit(creature.behavior_seed) * 1.80F;
        creature.wander_heading = wrap_angle(creature.wander_heading + next_signed_unit(creature.behavior_seed) * 0.72F);
    } else {
        creature.behavior_state = CreatureBehaviorState::Sniff;
        creature.behavior_timer = 0.65F + next_unit(creature.behavior_seed) * 0.85F;
        creature.wander_heading = wrap_angle(creature.wander_heading + next_signed_unit(creature.behavior_seed) * 0.28F);
    }
}

void pick_twilight_behavior(CreatureInstance& creature, float player_distance) {
    const auto choice = next_unit(creature.behavior_seed);
    if (player_distance < kNightDetectionDistance * 0.7F && choice < 0.34F) {
        creature.behavior_state = CreatureBehaviorState::Stare;
        creature.behavior_timer = 0.42F + next_unit(creature.behavior_seed) * 0.90F;
        return;
    }

    if (choice < 0.78F) {
        creature.behavior_state = CreatureBehaviorState::Lurk;
        creature.behavior_timer = 0.90F + next_unit(creature.behavior_seed) * 1.45F;
        creature.wander_heading = wrap_angle(creature.wander_heading + next_signed_unit(creature.behavior_seed) * 1.45F);
        return;
    }

    creature.behavior_state = CreatureBehaviorState::Twitch;
    creature.behavior_timer = 0.24F + next_unit(creature.behavior_seed) * 0.38F;
    creature.wander_heading = wrap_angle(creature.wander_heading + next_signed_unit(creature.behavior_seed) * 0.70F);
}

auto try_move_grounded(CreatureInstance& creature,
                       const World& world,
                       const glm::vec2& desired_delta,
                       float roam_radius,
                       std::optional<int> preferred_floor_y) -> bool {
    if (glm::dot(desired_delta, desired_delta) <= 1.0e-6F) {
        return false;
    }

    const glm::vec2 current {creature.position.x, creature.position.z};
    const glm::vec2 home {creature.anchor.spawn_position.x, creature.anchor.spawn_position.z};
    auto candidate = current + desired_delta;
    auto home_offset = candidate - home;
    const auto roam_limit_sq = roam_radius * roam_radius;
    if (glm::dot(home_offset, home_offset) > roam_limit_sq) {
        const auto to_home = home - current;
        if (glm::dot(to_home, to_home) <= 1.0e-6F) {
            return false;
        }
        candidate = current + glm::normalize(to_home) * glm::length(desired_delta);
        home_offset = candidate - home;
        if (glm::dot(home_offset, home_offset) > roam_limit_sq) {
            return false;
        }
    }

    const auto world_x = static_cast<int>(std::floor(candidate.x));
    const auto world_z = static_cast<int>(std::floor(candidate.y));
    const auto target_y = resolve_grounded_target_y(world, world_x, world_z, creature.position.y, preferred_floor_y);
    if (!target_y.has_value()) {
        return false;
    }

    creature.position = glm::vec3 {candidate.x, *target_y, candidate.y};
    return true;
}

auto try_move_grounded_with_steering(CreatureInstance& creature,
                                     const World& world,
                                     float base_heading,
                                     float step_distance,
                                     float roam_radius,
                                     bool aggressive,
                                     std::optional<int> preferred_floor_y) -> SteeringMoveResult {
    if (step_distance <= 1.0e-6F) {
        return {};
    }

    if (aggressive) {
        for (const auto heading_offset : std::array<float, 9> {0.0F, 0.28F, -0.28F, 0.56F, -0.56F, 0.92F, -0.92F, 1.28F, -1.28F}) {
            const auto heading = wrap_angle(base_heading + heading_offset);
            if (try_move_grounded(
                    creature,
                    world,
                    direction_from_yaw(heading) * step_distance,
                    roam_radius,
                    preferred_floor_y)) {
                return {true, std::abs(heading_offset) > 0.05F, heading};
            }
        }
        return {};
    }

    for (const auto heading_offset : std::array<float, 5> {0.0F, 0.34F, -0.34F, 0.68F, -0.68F}) {
        const auto heading = wrap_angle(base_heading + heading_offset);
        if (try_move_grounded(
                creature,
                world,
                direction_from_yaw(heading) * step_distance,
                roam_radius,
                preferred_floor_y)) {
            return {true, std::abs(heading_offset) > 0.05F, heading};
        }
    }
    return {};
}

auto is_morph_visible(const CreatureCycleState& cycle) noexcept -> bool {
    return cycle.phase == CreaturePhase::Night ||
           cycle.phase == CreaturePhase::DuskMorph ||
           cycle.phase == CreaturePhase::DawnRecover;
}

auto is_hostile_night(const CreatureCycleState& cycle) noexcept -> bool {
    return cycle.phase == CreaturePhase::Night;
}

auto smoothing_factor(float dt, float response_rate) noexcept -> float {
    if (dt <= 0.0F) {
        return 1.0F;
    }
    return 1.0F - std::exp(-dt * response_rate);
}

auto resolve_resident_anchor(const World& world, const CreatureSpawnAnchor& anchor) -> std::optional<CreatureSpawnAnchor> {
    const auto floor_y = resolve_resident_floor_y(world, anchor);
    if (!floor_y.has_value()) {
        return std::nullopt;
    }

    auto resolved = anchor;
    resolved.ground_block.y = *floor_y;
    resolved.spawn_position.y = static_cast<float>(*floor_y) + kGroundSnapOffset;
    return resolved;
}

auto same_resident_slot(const CreatureSpawnAnchor& lhs, const CreatureSpawnAnchor& rhs) noexcept -> bool {
    return lhs.chunk == rhs.chunk &&
           lhs.ground_block.x == rhs.ground_block.x &&
           lhs.ground_block.z == rhs.ground_block.z &&
           lhs.species == rhs.species;
}

} // namespace

void CreatureSystem::update(float dt,
                            const World& world,
                            const glm::vec3& player_position,
                            const EnvironmentState& environment,
                            const CreatureCycleState& cycle) {
    audit_stats_ = {};
    attacks_.clear();
    sync_active_creatures(world, player_position, cycle);

    for (auto& creature : creatures_) {
        update_creature(creature, dt, world, player_position, environment, cycle);
    }

    rebuild_render_instances(environment);
    audit_stats_.attacks = attacks_.size();
    audit_stats_.active_creatures = creatures_.size();
}

auto CreatureSystem::spawn_anchor_for_chunk(const World& world, const ChunkCoord& coord) const
    -> std::optional<CreatureSpawnAnchor> {
    return compute_spawn_anchor(world, coord);
}

auto CreatureSystem::active_creatures() const noexcept -> std::span<const CreatureInstance> {
    return creatures_;
}

auto CreatureSystem::render_instances() const noexcept -> std::span<const CreatureRenderInstance> {
    return render_instances_;
}

auto CreatureSystem::recent_attacks() const noexcept -> std::span<const CreatureAttackEvent> {
    return attacks_;
}

auto CreatureSystem::consume_audit_stats() noexcept -> CreatureAuditStats {
    const auto stats = audit_stats_;
    audit_stats_ = {};
    return stats;
}

void CreatureSystem::set_settlement_residents(std::vector<CreatureSpawnAnchor> residents) {
    settlement_residents_ = std::move(residents);
    settlement_residents_.erase(
        std::remove_if(settlement_residents_.begin(), settlement_residents_.end(), [](const CreatureSpawnAnchor& anchor) {
            return !is_resident_species(anchor.species);
        }),
        settlement_residents_.end());

    resident_profiles_.clear();
    resident_profiles_.reserve(settlement_residents_.size());
    settlement_center_ = {0.0F, 0.0F, 0.0F};

    std::vector<glm::vec3> home_positions {};
    home_positions.reserve(settlement_residents_.size());
    for (const auto& resident : settlement_residents_) {
        const auto routine_seed = hash_coords(
            resident.ground_block.x,
            resident.ground_block.z,
            static_cast<std::uint32_t>(resident.ground_block.y * 97 + resident.chunk.x * 17 + resident.chunk.z * 23));
        auto profile = settle_resident_profile(resident, glm::vec3 {0.0F, 0.0F, 0.0F}, routine_seed);
        resident_profiles_.push_back(profile);
        home_positions.push_back(profile.home_position);
    }

    if (home_positions.empty()) {
        settlement_center_ = {0.0F, 0.0F, 0.0F};
        return;
    }

    for (const auto& position : home_positions) {
        settlement_center_ += position;
    }
    settlement_center_ /= static_cast<float>(home_positions.size());

    for (auto& profile : resident_profiles_) {
        const auto home_xy = glm::vec2 {profile.home_position.x, profile.home_position.z};
        const auto center_xy = glm::vec2 {settlement_center_.x, settlement_center_.z};
        const auto to_center = normalize_or_cardinal(center_xy - home_xy, profile.routine_seed);
        const auto away_from_center = normalize_or_cardinal(home_xy - center_xy, profile.routine_seed + 13U);
        const auto lateral = perpendicular_left(to_center);
        const auto garden_lateral = perpendicular_left(away_from_center);
        const auto social_distance = 1.8F + static_cast<float>((profile.routine_seed >> 3U) % 4U) * 0.6F;
        const auto work_distance = 1.5F + static_cast<float>((profile.routine_seed >> 5U) % 3U) * 0.55F;
        const auto garden_distance = 1.2F + static_cast<float>((profile.routine_seed >> 7U) % 3U) * 0.45F;

        if (profile.anchor.patrol_point_count <= 1) {
            profile.interior_position =
                profile.home_position + glm::vec3 {to_center.x * 0.9F + lateral.x * 0.25F, 0.0F, to_center.y * 0.9F + lateral.y * 0.25F};
        }
        if (profile.anchor.patrol_point_count <= 2) {
            profile.social_position =
                profile.home_position + glm::vec3 {to_center.x * social_distance, 0.0F, to_center.y * social_distance};
        }
        if (profile.anchor.patrol_point_count <= 3) {
            profile.work_position =
                profile.home_position + glm::vec3 {away_from_center.x * work_distance, 0.0F, away_from_center.y * work_distance};
        }
        if (profile.anchor.patrol_point_count == 0) {
            profile.garden_position = profile.home_position + glm::vec3 {lateral.x * garden_distance + garden_lateral.x * 0.4F,
                                                                         0.0F,
                                                                         lateral.y * garden_distance + garden_lateral.y * 0.4F};
        }

        profile.interior_position.y = profile.home_position.y;
        profile.social_position.y = profile.home_position.y;
        profile.work_position.y = profile.home_position.y;
        profile.garden_position.y = profile.home_position.y;

        const auto farthest_target = std::max({
            6.0F,
            std::sqrt(horizontal_distance_squared(profile.home_position, profile.interior_position)),
            std::sqrt(horizontal_distance_squared(profile.home_position, profile.social_position)),
            std::sqrt(horizontal_distance_squared(profile.home_position, profile.work_position)),
            std::sqrt(horizontal_distance_squared(profile.home_position, profile.garden_position)),
        });
        profile.roam_radius = std::max(profile.roam_radius, farthest_target + 3.0F);
    }
}

void CreatureSystem::load_creatures(const std::vector<CreatureInstance>& creatures, const EnvironmentState& environment) {
    creatures_ = creatures;
    attacks_.clear();
    rebuild_render_instances(environment);
    audit_stats_ = {};
    audit_stats_.active_creatures = creatures_.size();
}

void CreatureSystem::clear() noexcept {
    creatures_.clear();
    render_instances_.clear();
    attacks_.clear();
    settlement_residents_.clear();
    resident_profiles_.clear();
    settlement_center_ = {0.0F, 0.0F, 0.0F};
    audit_stats_ = {};
}

void CreatureSystem::sync_active_creatures(const World& world,
                                           const glm::vec3& player_position,
                                           const CreatureCycleState& cycle) {
    const auto center = world.world_to_chunk(
        static_cast<int>(std::floor(player_position.x)),
        static_cast<int>(std::floor(player_position.z)));

    for (auto iterator = creatures_.begin(); iterator != creatures_.end();) {
        if (!is_chunk_within_radius(center, iterator->anchor.chunk, kCreatureKeepAliveRadiusChunks) ||
            world.find_chunk(iterator->anchor.chunk) == nullptr) {
            ++audit_stats_.despawned;
            iterator = creatures_.erase(iterator);
            continue;
        }

        if (is_resident_species(iterator->anchor.species)) {
            const auto* profile = find_resident_profile(iterator->anchor);
            if (profile == nullptr) {
                ++audit_stats_.despawned;
                iterator = creatures_.erase(iterator);
                continue;
            }
            if (const auto resolved_anchor = resolve_resident_anchor(world, profile->anchor); resolved_anchor.has_value()) {
                iterator->anchor = *resolved_anchor;
                const auto home_position = resident_floor_position(*resolved_anchor);
                const auto horizontal_drift = horizontal_distance_squared(iterator->position, home_position);
                if (std::abs(iterator->position.y - home_position.y) > kResidentHomeSnapThreshold ||
                    horizontal_drift > profile->roam_radius * profile->roam_radius) {
                    iterator->position = home_position;
                    iterator->yaw_radians = settle_yaw_from_seed(profile->routine_seed);
                    iterator->wander_heading = iterator->yaw_radians;
                    iterator->behavior_state = CreatureBehaviorState::Idle;
                    iterator->behavior_timer = 0.0F;
                }
            } else {
                ++audit_stats_.despawned;
                iterator = creatures_.erase(iterator);
                continue;
            }
            ++iterator;
            continue;
        }

        const auto refreshed_anchor = compute_spawn_anchor(world, iterator->anchor.chunk);
        if (!refreshed_anchor.has_value() || *refreshed_anchor != iterator->anchor) {
            ++audit_stats_.despawned;
            iterator = creatures_.erase(iterator);
            continue;
        }

        ++iterator;
    }

    std::vector<ResidentProfile> resident_candidates {};
    resident_candidates.reserve(resident_profiles_.size());
    for (const auto& profile : resident_profiles_) {
        if (!is_chunk_within_radius(center, profile.anchor.chunk, kCreatureActivationRadiusChunks) ||
            world.find_chunk(profile.anchor.chunk) == nullptr ||
            find_resident_creature(profile.anchor) != nullptr) {
            continue;
        }

        const auto resolved_anchor = resolve_resident_anchor(world, profile.anchor);
        if (!resolved_anchor.has_value()) {
            continue;
        }

        auto resolved_profile = profile;
        resolved_profile.anchor = *resolved_anchor;
        resolved_profile.home_position = resident_floor_position(*resolved_anchor);
        resolved_profile.interior_position.y = resolved_profile.home_position.y;
        resolved_profile.work_position.y = resolved_profile.home_position.y;
        resolved_profile.social_position.y = resolved_profile.home_position.y;
        resolved_profile.garden_position.y = resolved_profile.home_position.y;
        resident_candidates.push_back(resolved_profile);
    }

    auto wildlife_begin = std::stable_partition(creatures_.begin(), creatures_.end(), [](const CreatureInstance& creature) {
        return is_resident_species(creature.anchor.species);
    });
    const auto resident_count = static_cast<std::size_t>(std::distance(creatures_.begin(), wildlife_begin));
    const auto wildlife_count = static_cast<std::size_t>(std::distance(wildlife_begin, creatures_.end()));
    const auto reserved_resident_count = resident_count + resident_candidates.size();
    const auto wildlife_capacity =
        reserved_resident_count >= kCreatureMaxActiveCount ? 0U : kCreatureMaxActiveCount - reserved_resident_count;
    if (wildlife_count > wildlife_capacity) {
        std::sort(wildlife_begin, creatures_.end(), [&](const CreatureInstance& lhs, const CreatureInstance& rhs) {
            const auto lhs_distance = horizontal_distance_squared(lhs.position, player_position);
            const auto rhs_distance = horizontal_distance_squared(rhs.position, player_position);
            return lhs_distance < rhs_distance;
        });
        const auto trimmed_begin = wildlife_begin + static_cast<std::ptrdiff_t>(wildlife_capacity);
        audit_stats_.despawned += static_cast<std::size_t>(std::distance(trimmed_begin, creatures_.end()));
        creatures_.erase(trimmed_begin, creatures_.end());
    }

    for (const auto& resident : resident_candidates) {
        if (creatures_.size() >= kCreatureMaxActiveCount) {
            break;
        }

        creatures_.push_back(make_spawned_resident(resident, cycle));
        ++audit_stats_.spawned;
    }

    if (creatures_.size() >= kCreatureMaxActiveCount) {
        return;
    }

    std::vector<SpawnCandidate> candidates;
    candidates.reserve(static_cast<std::size_t>((kCreatureActivationRadiusChunks * 2 + 1) * (kCreatureActivationRadiusChunks * 2 + 1)));

    for (const auto& [coord, record] : world.chunk_records()) {
        (void)record;
        if (!is_chunk_within_radius(center, coord, kCreatureActivationRadiusChunks) || find_creature(coord) != nullptr) {
            continue;
        }
        if (std::any_of(settlement_residents_.begin(), settlement_residents_.end(), [&](const CreatureSpawnAnchor& resident) {
                return resident.chunk == coord;
            })) {
            continue;
        }
        candidates.push_back({coord, chunk_distance_squared_to_player(coord, player_position)});
    }

    std::sort(candidates.begin(), candidates.end(), [](const SpawnCandidate& lhs, const SpawnCandidate& rhs) {
        return lhs.distance_squared < rhs.distance_squared;
    });

    for (const auto& candidate : candidates) {
        if (creatures_.size() >= kCreatureMaxActiveCount) {
            break;
        }

        const auto anchor = compute_spawn_anchor(world, candidate.coord);
        if (!anchor.has_value()) {
            continue;
        }

        auto seed = hash_coords(anchor->ground_block.x, anchor->ground_block.z, static_cast<std::uint32_t>(world.seed()));
        creatures_.push_back(make_spawned_creature(*anchor, seed, cycle));
        ++audit_stats_.spawned;
    }
}

void CreatureSystem::update_creature(CreatureInstance& creature,
                                     float dt,
                                     const World& world,
                                     const glm::vec3& player_position,
                                     const EnvironmentState& environment,
                                     const CreatureCycleState& cycle) {
    if (is_resident_species(creature.anchor.species)) {
        if (const auto* profile = find_resident_profile(creature.anchor); profile != nullptr) {
            update_resident_creature(creature, *profile, dt, world, player_position, environment);
            return;
        }
    }

    const auto tuning = tuning_for(creature.anchor.species);
    const auto resident_species = is_resident_species(creature.anchor.species);
    creature.phase = resident_species ? CreaturePhase::Day : cycle.phase;
    creature.morph_factor = resident_species ? 0.0F : cycle.morph_factor;
    creature.animation_time += std::max(dt, 0.0F);
    creature.behavior_timer -= dt;
    creature.attack_cooldown = std::max(0.0F, creature.attack_cooldown - std::max(dt, 0.0F));

    const auto morph_visible = resident_species ? false : is_morph_visible(cycle);
    const auto hostile_night = resident_species ? false : is_hostile_night(cycle);
    const auto dawn_recover = !resident_species && cycle.phase == CreaturePhase::DawnRecover;
    const auto to_player = glm::vec2 {
        player_position.x - creature.position.x,
        player_position.z - creature.position.z,
    };
    const auto player_distance_sq = glm::dot(to_player, to_player);
    const auto player_distance = std::sqrt(std::max(player_distance_sq, 0.0F));
    const auto detection_distance = resident_species ? kResidentGreetingDistance : kNightDetectionDistance;
    const auto player_distance_factor = glm::clamp(1.0F - player_distance / detection_distance, 0.0F, 1.0F);
    const auto can_attack_player =
        resident_species ? false : can_strike_player(creature, world, player_position, player_distance, player_distance_sq);
    const auto same_melee_layer = shares_melee_height_layer(creature.position, player_position);
    bool close_melee_path_clear = false;
    if (same_melee_layer &&
        player_distance_sq > 1.0e-6F &&
        player_distance <= kNightAttackDistance + 0.55F) {
        const auto attack_origin = creature.position + glm::vec3 {0.0F, kNightAttackSampleHeight, 0.0F};
        const auto player_target = player_position + glm::vec3 {0.0F, kNightAttackSampleHeight, 0.0F};
        close_melee_path_clear = has_clear_melee_path(world, attack_origin, player_target);
    }
    const auto player_detected = player_distance <= detection_distance && player_distance_sq > 1.0e-6F;
    const auto position_before_move = creature.position;

    glm::vec2 desired_move {0.0F};
    auto desired_yaw = creature.yaw_radians;
    auto roam_radius = tuning.day_roam_radius;
    float target_motion_amount = morph_visible ? 0.22F : 0.08F;
    float target_gaze_weight = morph_visible ? 0.52F : 0.18F;
    float target_attack_amount = 0.0F;

    if (resident_species) {
        roam_radius = tuning.day_roam_radius;
        creature.attack_cooldown = 0.0F;
        creature.nervous_intensity =
            glm::clamp(environment.daylight_factor * 0.08F + player_distance_factor * 0.18F, 0.0F, 0.30F);

        if (player_distance < kResidentPersonalSpace && player_distance_sq > 1.0e-6F) {
            const auto give_space_direction = -glm::normalize(to_player);
            creature.behavior_state = CreatureBehaviorState::Wander;
            creature.behavior_timer = 0.28F;
            desired_move = give_space_direction * tuning.day_speed * dt * 0.42F;
            desired_yaw = yaw_from_direction(give_space_direction);
            target_motion_amount = 0.42F;
            target_gaze_weight = 0.18F;
        } else {
            if (creature.behavior_timer <= 0.0F ||
                creature.behavior_state == CreatureBehaviorState::Flee ||
                creature.behavior_state == CreatureBehaviorState::Chase ||
                creature.behavior_state == CreatureBehaviorState::Strike) {
                pick_resident_behavior(creature, player_distance);
            }

            switch (creature.behavior_state) {
            case CreatureBehaviorState::Wander:
                desired_move = direction_from_yaw(creature.wander_heading) * tuning.day_speed * dt * 0.72F;
                desired_yaw = creature.wander_heading;
                target_motion_amount = 0.46F;
                target_gaze_weight = 0.20F;
                break;
            case CreatureBehaviorState::Sniff:
                desired_yaw = wrap_angle(creature.wander_heading + std::sin(creature.animation_time * 4.2F) * 0.12F);
                target_motion_amount = 0.14F;
                target_gaze_weight = 0.38F;
                break;
            case CreatureBehaviorState::Stare:
                if (player_detected) {
                    desired_yaw = yaw_from_direction(glm::normalize(to_player));
                } else {
                    desired_yaw = creature.wander_heading;
                }
                target_motion_amount = 0.06F;
                target_gaze_weight = 0.78F;
                break;
            case CreatureBehaviorState::Idle:
            default:
                desired_yaw = creature.wander_heading;
                target_motion_amount = 0.04F;
                target_gaze_weight = 0.26F;
                break;
            }
        }
    } else if (!morph_visible) {
        creature.nervous_intensity =
            glm::clamp(player_distance < kPlayerShyDistance ? 0.30F : environment.daylight_factor * 0.06F, 0.0F, 0.36F);

        if (player_distance < kPlayerShyDistance && player_distance_sq > 1.0e-6F) {
            const auto flee_direction = -glm::normalize(to_player);
            creature.behavior_state = CreatureBehaviorState::Flee;
            creature.behavior_timer = 0.32F;
            desired_move = flee_direction * tuning.flee_speed * dt;
            desired_yaw = yaw_from_direction(flee_direction);
            target_motion_amount = 1.0F;
            target_gaze_weight = 0.10F;
        } else {
            if (creature.behavior_timer <= 0.0F || creature.behavior_state == CreatureBehaviorState::Flee) {
                pick_day_behavior(creature);
            }

            switch (creature.behavior_state) {
            case CreatureBehaviorState::Wander:
                desired_move = direction_from_yaw(creature.wander_heading) * tuning.day_speed * dt;
                desired_yaw = creature.wander_heading;
                target_motion_amount = 0.56F;
                target_gaze_weight = 0.20F;
                break;
            case CreatureBehaviorState::Sniff:
                desired_yaw = wrap_angle(creature.wander_heading + std::sin(creature.animation_time * 4.8F) * 0.18F);
                target_motion_amount = 0.20F;
                target_gaze_weight = 0.50F;
                break;
            case CreatureBehaviorState::Idle:
            default:
                desired_yaw = creature.wander_heading;
                break;
            }
        }
    } else if (hostile_night) {
        roam_radius = tuning.chase_radius;
        creature.nervous_intensity = glm::clamp(0.48F + cycle.morph_factor * 0.38F + player_distance_factor * 0.28F, 0.0F, 1.0F);
        const auto chase_memory_active =
            creature.behavior_state == CreatureBehaviorState::Chase && creature.behavior_timer > 0.0F;
        const auto close_but_blocked =
            player_distance_sq > 1.0e-6F &&
            player_distance <= kNightAttackDistance + 0.55F &&
            (!same_melee_layer || !close_melee_path_clear);

        if (can_attack_player) {
            const auto attack_direction = glm::normalize(to_player);
            desired_yaw = yaw_from_direction(attack_direction);
            creature.wander_heading = desired_yaw;
            creature.behavior_state = CreatureBehaviorState::Strike;
            creature.behavior_timer = std::max(creature.behavior_timer, 0.24F);
            target_motion_amount = 0.74F;
            target_gaze_weight = 0.96F;
            target_attack_amount = 1.0F;
            if (creature.attack_cooldown <= 0.0F) {
                creature.attack_cooldown = kNightStrikeCooldown;
                creature.behavior_timer = 0.18F;
                attacks_.push_back({
                    creature.anchor.species,
                    creature.position + glm::vec3 {attack_direction.x * 0.45F, 0.65F, attack_direction.y * 0.45F},
                    kZombieDamage,
                });
            }
        } else if (close_but_blocked) {
            creature.behavior_state = CreatureBehaviorState::Stare;
            desired_yaw = yaw_from_direction(glm::normalize(to_player));
            creature.behavior_timer = std::max(creature.behavior_timer, 0.22F);
            target_motion_amount = 0.16F + cycle.morph_factor * 0.06F;
            target_gaze_weight = 0.94F;
            target_attack_amount = 0.24F;
        } else if (player_detected) {
            const auto chase_direction = glm::normalize(to_player);
            creature.behavior_state = CreatureBehaviorState::Chase;
            creature.behavior_timer = kNightChasePersistenceSeconds;
            creature.wander_heading = yaw_from_direction(chase_direction);
            desired_move = chase_direction * tuning.chase_speed * dt;
            desired_yaw = creature.wander_heading;
            target_motion_amount = 0.86F;
            target_gaze_weight = 0.90F;
            target_attack_amount = 0.44F;
        } else if (chase_memory_active) {
            creature.behavior_state = CreatureBehaviorState::Chase;
            desired_yaw = creature.wander_heading;
            desired_move = direction_from_yaw(creature.wander_heading) * tuning.chase_speed * dt * 0.88F;
            target_motion_amount = 0.78F;
            target_gaze_weight = 0.84F;
            target_attack_amount = 0.30F;
        } else {
            if (creature.behavior_timer <= 0.0F ||
                creature.behavior_state == CreatureBehaviorState::Chase ||
                creature.behavior_state == CreatureBehaviorState::Strike) {
                pick_twilight_behavior(creature, player_distance);
            }

            roam_radius = tuning.night_roam_radius;
            switch (creature.behavior_state) {
            case CreatureBehaviorState::Lurk:
                desired_move = direction_from_yaw(creature.wander_heading) * tuning.lurk_speed * dt;
                desired_yaw = creature.wander_heading;
                target_motion_amount = 0.38F + cycle.morph_factor * 0.20F;
                target_gaze_weight = 0.58F + player_distance_factor * 0.14F;
                target_attack_amount = 0.16F;
                break;
            case CreatureBehaviorState::Stare:
                if (player_distance_sq > 1.0e-6F) {
                    desired_yaw = yaw_from_direction(glm::normalize(to_player));
                }
                target_motion_amount = 0.10F + cycle.morph_factor * 0.05F;
                target_gaze_weight = 0.88F;
                target_attack_amount = 0.08F;
                break;
            case CreatureBehaviorState::Twitch:
                desired_yaw = wrap_angle(
                    creature.wander_heading + std::sin(creature.animation_time * 18.0F + creature.nervous_intensity * 2.0F) * 0.75F);
                target_motion_amount = 0.24F + cycle.morph_factor * 0.12F;
                target_gaze_weight = 0.76F;
                target_attack_amount = 0.28F;
                break;
            default:
                desired_yaw = creature.wander_heading;
                target_attack_amount = 0.12F;
                break;
            }
        }
    } else {
        creature.nervous_intensity = glm::clamp(0.34F + cycle.morph_factor * 0.32F + player_distance_factor * 0.20F, 0.0F, 0.92F);

        if (creature.behavior_timer <= 0.0F ||
            creature.behavior_state == CreatureBehaviorState::Chase ||
            creature.behavior_state == CreatureBehaviorState::Strike) {
            pick_twilight_behavior(creature, player_distance);
        }

        roam_radius = tuning.night_roam_radius;
        switch (creature.behavior_state) {
        case CreatureBehaviorState::Lurk:
            desired_move = direction_from_yaw(creature.wander_heading) * tuning.lurk_speed * dt;
            desired_yaw = creature.wander_heading;
            target_motion_amount = 0.32F + cycle.morph_factor * 0.18F;
            target_gaze_weight = 0.52F + player_distance_factor * 0.12F;
            target_attack_amount = dawn_recover ? 0.02F : 0.10F;
            break;
        case CreatureBehaviorState::Stare:
            if (player_distance_sq > 1.0e-6F) {
                desired_yaw = yaw_from_direction(glm::normalize(to_player));
            }
            target_motion_amount = 0.08F + cycle.morph_factor * 0.05F;
            target_gaze_weight = 0.84F;
            target_attack_amount = dawn_recover ? 0.0F : 0.06F;
            break;
        case CreatureBehaviorState::Twitch:
            desired_yaw = wrap_angle(
                creature.wander_heading + std::sin(creature.animation_time * 17.0F + creature.nervous_intensity * 1.7F) * 0.72F);
            target_motion_amount = 0.22F + cycle.morph_factor * 0.10F;
            target_gaze_weight = 0.70F;
            target_attack_amount = dawn_recover ? 0.05F : 0.22F;
            break;
        default:
            desired_yaw = creature.wander_heading;
            break;
        }
    }

    const auto home_offset = glm::vec2 {
        creature.anchor.spawn_position.x - creature.position.x,
        creature.anchor.spawn_position.z - creature.position.z,
    };
    if (glm::dot(home_offset, home_offset) > roam_radius * roam_radius * 0.7F) {
        const auto home_direction = glm::normalize(home_offset);
        desired_move += home_direction * dt * (morph_visible ? tuning.lurk_speed : tuning.day_speed);
        if (glm::dot(desired_move, desired_move) > 1.0e-6F) {
            desired_yaw = yaw_from_direction(glm::normalize(desired_move));
        }
    }

    const auto turn_speed = hostile_night ? 8.8F : (morph_visible ? 7.6F : 5.5F);
    const auto rotated_yaw = rotate_towards(creature.yaw_radians, desired_yaw, turn_speed * std::max(dt, 0.0F));
    auto resolved_yaw = rotated_yaw;

    const auto desired_distance = glm::length(desired_move);
    bool attempted_move = false;
    bool moved = false;
    bool steering_diverted = false;
    if (desired_distance > 1.0e-6F) {
        const auto desired_direction = glm::normalize(desired_move);
        const auto facing_alignment =
            glm::clamp(glm::dot(direction_from_yaw(rotated_yaw), desired_direction), 0.0F, 1.0F);
        const auto forward_factor =
            creature.behavior_state == CreatureBehaviorState::Chase ?
                glm::mix(0.42F, 1.0F, facing_alignment) :
            (creature.behavior_state == CreatureBehaviorState::Flee ?
                glm::mix(0.30F, 1.0F, facing_alignment) :
                glm::smoothstep(0.08F, 0.82F, facing_alignment));
        const auto travel_distance = desired_distance * forward_factor;
        if (travel_distance > 1.0e-6F) {
            attempted_move = true;
            const auto steering_result = try_move_grounded_with_steering(
                creature,
                world,
                rotated_yaw,
                travel_distance,
                roam_radius,
                creature.behavior_state == CreatureBehaviorState::Chase ||
                    creature.behavior_state == CreatureBehaviorState::Flee);
            moved = steering_result.moved;
            steering_diverted = steering_result.diverted;
        }
    }

    const glm::vec2 travelled_horizontal {
        creature.position.x - position_before_move.x,
        creature.position.z - position_before_move.z,
    };
    if (glm::dot(travelled_horizontal, travelled_horizontal) > 1.0e-6F) {
        resolved_yaw = yaw_from_direction(glm::normalize(travelled_horizontal));
        if (creature.behavior_state == CreatureBehaviorState::Wander ||
            creature.behavior_state == CreatureBehaviorState::Lurk ||
            creature.behavior_state == CreatureBehaviorState::Flee) {
            creature.wander_heading = resolved_yaw;
        } else if (creature.behavior_state == CreatureBehaviorState::Chase) {
            if (player_detected) {
                creature.wander_heading = steering_diverted ? resolved_yaw : desired_yaw;
            } else if (steering_diverted) {
                creature.wander_heading = resolved_yaw;
            }
        }
    } else if (attempted_move && !moved &&
               (creature.behavior_state == CreatureBehaviorState::Wander ||
                creature.behavior_state == CreatureBehaviorState::Lurk ||
                creature.behavior_state == CreatureBehaviorState::Flee ||
                creature.behavior_state == CreatureBehaviorState::Chase)) {
        creature.wander_heading = wrap_angle(creature.wander_heading + kPi * 0.75F);
    }
    creature.yaw_radians = resolved_yaw;

    if (dt > 0.0F) {
        const auto reference_speed =
            creature.behavior_state == CreatureBehaviorState::Chase ? tuning.chase_speed :
            (morph_visible ? tuning.lurk_speed : tuning.day_speed);
        const auto reference_distance = std::max(reference_speed * dt, 0.001F);
        const auto realised_motion = glm::clamp(glm::length(travelled_horizontal) / reference_distance, 0.0F, 1.0F);
        target_motion_amount = std::max(target_motion_amount, realised_motion);
    }

    const auto response = smoothing_factor(dt, hostile_night ? 11.5F : (morph_visible ? 8.0F : 6.0F));
    creature.motion_amount = glm::mix(creature.motion_amount, glm::clamp(target_motion_amount, 0.0F, 1.0F), response);
    creature.gaze_weight = glm::mix(creature.gaze_weight, glm::clamp(target_gaze_weight, 0.0F, 1.0F), response);
    creature.attack_amount = glm::mix(creature.attack_amount, glm::clamp(target_attack_amount, 0.0F, 1.0F), response);

    if (hostile_night && creature.behavior_state == CreatureBehaviorState::Strike) {
        creature.attack_amount = std::max(creature.attack_amount, 0.64F);
    } else if (dawn_recover) {
        creature.attack_amount = std::min(creature.attack_amount, kDawnAttackVisualCap);
    }
}

void CreatureSystem::rebuild_render_instances(const EnvironmentState& environment) {
    render_instances_.clear();
    render_instances_.reserve(creatures_.size());

    for (const auto& creature : creatures_) {
        render_instances_.push_back({
            creature.anchor.species,
            creature.position,
            creature.yaw_radians,
            creature.animation_time,
            creature.morph_factor,
            environment.daylight_factor,
            glm::clamp(creature.nervous_intensity, 0.0F, 1.0F),
            creature.appearance_seed,
            creature.behavior_state,
            creature.phase,
            glm::clamp(creature.motion_amount, 0.0F, 1.0F),
            glm::clamp(creature.gaze_weight, 0.0F, 1.0F),
            glm::clamp(creature.attack_amount, 0.0F, 1.0F),
        });
    }
}

auto CreatureSystem::compute_spawn_anchor(const World& world, const ChunkCoord& coord) const
    -> std::optional<CreatureSpawnAnchor> {
    if (world.find_chunk(coord) == nullptr) {
        return std::nullopt;
    }

    auto seed = hash_coords(coord.x, coord.z, static_cast<std::uint32_t>(world.seed()));
    for (int candidate_index = 0; candidate_index < kSpawnCandidateCount; ++candidate_index) {
        const auto local_x = 1 + static_cast<int>(advance_seed(seed) % static_cast<std::uint32_t>(kChunkSizeX - 2));
        const auto local_z = 1 + static_cast<int>(advance_seed(seed) % static_cast<std::uint32_t>(kChunkSizeZ - 2));
        const auto world_x = coord.x * kChunkSizeX + local_x;
        const auto world_z = coord.z * kChunkSizeZ + local_z;
        const auto ground_y = world.loaded_surface_height(world_x, world_z);
        if (!ground_y.has_value() || !is_spawn_column_clear(world, world_x, *ground_y, world_z)) {
            continue;
        }

        const auto species = classify_spawn_species(world, world_x, *ground_y, world_z);
        if (!species.has_value()) {
            continue;
        }

        return CreatureSpawnAnchor {
            coord,
            {world_x, *ground_y, world_z},
            {static_cast<float>(world_x) + 0.5F, static_cast<float>(*ground_y) + kGroundSnapOffset, static_cast<float>(world_z) + 0.5F},
            *species,
        };
    }

    return std::nullopt;
}

auto CreatureSystem::find_creature(const ChunkCoord& coord) -> CreatureInstance* {
    const auto iterator = std::find_if(creatures_.begin(), creatures_.end(), [&](const CreatureInstance& creature) {
        return creature.anchor.chunk == coord;
    });
    return iterator == creatures_.end() ? nullptr : &(*iterator);
}

auto CreatureSystem::find_creature(const ChunkCoord& coord) const -> const CreatureInstance* {
    const auto iterator = std::find_if(creatures_.begin(), creatures_.end(), [&](const CreatureInstance& creature) {
        return creature.anchor.chunk == coord;
    });
    return iterator == creatures_.end() ? nullptr : &(*iterator);
}

auto CreatureSystem::find_resident(const CreatureSpawnAnchor& anchor) const -> const CreatureSpawnAnchor* {
    const auto iterator = std::find_if(settlement_residents_.begin(), settlement_residents_.end(), [&](const CreatureSpawnAnchor& resident) {
        return same_resident_slot(resident, anchor);
    });
    return iterator == settlement_residents_.end() ? nullptr : &(*iterator);
}

auto CreatureSystem::find_resident_creature(const CreatureSpawnAnchor& anchor) -> CreatureInstance* {
    const auto iterator = std::find_if(creatures_.begin(), creatures_.end(), [&](const CreatureInstance& creature) {
        return is_resident_species(creature.anchor.species) && same_resident_slot(creature.anchor, anchor);
    });
    return iterator == creatures_.end() ? nullptr : &(*iterator);
}

auto CreatureSystem::find_resident_creature(const CreatureSpawnAnchor& anchor) const -> const CreatureInstance* {
    const auto iterator = std::find_if(creatures_.begin(), creatures_.end(), [&](const CreatureInstance& creature) {
        return is_resident_species(creature.anchor.species) && same_resident_slot(creature.anchor, anchor);
    });
    return iterator == creatures_.end() ? nullptr : &(*iterator);
}

auto CreatureSystem::find_resident_profile(const CreatureSpawnAnchor& anchor) -> ResidentProfile* {
    const auto iterator = std::find_if(resident_profiles_.begin(), resident_profiles_.end(), [&](const ResidentProfile& profile) {
        return same_resident_slot(profile.anchor, anchor);
    });
    return iterator == resident_profiles_.end() ? nullptr : &(*iterator);
}

auto CreatureSystem::find_resident_profile(const CreatureSpawnAnchor& anchor) const -> const ResidentProfile* {
    const auto iterator = std::find_if(resident_profiles_.begin(), resident_profiles_.end(), [&](const ResidentProfile& profile) {
        return same_resident_slot(profile.anchor, anchor);
    });
    return iterator == resident_profiles_.end() ? nullptr : &(*iterator);
}

} // namespace valcraft
