#include "gameplay/PlayerProgression.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace valcraft {

namespace {

[[nodiscard]] auto legacy_experience_for_next_level(
    std::uint32_t level) noexcept -> std::uint64_t {
    auto threshold = std::uint64_t {100};
    const auto normalized_level =
        std::clamp(
            level,
            kPlayerProgressionMinLevel,
            kPlayerProgressionMaxLevel);
    for (auto current = kPlayerProgressionMinLevel;
         current < normalized_level;
         ++current) {
        const auto half_rounded_up =
            threshold / 2ULL +
            static_cast<std::uint64_t>(
                threshold % 2ULL != 0ULL);
        if (threshold >
            std::numeric_limits<std::uint64_t>::max() -
                half_rounded_up) {
            return std::numeric_limits<std::uint64_t>::max();
        }
        threshold += half_rounded_up;
    }
    return threshold;
}

} // namespace

auto player_progression_bonus_percent(std::uint32_t level) noexcept -> float {
    return (
               player_derived_stats(
                   level)
                       .attack_damage_multiplier -
               1.0F) *
           100.0F;
}

auto player_has_super_vision_power(std::uint32_t level) noexcept -> bool {
    return player_progression_capabilities(
               level)
        .super_vision
        .unlocked;
}

auto player_has_flight_power(std::uint32_t level) noexcept -> bool {
    return player_progression_capabilities(
               level)
        .flight
        .unlocked;
}

auto sanitize_player_progression_state(PlayerProgressionState state) noexcept -> PlayerProgressionState {
    state.level = std::clamp(state.level, kPlayerProgressionMinLevel, kPlayerProgressionMaxLevel);
    if (state.level >= kPlayerProgressionMaxLevel) {
        state.experience = 0ULL;
        return state;
    }

    while (state.level < kPlayerProgressionMaxLevel) {
        const auto threshold = player_experience_for_next_level(state.level);
        if (threshold == 0ULL || state.experience < threshold) {
            break;
        }
        state.experience -= threshold;
        ++state.level;
    }

    if (state.level >= kPlayerProgressionMaxLevel) {
        state.experience = 0ULL;
    }
    return state;
}

auto migrate_legacy_player_progression_state(
    PlayerProgressionState legacy_state) noexcept -> PlayerProgressionState {
    // Je préserve le niveau et la position relative dans son ancienne barre
    // d'XP : une migration ne doit ni offrir ni retirer arbitrairement un niveau.
    legacy_state.level =
        std::clamp(
            legacy_state.level,
            kPlayerProgressionMinLevel,
            kPlayerProgressionMaxLevel);
    if (legacy_state.level >=
        kPlayerProgressionMaxLevel) {
        legacy_state.experience = 0ULL;
        return legacy_state;
    }

    const auto old_threshold =
        legacy_experience_for_next_level(
            legacy_state.level);
    const auto new_threshold =
        player_experience_for_next_level(
            legacy_state.level);
    if (old_threshold == 0ULL ||
        new_threshold == 0ULL) {
        legacy_state.experience = 0ULL;
        return legacy_state;
    }

    const auto bounded_old_experience =
        std::min(
            legacy_state.experience,
            old_threshold - 1ULL);
    const auto ratio =
        static_cast<long double>(
            bounded_old_experience) /
        static_cast<long double>(
            old_threshold);
    const auto migrated =
        static_cast<std::uint64_t>(
            std::floor(
                ratio *
                    static_cast<long double>(
                        new_threshold) +
                0.5L));
    legacy_state.experience =
        std::min(
            migrated,
            new_threshold - 1ULL);
    return legacy_state;
}

auto block_break_experience(
    BlockId block_id,
    bool player_placed) noexcept -> std::uint64_t {
    return ExperienceRewardPolicy::harvest_experience(
        block_id,
        player_placed);
}

auto creature_kill_experience(CreatureSpecies species,
                              const glm::vec3& position,
                              std::uint32_t salt) noexcept -> std::uint64_t {
    (void)position;
    (void)salt;
    return ExperienceRewardPolicy::creature_profile_experience(
        ThreatRank::Zero,
        EntityWeight::Light,
        Faction::Neutral,
        species !=
            CreatureSpecies::Villager);
}

