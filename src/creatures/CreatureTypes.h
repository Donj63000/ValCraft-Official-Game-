#pragma once

#include "world/Block.h"
#include "world/Environment.h"

#include <array>
#include <glm/vec3.hpp>

#include <cstddef>
#include <cstdint>

namespace valcraft {

inline constexpr std::size_t kCreatureResidentPatrolPointCount = 4;

enum class CreatureSpecies : std::uint8_t {
    Pig = 0,
    Cow = 1,
    Sheep = 2,
    Villager = 3,
};

inline constexpr auto creature_max_health(CreatureSpecies species) noexcept -> float {
    switch (species) {
    case CreatureSpecies::Pig:
        return 8.0F;
    case CreatureSpecies::Cow:
        return 12.0F;
    case CreatureSpecies::Villager:
        return 14.0F;
    case CreatureSpecies::Sheep:
    default:
        return 8.0F;
    }
}

enum class CreatureResidentRole : std::uint8_t {
    Gardener = 0,
    Merchant = 1,
    Artisan = 2,
    Elder = 3,
};

enum class CreatureBehaviorState : std::uint8_t {
    Idle = 0,
    Wander = 1,
    Sniff = 2,
    Flee = 3,
    Lurk = 4,
    Stare = 5,
    Twitch = 6,
    Chase = 7,
    Strike = 8,
    Graze = 9,
    Work = 10,
    Socialize = 11,
    Sleep = 12,
    ReturnHome = 13,
};

struct CreatureSpawnAnchor {
    ChunkCoord chunk {};
    BlockCoord ground_block {};
    glm::vec3 spawn_position {0.0F};
    CreatureSpecies species = CreatureSpecies::Pig;
    float roam_radius = 0.0F;
    std::array<glm::vec3, kCreatureResidentPatrolPointCount> patrol_points {};
    std::uint8_t patrol_point_count = 0;
};

inline auto operator==(const CreatureSpawnAnchor& lhs, const CreatureSpawnAnchor& rhs) noexcept -> bool {
    if (lhs.chunk != rhs.chunk ||
        lhs.ground_block != rhs.ground_block ||
        lhs.spawn_position.x != rhs.spawn_position.x ||
        lhs.spawn_position.y != rhs.spawn_position.y ||
        lhs.spawn_position.z != rhs.spawn_position.z ||
        lhs.species != rhs.species ||
        lhs.roam_radius != rhs.roam_radius ||
        lhs.patrol_point_count != rhs.patrol_point_count) {
        return false;
    }

    for (std::size_t index = 0; index < lhs.patrol_points.size(); ++index) {
        if (lhs.patrol_points[index].x != rhs.patrol_points[index].x ||
            lhs.patrol_points[index].y != rhs.patrol_points[index].y ||
            lhs.patrol_points[index].z != rhs.patrol_points[index].z) {
            return false;
        }
    }

    return true;
}

struct CreatureInstance {
    CreatureSpawnAnchor anchor {};
    glm::vec3 position {0.0F};
    float yaw_radians = 0.0F;
    float behavior_timer = 0.0F;
    float animation_time = 0.0F;
    float wander_heading = 0.0F;
    float nervous_intensity = 0.0F;
    std::uint32_t behavior_seed = 0;
    std::uint32_t appearance_seed = 0;
    CreatureBehaviorState behavior_state = CreatureBehaviorState::Idle;
    CreaturePhase phase = CreaturePhase::Day;
    float morph_factor = 0.0F;
    float motion_amount = 0.0F;
    float gaze_weight = 0.0F;
    float attack_cooldown = 0.0F;
    float attack_amount = 0.0F;
    float hurt_timer = 0.0F;
    float health = creature_max_health(CreatureSpecies::Pig);
    glm::vec3 hit_direction {0.0F, 0.0F, 1.0F};
    std::uint8_t resident_target_index = 0;
};

struct CreatureRenderInstance {
    CreatureSpecies species = CreatureSpecies::Pig;
    glm::vec3 position {0.0F};
    float yaw_radians = 0.0F;
    float animation_time = 0.0F;
    float morph_factor = 0.0F;
    float daylight_factor = 1.0F;
    float tension = 0.0F;
    std::uint32_t appearance_seed = 0;
    CreatureBehaviorState behavior_state = CreatureBehaviorState::Idle;
    CreaturePhase phase = CreaturePhase::Day;
    float motion_amount = 0.0F;
    float gaze_weight = 0.0F;
    float attack_amount = 0.0F;
    float hurt_amount = 0.0F;
    float death_amount = 0.0F;
    glm::vec3 hit_direction {0.0F, 0.0F, 1.0F};
};

struct CreatureAttackEvent {
    CreatureSpecies species = CreatureSpecies::Pig;
    glm::vec3 origin {0.0F};
    float damage = 0.0F;
};

} // namespace valcraft
