#pragma once

#include "gameplay/progression/ProgressionCurve.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace valcraft {

enum class PlayerAttribute : std::uint8_t {
    Strength = 0,
    Wisdom = 1,
    Agility = 2,
    Robustness = 3,
    Count = 4,
};

inline constexpr std::size_t kPlayerAttributeCount =
    static_cast<std::size_t>(PlayerAttribute::Count);
inline constexpr std::uint8_t kPlayerAttributeLevelCap = 10U;
inline constexpr std::uint8_t kPlayerAttributeEffectiveCap = 15U;

struct PlayerAttributeAllocation {
    std::array<std::uint8_t, kPlayerAttributeCount> values {};

    auto operator==(const PlayerAttributeAllocation&) const -> bool = default;
};

[[nodiscard]] inline constexpr auto player_attribute_index(
    PlayerAttribute attribute) noexcept -> std::size_t {
    return static_cast<std::size_t>(attribute);
}

[[nodiscard]] inline auto sanitize_player_attribute_allocation(
    PlayerAttributeAllocation allocation,
    std::uint32_t level) noexcept -> PlayerAttributeAllocation {
    auto remaining =
        player_attribute_points_earned(
            level);
    for (auto& value : allocation.values) {
        const auto clamped =
            std::min<std::uint32_t>(
                value,
                kPlayerAttributeLevelCap);
        value =
            static_cast<std::uint8_t>(
                std::min(
                    clamped,
                    remaining));
        remaining -= value;
    }
    return allocation;
}

[[nodiscard]] inline constexpr auto player_attribute_value(
    const PlayerAttributeAllocation& allocation,
    PlayerAttribute attribute,
    std::uint8_t equipment_bonus = 0U) noexcept -> std::uint8_t {
    const auto index =
        player_attribute_index(
            attribute);
    if (index >= allocation.values.size()) {
        return 0U;
    }
    const auto total =
        static_cast<std::uint32_t>(
            allocation.values[index]) +
        static_cast<std::uint32_t>(
            equipment_bonus);
    return static_cast<std::uint8_t>(
        std::min<std::uint32_t>(
            total,
            kPlayerAttributeEffectiveCap));
}

[[nodiscard]] inline constexpr auto player_base_max_health(
    std::uint32_t level) noexcept -> float {
    const auto normalized_level =
        normalize_player_progression_level(
            level);
    return 20.0F +
           static_cast<float>(
               normalized_level / 10U);
}

[[nodiscard]] inline constexpr auto player_level_damage_multiplier(
    std::uint32_t level) noexcept -> float {
    const auto normalized_level =
        normalize_player_progression_level(
            level);
    return 1.0F +
           0.0025F *
               static_cast<float>(
                   normalized_level - 1U);
}

[[nodiscard]] inline constexpr auto player_level_mining_speed_multiplier(
    std::uint32_t level) noexcept -> float {
    const auto normalized_level =
        normalize_player_progression_level(
            level);
    const auto value =
        1.0F +
        0.0020F *
            static_cast<float>(
                normalized_level - 1U);
    return value < 1.20F ? value : 1.20F;
}

[[nodiscard]] inline constexpr auto player_level_movement_speed_multiplier(
    std::uint32_t level) noexcept -> float {
    const auto normalized_level =
        normalize_player_progression_level(
            level);
    const auto value =
        1.0F +
        0.0010F *
            static_cast<float>(
                normalized_level - 1U);
    return value < 1.10F ? value : 1.10F;
}

[[nodiscard]] inline constexpr auto player_level_damage_reduction(
    std::uint32_t level) noexcept -> float {
    const auto normalized_level =
        normalize_player_progression_level(
            level);
    const auto value =
        0.0010F *
        static_cast<float>(
            normalized_level - 1U);
    return value < 0.10F ? value : 0.10F;
}

[[nodiscard]] inline constexpr auto player_level_apnea_duration_multiplier(
    std::uint32_t level) noexcept -> float {
    const auto normalized_level =
        normalize_player_progression_level(
            level);
    return 1.0F +
           0.0050F *
               static_cast<float>(
                   normalized_level - 1U);
}

[[nodiscard]] inline constexpr auto player_level_safe_fall_multiplier(
    std::uint32_t level) noexcept -> float {
    const auto normalized_level =
        normalize_player_progression_level(
            level);
    return 1.0F +
           0.0025F *
               static_cast<float>(
                   normalized_level - 1U);
}

[[nodiscard]] inline constexpr auto player_melee_damage_multiplier(
    std::uint32_t level,
    std::uint8_t strength) noexcept -> float {
    const auto effective_strength =
        strength > kPlayerAttributeEffectiveCap
            ? kPlayerAttributeEffectiveCap
            : strength;
    return player_level_damage_multiplier(
               level) +
           0.02F *
               static_cast<float>(
                   effective_strength);
}

[[nodiscard]] inline constexpr auto player_ninja_damage_multiplier(
    std::uint32_t level,
    std::uint8_t agility) noexcept -> float {
    const auto effective_agility =
        agility > kPlayerAttributeEffectiveCap
            ? kPlayerAttributeEffectiveCap
            : agility;
    return player_level_damage_multiplier(
               level) +
           0.02F *
               static_cast<float>(
                   effective_agility);
}

