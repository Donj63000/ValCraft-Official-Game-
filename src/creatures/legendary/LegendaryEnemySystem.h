#pragma once

#include "creatures/CreatureTypes.h"

#include <glm/vec3.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace valcraft {

using LegendaryEnemyId = std::uint64_t;

inline constexpr std::size_t kMaximumLegendaryEnemies = 32U;
inline constexpr std::size_t kMaximumLegendaryEnemyEvents = 128U;
inline constexpr float kLegendaryEnemyFixedStepSeconds = 1.0F / 60.0F;

enum class TemporaryPersistencePolicy : std::uint8_t {
    NeverSaved = 0,
};

enum class LegendaryEnemyArchetype : std::uint8_t {
    CorruptedBrute = 0,
    SwiftHunter,
    ArmoredGuard,
    AstralCreature,
    ForgeGuardian,
    ArenaMinion,
    AstralBoss,
};

enum class LegendaryEnemyBehavior : std::uint8_t {
    Idle = 0,
    Approach,
    Strafe,
    Telegraph,
    Attack,
    Recover,
    Staggered,
    Dead,
};

enum class AstralHitInteraction : std::uint8_t {
    Physical = 0,
    Deflected,
    PartiallyEffective,
    FullyEffective,
};

struct LegendaryEnemyProfile {
    LegendaryEnemyArchetype archetype =
        LegendaryEnemyArchetype::CorruptedBrute;
    ThreatRank threat_rank = ThreatRank::One;
    EntityWeight weight = EntityWeight::Light;
    ExperienceReward reward {};
    float minimum_health = 1.0F;
    float maximum_health = 1.0F;
    float maximum_armor = 0.0F;
    float maximum_stagger = 1.0F;
    float movement_speed = 1.0F;
    float attack_range = 1.0F;
    float attack_damage = 1.0F;
    float attack_windup_seconds = 0.5F;
    float attack_recovery_seconds = 0.5F;
    float frontal_armor_reduction = 0.0F;
    float rear_armor_reduction = 0.0F;
    float stagger_susceptibility = 1.0F;
    bool corrupted = false;
    bool astral = false;
    bool temporary_reward_suppressed = false;

    auto operator==(const LegendaryEnemyProfile&) const -> bool = default;
};

[[nodiscard]] auto legendary_enemy_profile(
    LegendaryEnemyArchetype archetype) noexcept
    -> LegendaryEnemyProfile;

enum class LegendaryEnemySpawnError : std::uint8_t {
    None = 0,
    CapacityReached,
    InvalidArchetype,
    InvalidPosition,
    InvalidFacing,
};

struct LegendaryEnemySpawnRequest {
    LegendaryEnemyArchetype archetype =
        LegendaryEnemyArchetype::CorruptedBrute;
    std::uint32_t deterministic_seed = 0U;
    glm::vec3 position {};
    float facing_yaw_radians = 0.0F;
};

struct LegendaryEnemySpawnResult {
    bool spawned = false;
    LegendaryEnemySpawnError error =
        LegendaryEnemySpawnError::None;
    LegendaryEnemyId id = 0U;
    float maximum_health = 0.0F;
};

struct LegendaryEnemyWorldInput {
    glm::vec3 target_position {};
    bool target_alive = true;
    bool incoming_heavy_attack = false;
    bool target_recovery_exposed = false;
};

struct LegendaryEnemyUpdateResult {
    bool accepted = false;
    std::uint64_t advanced_ticks = 0U;
    std::size_t dropped_event_count = 0U;
};

struct LegendaryEnemyHitRequest {
    float physical_damage = 0.0F;
    float stagger_power = 0.0F;
    bool frontal = true;
    std::uint8_t weapon_awakening_level = 0U;
};

enum class LegendaryEnemyHitError : std::uint8_t {
    None = 0,
    UnknownEnemy,
    InvalidDamage,
    InvalidStagger,
};

struct LegendaryEnemyHitResult {
    bool accepted = false;
    LegendaryEnemyHitError error =
        LegendaryEnemyHitError::None;
    AstralHitInteraction astral_interaction =
        AstralHitInteraction::Physical;
    float requested_damage = 0.0F;
    float applied_health_damage = 0.0F;
    float applied_armor_damage = 0.0F;
    float applied_stagger = 0.0F;
    float remaining_health = 0.0F;
    float remaining_armor = 0.0F;
    bool armor_broken_now = false;
    bool staggered_now = false;
    bool killed_now = false;
    ExperienceReward reward {};
};

enum class LegendaryEnemyEventKind : std::uint8_t {
    Spawned = 0,
    Alerted,
    Dodged,
    AttackTelegraphed,
    AttackActive,
    AttackFinished,
    Damaged,
    AstralDeflected,
    ArmorBroken,
    Staggered,
    Died,
    RewardAvailable,
};

