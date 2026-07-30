#pragma once

#include "gameplay/progression/AbilityCatalog.h"

#include <glm/vec3.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>

namespace valcraft {

using NinjaEntityId = std::uint64_t;
using NinjaActivationId = std::uint64_t;

inline constexpr double kNinjaFixedStepSeconds = 1.0 / 60.0;
inline constexpr std::uint32_t kNinjaMaximumTicksPerUpdate = 600U;
inline constexpr std::size_t kNinjaMaximumQueryEntities = 64U;
inline constexpr std::size_t kNinjaMaximumSmokeProtectedActors = 16U;
inline constexpr std::size_t kNinjaMaximumSmokeObservers = 64U;
inline constexpr std::size_t kNinjaMaximumMarks = 16U;
inline constexpr std::size_t kNinjaMaximumKunaiHits = 8U;
inline constexpr float kNinjaMaximumAgility = 15.0F;
inline constexpr std::uint32_t kNinjaMaximumLevel = 100U;

enum class NinjaAbilityRank : std::uint8_t {
    RankOne = 1U,
    RankTwo = 2U,
    RankThree = 3U,
};

enum class NinjaEffectFailure : std::uint8_t {
    None = 0U,
    InvalidOwner,
    InvalidActivation,
    InvalidRank,
    NonFiniteInput,
    InvalidDirection,
    InvalidTarget,
    UnsafeDestination,
    MovementRejected,
    AirUseUnavailable,
    FreeImpulseUnavailable,
    NoCharge,
    MissingCallback,
};

enum class NinjaEntityQueryKind : std::uint8_t {
    InsideSmoke = 0U,
    LockingTarget,
    DashPath,
    LandingArea,
};

enum class NinjaSafeMovementKind : std::uint8_t {
    ShinobiLeap = 0U,
    ShinobiFreeImpulse,
    LightningDash,
    LightningDashBehindTarget,
};

enum class NinjaContactKind : std::uint8_t {
    Ground = 0U,
    Water,
    ShipDeck,
};

enum class NinjaModifierKind : std::uint8_t {
    MovementSpeed = 0U,
    Slow,
};

enum class NinjaDamagePass : std::uint8_t {
    ShinobiLanding = 0U,
    LightningDash,
    SpectralKunaiOutward,
    SpectralKunaiReturn,
};

struct NinjaSmokeBombTuning {
    float energy_cost = 20.0F;
    float cooldown_seconds = 15.0F;
    float radius = 4.0F;
    float duration_seconds = 5.0F;
    float ninja_speed_bonus = 0.10F;
    float attack_bonus = 0.0F;
    float slow_fraction = 0.0F;
    float slow_duration_seconds = 0.0F;
    float allied_speed_bonus = 0.10F;
    float lock_break_delay_seconds = 1.0F;
};

struct NinjaShinobiLeapTuning {
    float energy_cost = 15.0F;
    float cooldown_seconds = 8.0F;
    float distance = 4.0F;
    bool aerial_use_allowed = false;
    bool free_second_impulse_allowed = false;
    float free_second_impulse_distance = 0.0F;
    float mastery_window_seconds = 2.0F;
    float mastery_wave_damage = 5.0F;
    float mastery_wave_radius = 2.0F;
};

struct NinjaLightningDashTuning {
    float energy_cost = 22.0F;
    float cooldown_seconds = 11.0F;
    float distance = 6.0F;
    float base_damage = 7.0F;
    float kill_cooldown_reduction = 0.0F;
    std::uint8_t mastery_charge_count = 2U;
    float mastery_charge_recharge_seconds = 12.0F;
    float mastery_repeat_target_interval_seconds = 0.75F;
};

struct NinjaSpectralKunaiTuning {
    float energy_cost = 20.0F;
    float cooldown_seconds = 8.0F;
    float base_damage = 7.0F;
    std::uint8_t bounce_count = 1U;
    float mark_duration_seconds = 6.0F;
    float mark_melee_bonus = 0.30F;
    float bounce_power_loss = 0.25F;
    float mastery_return_power = 0.60F;
};

[[nodiscard]] constexpr auto ninja_rank_is_valid(
    NinjaAbilityRank rank) noexcept -> bool {
    const auto value = static_cast<std::uint8_t>(rank);
    return value >= 1U && value <= 3U;
}

[[nodiscard]] constexpr auto ninja_smoke_bomb_tuning(
    NinjaAbilityRank rank) noexcept -> NinjaSmokeBombTuning {
    switch (rank) {
    case NinjaAbilityRank::RankThree:
        return {
            20.0F,
            11.0F,
            6.0F,
            7.0F,
            0.20F,
            0.40F,
            0.20F,
            2.0F,
            0.10F,
            1.0F,
        };
    case NinjaAbilityRank::RankTwo:
        return {
            20.0F,
            13.0F,
            5.0F,
            6.0F,
            0.15F,
            0.30F,
            0.0F,
            0.0F,
            0.10F,
            1.0F,
        };
    case NinjaAbilityRank::RankOne:
    default:
        return {};
    }
}

[[nodiscard]] constexpr auto ninja_shinobi_leap_tuning(
    NinjaAbilityRank rank) noexcept -> NinjaShinobiLeapTuning {
    switch (rank) {
    case NinjaAbilityRank::RankThree:
        return {
            15.0F,
            6.0F,
            6.0F,
            true,
            true,
            3.0F,
            2.0F,
            5.0F,
            2.0F,
        };
    case NinjaAbilityRank::RankTwo:
        return {
            15.0F,
            7.0F,
            5.0F,
            true,
            false,
            0.0F,
            2.0F,
            5.0F,
            2.0F,
        };
    case NinjaAbilityRank::RankOne:
    default:
        return {};
    }
}

[[nodiscard]] constexpr auto ninja_lightning_dash_tuning(
    NinjaAbilityRank rank) noexcept -> NinjaLightningDashTuning {
    switch (rank) {
    case NinjaAbilityRank::RankThree:
        return {
            22.0F,
            8.0F,
            8.0F,
            11.0F,
            0.50F,
            2U,
            12.0F,
            0.75F,
        };
    case NinjaAbilityRank::RankTwo:
        return {
            22.0F,
            9.5F,
            7.0F,
            9.0F,
            0.40F,
            2U,
            12.0F,
            0.75F,
        };
    case NinjaAbilityRank::RankOne:
    default:
        return {};
    }
}

[[nodiscard]] constexpr auto ninja_spectral_kunai_tuning(
    NinjaAbilityRank rank) noexcept -> NinjaSpectralKunaiTuning {
    switch (rank) {
    case NinjaAbilityRank::RankThree:
        return {
            20.0F,
            6.0F,
            11.0F,
            3U,
            6.0F,
            0.50F,
            0.15F,
            0.60F,
        };
    case NinjaAbilityRank::RankTwo:
        return {
            20.0F,
            7.0F,
            9.0F,
            2U,
            6.0F,
            0.40F,
            0.20F,
            0.60F,
        };
    case NinjaAbilityRank::RankOne:
    default:
        return {};
    }
}

[[nodiscard]] auto ninja_spell_damage(
    float base_damage,
    std::uint32_t player_level,
    float agility) noexcept -> float;

struct NinjaCastContext {
    NinjaEntityId owner_id = 0U;
    NinjaActivationId activation_id = 0U;
    NinjaAbilityRank rank = NinjaAbilityRank::RankOne;
    std::uint32_t player_level = 1U;
    float agility = 0.0F;
    bool mastered = false;
};

struct NinjaEntitySnapshot {
    NinjaEntityId entity_id = 0U;
    NinjaEntityId target_id = 0U;
    glm::vec3 position {0.0F};
    glm::vec3 forward {0.0F, 0.0F, 1.0F};
    bool alive = true;
    bool hostile_to_owner = false;
    bool allied_with_owner = false;
};

struct NinjaEntityQuery {
    NinjaEntityQueryKind kind = NinjaEntityQueryKind::InsideSmoke;
    NinjaEntityId owner_id = 0U;
    NinjaEntityId target_id = 0U;
    glm::vec3 center {0.0F};
    glm::vec3 start {0.0F};
    glm::vec3 end {0.0F};
    float radius = 0.0F;
};

struct NinjaSmokePlacementRequest {
    NinjaEntityId owner_id = 0U;
    glm::vec3 center {0.0F};
    float radius = 0.0F;
};

struct NinjaSmokeOcclusionQuery {
    NinjaEntityId enemy_id = 0U;
    NinjaEntityId target_id = 0U;
    glm::vec3 enemy_position {0.0F};
    glm::vec3 target_position {0.0F};
    glm::vec3 smoke_center {0.0F};
    float smoke_radius = 0.0F;
};

struct NinjaBreakTargetRequest {
    NinjaEntityId enemy_id = 0U;
    NinjaEntityId target_id = 0U;
    NinjaActivationId activation_id = 0U;
};

struct NinjaModifierRequest {
    NinjaEntityId source_id = 0U;
    NinjaEntityId target_id = 0U;
    NinjaActivationId activation_id = 0U;
    NinjaModifierKind kind = NinjaModifierKind::MovementSpeed;
    float magnitude = 0.0F;
    float duration_seconds = 0.0F;
};

struct NinjaSafeMovementRequest {
    NinjaSafeMovementKind kind = NinjaSafeMovementKind::ShinobiLeap;
    NinjaEntityId owner_id = 0U;
    glm::vec3 start {0.0F};
    glm::vec3 requested_end {0.0F};
    float maximum_distance = 0.0F;
};

struct NinjaSafeMovementResult {
    bool valid = false;
    bool reached_requested_end = false;
    glm::vec3 destination {0.0F};
};

struct NinjaMovementCommit {
    NinjaEntityId owner_id = 0U;
    NinjaActivationId activation_id = 0U;
    AbilityId ability_id = AbilityId::None;
    glm::vec3 start {0.0F};
    glm::vec3 destination {0.0F};
};

struct NinjaDamageRequest {
    NinjaEntityId owner_id = 0U;
    NinjaEntityId target_id = 0U;
    NinjaActivationId activation_id = 0U;
    AbilityId ability_id = AbilityId::None;
    NinjaDamagePass pass = NinjaDamagePass::LightningDash;
    std::uint8_t hit_index = 0U;
    float damage = 0.0F;
};

struct NinjaDamageResult {
    bool hit = false;
    bool killed = false;
    float applied_damage = 0.0F;
};

struct NinjaCooldownReductionRequest {
    NinjaEntityId owner_id = 0U;
    AbilityId ability_id = AbilityId::None;
    float remaining_fraction_to_remove = 0.0F;
};

struct NinjaKunaiTargetQuery {
    NinjaEntityId owner_id = 0U;
    NinjaEntityId previous_target_id = 0U;
    glm::vec3 origin {0.0F};
    std::span<const NinjaEntityId> excluded_targets {};
    std::uint8_t bounce_index = 0U;
};

struct NinjaWorldCallbacks {
    std::function<bool(const NinjaSmokePlacementRequest&)>
        validate_smoke_placement {};
    std::function<std::optional<NinjaEntitySnapshot>(NinjaEntityId)>
        entity_snapshot {};
    std::function<std::size_t(
        const NinjaEntityQuery&,
        std::span<NinjaEntitySnapshot>)>
        query_entities {};
    std::function<bool(const NinjaSmokeOcclusionQuery&)>
        smoke_occludes {};
    std::function<void(const NinjaBreakTargetRequest&)>
        break_target_lock {};
    std::function<void(const NinjaModifierRequest&)>
        apply_modifier {};
    std::function<NinjaSafeMovementResult(
        const NinjaSafeMovementRequest&)>
        find_safe_movement {};
    std::function<bool(const NinjaMovementCommit&)>
        commit_movement {};
    std::function<NinjaDamageResult(const NinjaDamageRequest&)>
        apply_damage {};
    std::function<void(const NinjaCooldownReductionRequest&)>
        reduce_cooldown {};
    std::function<std::optional<NinjaEntitySnapshot>(
        const NinjaKunaiTargetQuery&)>
        select_kunai_target {};
};

struct NinjaSmokeBombCastResult {
    bool cast = false;
    NinjaEffectFailure failure = NinjaEffectFailure::None;
    glm::vec3 center {0.0F};
    NinjaSmokeBombTuning tuning {};
};

struct NinjaSmokeAttackRequest {
    NinjaEntityId owner_id = 0U;
    NinjaEntityId target_id = 0U;
    glm::vec3 origin {0.0F};
    glm::vec3 impact {0.0F};
    bool attack_landed = false;
};

struct NinjaSmokeAttackResult {
    bool empowered = false;
    float bonus_damage_fraction = 0.0F;
    float slow_fraction = 0.0F;
    float slow_duration_seconds = 0.0F;
};

struct NinjaShinobiLeapCastRequest {
    glm::vec3 start {0.0F};
    glm::vec3 direction {0.0F};
    bool airborne = false;
    bool use_free_second_impulse = false;
};

struct NinjaShinobiLeapCastResult {
    bool moved = false;
    bool consumes_energy_and_cooldown = true;
    bool armed_free_second_impulse = false;
    NinjaEffectFailure failure = NinjaEffectFailure::None;
    glm::vec3 destination {0.0F};
    float requested_distance = 0.0F;
};

struct NinjaHitOutcome {
    NinjaEntityId target_id = 0U;
    NinjaDamagePass pass = NinjaDamagePass::LightningDash;
    float requested_damage = 0.0F;
    float applied_damage = 0.0F;
    bool hit = false;
    bool killed = false;
};

struct NinjaContactRequest {
    NinjaEntityId owner_id = 0U;
    glm::vec3 position {0.0F};
    NinjaContactKind kind = NinjaContactKind::Ground;
};

struct NinjaLandingResult {
    bool mastery_triggered = false;
    bool cancel_fall_damage = false;
    NinjaEffectFailure failure = NinjaEffectFailure::None;
    std::array<NinjaHitOutcome, kNinjaMaximumQueryEntities> hits {};
    std::size_t hit_count = 0U;
};

struct NinjaLightningDashCastRequest {
    glm::vec3 start {0.0F};
    glm::vec3 direction {0.0F};
    std::optional<NinjaEntitySnapshot> primary_target {};
};

struct NinjaLightningDashCastResult {
    bool moved = false;
    NinjaEffectFailure failure = NinjaEffectFailure::None;
    glm::vec3 destination {0.0F};
    std::array<NinjaHitOutcome, kNinjaMaximumQueryEntities> hits {};
    std::size_t hit_count = 0U;
    std::size_t kill_count = 0U;
    std::uint8_t remaining_mastery_charges = 0U;
    bool query_saturated = false;
};

struct NinjaSpectralKunaiCastRequest {
    NinjaEntityId first_target_id = 0U;
};

struct NinjaSpectralKunaiCastResult {
    bool cast = false;
    NinjaEffectFailure failure = NinjaEffectFailure::None;
    std::array<NinjaHitOutcome, kNinjaMaximumKunaiHits> hits {};
    std::size_t hit_count = 0U;
    std::size_t outward_hit_count = 0U;
    std::size_t return_hit_count = 0U;
    bool mark_applied = false;
};

struct NinjaMeleeMarkRequest {
    NinjaEntityId owner_id = 0U;
    NinjaEntityId target_id = 0U;
    float melee_damage = 0.0F;
    bool melee_hit = false;
};

struct NinjaMeleeMarkResult {
    bool consumed = false;
    float bonus_damage_fraction = 0.0F;
    float bonus_damage = 0.0F;
};

struct NinjaFixedUpdateResult {
    bool accepted = false;
    bool saturated = false;
    std::uint32_t simulated_ticks = 0U;
    std::size_t lock_break_count = 0U;
    std::size_t modifier_application_count = 0U;
    std::size_t expired_mark_count = 0U;
    std::size_t recharged_dash_charge_count = 0U;
    bool smoke_expired = false;
    bool landing_window_expired = false;
};

struct NinjaSmokeStateView {
    bool active = false;
    NinjaEntityId owner_id = 0U;
    NinjaActivationId activation_id = 0U;
    glm::vec3 center {0.0F};
    float radius = 0.0F;
    float remaining_seconds = 0.0F;
    NinjaAbilityRank rank = NinjaAbilityRank::RankOne;
    bool mastered = false;
    bool attack_bonus_available = false;
};

struct NinjaLightningDashChargeState {
    std::array<float, 2U> remaining_recharge_seconds {};
};

class NinjaAbilityEffects {
public:
    [[nodiscard]] auto cast_smoke_bomb(
        const NinjaCastContext& context,
        const glm::vec3& target,
        const NinjaWorldCallbacks& callbacks)
        -> NinjaSmokeBombCastResult;

