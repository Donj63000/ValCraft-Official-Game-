#include "gameplay/progression/ProgressionCurve.h"

#include <algorithm>

namespace valcraft {

namespace {

[[nodiscard]] auto experience_threshold(
    std::uint32_t level,
    std::uint64_t linear_coefficient,
    std::uint64_t quadratic_coefficient) noexcept -> std::uint64_t {
    const auto normalized_level =
        normalize_player_progression_level(
            level);
    if (normalized_level >=
        kPlayerProgressionMaxLevel) {
        return 0ULL;
    }

    const auto offset =
        static_cast<std::uint64_t>(
            normalized_level -
            kPlayerProgressionMinLevel);
    return 100ULL +
           linear_coefficient *
               offset +
           quadratic_coefficient *
               offset *
               offset;
}

} // namespace

auto player_experience_for_next_level(
    std::uint32_t level) noexcept -> std::uint64_t {
    return experience_threshold(
        level,
        16ULL,
        1ULL);
}

auto player_experience_for_next_level_v13(
    std::uint32_t level) noexcept -> std::uint64_t {
    return experience_threshold(
        level,
        20ULL,
        2ULL);
}

auto player_cumulative_experience_for_level(
    std::uint32_t level) noexcept -> std::uint64_t {
    const auto normalized_level =
        normalize_player_progression_level(
            level);
    auto total = std::uint64_t {0};
    for (auto current = kPlayerProgressionMinLevel;
         current < normalized_level;
         ++current) {
        total +=
            player_experience_for_next_level(
                current);
    }
    return total;
}

auto player_skill_points_earned(
    std::uint32_t level) noexcept -> std::uint32_t {
    return std::clamp(
        level,
        kPlayerProgressionMinLevel,
        kPlayerProgressionMaxLevel);
}

auto player_attribute_points_earned(
    std::uint32_t level) noexcept -> std::uint32_t {
    return std::clamp(
               level,
               kPlayerProgressionMinLevel,
               kPlayerProgressionMaxLevel) /
           5U;
}

auto player_mastery_points_earned(
    std::uint32_t level) noexcept -> std::uint32_t {
    const auto normalized_level =
        std::clamp(
            level,
            kPlayerProgressionMinLevel,
            kPlayerProgressionMaxLevel);
    if (normalized_level < 60U) {
        return 0U;
    }
    return std::min(
        5U,
        (normalized_level - 50U) / 10U);
}

} // namespace valcraft
