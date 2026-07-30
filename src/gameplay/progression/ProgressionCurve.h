#pragma once

#include <cstdint>

namespace valcraft {

inline constexpr std::uint32_t kPlayerProgressionMinLevel = 1U;
inline constexpr std::uint32_t kPlayerProgressionMaxLevel = 100U;
inline constexpr std::uint32_t kPlayerProgressionSuperVisionLevel = 30U;
inline constexpr std::uint32_t kPlayerProgressionFlightLevel =
    kPlayerProgressionMaxLevel;
inline constexpr std::uint64_t kPlayerProgressionTotalExperience =
    406'065ULL;
inline constexpr std::uint64_t kPlayerProgressionV13TotalExperience =
    744'018ULL;

[[nodiscard]] inline constexpr auto normalize_player_progression_level(
    std::uint32_t level) noexcept -> std::uint32_t {
    if (level < kPlayerProgressionMinLevel) {
        return kPlayerProgressionMinLevel;
    }
    if (level > kPlayerProgressionMaxLevel) {
        return kPlayerProgressionMaxLevel;
    }
    return level;
}

// Je centralise ici la courbe officielle afin que le jeu, la sauvegarde et
// les tests ne puissent plus diverger sur un seuil de niveau.
[[nodiscard]] auto player_experience_for_next_level(
    std::uint32_t level) noexcept -> std::uint64_t;

// Je conserve la formule de la version 13 sous un nom explicite : une future
// migration pourra relire son ancien seuil sans dépendre de la courbe active.
[[nodiscard]] auto player_experience_for_next_level_v13(
    std::uint32_t level) noexcept -> std::uint64_t;

[[nodiscard]] auto player_cumulative_experience_for_level(
    std::uint32_t level) noexcept -> std::uint64_t;

[[nodiscard]] auto player_skill_points_earned(
    std::uint32_t level) noexcept -> std::uint32_t;

[[nodiscard]] auto player_attribute_points_earned(
    std::uint32_t level) noexcept -> std::uint32_t;

[[nodiscard]] auto player_mastery_points_earned(
    std::uint32_t level) noexcept -> std::uint32_t;

} // namespace valcraft