    [[nodiscard]] auto resolve_smoke_attack(
        const NinjaSmokeAttackRequest& request) noexcept
        -> NinjaSmokeAttackResult;

    [[nodiscard]] auto cast_shinobi_leap(
        const NinjaCastContext& context,
        const NinjaShinobiLeapCastRequest& request,
        const NinjaWorldCallbacks& callbacks)
        -> NinjaShinobiLeapCastResult;

    [[nodiscard]] auto handle_contact(
        const NinjaContactRequest& request,
        const NinjaWorldCallbacks& callbacks)
        -> NinjaLandingResult;

    [[nodiscard]] auto cast_lightning_dash(
        const NinjaCastContext& context,
        const NinjaLightningDashCastRequest& request,
        const NinjaWorldCallbacks& callbacks)
        -> NinjaLightningDashCastResult;

    [[nodiscard]] auto cast_spectral_kunai(
        const NinjaCastContext& context,
        const NinjaSpectralKunaiCastRequest& request,
        const NinjaWorldCallbacks& callbacks)
        -> NinjaSpectralKunaiCastResult;

    [[nodiscard]] auto consume_spectral_mark(
        const NinjaMeleeMarkRequest& request) noexcept
        -> NinjaMeleeMarkResult;

    [[nodiscard]] auto update(
        float dt,
        const NinjaWorldCallbacks& callbacks)
        -> NinjaFixedUpdateResult;

