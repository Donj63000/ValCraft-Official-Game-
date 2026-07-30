#pragma once

#include <glm/vec3.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>

namespace valcraft {

using CommanderActivationId = std::uint64_t;
using CommanderEntityId = std::uint64_t;
using CommanderUnitId = std::uint64_t;
using CommanderOwnerId = std::uint64_t;
using CommanderProjectileId = std::uint64_t;

inline constexpr std::size_t kCommanderMaximumCombatUnits = 8U;
inline constexpr std::size_t kFleetShooterMaximumShotsPerUpdate = 32U;
inline constexpr float kCommanderMasteryRetargetRadius = 8.0F;
inline constexpr float kCommanderMasteryOrderExtensionSeconds = 3.0F;
inline constexpr float kCommanderMasteryEnergyRefund = 5.0F;
inline constexpr float kCommanderAssaultVulnerability = 0.10F;
inline constexpr float kCommanderAssaultVulnerabilitySeconds = 5.0F;
inline constexpr float kFleetShooterPierceMultiplier = 0.60F;
inline constexpr float kWarBannerMasteryEnergyRegenerationBonus = 0.15F;
inline constexpr float kRampartProjectileBlockChance = 0.60F;

enum class CommanderRank : std::uint8_t {
    RankOne = 1,
    RankTwo = 2,
    RankThree = 3,
};

enum class CommanderEffectError : std::uint8_t {
    None = 0,
    InvalidRank,
    InvalidInput,
    InvalidTarget,
    InvalidPosition,
    LimitReached,
    AlreadyActive,
    CallbackRejected,
};

enum class CommanderEffectEndReason : std::uint8_t {
    Expired = 0,
    Replaced,
    Destroyed,
    Cleared,
};

enum class CommanderAuraRecipient : std::uint8_t {
    Player = 0,
    Invocation,
    Crew,
    Other,
};

enum class FleetShooterTargetPriority : std::uint8_t {
    MarkedByOrder = 0,
    AttackingPlayer,
    NearestHostile,
};

struct AssaultOrderSpec {
    float energy_cost = 0.0F;
    float cooldown_seconds = 0.0F;
    float duration_seconds = 0.0F;
    float movement_speed_bonus = 0.0F;
    float damage_bonus = 0.0F;
    float invocation_vulnerability = 0.0F;
    float vulnerability_seconds = 0.0F;
};

struct FleetShooterSpec {
    float energy_cost = 0.0F;
    float cooldown_seconds = 0.0F;
    float duration_seconds = 0.0F;
    float base_health = 0.0F;
    float range = 0.0F;
    float base_damage = 0.0F;
    float attack_interval_seconds = 0.0F;
    std::uint8_t piercing_shot_period = 0U;
    float piercing_damage_multiplier = 0.0F;
};

struct WarBannerSpec {
    float energy_cost = 0.0F;
    float cooldown_seconds = 0.0F;
    float duration_seconds = 0.0F;
    float radius = 0.0F;
    float base_health = 0.0F;
    float ally_damage_bonus = 0.0F;
    float invocation_healing_per_second = 0.0F;
    float player_healing_limit = 0.0F;
};

struct RampartFormationSpec {
    float energy_cost = 0.0F;
    float cooldown_seconds = 0.0F;
    float duration_seconds = 0.0F;
    float frontal_unit_damage_reduction = 0.0F;
    float protected_ally_damage_reduction = 0.0F;
    float crossing_projectile_block_chance = 0.0F;
};

[[nodiscard]] auto commander_rank_is_valid(CommanderRank rank) noexcept
    -> bool;
[[nodiscard]] auto assault_order_spec(CommanderRank rank) noexcept
    -> std::optional<AssaultOrderSpec>;
[[nodiscard]] auto fleet_shooter_spec(CommanderRank rank) noexcept
    -> std::optional<FleetShooterSpec>;
[[nodiscard]] auto war_banner_spec(CommanderRank rank) noexcept
    -> std::optional<WarBannerSpec>;
[[nodiscard]] auto rampart_formation_spec(CommanderRank rank) noexcept
    -> std::optional<RampartFormationSpec>;

[[nodiscard]] auto commander_summon_health_multiplier(
    std::uint16_t player_level,
    std::uint8_t wisdom) noexcept -> float;
[[nodiscard]] auto commander_summon_damage_multiplier(
    std::uint16_t player_level,
    std::uint8_t wisdom) noexcept -> float;

struct CommanderTarget {
    CommanderEntityId entity_id = 0U;
    glm::vec3 position {0.0F};
};

struct AssaultOrderActivationRequest {
    CommanderRank rank = CommanderRank::RankOne;
    bool mastered = false;
    std::optional<CommanderTarget> target {};
    glm::vec3 destination {0.0F};
};

struct AssaultOrderDispatch {
    CommanderActivationId activation_id = 0U;
    std::optional<CommanderTarget> target {};
    glm::vec3 destination {0.0F};
    bool retarget = false;
};

struct AssaultOrderRetargetRequest {
    CommanderActivationId activation_id = 0U;
    CommanderEntityId defeated_target_id = 0U;
    glm::vec3 search_origin {0.0F};
    float search_radius = kCommanderMasteryRetargetRadius;
};

struct AssaultVulnerabilityRequest {
    CommanderActivationId activation_id = 0U;
    CommanderUnitId unit_id = 0U;
    CommanderEntityId target_id = 0U;
    float amount = kCommanderAssaultVulnerability;
    float duration_seconds = kCommanderAssaultVulnerabilitySeconds;
};

struct AssaultOrderCallbacks {
    std::function<bool(const CommanderTarget&)> validate_target {};
    std::function<bool(const glm::vec3&)> validate_destination {};
    std::function<bool(const AssaultOrderDispatch&)> dispatch_order {};
    std::function<void(
        CommanderActivationId,
        CommanderEffectEndReason)> order_ended {};
    std::function<void(const AssaultVulnerabilityRequest&)>
        apply_invocation_vulnerability {};
    std::function<std::optional<CommanderTarget>(
        const AssaultOrderRetargetRequest&)> acquire_replacement_target {};
    std::function<void(float)> refund_energy {};
};

struct CommanderActivationResult {
    bool activated = false;
    CommanderEffectError error = CommanderEffectError::None;
    CommanderActivationId activation_id = 0U;
};

struct AssaultOrderAttackResult {
    bool order_active = false;
    bool vulnerability_applied = false;
};

struct AssaultOrderRetargetResult {
    bool handled = false;
    bool retargeted = false;
    bool duration_extended = false;
    float energy_refund = 0.0F;
    std::optional<CommanderTarget> replacement_target {};
};

struct AssaultOrderStateView {
    bool active = false;
    CommanderActivationId activation_id = 0U;
    CommanderRank rank = CommanderRank::RankOne;
    bool mastered = false;
    std::optional<CommanderTarget> target {};
    glm::vec3 destination {0.0F};
    float remaining_seconds = 0.0F;
    float movement_speed_bonus = 0.0F;
    float damage_bonus = 0.0F;
    bool mastery_retarget_used = false;
};

class AssaultOrderSystem {
public:
    [[nodiscard]] auto activate(
        const AssaultOrderActivationRequest& request,
        const AssaultOrderCallbacks& callbacks)
        -> CommanderActivationResult;
    [[nodiscard]] auto update(
        float dt,
        const AssaultOrderCallbacks& callbacks) -> bool;
    [[nodiscard]] auto notify_unit_attack(
        CommanderUnitId unit_id,
        CommanderEntityId target_id,
        const AssaultOrderCallbacks& callbacks)
        -> AssaultOrderAttackResult;
    [[nodiscard]] auto notify_target_defeated(
        CommanderEntityId target_id,
        const glm::vec3& defeat_position,
        const AssaultOrderCallbacks& callbacks)
        -> AssaultOrderRetargetResult;
    void clear(const AssaultOrderCallbacks& callbacks);

