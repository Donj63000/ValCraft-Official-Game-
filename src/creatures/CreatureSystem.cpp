#include "creatures/CreatureSystem.h"

#include <array>
#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/vec2.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>

namespace valcraft {

namespace {

constexpr float kGroundSnapOffset = 1.001F;
constexpr float kPi = 3.14159265358979323846F;
constexpr float kTwoPi = 2.0F * kPi;
constexpr int kSpawnCandidateCount = 4;
constexpr float kPlayerShyDistance = 3.25F;
constexpr float kNightDetectionDistance = 11.5F;
constexpr float kNightAttackDistance = 1.65F;
constexpr float kCreatureBodyRadius = 0.30F;
constexpr float kNightMonsterAttackDistance = 2.85F;
constexpr float kNightMonsterHitboxRadius = 0.82F;
constexpr float kNightMonsterHitboxHeight = 4.70F;
constexpr float kNightMonsterMovementRadius = kCreatureBodyRadius;
constexpr float kNightMonsterMinAttackHeight = 0.95F;
constexpr float kNightMonsterMaxAttackHeight = 3.15F;
constexpr float kPlayerMeleeTargetHeight = 0.90F;
constexpr float kNightBlockedAttackMargin = 0.55F;
constexpr float kNightMonsterBlockedAttackMargin = 0.25F;
constexpr float kNightStrikeCooldown = 0.9F;
constexpr float kNightChaseRoamMargin = 3.20F;
constexpr float kNightChaseRoamRadiusCap = 24.0F;
constexpr float kZombieDamage = 3.0F;
constexpr float kMaxStepHeight = 1.4F;
constexpr float kDawnAttackVisualCap = 0.42F;
constexpr float kNightChasePersistenceSeconds = 1.35F;
constexpr float kResidentGreetingDistance = 5.0F;
constexpr float kResidentPersonalSpace = 1.65F;
constexpr float kResidentHomeSnapThreshold = 1.75F;
constexpr float kResidentArrivalRadius = 0.72F;
constexpr float kResidentCrowdRadius = 1.20F;
constexpr float kResidentSocialDistance = 5.75F;
constexpr float kResidentPlayerLookDistance = 6.75F;
constexpr float kResidentPanicDuration = 1.45F;
constexpr float kResidentStepLookahead = 0.90F;
constexpr int kResidentTargetSearchRadius = 3;
constexpr float kCreatureHurtDuration = 0.34F;
constexpr float kCreatureDeathVisualDuration = 1.18F;
constexpr float kCreatureSpawnSuppressionDuration = 14.0F;
constexpr float kPopulationSyncIntervalSeconds = 0.25F;
constexpr float kResidentTargetRefreshSeconds = 0.25F;

struct SpeciesTuning {
    float day_speed = 1.0F;
    float flee_speed = 2.0F;
    float lurk_speed = 0.6F;
    float chase_speed = 1.6F;
    float day_roam_radius = 4.5F;
    float night_roam_radius = 5.5F;
    float chase_radius = 10.5F;
};

struct SteeringMoveResult {
    bool moved = false;
    bool diverted = false;
    float heading = 0.0F;
};

struct ResidentCrowdContext {
    glm::vec2 separation {0.0F};
    glm::vec2 social_pull {0.0F};
    glm::vec2 nearest_resident_direction {0.0F};
    float nearest_resident_distance = std::numeric_limits<float>::max();
    bool has_nearby_resident = false;
};

struct CreatureHitbox {
    float radius = 0.45F;
    float height = 1.0F;
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
auto is_creature_airspace_clear(const World& world, int world_x, int ground_y, int world_z) -> bool;
auto creature_body_blocker_count_at(const World& world,
                                    float center_x,
                                    int ground_y,
                                    float center_z,
                                    float body_radius,
                                    int clear_air_blocks) -> int;
auto tuning_for(CreatureSpecies species) noexcept -> SpeciesTuning;
auto hitbox_for(CreatureSpecies species) noexcept -> CreatureHitbox;
auto is_resident_species(CreatureSpecies species) noexcept -> bool;
auto hitbox_for(const CreatureInstance& creature) noexcept -> CreatureHitbox;
auto is_large_night_creature(const CreatureInstance& creature) noexcept -> bool;
auto attack_distance_for(const CreatureInstance& creature) noexcept -> float;
auto movement_body_radius_for(const CreatureInstance& creature) noexcept -> float;
auto movement_clear_air_blocks_for(const CreatureInstance& creature) noexcept -> int;
auto player_height_reachable_by(const CreatureInstance& creature, const glm::vec3& player_position) noexcept -> bool;
auto wrap_angle(float angle) noexcept -> float;
auto has_clear_creature_attack_path(const CreatureInstance& creature,
                                    const World& world,
                                    const glm::vec3& player_position,
                                    float horizontal_distance_sq) -> bool;
void ensure_creature_health(CreatureInstance& creature) noexcept;
auto horizontal_direction_or_fallback(const glm::vec3& value, const glm::vec3& fallback) noexcept -> glm::vec3;
auto ray_intersects_aabb(const glm::vec3& origin,
                         const glm::vec3& direction,
                         const glm::vec3& min_corner,
                         const glm::vec3& max_corner,
                         float max_distance) noexcept -> std::optional<float>;
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
auto try_move_grounded_towards(CreatureInstance& creature,
                               const World& world,
                               float base_heading,
                               float step_distance,
                               float roam_radius,
                               const glm::vec2& target_position,
                               const glm::vec2& preferred_direction,
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

auto finite_or(float value, float fallback) noexcept -> float {
    return std::isfinite(value) ? value : fallback;
}

auto non_negative_finite_or(float value, float fallback) noexcept -> float {
    return std::isfinite(value) ? std::max(value, 0.0F) : fallback;
}

auto non_negative_finite(float value) noexcept -> float {
    return non_negative_finite_or(value, 0.0F);
}

auto is_finite_vec3(const glm::vec3& value) noexcept -> bool {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

auto finite_vec3_or(const glm::vec3& value, const glm::vec3& fallback) noexcept -> glm::vec3 {
    return {
        finite_or(value.x, fallback.x),
        finite_or(value.y, fallback.y),
        finite_or(value.z, fallback.z),
    };
}

auto sanitize_creature_position(const glm::vec3& value, const glm::vec3& fallback) noexcept -> glm::vec3 {
    auto result = finite_vec3_or(value, fallback);
    result.y = std::clamp(result.y, static_cast<float>(kWorldMinY) - 4.0F, static_cast<float>(kWorldMaxY) + 8.0F);
    return result;
}

auto fallback_spawn_position(const CreatureSpawnAnchor& anchor) noexcept -> glm::vec3 {
    return {
        static_cast<float>(anchor.ground_block.x) + 0.5F,
        static_cast<float>(anchor.ground_block.y) + kGroundSnapOffset,
        static_cast<float>(anchor.ground_block.z) + 0.5F,
    };
}

auto is_known_creature_species(CreatureSpecies species) noexcept -> bool {
    return static_cast<std::uint8_t>(species) <= static_cast<std::uint8_t>(CreatureSpecies::Villager);
}

auto is_known_behavior_state(CreatureBehaviorState state) noexcept -> bool {
    return static_cast<std::uint8_t>(state) <= static_cast<std::uint8_t>(CreatureBehaviorState::ReturnHome);
}

auto is_known_creature_phase(CreaturePhase phase) noexcept -> bool {
    return static_cast<unsigned char>(phase) <= static_cast<unsigned char>(CreaturePhase::DawnRecover);
}

void sanitize_loaded_anchor(CreatureSpawnAnchor& anchor) noexcept {
    if (!is_known_creature_species(anchor.species)) {
        anchor.species = CreatureSpecies::Pig;
    }

    anchor.roam_radius = std::clamp(non_negative_finite_or(anchor.roam_radius, 0.0F), 0.0F, 128.0F);
    anchor.spawn_position = sanitize_creature_position(anchor.spawn_position, fallback_spawn_position(anchor));
    anchor.patrol_point_count =
        static_cast<std::uint8_t>(std::min<std::size_t>(anchor.patrol_point_count, anchor.patrol_points.size()));
    for (auto& point : anchor.patrol_points) {
        point = sanitize_creature_position(point, anchor.spawn_position);
    }
}

void sanitize_loaded_creature(CreatureInstance& creature) noexcept {
    sanitize_loaded_anchor(creature.anchor);
    creature.position = sanitize_creature_position(creature.position, creature.anchor.spawn_position);
    creature.yaw_radians = wrap_angle(creature.yaw_radians);
    creature.behavior_timer = non_negative_finite(creature.behavior_timer);
    creature.animation_time = non_negative_finite(creature.animation_time);
    creature.wander_heading = wrap_angle(creature.wander_heading);
    creature.nervous_intensity = std::clamp(finite_or(creature.nervous_intensity, 0.0F), 0.0F, 1.0F);
    if (!is_known_behavior_state(creature.behavior_state)) {
        creature.behavior_state = CreatureBehaviorState::Idle;
    }
    if (!is_known_creature_phase(creature.phase)) {
        creature.phase = CreaturePhase::Day;
    }
    creature.morph_factor = std::clamp(finite_or(creature.morph_factor, 0.0F), 0.0F, 1.0F);
    creature.motion_amount = std::clamp(finite_or(creature.motion_amount, 0.0F), 0.0F, 1.0F);
    creature.gaze_weight = std::clamp(finite_or(creature.gaze_weight, 0.0F), 0.0F, 1.0F);
    creature.attack_cooldown = non_negative_finite(creature.attack_cooldown);
    creature.attack_amount = std::clamp(finite_or(creature.attack_amount, 0.0F), 0.0F, 1.0F);
    creature.hurt_timer = non_negative_finite(creature.hurt_timer);
    creature.hit_direction = horizontal_direction_or_fallback(creature.hit_direction, {0.0F, 0.0F, 1.0F});
    if (creature.anchor.patrol_point_count == 0U) {
        creature.resident_target_index = 0U;
    } else {
        creature.resident_target_index =
            static_cast<std::uint8_t>(std::min<std::uint8_t>(
                creature.resident_target_index,
                static_cast<std::uint8_t>(creature.anchor.patrol_point_count - 1U)));
    }
    creature.resident_cached_target = {};
    creature.resident_target_refresh_timer = 0.0F;
    creature.resident_cached_heading = 0.0F;
    creature.resident_cached_phase = 0U;
    creature.resident_target_valid = false;
    creature.resident_heading_valid = false;
    ensure_creature_health(creature);
}

auto wrap_angle(float angle) noexcept -> float {
    if (!std::isfinite(angle)) {
        return 0.0F;
    }
    angle = std::fmod(angle, kTwoPi);
    if (angle <= -kPi) {
        angle += kTwoPi;
    }
    if (angle > kPi) {
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
                               float center_x,
                               float center_z,
                               float current_y,
                               std::optional<int> preferred_floor_y = std::nullopt) -> std::optional<float> {
    const auto world_x = static_cast<int>(std::floor(center_x));
    const auto world_z = static_cast<int>(std::floor(center_z));
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
        }
        return std::nullopt;
    }

    const auto surface_y = world.loaded_surface_height(world_x, world_z);
    if (!surface_y.has_value()) {
        return std::nullopt;
    }
    return try_floor(*surface_y);
}

auto clamp_vec2_length(const glm::vec2& value, float max_length) noexcept -> glm::vec2 {
    const auto length_sq = glm::dot(value, value);
    if (length_sq <= 1.0e-8F) {
        return {0.0F, 0.0F};
    }

    const auto length = std::sqrt(length_sq);
    if (length <= max_length) {
        return value;
    }
    return value / length * max_length;
}

auto resident_phase_offset(const ResidentProfile& profile, ResidentRoutinePhase phase, float radius) noexcept -> glm::vec2 {
    if (radius <= 1.0e-4F) {
        return {0.0F, 0.0F};
    }

    auto seed = profile.routine_seed ^ (static_cast<std::uint32_t>(phase) + 1U) * 0x9E3779B9U;
    const auto angle = next_unit(seed) * kTwoPi;
    const auto distance = radius * (0.35F + next_unit(seed) * 0.65F);
    return direction_from_yaw(angle) * distance;
}

auto resident_activity_target(const ResidentProfile& profile, ResidentRoutinePhase phase) noexcept -> glm::vec3 {
    auto target = resident_target_for_phase(profile, phase);
    float jitter_radius = 0.0F;
    switch (phase) {
    case ResidentRoutinePhase::Morning:
        jitter_radius = 0.28F;
        break;
    case ResidentRoutinePhase::Work:
        jitter_radius = profile.role == CreatureResidentRole::Gardener ? 0.55F : 0.34F;
        break;
    case ResidentRoutinePhase::Social:
        jitter_radius = 0.82F;
        break;
    case ResidentRoutinePhase::Evening:
        jitter_radius = 0.26F;
        break;
    case ResidentRoutinePhase::Home:
    case ResidentRoutinePhase::Night:
    default:
        jitter_radius = 0.10F;
        break;
    }

    const auto offset = resident_phase_offset(profile, phase, jitter_radius);
    target.x += offset.x;
    target.z += offset.y;
    return target;
}

auto resolve_resident_activity_target(const CreatureInstance& creature,
                                      const ResidentProfile& profile,
                                      const World& world,
                                      const glm::vec3& raw_target) -> glm::vec3 {
    const auto preferred_floor_y = creature.anchor.ground_block.y;
    const auto target_block_x = static_cast<int>(std::floor(raw_target.x));
    const auto target_block_z = static_cast<int>(std::floor(raw_target.z));
    const glm::vec2 raw_xz {raw_target.x, raw_target.z};
    const glm::vec2 current_xz {creature.position.x, creature.position.z};
    const glm::vec2 home_xz {profile.home_position.x, profile.home_position.z};
    const auto roam_radius_sq = profile.roam_radius * profile.roam_radius;
    const auto body_radius = movement_body_radius_for(creature);
    const auto clear_air_blocks = movement_clear_air_blocks_for(creature);

    glm::vec3 best_target = profile.home_position;
    auto best_score = std::numeric_limits<float>::max();
    auto found_walkable_target = false;

    for (int radius = 0; radius <= kResidentTargetSearchRadius; ++radius) {
        for (int dz = -radius; dz <= radius; ++dz) {
            for (int dx = -radius; dx <= radius; ++dx) {
                if (radius != 0 && std::abs(dx) != radius && std::abs(dz) != radius) {
                    continue;
                }

                const auto block_x = target_block_x + dx;
                const auto block_z = target_block_z + dz;
                if (!is_spawn_column_clear(world, block_x, preferred_floor_y, block_z)) {
                    continue;
                }

                const auto candidate = radius == 0 ?
                    glm::vec3 {raw_target.x, static_cast<float>(preferred_floor_y) + kGroundSnapOffset, raw_target.z} :
                    glm::vec3 {static_cast<float>(block_x) + 0.5F,
                               static_cast<float>(preferred_floor_y) + kGroundSnapOffset,
                               static_cast<float>(block_z) + 0.5F};
                const glm::vec2 candidate_xz {candidate.x, candidate.z};
                const auto home_delta = candidate_xz - home_xz;
                if (glm::dot(home_delta, home_delta) > roam_radius_sq) {
                    continue;
                }
                if (creature_body_blocker_count_at(
                        world,
                        candidate.x,
                        preferred_floor_y,
                        candidate.z,
                        body_radius,
                        clear_air_blocks) != 0) {
                    continue;
                }

                const auto target_delta = candidate_xz - raw_xz;
                const auto current_delta = candidate_xz - current_xz;
                const auto score =
                    glm::dot(target_delta, target_delta) +
                    glm::dot(current_delta, current_delta) * 0.035F +
                    static_cast<float>(radius) * 0.08F;
                if (score < best_score) {
                    best_score = score;
                    best_target = candidate;
                    found_walkable_target = true;
                }
            }
        }

        if (found_walkable_target && radius <= 1) {
            break;
        }
    }

    if (found_walkable_target) {
        return best_target;
    }

    const auto home_block_x = static_cast<int>(std::floor(profile.home_position.x));
    const auto home_block_z = static_cast<int>(std::floor(profile.home_position.z));
    if (is_spawn_column_clear(world, home_block_x, preferred_floor_y, home_block_z)) {
        return {
            profile.home_position.x,
            static_cast<float>(preferred_floor_y) + kGroundSnapOffset,
            profile.home_position.z,
        };
    }

    return creature.position;
}

auto is_resident_travel_state(CreatureBehaviorState state) noexcept -> bool {
    return state == CreatureBehaviorState::Wander ||
           state == CreatureBehaviorState::ReturnHome ||
           state == CreatureBehaviorState::Flee;
}

auto is_resident_stationary_activity(CreatureBehaviorState state) noexcept -> bool {
    return state == CreatureBehaviorState::Idle ||
           state == CreatureBehaviorState::Sniff ||
           state == CreatureBehaviorState::Stare ||
           state == CreatureBehaviorState::Graze ||
           state == CreatureBehaviorState::Work ||
           state == CreatureBehaviorState::Socialize ||
           state == CreatureBehaviorState::Sleep;
}

auto resident_crowd_context(const CreatureInstance& creature,
                            std::span<const CreatureInstance> active_creatures) noexcept -> ResidentCrowdContext {
    ResidentCrowdContext context {};

    for (const auto& other : active_creatures) {
        if (&other == &creature ||
            !is_resident_species(other.anchor.species) ||
            other.health <= 0.0F) {
            continue;
        }

        const glm::vec2 delta {other.position.x - creature.position.x, other.position.z - creature.position.z};
        const auto distance_sq = glm::dot(delta, delta);
        if (distance_sq <= 1.0e-6F) {
            continue;
        }

        const auto distance = std::sqrt(distance_sq);
        const auto direction = delta / distance;
        if (distance < kResidentCrowdRadius) {
            const auto strength = (kResidentCrowdRadius - distance) / kResidentCrowdRadius;
            context.separation -= direction * strength;
        }

        if (distance < context.nearest_resident_distance) {
            context.nearest_resident_distance = distance;
            context.nearest_resident_direction = direction;
            context.has_nearby_resident = true;
        }
    }

    context.separation = clamp_vec2_length(context.separation, 1.0F);
    if (context.has_nearby_resident &&
        context.nearest_resident_distance > kResidentCrowdRadius * 1.22F &&
        context.nearest_resident_distance < kResidentSocialDistance) {
        const auto attraction = 1.0F - context.nearest_resident_distance / kResidentSocialDistance;
        context.social_pull = context.nearest_resident_direction * glm::clamp(attraction, 0.0F, 1.0F);
    }
    return context;
}

auto resident_choose_arrived_activity(const ResidentProfile& profile,
                                      ResidentRoutinePhase phase,
                                      float player_distance,
                                      bool has_nearby_resident,
                                      std::uint32_t& behavior_seed) noexcept -> CreatureBehaviorState {
    const auto choice = next_unit(behavior_seed);

    switch (phase) {
    case ResidentRoutinePhase::Night:
    case ResidentRoutinePhase::Home:
        if (player_distance < kResidentGreetingDistance && choice < 0.24F) {
            return CreatureBehaviorState::Stare;
        }
        return choice < 0.78F ? CreatureBehaviorState::Sleep : CreatureBehaviorState::Idle;
    case ResidentRoutinePhase::Morning:
        if (profile.role == CreatureResidentRole::Gardener && choice < 0.70F) {
            return CreatureBehaviorState::Graze;
        }
        if (choice < 0.58F) {
            return CreatureBehaviorState::Work;
        }
        return choice < 0.80F ? CreatureBehaviorState::Sniff : CreatureBehaviorState::Idle;
    case ResidentRoutinePhase::Work:
        if (profile.role == CreatureResidentRole::Gardener) {
            return choice < 0.72F ? CreatureBehaviorState::Graze : CreatureBehaviorState::Sniff;
        }
        if (profile.role == CreatureResidentRole::Elder) {
            return choice < 0.38F ? CreatureBehaviorState::Stare : CreatureBehaviorState::Idle;
        }
        return choice < 0.76F ? CreatureBehaviorState::Work : CreatureBehaviorState::Sniff;
    case ResidentRoutinePhase::Social:
        if (has_nearby_resident && choice < 0.68F) {
            return CreatureBehaviorState::Socialize;
        }
        if (player_distance < kResidentGreetingDistance && choice < 0.48F) {
            return CreatureBehaviorState::Stare;
        }
        return choice < 0.78F ? CreatureBehaviorState::Socialize : CreatureBehaviorState::Idle;
    case ResidentRoutinePhase::Evening:
    default:
        if (choice < 0.42F) {
            return CreatureBehaviorState::Socialize;
        }
        if (choice < 0.68F) {
            return CreatureBehaviorState::Stare;
        }
        return CreatureBehaviorState::Idle;
    }
}

auto resident_activity_timer(CreatureBehaviorState state,
                             CreatureResidentRole role,
                             std::uint32_t& behavior_seed) noexcept -> float {
    const auto roll = next_unit(behavior_seed);
    const auto role_bias = resident_behavior_bias(role);
    switch (state) {
    case CreatureBehaviorState::Sleep:
        return 1.35F + roll * 1.85F;
    case CreatureBehaviorState::Work:
        return 0.95F + roll * 1.25F + role_bias * 0.35F;
    case CreatureBehaviorState::Graze:
        return 0.85F + roll * 1.15F;
    case CreatureBehaviorState::Socialize:
        return 0.72F + roll * 1.10F;
    case CreatureBehaviorState::ReturnHome:
    case CreatureBehaviorState::Wander:
        return 0.58F + roll * 0.82F;
    case CreatureBehaviorState::Stare:
        return 0.46F + roll * 0.72F;
    case CreatureBehaviorState::Sniff:
        return 0.52F + roll * 0.70F;
    case CreatureBehaviorState::Idle:
    default:
        return 0.72F + roll * 0.92F;
    }
}

auto resident_travel_state_for_phase(ResidentRoutinePhase phase) noexcept -> CreatureBehaviorState {
    switch (phase) {
    case ResidentRoutinePhase::Evening:
    case ResidentRoutinePhase::Home:
    case ResidentRoutinePhase::Night:
        return CreatureBehaviorState::ReturnHome;
    case ResidentRoutinePhase::Morning:
    case ResidentRoutinePhase::Work:
    case ResidentRoutinePhase::Social:
    default:
        return CreatureBehaviorState::Wander;
    }
}

void update_resident_creature(CreatureInstance& creature,
                              const ResidentProfile& profile,
                              float dt,
                              const World& world,
                              const glm::vec3& player_position,
                              const EnvironmentState& environment,
                              std::span<const CreatureInstance> active_creatures,
                              CreatureAuditStats& audit_stats) {
    const auto tuning = tuning_for(creature.anchor.species);
    const auto home_position = resident_floor_position(creature.anchor);
    const auto preferred_floor_y = creature.anchor.ground_block.y;
    if (std::abs(creature.position.y - home_position.y) > kResidentHomeSnapThreshold ||
        horizontal_distance_squared(creature.position, home_position) > profile.roam_radius * profile.roam_radius) {
        creature.position = home_position;
        creature.yaw_radians = settle_yaw_from_seed(profile.routine_seed);
        creature.wander_heading = creature.yaw_radians;
        creature.behavior_state = CreatureBehaviorState::Idle;
        creature.behavior_timer = 0.0F;
        creature.resident_target_valid = false;
        creature.resident_heading_valid = false;
    }

    creature.phase = CreaturePhase::Day;
    creature.morph_factor = 0.0F;
    creature.animation_time += non_negative_finite(dt);
    creature.behavior_timer = std::max(0.0F, creature.behavior_timer - dt);
    creature.attack_cooldown = 0.0F;

    const auto to_player = glm::vec2 {
        player_position.x - creature.position.x,
        player_position.z - creature.position.z,
    };
    const auto player_distance_sq = glm::dot(to_player, to_player);
    const auto player_distance = std::sqrt(std::max(player_distance_sq, 0.0F));
    const auto player_close = player_distance < kResidentPersonalSpace && player_distance_sq > 1.0e-6F;
    const auto player_lookable = player_distance < kResidentPlayerLookDistance && player_distance_sq > 1.0e-6F;
    const auto phase = resident_routine_phase(environment.time_of_day);
    auto requested_target = resident_activity_target(profile, phase);
    const auto crowd = resident_crowd_context(creature, active_creatures);

    if (phase == ResidentRoutinePhase::Social && crowd.has_nearby_resident) {
        const auto meet_distance = glm::clamp(crowd.nearest_resident_distance * 0.45F, 1.65F, 2.80F);
        const auto meet_target = creature.position + glm::vec3 {
            crowd.nearest_resident_direction.x * meet_distance,
            0.0F,
            crowd.nearest_resident_direction.y * meet_distance,
        };
        requested_target.x = glm::mix(requested_target.x, meet_target.x, 0.34F);
        requested_target.z = glm::mix(requested_target.z, meet_target.z, 0.34F);
    }
    creature.resident_target_refresh_timer =
        std::max(0.0F, creature.resident_target_refresh_timer - non_negative_finite(dt));
    const auto phase_value = static_cast<std::uint8_t>(phase);
    const auto phase_changed = !creature.resident_target_valid || creature.resident_cached_phase != phase_value;
    if (phase_changed || creature.resident_target_refresh_timer <= 0.0F) {
        // Je replie l'objectif vers une case marchable quatre fois par seconde au
        // lieu de rescanner jusqu'a 49 colonnes a chaque tick de rendu.
        const auto previous_target = creature.resident_cached_target;
        creature.resident_cached_target =
            resolve_resident_activity_target(creature, profile, world, requested_target);
        creature.resident_cached_phase = phase_value;
        creature.resident_target_valid = true;
        const auto jitter = static_cast<float>((profile.routine_seed >> 9U) & 3U) * 0.015F;
        creature.resident_target_refresh_timer = kResidentTargetRefreshSeconds + jitter;
        if (phase_changed ||
            horizontal_distance_squared(previous_target, creature.resident_cached_target) > 0.20F * 0.20F) {
            creature.resident_heading_valid = false;
        }
        ++audit_stats.resident_target_refreshes;
    }
    const auto target_position = creature.resident_cached_target;

    const auto to_target = glm::vec2 {
        target_position.x - creature.position.x,
        target_position.z - creature.position.z,
    };
    const auto target_distance_sq = glm::dot(to_target, to_target);
    const auto target_distance = std::sqrt(std::max(target_distance_sq, 0.0F));
    const auto target_direction = normalize_or_cardinal(to_target, profile.routine_seed);
    const auto orbit_direction = perpendicular_left(target_direction) * ((profile.routine_seed & 1U) == 0U ? 1.0F : -1.0F);
    const auto resident_speed = tuning.day_speed * resident_speed_factor(profile.role);
    const auto role_bias = resident_behavior_bias(profile.role);
    const auto position_before_move = creature.position;

    const auto far_from_target = target_distance > kResidentArrivalRadius * 1.70F;
    const auto panic_active = creature.behavior_state == CreatureBehaviorState::Flee && creature.behavior_timer > 0.0F;
    const auto recently_hurt = creature.hurt_timer > 0.0F;
    const auto should_pick_routine_behavior =
        creature.behavior_timer <= 0.0F ||
        creature.behavior_state == CreatureBehaviorState::Chase ||
        creature.behavior_state == CreatureBehaviorState::Strike ||
        (!far_from_target && is_resident_travel_state(creature.behavior_state));

    if ((recently_hurt || panic_active) && player_distance_sq > 1.0e-6F) {
        creature.behavior_state = CreatureBehaviorState::Flee;
        creature.behavior_timer = std::max(creature.behavior_timer, kResidentPanicDuration);
    } else if (player_close) {
        creature.behavior_state = CreatureBehaviorState::Wander;
        creature.behavior_timer = 0.34F;
    } else if (far_from_target &&
               (creature.behavior_timer <= 0.0F || is_resident_stationary_activity(creature.behavior_state))) {
        creature.behavior_state = resident_travel_state_for_phase(phase);
        creature.behavior_timer = resident_activity_timer(creature.behavior_state, profile.role, creature.behavior_seed);
        creature.wander_heading = yaw_from_direction(target_direction);
    } else if (should_pick_routine_behavior) {
        creature.behavior_state = resident_choose_arrived_activity(
            profile,
            phase,
            player_distance,
            crowd.has_nearby_resident,
            creature.behavior_seed);
        creature.behavior_timer = resident_activity_timer(creature.behavior_state, profile.role, creature.behavior_seed);
        if (creature.behavior_state == CreatureBehaviorState::Sniff ||
            creature.behavior_state == CreatureBehaviorState::Graze ||
            creature.behavior_state == CreatureBehaviorState::Socialize) {
            creature.wander_heading = yaw_from_direction(orbit_direction);
        }
    }

    glm::vec2 desired_move {0.0F};
    auto desired_yaw = creature.yaw_radians;
    float target_motion_amount = 0.07F;
    float target_gaze_weight = 0.24F + role_bias * 0.14F;
    float target_attack_amount = 0.0F;
    float speed_multiplier = 0.70F;

    if (creature.behavior_state == CreatureBehaviorState::Flee && player_distance_sq > 1.0e-6F) {
        const auto flee_direction = -glm::normalize(to_player);
        desired_move = flee_direction * tuning.flee_speed * dt * 0.78F;
        desired_yaw = yaw_from_direction(flee_direction);
        target_motion_amount = 0.92F;
        target_gaze_weight = 0.10F;
    } else if (player_close && player_distance_sq > 1.0e-6F) {
        const auto give_space_direction = -glm::normalize(to_player);
        desired_move = give_space_direction * resident_speed * dt * (0.42F + role_bias * 0.12F);
        desired_yaw = yaw_from_direction(give_space_direction);
        target_motion_amount = 0.46F + role_bias * 0.10F;
        target_gaze_weight = 0.16F;
    } else {
        switch (creature.behavior_state) {
        case CreatureBehaviorState::ReturnHome:
            speed_multiplier = phase == ResidentRoutinePhase::Night ? 0.92F : 0.78F;
            desired_move = target_direction * resident_speed * dt * speed_multiplier;
            desired_yaw = yaw_from_direction(target_direction);
            target_motion_amount = 0.54F + role_bias * 0.18F;
            target_gaze_weight = 0.20F;
            break;
        case CreatureBehaviorState::Wander:
            if (target_distance > kResidentArrivalRadius) {
                creature.wander_heading = rotate_towards(
                    creature.wander_heading,
                    yaw_from_direction(target_direction),
                    (1.60F + role_bias * 0.35F) * non_negative_finite(dt));
            }
            desired_move = direction_from_yaw(creature.wander_heading) * resident_speed * dt * (0.56F + role_bias * 0.20F);
            desired_yaw = creature.wander_heading;
            target_motion_amount = 0.40F + role_bias * 0.28F;
            target_gaze_weight = 0.20F + role_bias * 0.15F;
            break;
        case CreatureBehaviorState::Work:
            if (target_distance > kResidentArrivalRadius) {
                desired_move = target_direction * resident_speed * dt * (0.42F + role_bias * 0.14F);
                desired_yaw = yaw_from_direction(target_direction);
                target_motion_amount = 0.34F + role_bias * 0.18F;
            } else {
                desired_yaw = wrap_angle(yaw_from_direction(target_direction) + std::sin(creature.animation_time * 1.6F) * 0.20F);
                target_motion_amount = 0.13F + role_bias * 0.08F;
            }
            target_gaze_weight = 0.38F + role_bias * 0.22F;
            target_attack_amount = 0.58F;
            break;
        case CreatureBehaviorState::Graze:
            if (target_distance > kResidentArrivalRadius * 1.20F) {
                desired_move = target_direction * resident_speed * dt * 0.38F;
                desired_yaw = yaw_from_direction(target_direction);
                target_motion_amount = 0.28F;
            } else {
                desired_move = direction_from_yaw(creature.wander_heading) * resident_speed * dt * 0.10F;
                desired_yaw = wrap_angle(creature.wander_heading + std::sin(creature.animation_time * 3.2F) * 0.10F);
                target_motion_amount = 0.18F;
            }
            target_gaze_weight = 0.44F;
            target_attack_amount = 0.34F;
            break;
        case CreatureBehaviorState::Socialize:
            if (target_distance > kResidentArrivalRadius * 1.45F) {
                desired_move = target_direction * resident_speed * dt * (0.40F + role_bias * 0.16F);
                desired_yaw = yaw_from_direction(target_direction);
                target_motion_amount = 0.34F + role_bias * 0.16F;
            } else if (crowd.has_nearby_resident) {
                desired_yaw = yaw_from_direction(crowd.nearest_resident_direction);
                desired_move = (crowd.social_pull + orbit_direction * 0.18F) * resident_speed * dt * 0.12F;
                target_motion_amount = 0.16F + role_bias * 0.08F;
            } else {
                desired_yaw = player_lookable ? yaw_from_direction(glm::normalize(to_player)) : creature.wander_heading;
                target_motion_amount = 0.10F + role_bias * 0.05F;
            }
            target_gaze_weight = 0.74F + role_bias * 0.16F;
            target_attack_amount = 0.44F;
            break;
        case CreatureBehaviorState::Sleep:
            if (target_distance > kResidentArrivalRadius * 0.75F) {
                desired_move = target_direction * resident_speed * dt * 0.36F;
                desired_yaw = yaw_from_direction(target_direction);
                target_motion_amount = 0.26F;
                target_gaze_weight = 0.10F;
            } else {
                desired_yaw = creature.wander_heading;
                target_motion_amount = 0.02F;
                target_gaze_weight = player_lookable ? 0.20F : 0.06F;
                target_attack_amount = 0.0F;
            }
            break;
        case CreatureBehaviorState::Sniff:
            desired_move = direction_from_yaw(creature.wander_heading) * resident_speed * dt * (0.10F + role_bias * 0.06F);
            desired_yaw = wrap_angle(creature.wander_heading + std::sin(creature.animation_time * 3.8F) * 0.12F);
            target_motion_amount = 0.20F + role_bias * 0.12F;
            target_gaze_weight = 0.36F;
            break;
        case CreatureBehaviorState::Stare:
            if (player_lookable) {
                desired_yaw = yaw_from_direction(glm::normalize(to_player));
            } else if (crowd.has_nearby_resident) {
                desired_yaw = yaw_from_direction(crowd.nearest_resident_direction);
            } else {
                desired_yaw = creature.wander_heading;
            }
            target_motion_amount = 0.07F + role_bias * 0.04F;
            target_gaze_weight = 0.82F;
            break;
        case CreatureBehaviorState::Idle:
        default:
            desired_yaw = player_lookable && player_distance < kResidentGreetingDistance * 0.72F ?
                yaw_from_direction(glm::normalize(to_player)) : creature.wander_heading;
            target_motion_amount = 0.05F + role_bias * 0.04F;
            target_gaze_weight = player_lookable ? 0.46F : 0.22F + role_bias * 0.10F;
            break;
        }
    }

    if (glm::dot(crowd.separation, crowd.separation) > 1.0e-6F &&
        creature.behavior_state != CreatureBehaviorState::Sleep) {
        desired_move += crowd.separation * resident_speed * dt * 0.62F;
        if (glm::dot(desired_move, desired_move) > 1.0e-6F &&
            (creature.behavior_state == CreatureBehaviorState::Idle || creature.behavior_state == CreatureBehaviorState::Socialize)) {
            desired_yaw = yaw_from_direction(glm::normalize(desired_move));
        }
        target_motion_amount = std::max(target_motion_amount, 0.28F);
    }

    const auto desired_distance = glm::length(desired_move);
    bool attempted_move = false;
    bool moved = false;
    bool steering_diverted = false;
    auto resolved_yaw = desired_yaw;
    if (desired_distance > 1.0e-6F) {
        attempted_move = true;
        const auto base_heading = yaw_from_direction(normalize_or_cardinal(desired_move, profile.routine_seed));
        if (creature.resident_heading_valid) {
            auto probe = creature;
            if (try_move_grounded(
                    probe,
                    world,
                    direction_from_yaw(creature.resident_cached_heading) * desired_distance,
                    profile.roam_radius,
                    preferred_floor_y)) {
                const glm::vec2 target_xz {target_position.x, target_position.z};
                const glm::vec2 before_xz {creature.position.x, creature.position.z};
                const glm::vec2 after_xz {probe.position.x, probe.position.z};
                if (glm::dot(target_xz - after_xz, target_xz - after_xz) <=
                    glm::dot(target_xz - before_xz, target_xz - before_xz) + 0.01F) {
                    creature.position = probe.position;
                    moved = true;
                    resolved_yaw = creature.resident_cached_heading;
                    steering_diverted = std::abs(wrap_angle(resolved_yaw - base_heading)) > 0.05F;
                }
            }
            if (!moved) {
                creature.resident_heading_valid = false;
            }
        }

        if (!moved) {
            // Je ne relance la recherche exhaustive de 60 directions que lorsque
            // le cap valide de la frame precedente est bloque ou devenu mauvais.
            ++audit_stats.resident_steering_fallbacks;
            const auto steering_result = try_move_grounded_towards(
                creature,
                world,
                base_heading,
                desired_distance,
                profile.roam_radius,
                {target_position.x, target_position.z},
                normalize_or_cardinal(desired_move, profile.routine_seed),
                true,
                preferred_floor_y);
            moved = steering_result.moved;
            steering_diverted = steering_result.diverted;
            if (moved) {
                resolved_yaw = steering_result.heading;
                creature.resident_cached_heading = steering_result.heading;
                creature.resident_heading_valid = true;
            }
        }
    }

    const glm::vec2 travelled_horizontal {
        creature.position.x - position_before_move.x,
        creature.position.z - position_before_move.z,
    };
    if (glm::dot(travelled_horizontal, travelled_horizontal) > 1.0e-6F) {
        resolved_yaw = yaw_from_direction(glm::normalize(travelled_horizontal));
        if (creature.behavior_state == CreatureBehaviorState::Wander ||
            creature.behavior_state == CreatureBehaviorState::ReturnHome ||
            creature.behavior_state == CreatureBehaviorState::Sniff ||
            creature.behavior_state == CreatureBehaviorState::Graze) {
            creature.wander_heading = resolved_yaw;
        }
    } else if (attempted_move && !moved) {
        creature.wander_heading = wrap_angle(creature.wander_heading + next_signed_unit(creature.behavior_seed) * 1.18F + 0.72F);
        target_motion_amount = std::min(target_motion_amount, 0.16F);
        if (far_from_target) {
            creature.behavior_timer = std::min(creature.behavior_timer, 0.22F);
        }
    }
    if (steering_diverted &&
        (creature.behavior_state == CreatureBehaviorState::Wander || creature.behavior_state == CreatureBehaviorState::ReturnHome)) {
        creature.wander_heading = resolved_yaw;
    }
    creature.yaw_radians = moved ?
        resolved_yaw :
        rotate_towards(creature.yaw_radians, resolved_yaw, 5.8F * non_negative_finite(dt));

    if (dt > 0.0F) {
        const auto reference_distance = std::max(resident_speed * dt, 0.001F);
        const auto realised_motion = glm::clamp(glm::length(travelled_horizontal) / reference_distance, 0.0F, 1.0F);
        target_motion_amount = std::max(target_motion_amount, realised_motion * 0.92F);
    }

    creature.nervous_intensity = glm::clamp(
        (recently_hurt || panic_active ? 0.55F : 0.0F) +
            (player_close ? 0.18F : 0.0F) +
            std::max(0.0F, 0.16F - environment.daylight_factor * 0.10F),
        0.0F,
        0.72F);

    const auto response = smoothing_factor(dt, 8.6F);
    creature.motion_amount = glm::mix(creature.motion_amount, glm::clamp(target_motion_amount, 0.0F, 1.0F), response);
    creature.gaze_weight = glm::mix(creature.gaze_weight, glm::clamp(target_gaze_weight, 0.0F, 1.0F), response);
    creature.attack_amount = glm::mix(creature.attack_amount, glm::clamp(target_attack_amount, 0.0F, 1.0F), response);
}

auto is_chunk_within_radius(const ChunkCoord& center, const ChunkCoord& coord, int radius) noexcept -> bool {
    const auto dx = static_cast<long long>(coord.x) - static_cast<long long>(center.x);
    const auto dz = static_cast<long long>(coord.z) - static_cast<long long>(center.z);
    const auto abs_dx = dx < 0 ? -dx : dx;
    const auto abs_dz = dz < 0 ? -dz : dz;
    return abs_dx <= static_cast<long long>(radius) && abs_dz <= static_cast<long long>(radius);
}

auto chunk_distance_squared_to_player(const ChunkCoord& coord, const glm::vec3& player_position) noexcept -> float {
    const auto center_x = static_cast<double>(coord.x) * static_cast<double>(kChunkSizeX) +
                          static_cast<double>(kChunkSizeX) * 0.5;
    const auto center_z = static_cast<double>(coord.z) * static_cast<double>(kChunkSizeZ) +
                          static_cast<double>(kChunkSizeZ) * 0.5;
    const auto dx = center_x - player_position.x;
    const auto dz = center_z - player_position.z;
    return static_cast<float>(dx * dx + dz * dz);
}

auto is_chunk_within_population_interest(const ChunkCoord& player_center,
                                         const ChunkCoord& coord,
                                         int player_radius,
                                         const std::optional<CreaturePopulationInterest>& secondary,
                                         int secondary_radius_offset = 0) noexcept -> bool {
    if (is_chunk_within_radius(player_center, coord, player_radius)) {
        return true;
    }
    if (!secondary.has_value()) {
        return false;
    }

    const ChunkCoord secondary_center {
        static_cast<int>(std::floor(secondary->center.x / static_cast<float>(kChunkSizeX))),
        static_cast<int>(std::floor(secondary->center.z / static_cast<float>(kChunkSizeZ))),
    };
    return is_chunk_within_radius(
        secondary_center,
        coord,
        std::max(1, secondary->radius_chunks + secondary_radius_offset));
}

auto chunk_distance_squared_to_population_interests(
    const ChunkCoord& coord,
    const glm::vec3& player_position,
    const std::optional<CreaturePopulationInterest>& secondary) noexcept -> float {
    auto closest_distance = chunk_distance_squared_to_player(coord, player_position);
    if (secondary.has_value()) {
        closest_distance = std::min(
            closest_distance,
            chunk_distance_squared_to_player(coord, secondary->center));
    }
    return closest_distance;
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
    // Je refuse un coup dont la ligne de melee part deja dans un obstacle ou vise un bloc solide.
    if (is_block_collidable(world.get_block(current.x, current.y, current.z)) ||
        is_block_collidable(world.get_block(target_cell.x, target_cell.y, target_cell.z))) {
        return false;
    }
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
    if (horizontal_distance_sq <= 1.0e-6F || horizontal_distance > attack_distance_for(creature)) {
        return false;
    }
    if (!player_height_reachable_by(creature, player_position)) {
        return false;
    }

    return has_clear_creature_attack_path(creature, world, player_position, horizontal_distance_sq);
}

auto night_chase_roam_radius_for(const CreatureInstance& creature,
                                 const SpeciesTuning& tuning,
                                 const glm::vec3& player_position) noexcept -> float {
    const glm::vec2 anchor_to_player {
        player_position.x - creature.anchor.spawn_position.x,
        player_position.z - creature.anchor.spawn_position.z,
    };
    const auto player_radius = glm::length(anchor_to_player);
    const auto dynamic_radius = player_radius + attack_distance_for(creature) + kNightChaseRoamMargin;
    return glm::clamp(std::max(tuning.chase_radius, dynamic_radius), tuning.chase_radius, kNightChaseRoamRadiusCap);
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

auto hitbox_for(CreatureSpecies species) noexcept -> CreatureHitbox {
    switch (species) {
    case CreatureSpecies::Cow:
        return {0.58F, 1.30F};
    case CreatureSpecies::Villager:
        return {0.48F, 1.84F};
    case CreatureSpecies::Sheep:
        return {0.50F, 1.02F};
    case CreatureSpecies::Pig:
    default:
        return {0.46F, 0.88F};
    }
}

auto is_large_night_creature(const CreatureInstance& creature) noexcept -> bool {
    return !is_resident_species(creature.anchor.species) &&
           (creature.phase == CreaturePhase::Night || creature.morph_factor >= 0.55F);
}

auto hitbox_for(const CreatureInstance& creature) noexcept -> CreatureHitbox {
    if (is_large_night_creature(creature)) {
        return {kNightMonsterHitboxRadius, kNightMonsterHitboxHeight};
    }
    return hitbox_for(creature.anchor.species);
}

auto attack_distance_for(const CreatureInstance& creature) noexcept -> float {
    return is_large_night_creature(creature) ? kNightMonsterAttackDistance : kNightAttackDistance;
}

auto movement_body_radius_for(const CreatureInstance& creature) noexcept -> float {
    return is_large_night_creature(creature) ? kNightMonsterMovementRadius : kCreatureBodyRadius;
}

auto movement_clear_air_blocks_for(const CreatureInstance& creature) noexcept -> int {
    if (!is_large_night_creature(creature)) {
        return 2;
    }

    return std::max(2, static_cast<int>(std::ceil(kNightMonsterHitboxHeight)));
}

auto player_height_reachable_by(const CreatureInstance& creature, const glm::vec3& player_position) noexcept -> bool {
    if (!is_large_night_creature(creature)) {
        return shares_melee_height_layer(creature.position, player_position);
    }

    const auto target_y = player_position.y + kPlayerMeleeTargetHeight;
    const auto min_y = creature.position.y + 0.12F;
    const auto max_y = creature.position.y + kNightMonsterHitboxHeight;
    return target_y >= min_y && target_y <= max_y;
}

auto has_clear_creature_attack_path(const CreatureInstance& creature,
                                    const World& world,
                                    const glm::vec3& player_position,
                                    float horizontal_distance_sq) -> bool {
    if (horizontal_distance_sq <= 1.0e-6F) {
        return false;
    }

    const auto to_player = glm::vec2 {
        player_position.x - creature.position.x,
        player_position.z - creature.position.z,
    };
    const auto attack_direction = glm::normalize(to_player);
    const auto target_height = player_position.y + kPlayerMeleeTargetHeight - creature.position.y;
    const auto source_height = is_large_night_creature(creature) ?
        glm::clamp(target_height, kNightMonsterMinAttackHeight, kNightMonsterMaxAttackHeight) :
        kPlayerMeleeTargetHeight;
    const auto reach_offset = is_large_night_creature(creature) ? 0.58F : 0.0F;
    const auto attack_origin =
        creature.position + glm::vec3 {attack_direction.x * reach_offset, source_height, attack_direction.y * reach_offset};
    const auto player_target = player_position + glm::vec3 {0.0F, kPlayerMeleeTargetHeight, 0.0F};
    return has_clear_melee_path(world, attack_origin, player_target);
}

void ensure_creature_health(CreatureInstance& creature) noexcept {
    const auto max_health = creature_max_health(creature.anchor.species);
    if (!std::isfinite(creature.health)) {
        creature.health = max_health;
        return;
    }

    creature.health = std::clamp(creature.health, 0.0F, max_health);
}

auto horizontal_direction_or_fallback(const glm::vec3& value, const glm::vec3& fallback) noexcept -> glm::vec3 {
    const auto horizontal = glm::vec3 {value.x, 0.0F, value.z};
    if (glm::dot(horizontal, horizontal) > 1.0e-6F) {
        return glm::normalize(horizontal);
    }

    const auto fallback_horizontal = glm::vec3 {fallback.x, 0.0F, fallback.z};
    if (glm::dot(fallback_horizontal, fallback_horizontal) > 1.0e-6F) {
        return glm::normalize(fallback_horizontal);
    }

    return {0.0F, 0.0F, 1.0F};
}

auto ray_intersects_aabb(const glm::vec3& origin,
                         const glm::vec3& direction,
                         const glm::vec3& min_corner,
                         const glm::vec3& max_corner,
                         float max_distance) noexcept -> std::optional<float> {
    auto t_min = 0.0F;
    auto t_max = max_distance;

    for (int axis = 0; axis < 3; ++axis) {
        const auto ray_component = direction[axis];
        const auto origin_component = origin[axis];

        if (std::abs(ray_component) <= 1.0e-6F) {
            if (origin_component < min_corner[axis] || origin_component > max_corner[axis]) {
                return std::nullopt;
            }
            continue;
        }

        const auto inv_direction = 1.0F / ray_component;
        auto t1 = (min_corner[axis] - origin_component) * inv_direction;
        auto t2 = (max_corner[axis] - origin_component) * inv_direction;
        if (t1 > t2) {
            std::swap(t1, t2);
        }

        t_min = std::max(t_min, t1);
        t_max = std::min(t_max, t2);
        if (t_min > t_max) {
            return std::nullopt;
        }
    }

    if (t_min < 0.0F || t_min > max_distance) {
        return std::nullopt;
    }
    return t_min;
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
    creature.hurt_timer = 0.0F;
    creature.health = creature_max_health(anchor.species);
    const auto initial_hit_direction = direction_from_yaw(creature.yaw_radians);
    creature.hit_direction = {initial_hit_direction.x, 0.0F, initial_hit_direction.y};

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
    if (world.has_water(world_x, ground_y, world_z) ||
        !is_block_collidable(world.get_block(world_x, ground_y, world_z))) {
        return false;
    }
    return is_creature_airspace_clear(world, world_x, ground_y, world_z);
}

auto is_creature_airspace_clear(const World& world, int world_x, int ground_y, int world_z) -> bool {
    for (int y = ground_y + 1; y <= ground_y + 2; ++y) {
        if (!is_world_y_valid(y) ||
            world.has_water(world_x, y, world_z) ||
            is_block_collidable(world.get_block(world_x, y, world_z))) {
            return false;
        }
    }
    return true;
}

auto creature_body_blocker_count_at(const World& world,
                                    float center_x,
                                    int ground_y,
                                    float center_z,
                                    float body_radius,
                                    int clear_air_blocks) -> int {
    const std::array<glm::vec2, 5> sample_offsets {{
        {0.0F, 0.0F},
        {body_radius, 0.0F},
        {-body_radius, 0.0F},
        {0.0F, body_radius},
        {0.0F, -body_radius},
    }};

    int blockers = 0;
    for (const auto& offset : sample_offsets) {
        const auto sample_x = static_cast<int>(std::floor(center_x + offset.x));
        const auto sample_z = static_cast<int>(std::floor(center_z + offset.y));
        bool blocked = world.has_water(sample_x, ground_y, sample_z) ||
                       !is_block_collidable(world.get_block(sample_x, ground_y, sample_z));
        for (int clear_index = 1; !blocked && clear_index <= clear_air_blocks; ++clear_index) {
            const auto y = ground_y + clear_index;
            blocked = !is_world_y_valid(y) ||
                      world.has_water(sample_x, y, sample_z) ||
                      is_block_collidable(world.get_block(sample_x, y, sample_z));
        }
        if (blocked) {
            ++blockers;
        }
    }
    return blockers;
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
    if (choice < 0.26F) {
        creature.behavior_state = CreatureBehaviorState::Idle;
        creature.behavior_timer = 0.85F + next_unit(creature.behavior_seed) * 1.10F;
    } else if (choice < 0.66F) {
        creature.behavior_state = CreatureBehaviorState::Wander;
        creature.behavior_timer = 1.10F + next_unit(creature.behavior_seed) * 1.55F;
        creature.wander_heading = wrap_angle(creature.wander_heading + next_signed_unit(creature.behavior_seed) * 1.05F);
    } else if (choice < 0.86F) {
        creature.behavior_state = CreatureBehaviorState::Graze;
        creature.behavior_timer = 0.90F + next_unit(creature.behavior_seed) * 1.35F;
        creature.wander_heading = wrap_angle(creature.wander_heading + next_signed_unit(creature.behavior_seed) * 0.48F);
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

    const auto target_y = resolve_grounded_target_y(world, candidate.x, candidate.y, creature.position.y, preferred_floor_y);
    if (!target_y.has_value()) {
        return false;
    }
    const auto target_floor_y = static_cast<int>(std::floor(*target_y - kGroundSnapOffset + 0.01F));
    const auto body_radius = movement_body_radius_for(creature);
    const auto clear_air_blocks = movement_clear_air_blocks_for(creature);
    const auto candidate_body_blockers =
        creature_body_blocker_count_at(world, candidate.x, target_floor_y, candidate.y, body_radius, clear_air_blocks);
    if (candidate_body_blockers != 0) {
        const auto current_body_blockers = creature_body_blocker_count_at(
            world,
            creature.position.x,
            target_floor_y,
            creature.position.z,
            body_radius,
            clear_air_blocks);
        if (current_body_blockers == 0 || candidate_body_blockers > current_body_blockers) {
            return false;
        }
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

    constexpr std::array<float, 4> kStepScales {{1.0F, 0.62F, 0.36F, 0.18F}};
    if (aggressive) {
        for (const auto step_scale : kStepScales) {
            for (const auto heading_offset :
                 std::array<float, 13> {0.0F, 0.28F, -0.28F, 0.56F, -0.56F, 0.92F, -0.92F, 1.28F, -1.28F, 1.57F, -1.57F, 2.20F, -2.20F}) {
                const auto heading = wrap_angle(base_heading + heading_offset);
                if (try_move_grounded(
                        creature,
                        world,
                        direction_from_yaw(heading) * step_distance * step_scale,
                        roam_radius,
                        preferred_floor_y)) {
                    return {true, std::abs(heading_offset) > 0.05F, heading};
                }
            }
        }
        return {};
    }

    for (const auto step_scale : kStepScales) {
        for (const auto heading_offset : std::array<float, 5> {0.0F, 0.34F, -0.34F, 0.68F, -0.68F}) {
            const auto heading = wrap_angle(base_heading + heading_offset);
            if (try_move_grounded(
                    creature,
                    world,
                    direction_from_yaw(heading) * step_distance * step_scale,
                    roam_radius,
                    preferred_floor_y)) {
                return {true, std::abs(heading_offset) > 0.05F, heading};
            }
        }
    }
    return {};
}

auto try_move_grounded_towards(CreatureInstance& creature,
                               const World& world,
                               float base_heading,
                               float step_distance,
                               float roam_radius,
                               const glm::vec2& target_position,
                               const glm::vec2& preferred_direction,
                               bool aggressive,
                               std::optional<int> preferred_floor_y) -> SteeringMoveResult {
    if (step_distance <= 1.0e-6F) {
        return {};
    }

    const auto candidate_offsets = aggressive ?
        std::array<float, 15> {0.0F, 0.22F, -0.22F, 0.44F, -0.44F, 0.72F, -0.72F, 1.04F, -1.04F, 1.36F, -1.36F, 1.57F, -1.57F, 2.18F, -2.18F} :
        std::array<float, 15> {0.0F, 0.30F, -0.30F, 0.60F, -0.60F, 0.92F, -0.92F, 1.20F, -1.20F, 1.57F, -1.57F, 2.05F, -2.05F, 2.60F, -2.60F};

    auto best_score = std::numeric_limits<float>::max();
    auto best_creature = creature;
    SteeringMoveResult best_result {};
    const glm::vec2 before {creature.position.x, creature.position.z};
    const auto preferred = glm::dot(preferred_direction, preferred_direction) > 1.0e-6F ?
        glm::normalize(preferred_direction) :
        direction_from_yaw(base_heading);

    constexpr std::array<float, 4> kStepScales {{1.0F, 0.62F, 0.36F, 0.18F}};
    for (const auto step_scale : kStepScales) {
        for (const auto heading_offset : candidate_offsets) {
            const auto heading = wrap_angle(base_heading + heading_offset);
            auto probe = creature;
            if (!try_move_grounded(
                    probe,
                    world,
                    direction_from_yaw(heading) * step_distance * step_scale,
                    roam_radius,
                    preferred_floor_y)) {
                continue;
            }

            const glm::vec2 after {probe.position.x, probe.position.z};
            const auto to_target = target_position - after;
            const auto moved_delta = after - before;
            const auto moved_distance = glm::length(moved_delta);
            const auto moved_direction = moved_distance > 1.0e-6F ? moved_delta / moved_distance : direction_from_yaw(heading);
            const auto target_score = glm::dot(to_target, to_target);
            const auto alignment_bonus = glm::clamp(glm::dot(moved_direction, preferred), -1.0F, 1.0F);
            const auto offset_penalty = std::abs(heading_offset) * 0.055F;
            const auto short_step_penalty = (1.0F - step_scale) * 0.42F;
            const auto score = target_score + offset_penalty + short_step_penalty - alignment_bonus * kResidentStepLookahead;
            if (score < best_score) {
                best_score = score;
                best_creature = probe;
                best_result = {true, std::abs(heading_offset) > 0.05F, heading};
            }
        }
    }

    if (!best_result.moved) {
        return {};
    }

    creature.position = best_creature.position;
    creature.anchor = best_creature.anchor;
    return best_result;
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

    const auto clamped_dt = non_negative_finite(dt);
    update_spawn_suppressions(clamped_dt);
    const auto safe_player_position = finite_vec3_or(player_position, {});
    if (creatures_.capacity() < kCreatureMaxActiveCount) {
        creatures_.reserve(kCreatureMaxActiveCount);
    }
    if (render_instances_.capacity() < kCreatureMaxActiveCount + death_visuals_.size()) {
        render_instances_.reserve(kCreatureMaxActiveCount + death_visuals_.size());
    }

    population_sync_accumulator_ += clamped_dt;
    const auto population_center = world.world_to_chunk(
        static_cast<int>(std::floor(safe_player_position.x)),
        static_cast<int>(std::floor(safe_player_position.z)));
    const auto center_changed = !last_population_center_.has_value() || *last_population_center_ != population_center;
    const auto loaded_chunk_count = world.chunk_records().size();
    const auto loaded_chunks_changed = loaded_chunk_count != last_loaded_chunk_count_;
    if (population_sync_requested_ || center_changed || loaded_chunks_changed ||
        population_sync_accumulator_ >= kPopulationSyncIntervalSeconds) {
        // Je reserve le travail de population aux changements structurels et a
        // une cadence de 4 Hz; l'animation et les combats restent fluides a 60 Hz.
        sync_active_creatures(world, safe_player_position, cycle);
        prune_spawn_anchor_cache(world);
        population_sync_accumulator_ = 0.0F;
        population_sync_requested_ = false;
        last_population_center_ = population_center;
        last_loaded_chunk_count_ = loaded_chunk_count;
        ++audit_stats_.population_syncs;
    }

    const auto active_view = std::span<const CreatureInstance>(creatures_.data(), creatures_.size());
    for (auto& creature : creatures_) {
        update_creature(creature, clamped_dt, world, safe_player_position, environment, cycle, active_view);
    }

    update_death_visuals(clamped_dt);
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

auto CreatureSystem::raycast_first_creature(const glm::vec3& origin,
                                            const glm::vec3& direction,
                                            float max_distance) const -> CreatureRaycastResult {
    if (!is_finite_vec3(origin) ||
        !is_finite_vec3(direction) ||
        !std::isfinite(max_distance) ||
        max_distance <= 0.0F ||
        glm::dot(direction, direction) <= 1.0e-6F) {
        return {};
    }

    const auto ray_direction = glm::normalize(direction);
    auto best_index = creatures_.size();
    auto best_distance = std::numeric_limits<float>::max();
    for (std::size_t index = 0; index < creatures_.size(); ++index) {
        const auto& creature = creatures_[index];
        if (!std::isfinite(creature.health) || creature.health <= 0.0F) {
            continue;
        }

        const auto hitbox = hitbox_for(creature);
        const auto min_corner = creature.position + glm::vec3 {-hitbox.radius, 0.05F, -hitbox.radius};
        const auto max_corner = creature.position + glm::vec3 { hitbox.radius, hitbox.height, hitbox.radius};
        const auto hit_distance =
            ray_intersects_aabb(origin, ray_direction, min_corner, max_corner, max_distance);
        if (!hit_distance.has_value() || *hit_distance >= best_distance) {
            continue;
        }

        best_index = index;
        best_distance = *hit_distance;
    }

    if (best_index >= creatures_.size()) {
        return {};
    }

    const auto& creature = creatures_[best_index];
    return {
        true,
        creature_id_from_anchor(creature.anchor),
        creature.anchor.species,
        creature_disposition(creature),
        creature.position,
        best_distance,
    };
}

auto CreatureSystem::apply_damage(CreatureId target_id,
                                  float damage,
                                  CreatureDamageSource source,
                                  const glm::vec3& hit_direction) -> CreatureDamageResult {
    CreatureDamageResult result {};
    result.source =
        source == CreatureDamageSource::Player ||
        source == CreatureDamageSource::OldGuard ||
        source == CreatureDamageSource::Environment
            ? source
            : CreatureDamageSource::Environment;
    result.id = target_id;

    if (target_id == 0U || !std::isfinite(damage) || damage <= 0.0F) {
        return result;
    }

    const auto iterator = std::find_if(creatures_.begin(), creatures_.end(), [&](const CreatureInstance& creature) {
        return creature_id_from_anchor(creature.anchor) == target_id;
    });
    if (iterator == creatures_.end()) {
        return result;
    }

    const auto index = static_cast<std::size_t>(std::distance(creatures_.begin(), iterator));
    auto& creature = *iterator;
    ensure_creature_health(creature);
    if (creature.health <= 0.0F) {
        return result;
    }

    const auto safe_hit_direction =
        is_finite_vec3(hit_direction) ? hit_direction : glm::vec3 {0.0F, 0.0F, 1.0F};
    const auto impact_direction =
        horizontal_direction_or_fallback(safe_hit_direction, creature.hit_direction);
    const auto applied_damage = std::min(damage, creature.health);
    creature.health = std::max(0.0F, creature.health - applied_damage);
    creature.nervous_intensity = 1.0F;
    creature.attack_amount = std::max(creature.attack_amount, 0.82F);
    creature.hurt_timer = kCreatureHurtDuration;
    creature.hit_direction = impact_direction;
    creature.behavior_state = CreatureBehaviorState::Flee;
    creature.behavior_timer = std::max(creature.behavior_timer, 0.56F);

    result.hit = true;
    result.killed = creature.health <= 0.0F;
    result.grants_player_rewards = result.killed && result.source == CreatureDamageSource::Player;
    result.species = creature.anchor.species;
    result.disposition = creature_disposition(creature);
    result.position = creature.position;
    result.damage = applied_damage;
    result.remaining_health = creature.health;

    if (result.killed) {
        // Je conserve la meme mort et la meme suppression de respawn quelle
        // que soit la source; seule la recompense appartient au joueur.
        spawn_death_visual(creature, impact_direction);
        suppress_spawn_after_death(creature.anchor);
        if (is_resident_species(creature.anchor.species)) {
            remember_session_dead_resident(creature.anchor);
        }
        creatures_.erase(creatures_.begin() + static_cast<std::ptrdiff_t>(index));
        if (index < render_instances_.size()) {
            render_instances_.erase(render_instances_.begin() + static_cast<std::ptrdiff_t>(index));
        }
        ++audit_stats_.despawned;
        audit_stats_.active_creatures = creatures_.size();
    }

    return result;
}

auto CreatureSystem::try_damage_by_ray(const glm::vec3& origin,
                                       const glm::vec3& direction,
                                       float max_distance,
                                       float damage,
                                       CreatureDamageSource source) -> CreatureDamageResult {
    const auto hit = raycast_first_creature(origin, direction, max_distance);
    if (!hit.hit) {
        CreatureDamageResult miss {};
        miss.source = source;
        return miss;
    }

    auto result = apply_damage(hit.id, damage, source, hit.position - origin);
    result.distance = hit.distance;
    return result;
}

auto CreatureSystem::try_damage_from_player(const glm::vec3& origin,
                                            const glm::vec3& direction,
                                            float max_distance,
                                            float damage) -> CreatureDamageResult {
    return try_damage_by_ray(
        origin,
        direction,
        max_distance,
        damage,
        CreatureDamageSource::Player);
}

void CreatureSystem::set_secondary_population_interest(const glm::vec3& center,
                                                       int radius_chunks) noexcept {
    if (!is_finite_vec3(center)) {
        clear_secondary_population_interest();
        return;
    }

    // Je borne le centre avant tout floor/cast entier : meme FLT_MAX reste
    // ainsi representable quand je le convertis en coordonnees de chunk.
    const auto coordinate_limit = glm::vec3 {kCreaturePopulationInterestCoordinateLimit};
    const auto sanitized_center = glm::clamp(center, -coordinate_limit, coordinate_limit);
    CreaturePopulationInterest sanitized {
        sanitized_center,
        std::clamp(radius_chunks, 1, 16),
    };
    if (secondary_population_interest_.has_value()) {
        const auto previous_chunk_x = static_cast<int>(std::floor(
            secondary_population_interest_->center.x / static_cast<float>(kChunkSizeX)));
        const auto previous_chunk_z = static_cast<int>(std::floor(
            secondary_population_interest_->center.z / static_cast<float>(kChunkSizeZ)));
        const auto next_chunk_x =
            static_cast<int>(std::floor(sanitized.center.x / static_cast<float>(kChunkSizeX)));
        const auto next_chunk_z =
            static_cast<int>(std::floor(sanitized.center.z / static_cast<float>(kChunkSizeZ)));
        const auto structural_change =
            previous_chunk_x != next_chunk_x ||
            previous_chunk_z != next_chunk_z ||
            secondary_population_interest_->radius_chunks != sanitized.radius_chunks;
        secondary_population_interest_ = sanitized;
        population_sync_requested_ = population_sync_requested_ || structural_change;
        return;
    }

    secondary_population_interest_ = sanitized;
    population_sync_requested_ = true;
}

void CreatureSystem::clear_secondary_population_interest() noexcept {
    if (!secondary_population_interest_.has_value()) {
        return;
    }
    secondary_population_interest_.reset();
    population_sync_requested_ = true;
}

auto CreatureSystem::secondary_population_interest() const noexcept
    -> const std::optional<CreaturePopulationInterest>& {
    return secondary_population_interest_;
}

void CreatureSystem::set_settlement_residents(std::vector<CreatureSpawnAnchor> residents) {
    session_dead_residents_.clear();
    spawn_suppressions_.clear();
    settlement_residents_ = std::move(residents);
    population_sync_requested_ = true;
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
    for (auto& creature : creatures_) {
        sanitize_loaded_creature(creature);
    }
    attacks_.clear();
    death_visuals_.clear();
    spawn_suppressions_.clear();
    spawn_anchor_cache_.clear();
    population_sync_accumulator_ = 0.0F;
    population_sync_requested_ = true;
    last_population_center_.reset();
    last_loaded_chunk_count_ = 0;
    rebuild_render_instances(environment);
    audit_stats_ = {};
    audit_stats_.active_creatures = creatures_.size();
}

void CreatureSystem::clear() noexcept {
    creatures_.clear();
    render_instances_.clear();
    attacks_.clear();
    death_visuals_.clear();
    spawn_suppressions_.clear();
    settlement_residents_.clear();
    resident_profiles_.clear();
    session_dead_residents_.clear();
    resident_candidates_scratch_.clear();
    spawn_candidates_scratch_.clear();
    spawn_anchor_cache_.clear();
    last_population_center_.reset();
    secondary_population_interest_.reset();
    last_loaded_chunk_count_ = 0;
    population_sync_accumulator_ = 0.0F;
    population_sync_requested_ = true;
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
        if (!is_chunk_within_population_interest(
                center,
                iterator->anchor.chunk,
                kCreatureKeepAliveRadiusChunks,
                secondary_population_interest_,
                1) ||
            world.find_chunk(iterator->anchor.chunk) == nullptr) {
            ++audit_stats_.despawned;
            iterator = creatures_.erase(iterator);
            continue;
        }

        if (is_resident_species(iterator->anchor.species)) {
            if (is_session_dead_resident(iterator->anchor)) {
                ++audit_stats_.despawned;
                iterator = creatures_.erase(iterator);
                continue;
            }

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

        const auto refreshed_anchor = cached_spawn_anchor(world, iterator->anchor.chunk);
        if (!refreshed_anchor.has_value() || *refreshed_anchor != iterator->anchor) {
            ++audit_stats_.despawned;
            iterator = creatures_.erase(iterator);
            continue;
        }

        ++iterator;
    }

    auto& resident_candidates = resident_candidates_scratch_;
    resident_candidates.clear();
    if (resident_candidates.capacity() < resident_profiles_.size()) {
        resident_candidates.reserve(resident_profiles_.size());
    }
    for (const auto& profile : resident_profiles_) {
        if (!is_chunk_within_population_interest(
                center,
                profile.anchor.chunk,
                kCreatureActivationRadiusChunks,
                secondary_population_interest_) ||
            world.find_chunk(profile.anchor.chunk) == nullptr ||
            is_session_dead_resident(profile.anchor) ||
            is_spawn_suppressed(profile.anchor) ||
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

    const auto resident_count = static_cast<std::size_t>(
        std::count_if(creatures_.begin(), creatures_.end(), [](const CreatureInstance& creature) {
            return is_resident_species(creature.anchor.species);
        }));
    auto wildlife_count = creatures_.size() - resident_count;
    const auto reserved_resident_count = resident_count + resident_candidates.size();
    const auto wildlife_capacity =
        reserved_resident_count >= kCreatureMaxActiveCount ? 0U : kCreatureMaxActiveCount - reserved_resident_count;
    while (wildlife_count > wildlife_capacity) {
        auto farthest = creatures_.end();
        auto farthest_distance = -1.0F;
        for (auto iterator = creatures_.begin(); iterator != creatures_.end(); ++iterator) {
            if (is_resident_species(iterator->anchor.species)) {
                continue;
            }
            auto distance = horizontal_distance_squared(iterator->position, player_position);
            if (secondary_population_interest_.has_value()) {
                distance = std::min(
                    distance,
                    horizontal_distance_squared(
                        iterator->position,
                        secondary_population_interest_->center));
            }
            if (distance > farthest_distance) {
                farthest_distance = distance;
                farthest = iterator;
            }
        }
        if (farthest == creatures_.end()) {
            break;
        }
        creatures_.erase(farthest);
        --wildlife_count;
        ++audit_stats_.despawned;
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

    auto& candidates = spawn_candidates_scratch_;
    candidates.clear();
    auto maximum_candidate_count =
        static_cast<std::size_t>((kCreatureActivationRadiusChunks * 2 + 1) * (kCreatureActivationRadiusChunks * 2 + 1));
    if (secondary_population_interest_.has_value()) {
        const auto secondary_diameter = secondary_population_interest_->radius_chunks * 2 + 1;
        maximum_candidate_count += static_cast<std::size_t>(secondary_diameter * secondary_diameter);
    }
    if (candidates.capacity() < maximum_candidate_count) {
        candidates.reserve(maximum_candidate_count);
    }

    for (const auto& [coord, record] : world.chunk_records()) {
        (void)record;
        if (!is_chunk_within_population_interest(
                center,
                coord,
                kCreatureActivationRadiusChunks,
                secondary_population_interest_) ||
            find_creature(coord) != nullptr) {
            continue;
        }
        if (std::any_of(settlement_residents_.begin(), settlement_residents_.end(), [&](const CreatureSpawnAnchor& resident) {
                return resident.chunk == coord;
            })) {
            continue;
        }
        candidates.push_back(coord);
    }

    std::sort(candidates.begin(), candidates.end(), [&](const ChunkCoord& lhs, const ChunkCoord& rhs) {
        return chunk_distance_squared_to_population_interests(
                   lhs,
                   player_position,
                   secondary_population_interest_) <
               chunk_distance_squared_to_population_interests(
                   rhs,
                   player_position,
                   secondary_population_interest_);
    });

    for (const auto& candidate : candidates) {
        if (creatures_.size() >= kCreatureMaxActiveCount) {
            break;
        }

        const auto anchor = cached_spawn_anchor(world, candidate);
        if (!anchor.has_value() || is_spawn_suppressed(*anchor)) {
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
                                     const CreatureCycleState& cycle,
                                     std::span<const CreatureInstance> active_creatures) {
    ensure_creature_health(creature);
    creature.hurt_timer = std::max(0.0F, creature.hurt_timer - non_negative_finite(dt));
    if (creature.health <= 0.0F) {
        creature.motion_amount = 0.0F;
        creature.gaze_weight = 0.0F;
        creature.attack_amount = 0.0F;
        return;
    }

    if (is_resident_species(creature.anchor.species)) {
        if (const auto* profile = find_resident_profile(creature.anchor); profile != nullptr) {
            update_resident_creature(
                creature,
                *profile,
                dt,
                world,
                player_position,
                environment,
                active_creatures,
                audit_stats_);
            return;
        }
    }

    const auto tuning = tuning_for(creature.anchor.species);
    const auto resident_species = is_resident_species(creature.anchor.species);
    creature.phase = resident_species ? CreaturePhase::Day : cycle.phase;
    creature.morph_factor = resident_species ? 0.0F : cycle.morph_factor;
    creature.animation_time += non_negative_finite(dt);
    creature.behavior_timer -= dt;
    creature.attack_cooldown = std::max(0.0F, creature.attack_cooldown - non_negative_finite(dt));

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
    const auto melee_height_reachable = player_height_reachable_by(creature, player_position);
    const auto melee_attack_distance = attack_distance_for(creature);
    const auto melee_blocked_margin =
        is_large_night_creature(creature) ? kNightMonsterBlockedAttackMargin : kNightBlockedAttackMargin;
    bool close_melee_path_clear = false;
    if (melee_height_reachable &&
        player_distance_sq > 1.0e-6F &&
        player_distance <= melee_attack_distance + melee_blocked_margin) {
        close_melee_path_clear = has_clear_creature_attack_path(creature, world, player_position, player_distance_sq);
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
            case CreatureBehaviorState::Graze:
                desired_move = direction_from_yaw(creature.wander_heading) * tuning.day_speed * dt * 0.10F;
                desired_yaw = wrap_angle(creature.wander_heading + std::sin(creature.animation_time * 3.4F) * 0.10F);
                target_motion_amount = 0.16F;
                target_gaze_weight = 0.68F;
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
            player_distance <= melee_attack_distance + melee_blocked_margin &&
            (!melee_height_reachable || !close_melee_path_clear);
        if (player_detected || chase_memory_active || can_attack_player || close_but_blocked) {
            roam_radius = night_chase_roam_radius_for(creature, tuning, player_position);
        }

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
                    creature.position + glm::vec3 {
                        attack_direction.x * (is_large_night_creature(creature) ? 0.80F : 0.45F),
                        is_large_night_creature(creature) ? 1.55F : 0.65F,
                        attack_direction.y * (is_large_night_creature(creature) ? 0.80F : 0.45F),
                    },
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
    const auto rotated_yaw = rotate_towards(creature.yaw_radians, desired_yaw, turn_speed * non_negative_finite(dt));
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
            creature.behavior_state == CreatureBehaviorState::Flee ||
            creature.behavior_state == CreatureBehaviorState::Graze) {
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
                creature.behavior_state == CreatureBehaviorState::Graze ||
                creature.behavior_state == CreatureBehaviorState::Chase)) {
        creature.wander_heading = wrap_angle(creature.wander_heading + kPi * 0.75F);
    }
    if (desired_distance > 1.0e-6F && !moved) {
        target_motion_amount =
            std::min(target_motion_amount, hostile_night ? 0.34F : (morph_visible ? 0.24F : 0.16F));
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

void CreatureSystem::update_death_visuals(float dt) noexcept {
    if (death_visuals_.empty()) {
        return;
    }

    for (auto& visual : death_visuals_) {
        visual.age_seconds = std::min(visual.age_seconds + non_negative_finite(dt), visual.duration_seconds + 0.1F);
        visual.animation_time += non_negative_finite(dt) * 0.60F;
    }

    death_visuals_.erase(
        std::remove_if(death_visuals_.begin(), death_visuals_.end(), [](const CreatureDeathVisual& visual) {
            return visual.age_seconds >= visual.duration_seconds;
        }),
        death_visuals_.end());
}

void CreatureSystem::update_spawn_suppressions(float dt) noexcept {
    if (spawn_suppressions_.empty()) {
        return;
    }

    const auto clamped_dt = non_negative_finite(dt);
    for (auto& suppression : spawn_suppressions_) {
        suppression.remaining_seconds -= clamped_dt;
    }

    spawn_suppressions_.erase(
        std::remove_if(spawn_suppressions_.begin(), spawn_suppressions_.end(), [](const SpawnSuppression& suppression) {
            return suppression.remaining_seconds <= 0.0F;
        }),
        spawn_suppressions_.end());
}

void CreatureSystem::rebuild_render_instances(const EnvironmentState& environment) {
    render_instances_.clear();
    render_instances_.reserve(creatures_.size() + death_visuals_.size());
    const auto daylight_factor = std::clamp(finite_or(environment.daylight_factor, 1.0F), 0.0F, 1.0F);

    for (const auto& creature : creatures_) {
        const auto hurt_amount = glm::clamp(creature.hurt_timer / kCreatureHurtDuration, 0.0F, 1.0F);
        render_instances_.push_back({
            creature.anchor.species,
            creature.position,
            creature.yaw_radians,
            creature.animation_time,
            creature.morph_factor,
            daylight_factor,
            glm::clamp(std::max(creature.nervous_intensity, hurt_amount * 0.78F), 0.0F, 1.0F),
            creature.appearance_seed,
            creature.behavior_state,
            creature.phase,
            glm::clamp(creature.motion_amount, 0.0F, 1.0F),
            glm::clamp(creature.gaze_weight, 0.0F, 1.0F),
            glm::clamp(std::max(creature.attack_amount, hurt_amount * 0.50F), 0.0F, 1.0F),
            hurt_amount,
            0.0F,
            creature.hit_direction,
        });
    }

    for (const auto& visual : death_visuals_) {
        const auto death_progress = glm::clamp(
            visual.duration_seconds <= 1.0e-4F ? 1.0F : visual.age_seconds / visual.duration_seconds,
            0.0F,
            1.0F);
        const auto death_amount = death_progress * death_progress * (3.0F - 2.0F * death_progress);
        const auto hit_amount = glm::clamp(1.0F - death_progress / 0.30F, 0.0F, 1.0F);

        render_instances_.push_back({
            visual.anchor.species,
            visual.position,
            visual.yaw_radians,
            visual.animation_time,
            visual.morph_factor,
            visual.daylight_factor,
            glm::clamp(std::max(visual.tension, hit_amount * 0.88F), 0.0F, 1.0F),
            visual.appearance_seed,
            visual.behavior_state,
            visual.phase,
            glm::clamp(visual.motion_amount * (1.0F - death_amount), 0.0F, 1.0F),
            glm::clamp(visual.gaze_weight * (1.0F - death_amount), 0.0F, 1.0F),
            glm::clamp(visual.attack_amount * (1.0F - death_amount), 0.0F, 1.0F),
            hit_amount,
            death_amount,
            visual.hit_direction,
        });
    }
}

auto CreatureSystem::cached_spawn_anchor(const World& world, const ChunkCoord& coord)
    -> std::optional<CreatureSpawnAnchor> {
    if (world.find_chunk(coord) == nullptr) {
        spawn_anchor_cache_.erase(coord);
        return std::nullopt;
    }

    const auto revision = world.mesh_revision(coord);
    if (const auto cached = spawn_anchor_cache_.find(coord); cached != spawn_anchor_cache_.end()) {
        if (cached->second.mesh_revision == revision &&
            cached_anchor_is_still_valid(world, cached->second.anchor)) {
            return cached->second.anchor;
        }
    }

    ++audit_stats_.spawn_anchor_computations;
    const auto refreshed = compute_spawn_anchor(world, coord);
    if (!refreshed.has_value()) {
        // Je ne cache pas les echecs : un chunk encore en cours de preparation
        // doit pouvoir devenir eligible au prochain passage de population.
        spawn_anchor_cache_.erase(coord);
        return std::nullopt;
    }
    spawn_anchor_cache_.insert_or_assign(coord, SpawnAnchorCacheEntry {*refreshed, revision});
    return refreshed;
}

auto CreatureSystem::cached_anchor_is_still_valid(const World& world, const CreatureSpawnAnchor& anchor) const -> bool {
    const auto surface_y = world.loaded_surface_height(anchor.ground_block.x, anchor.ground_block.z);
    return surface_y.has_value() &&
           *surface_y == anchor.ground_block.y &&
           is_spawn_column_clear(world, anchor.ground_block.x, anchor.ground_block.y, anchor.ground_block.z);
}

void CreatureSystem::prune_spawn_anchor_cache(const World& world) {
    for (auto iterator = spawn_anchor_cache_.begin(); iterator != spawn_anchor_cache_.end();) {
        if (world.find_chunk(iterator->first) == nullptr) {
            iterator = spawn_anchor_cache_.erase(iterator);
        } else {
            ++iterator;
        }
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

auto CreatureSystem::find_creature(CreatureId id) -> CreatureInstance* {
    const auto iterator = std::find_if(creatures_.begin(), creatures_.end(), [&](const CreatureInstance& creature) {
        return creature_id_from_anchor(creature.anchor) == id;
    });
    return iterator == creatures_.end() ? nullptr : &(*iterator);
}

auto CreatureSystem::find_creature(CreatureId id) const -> const CreatureInstance* {
    const auto iterator = std::find_if(creatures_.begin(), creatures_.end(), [&](const CreatureInstance& creature) {
        return creature_id_from_anchor(creature.anchor) == id;
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

auto CreatureSystem::is_session_dead_resident(const CreatureSpawnAnchor& anchor) const -> bool {
    if (!is_resident_species(anchor.species)) {
        return false;
    }
    return std::any_of(session_dead_residents_.begin(), session_dead_residents_.end(), [&](const CreatureSpawnAnchor& dead_resident) {
        return same_resident_slot(dead_resident, anchor);
    });
}

auto CreatureSystem::is_spawn_suppressed(const CreatureSpawnAnchor& anchor) const -> bool {
    return std::any_of(spawn_suppressions_.begin(), spawn_suppressions_.end(), [&](const SpawnSuppression& suppression) {
        return same_resident_slot(suppression.anchor, anchor);
    });
}

void CreatureSystem::remember_session_dead_resident(const CreatureSpawnAnchor& anchor) {
    if (!is_resident_species(anchor.species) || is_session_dead_resident(anchor)) {
        return;
    }
    session_dead_residents_.push_back(anchor);
}

void CreatureSystem::suppress_spawn_after_death(const CreatureSpawnAnchor& anchor) {
    if (is_resident_species(anchor.species)) {
        return;
    }

    const auto existing = std::find_if(spawn_suppressions_.begin(), spawn_suppressions_.end(), [&](const SpawnSuppression& suppression) {
        return same_resident_slot(suppression.anchor, anchor);
    });
    if (existing != spawn_suppressions_.end()) {
        existing->remaining_seconds = std::max(existing->remaining_seconds, kCreatureSpawnSuppressionDuration);
        return;
    }

    spawn_suppressions_.push_back({anchor, kCreatureSpawnSuppressionDuration});
}

void CreatureSystem::spawn_death_visual(const CreatureInstance& creature, const glm::vec3& hit_direction) {
    CreatureDeathVisual visual {};
    visual.anchor = creature.anchor;
    visual.position = creature.position;
    visual.yaw_radians = creature.yaw_radians;
    visual.animation_time = creature.animation_time;
    visual.morph_factor = creature.morph_factor;
    visual.daylight_factor = 1.0F;
    visual.tension = std::max(creature.nervous_intensity, 0.82F);
    visual.appearance_seed = creature.appearance_seed;
    visual.behavior_state = creature.behavior_state;
    visual.phase = creature.phase;
    visual.motion_amount = creature.motion_amount;
    visual.gaze_weight = creature.gaze_weight;
    visual.attack_amount = creature.attack_amount;
    visual.age_seconds = 0.0F;
    visual.duration_seconds = kCreatureDeathVisualDuration;
    visual.hit_direction = horizontal_direction_or_fallback(hit_direction, creature.hit_direction);
    death_visuals_.push_back(visual);
}

} // namespace valcraft
