#pragma once

#include <glm/vec3.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace valcraft {

using KnightEntityId = std::uint64_t;
using KnightActivationId = std::uint64_t;
using KnightAttackId = std::uint64_t;
using KnightEffectStackTag = std::uint64_t;

inline constexpr float kKnightAbilityFixedStepSeconds = 1.0F / 60.0F;
inline constexpr float kKnightMaximumChargeStepMeters = 0.45F;
inline constexpr float kKnightMaximumUpdateSeconds = 86'400.0F;
inline constexpr std::size_t kKnightMaximumChargeSteps = 16U;
inline constexpr std::size_t kKnightMaximumChargeTargets = 3U;
inline constexpr std::size_t kKnightMaximumNearbyTargets = 64U;

enum class KnightAbilityRank : std::uint8_t {
    RankOne = 1,
    RankTwo = 2,
    RankThree = 3,
};

enum class KnightAdvancedAbilityError : std::uint8_t {
    None = 0,
    InvalidActivation,
    InvalidEntity,
    InvalidRank,
    InvalidInput,
    MissingCallback,
    Busy,
    InvalidCallbackResult,
    CapacityExceeded,
    ExternalCommitRejected,
    NotActive,
    NotParryable,
    NoMatchingWindow,
};

enum class KnightTargetWeight : std::uint8_t {
    Light = 0,
    Normal = 1,
    Heavy = 2,
    Boss = 3,
};

[[nodiscard]] constexpr auto knight_knockback_multiplier(
    KnightTargetWeight weight) noexcept -> float {
    switch (weight) {
    case KnightTargetWeight::Light:
        return 1.0F;
    case KnightTargetWeight::Normal:
        return 0.70F;
    case KnightTargetWeight::Heavy:
        return 0.20F;
    case KnightTargetWeight::Boss:
        return 0.0F;
    }
    return 0.0F;
}

struct KnightBulwarkChargeDefinition {
    float energy_cost = 0.0F;
    float cooldown_seconds = 0.0F;
    float distance_meters = 0.0F;
    float weapon_damage_multiplier = 0.0F;
    std::uint8_t maximum_targets = 0U;
    float wall_impact_window_seconds = 0.0F;
    float wall_stun_seconds = 0.0F;
};

struct KnightChampionCryDefinition {
    float energy_cost = 0.0F;
    float cooldown_seconds = 0.0F;
    float radius_meters = 0.0F;
    float duration_seconds = 0.0F;
    float self_melee_damage_bonus = 0.0F;
    float ally_melee_damage_bonus = 0.0F;
    float immediate_self_heal = 0.0F;
};

struct KnightPerfectRiposteDefinition {
    float energy_cost = 0.0F;
    float cooldown_seconds = 0.0F;
    float parry_window_seconds = 0.0F;
    float counter_weapon_damage_multiplier = 0.0F;
    float energy_refund = 0.0F;
    float secondary_cone_damage = 0.0F;
    float light_target_stun_seconds = 0.0F;
};

inline constexpr std::array<KnightBulwarkChargeDefinition, 3U>
    kKnightBulwarkChargeDefinitions {{
        {18.0F, 9.0F, 5.0F, 0.80F, 1U, 0.0F, 0.0F},
        {18.0F, 8.5F, 6.0F, 1.00F, 1U, 1.0F, 1.0F},
        {18.0F, 8.0F, 7.0F, 1.20F, 3U, 1.0F, 1.0F},
    }};

inline constexpr std::array<KnightChampionCryDefinition, 3U>
    kKnightChampionCryDefinitions {{
        {25.0F, 20.0F, 6.0F, 7.0F, 0.15F, 0.10F, 0.0F},
        {25.0F, 18.0F, 7.0F, 8.0F, 0.20F, 0.15F, 0.0F},
        {25.0F, 16.0F, 8.0F, 9.0F, 0.25F, 0.20F, 3.0F},
    }};

inline constexpr std::array<KnightPerfectRiposteDefinition, 3U>
    kKnightPerfectRiposteDefinitions {{
        {15.0F, 12.0F, 0.35F, 1.80F, 0.0F, 0.0F, 0.0F},
        {15.0F, 10.0F, 0.45F, 2.20F, 5.0F, 0.0F, 0.0F},
        {15.0F, 8.0F, 0.55F, 2.60F, 5.0F, 4.0F, 1.0F},
    }};

[[nodiscard]] constexpr auto knight_bulwark_charge_definition(
    KnightAbilityRank rank) noexcept
    -> const KnightBulwarkChargeDefinition* {
    const auto index = static_cast<std::uint8_t>(rank);
    return index >= 1U && index <= kKnightBulwarkChargeDefinitions.size()
               ? &kKnightBulwarkChargeDefinitions[index - 1U]
               : nullptr;
}