    [[nodiscard]] auto state() const noexcept -> AssaultOrderStateView;

private:
    [[nodiscard]] auto next_activation_id() noexcept
        -> CommanderActivationId;
    void end(
        CommanderEffectEndReason reason,
        const AssaultOrderCallbacks& callbacks);
    [[nodiscard]] auto vulnerability_already_used(
        CommanderUnitId unit_id) const noexcept -> bool;
    [[nodiscard]] auto remember_vulnerability_unit(
        CommanderUnitId unit_id) noexcept -> bool;

    CommanderActivationId next_activation_id_ = 1U;
    CommanderActivationId activation_id_ = 0U;
    CommanderRank rank_ = CommanderRank::RankOne;
    std::optional<CommanderTarget> target_ {};
    glm::vec3 destination_ {0.0F};
    double remaining_seconds_ = 0.0;
    std::array<CommanderUnitId, kCommanderMaximumCombatUnits>
        vulnerability_units_ {};
    std::size_t vulnerability_unit_count_ = 0U;
    bool active_ = false;
    bool mastered_ = false;
    bool mastery_retarget_used_ = false;
};

struct FleetShooterSpawnRequest {
    CommanderOwnerId owner_id = 0U;
    glm::vec3 position {0.0F};
    CommanderRank rank = CommanderRank::RankOne;
    std::uint16_t player_level = 1U;
    std::uint8_t wisdom = 0U;
    std::size_t active_combat_invocations = 0U;
    bool mastered = false;
};

struct FleetShooterAcquireRequest {
    CommanderActivationId shooter_id = 0U;
    CommanderOwnerId owner_id = 0U;
    glm::vec3 origin {0.0F};
    float range = 0.0F;
    FleetShooterTargetPriority priority =
        FleetShooterTargetPriority::MarkedByOrder;
};

struct FleetShooterFireRequest {
    CommanderActivationId shooter_id = 0U;
    CommanderOwnerId owner_id = 0U;
    CommanderTarget target {};
    glm::vec3 origin {0.0F};
    std::uint32_t shot_number = 0U;
    float range = 0.0F;
    float damage = 0.0F;
    bool pierces_first_target = false;
    float secondary_damage_multiplier = 0.0F;
    bool interrupts_light_target = false;
};

struct FleetShooterFireResult {
    bool fired = false;
    bool primary_hit = false;
    bool primary_killed = false;
    bool secondary_hit = false;
    bool secondary_killed = false;
    float primary_applied_damage = 0.0F;
    float secondary_applied_damage = 0.0F;
};

struct FleetShooterCallbacks {
    std::function<bool(const FleetShooterSpawnRequest&)> validate_spawn {};
    std::function<std::optional<CommanderTarget>(
        const FleetShooterAcquireRequest&)> acquire_target {};
    std::function<FleetShooterFireResult(
        const FleetShooterFireRequest&)> fire_shot {};
    std::function<void(
        CommanderActivationId,
        CommanderEffectEndReason)> shooter_ended {};
};

struct FleetShooterShotOutcome {
    std::uint32_t shot_number = 0U;
    FleetShooterTargetPriority selected_priority =
        FleetShooterTargetPriority::MarkedByOrder;
    CommanderEntityId target_id = 0U;
    float requested_damage = 0.0F;
    bool mastery_first_salvo = false;
    bool piercing_shot = false;
    FleetShooterFireResult result {};
};

struct FleetShooterUpdateResult {
    std::array<
        FleetShooterShotOutcome,
        kFleetShooterMaximumShotsPerUpdate>
        shots {};
    std::size_t shot_count = 0U;
    std::size_t acquisition_attempt_count = 0U;
    bool expired = false;