struct LegendaryEnemyEvent {
    LegendaryEnemyEventKind kind =
        LegendaryEnemyEventKind::Spawned;
    LegendaryEnemyId enemy_id = 0U;
    LegendaryEnemyArchetype archetype =
        LegendaryEnemyArchetype::CorruptedBrute;
    std::uint64_t simulation_tick = 0U;
    glm::vec3 local_position {};
    float amount = 0.0F;
    ExperienceReward reward {};
    bool targets_player_only = false;
};

struct LegendaryEnemyRenderSnapshot {
    LegendaryEnemyId id = 0U;
    LegendaryEnemyArchetype archetype =
        LegendaryEnemyArchetype::CorruptedBrute;
    LegendaryEnemyBehavior behavior =
        LegendaryEnemyBehavior::Idle;
    glm::vec3 position {};
    float facing_yaw_radians = 0.0F;
    float health_ratio = 0.0F;
    float armor_ratio = 0.0F;
    float stagger_ratio = 0.0F;
    bool astral_intangible = false;
    bool alive = false;
};

struct LegendaryEnemyCombatSnapshot {
    LegendaryEnemyId id = 0U;
    glm::vec3 hit_center {};
    float hit_radius = 0.0F;
    EntityWeight weight = EntityWeight::Light;
    Faction faction = Faction::Hostile;
    bool corrupted = false;
    bool astral = false;
    bool damageable = false;
};

class LegendaryEnemySystem {
public:
    [[nodiscard]] auto spawn(
        const LegendaryEnemySpawnRequest& request) noexcept
        -> LegendaryEnemySpawnResult;
    [[nodiscard]] auto update(
        float dt,
        const LegendaryEnemyWorldInput& input) noexcept
        -> LegendaryEnemyUpdateResult;
    [[nodiscard]] auto apply_hit(
        LegendaryEnemyId id,
        const LegendaryEnemyHitRequest& request) noexcept
        -> LegendaryEnemyHitResult;
    [[nodiscard]] auto apply_knockback(
        LegendaryEnemyId id,
        const glm::vec3& direction,
        float distance) noexcept -> bool;

    [[nodiscard]] auto render_snapshots(
        std::span<LegendaryEnemyRenderSnapshot> output) const noexcept
        -> std::size_t;
    [[nodiscard]] auto combat_snapshots(
        std::span<LegendaryEnemyCombatSnapshot> output) const noexcept
        -> std::size_t;
    [[nodiscard]] auto render_snapshot(
        LegendaryEnemyId id) const noexcept
        -> std::optional<LegendaryEnemyRenderSnapshot>;
    [[nodiscard]] auto consume_events(
        std::span<LegendaryEnemyEvent> output) noexcept
        -> std::size_t;

    [[nodiscard]] auto size() const noexcept -> std::size_t;
    [[nodiscard]] static constexpr auto persistence_policy() noexcept
        -> TemporaryPersistencePolicy {
        return TemporaryPersistencePolicy::NeverSaved;
    }

    void clear() noexcept;

private:
    struct Entry {
        bool active = false;
        LegendaryEnemyId id = 0U;
        LegendaryEnemyProfile profile {};
        std::uint32_t deterministic_seed = 0U;
        glm::vec3 position {};
        float facing_yaw_radians = 0.0F;
        float health = 0.0F;
        float maximum_health = 0.0F;
        float armor = 0.0F;
        float stagger = 0.0F;
        LegendaryEnemyBehavior behavior =
            LegendaryEnemyBehavior::Idle;
        std::uint32_t behavior_ticks_remaining = 0U;
        std::uint32_t stagger_ticks_remaining = 0U;
        std::uint32_t stagger_recovery_delay_ticks = 0U;
        std::uint32_t dodge_cooldown_ticks = 0U;
        std::uint32_t attack_sequence = 0U;
        bool alerted = false;
    };

    [[nodiscard]] auto find(
        LegendaryEnemyId id) noexcept -> Entry*;
    [[nodiscard]] auto find(
        LegendaryEnemyId id) const noexcept -> const Entry*;
    void update_tick(
        Entry& entry,
        const LegendaryEnemyWorldInput& input) noexcept;
    void push_event(
        const Entry& entry,
        LegendaryEnemyEventKind kind,
        float amount = 0.0F,
        ExperienceReward reward = {},
        bool targets_player_only = false) noexcept;

    std::array<Entry, kMaximumLegendaryEnemies> entries_ {};
    std::array<LegendaryEnemyEvent, kMaximumLegendaryEnemyEvents>
        events_ {};
    std::size_t size_ = 0U;
    std::size_t event_count_ = 0U;
    std::size_t dropped_event_count_ = 0U;
    LegendaryEnemyId next_id_ = 1U;
    std::uint64_t simulation_tick_ = 0U;
    double tick_accumulator_ = 0.0;
};

} // namespace valcraft