[[nodiscard]] constexpr auto knight_champion_cry_definition(
    KnightAbilityRank rank) noexcept
    -> const KnightChampionCryDefinition* {
    const auto index = static_cast<std::uint8_t>(rank);
    return index >= 1U && index <= kKnightChampionCryDefinitions.size()
               ? &kKnightChampionCryDefinitions[index - 1U]
               : nullptr;
}

[[nodiscard]] constexpr auto knight_perfect_riposte_definition(
    KnightAbilityRank rank) noexcept
    -> const KnightPerfectRiposteDefinition* {
    const auto index = static_cast<std::uint8_t>(rank);
    return index >= 1U && index <= kKnightPerfectRiposteDefinitions.size()
               ? &kKnightPerfectRiposteDefinitions[index - 1U]
               : nullptr;
}

[[nodiscard]] constexpr auto knight_stack_tag(
    const char* text) noexcept -> KnightEffectStackTag {
    auto hash = std::uint64_t {14'695'981'039'346'656'037ULL};
    while (text != nullptr && *text != '\0') {
        hash ^= static_cast<std::uint8_t>(*text);
        hash *= 1'099'511'628'211ULL;
        ++text;
    }
    return hash;
}

inline constexpr KnightEffectStackTag kKnightMeleeDamageBuffStackTag =
    knight_stack_tag("MELEE_DAMAGE_BUFF");
inline constexpr KnightEffectStackTag kKnightBreachVulnerabilityStackTag =
    knight_stack_tag("KNIGHT_BREACH_PHYSICAL_VULNERABILITY");
inline constexpr KnightEffectStackTag kKnightRiposteMasteryStackTag =
    knight_stack_tag("KNIGHT_PERFECT_RIPOSTE_MASTERY");

enum class KnightChargeTraversal : std::uint8_t {
    Safe = 0,
    WorldBlocked = 1,
    ChunkNotReady = 2,
};

struct KnightChargeContact {
    KnightEntityId target_id = 0U;
    KnightTargetWeight weight = KnightTargetWeight::Normal;
};

struct KnightChargeStepProbeRequest {
    KnightActivationId activation_id = 0U;
    glm::vec3 from {0.0F};
    glm::vec3 candidate {0.0F};
    float step_distance_meters = 0.0F;
    std::uint8_t step_index = 0U;
};

struct KnightChargeStepProbeResult {
    KnightChargeTraversal traversal = KnightChargeTraversal::Safe;
    std::array<KnightChargeContact, kKnightMaximumChargeTargets> contacts {};
    std::size_t contact_count = 0U;
    bool enemy_blocks_path = false;
};

struct KnightChargeHit {
    KnightEntityId target_id = 0U;
    KnightTargetWeight weight = KnightTargetWeight::Normal;
    float damage = 0.0F;
    float knockback_multiplier = 0.0F;
};

struct KnightBulwarkChargeCommitRequest {
    KnightActivationId activation_id = 0U;
    KnightEntityId caster_id = 0U;
    KnightAbilityRank rank = KnightAbilityRank::RankOne;
    glm::vec3 start_position {0.0F};
    glm::vec3 final_position {0.0F};
    glm::vec3 direction {0.0F};
    float travelled_distance_meters = 0.0F;
    std::array<glm::vec3, kKnightMaximumChargeSteps> safe_path {};
    std::size_t safe_path_count = 0U;
    std::array<KnightChargeHit, kKnightMaximumChargeTargets> hits {};
    std::size_t hit_count = 0U;
    bool ended_in_enemy_contact = false;
    bool stopped_by_world = false;
    bool stopped_by_unready_chunk = false;

    [[nodiscard]] auto path() const noexcept -> std::span<const glm::vec3> {
        return {
            safe_path.data(),
            std::min(safe_path_count, safe_path.size()),
        };
    }

    [[nodiscard]] auto targets_hit() const noexcept
        -> std::span<const KnightChargeHit> {
        return {
            hits.data(),
            std::min(hit_count, hits.size()),
        };
    }
};

using KnightChargeStepProbe = KnightChargeStepProbeResult (*)(
    void* user_data,
    const KnightChargeStepProbeRequest& request) noexcept;
using KnightBulwarkChargeCommit = bool (*)(
    void* user_data,
    const KnightBulwarkChargeCommitRequest& request) noexcept;