    [[nodiscard]] auto shot_results() const noexcept
        -> std::span<const FleetShooterShotOutcome> {
        return {shots.data(), shot_count};
    }
};

struct FleetShooterStateView {
    bool active = false;
    CommanderActivationId shooter_id = 0U;
    CommanderOwnerId owner_id = 0U;
    CommanderRank rank = CommanderRank::RankOne;
    glm::vec3 position {0.0F};
    float health = 0.0F;
    float maximum_health = 0.0F;
    float damage = 0.0F;
    float range = 0.0F;
    float remaining_seconds = 0.0F;
    std::uint32_t shots_fired = 0U;
    bool mastered = false;
    bool first_salvo_available = false;
};

struct FleetShooterDamageResult {
    bool handled = false;
    bool destroyed = false;
    float applied_damage = 0.0F;
    float remaining_health = 0.0F;
};

class FleetShooterSystem {
public:
    [[nodiscard]] auto summon(
        const FleetShooterSpawnRequest& request,
        const FleetShooterCallbacks& callbacks)
        -> CommanderActivationResult;
    [[nodiscard]] auto update(
        float dt,
        const FleetShooterCallbacks& callbacks)
        -> FleetShooterUpdateResult;
    [[nodiscard]] auto apply_damage(
        float damage,
        const FleetShooterCallbacks& callbacks)
        -> FleetShooterDamageResult;
    void set_position(const glm::vec3& position) noexcept;
    void clear(const FleetShooterCallbacks& callbacks);

    [[nodiscard]] auto state() const noexcept -> FleetShooterStateView;

private:
    [[nodiscard]] auto next_shooter_id() noexcept
        -> CommanderActivationId;
    [[nodiscard]] auto acquire_target(
        const FleetShooterCallbacks& callbacks,
        FleetShooterTargetPriority& selected_priority)
        const -> std::optional<CommanderTarget>;
    void end(
        CommanderEffectEndReason reason,
        const FleetShooterCallbacks& callbacks);