[[nodiscard]] inline constexpr auto player_max_val_energy(
    std::uint8_t wisdom) noexcept -> float {
    const auto effective_wisdom =
        wisdom > kPlayerAttributeEffectiveCap
            ? kPlayerAttributeEffectiveCap
            : wisdom;
    return 100.0F +
           4.0F *
               static_cast<float>(
                   effective_wisdom);
}

[[nodiscard]] inline constexpr auto player_val_energy_regeneration(
    std::uint8_t wisdom) noexcept -> float {
    const auto effective_wisdom =
        wisdom > kPlayerAttributeEffectiveCap
            ? kPlayerAttributeEffectiveCap
            : wisdom;
    return 8.0F *
           (1.0F +
            0.01F *
                static_cast<float>(
                    effective_wisdom));
}

[[nodiscard]] inline constexpr auto player_total_movement_speed_multiplier(
    std::uint32_t level,
    std::uint8_t agility) noexcept -> float {
    const auto effective_agility =
        agility > kPlayerAttributeEffectiveCap
            ? kPlayerAttributeEffectiveCap
            : agility;
    return player_level_movement_speed_multiplier(
               level) *
           (1.0F +
            0.01F *
                static_cast<float>(
                    effective_agility));
}

[[nodiscard]] inline constexpr auto player_melee_recovery_multiplier(
    std::uint8_t agility) noexcept -> float {
    const auto effective_agility =
        agility > kPlayerAttributeEffectiveCap
            ? kPlayerAttributeEffectiveCap
            : agility;
    return 1.0F +
           0.015F *
               static_cast<float>(
                   effective_agility);
}

[[nodiscard]] inline constexpr auto player_robustness_damage_reduction(
    std::uint8_t robustness) noexcept -> float {
    const auto effective_robustness =
        robustness > kPlayerAttributeEffectiveCap
            ? kPlayerAttributeEffectiveCap
            : robustness;
    return 0.01F *
           static_cast<float>(
               effective_robustness);
}

[[nodiscard]] inline constexpr auto player_total_apnea_duration_multiplier(
    std::uint32_t level,
    std::uint8_t robustness) noexcept -> float {
    const auto effective_robustness =
        robustness > kPlayerAttributeEffectiveCap
            ? kPlayerAttributeEffectiveCap
            : robustness;
    return player_level_apnea_duration_multiplier(
               level) *
           (1.0F +
            0.03F *
                static_cast<float>(
                    effective_robustness));
}

[[nodiscard]] inline constexpr auto player_knockback_power_multiplier(
    std::uint8_t strength) noexcept -> float {
    const auto effective_strength =
        strength > kPlayerAttributeEffectiveCap
            ? kPlayerAttributeEffectiveCap
            : strength;
    return 1.0F +
           0.03F *
               static_cast<float>(
                   effective_strength);
}

[[nodiscard]] inline constexpr auto player_knockback_resistance_multiplier(
    std::uint8_t robustness) noexcept -> float {
    const auto effective_robustness =
        robustness > kPlayerAttributeEffectiveCap
            ? kPlayerAttributeEffectiveCap
            : robustness;
    return 1.0F +
           0.05F *
               static_cast<float>(
                   effective_robustness);
}

[[nodiscard]] inline constexpr auto player_summon_power_multiplier(
    std::uint8_t wisdom) noexcept -> float {
    const auto effective_wisdom =
        wisdom > kPlayerAttributeEffectiveCap
            ? kPlayerAttributeEffectiveCap
            : wisdom;
    return 1.0F +
           0.02F *
               static_cast<float>(
                   effective_wisdom);
}

[[nodiscard]] inline constexpr auto player_summon_health_multiplier(
    std::uint32_t level,
    std::uint8_t wisdom) noexcept -> float {
    const auto effective_wisdom =
        wisdom > kPlayerAttributeEffectiveCap
            ? kPlayerAttributeEffectiveCap
            : wisdom;
    const auto normalized_level =
        normalize_player_progression_level(
            level);
    return 1.0F +
           0.005F *
               static_cast<float>(
                   normalized_level - 1U) +
           0.02F *
               static_cast<float>(
                   effective_wisdom);
}

[[nodiscard]] inline constexpr auto player_summon_damage_multiplier(
    std::uint32_t level,
    std::uint8_t wisdom) noexcept -> float {
    const auto effective_wisdom =
        wisdom > kPlayerAttributeEffectiveCap
            ? kPlayerAttributeEffectiveCap
            : wisdom;
    const auto normalized_level =
        normalize_player_progression_level(
            level);
    return 1.0F +
           0.003F *
               static_cast<float>(
                   normalized_level - 1U) +
           0.02F *
               static_cast<float>(
                   effective_wisdom);
}

[[nodiscard]] inline constexpr auto player_builder_power_multiplier(
    std::uint8_t wisdom) noexcept -> float {
    return player_summon_power_multiplier(
        wisdom);
}

} // namespace valcraft
