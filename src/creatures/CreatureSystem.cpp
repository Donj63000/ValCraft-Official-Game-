#include "creatures/CreatureSystem.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/vec2.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
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

auto tuning_for(CreatureSpecies species) noexcept -> SpeciesTuning {
    switch (species) {
    case CreatureSpecies::Pig:
        return {1.05F, 2.30F, 0.58F, 1.65F, 4.25F, 5.20F, 10.25F};
    case CreatureSpecies::Cow:
        return {0.92F, 1.95F, 0.52F, 1.45F, 4.80F, 5.80F, 11.25F};
    case CreatureSpecies::Sheep:
    default:
        return {0.88F, 2.05F, 0.54F, 1.52F, 4.40F, 5.50F, 10.75F};
    }
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

auto grounded_target_y(const World& world, int world_x, int world_z, float current_y) -> std::optional<float> {
    const auto ground_y = world.loaded_surface_height(world_x, world_z);
    if (!ground_y.has_value()) {
        return std::nullopt;
    }
    if (!is_spawn_column_clear(world, world_x, *ground_y, world_z)) {
        return std::nullopt;
    }

    const auto target_y = static_cast<float>(*ground_y) + kGroundSnapOffset;
    if (std::abs(target_y - current_y) > kMaxStepHeight) {
        return std::nullopt;
    }
    return target_y;
}

auto try_move_grounded(CreatureInstance& creature,
                       const World& world,
                       const glm::vec2& desired_delta,
                       float roam_radius) -> bool {
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
    const auto target_y = grounded_target_y(world, world_x, world_z, creature.position.y);
    if (!target_y.has_value()) {
        return false;
    }

    creature.position = glm::vec3 {candidate.x, *target_y, candidate.y};
    return true;
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

} // namespace

void CreatureSystem::update(float dt,
                            const World& world,
                            const glm::vec3& player_position,
                            const EnvironmentState& environment,
                            const CreatureCycleState& cycle) {
    attacks_.clear();
    sync_active_creatures(world, player_position, cycle);

    for (auto& creature : creatures_) {
        update_creature(creature, dt, world, player_position, environment, cycle);
    }

    rebuild_render_instances(environment);
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

void CreatureSystem::sync_active_creatures(const World& world,
                                           const glm::vec3& player_position,
                                           const CreatureCycleState& cycle) {
    const auto center = world.world_to_chunk(
        static_cast<int>(std::floor(player_position.x)),
        static_cast<int>(std::floor(player_position.z)));

    for (auto iterator = creatures_.begin(); iterator != creatures_.end();) {
        if (!is_chunk_within_radius(center, iterator->anchor.chunk, kCreatureKeepAliveRadiusChunks) ||
            world.find_chunk(iterator->anchor.chunk) == nullptr) {
            iterator = creatures_.erase(iterator);
            continue;
        }

        const auto refreshed_anchor = compute_spawn_anchor(world, iterator->anchor.chunk);
        if (!refreshed_anchor.has_value() || *refreshed_anchor != iterator->anchor) {
            iterator = creatures_.erase(iterator);
            continue;
        }

        ++iterator;
    }

    if (creatures_.size() > kCreatureMaxActiveCount) {
        std::sort(creatures_.begin(), creatures_.end(), [&](const CreatureInstance& lhs, const CreatureInstance& rhs) {
            const auto lhs_distance = horizontal_distance_squared(lhs.position, player_position);
            const auto rhs_distance = horizontal_distance_squared(rhs.position, player_position);
            return lhs_distance < rhs_distance;
        });
        creatures_.resize(kCreatureMaxActiveCount);
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

        auto seed = hash_coords(candidate.coord.x, candidate.coord.z, static_cast<std::uint32_t>(world.seed()));
        CreatureInstance creature {};
        creature.anchor = *anchor;
        creature.position = anchor->spawn_position;
        creature.yaw_radians = next_unit(seed) * kTwoPi - kPi;
        creature.wander_heading = creature.yaw_radians;
        creature.behavior_timer = 0.0F;
        creature.animation_time = next_unit(seed) * 5.0F;
        creature.nervous_intensity = 0.0F;
        creature.behavior_seed = hash_coords(candidate.coord.x * 3, candidate.coord.z * 7, seed ^ 0xA53C9E1BU);
        creature.appearance_seed = hash_coords(candidate.coord.x * 11, candidate.coord.z * 5, seed ^ 0x6C8E9CF5U);
        creature.phase = cycle.phase;
        creature.morph_factor = cycle.morph_factor;
        creature.behavior_state = is_hostile_night(cycle) ? CreatureBehaviorState::Lurk : CreatureBehaviorState::Idle;
        creature.motion_amount = is_morph_visible(cycle) ? 0.18F : 0.10F;
        creature.gaze_weight = is_morph_visible(cycle) ? 0.48F : 0.16F;
        creature.attack_cooldown = next_unit(seed) * 0.35F;
        creature.attack_amount = 0.0F;
        creatures_.push_back(creature);
    }
}

void CreatureSystem::update_creature(CreatureInstance& creature,
                                     float dt,
                                     const World& world,
                                     const glm::vec3& player_position,
                                     const EnvironmentState& environment,
                                     const CreatureCycleState& cycle) {
    const auto tuning = tuning_for(creature.anchor.species);
    creature.phase = cycle.phase;
    creature.morph_factor = cycle.morph_factor;
    creature.animation_time += std::max(dt, 0.0F);
    creature.behavior_timer -= dt;
    creature.attack_cooldown = std::max(0.0F, creature.attack_cooldown - std::max(dt, 0.0F));

    const auto morph_visible = is_morph_visible(cycle);
    const auto hostile_night = is_hostile_night(cycle);
    const auto dawn_recover = cycle.phase == CreaturePhase::DawnRecover;
    const auto to_player = glm::vec2 {
        player_position.x - creature.position.x,
        player_position.z - creature.position.z,
    };
    const auto player_distance_sq = glm::dot(to_player, to_player);
    const auto player_distance = std::sqrt(std::max(player_distance_sq, 0.0F));
    const auto player_distance_factor = glm::clamp(1.0F - player_distance / kNightDetectionDistance, 0.0F, 1.0F);

    glm::vec2 desired_move {0.0F};
    auto desired_yaw = creature.yaw_radians;
    auto roam_radius = tuning.day_roam_radius;
    float target_motion_amount = morph_visible ? 0.22F : 0.08F;
    float target_gaze_weight = morph_visible ? 0.52F : 0.18F;
    float target_attack_amount = 0.0F;

    if (!morph_visible) {
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

        if (player_distance <= kNightAttackDistance && player_distance_sq > 1.0e-6F) {
            const auto attack_direction = glm::normalize(to_player);
            desired_yaw = yaw_from_direction(attack_direction);
            creature.behavior_state = CreatureBehaviorState::Strike;
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
        } else if (player_distance < kNightDetectionDistance && player_distance_sq > 1.0e-6F) {
            const auto chase_direction = glm::normalize(to_player);
            creature.behavior_state = CreatureBehaviorState::Chase;
            desired_move = chase_direction * tuning.chase_speed * dt;
            desired_yaw = yaw_from_direction(chase_direction);
            target_motion_amount = 0.86F;
            target_gaze_weight = 0.90F;
            target_attack_amount = 0.44F;
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

    if (glm::dot(desired_move, desired_move) > 1.0e-6F) {
        const auto moved = try_move_grounded(creature, world, desired_move, roam_radius);
        if (!moved &&
            (creature.behavior_state == CreatureBehaviorState::Wander ||
             creature.behavior_state == CreatureBehaviorState::Lurk ||
             creature.behavior_state == CreatureBehaviorState::Flee ||
             creature.behavior_state == CreatureBehaviorState::Chase)) {
            creature.wander_heading = wrap_angle(creature.wander_heading + kPi * 0.75F);
        }
    }

    if (dt > 0.0F) {
        const auto reference_speed =
            creature.behavior_state == CreatureBehaviorState::Chase ? tuning.chase_speed :
            (morph_visible ? tuning.lurk_speed : tuning.day_speed);
        const auto reference_distance = std::max(reference_speed * dt, 0.001F);
        const auto desired_motion = glm::clamp(glm::length(desired_move) / reference_distance, 0.0F, 1.0F);
        target_motion_amount = std::max(target_motion_amount, desired_motion);
    }

    const auto turn_speed = hostile_night ? 8.8F : (morph_visible ? 7.6F : 5.5F);
    creature.yaw_radians = rotate_towards(creature.yaw_radians, desired_yaw, turn_speed * std::max(dt, 0.0F));

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

} // namespace valcraft
