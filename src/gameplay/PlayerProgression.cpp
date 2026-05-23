#include "gameplay/PlayerProgression.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace valcraft {

namespace {

[[nodiscard]] auto next_experience_threshold(std::uint64_t current) noexcept -> std::uint64_t {
    const auto half_rounded_up = current / 2ULL + static_cast<std::uint64_t>((current % 2ULL) != 0ULL);
    if (current > std::numeric_limits<std::uint64_t>::max() - half_rounded_up) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    return current + half_rounded_up;
}

[[nodiscard]] auto quantized_axis(float value) noexcept -> std::int32_t {
    if (!std::isfinite(value)) {
        return 0;
    }

    const auto clamped = std::clamp(value, -1000000.0F, 1000000.0F);
    return static_cast<std::int32_t>(std::floor(clamped * 16.0F));
}

[[nodiscard]] auto mix_hash(std::uint32_t value) noexcept -> std::uint32_t {
    value ^= value >> 16U;
    value *= 0x7FEB352DU;
    value ^= value >> 15U;
    value *= 0x846CA68BU;
    value ^= value >> 16U;
    return value;
}

[[nodiscard]] auto hash_axis(std::int32_t value) noexcept -> std::uint32_t {
    return static_cast<std::uint32_t>(value);
}

} // namespace

auto player_experience_for_next_level(std::uint32_t level) noexcept -> std::uint64_t {
    if (level >= kPlayerProgressionMaxLevel) {
        return 0ULL;
    }

    auto threshold = kPlayerProgressionFirstLevelExperience;
    auto current_level = kPlayerProgressionMinLevel;
    while (current_level < std::max(level, kPlayerProgressionMinLevel)) {
        threshold = next_experience_threshold(threshold);
        ++current_level;
    }
    return threshold;
}

auto player_progression_bonus_percent(std::uint32_t level) noexcept -> float {
    const auto clamped_level = std::clamp(level, kPlayerProgressionMinLevel, kPlayerProgressionMaxLevel);
    return static_cast<float>(clamped_level - kPlayerProgressionMinLevel);
}

auto player_has_super_vision_power(std::uint32_t level) noexcept -> bool {
    return std::clamp(level, kPlayerProgressionMinLevel, kPlayerProgressionMaxLevel) >= kPlayerProgressionSuperVisionLevel;
}

auto player_has_flight_power(std::uint32_t level) noexcept -> bool {
    return std::clamp(level, kPlayerProgressionMinLevel, kPlayerProgressionMaxLevel) >= kPlayerProgressionFlightLevel;
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

auto block_break_experience(BlockId block_id) noexcept -> std::uint64_t {
    const auto item_id = block_item_id(block_id);
    switch (static_cast<BlockType>(item_id)) {
    case BlockType::Wood:
    case BlockType::PineWood:
    case BlockType::Planks:
        return 15ULL;
    case BlockType::Air:
    case BlockType::Pastron:
    case BlockType::RoundShield:
    case BlockType::Sword:
    case BlockType::Spear:
    case BlockType::Shoes:
    case BlockType::Pants:
        return 0ULL;
    default:
        return is_block_breakable(item_id) ? 10ULL : 0ULL;
    }
}

auto creature_kill_experience(CreatureSpecies species,
                              const glm::vec3& position,
                              std::uint32_t salt) noexcept -> std::uint64_t {
    auto hash = salt ^ 0x9E3779B9U;
    hash ^= mix_hash(static_cast<std::uint32_t>(species) + 0x85EBCA6BU);
    hash ^= mix_hash(hash_axis(quantized_axis(position.x)) + 0xC2B2AE35U);
    hash ^= mix_hash(hash_axis(quantized_axis(position.y)) + 0x27D4EB2FU);
    hash ^= mix_hash(hash_axis(quantized_axis(position.z)) + 0x165667B1U);
    return 1ULL + static_cast<std::uint64_t>(mix_hash(hash) % 100U);
}

auto experience_multiplier_for_activity(const CreatureCycleState& cycle,
                                        std::optional<int> surface_y,
                                        int activity_y) noexcept -> std::uint32_t {
    if (cycle.phase != CreaturePhase::Night || !surface_y.has_value()) {
        return 1U;
    }
    return activity_y >= *surface_y ? 2U : 1U;
}

auto multiply_experience(std::uint64_t base_experience, std::uint32_t multiplier) noexcept -> std::uint64_t {
    if (base_experience == 0ULL || multiplier == 0U) {
        return 0ULL;
    }
    if (base_experience > std::numeric_limits<std::uint64_t>::max() / static_cast<std::uint64_t>(multiplier)) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    return base_experience * static_cast<std::uint64_t>(multiplier);
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
    return 1.0F + player_progression_bonus_percent(state_.level) * 0.01F;
}

auto PlayerProgression::damage_resistance_percent() const noexcept -> float {
    return player_progression_bonus_percent(state_.level);
}

auto PlayerProgression::apnea_resistance_percent() const noexcept -> float {
    return player_progression_bonus_percent(state_.level);
}

auto PlayerProgression::fall_safety_multiplier() const noexcept -> float {
    return 1.0F + player_progression_bonus_percent(state_.level) * 0.01F;
}

auto PlayerProgression::movement_speed_multiplier() const noexcept -> float {
    return 1.0F + player_progression_bonus_percent(state_.level) * 0.01F;
}

auto PlayerProgression::block_break_speed_multiplier() const noexcept -> float {
    return 1.0F + player_progression_bonus_percent(state_.level) * 0.01F;
}

auto PlayerProgression::has_super_vision_power() const noexcept -> bool {
    return player_has_super_vision_power(state_.level);
}

auto PlayerProgression::has_flight_power() const noexcept -> bool {
    return player_has_flight_power(state_.level);
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