    CommanderActivationId next_shooter_id_ = 1U;
    CommanderActivationId shooter_id_ = 0U;
    CommanderOwnerId owner_id_ = 0U;
    CommanderRank rank_ = CommanderRank::RankOne;
    glm::vec3 position_ {0.0F};
    FleetShooterSpec spec_ {};
    double age_seconds_ = 0.0;
    double next_shot_seconds_ = 0.0;
    float maximum_health_ = 0.0F;
    float health_ = 0.0F;
    float damage_ = 0.0F;
    std::uint32_t shots_fired_ = 0U;
    bool active_ = false;
    bool mastered_ = false;
    bool first_salvo_available_ = false;
};

struct WarBannerPlacementRequest {
    CommanderOwnerId owner_id = 0U;
    glm::vec3 position {0.0F};
    CommanderRank rank = CommanderRank::RankOne;
    std::uint16_t player_level = 1U;
    std::uint8_t wisdom = 0U;
    bool mastered = false;
};

struct WarBannerPulseRequest {
    CommanderActivationId banner_id = 0U;
    CommanderOwnerId owner_id = 0U;
    glm::vec3 position {0.0F};
    float radius = 0.0F;
    float active_seconds = 0.0F;
    float invocation_healing_per_unit = 0.0F;
    float requested_player_healing = 0.0F;
};

struct WarBannerPulseResult {
    float applied_player_healing = 0.0F;
};

struct WarBannerCallbacks {
    std::function<bool(const WarBannerPlacementRequest&)>
        validate_placement {};
    std::function<WarBannerPulseResult(
        const WarBannerPulseRequest&)> apply_healing_pulse {};
    std::function<void(
        CommanderActivationId,
        CommanderEffectEndReason)> banner_ended {};
};

struct WarBannerAuraSample {
    bool inside = false;
    float ally_damage_bonus = 0.0F;
    float invocation_healing_per_second = 0.0F;
    float player_energy_regeneration_bonus = 0.0F;
};

struct WarBannerStateView {
    bool active = false;
    CommanderActivationId banner_id = 0U;
    CommanderOwnerId owner_id = 0U;
    CommanderRank rank = CommanderRank::RankOne;
    glm::vec3 position {0.0F};
    float health = 0.0F;
    float maximum_health = 0.0F;
    float remaining_seconds = 0.0F;
    float radius = 0.0F;
    float player_healing_applied = 0.0F;
    float player_healing_limit = 0.0F;
    bool mastered = false;
};

struct WarBannerUpdateResult {
    bool expired = false;
    float active_seconds = 0.0F;
    float requested_player_healing = 0.0F;
    float applied_player_healing = 0.0F;
};

struct WarBannerDamageResult {
    bool handled = false;
    bool destroyed = false;
    float applied_damage = 0.0F;
    float remaining_health = 0.0F;
};

class WarBannerSystem {
public:
    [[nodiscard]] auto place(
        const WarBannerPlacementRequest& request,
        const WarBannerCallbacks& callbacks)
        -> CommanderActivationResult;
    [[nodiscard]] auto update(
        float dt,
        const glm::vec3& player_position,
        const WarBannerCallbacks& callbacks)
        -> WarBannerUpdateResult;
    [[nodiscard]] auto sample_aura(
        const glm::vec3& position,
        CommanderAuraRecipient recipient) const noexcept
        -> WarBannerAuraSample;
    [[nodiscard]] auto apply_damage(
        float damage,
        const WarBannerCallbacks& callbacks)
        -> WarBannerDamageResult;
    void clear(const WarBannerCallbacks& callbacks);

    [[nodiscard]] auto state() const noexcept -> WarBannerStateView;

private:
    [[nodiscard]] auto next_banner_id() noexcept
        -> CommanderActivationId;
    void end(
        CommanderEffectEndReason reason,
        const WarBannerCallbacks& callbacks);