    [[nodiscard]] auto smoke_state() const noexcept
        -> NinjaSmokeStateView;
    [[nodiscard]] auto available_lightning_dash_charges() const noexcept
        -> std::uint8_t;
    [[nodiscard]] auto lightning_dash_charge_state() const noexcept
        -> NinjaLightningDashChargeState;
    [[nodiscard]] auto restore_lightning_dash_charge_state(
        const NinjaLightningDashChargeState& state) noexcept -> bool;
    [[nodiscard]] auto active_mark_count() const noexcept
        -> std::size_t;
    [[nodiscard]] auto free_shinobi_impulse_available() const noexcept
        -> bool;

    void clear() noexcept;

private:
    struct SmokeState {
        NinjaEntityId owner_id = 0U;
        NinjaActivationId activation_id = 0U;
        NinjaAbilityRank rank = NinjaAbilityRank::RankOne;
        glm::vec3 center {0.0F};
        std::uint32_t remaining_ticks = 0U;
        bool active = false;
        bool mastered = false;
        bool attack_bonus_available = false;
    };

    struct SmokeObserverState {
        NinjaEntityId enemy_id = 0U;
        NinjaEntityId target_id = 0U;
        std::uint32_t obscured_ticks = 0U;
        std::uint64_t last_seen_tick = 0U;
        bool active = false;
    };