auto apply_experience_modifiers(
    std::uint64_t base_experience,
    const ExperienceAwardContext& context) noexcept -> std::uint64_t {
    if (context.reason !=
        ExperienceReason::Combat) {
        return base_experience;
    }
    return ExperienceRewardPolicy::combat_experience(
        base_experience,
        context.hostile_target,
        context.surface_water_context,
        context.phase);
}

void PlayerProgression::reset() noexcept {
    state_ = {};
}

void PlayerProgression::load_state(PlayerProgressionState state) noexcept {
    state_ = sanitize_player_progression_state(state);
}

auto PlayerProgression::state() const noexcept -> PlayerProgressionState {
    return state_;
}

auto PlayerProgression::level() const noexcept -> std::uint32_t {
    return state_.level;
}

auto PlayerProgression::experience() const noexcept -> std::uint64_t {
    return state_.experience;
}

auto PlayerProgression::experience_for_next_level() const noexcept -> std::uint64_t {
    return player_experience_for_next_level(state_.level);
}

auto PlayerProgression::level_progress_ratio() const noexcept -> float {
    const auto threshold = experience_for_next_level();
    if (threshold == 0ULL) {
        return 1.0F;
    }
    return std::clamp(static_cast<float>(state_.experience) / static_cast<float>(threshold), 0.0F, 1.0F);
}

auto PlayerProgression::attack_damage_multiplier() const noexcept -> float {
    return derived_stats()
        .attack_damage_multiplier;
}

auto PlayerProgression::damage_resistance_percent() const noexcept -> float {
    return derived_stats()
        .damage_reduction_percent;
}

auto PlayerProgression::apnea_resistance_percent() const noexcept -> float {
    return derived_stats()
        .apnea_resistance_percent;
}

auto PlayerProgression::fall_safety_multiplier() const noexcept -> float {
    return derived_stats()
        .safe_fall_multiplier;
}

auto PlayerProgression::movement_speed_multiplier() const noexcept -> float {
    return derived_stats()
        .movement_speed_multiplier;
}

auto PlayerProgression::block_break_speed_multiplier() const noexcept -> float {
    return derived_stats()
        .mining_speed_multiplier;
}

auto PlayerProgression::base_max_health() const noexcept -> float {
    return derived_stats()
        .base_max_health;
}

auto PlayerProgression::derived_stats() const noexcept -> PlayerDerivedStats {
    return player_derived_stats(
        state_.level);
}

auto PlayerProgression::capabilities(
    PlayerProgressionMode mode) const noexcept
    -> PlayerProgressionCapabilities {
    return player_progression_capabilities(
        state_.level,
        mode);
}

auto PlayerProgression::has_super_vision_power() const noexcept -> bool {
    return capabilities()
        .super_vision
        .unlocked;
}

auto PlayerProgression::has_flight_power() const noexcept -> bool {
    return capabilities()
        .flight
        .unlocked;
}

auto PlayerProgression::is_max_level() const noexcept -> bool {
    return state_.level >= kPlayerProgressionMaxLevel;
}

auto PlayerProgression::add_experience(std::uint64_t amount) noexcept -> PlayerExperienceGainResult {
    PlayerExperienceGainResult result {};

    if (amount == 0ULL) {
        return result;
    }
    if (is_max_level()) {
        result.reached_max_level = is_max_level();
        return result;
    }

    auto pending = amount;
    while (pending > 0ULL && state_.level < kPlayerProgressionMaxLevel) {
        const auto threshold = player_experience_for_next_level(state_.level);
        const auto remaining = threshold > state_.experience ? threshold - state_.experience : 0ULL;
        if (pending < remaining) {
            state_.experience += pending;
            result.awarded_experience += pending;
            pending = 0ULL;
            break;
        }

        pending -= remaining;
        result.awarded_experience += remaining;
        state_.experience = 0ULL;
        ++state_.level;
        ++result.levels_gained;
    }

    if (state_.level >= kPlayerProgressionMaxLevel) {
        state_.level = kPlayerProgressionMaxLevel;
        state_.experience = 0ULL;
        result.reached_max_level = true;
    }

    return result;
}

} // namespace valcraft
