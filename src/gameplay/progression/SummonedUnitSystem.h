#pragma once

#include <glm/vec3.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>

namespace valcraft {

using SummonedUnitId = std::uint64_t;
using SummonedUnitOwnerId = std::uint64_t;
using SummonedUnitTargetId = std::uint64_t;
using SummonedUnitCastSequence = std::uint64_t;

inline constexpr float kSummonedUnitAttackIntervalSeconds = 1.2F;
inline constexpr float kSummonedUnitTauntIntervalSeconds = 6.0F;
inline constexpr float kSummonedUnitTauntRadius = 6.0F;
inline constexpr float kSummonedUnitProjectileBlockIntervalSeconds = 6.0F;
inline constexpr float kSummonedUnitMasteryDamageReduction = 0.50F;
inline constexpr float kSummonedUnitMasteryDamageReductionSeconds = 3.0F;
inline constexpr float kSummonedUnitDefaultPowerMultiplier = 1.0F;
inline constexpr float kSummonedUnitMinimumPowerMultiplier = 0.25F;
inline constexpr float kSummonedUnitMaximumPowerMultiplier = 4.0F;
inline constexpr float kSummonedUnitMaximumDurationSeconds = 86'400.0F;
inline constexpr float kSummonedUnitMaximumBaseStat = 1'000'000.0F;
inline constexpr float kSummonedUnitMaximumResolvedStat =
    kSummonedUnitMaximumBaseStat *
    kSummonedUnitMaximumPowerMultiplier;
inline constexpr float kSummonedUnitMinimumIntervalSeconds =
    1.0F / 60.0F;
inline constexpr float kSummonedUnitMaximumRadius = 10'000.0F;
inline constexpr std::size_t kSummonedUnitMaxAttackOutcomesPerUpdate = 32U;

enum class SummonedUnitRank : std::uint8_t {
    RankOne = 1,
    RankTwo = 2,
    RankThree = 3,
};

enum class SummonedUnitDamageKind : std::uint8_t {
    Melee = 0,
    Projectile = 1,
    Environment = 2,
};

enum class SummonedUnitDamageSource : std::uint8_t {
    PlayerSummon = 0,
};

struct SummonedUnitStats {
    float duration_seconds = 20.0F;
    float maximum_health = 14.0F;
    float attack_damage = 3.0F;
    float attack_interval_seconds = kSummonedUnitAttackIntervalSeconds;
    bool has_light_taunt = false;
    bool has_projectile_block = false;
    float taunt_interval_seconds = kSummonedUnitTauntIntervalSeconds;
    float taunt_radius = kSummonedUnitTauntRadius;
    float projectile_block_interval_seconds =
        kSummonedUnitProjectileBlockIntervalSeconds;
    float mastery_survival_health = 1.0F;
    float mastery_damage_reduction =
        kSummonedUnitMasteryDamageReduction;
    float mastery_damage_reduction_seconds =
        kSummonedUnitMasteryDamageReductionSeconds;