    struct LandingState {
        NinjaEntityId owner_id = 0U;
        NinjaActivationId activation_id = 0U;
        std::uint32_t remaining_ticks = 0U;
        std::uint32_t player_level = 1U;
        float agility = 0.0F;
        bool active = false;
    };

    struct MarkState {
        NinjaEntityId owner_id = 0U;
        NinjaEntityId target_id = 0U;
        NinjaActivationId activation_id = 0U;
        std::uint32_t remaining_ticks = 0U;
        float melee_bonus = 0.0F;
        bool active = false;
    };

    struct RecentDashHitState {
        NinjaEntityId target_id = 0U;
        std::uint32_t remaining_ticks = 0U;
        bool active = false;
    };

    void update_one_tick(
        const NinjaWorldCallbacks& callbacks,
        NinjaFixedUpdateResult& result);
    void update_smoke_tick(
        const NinjaWorldCallbacks& callbacks,
        NinjaFixedUpdateResult& result);
    void update_smoke_target(
        const NinjaEntitySnapshot& target,
        const NinjaWorldCallbacks& callbacks,
        NinjaFixedUpdateResult& result);
    void clear_smoke_observers() noexcept;
    void record_recent_dash_hit(NinjaEntityId target_id) noexcept;
    [[nodiscard]] auto recent_dash_hit_blocked(
        NinjaEntityId target_id) const noexcept -> bool;
    void apply_dash_kill_reduction(
        const NinjaCastContext& context,
        std::size_t consumed_charge_index,
        float fraction,
        const NinjaWorldCallbacks& callbacks);
    void apply_mark(
        const NinjaCastContext& context,
        NinjaEntityId target_id,
        float bonus) noexcept;

    double fixed_accumulator_seconds_ = 0.0;
    std::uint64_t simulation_tick_ = 0U;
    SmokeState smoke_ {};
    std::array<SmokeObserverState, kNinjaMaximumSmokeObservers>
        smoke_observers_ {};
    LandingState landing_ {};
    std::array<MarkState, kNinjaMaximumMarks> marks_ {};
    std::array<RecentDashHitState, kNinjaMaximumQueryEntities>
        recent_dash_hits_ {};
    std::array<std::uint32_t, 2U> dash_charge_ticks_ {};
    NinjaEntityId aerial_bond_owner_ = 0U;
    bool aerial_bond_used_ = false;
    bool free_second_impulse_available_ = false;
};

} // namespace valcraft
