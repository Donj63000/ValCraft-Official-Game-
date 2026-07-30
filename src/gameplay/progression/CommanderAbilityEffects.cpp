#include "gameplay/progression/CommanderAbilityEffects.h"

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace valcraft {
namespace {

constexpr double kCommanderTimeEpsilon = 1.0e-7;
constexpr float kMinimumDirectionLengthSquared = 1.0e-8F;
constexpr float kScaledStatPrecision = 100.0F;

[[nodiscard]] auto finite_vec3(const glm::vec3& value) noexcept -> bool {
    return std::isfinite(value.x) &&
           std::isfinite(value.y) &&
           std::isfinite(value.z);
}

[[nodiscard]] auto finite_positive(float value) noexcept -> bool {
    return std::isfinite(value) && value > 0.0F;
}

[[nodiscard]] auto target_is_valid(const CommanderTarget& target) noexcept
    -> bool {
    return target.entity_id != 0U && finite_vec3(target.position);
}

[[nodiscard]] auto distance_squared(
    const glm::vec3& lhs,
    const glm::vec3& rhs) noexcept -> float {
    const auto delta = lhs - rhs;
    return glm::dot(delta, delta);
}

[[nodiscard]] auto rounded_scaled_stat(
    float base_value,
    float multiplier) noexcept -> float {
    const auto scaled = base_value * multiplier;
    return std::floor(
               scaled * kScaledStatPrecision +
               0.5F) /
           kScaledStatPrecision;
}

[[nodiscard]] auto saturating_float(double value) noexcept -> float {
    if (!std::isfinite(value) || value <= 0.0) {
        return 0.0F;
    }
    return static_cast<float>(
        std::min(
            value,
            static_cast<double>(
                std::numeric_limits<float>::max())));
}

[[nodiscard]] auto bounded_callback_damage(
    float value,
    float maximum) noexcept -> float {
    if (!std::isfinite(value) ||
        !std::isfinite(maximum) ||
        maximum <= 0.0F) {
        return 0.0F;
    }
    return std::clamp(
        value,
        0.0F,
        maximum);
}

[[nodiscard]] auto sanitized_fire_result(
    FleetShooterFireResult result,
    float primary_damage,
    float secondary_multiplier,
    bool piercing) noexcept -> FleetShooterFireResult {
    // Je ne laisse jamais un adaptateur de combat propager un nombre non
    // fini dans le journal déterministe du tireur.
    result.primary_applied_damage =
        bounded_callback_damage(
            result.primary_applied_damage,
            primary_damage);
    if (!piercing) {
        result.secondary_hit = false;
        result.secondary_killed = false;
        result.secondary_applied_damage = 0.0F;
    } else {
        result.secondary_applied_damage =
            bounded_callback_damage(
                result.secondary_applied_damage,
                primary_damage *
                    secondary_multiplier);
    }
    return result;
}

[[nodiscard]] auto splitmix64(std::uint64_t value) noexcept
    -> std::uint64_t {
    value += 0x9E3779B97F4A7C15ULL;
    value =
        (value ^ (value >> 30U)) *
        0xBF58476D1CE4E5B9ULL;
    value =
        (value ^ (value >> 27U)) *
        0x94D049BB133111EBULL;
    return value ^ (value >> 31U);
}

[[nodiscard]] auto deterministic_sixty_percent(
    std::uint64_t value) noexcept -> bool {
    constexpr auto threshold =
        static_cast<std::uint64_t>(
            static_cast<long double>(
                std::numeric_limits<std::uint64_t>::max()) *
            static_cast<long double>(
                kRampartProjectileBlockChance));
    return splitmix64(value) <= threshold;
}

} // namespace

auto commander_rank_is_valid(CommanderRank rank) noexcept -> bool {
    switch (rank) {
    case CommanderRank::RankOne:
    case CommanderRank::RankTwo:
    case CommanderRank::RankThree:
        return true;
    default:
        return false;
    }
}

auto assault_order_spec(CommanderRank rank) noexcept
    -> std::optional<AssaultOrderSpec> {
    switch (rank) {
    case CommanderRank::RankOne:
        return AssaultOrderSpec {
            10.0F,
            10.0F,
            6.0F,
            0.20F,
            0.15F,
            0.0F,
            0.0F,
        };
    case CommanderRank::RankTwo:
        return AssaultOrderSpec {
            10.0F,
            8.0F,
            8.0F,
            0.30F,
            0.20F,
            0.0F,
            0.0F,
        };
    case CommanderRank::RankThree:
        return AssaultOrderSpec {
            10.0F,
            6.0F,
            10.0F,
            0.40F,
            0.25F,
            kCommanderAssaultVulnerability,
            kCommanderAssaultVulnerabilitySeconds,
        };
    default:
        return std::nullopt;
    }
}

auto fleet_shooter_spec(CommanderRank rank) noexcept
    -> std::optional<FleetShooterSpec> {
    switch (rank) {
    case CommanderRank::RankOne:
        return FleetShooterSpec {
            30.0F,
            24.0F,
            20.0F,
            9.0F,
            14.0F,
            4.0F,
            2.4F,
            0U,
            0.0F,
        };
    case CommanderRank::RankTwo:
        return FleetShooterSpec {
            30.0F,
            22.0F,
            25.0F,
            12.0F,
            16.0F,
            5.0F,
            2.2F,
            0U,
            0.0F,
        };
    case CommanderRank::RankThree:
        return FleetShooterSpec {
            30.0F,
            20.0F,
            30.0F,
            15.0F,
            18.0F,
            6.0F,
            2.0F,
            4U,
            kFleetShooterPierceMultiplier,
        };
    default:
        return std::nullopt;
    }
}

auto war_banner_spec(CommanderRank rank) noexcept
    -> std::optional<WarBannerSpec> {
    switch (rank) {
    case CommanderRank::RankOne:
        return WarBannerSpec {
            30.0F,
            28.0F,
            12.0F,
            6.0F,
            12.0F,
            0.10F,
            0.25F,
            3.0F,
        };
    case CommanderRank::RankTwo:
        return WarBannerSpec {
            30.0F,
            24.0F,
            15.0F,
            8.0F,
            16.0F,
            0.15F,
            0.50F,
            4.0F,
        };
    case CommanderRank::RankThree:
        return WarBannerSpec {
            30.0F,
            20.0F,
            18.0F,
            10.0F,
            20.0F,
            0.20F,
            0.75F,
            5.0F,
        };
    default:
        return std::nullopt;
    }
}

auto rampart_formation_spec(CommanderRank rank) noexcept
    -> std::optional<RampartFormationSpec> {
    switch (rank) {
    case CommanderRank::RankOne:
        return RampartFormationSpec {
            25.0F,
            20.0F,
            6.0F,
            0.25F,
            0.15F,
            0.0F,
        };
    case CommanderRank::RankTwo:
        return RampartFormationSpec {
            25.0F,
            18.0F,
            8.0F,
            0.35F,
            0.25F,
            0.0F,
        };
    case CommanderRank::RankThree:
        return RampartFormationSpec {
            25.0F,
            16.0F,
            10.0F,
            0.45F,
            0.30F,
            kRampartProjectileBlockChance,
        };
    default:
        return std::nullopt;
    }
}

auto commander_summon_health_multiplier(
    std::uint16_t player_level,
    std::uint8_t wisdom) noexcept -> float {
    if (player_level == 0U) {
        return 1.0F;
    }
    const auto bounded_level =
        std::min<std::uint16_t>(
            player_level,
            100U);
    const auto bounded_wisdom =
        std::min<std::uint8_t>(
            wisdom,
            15U);
    return 1.0F +
           0.005F *
               static_cast<float>(
                   bounded_level - 1U) +
           0.02F *
               static_cast<float>(
                   bounded_wisdom);
}

auto commander_summon_damage_multiplier(
    std::uint16_t player_level,
    std::uint8_t wisdom) noexcept -> float {
    if (player_level == 0U) {
        return 1.0F;
    }
    const auto bounded_level =
        std::min<std::uint16_t>(
            player_level,
            100U);
    const auto bounded_wisdom =
        std::min<std::uint8_t>(
            wisdom,
            15U);
    return 1.0F +
           0.003F *
               static_cast<float>(
                   bounded_level - 1U) +
           0.02F *
               static_cast<float>(
                   bounded_wisdom);
}

auto AssaultOrderSystem::next_activation_id() noexcept
    -> CommanderActivationId {
    const auto result = next_activation_id_;
    ++next_activation_id_;
    if (next_activation_id_ == 0U) {
        next_activation_id_ = 1U;
    }
    return result;
}

auto AssaultOrderSystem::activate(
    const AssaultOrderActivationRequest& request,
    const AssaultOrderCallbacks& callbacks)
    -> CommanderActivationResult {
    const auto spec =
        assault_order_spec(request.rank);
    if (!spec.has_value()) {
        return {
            false,
            CommanderEffectError::InvalidRank,
            0U,
        };
    }

    const auto destination =
        request.target.has_value()
            ? request.target->position
            : request.destination;
    if (!finite_vec3(destination)) {
        return {
            false,
            CommanderEffectError::InvalidPosition,
            0U,
        };
    }

    if (request.target.has_value()) {
        if (!target_is_valid(*request.target)) {
            return {
                false,
                CommanderEffectError::InvalidTarget,
                0U,
            };
        }
        if (callbacks.validate_target &&
            !callbacks.validate_target(
                *request.target)) {
            return {
                false,
                CommanderEffectError::InvalidTarget,
                0U,
            };
        }
    } else if (
        callbacks.validate_destination &&
        !callbacks.validate_destination(
            destination)) {
        return {
            false,
            CommanderEffectError::InvalidPosition,
            0U,
        };
    }

    const auto candidate_id =
        next_activation_id();
    const AssaultOrderDispatch dispatch {
        candidate_id,
        request.target,
        destination,
        false,
    };
    if (callbacks.dispatch_order &&
        !callbacks.dispatch_order(dispatch)) {
        return {
            false,
            CommanderEffectError::CallbackRejected,
            0U,
        };
    }

    if (active_) {
        end(
            CommanderEffectEndReason::Replaced,
            callbacks);
    }

    activation_id_ = candidate_id;
    rank_ = request.rank;
    target_ = request.target;
    destination_ = destination;
    remaining_seconds_ =
        static_cast<double>(
            spec->duration_seconds);
    vulnerability_units_.fill(0U);
    vulnerability_unit_count_ = 0U;
    active_ = true;
    mastered_ = request.mastered;
    mastery_retarget_used_ = false;
    return {
        true,
        CommanderEffectError::None,
        activation_id_,
    };
}

auto AssaultOrderSystem::update(
    float dt,
    const AssaultOrderCallbacks& callbacks) -> bool {
    if (!active_ ||
        !finite_positive(dt)) {
        return false;
    }

    remaining_seconds_ =
        std::max(
            0.0,
            remaining_seconds_ -
                static_cast<double>(dt));
    if (remaining_seconds_ >
        kCommanderTimeEpsilon) {
        return false;
    }

    end(
        CommanderEffectEndReason::Expired,
        callbacks);
    return true;
}

auto AssaultOrderSystem::vulnerability_already_used(
    CommanderUnitId unit_id) const noexcept -> bool {
    return std::find(
               vulnerability_units_.begin(),
               vulnerability_units_.begin() +
                   static_cast<std::ptrdiff_t>(
                       vulnerability_unit_count_),
               unit_id) !=
           vulnerability_units_.begin() +
               static_cast<std::ptrdiff_t>(
                   vulnerability_unit_count_);
}

auto AssaultOrderSystem::remember_vulnerability_unit(
    CommanderUnitId unit_id) noexcept -> bool {
    if (vulnerability_already_used(unit_id) ||
        vulnerability_unit_count_ >=
            vulnerability_units_.size()) {
        return false;
    }
    vulnerability_units_[vulnerability_unit_count_] =
        unit_id;
    ++vulnerability_unit_count_;
    return true;
}

auto AssaultOrderSystem::notify_unit_attack(
    CommanderUnitId unit_id,
    CommanderEntityId target_id,
    const AssaultOrderCallbacks& callbacks)
    -> AssaultOrderAttackResult {
    AssaultOrderAttackResult result {};
    result.order_active = active_;
    if (!active_ ||
        rank_ != CommanderRank::RankThree ||
        unit_id == 0U ||
        target_id == 0U ||
        !callbacks.apply_invocation_vulnerability ||
        !remember_vulnerability_unit(unit_id)) {
        return result;
    }

    callbacks.apply_invocation_vulnerability(
        AssaultVulnerabilityRequest {
            activation_id_,
            unit_id,
            target_id,
            kCommanderAssaultVulnerability,
            kCommanderAssaultVulnerabilitySeconds,
        });
    result.vulnerability_applied = true;
    return result;
}

auto AssaultOrderSystem::notify_target_defeated(
    CommanderEntityId target_id,
    const glm::vec3& defeat_position,
    const AssaultOrderCallbacks& callbacks)
    -> AssaultOrderRetargetResult {
    AssaultOrderRetargetResult result {};
    if (!active_ ||
        !target_.has_value() ||
        target_->entity_id != target_id) {
        return result;
    }
    result.handled = true;
    if (!mastered_ ||
        mastery_retarget_used_ ||
        !finite_vec3(defeat_position)) {
        return result;
    }

    mastery_retarget_used_ = true;
    if (!callbacks.acquire_replacement_target) {
        return result;
    }

    const auto replacement =
        callbacks.acquire_replacement_target(
            AssaultOrderRetargetRequest {
                activation_id_,
                target_id,
                defeat_position,
                kCommanderMasteryRetargetRadius,
            });
    if (!replacement.has_value() ||
        !target_is_valid(*replacement) ||
        replacement->entity_id == target_id ||
        distance_squared(
            defeat_position,
            replacement->position) >
            kCommanderMasteryRetargetRadius *
                kCommanderMasteryRetargetRadius ||
        (callbacks.validate_target &&
         !callbacks.validate_target(
             *replacement))) {
        return result;
    }

    const AssaultOrderDispatch dispatch {
        activation_id_,
        replacement,
        replacement->position,
        true,
    };
    if (callbacks.dispatch_order &&
        !callbacks.dispatch_order(dispatch)) {
        return result;
    }

    target_ = replacement;
    destination_ = replacement->position;
    remaining_seconds_ +=
        static_cast<double>(
            kCommanderMasteryOrderExtensionSeconds);
    if (callbacks.refund_energy) {
        callbacks.refund_energy(
            kCommanderMasteryEnergyRefund);
    }
    result.retargeted = true;
    result.duration_extended = true;
    result.energy_refund =
        kCommanderMasteryEnergyRefund;
    result.replacement_target = replacement;
    return result;
}

void AssaultOrderSystem::clear(
    const AssaultOrderCallbacks& callbacks) {
    if (active_) {
        end(
            CommanderEffectEndReason::Cleared,
            callbacks);
    }
}

auto AssaultOrderSystem::state() const noexcept
    -> AssaultOrderStateView {
    const auto spec =
        assault_order_spec(rank_)
            .value_or(AssaultOrderSpec {});
    return {
        active_,
        activation_id_,
        rank_,
        mastered_,
        target_,
        destination_,
        saturating_float(remaining_seconds_),
        active_ ? spec.movement_speed_bonus : 0.0F,
        active_ ? spec.damage_bonus : 0.0F,
        mastery_retarget_used_,
    };
}

void AssaultOrderSystem::end(
    CommanderEffectEndReason reason,
    const AssaultOrderCallbacks& callbacks) {
    const auto ended_id = activation_id_;
    active_ = false;
    activation_id_ = 0U;
    target_.reset();
    remaining_seconds_ = 0.0;
    vulnerability_units_.fill(0U);
    vulnerability_unit_count_ = 0U;
    mastered_ = false;
    mastery_retarget_used_ = false;
    if (callbacks.order_ended) {
        callbacks.order_ended(
            ended_id,
            reason);
    }
}

auto FleetShooterSystem::next_shooter_id() noexcept
    -> CommanderActivationId {
    const auto result = next_shooter_id_;
    ++next_shooter_id_;
    if (next_shooter_id_ == 0U) {
        next_shooter_id_ = 1U;
    }
    return result;
}

auto FleetShooterSystem::summon(
    const FleetShooterSpawnRequest& request,
    const FleetShooterCallbacks& callbacks)
    -> CommanderActivationResult {
    const auto spec =
        fleet_shooter_spec(request.rank);
    if (!spec.has_value()) {
        return {
            false,
            CommanderEffectError::InvalidRank,
            0U,
        };
    }
    if (!finite_vec3(request.position) ||
        request.player_level == 0U ||
        request.player_level > 100U) {
        return {
            false,
            CommanderEffectError::InvalidInput,
            0U,
        };
    }
    if (active_) {
        return {
            false,
            CommanderEffectError::AlreadyActive,
            0U,
        };
    }
    if (request.active_combat_invocations >=
        kCommanderMaximumCombatUnits) {
        return {
            false,
            CommanderEffectError::LimitReached,
            0U,
        };
    }
    if (callbacks.validate_spawn &&
        !callbacks.validate_spawn(request)) {
        return {
            false,
            CommanderEffectError::CallbackRejected,
            0U,
        };
    }

    shooter_id_ = next_shooter_id();
    owner_id_ = request.owner_id;
    rank_ = request.rank;
    position_ = request.position;
    spec_ = *spec;
    age_seconds_ = 0.0;
    next_shot_seconds_ =
        static_cast<double>(
            spec_.attack_interval_seconds);
    maximum_health_ =
        rounded_scaled_stat(
            spec_.base_health,
            commander_summon_health_multiplier(
                request.player_level,
                request.wisdom));
    health_ = maximum_health_;
    damage_ =
        rounded_scaled_stat(
            spec_.base_damage,
            commander_summon_damage_multiplier(
                request.player_level,
                request.wisdom));
    shots_fired_ = 0U;
    active_ = true;
    mastered_ = request.mastered;
    first_salvo_available_ = request.mastered;
    return {
        true,
        CommanderEffectError::None,
        shooter_id_,
    };
}

auto FleetShooterSystem::acquire_target(
    const FleetShooterCallbacks& callbacks,
    FleetShooterTargetPriority& selected_priority) const
    -> std::optional<CommanderTarget> {
    if (!callbacks.acquire_target) {
        return std::nullopt;
    }

    for (const auto priority : {
             FleetShooterTargetPriority::MarkedByOrder,
             FleetShooterTargetPriority::AttackingPlayer,
             FleetShooterTargetPriority::NearestHostile,
         }) {
        const auto target =
            callbacks.acquire_target(
                FleetShooterAcquireRequest {
                    shooter_id_,
                    owner_id_,
                    position_,
                    spec_.range,
                    priority,
                });
        if (!target.has_value() ||
            !target_is_valid(*target) ||
            distance_squared(
                position_,
                target->position) >
                spec_.range * spec_.range) {
            continue;
        }
        selected_priority = priority;
        return target;
    }
    return std::nullopt;
}

auto FleetShooterSystem::update(
    float dt,
    const FleetShooterCallbacks& callbacks)
    -> FleetShooterUpdateResult {
    FleetShooterUpdateResult result {};
    if (!active_ ||
        !finite_positive(dt)) {
        return result;
    }

    const auto duration =
        static_cast<double>(
            spec_.duration_seconds);
    const auto end_time =
        std::min(
            duration,
            age_seconds_ +
                static_cast<double>(dt));

    while (
        next_shot_seconds_ <=
            end_time + kCommanderTimeEpsilon &&
        next_shot_seconds_ <=
            duration + kCommanderTimeEpsilon &&
        result.shot_count <
            result.shots.size()) {
        ++result.acquisition_attempt_count;
        auto selected_priority =
            FleetShooterTargetPriority::MarkedByOrder;
        const auto target =
            acquire_target(
                callbacks,
                selected_priority);
        if (target.has_value() &&
            callbacks.fire_shot) {
            const auto candidate_shot =
                shots_fired_ + 1U;
            const auto mastery_salvo =
                first_salvo_available_;
            const auto piercing =
                spec_.piercing_shot_period != 0U &&
                candidate_shot %
                        spec_.piercing_shot_period ==
                    0U;
            const auto requested_damage =
                damage_ *
                (mastery_salvo ? 2.0F : 1.0F);
            const auto raw_fire_result =
                callbacks.fire_shot(
                    FleetShooterFireRequest {
                        shooter_id_,
                        owner_id_,
                        *target,
                        position_,
                        candidate_shot,
                        spec_.range,
                        requested_damage,
                        piercing,
                        piercing
                            ? spec_.piercing_damage_multiplier
                            : 0.0F,
                        mastery_salvo,
                    });
            const auto fire_result =
                sanitized_fire_result(
                    raw_fire_result,
                    requested_damage,
                    spec_.piercing_damage_multiplier,
                    piercing);

            auto& outcome =
                result.shots[result.shot_count];
            outcome.shot_number =
                candidate_shot;
            outcome.selected_priority =
                selected_priority;
            outcome.target_id =
                target->entity_id;
            outcome.requested_damage =
                requested_damage;
            outcome.mastery_first_salvo =
                mastery_salvo;
            outcome.piercing_shot =
                piercing;
            outcome.result =
                fire_result;
            ++result.shot_count;

            if (fire_result.fired) {
                ++shots_fired_;
                if (mastery_salvo) {
                    first_salvo_available_ = false;
                }
            }
        }

        next_shot_seconds_ +=
            static_cast<double>(
                spec_.attack_interval_seconds);
    }

    age_seconds_ = end_time;
    if (age_seconds_ + kCommanderTimeEpsilon >=
        duration) {
        result.expired = true;
        end(
            CommanderEffectEndReason::Expired,
            callbacks);
    }
    return result;
}

auto FleetShooterSystem::apply_damage(
    float damage,
    const FleetShooterCallbacks& callbacks)
    -> FleetShooterDamageResult {
    FleetShooterDamageResult result {};
    if (!active_ ||
        !finite_positive(damage)) {
        return result;
    }

    result.handled = true;
    result.applied_damage =
        std::min(damage, health_);
    health_ -= result.applied_damage;
    result.remaining_health = health_;
    if (health_ <= 0.0F) {
        result.destroyed = true;
        end(
            CommanderEffectEndReason::Destroyed,
            callbacks);
        result.remaining_health = 0.0F;
    }
    return result;
}

void FleetShooterSystem::set_position(
    const glm::vec3& position) noexcept {
    if (active_ &&
        finite_vec3(position)) {
        position_ = position;
    }
}

void FleetShooterSystem::clear(
    const FleetShooterCallbacks& callbacks) {
    if (active_) {
        end(
            CommanderEffectEndReason::Cleared,
            callbacks);
    }
}

auto FleetShooterSystem::state() const noexcept
    -> FleetShooterStateView {
    return {
        active_,
        shooter_id_,
        owner_id_,
        rank_,
        position_,
        health_,
        maximum_health_,
        damage_,
        spec_.range,
        active_
            ? saturating_float(
                  static_cast<double>(
                      spec_.duration_seconds) -
                  age_seconds_)
            : 0.0F,
        shots_fired_,
        mastered_,
        first_salvo_available_,
    };
}

void FleetShooterSystem::end(
    CommanderEffectEndReason reason,
    const FleetShooterCallbacks& callbacks) {
    const auto ended_id = shooter_id_;
    active_ = false;
    shooter_id_ = 0U;
    age_seconds_ = 0.0;
    next_shot_seconds_ = 0.0;
    health_ = 0.0F;
    maximum_health_ = 0.0F;
    damage_ = 0.0F;
    shots_fired_ = 0U;
    mastered_ = false;
    first_salvo_available_ = false;
    if (callbacks.shooter_ended) {
        callbacks.shooter_ended(
            ended_id,
            reason);
    }
}

auto WarBannerSystem::next_banner_id() noexcept
    -> CommanderActivationId {
    const auto result = next_banner_id_;
    ++next_banner_id_;
    if (next_banner_id_ == 0U) {
        next_banner_id_ = 1U;
    }
    return result;
}

auto WarBannerSystem::place(
    const WarBannerPlacementRequest& request,
    const WarBannerCallbacks& callbacks)
    -> CommanderActivationResult {
    const auto spec =
        war_banner_spec(request.rank);
    if (!spec.has_value()) {
        return {
            false,
            CommanderEffectError::InvalidRank,
            0U,
        };
    }
    if (!finite_vec3(request.position) ||
        request.player_level == 0U ||
        request.player_level > 100U) {
        return {
            false,
            CommanderEffectError::InvalidInput,
            0U,
        };
    }
    if (callbacks.validate_placement &&
        !callbacks.validate_placement(request)) {
        return {
            false,
            CommanderEffectError::CallbackRejected,
            0U,
        };
    }

    const auto candidate_id =
        next_banner_id();
    if (active_) {
        end(
            CommanderEffectEndReason::Replaced,
            callbacks);
    }

    banner_id_ = candidate_id;
    owner_id_ = request.owner_id;
    rank_ = request.rank;
    position_ = request.position;
    spec_ = *spec;
    remaining_seconds_ =
        static_cast<double>(
            spec_.duration_seconds);
    healing_schedule_seconds_ = 0.0;
    healing_dispatched_seconds_ = 0.0;
    maximum_health_ =
        rounded_scaled_stat(
            spec_.base_health,
            commander_summon_health_multiplier(
                request.player_level,
                request.wisdom));
    health_ = maximum_health_;
    player_healing_applied_ = 0.0F;
    active_ = true;
    mastered_ = request.mastered;
    return {
        true,
        CommanderEffectError::None,
        banner_id_,
    };
}

auto WarBannerSystem::update(
    float dt,
    const glm::vec3& player_position,
    const WarBannerCallbacks& callbacks)
    -> WarBannerUpdateResult {
    WarBannerUpdateResult result {};
    if (!active_ ||
        !finite_positive(dt)) {
        return result;
    }

    const auto active_seconds =
        std::min(
            static_cast<double>(dt),
            remaining_seconds_);
    result.active_seconds =
        saturating_float(active_seconds);
    healing_schedule_seconds_ +=
        active_seconds;

    const auto player_inside =
        finite_vec3(player_position) &&
        distance_squared(
            player_position,
            position_) <=
            spec_.radius * spec_.radius;
    if (player_inside) {
        result.requested_player_healing =
            std::min(
                saturating_float(
                    active_seconds *
                    static_cast<double>(
                        spec_
                            .invocation_healing_per_second)),
                std::max(
                    0.0F,
                    spec_.player_healing_limit -
                        player_healing_applied_));
    }

    if (callbacks.apply_healing_pulse) {
        const auto pulse_result =
            callbacks.apply_healing_pulse(
                WarBannerPulseRequest {
                    banner_id_,
                    owner_id_,
                    position_,
                    spec_.radius,
                    result.active_seconds,
                    saturating_float(
                        active_seconds *
                        static_cast<double>(
                            spec_
                                .invocation_healing_per_second)),
                    result.requested_player_healing,
                });
        if (std::isfinite(
                pulse_result
                    .applied_player_healing)) {
            result.applied_player_healing =
                std::clamp(
                    pulse_result
                        .applied_player_healing,
                    0.0F,
                    result.requested_player_healing);
        }
        player_healing_applied_ +=
            result.applied_player_healing;
    }
    healing_dispatched_seconds_ +=
        active_seconds;

    remaining_seconds_ =
        std::max(
            0.0,
            remaining_seconds_ -
                active_seconds);
    if (remaining_seconds_ <=
        kCommanderTimeEpsilon) {
        result.expired = true;
        end(
            CommanderEffectEndReason::Expired,
            callbacks);
    }
    return result;
}

auto WarBannerSystem::sample_aura(
    const glm::vec3& position,
    CommanderAuraRecipient recipient) const noexcept
    -> WarBannerAuraSample {
    WarBannerAuraSample result {};
    if (!active_ ||
        !finite_vec3(position) ||
        distance_squared(
            position,
            position_) >
            spec_.radius * spec_.radius) {
        return result;
    }

    result.inside = true;
    if (recipient ==
            CommanderAuraRecipient::Invocation ||
        recipient ==
            CommanderAuraRecipient::Crew) {
        result.ally_damage_bonus =
            spec_.ally_damage_bonus;
    }
    if (recipient ==
        CommanderAuraRecipient::Invocation) {
        result.invocation_healing_per_second =
            spec_.invocation_healing_per_second;
    }
    if (recipient ==
            CommanderAuraRecipient::Player &&
        mastered_) {
        result.player_energy_regeneration_bonus =
            kWarBannerMasteryEnergyRegenerationBonus;
    }
    return result;
}

auto WarBannerSystem::apply_damage(
    float damage,
    const WarBannerCallbacks& callbacks)
    -> WarBannerDamageResult {
    WarBannerDamageResult result {};
    if (!active_ ||
        !finite_positive(damage)) {
        return result;
    }

    result.handled = true;
    result.applied_damage =
        std::min(damage, health_);
    health_ -= result.applied_damage;
    result.remaining_health = health_;
    if (health_ <= 0.0F) {
        result.destroyed = true;
        end(
            CommanderEffectEndReason::Destroyed,
            callbacks);
        result.remaining_health = 0.0F;
    }
    return result;
}

void WarBannerSystem::clear(
    const WarBannerCallbacks& callbacks) {
    if (active_) {
        end(
            CommanderEffectEndReason::Cleared,
            callbacks);
    }
}

auto WarBannerSystem::state() const noexcept
    -> WarBannerStateView {
    return {
        active_,
        banner_id_,
        owner_id_,
        rank_,
        position_,
        health_,
        maximum_health_,
        saturating_float(remaining_seconds_),
        active_ ? spec_.radius : 0.0F,
        player_healing_applied_,
        active_ ? spec_.player_healing_limit : 0.0F,
        mastered_,
    };
}

void WarBannerSystem::end(
    CommanderEffectEndReason reason,
    const WarBannerCallbacks& callbacks) {
    const auto ended_id = banner_id_;
    active_ = false;
    banner_id_ = 0U;
    remaining_seconds_ = 0.0;
    health_ = 0.0F;
    maximum_health_ = 0.0F;
    player_healing_applied_ = 0.0F;
    mastered_ = false;
    if (callbacks.banner_ended) {
        callbacks.banner_ended(
            ended_id,
            reason);
    }
}

auto RampartFormationSystem::next_activation_id() noexcept
    -> CommanderActivationId {
    const auto result = next_activation_id_;
    ++next_activation_id_;
    if (next_activation_id_ == 0U) {
        next_activation_id_ = 1U;
    }
    return result;
}

auto RampartFormationSystem::activate(
    const RampartFormationActivationRequest& request,
    const RampartFormationCallbacks& callbacks)
    -> CommanderActivationResult {
    const auto spec =
        rampart_formation_spec(request.rank);
    if (!spec.has_value()) {
        return {
            false,
            CommanderEffectError::InvalidRank,
            0U,
        };
    }
    if (!finite_vec3(request.anchor) ||
        !finite_vec3(request.forward) ||
        glm::dot(
            request.forward,
            request.forward) <=
            kMinimumDirectionLengthSquared) {
        return {
            false,
            CommanderEffectError::InvalidPosition,
            0U,
        };
    }
    if (request.unit_ids.size() >
        kCommanderMaximumCombatUnits) {
        return {
            false,
            CommanderEffectError::LimitReached,
            0U,
        };
    }

    std::array<
        CommanderUnitId,
        kCommanderMaximumCombatUnits>
        units {};
    for (std::size_t index = 0U;
         index < request.unit_ids.size();
         ++index) {
        const auto unit_id =
            request.unit_ids[index];
        if (unit_id == 0U ||
            std::find(
                units.begin(),
                units.begin() +
                    static_cast<std::ptrdiff_t>(
                        index),
                unit_id) !=
                units.begin() +
                    static_cast<std::ptrdiff_t>(
                        index)) {
            return {
                false,
                CommanderEffectError::InvalidInput,
                0U,
            };
        }
        units[index] = unit_id;
    }

    const auto normalized_forward =
        glm::normalize(request.forward);
    const auto candidate_id =
        next_activation_id();
    RampartFormationDispatch dispatch {};
    dispatch.activation_id =
        candidate_id;
    dispatch.anchor =
        request.anchor;
    dispatch.forward =
        normalized_forward;
    dispatch.unit_ids =
        units;
    dispatch.unit_count =
        request.unit_ids.size();
    dispatch.on_dynamic_ship =
        request.on_dynamic_ship;
    if (callbacks.dispatch_formation &&
        !callbacks.dispatch_formation(dispatch)) {
        return {
            false,
            CommanderEffectError::CallbackRejected,
            0U,
        };
    }

    if (active_) {
        end(
            CommanderEffectEndReason::Replaced,
            callbacks);
    }

    activation_id_ = candidate_id;
    rank_ = request.rank;
    anchor_ = request.anchor;
    forward_ = normalized_forward;
    spec_ = *spec;
    remaining_seconds_ =
        static_cast<double>(
            spec_.duration_seconds);
    unit_ids_ = units;
    mastery_blocks_consumed_.fill(false);
    unit_count_ =
        request.unit_ids.size();
    active_ = true;
    mastered_ = request.mastered;
    on_dynamic_ship_ =
        request.on_dynamic_ship;
    return {
        true,
        CommanderEffectError::None,
        activation_id_,
    };
}

auto RampartFormationSystem::update(
    float dt,
    const RampartFormationCallbacks& callbacks) -> bool {
    if (!active_ ||
        !finite_positive(dt)) {
        return false;
    }
    remaining_seconds_ =
        std::max(
            0.0,
            remaining_seconds_ -
                static_cast<double>(dt));
    if (remaining_seconds_ >
        kCommanderTimeEpsilon) {
        return false;
    }
    end(
        CommanderEffectEndReason::Expired,
        callbacks);
    return true;
}

auto RampartFormationSystem::unit_index(
    CommanderUnitId unit_id) const noexcept
    -> std::optional<std::size_t> {
    if (unit_id == 0U) {
        return std::nullopt;
    }
    for (std::size_t index = 0U;
         index < unit_count_;
         ++index) {
        if (unit_ids_[index] == unit_id) {
            return index;
        }
    }
    return std::nullopt;
}

auto RampartFormationSystem::sample_unit_defense(
    CommanderUnitId unit_id,
    bool attack_from_front) const noexcept
    -> RampartUnitDefenseSample {
    const auto member =
        active_ &&
        unit_index(unit_id).has_value();
    return {
        member,
        member,
        member && attack_from_front
            ? spec_
                  .frontal_unit_damage_reduction
            : 0.0F,
    };
}

auto RampartFormationSystem::sample_ally_defense(
    bool actually_behind_line) const noexcept
    -> RampartAllyDefenseSample {
    const auto protected_by_line =
        active_ &&
        actually_behind_line;
    return {
        protected_by_line,
        protected_by_line
            ? spec_
                  .protected_ally_damage_reduction
            : 0.0F,
    };
}

auto RampartFormationSystem::resolve_unit_ranged_attack(
    CommanderUnitId unit_id,
    bool unstoppable_boss_attack) noexcept
    -> RampartRangedAttackResult {
    RampartRangedAttackResult result {};
    if (!active_) {
        return result;
    }
    const auto index =
        unit_index(unit_id);
    if (!index.has_value()) {
        return result;
    }

    result.handled = true;
    if (!mastered_ ||
        unstoppable_boss_attack ||
        mastery_blocks_consumed_[*index]) {
        return result;
    }
    mastery_blocks_consumed_[*index] = true;
    result.completely_blocked = true;
    result.mastery_consumed = true;
    return result;
}

auto RampartFormationSystem::resolve_crossing_projectile(
    CommanderProjectileId projectile_id,
    bool actually_crosses_line) const noexcept
    -> RampartProjectileCrossingResult {
    RampartProjectileCrossingResult result {};
    if (!active_ ||
        rank_ != CommanderRank::RankThree ||
        projectile_id == 0U ||
        !actually_crosses_line) {
        return result;
    }

    result.handled = true;
    result.block_chance =
        spec_.crossing_projectile_block_chance;
    result.blocked =
        deterministic_sixty_percent(
            projectile_id ^
            splitmix64(activation_id_));
    return result;
}

void RampartFormationSystem::clear(
    const RampartFormationCallbacks& callbacks) {
    if (active_) {
        end(
            CommanderEffectEndReason::Cleared,
            callbacks);
    }
}

auto RampartFormationSystem::state() const noexcept
    -> RampartFormationStateView {
    return {
        active_,
        activation_id_,
        rank_,
        anchor_,
        forward_,
        saturating_float(remaining_seconds_),
        active_
            ? spec_
                  .frontal_unit_damage_reduction
            : 0.0F,
        active_
            ? spec_
                  .protected_ally_damage_reduction
            : 0.0F,
        active_ ? unit_count_ : 0U,
        mastered_,
        on_dynamic_ship_,
    };
}

void RampartFormationSystem::end(
    CommanderEffectEndReason reason,
    const RampartFormationCallbacks& callbacks) {
    const auto ended_id = activation_id_;
    active_ = false;
    activation_id_ = 0U;
    remaining_seconds_ = 0.0;
    unit_ids_.fill(0U);
    mastery_blocks_consumed_.fill(false);
    unit_count_ = 0U;
    mastered_ = false;
    on_dynamic_ship_ = false;
    if (callbacks.formation_ended) {
        callbacks.formation_ended(
            ended_id,
            reason);
    }
}

} // namespace valcraft