struct KnightBulwarkChargeCallbacks {
    void* user_data = nullptr;
    KnightChargeStepProbe probe_step = nullptr;
    KnightBulwarkChargeCommit commit = nullptr;
};

struct KnightBulwarkChargeRequest {
    KnightActivationId activation_id = 0U;
    KnightEntityId caster_id = 0U;
    KnightAbilityRank rank = KnightAbilityRank::RankOne;
    glm::vec3 start_position {0.0F};
    glm::vec3 direction {0.0F, 0.0F, 1.0F};
    float weapon_damage = 0.0F;
    bool mastery_active = false;
};

struct KnightBulwarkChargeResult {
    KnightAdvancedAbilityError error = KnightAdvancedAbilityError::None;
    KnightBulwarkChargeCommitRequest committed {};

    [[nodiscard]] constexpr auto succeeded() const noexcept -> bool {
        return error == KnightAdvancedAbilityError::None;
    }
};

struct KnightWallImpactCommitRequest {
    KnightActivationId activation_id = 0U;
    KnightEntityId target_id = 0U;
    float stun_seconds = 0.0F;
    bool apply_before_control_resistance = true;
};

using KnightWallImpactCommit = bool (*)(
    void* user_data,
    const KnightWallImpactCommitRequest& request) noexcept;

struct KnightWallImpactResult {
    KnightAdvancedAbilityError error = KnightAdvancedAbilityError::None;
    KnightWallImpactCommitRequest committed {};

    [[nodiscard]] constexpr auto succeeded() const noexcept -> bool {
        return error == KnightAdvancedAbilityError::None;
    }
};

struct KnightBreachMeleeCommitRequest {
    KnightActivationId activation_id = 0U;
    KnightEntityId target_id = 0U;
    float base_damage = 0.0F;
    float total_damage = 0.0F;
    float bonus_damage_multiplier = 0.40F;
    float physical_vulnerability = 0.10F;
    float vulnerability_duration_seconds = 5.0F;
    KnightEffectStackTag vulnerability_stack_tag =
        kKnightBreachVulnerabilityStackTag;
};

using KnightBreachMeleeCommit = bool (*)(
    void* user_data,
    const KnightBreachMeleeCommitRequest& request) noexcept;

struct KnightBreachMeleeResult {
    KnightAdvancedAbilityError error = KnightAdvancedAbilityError::None;
    KnightBreachMeleeCommitRequest committed {};

    [[nodiscard]] constexpr auto succeeded() const noexcept -> bool {
        return error == KnightAdvancedAbilityError::None;
    }
};

enum class KnightTargetRelation : std::uint8_t {
    Ally = 0,
    Enemy = 1,
    Neutral = 2,
};

struct KnightNearbyTarget {
    KnightEntityId target_id = 0U;
    glm::vec3 position {0.0F};
    KnightTargetRelation relation = KnightTargetRelation::Neutral;
    bool can_be_taunted = false;
};

struct KnightNearbyQueryRequest {
    KnightActivationId activation_id = 0U;
    KnightEntityId caster_id = 0U;
    glm::vec3 center {0.0F};
    float radius_meters = 0.0F;
};

struct KnightNearbyQueryResult {
    std::array<KnightNearbyTarget, kKnightMaximumNearbyTargets> targets {};
    std::size_t target_count = 0U;
};

enum class KnightEffectStackPolicy : std::uint8_t {
    Strongest = 0,
};

struct KnightChampionAllyEffect {
    KnightEntityId target_id = 0U;
    float melee_damage_bonus = 0.0F;
    float movement_speed_bonus = 0.0F;
    float duration_seconds = 0.0F;
    KnightEffectStackTag melee_damage_stack_tag =
        kKnightMeleeDamageBuffStackTag;
    KnightEffectStackPolicy stack_policy =
        KnightEffectStackPolicy::Strongest;
    bool ignore_first_interruption = false;
};

struct KnightChampionTaunt {
    KnightEntityId target_id = 0U;
    KnightEntityId priority_target_id = 0U;
    float duration_seconds = 0.0F;
};

struct KnightChampionCryCommitRequest {
    KnightActivationId activation_id = 0U;
    KnightEntityId caster_id = 0U;
    KnightAbilityRank rank = KnightAbilityRank::RankOne;
    glm::vec3 center {0.0F};
    float radius_meters = 0.0F;
    float duration_seconds = 0.0F;
    float self_melee_damage_bonus = 0.0F;
    float immediate_self_heal = 0.0F;
    KnightEffectStackTag melee_damage_stack_tag =
        kKnightMeleeDamageBuffStackTag;
    KnightEffectStackPolicy stack_policy =
        KnightEffectStackPolicy::Strongest;
    std::array<KnightChampionAllyEffect, kKnightMaximumNearbyTargets> allies {};
    std::size_t ally_count = 0U;
    std::array<KnightChampionTaunt, kKnightMaximumNearbyTargets> taunts {};
    std::size_t taunt_count = 0U;

