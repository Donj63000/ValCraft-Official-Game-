#include "creatures/legendary/LegendaryEnemySystem.h"

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace valcraft {

namespace {

[[nodiscard]] auto valid_archetype(
    LegendaryEnemyArchetype archetype) noexcept -> bool {
    switch (archetype) {
    case LegendaryEnemyArchetype::CorruptedBrute:
    case LegendaryEnemyArchetype::SwiftHunter:
    case LegendaryEnemyArchetype::ArmoredGuard:
    case LegendaryEnemyArchetype::AstralCreature:
    case LegendaryEnemyArchetype::ForgeGuardian:
    case LegendaryEnemyArchetype::ArenaMinion:
    case LegendaryEnemyArchetype::AstralBoss:
        return true;
    }
    return false;
}

[[nodiscard]] auto finite_vec3(const glm::vec3& value) noexcept -> bool {
    return std::isfinite(value.x) &&
           std::isfinite(value.y) &&
           std::isfinite(value.z);
}

[[nodiscard]] auto mixed_seed(std::uint32_t value) noexcept
    -> std::uint32_t {
    value ^= value >> 16U;
    value *= 0x7FEB352DU;
    value ^= value >> 15U;
    value *= 0x846CA68BU;
    value ^= value >> 16U;
    return value;
}

[[nodiscard]] auto deterministic_health(
    const LegendaryEnemyProfile& profile,
    std::uint32_t seed) noexcept -> float {
    const auto unit =
        static_cast<float>(mixed_seed(seed) & 0x00FFFFFFU) /
        static_cast<float>(0x00FFFFFFU);
    return profile.minimum_health +
           (profile.maximum_health - profile.minimum_health) * unit;
}

[[nodiscard]] auto duration_ticks(float seconds) noexcept
    -> std::uint32_t {
    return static_cast<std::uint32_t>(
        std::max(1.0, std::ceil(static_cast<double>(seconds) * 60.0)));
}

[[nodiscard]] auto horizontal_direction(
    const glm::vec3& from,
    const glm::vec3& to) noexcept -> glm::vec3 {
    const auto delta =
        glm::vec3 {to.x - from.x, 0.0F, to.z - from.z};
    const auto length = glm::length(delta);
    if (!std::isfinite(length) || length <= 0.0001F) {
        return glm::vec3 {0.0F, 0.0F, 1.0F};
    }
    return delta / length;
}

[[nodiscard]] auto horizontal_distance(
    const glm::vec3& lhs,
    const glm::vec3& rhs) noexcept -> float {
    const auto delta =
        glm::vec3 {rhs.x - lhs.x, 0.0F, rhs.z - lhs.z};
    return glm::length(delta);
}

[[nodiscard]] auto astral_multiplier(
    std::uint8_t awakening_level,
    AstralHitInteraction& interaction) noexcept -> float {
    if (awakening_level == 0U) {
        interaction = AstralHitInteraction::Deflected;
        return 0.08F;
    }
    if (awakening_level == 1U) {
        interaction = AstralHitInteraction::PartiallyEffective;
        return 0.25F;
    }
    interaction = AstralHitInteraction::FullyEffective;
    return awakening_level >= 3U ? 1.10F : 1.0F;
}

} // namespace

auto legendary_enemy_profile(
    LegendaryEnemyArchetype archetype) noexcept
    -> LegendaryEnemyProfile {
    switch (archetype) {
    case LegendaryEnemyArchetype::SwiftHunter:
        return {
            archetype,
            ThreatRank::Three,
            EntityWeight::Light,
            deterministic_experience_reward(
                ThreatRank::Three,
                EntityWeight::Light,
                Faction::Hostile),
            25.0F,
            45.0F,
            0.0F,
            42.0F,
            4.8F,
            2.0F,
            9.0F,
            0.22F,
            0.35F,
            0.0F,
            0.0F,
            0.85F,
            false,
            false,
            false,
        };
    case LegendaryEnemyArchetype::ArmoredGuard:
        return {
            archetype,
            ThreatRank::Five,
            EntityWeight::Heavy,
            deterministic_experience_reward(
                ThreatRank::Five,
                EntityWeight::Heavy,
                Faction::Hostile),
            100.0F,
            160.0F,
            72.0F,
            95.0F,
            1.7F,
            2.2F,
            18.0F,
            0.72F,
            0.65F,
            0.68F,
            0.12F,
            0.72F,
            false,
            false,
            false,
        };
    case LegendaryEnemyArchetype::AstralCreature:
        return {
            archetype,
            ThreatRank::Five,
            EntityWeight::Normal,
            deterministic_experience_reward(
                ThreatRank::Five,
                EntityWeight::Normal,
                Faction::Hostile),
            75.0F,
            125.0F,
            0.0F,
            80.0F,
            3.1F,
            2.8F,
            16.0F,
            0.48F,
            0.42F,
            0.0F,
            0.0F,
            0.62F,
            false,
            true,
            false,
        };
    case LegendaryEnemyArchetype::AstralBoss:
        return {
            archetype,
            ThreatRank::Six,
            EntityWeight::Boss,
            deterministic_experience_reward(
                ThreatRank::Six,
                EntityWeight::Boss,
                Faction::Hostile,
                false,
                1'400U),
            520.0F,
            700.0F,
            0.0F,
            210.0F,
            1.85F,
            3.4F,
            30.0F,
            0.85F,
            0.70F,
            0.0F,
            0.0F,
            0.42F,
            false,
            true,
            false,
        };
    case LegendaryEnemyArchetype::ForgeGuardian:
        return {
            archetype,
            ThreatRank::Six,
            EntityWeight::Boss,
            deterministic_experience_reward(
                ThreatRank::Six,
                EntityWeight::Boss,
                Faction::Hostile,
                false,
                900U),
            320.0F,
            320.0F,
            110.0F,
            165.0F,
            1.55F,
            3.0F,
            28.0F,
            0.95F,
            0.82F,
            0.55F,
            0.20F,
            0.48F,
            false,
            false,
            false,
        };
    case LegendaryEnemyArchetype::ArenaMinion:
        return {
            archetype,
            ThreatRank::Zero,
            EntityWeight::Light,
            ExperienceReward {0U},
            8.0F,
            12.0F,
            0.0F,
            12.0F,
            3.6F,
            1.35F,
            4.0F,
            0.28F,
            0.25F,
            0.0F,
            0.0F,
            2.0F,
            false,
            false,
            true,
        };
    case LegendaryEnemyArchetype::CorruptedBrute:
    default:
        return {
            LegendaryEnemyArchetype::CorruptedBrute,
            ThreatRank::Four,
            EntityWeight::Heavy,
            deterministic_experience_reward(
                ThreatRank::Four,
                EntityWeight::Heavy,
                Faction::Hostile),
            60.0F,
            100.0F,
            24.0F,
            72.0F,
            1.9F,
            2.7F,
            22.0F,
            0.88F,
            0.72F,
            0.22F,
            0.08F,
            1.30F,
            true,
            false,
            false,
        };
    }
}

auto LegendaryEnemySystem::find(
    LegendaryEnemyId id) noexcept -> Entry* {
    for (auto& entry : entries_) {
        if (entry.active && entry.id == id) {
            return &entry;
        }
    }
    return nullptr;
}

auto LegendaryEnemySystem::find(
    LegendaryEnemyId id) const noexcept -> const Entry* {
    for (const auto& entry : entries_) {
        if (entry.active && entry.id == id) {
            return &entry;
        }
    }
    return nullptr;
}

void LegendaryEnemySystem::push_event(
    const Entry& entry,
    LegendaryEnemyEventKind kind,
    float amount,
    ExperienceReward reward,
    bool targets_player_only) noexcept {
    const auto event = LegendaryEnemyEvent {
        kind,
        entry.id,
        entry.profile.archetype,
        simulation_tick_,
        entry.position,
        amount,
        reward,
        targets_player_only,
    };
    if (event_count_ < events_.size()) {
        events_[event_count_] = event;
        ++event_count_;
        return;
    }

    // Je garde les événements les plus récents sans jamais laisser la file
    // allouer de mémoire au milieu d'un combat.
    std::move(events_.begin() + 1, events_.end(), events_.begin());
    events_.back() = event;
    ++dropped_event_count_;
}

auto LegendaryEnemySystem::spawn(
    const LegendaryEnemySpawnRequest& request) noexcept
    -> LegendaryEnemySpawnResult {
    LegendaryEnemySpawnResult result {};
    if (!valid_archetype(request.archetype)) {
        result.error = LegendaryEnemySpawnError::InvalidArchetype;
        return result;
    }
    if (!finite_vec3(request.position)) {
        result.error = LegendaryEnemySpawnError::InvalidPosition;
        return result;
    }
    if (!std::isfinite(request.facing_yaw_radians)) {
        result.error = LegendaryEnemySpawnError::InvalidFacing;
        return result;
    }
    if (size_ >= entries_.size() ||
        next_id_ == std::numeric_limits<LegendaryEnemyId>::max()) {
        result.error = LegendaryEnemySpawnError::CapacityReached;
        return result;
    }

    for (auto& entry : entries_) {
        if (entry.active) {
            continue;
        }
        const auto profile =
            legendary_enemy_profile(request.archetype);
        const auto maximum_health =
            deterministic_health(
                profile,
                request.deterministic_seed);
        entry = {};
        entry.active = true;
        entry.id = next_id_;
        ++next_id_;
        entry.profile = profile;
        entry.deterministic_seed = request.deterministic_seed;
        entry.position = request.position;
        entry.facing_yaw_radians =
            request.facing_yaw_radians;
        entry.health = maximum_health;
        entry.maximum_health = maximum_health;
        entry.armor = profile.maximum_armor;
        entry.behavior = LegendaryEnemyBehavior::Idle;
        ++size_;
        push_event(entry, LegendaryEnemyEventKind::Spawned);
        result.spawned = true;
        result.id = entry.id;
        result.maximum_health = maximum_health;
        return result;
    }

    result.error = LegendaryEnemySpawnError::CapacityReached;
    return result;
}

void LegendaryEnemySystem::update_tick(
    Entry& entry,
    const LegendaryEnemyWorldInput& input) noexcept {
    if (entry.health <= 0.0F) {
        entry.behavior = LegendaryEnemyBehavior::Dead;
        return;
    }

    if (entry.dodge_cooldown_ticks > 0U) {
        --entry.dodge_cooldown_ticks;
    }
    if (entry.stagger_recovery_delay_ticks > 0U) {
        --entry.stagger_recovery_delay_ticks;
    } else if (entry.stagger > 0.0F &&
               entry.stagger_ticks_remaining == 0U) {
        entry.stagger =
            std::max(0.0F, entry.stagger - 7.0F / 60.0F);
    }

    if (entry.stagger_ticks_remaining > 0U) {
        --entry.stagger_ticks_remaining;
        entry.behavior = LegendaryEnemyBehavior::Staggered;
        if (entry.stagger_ticks_remaining == 0U) {
            entry.stagger = 0.0F;
            entry.behavior = LegendaryEnemyBehavior::Approach;
        }
        return;
    }

    if (!input.target_alive) {
        entry.behavior = LegendaryEnemyBehavior::Idle;
        return;
    }

    const auto distance =
        horizontal_distance(entry.position, input.target_position);
    const auto direction =
        horizontal_direction(entry.position, input.target_position);
    entry.facing_yaw_radians =
        std::atan2(direction.x, direction.z);

    if (entry.profile.archetype ==
            LegendaryEnemyArchetype::SwiftHunter &&
        input.incoming_heavy_attack &&
        entry.dodge_cooldown_ticks == 0U &&
        distance <= 5.0F &&
        entry.behavior != LegendaryEnemyBehavior::Telegraph &&
        entry.behavior != LegendaryEnemyBehavior::Attack) {
        const auto sign =
            ((entry.deterministic_seed + entry.attack_sequence) & 1U) == 0U
                ? 1.0F
                : -1.0F;
        const auto side =
            glm::vec3 {direction.z * sign, 0.0F, -direction.x * sign};
        entry.position += side * 1.65F;
        entry.behavior = LegendaryEnemyBehavior::Strafe;
        entry.dodge_cooldown_ticks = 150U;
        ++entry.attack_sequence;
        push_event(entry, LegendaryEnemyEventKind::Dodged);
        return;
    }

    if (entry.behavior == LegendaryEnemyBehavior::Telegraph) {
        if (entry.behavior_ticks_remaining > 0U) {
            --entry.behavior_ticks_remaining;
        }
        if (entry.behavior_ticks_remaining == 0U) {
            entry.behavior = LegendaryEnemyBehavior::Attack;
            push_event(
                entry,
                LegendaryEnemyEventKind::AttackActive,
                entry.profile.attack_damage,
                {},
                true);
            entry.behavior = LegendaryEnemyBehavior::Recover;
            entry.behavior_ticks_remaining =
                duration_ticks(entry.profile.attack_recovery_seconds);
        }
        return;
    }
    if (entry.behavior == LegendaryEnemyBehavior::Recover) {
        if (entry.behavior_ticks_remaining > 0U) {
            --entry.behavior_ticks_remaining;
        }
        if (entry.behavior_ticks_remaining == 0U) {
            entry.behavior = LegendaryEnemyBehavior::Approach;
            push_event(entry, LegendaryEnemyEventKind::AttackFinished);
        }
        return;
    }

    if (distance > entry.profile.attack_range) {
        entry.behavior = LegendaryEnemyBehavior::Approach;
        const auto speed =
            entry.profile.movement_speed *
            (input.target_recovery_exposed ? 1.12F : 1.0F);
        entry.position += direction * (speed / 60.0F);
        if (!entry.alerted) {
            entry.alerted = true;
            push_event(entry, LegendaryEnemyEventKind::Alerted);
        }
        return;
    }

    entry.behavior = LegendaryEnemyBehavior::Telegraph;
    entry.behavior_ticks_remaining =
        duration_ticks(entry.profile.attack_windup_seconds);
    ++entry.attack_sequence;
    push_event(
        entry,
        LegendaryEnemyEventKind::AttackTelegraphed,
        entry.profile.attack_windup_seconds,
        {},
        true);
}

auto LegendaryEnemySystem::update(
    float dt,
    const LegendaryEnemyWorldInput& input) noexcept
    -> LegendaryEnemyUpdateResult {
    LegendaryEnemyUpdateResult result {};
    if (!std::isfinite(dt) ||
        dt < 0.0F ||
        dt > 10.0F ||
        !finite_vec3(input.target_position)) {
        return result;
    }

    result.accepted = true;
    const auto dropped_before = dropped_event_count_;
    tick_accumulator_ += static_cast<double>(dt) * 60.0;
    const auto nearest = std::round(tick_accumulator_);
    if (std::abs(tick_accumulator_ - nearest) <= 1.0e-5) {
        tick_accumulator_ = nearest;
    }
    const auto ticks =
        static_cast<std::uint64_t>(
            std::floor(tick_accumulator_));
    tick_accumulator_ -= static_cast<double>(ticks);
    result.advanced_ticks = ticks;

    for (std::uint64_t tick = 0U; tick < ticks; ++tick) {
        ++simulation_tick_;
        for (auto& entry : entries_) {
            if (entry.active) {
                update_tick(entry, input);
            }
        }
    }
    result.dropped_event_count =
        dropped_event_count_ - dropped_before;
    return result;
}

auto LegendaryEnemySystem::apply_hit(
    LegendaryEnemyId id,
    const LegendaryEnemyHitRequest& request) noexcept
    -> LegendaryEnemyHitResult {
    LegendaryEnemyHitResult result {};
    result.requested_damage = request.physical_damage;
    auto* entry = find(id);
    if (entry == nullptr || entry->health <= 0.0F) {
        result.error = LegendaryEnemyHitError::UnknownEnemy;
        return result;
    }
    if (!std::isfinite(request.physical_damage) ||
        request.physical_damage < 0.0F ||
        request.physical_damage > 1'000'000.0F) {
        result.error = LegendaryEnemyHitError::InvalidDamage;
        return result;
    }
    if (!std::isfinite(request.stagger_power) ||
        request.stagger_power < 0.0F ||
        request.stagger_power > 1'000'000.0F) {
        result.error = LegendaryEnemyHitError::InvalidStagger;
        return result;
    }

    result.accepted = true;
    auto damage = request.physical_damage;
    if (entry->profile.astral) {
        damage *= astral_multiplier(
            request.weapon_awakening_level,
            result.astral_interaction);
        if (result.astral_interaction ==
            AstralHitInteraction::Deflected) {
            push_event(
                *entry,
                LegendaryEnemyEventKind::AstralDeflected,
                damage);
        }
    }

    if (entry->armor > 0.0F) {
        const auto reduction =
            request.frontal
                ? entry->profile.frontal_armor_reduction
                : entry->profile.rear_armor_reduction;
        const auto weakness_multiplier =
            entry->behavior == LegendaryEnemyBehavior::Staggered
                ? 0.30F
                : 1.0F;
        damage *=
            1.0F - std::clamp(
                reduction * weakness_multiplier,
                0.0F,
                0.95F);
        const auto requested_armor_damage =
            request.physical_damage *
            (request.frontal ? 1.25F : 0.75F);
        result.applied_armor_damage =
            std::min(entry->armor, requested_armor_damage);
        entry->armor -= result.applied_armor_damage;
        if (entry->armor <= 0.0F &&
            result.applied_armor_damage > 0.0F) {
            entry->armor = 0.0F;
            result.armor_broken_now = true;
            push_event(*entry, LegendaryEnemyEventKind::ArmorBroken);
        }
    }

    result.applied_health_damage =
        std::min(entry->health, std::max(0.0F, damage));
    entry->health -= result.applied_health_damage;
    const auto requested_stagger =
        request.stagger_power *
        entry->profile.stagger_susceptibility;
    const auto stagger_available =
        std::max(0.0F, entry->profile.maximum_stagger - entry->stagger);
    result.applied_stagger =
        std::min(stagger_available, requested_stagger);
    entry->stagger += result.applied_stagger;
    if (result.applied_stagger > 0.0F) {
        entry->stagger_recovery_delay_ticks = 90U;
    }

    push_event(
        *entry,
        LegendaryEnemyEventKind::Damaged,
        result.applied_health_damage);
    if (entry->health > 0.0F &&
        entry->stagger >= entry->profile.maximum_stagger &&
        entry->stagger_ticks_remaining == 0U) {
        entry->stagger =
            entry->profile.maximum_stagger;
        entry->stagger_ticks_remaining =
            entry->profile.weight == EntityWeight::Boss ? 54U : 75U;
        entry->behavior = LegendaryEnemyBehavior::Staggered;
        result.staggered_now = true;
        push_event(*entry, LegendaryEnemyEventKind::Staggered);
    }

    if (entry->health <= 0.0F) {
        entry->health = 0.0F;
        entry->behavior = LegendaryEnemyBehavior::Dead;
        result.killed_now = true;
        push_event(*entry, LegendaryEnemyEventKind::Died);
        if (!entry->profile.temporary_reward_suppressed) {
            result.reward = entry->profile.reward;
            push_event(
                *entry,
                LegendaryEnemyEventKind::RewardAvailable,
                0.0F,
                result.reward);
        }
    }

    result.remaining_health = entry->health;
    result.remaining_armor = entry->armor;
    return result;
}

auto LegendaryEnemySystem::apply_knockback(
    LegendaryEnemyId id,
    const glm::vec3& direction,
    float distance) noexcept -> bool {
    auto* entry = find(id);
    if (entry == nullptr ||
        entry->health <= 0.0F ||
        !finite_vec3(direction) ||
        !std::isfinite(distance) ||
        distance <= 0.0F ||
        entry->profile.weight == EntityWeight::Boss) {
        return false;
    }
    const auto horizontal =
        glm::vec3 {direction.x, 0.0F, direction.z};
    const auto length_squared =
        glm::dot(horizontal, horizontal);
    if (!std::isfinite(length_squared) ||
        length_squared <= 1.0e-6F) {
        return false;
    }
    const auto bounded_distance =
        std::clamp(distance, 0.0F, 4.0F);
    entry->position +=
        horizontal /
        std::sqrt(length_squared) *
        bounded_distance;
    entry->alerted = true;
    return true;
}

auto LegendaryEnemySystem::render_snapshot(
    LegendaryEnemyId id) const noexcept
    -> std::optional<LegendaryEnemyRenderSnapshot> {
    const auto* entry = find(id);
    if (entry == nullptr) {
        return std::nullopt;
    }
    const auto armor_ratio =
        entry->profile.maximum_armor > 0.0F
            ? entry->armor / entry->profile.maximum_armor
            : 0.0F;
    return LegendaryEnemyRenderSnapshot {
        entry->id,
        entry->profile.archetype,
        entry->behavior,
        entry->position,
        entry->facing_yaw_radians,
        entry->maximum_health > 0.0F
            ? entry->health / entry->maximum_health
            : 0.0F,
        armor_ratio,
        entry->profile.maximum_stagger > 0.0F
            ? entry->stagger / entry->profile.maximum_stagger
            : 0.0F,
        entry->profile.astral,
        entry->health > 0.0F,
    };
}

auto LegendaryEnemySystem::render_snapshots(
    std::span<LegendaryEnemyRenderSnapshot> output) const noexcept
    -> std::size_t {
    auto written = std::size_t {0U};
    for (const auto& entry : entries_) {
        if (!entry.active || written >= output.size()) {
            continue;
        }
        const auto snapshot = render_snapshot(entry.id);
        if (snapshot.has_value()) {
            output[written] = *snapshot;
            ++written;
        }
    }
    return written;
}

auto LegendaryEnemySystem::combat_snapshots(
    std::span<LegendaryEnemyCombatSnapshot> output) const noexcept
    -> std::size_t {
    auto written = std::size_t {0U};
    for (const auto& entry : entries_) {
        if (!entry.active || written >= output.size()) {
            continue;
        }
        output[written] = {
            entry.id,
            entry.position + glm::vec3 {0.0F, 1.05F, 0.0F},
            entry.profile.weight == EntityWeight::Boss
                ? 1.35F
                : (entry.profile.weight == EntityWeight::Heavy
                       ? 0.85F
                       : 0.58F),
            entry.profile.weight,
            Faction::Hostile,
            entry.profile.corrupted,
            entry.profile.astral,
            entry.health > 0.0F,
        };
        ++written;
    }
    return written;
}

auto LegendaryEnemySystem::consume_events(
    std::span<LegendaryEnemyEvent> output) noexcept
    -> std::size_t {
    const auto count = std::min(output.size(), event_count_);
    std::copy_n(events_.begin(), count, output.begin());
    std::move(
        events_.begin() + static_cast<std::ptrdiff_t>(count),
        events_.begin() + static_cast<std::ptrdiff_t>(event_count_),
        events_.begin());
    event_count_ -= count;
    return count;
}

auto LegendaryEnemySystem::size() const noexcept -> std::size_t {
    return size_;
}

void LegendaryEnemySystem::clear() noexcept {
    entries_ = {};
    events_ = {};
    size_ = 0U;
    event_count_ = 0U;
    dropped_event_count_ = 0U;
    next_id_ = 1U;
    simulation_tick_ = 0U;
    tick_accumulator_ = 0.0;
}

} // namespace valcraft