    CommanderActivationId next_banner_id_ = 1U;
    CommanderActivationId banner_id_ = 0U;
    CommanderOwnerId owner_id_ = 0U;
    CommanderRank rank_ = CommanderRank::RankOne;
    glm::vec3 position_ {0.0F};
    WarBannerSpec spec_ {};
    double remaining_seconds_ = 0.0;
    double healing_schedule_seconds_ = 0.0;
    double healing_dispatched_seconds_ = 0.0;
    float maximum_health_ = 0.0F;
    float health_ = 0.0F;
    float player_healing_applied_ = 0.0F;
    bool active_ = false;
    bool mastered_ = false;
};

struct RampartFormationActivationRequest {
    CommanderRank rank = CommanderRank::RankOne;
    bool mastered = false;
    glm::vec3 anchor {0.0F};
    glm::vec3 forward {0.0F, 0.0F, 1.0F};
    std::span<const CommanderUnitId> unit_ids {};
    bool on_dynamic_ship = false;
};

struct RampartFormationDispatch {
    CommanderActivationId activation_id = 0U;
    glm::vec3 anchor {0.0F};
    glm::vec3 forward {0.0F, 0.0F, 1.0F};
    std::array<CommanderUnitId, kCommanderMaximumCombatUnits> unit_ids {};
    std::size_t unit_count = 0U;
    bool on_dynamic_ship = false;

    [[nodiscard]] auto units() const noexcept
        -> std::span<const CommanderUnitId> {
        return {unit_ids.data(), unit_count};
    }
};

struct RampartFormationCallbacks {
    std::function<bool(const RampartFormationDispatch&)>
        dispatch_formation {};
    std::function<void(
        CommanderActivationId,
        CommanderEffectEndReason)> formation_ended {};
};

struct RampartUnitDefenseSample {
    bool in_formation = false;
    bool stop_distant_pursuit = false;
    float frontal_damage_reduction = 0.0F;
};

struct RampartAllyDefenseSample {
    bool protected_by_line = false;
    float damage_reduction = 0.0F;
};

struct RampartRangedAttackResult {
    bool handled = false;
    bool completely_blocked = false;
    bool mastery_consumed = false;
};

struct RampartProjectileCrossingResult {
    bool handled = false;
    bool blocked = false;
    float block_chance = 0.0F;
};

struct RampartFormationStateView {
    bool active = false;
    CommanderActivationId activation_id = 0U;
    CommanderRank rank = CommanderRank::RankOne;
    glm::vec3 anchor {0.0F};
    glm::vec3 forward {0.0F, 0.0F, 1.0F};
    float remaining_seconds = 0.0F;
    float frontal_unit_damage_reduction = 0.0F;
    float protected_ally_damage_reduction = 0.0F;
    std::size_t unit_count = 0U;
    bool mastered = false;
    bool on_dynamic_ship = false;
};

class RampartFormationSystem {
public:
    [[nodiscard]] auto activate(
        const RampartFormationActivationRequest& request,
        const RampartFormationCallbacks& callbacks)
        -> CommanderActivationResult;
    [[nodiscard]] auto update(
        float dt,
        const RampartFormationCallbacks& callbacks) -> bool;
    [[nodiscard]] auto sample_unit_defense(
        CommanderUnitId unit_id,
        bool attack_from_front) const noexcept
        -> RampartUnitDefenseSample;
    [[nodiscard]] auto sample_ally_defense(
        bool actually_behind_line) const noexcept
        -> RampartAllyDefenseSample;
    [[nodiscard]] auto resolve_unit_ranged_attack(
        CommanderUnitId unit_id,
        bool unstoppable_boss_attack) noexcept
        -> RampartRangedAttackResult;
    [[nodiscard]] auto resolve_crossing_projectile(
        CommanderProjectileId projectile_id,
        bool actually_crosses_line) const noexcept
        -> RampartProjectileCrossingResult;
    void clear(const RampartFormationCallbacks& callbacks);

    [[nodiscard]] auto state() const noexcept
        -> RampartFormationStateView;

private:
    [[nodiscard]] auto next_activation_id() noexcept
        -> CommanderActivationId;
    [[nodiscard]] auto unit_index(
        CommanderUnitId unit_id) const noexcept
        -> std::optional<std::size_t>;
    void end(
        CommanderEffectEndReason reason,
        const RampartFormationCallbacks& callbacks);

    CommanderActivationId next_activation_id_ = 1U;
    CommanderActivationId activation_id_ = 0U;
    CommanderRank rank_ = CommanderRank::RankOne;
    glm::vec3 anchor_ {0.0F};
    glm::vec3 forward_ {0.0F, 0.0F, 1.0F};
    RampartFormationSpec spec_ {};
    double remaining_seconds_ = 0.0;
    std::array<CommanderUnitId, kCommanderMaximumCombatUnits> unit_ids_ {};
    std::array<bool, kCommanderMaximumCombatUnits>
        mastery_blocks_consumed_ {};
    std::size_t unit_count_ = 0U;
    bool active_ = false;
    bool mastered_ = false;
    bool on_dynamic_ship_ = false;
};

} // namespace valcraft