    [[nodiscard]] auto affected_allies() const noexcept
        -> std::span<const KnightChampionAllyEffect> {
        return {
            allies.data(),
            std::min(ally_count, allies.size()),
        };
    }

    [[nodiscard]] auto affected_enemies() const noexcept
        -> std::span<const KnightChampionTaunt> {
        return {
            taunts.data(),
            std::min(taunt_count, taunts.size()),
        };
    }
};

using KnightNearbyQuery = KnightNearbyQueryResult (*)(
    void* user_data,
    const KnightNearbyQueryRequest& request) noexcept;
using KnightChampionCryCommit = bool (*)(
    void* user_data,
    const KnightChampionCryCommitRequest& request) noexcept;

struct KnightChampionCryCallbacks {
    void* user_data = nullptr;
    KnightNearbyQuery query_nearby = nullptr;
    KnightChampionCryCommit commit = nullptr;
};

struct KnightChampionCryRequest {
    KnightActivationId activation_id = 0U;
    KnightEntityId caster_id = 0U;
    KnightAbilityRank rank = KnightAbilityRank::RankOne;
    glm::vec3 center {0.0F};
    bool mastery_active = false;
};

struct KnightChampionCryResult {
    KnightAdvancedAbilityError error = KnightAdvancedAbilityError::None;
    KnightChampionCryCommitRequest committed {};

    [[nodiscard]] constexpr auto succeeded() const noexcept -> bool {
        return error == KnightAdvancedAbilityError::None;
    }
};

struct KnightPerfectRiposteActivationRequest {
    KnightActivationId activation_id = 0U;
    KnightEntityId caster_id = 0U;
    KnightAbilityRank rank = KnightAbilityRank::RankOne;
    float weapon_damage = 0.0F;
    bool mastery_active = false;
};

struct KnightPerfectRiposteActivationResult {
    KnightAdvancedAbilityError error = KnightAdvancedAbilityError::None;
    float parry_window_seconds = 0.0F;

    [[nodiscard]] constexpr auto succeeded() const noexcept -> bool {
        return error == KnightAdvancedAbilityError::None;
    }
};

struct KnightIncomingAttack {
    KnightAttackId attack_id = 0U;
    KnightEntityId attacker_id = 0U;
    float incoming_damage = 0.0F;
    bool parryable = true;
};

struct KnightPerfectRiposteCommitRequest {
    KnightActivationId activation_id = 0U;
    KnightAttackId incoming_attack_id = 0U;
    KnightEntityId caster_id = 0U;
    KnightEntityId attacker_id = 0U;
    KnightAbilityRank rank = KnightAbilityRank::RankOne;
    bool cancel_incoming_attack = true;
    float cancelled_incoming_damage = 0.0F;
    float counter_damage = 0.0F;
    float counter_weapon_damage_multiplier = 0.0F;
    float energy_refund = 0.0F;
    bool emit_secondary_cone = false;
    bool exclude_primary_from_secondary_cone = true;
    float secondary_cone_damage = 0.0F;
    bool stun_light_targets_only = true;
    float light_target_stun_seconds = 0.0F;
    bool reset_vanguard_strike_cooldown = false;
    float mastery_damage_reduction = 0.0F;
    float mastery_damage_reduction_seconds = 0.0F;
    KnightEffectStackTag mastery_stack_tag =
        kKnightRiposteMasteryStackTag;
};

using KnightPerfectRiposteCommit = bool (*)(
    void* user_data,
    const KnightPerfectRiposteCommitRequest& request) noexcept;

struct KnightPerfectRiposteCallbacks {
    void* user_data = nullptr;
    KnightPerfectRiposteCommit commit = nullptr;
};

struct KnightPerfectRiposteResult {
    KnightAdvancedAbilityError error = KnightAdvancedAbilityError::None;
    bool incoming_attack_cancelled = false;
    KnightPerfectRiposteCommitRequest committed {};

    [[nodiscard]] constexpr auto succeeded() const noexcept -> bool {
        return error == KnightAdvancedAbilityError::None;
    }
};

struct KnightAdvancedAbilitySnapshot {
    bool breach_armed = false;
    float breach_remaining_seconds = 0.0F;
    bool champion_cry_active = false;
    float champion_cry_remaining_seconds = 0.0F;
    bool perfect_riposte_armed = false;
    float perfect_riposte_remaining_seconds = 0.0F;
    float mastery_damage_reduction = 0.0F;
    float mastery_damage_reduction_remaining_seconds = 0.0F;
    std::size_t pending_wall_impact_count = 0U;
};

