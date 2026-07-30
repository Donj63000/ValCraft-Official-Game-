#include "gameplay/progression/SummonedUnitSystem.h"

#include <glm/geometric.hpp>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>

namespace valcraft {
namespace {

constexpr double kEventTimeAbsoluteEpsilon = 1.0e-7;
constexpr double kEventTimeRelativeEpsilon =
    static_cast<double>(
        std::numeric_limits<float>::epsilon()) *
    4.0;
constexpr float kAttackVisualSeconds = 0.24F;
constexpr float kTauntVisualSeconds = 0.38F;
constexpr float kScaledStatPrecision = 100.0F;
std::atomic<SummonedUnitId> gNextSummonedUnitId {1U};

[[nodiscard]] auto event_time_epsilon(
    double first,
    double second) noexcept -> double {
    // Je calibre la marge sur la précision des durées publiques en float :
    // un lot et plusieurs petits pas franchissent ainsi la même échéance.
    return std::max(
        kEventTimeAbsoluteEpsilon,
        std::max(
            std::abs(first),
            std::abs(second)) *
            kEventTimeRelativeEpsilon);
}

[[nodiscard]] auto finite_vec3(const glm::vec3& value) noexcept -> bool {
    return std::isfinite(value.x) &&
           std::isfinite(value.y) &&
           std::isfinite(value.z);
}

[[nodiscard]] auto non_negative_finite(float value) noexcept -> float {
    return std::isfinite(value)
               ? std::max(value, 0.0F)
               : 0.0F;
}

[[nodiscard]] auto sanitized_rank(
    SummonedUnitRank rank) noexcept -> SummonedUnitRank {
    switch (rank) {
    case SummonedUnitRank::RankTwo:
    case SummonedUnitRank::RankThree:
        return rank;
    case SummonedUnitRank::RankOne:
    default:
        return SummonedUnitRank::RankOne;
    }
}

[[nodiscard]] auto valid_rank(
    SummonedUnitRank rank) noexcept -> bool {
    return rank == SummonedUnitRank::RankOne ||
           rank == SummonedUnitRank::RankTwo ||
           rank == SummonedUnitRank::RankThree;
}

[[nodiscard]] auto valid_positive_bounded(
    float value,
    float maximum) noexcept -> bool {
    return std::isfinite(value) &&
           value > 0.0F &&
           value <= maximum;
}

[[nodiscard]] auto valid_stats(
    const SummonedUnitStats& stats,
    float maximum_stat) noexcept -> bool {
    if (!valid_positive_bounded(
            stats.duration_seconds,
            kSummonedUnitMaximumDurationSeconds) ||
        !valid_positive_bounded(
            stats.maximum_health,
            maximum_stat) ||
        !std::isfinite(stats.attack_damage) ||
        stats.attack_damage < 0.0F ||
        stats.attack_damage >
            maximum_stat ||
        !valid_positive_bounded(
            stats.attack_interval_seconds,
            kSummonedUnitMaximumDurationSeconds) ||
        stats.attack_interval_seconds <
            kSummonedUnitMinimumIntervalSeconds ||
        !std::isfinite(
            stats.mastery_damage_reduction) ||
        stats.mastery_damage_reduction < 0.0F ||
        stats.mastery_damage_reduction > 1.0F ||
        !valid_positive_bounded(
            stats.mastery_survival_health,
            stats.maximum_health) ||
        !valid_positive_bounded(
            stats.mastery_damage_reduction_seconds,
            kSummonedUnitMaximumDurationSeconds)) {
        return false;
    }
    const auto valid_taunt_interval =
        std::isfinite(
            stats.taunt_interval_seconds) &&
        stats.taunt_interval_seconds >= 0.0F &&
        stats.taunt_interval_seconds <=
            kSummonedUnitMaximumDurationSeconds;
    const auto valid_taunt_radius =
        std::isfinite(stats.taunt_radius) &&
        stats.taunt_radius >= 0.0F &&
        stats.taunt_radius <=
            kSummonedUnitMaximumRadius;
    if (!valid_taunt_interval ||
        !valid_taunt_radius ||
        (stats.has_light_taunt &&
         (stats.taunt_interval_seconds <
              kSummonedUnitMinimumIntervalSeconds ||
          stats.taunt_radius <= 0.0F))) {
        return false;
    }
    const auto valid_projectile_interval =
        std::isfinite(
            stats.projectile_block_interval_seconds) &&
        stats.projectile_block_interval_seconds >=
            0.0F &&
        stats.projectile_block_interval_seconds <=
            kSummonedUnitMaximumDurationSeconds;
    if (!valid_projectile_interval ||
        (stats.has_projectile_block &&
         stats.projectile_block_interval_seconds <
             kSummonedUnitMinimumIntervalSeconds)) {
        return false;
    }
    return true;
}

[[nodiscard]] auto sanitized_power_multiplier(
    float multiplier) noexcept -> float {
    if (!std::isfinite(multiplier) ||
        multiplier <= 0.0F) {
        return kSummonedUnitDefaultPowerMultiplier;
    }
    return std::clamp(
        multiplier,
        kSummonedUnitMinimumPowerMultiplier,
        kSummonedUnitMaximumPowerMultiplier);
}

[[nodiscard]] auto resolved_power_multiplier(
    const std::optional<float>& specialized_multiplier,
    float legacy_multiplier) noexcept -> float {
    // Je donne la priorité au facteur spécialisé tout en conservant le
    // facteur commun comme repli exact pour les anciens appels.
    return sanitized_power_multiplier(
        specialized_multiplier.value_or(
            legacy_multiplier));
}

[[nodiscard]] auto scaled_stat(
    float base_value,
    float multiplier) noexcept -> float {
    // J'arrondis ici au centième avec une règle indépendante du mode
    // d'arrondi du processeur afin de préserver le déterminisme.
    const auto scaled =
        std::clamp(
            static_cast<double>(base_value) *
                static_cast<double>(multiplier),
            0.0,
            static_cast<double>(
                kSummonedUnitMaximumResolvedStat));
    return static_cast<float>(
        std::floor(
            scaled * kScaledStatPrecision +
            0.5) /
        static_cast<double>(
            kScaledStatPrecision));
}

[[nodiscard]] auto unit_ratio(float value, float maximum) noexcept -> float {
    if (!std::isfinite(value) ||
        !std::isfinite(maximum) ||
        maximum <= 0.0F) {
        return 0.0F;
    }
    return std::clamp(value / maximum, 0.0F, 1.0F);
}

} // namespace

auto allocate_summoned_unit_id() noexcept
    -> SummonedUnitId {
    auto current =
        gNextSummonedUnitId.load(
            std::memory_order_relaxed);
    for (;;) {
        const auto next =
            current ==
                    std::numeric_limits<
                        SummonedUnitId>::max()
                ? SummonedUnitId {1U}
                : current + 1U;
        if (gNextSummonedUnitId
                .compare_exchange_weak(
                    current,
                    next,
                    std::memory_order_relaxed,
                    std::memory_order_relaxed)) {
            return current;
        }
    }
}

auto next_summoned_unit_id() noexcept
    -> SummonedUnitId {
    return gNextSummonedUnitId.load(
        std::memory_order_relaxed);
}

void reserve_next_summoned_unit_id(
    SummonedUnitId minimum_next_unit_id) noexcept {
    if (minimum_next_unit_id == 0U) {
        return;
    }
    auto current =
        gNextSummonedUnitId.load(
            std::memory_order_relaxed);
    while (current <
           minimum_next_unit_id) {
        if (gNextSummonedUnitId
                .compare_exchange_weak(
                    current,
                    minimum_next_unit_id,
                    std::memory_order_relaxed,
                    std::memory_order_relaxed)) {
            return;
        }
    }
}

void reserve_summoned_unit_id(
    SummonedUnitId unit_id) noexcept {
    if (unit_id == 0U ||
        unit_id ==
            std::numeric_limits<
                SummonedUnitId>::max()) {
        return;
    }
    reserve_next_summoned_unit_id(
        unit_id + 1U);
}

auto SummonedUnitSystem::summon(
    const SummonedUnitSpawnRequest& request) noexcept
    -> SummonedUnitSpawnResult {
    if (!finite_vec3(request.position)) {
        return {};
    }

    const auto resolved_rank =
        sanitized_rank(request.rank);
    const auto requested_stats =
        request.stats.value_or(
            summoned_unit_stats(
                resolved_rank));
    if (!valid_stats(
            requested_stats,
            kSummonedUnitMaximumBaseStat)) {
        return {};
    }

    const auto health_power_multiplier =
        resolved_power_multiplier(
            request.health_power_multiplier,
            request.power_multiplier);
    const auto attack_power_multiplier =
        resolved_power_multiplier(
            request.attack_power_multiplier,
            request.power_multiplier);
    auto resolved_stats = requested_stats;
    resolved_stats.maximum_health =
        scaled_stat(
            resolved_stats.maximum_health,
            health_power_multiplier);
    resolved_stats.attack_damage =
        scaled_stat(
            resolved_stats.attack_damage,
            attack_power_multiplier);
    if (resolved_stats.maximum_health <= 0.0F) {
        return {};
    }
    // Je borne le seuil de survie après la mise à l'échelle des PV afin que
    // tout état créé en mémoire puisse être sauvegardé puis rechargé tel quel.
    resolved_stats.mastery_survival_health =
        std::min(
            resolved_stats.mastery_survival_health,
            resolved_stats.maximum_health);
    if (!valid_stats(
            resolved_stats,
            kSummonedUnitMaximumResolvedStat)) {
        return {};
    }

    auto resolved_unit_id =
        SummonedUnitId {0U};
    if (request.unit_id.has_value()) {
        resolved_unit_id =
            *request.unit_id;
        if (resolved_unit_id == 0U) {
            return {};
        }
        reserve_summoned_unit_id(
            resolved_unit_id);
    } else {
        resolved_unit_id =
            allocate_summoned_unit_id();
        if (resolved_unit_id == 0U) {
            return {};
        }
    }

    unit_id_ = resolved_unit_id;
    owner_id_ = request.owner_id;
    cast_sequence_ =
        request.cast_sequence;
    position_ = request.position;
    rank_ = resolved_rank;
    stats_ = resolved_stats;
    age_seconds_ = 0.0;
    next_attack_seconds_ =
        static_cast<double>(
            stats_.attack_interval_seconds);
    next_taunt_seconds_ =
        static_cast<double>(
            stats_.taunt_interval_seconds);
    health_ = stats_.maximum_health;
    projectile_block_cooldown_ = 0.0F;
    mastery_damage_reduction_seconds_ = 0.0F;
    yaw_radians_ = 0.0F;
    animation_time_ = 0.0F;
    last_attack_event_seconds_ = -1.0;
    last_taunt_event_seconds_ = -1.0;
    active_ = true;
    mastered_ = request.mastered;
    death_refusal_used_ = false;
    pending_mastery_taunt_ = false;
    rebuild_render_snapshot();
    return {
        true,
        unit_id_,
    };
}

void SummonedUnitSystem::clear() noexcept {
    *this = SummonedUnitSystem {};
}

auto SummonedUnitSystem::update(
    float dt,
    const SummonedUnitCallbacks& callbacks)
    -> SummonedUnitUpdateResult {
    SummonedUnitUpdateResult result {};
    if (!active_) {
        return result;
    }

    if (pending_mastery_taunt_) {
        pending_mastery_taunt_ = false;
        emit_taunt(
            age_seconds_,
            true,
            callbacks,
            result);
    }

    const auto safe_dt = non_negative_finite(dt);
    const auto duration =
        static_cast<double>(stats_.duration_seconds);
    const auto previous_age = age_seconds_;
    const auto next_age =
        std::min(
            previous_age + static_cast<double>(safe_dt),
            duration);
    const auto active_dt =
        static_cast<float>(
            std::max(0.0, next_age - previous_age));

    animation_time_ += active_dt;
    projectile_block_cooldown_ =
        std::max(
            0.0F,
            projectile_block_cooldown_ - active_dt);
    mastery_damage_reduction_seconds_ =
        std::max(
            0.0F,
            mastery_damage_reduction_seconds_ - active_dt);

    while (active_ &&
           next_attack_seconds_ <=
               next_age +
                   event_time_epsilon(
                       next_attack_seconds_,
                       next_age)) {
        const auto event_time = next_attack_seconds_;
        emit_attack(
            event_time,
            callbacks,
            result);
        next_attack_seconds_ +=
            static_cast<double>(
                stats_.attack_interval_seconds);

        if (stats_.has_light_taunt &&
            next_taunt_seconds_ <=
                event_time +
                    event_time_epsilon(
                        next_taunt_seconds_,
                        event_time)) {
            emit_taunt(
                next_taunt_seconds_,
                false,
                callbacks,
                result);
            next_taunt_seconds_ +=
                static_cast<double>(
                    stats_.taunt_interval_seconds);
        }
    }

    while (active_ &&
           stats_.has_light_taunt &&
           next_taunt_seconds_ <=
               next_age +
                   event_time_epsilon(
                       next_taunt_seconds_,
                       next_age)) {
        emit_taunt(
            next_taunt_seconds_,
            false,
            callbacks,
            result);
        next_taunt_seconds_ +=
            static_cast<double>(
                stats_.taunt_interval_seconds);
    }

    age_seconds_ = next_age;
    if (active_ &&
        age_seconds_ >=
            duration -
                event_time_epsilon(
                    age_seconds_,
                    duration)) {
        result.expired = true;
        deactivate();
        return result;
    }

    rebuild_render_snapshot();
    return result;
}

auto SummonedUnitSystem::apply_damage(
    const SummonedUnitDamageRequest& request) noexcept
    -> SummonedUnitDamageResult {
    SummonedUnitDamageResult result {};
    result.requested_damage =
        non_negative_finite(request.damage);
    result.remaining_health =
        active_ ? health_ : 0.0F;
    if (!active_ ||
        result.requested_damage <= 0.0F) {
        return result;
    }

    result.handled = true;
    if (request.kind ==
            SummonedUnitDamageKind::Projectile &&
        stats_.has_projectile_block &&
        projectile_block_cooldown_ <= 0.0F) {
        projectile_block_cooldown_ =
            stats_
                .projectile_block_interval_seconds;
        result.blocked = true;
        result.remaining_health = health_;
        rebuild_render_snapshot();
        return result;
    }

    const auto reduction =
        mastery_damage_reduction_seconds_ > 0.0F
            ? stats_
                  .mastery_damage_reduction
            : 0.0F;
    const auto mitigated_damage =
        result.requested_damage *
        (1.0F - reduction);
    result.applied_damage =
        std::min(
            mitigated_damage,
            health_);
    health_ =
        std::max(
            0.0F,
            health_ - result.applied_damage);

    if (health_ <= 0.0F &&
        mastered_ &&
        !death_refusal_used_) {
        health_ =
            std::min(
                stats_.maximum_health,
                stats_.mastery_survival_health);
        death_refusal_used_ = true;
        pending_mastery_taunt_ = true;
        mastery_damage_reduction_seconds_ =
            stats_
                .mastery_damage_reduction_seconds;
        result.death_refused = true;
    } else if (health_ <= 0.0F) {
        result.killed = true;
        deactivate();
        result.remaining_health = 0.0F;
        return result;
    }

    result.remaining_health = health_;
    rebuild_render_snapshot();
    return result;
}

void SummonedUnitSystem::set_position(
    const glm::vec3& position) noexcept {
    if (!active_ ||
        !finite_vec3(position)) {
        return;
    }
    position_ = position;
    rebuild_render_snapshot();
}

auto SummonedUnitSystem::active() const noexcept -> bool {
    return active_;
}

auto SummonedUnitSystem::state() const noexcept
    -> SummonedUnitStateView {
    const auto remaining =
        active_
            ? std::max(
                  0.0,
                  static_cast<double>(
                      stats_.duration_seconds) -
                      age_seconds_)
            : 0.0;
    return {
        active_,
        unit_id_,
        owner_id_,
        position_,
        rank_,
        active_ ? health_ : 0.0F,
        stats_.maximum_health,
        static_cast<float>(remaining),
        active_ ? projectile_block_cooldown_ : 0.0F,
        active_
            ? mastery_damage_reduction_seconds_
            : 0.0F,
        mastered_,
        death_refusal_used_,
        cast_sequence_,
    };
}

auto SummonedUnitSystem::render_snapshots() const noexcept
    -> std::span<const SummonedUnitRenderSnapshot> {
    return {
        render_snapshots_.data(),
        render_snapshot_count_,
    };
}

auto SummonedUnitSystem::snapshot() const noexcept
    -> SummonedUnitSystemSnapshot {
    if (!active_) {
        return {};
    }
    return {
        true,
        unit_id_,
        owner_id_,
        cast_sequence_,
        position_,
        rank_,
        stats_,
        age_seconds_,
        next_attack_seconds_,
        next_taunt_seconds_,
        health_,
        projectile_block_cooldown_,
        mastery_damage_reduction_seconds_,
        yaw_radians_,
        animation_time_,
        last_attack_event_seconds_,
        last_taunt_event_seconds_,
        mastered_,
        death_refusal_used_,
        pending_mastery_taunt_,
    };
}

auto SummonedUnitSystem::load_state(
    const SummonedUnitSystemSnapshot& requested) noexcept
    -> SummonedUnitLoadResult {
    SummonedUnitLoadResult result {};
    if (!requested.active) {
        result.sanitized =
            requested !=
            SummonedUnitSystemSnapshot {};
        clear();
        return result;
    }

    const auto critical_state_valid =
        requested.unit_id != 0U &&
        finite_vec3(requested.position) &&
        valid_rank(requested.rank) &&
        valid_stats(
            requested.stats,
            kSummonedUnitMaximumResolvedStat) &&
        std::isfinite(requested.age_seconds) &&
        requested.age_seconds >= 0.0;
    if (!critical_state_valid) {
        clear();
        result.sanitized = true;
        return result;
    }
    const auto duration =
        static_cast<double>(
            requested.stats.duration_seconds);
    if (requested.age_seconds >=
        duration -
            event_time_epsilon(
                requested.age_seconds,
                duration)) {
        clear();
        result.sanitized = true;
        result.expired = true;
        return result;
    }
    if (!std::isfinite(requested.health) ||
        requested.health <= 0.0F) {
        clear();
        result.sanitized = true;
        return result;
    }

    SummonedUnitSystem staged {};
    staged.unit_id_ = requested.unit_id;
    staged.owner_id_ = requested.owner_id;
    staged.cast_sequence_ =
        requested.cast_sequence;
    staged.position_ = requested.position;
    staged.rank_ = requested.rank;
    staged.stats_ = requested.stats;
    staged.age_seconds_ =
        requested.age_seconds;
    staged.health_ =
        std::min(
            requested.health,
            requested.stats.maximum_health);
    result.sanitized =
        staged.health_ != requested.health;

    const auto sanitize_next_event =
        [&result](
            double requested_time,
            double age,
            float interval) noexcept {
        const auto maximum =
            age +
            static_cast<double>(interval);
        const auto epsilon =
            event_time_epsilon(
                requested_time,
                maximum);
        if (!std::isfinite(requested_time) ||
            requested_time <=
                age + epsilon ||
            requested_time >
                maximum + epsilon) {
            result.sanitized = true;
            return age +
                   static_cast<double>(
                       interval);
        }
        return requested_time;
    };
    staged.next_attack_seconds_ =
        sanitize_next_event(
            requested.next_attack_seconds,
            requested.age_seconds,
            requested.stats
                .attack_interval_seconds);
    if (requested.stats.has_light_taunt) {
        staged.next_taunt_seconds_ =
            sanitize_next_event(
                requested.next_taunt_seconds,
                requested.age_seconds,
                requested.stats
                    .taunt_interval_seconds);
    } else if (
        std::isfinite(
            requested.next_taunt_seconds) &&
        requested.next_taunt_seconds >= 0.0) {
        staged.next_taunt_seconds_ =
            requested.next_taunt_seconds;
    } else {
        staged.next_taunt_seconds_ =
            static_cast<double>(
                requested.stats
                    .taunt_interval_seconds);
        result.sanitized = true;
    }

    const auto sanitize_timer =
        [&result](
            float value,
            float maximum) noexcept {
        if (!std::isfinite(value) ||
            value < 0.0F) {
            result.sanitized = true;
            return 0.0F;
        }
        if (value > maximum) {
            result.sanitized = true;
            return maximum;
        }
        return value;
    };
    staged.projectile_block_cooldown_ =
        requested.stats.has_projectile_block
            ? sanitize_timer(
                  requested
                      .projectile_block_cooldown,
                  requested.stats
                      .projectile_block_interval_seconds)
            : 0.0F;
    if (!requested.stats.has_projectile_block &&
        requested.projectile_block_cooldown !=
            0.0F) {
        result.sanitized = true;
    }
    staged.mastery_damage_reduction_seconds_ =
        requested.mastered
            ? sanitize_timer(
                  requested
                      .mastery_damage_reduction_seconds,
                  requested.stats
                      .mastery_damage_reduction_seconds)
            : 0.0F;
    if (!requested.mastered &&
        requested
                .mastery_damage_reduction_seconds !=
            0.0F) {
        result.sanitized = true;
    }

    staged.yaw_radians_ =
        std::isfinite(requested.yaw_radians)
            ? requested.yaw_radians
            : 0.0F;
    if (staged.yaw_radians_ !=
        requested.yaw_radians) {
        result.sanitized = true;
    }
    const auto maximum_animation_time =
        requested.age_seconds +
        event_time_epsilon(
            requested.animation_time,
            requested.age_seconds);
    staged.animation_time_ =
        std::isfinite(requested.animation_time) &&
                requested.animation_time >= 0.0F &&
                static_cast<double>(
                    requested.animation_time) <=
                    maximum_animation_time
            ? requested.animation_time
            : static_cast<float>(
                  requested.age_seconds);
    if (staged.animation_time_ !=
        requested.animation_time) {
        result.sanitized = true;
    }

    const auto sanitize_last_event =
        [&result](
            double value,
            double age) noexcept {
        if (value == -1.0) {
            return value;
        }
        if (!std::isfinite(value) ||
            value < 0.0 ||
            value >
                age +
                    event_time_epsilon(
                        value,
                        age)) {
            result.sanitized = true;
            return -1.0;
        }
        return value;
    };
    staged.last_attack_event_seconds_ =
        sanitize_last_event(
            requested
                .last_attack_event_seconds,
            requested.age_seconds);
    staged.last_taunt_event_seconds_ =
        sanitize_last_event(
            requested
                .last_taunt_event_seconds,
            requested.age_seconds);
    staged.mastered_ = requested.mastered;
    staged.death_refusal_used_ =
        requested.mastered &&
        requested.death_refusal_used;
    staged.pending_mastery_taunt_ =
        requested.mastered &&
        requested.death_refusal_used &&
        requested.pending_mastery_taunt;
    if (staged.death_refusal_used_ !=
            requested.death_refusal_used ||
        staged.pending_mastery_taunt_ !=
            requested.pending_mastery_taunt) {
        result.sanitized = true;
    }
    staged.active_ = true;
    staged.rebuild_render_snapshot();

    reserve_summoned_unit_id(
        staged.unit_id_);
    *this = staged;
    result.restored = true;
    return result;
}

void SummonedUnitSystem::emit_attack(
    double event_time,
    const SummonedUnitCallbacks& callbacks,
    SummonedUnitUpdateResult& result) {
    ++result.attack_window_count;
    last_attack_event_seconds_ =
        event_time;
    if (!callbacks.acquire_target ||
        !callbacks.strike_target) {
        return;
    }

    const auto target =
        callbacks.acquire_target({
            unit_id_,
            owner_id_,
            position_,
            rank_,
            static_cast<float>(event_time),
        });
    if (!target.has_value() ||
        target->target_id == 0U ||
        !finite_vec3(target->position)) {
        return;
    }

    const auto direction =
        target->position - position_;
    const auto horizontal_length_squared =
        direction.x * direction.x +
        direction.z * direction.z;
    if (std::isfinite(horizontal_length_squared) &&
        horizontal_length_squared >
            std::numeric_limits<float>::epsilon()) {
        yaw_radians_ =
            std::atan2(direction.x, direction.z);
    }

    const auto strike =
        callbacks.strike_target({
            unit_id_,
            owner_id_,
            *target,
            position_,
            stats_.attack_damage,
            SummonedUnitDamageSource::PlayerSummon,
        });
    if (result.attack_count >=
        result.attacks.size()) {
        return;
    }

    const auto applied_damage =
        std::clamp(
            non_negative_finite(
                strike.applied_damage),
            0.0F,
            stats_.attack_damage);
    auto& outcome =
        result.attacks[result.attack_count++];
    outcome.unit_id = unit_id_;
    outcome.owner_id = owner_id_;
    outcome.target_id = target->target_id;
    outcome.source =
        SummonedUnitDamageSource::PlayerSummon;
    outcome.hit = strike.hit;
    outcome.killed =
        strike.hit &&
        strike.killed;
    outcome.requested_damage =
        stats_.attack_damage;
    outcome.applied_damage =
        strike.hit ? applied_damage : 0.0F;
    if (outcome.killed) {
        ++result.kill_count;
    }
}

void SummonedUnitSystem::emit_taunt(
    double event_time,
    bool mastery_triggered,
    const SummonedUnitCallbacks& callbacks,
    SummonedUnitUpdateResult& result) {
    ++result.taunt_count;
    last_taunt_event_seconds_ =
        event_time;
    if (callbacks.taunt) {
        callbacks.taunt({
            unit_id_,
            owner_id_,
            position_,
            stats_.taunt_radius,
            mastery_triggered,
            stats_.taunt_interval_seconds,
        });
    }
}

void SummonedUnitSystem::rebuild_render_snapshot() noexcept {
    if (!active_) {
        render_snapshot_count_ = 0U;
        return;
    }

    auto& snapshot = render_snapshots_.front();
    snapshot.unit_id = unit_id_;
    snapshot.owner_id = owner_id_;
    snapshot.position = position_;
    snapshot.yaw_radians = yaw_radians_;
    snapshot.animation_time = animation_time_;
    snapshot.health_ratio =
        unit_ratio(
            health_,
            stats_.maximum_health);
    snapshot.remaining_life_ratio =
        unit_ratio(
            static_cast<float>(
                std::max(
                    0.0,
                    static_cast<double>(
                        stats_.duration_seconds) -
                        age_seconds_)),
            stats_.duration_seconds);
    const auto visual_amount =
        [this](
            double event_time,
            float duration) noexcept {
        if (event_time < 0.0) {
            return 0.0F;
        }
        const auto elapsed =
            static_cast<float>(
                std::max(
                    0.0,
                    age_seconds_ -
                        event_time));
        return unit_ratio(
            duration - elapsed,
            duration);
    };
    snapshot.attack_amount =
        visual_amount(
            last_attack_event_seconds_,
            kAttackVisualSeconds);
    snapshot.taunt_amount =
        visual_amount(
            last_taunt_event_seconds_,
            kTauntVisualSeconds);
    snapshot.rank = rank_;
    snapshot.projectile_block_ready =
        stats_.has_projectile_block &&
        projectile_block_cooldown_ <= 0.0F;
    snapshot.mastery_damage_reduction_active =
        mastery_damage_reduction_seconds_ > 0.0F;
    render_snapshot_count_ = 1U;
}

void SummonedUnitSystem::deactivate() noexcept {
    active_ = false;
    health_ = 0.0F;
    projectile_block_cooldown_ = 0.0F;
    mastery_damage_reduction_seconds_ = 0.0F;
    pending_mastery_taunt_ = false;
    render_snapshot_count_ = 0U;
}

} // namespace valcraft
