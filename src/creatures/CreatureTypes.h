#pragma once

#include "world/Block.h"
#include "world/Environment.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <glm/vec3.hpp>

#include <cstddef>
#include <cstdint>

namespace valcraft {

inline constexpr std::size_t kCreatureResidentPatrolPointCount = 4;
using CreatureId = std::uint64_t;

enum class CreatureSpecies : std::uint8_t {
    Pig = 0,
    Cow = 1,
    Sheep = 2,
    Villager = 3,
};

enum class ThreatRank : std::uint8_t {
    Zero = 0,
    One = 1,
    Two = 2,
    Three = 3,
    Four = 4,
    Five = 5,
    Six = 6,
};

enum class EntityWeight : std::uint8_t {
    Light = 0,
    Normal = 1,
    Heavy = 2,
    Boss = 3,
};

enum class Faction : std::uint8_t {
    Neutral = 0,
    Hostile = 1,
    Player = 2,
    Ally = 3,
};

struct StatusResistance {
    float knockback_multiplier = 1.0F;
    float stun_duration_multiplier = 1.0F;
};

enum class AbilityTargetTags : std::uint32_t {
    None = 0U,
    Creature = 1U << 0U,
    Living = 1U << 1U,
    Damageable = 1U << 2U,
    Neutral = 1U << 3U,
    Hostile = 1U << 4U,
    Wildlife = 1U << 5U,
    Civilian = 1U << 6U,
    Light = 1U << 7U,
    Normal = 1U << 8U,
    Heavy = 1U << 9U,
    Boss = 1U << 10U,
};

[[nodiscard]] inline constexpr auto operator|(
    AbilityTargetTags lhs,
    AbilityTargetTags rhs) noexcept -> AbilityTargetTags {
    return static_cast<AbilityTargetTags>(
        static_cast<std::uint32_t>(lhs) |
        static_cast<std::uint32_t>(rhs));
}

[[nodiscard]] inline constexpr auto operator&(
    AbilityTargetTags lhs,
    AbilityTargetTags rhs) noexcept -> AbilityTargetTags {
    return static_cast<AbilityTargetTags>(
        static_cast<std::uint32_t>(lhs) &
        static_cast<std::uint32_t>(rhs));
}

inline constexpr auto operator|=(
    AbilityTargetTags& lhs,
    AbilityTargetTags rhs) noexcept -> AbilityTargetTags& {
    lhs = lhs | rhs;
    return lhs;
}

[[nodiscard]] inline constexpr auto ability_target_has_tag(
    AbilityTargetTags tags,
    AbilityTargetTags expected) noexcept -> bool {
    return (tags & expected) == expected;
}

struct ExperienceReward {
    std::uint32_t experience_points = 0U;
};

struct CreatureCombatProfile {
    ThreatRank threat_rank = ThreatRank::Zero;
    EntityWeight weight = EntityWeight::Light;
    Faction faction = Faction::Neutral;
    StatusResistance status_resistance {};
    AbilityTargetTags target_tags =
        AbilityTargetTags::Creature |
        AbilityTargetTags::Living |
        AbilityTargetTags::Damageable |
        AbilityTargetTags::Neutral |
        AbilityTargetTags::Wildlife |
        AbilityTargetTags::Light;
    ExperienceReward experience_reward {};
    float maximum_health = 8.0F;
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

[[nodiscard]] inline constexpr auto status_resistance_for(
    EntityWeight weight) noexcept -> StatusResistance {
    switch (weight) {
    case EntityWeight::Normal:
        return {0.70F, 0.80F};
    case EntityWeight::Heavy:
        return {0.20F, 0.40F};
    case EntityWeight::Boss:
        return {0.0F, 0.15F};
    case EntityWeight::Light:
    default:
        return {1.0F, 1.0F};
    }
}

[[nodiscard]] inline constexpr auto deterministic_experience_reward(
    ThreatRank threat_rank,
    EntityWeight weight,
    Faction faction,
    bool ordinary_neutral_wildlife = false,
    std::uint32_t explicit_boss_reward = 0U) noexcept -> ExperienceReward {
    if (weight == EntityWeight::Boss) {
        // Je refuse d'inventer une valeur de boss : lorsqu'elle est fournie,
        // je la maintiens dans la plage d'equilibrage prevue par le plan.
        return {
            explicit_boss_reward == 0U
                ? 0U
                : std::clamp(explicit_boss_reward, 500U, 3'000U),
        };
    }
    if (faction != Faction::Hostile) {
        return {ordinary_neutral_wildlife ? 2U : 0U};
    }

    switch (threat_rank) {
    case ThreatRank::One:
        return {15U};
    case ThreatRank::Two:
        return {30U};
    case ThreatRank::Three:
        return {55U};
    case ThreatRank::Four:
        return {95U};
    case ThreatRank::Five:
        return {160U};
    case ThreatRank::Six:
        return {240U};
    case ThreatRank::Zero:
    default:
        return {0U};
    }
}

[[nodiscard]] inline auto resolved_stun_duration(
    const StatusResistance& resistance,
    float requested_duration_seconds,
    float seconds_since_previous_stun) noexcept -> float {
    if (!std::isfinite(requested_duration_seconds) ||
        requested_duration_seconds <= 0.0F ||
        !std::isfinite(resistance.stun_duration_multiplier)) {
        return 0.0F;
    }

    const auto base_duration =
        requested_duration_seconds *
        std::clamp(resistance.stun_duration_multiplier, 0.0F, 1.0F);
    if (!std::isfinite(seconds_since_previous_stun) ||
        seconds_since_previous_stun >= 9.0F) {
        return base_duration;
    }
    if (seconds_since_previous_stun < 0.0F) {
        return base_duration;
    }
    if (seconds_since_previous_stun < 3.0F) {
        return 0.0F;
    }

    // Je garde trois secondes d'immunite, puis je divise par deux les
    // etourdissements recus pendant les six secondes suivantes.
    return base_duration * 0.5F;
}

[[nodiscard]] inline constexpr auto weight_target_tag(
    EntityWeight weight) noexcept -> AbilityTargetTags {
    switch (weight) {
    case EntityWeight::Normal:
        return AbilityTargetTags::Normal;
    case EntityWeight::Heavy:
        return AbilityTargetTags::Heavy;
    case EntityWeight::Boss:
        return AbilityTargetTags::Boss;
    case EntityWeight::Light:
    default:
        return AbilityTargetTags::Light;
    }
}

[[nodiscard]] inline constexpr auto creature_combat_profile(
    CreatureSpecies species,
    CreaturePhase phase) noexcept -> CreatureCombatProfile {
    const auto safe_species =
        static_cast<std::uint8_t>(species) <=
                static_cast<std::uint8_t>(CreatureSpecies::Villager)
            ? species
            : CreatureSpecies::Pig;
    const auto night_hostile =
        phase == CreaturePhase::Night &&
        safe_species != CreatureSpecies::Villager;
    if (night_hostile) {
        const auto threat_rank =
            safe_species == CreatureSpecies::Pig
                ? ThreatRank::One
                : (safe_species == CreatureSpecies::Sheep
                       ? ThreatRank::Two
                       : ThreatRank::Three);
        const auto weight =
            safe_species == CreatureSpecies::Pig
                ? EntityWeight::Light
                : (safe_species == CreatureSpecies::Sheep
                       ? EntityWeight::Normal
                       : EntityWeight::Heavy);
        const auto maximum_health =
            safe_species == CreatureSpecies::Pig
                ? 20.0F
                : (safe_species == CreatureSpecies::Sheep ? 30.0F : 44.0F);
        return {
            threat_rank,
            weight,
            Faction::Hostile,
            status_resistance_for(weight),
            AbilityTargetTags::Creature |
                AbilityTargetTags::Living |
                AbilityTargetTags::Damageable |
                AbilityTargetTags::Hostile |
                AbilityTargetTags::Wildlife |
                weight_target_tag(weight),
            deterministic_experience_reward(
                threat_rank,
                weight,
                Faction::Hostile),
            maximum_health,
        };
    }

    const auto civilian =
        safe_species == CreatureSpecies::Villager;
    const auto weight =
        safe_species == CreatureSpecies::Cow
            ? EntityWeight::Heavy
            : (safe_species == CreatureSpecies::Sheep
                   ? EntityWeight::Normal
                   : EntityWeight::Light);
    return {
        ThreatRank::Zero,
        weight,
        Faction::Neutral,
        status_resistance_for(weight),
        AbilityTargetTags::Creature |
            AbilityTargetTags::Living |
            AbilityTargetTags::Damageable |
            AbilityTargetTags::Neutral |
            (civilian
                 ? AbilityTargetTags::Civilian
                 : AbilityTargetTags::Wildlife) |
            weight_target_tag(weight),
        deterministic_experience_reward(
            ThreatRank::Zero,
            weight,
            Faction::Neutral,
            !civilian),
        creature_max_health(safe_species),
    };
}

[[nodiscard]] inline constexpr auto creature_max_health(
    CreatureSpecies species,
    CreaturePhase phase) noexcept -> float {
    return creature_combat_profile(species, phase).maximum_health;
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

enum class CreatureDisposition : std::uint8_t {
    Neutral = 0,
    Hostile = 1,
};

enum class CreatureDamageSource : std::uint8_t {
    Player = 0,
    OldGuard = 1,
    Environment = 2,
    PlayerAbility = 3,
    PlayerSummon = 4,
    PlayerConstruct = 5,
};

[[nodiscard]] inline constexpr auto creature_damage_source_has_player_owner(
    CreatureDamageSource source) noexcept -> bool {
    return source == CreatureDamageSource::Player ||
           source == CreatureDamageSource::PlayerAbility ||
           source == CreatureDamageSource::PlayerSummon ||
           source == CreatureDamageSource::PlayerConstruct;
}

struct CreatureSpawnAnchor {
    ChunkCoord chunk {};
    BlockCoord ground_block {};
    glm::vec3 spawn_position {0.0F};
    CreatureSpecies species = CreatureSpecies::Pig;
    float roam_radius = 0.0F;
    std::array<glm::vec3, kCreatureResidentPatrolPointCount> patrol_points {};
    std::uint8_t patrol_point_count = 0;
};

inline constexpr auto creature_id_from_anchor(const CreatureSpawnAnchor& anchor) noexcept -> CreatureId {
    // Je derive l'identite des seules donnees entieres stables de l'ancre :
    // une correction de hauteur ou de ronde ne peut donc pas changer la cible.
    auto hash = UINT64_C(1469598103934665603);
    const auto mix = [&hash](std::uint32_t value) constexpr noexcept {
        for (unsigned int shift = 0; shift < 32U; shift += 8U) {
            hash ^= static_cast<std::uint8_t>(value >> shift);
            hash *= UINT64_C(1099511628211);
        }
    };
    mix(static_cast<std::uint32_t>(anchor.chunk.x));
    mix(static_cast<std::uint32_t>(anchor.chunk.z));
    mix(static_cast<std::uint32_t>(anchor.ground_block.x));
    mix(static_cast<std::uint32_t>(anchor.ground_block.y));
    mix(static_cast<std::uint32_t>(anchor.ground_block.z));
    mix(static_cast<std::uint32_t>(anchor.species));
    return hash == 0U ? 1U : hash;
}

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

    // Je garde ces donnees comme cache d'execution uniquement : elles ne font
    // pas partie du format de sauvegarde et sont reconstruites au chargement.
    glm::vec3 resident_cached_target {0.0F};
    float resident_target_refresh_timer = 0.0F;
    float resident_cached_heading = 0.0F;
    std::uint8_t resident_cached_phase = 0;
    bool resident_target_valid = false;
    bool resident_heading_valid = false;
    float combat_profile_max_health = 0.0F;
};

[[nodiscard]] inline auto creature_combat_profile(
    const CreatureInstance& creature) noexcept -> CreatureCombatProfile {
    // Je ne rends hostile que la transformation nocturne achevee, comme le
    // comportement historique. Un morph corrompu ou incomplet reste neutre.
    const auto effective_phase =
        creature.phase == CreaturePhase::Night &&
                std::isfinite(creature.morph_factor) &&
                creature.morph_factor >= 0.999F
            ? CreaturePhase::Night
            : CreaturePhase::Day;
    return creature_combat_profile(
        creature.anchor.species,
        effective_phase);
}

inline auto creature_disposition(const CreatureInstance& creature) noexcept -> CreatureDisposition {
    return creature_combat_profile(creature).faction == Faction::Hostile
        ? CreatureDisposition::Hostile
        : CreatureDisposition::Neutral;
}

inline auto is_hostile_creature(const CreatureInstance& creature) noexcept -> bool {
    return creature_disposition(creature) == CreatureDisposition::Hostile;
}

struct CreatureRaycastResult {
    bool hit = false;
    CreatureId id = 0;
    CreatureSpecies species = CreatureSpecies::Pig;
    CreatureDisposition disposition = CreatureDisposition::Neutral;
    glm::vec3 position {0.0F};
    float distance = 0.0F;
    CreatureCombatProfile combat_profile {};
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

enum class CreatureAttackKind : std::uint8_t {
    Melee = 0,
    Projectile = 1,
};

struct CreatureAttackEvent {
    CreatureSpecies species = CreatureSpecies::Pig;
    glm::vec3 origin {0.0F};
    float damage = 0.0F;
    CreatureAttackKind kind =
        CreatureAttackKind::Melee;
    glm::vec3 direction {0.0F, 0.0F, 1.0F};
    std::uint64_t target_id = 0U;
};

} // namespace valcraft