struct KnightAdvancedAbilityUpdateResult {
    bool accepted = false;
    std::uint64_t advanced_ticks = 0U;
    std::size_t expired_wall_impact_count = 0U;
    bool breach_expired = false;
    bool champion_cry_expired = false;
    bool perfect_riposte_expired = false;
    bool mastery_damage_reduction_expired = false;
};

class KnightAdvancedAbilitySystem {
public:
    [[nodiscard]] auto execute_bulwark_charge(
        const KnightBulwarkChargeRequest& request,
        const KnightBulwarkChargeCallbacks& callbacks) noexcept
        -> KnightBulwarkChargeResult;

    [[nodiscard]] auto notify_charge_wall_impact(
        KnightEntityId target_id,
        void* user_data,
        KnightWallImpactCommit commit) noexcept
        -> KnightWallImpactResult;

    [[nodiscard]] auto resolve_breach_melee_hit(
        KnightEntityId target_id,
        float base_damage,
        void* user_data,
        KnightBreachMeleeCommit commit) noexcept
        -> KnightBreachMeleeResult;

    [[nodiscard]] auto activate_champion_cry(
        const KnightChampionCryRequest& request,
        const KnightChampionCryCallbacks& callbacks) noexcept
        -> KnightChampionCryResult;

    [[nodiscard]] auto arm_perfect_riposte(
        const KnightPerfectRiposteActivationRequest& request) noexcept
        -> KnightPerfectRiposteActivationResult;

    [[nodiscard]] auto resolve_incoming_attack(
        const KnightIncomingAttack& attack,
        const KnightPerfectRiposteCallbacks& callbacks) noexcept
        -> KnightPerfectRiposteResult;

    [[nodiscard]] auto update(float elapsed_seconds) noexcept
        -> KnightAdvancedAbilityUpdateResult;

    [[nodiscard]] auto consume_champion_ally_interruption(
        KnightEntityId ally_id) noexcept -> bool;
    [[nodiscard]] auto melee_damage_bonus(
        KnightEntityId target_id) const noexcept -> float;
    [[nodiscard]] auto movement_speed_bonus(
        KnightEntityId target_id) const noexcept -> float;
    [[nodiscard]] auto snapshot() const noexcept
        -> KnightAdvancedAbilitySnapshot;

    void clear() noexcept;

private:
    struct PendingWallImpact {
        KnightActivationId activation_id = 0U;
        KnightEntityId target_id = 0U;
        std::uint64_t remaining_ticks = 0U;
        bool active = false;
    };

    struct ChampionAllyState {
        KnightEntityId target_id = 0U;
        bool interruption_consumed = false;
        bool active = false;
    };

    [[nodiscard]] static auto seconds_to_ticks(float seconds) noexcept
        -> std::uint64_t;
    void advance_ticks(
        std::uint64_t ticks,
        KnightAdvancedAbilityUpdateResult& result) noexcept;

    std::array<PendingWallImpact, kKnightMaximumChargeTargets>
        pending_wall_impacts_ {};
    std::array<ChampionAllyState, kKnightMaximumNearbyTargets>
        champion_allies_ {};
    std::size_t champion_ally_count_ = 0U;

    KnightActivationId breach_activation_id_ = 0U;
    std::uint64_t breach_remaining_ticks_ = 0U;
    KnightActivationId last_charge_activation_id_ = 0U;

    KnightActivationId champion_activation_id_ = 0U;
    KnightActivationId last_champion_activation_id_ = 0U;
    KnightEntityId champion_caster_id_ = 0U;
    std::uint64_t champion_remaining_ticks_ = 0U;
    float champion_self_melee_bonus_ = 0.0F;
    float champion_ally_melee_bonus_ = 0.0F;
    bool champion_mastery_active_ = false;

    KnightActivationId riposte_activation_id_ = 0U;
    KnightActivationId last_riposte_activation_id_ = 0U;
    KnightEntityId riposte_caster_id_ = 0U;
    KnightAbilityRank riposte_rank_ = KnightAbilityRank::RankOne;
    std::uint64_t riposte_remaining_ticks_ = 0U;
    float riposte_weapon_damage_ = 0.0F;
    bool riposte_mastery_active_ = false;

    std::uint64_t mastery_damage_reduction_ticks_ = 0U;
    double fixed_step_accumulator_seconds_ = 0.0;
};

} // namespace valcraft