    auto operator==(const SummonedUnitStats&) const -> bool = default;
};

[[nodiscard]] constexpr auto summoned_unit_stats(SummonedUnitRank rank) noexcept
    -> SummonedUnitStats {
    switch (rank) {
    case SummonedUnitRank::RankThree:
        return {
            30.0F,
            22.0F,
            5.0F,
            kSummonedUnitAttackIntervalSeconds,
            true,
            true,
        };
    case SummonedUnitRank::RankTwo:
        return {
            25.0F,
            18.0F,
            4.0F,
            kSummonedUnitAttackIntervalSeconds,
            true,
            false,
        };
    case SummonedUnitRank::RankOne:
    default:
        return {
            20.0F,
            14.0F,
            3.0F,
            kSummonedUnitAttackIntervalSeconds,
            false,
            false,
        };
    }
}

// J'alloue les identifiants de repli dans un domaine commun à toutes les
// instances. Un propriétaire peut aussi injecter un identifiant persistant.
[[nodiscard]] auto allocate_summoned_unit_id() noexcept
    -> SummonedUnitId;
// Je rends le curseur persistant sans autoriser un chargement à le reculer.
[[nodiscard]] auto next_summoned_unit_id() noexcept
    -> SummonedUnitId;
void reserve_summoned_unit_id(
    SummonedUnitId unit_id) noexcept;
void reserve_next_summoned_unit_id(
    SummonedUnitId minimum_next_unit_id) noexcept;

struct SummonedUnitSpawnRequest {
    SummonedUnitOwnerId owner_id = 0U;
    glm::vec3 position {0.0F};
    SummonedUnitRank rank = SummonedUnitRank::RankOne;
    bool mastered = false;
    // Je conserve ce facteur commun pour que les anciens appels continuent
    // d'obtenir strictement la même mise à l'échelle des PV et des dégâts.
    float power_multiplier = kSummonedUnitDefaultPowerMultiplier;
    // Je n'applique ces facteurs spécialisés que lorsqu'ils sont fournis.
    // Une valeur invalide explicite revient à 1 au lieu de contaminer l'état.
    std::optional<float> health_power_multiplier {};
    std::optional<float> attack_power_multiplier {};
    // J'accepte l'identifiant persistant du propriétaire et les statistiques
    // déjà résolues par le catalogue ; les anciens appels gardent leurs replis.
    std::optional<SummonedUnitId> unit_id {};
    std::optional<SummonedUnitStats> stats {};
    SummonedUnitCastSequence cast_sequence = 0U;
};

struct SummonedUnitSpawnResult {
    bool spawned = false;
    SummonedUnitId unit_id = 0U;
};

struct SummonedUnitTarget {
    SummonedUnitTargetId target_id = 0U;
    glm::vec3 position {0.0F};
};

struct SummonedUnitAcquireRequest {
    SummonedUnitId unit_id = 0U;
    SummonedUnitOwnerId owner_id = 0U;
    glm::vec3 origin {0.0F};
    SummonedUnitRank rank = SummonedUnitRank::RankOne;
    float simulation_time_seconds = 0.0F;
};

struct SummonedUnitStrikeRequest {
    SummonedUnitId unit_id = 0U;
    SummonedUnitOwnerId owner_id = 0U;
    SummonedUnitTarget target {};
    glm::vec3 origin {0.0F};
    float damage = 0.0F;
    SummonedUnitDamageSource source =
        SummonedUnitDamageSource::PlayerSummon;
};

struct SummonedUnitStrikeResult {
    bool hit = false;
    bool killed = false;
    float applied_damage = 0.0F;
};

struct SummonedUnitTauntRequest {
    SummonedUnitId unit_id = 0U;
    SummonedUnitOwnerId owner_id = 0U;
    glm::vec3 origin {0.0F};
    float radius = kSummonedUnitTauntRadius;
    bool mastery_triggered = false;
    float duration_seconds =
        kSummonedUnitTauntIntervalSeconds;
};

struct SummonedUnitCallbacks {
    std::function<std::optional<SummonedUnitTarget>(
        const SummonedUnitAcquireRequest&)> acquire_target {};
    std::function<SummonedUnitStrikeResult(
        const SummonedUnitStrikeRequest&)> strike_target {};
    std::function<void(const SummonedUnitTauntRequest&)> taunt {};
};

struct SummonedUnitAttackOutcome {
    SummonedUnitId unit_id = 0U;
    SummonedUnitOwnerId owner_id = 0U;
    SummonedUnitTargetId target_id = 0U;
    SummonedUnitDamageSource source =
        SummonedUnitDamageSource::PlayerSummon;
    bool hit = false;
    bool killed = false;
    float requested_damage = 0.0F;
    float applied_damage = 0.0F;
};

struct SummonedUnitUpdateResult {
    std::array<
        SummonedUnitAttackOutcome,
        kSummonedUnitMaxAttackOutcomesPerUpdate>
        attacks {};
    std::size_t attack_window_count = 0U;
    std::size_t attack_count = 0U;
    std::size_t kill_count = 0U;
    std::size_t taunt_count = 0U;
    bool expired = false;

