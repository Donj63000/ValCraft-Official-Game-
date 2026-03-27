#pragma once

#include "world/Block.h"
#include "world/Environment.h"

#include <glm/vec3.hpp>

#include <cstdint>

namespace valcraft {

enum class CreatureSpecies : std::uint8_t {
    Pig = 0,
    Cow = 1,
    Sheep = 2,
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
};

struct CreatureSpawnAnchor {
    ChunkCoord chunk {};
    BlockCoord ground_block {};
    glm::vec3 spawn_position {0.0F};
    CreatureSpecies species = CreatureSpecies::Pig;
};

inline auto operator==(const CreatureSpawnAnchor& lhs, const CreatureSpawnAnchor& rhs) noexcept -> bool {
    return lhs.chunk == rhs.chunk &&
           lhs.ground_block == rhs.ground_block &&
           lhs.spawn_position.x == rhs.spawn_position.x &&
           lhs.spawn_position.y == rhs.spawn_position.y &&
           lhs.spawn_position.z == rhs.spawn_position.z &&
           lhs.species == rhs.species;
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
};

struct CreatureAttackEvent {
    CreatureSpecies species = CreatureSpecies::Pig;
    glm::vec3 origin {0.0F};
    float damage = 0.0F;
};

} // namespace valcraft