    [[nodiscard]] auto attack_results() const noexcept
        -> std::span<const SummonedUnitAttackOutcome> {
        return {
            attacks.data(),
            attack_count,
        };
    }
};

struct SummonedUnitDamageRequest {
    float damage = 0.0F;
    SummonedUnitDamageKind kind = SummonedUnitDamageKind::Melee;
};

struct SummonedUnitDamageResult {
    bool handled = false;
    bool blocked = false;
    bool death_refused = false;
    bool killed = false;
    float requested_damage = 0.0F;
    float applied_damage = 0.0F;
    float remaining_health = 0.0F;
};

struct SummonedUnitStateView {
    bool active = false;
    SummonedUnitId unit_id = 0U;
    SummonedUnitOwnerId owner_id = 0U;
    glm::vec3 position {0.0F};
    SummonedUnitRank rank = SummonedUnitRank::RankOne;
    float health = 0.0F;
    float maximum_health = 0.0F;
    float remaining_seconds = 0.0F;
    float projectile_block_cooldown = 0.0F;
    float mastery_damage_reduction_seconds = 0.0F;
    bool mastered = false;
    bool death_refusal_used = false;
    SummonedUnitCastSequence cast_sequence = 0U;
};

struct SummonedUnitRenderSnapshot {
    SummonedUnitId unit_id = 0U;
    SummonedUnitOwnerId owner_id = 0U;
    glm::vec3 position {0.0F};
    float yaw_radians = 0.0F;
    float animation_time = 0.0F;
    float health_ratio = 1.0F;
    float remaining_life_ratio = 1.0F;
    float attack_amount = 0.0F;
    float taunt_amount = 0.0F;
    SummonedUnitRank rank = SummonedUnitRank::RankOne;
    bool projectile_block_ready = false;
    bool mastery_damage_reduction_active = false;
};

struct SummonedUnitSystemSnapshot {
    bool active = false;
    SummonedUnitId unit_id = 0U;
    SummonedUnitOwnerId owner_id = 0U;
    SummonedUnitCastSequence cast_sequence = 0U;
    glm::vec3 position {0.0F};
    SummonedUnitRank rank = SummonedUnitRank::RankOne;
    SummonedUnitStats stats {};
    double age_seconds = 0.0;
    double next_attack_seconds =
        static_cast<double>(
            kSummonedUnitAttackIntervalSeconds);
    double next_taunt_seconds =
        static_cast<double>(
            kSummonedUnitTauntIntervalSeconds);
    float health = 0.0F;
    float projectile_block_cooldown = 0.0F;
    float mastery_damage_reduction_seconds = 0.0F;
    float yaw_radians = 0.0F;
    float animation_time = 0.0F;
    double last_attack_event_seconds = -1.0;
    double last_taunt_event_seconds = -1.0;
    bool mastered = false;
    bool death_refusal_used = false;
    bool pending_mastery_taunt = false;

    auto operator==(const SummonedUnitSystemSnapshot&) const
        -> bool = default;
};

struct SummonedUnitLoadResult {
    bool restored = false;
    bool sanitized = false;
    bool expired = false;
};

class SummonedUnitSystem {
public:
    [[nodiscard]] auto summon(
        const SummonedUnitSpawnRequest& request) noexcept
        -> SummonedUnitSpawnResult;
    void clear() noexcept;

    [[nodiscard]] auto update(
        float dt,
        const SummonedUnitCallbacks& callbacks)
        -> SummonedUnitUpdateResult;
    [[nodiscard]] auto apply_damage(
        const SummonedUnitDamageRequest& request) noexcept
        -> SummonedUnitDamageResult;

    void set_position(const glm::vec3& position) noexcept;

    [[nodiscard]] auto active() const noexcept -> bool;
    [[nodiscard]] auto state() const noexcept -> SummonedUnitStateView;
    [[nodiscard]] auto render_snapshots() const noexcept
        -> std::span<const SummonedUnitRenderSnapshot>;
    [[nodiscard]] auto snapshot() const noexcept
        -> SummonedUnitSystemSnapshot;
    [[nodiscard]] auto load_state(
        const SummonedUnitSystemSnapshot& snapshot) noexcept
        -> SummonedUnitLoadResult;

private:
    void emit_attack(
        double event_time,
        const SummonedUnitCallbacks& callbacks,
        SummonedUnitUpdateResult& result);
    void emit_taunt(
        double event_time,
        bool mastery_triggered,
        const SummonedUnitCallbacks& callbacks,
        SummonedUnitUpdateResult& result);
    void rebuild_render_snapshot() noexcept;
    void deactivate() noexcept;

    SummonedUnitId unit_id_ = 0U;
    SummonedUnitOwnerId owner_id_ = 0U;
    SummonedUnitCastSequence cast_sequence_ = 0U;
    glm::vec3 position_ {0.0F};
    SummonedUnitRank rank_ = SummonedUnitRank::RankOne;
    SummonedUnitStats stats_ {};
    double age_seconds_ = 0.0;
    double next_attack_seconds_ =
        static_cast<double>(kSummonedUnitAttackIntervalSeconds);
    double next_taunt_seconds_ =
        static_cast<double>(kSummonedUnitTauntIntervalSeconds);
    float health_ = 0.0F;
    float projectile_block_cooldown_ = 0.0F;
    float mastery_damage_reduction_seconds_ = 0.0F;
    float yaw_radians_ = 0.0F;
    float animation_time_ = 0.0F;
    double last_attack_event_seconds_ = -1.0;
    double last_taunt_event_seconds_ = -1.0;
    bool active_ = false;
    bool mastered_ = false;
    bool death_refusal_used_ = false;
    bool pending_mastery_taunt_ = false;
    std::array<SummonedUnitRenderSnapshot, 1U> render_snapshots_ {};
    std::size_t render_snapshot_count_ = 0U;
};

} // namespace valcraft
