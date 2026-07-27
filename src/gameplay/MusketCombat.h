#pragma once

#include <glm/vec3.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>

namespace valcraft {

inline constexpr float kMusketHitTieEpsilon = 0.025F;

enum class MusketHitKind : std::uint8_t {
    None = 0,
    World = 1,
    Ship = 2,
    OldGuard = 3,
    Crew = 4,
    Creature = 5,
};

struct MusketHit {
    MusketHitKind kind = MusketHitKind::None;
    glm::vec3 position {0.0F};
    float distance = 0.0F;
    std::uint64_t target_id = 0U;

    [[nodiscard]] constexpr auto hit() const noexcept -> bool {
        return kind != MusketHitKind::None;
    }
};

[[nodiscard]] inline constexpr auto musket_hit_priority(
    MusketHitKind kind) noexcept -> int {

    switch (kind) {
    case MusketHitKind::World:
        return 0;
    case MusketHitKind::Ship:
        return 1;
    case MusketHitKind::OldGuard:
        return 2;
    case MusketHitKind::Crew:
        return 3;
    case MusketHitKind::Creature:
        return 4;
    case MusketHitKind::None:
    default:
        return 5;
    }
}

[[nodiscard]] inline constexpr auto musket_hit_is_priority_blocker(
    MusketHitKind kind) noexcept -> bool {
    return kind == MusketHitKind::World ||
           kind == MusketHitKind::Ship ||
           kind == MusketHitKind::OldGuard;
}

[[nodiscard]] inline constexpr auto musket_hit_can_receive_damage(
    MusketHitKind kind) noexcept -> bool {
    return kind == MusketHitKind::Crew ||
           kind == MusketHitKind::Creature;
}

[[nodiscard]] inline auto musket_hit_precedes(
    const MusketHit& candidate,
    const MusketHit& current,
    float tie_epsilon) noexcept -> bool {

    const auto distance_difference =
        std::abs(candidate.distance - current.distance);
    const auto tied =
        distance_difference == 0.0F ||
        distance_difference < tie_epsilon;
    if (!tied) {
        return candidate.distance < current.distance;
    }

    const auto candidate_is_blocker =
        musket_hit_is_priority_blocker(
            candidate.kind);
    const auto current_is_blocker =
        musket_hit_is_priority_blocker(
            current.kind);
    if (candidate_is_blocker != current_is_blocker) {
        return candidate_is_blocker;
    }
    if (candidate.distance != current.distance) {
        // Je conserve la vraie surface la plus proche lorsque deux bloqueurs,
        // ou deux cibles dommageables, appartiennent a la meme zone d'impact.
        return candidate.distance < current.distance;
    }

    const auto candidate_priority =
        musket_hit_priority(candidate.kind);
    const auto current_priority =
        musket_hit_priority(current.kind);
    if (candidate_priority != current_priority) {
        return candidate_priority < current_priority;
    }
    return candidate.target_id < current.target_id;
}

[[nodiscard]] inline auto select_nearest_musket_hit(
    std::span<const MusketHit> candidates,
    float maximum_distance,
    float tie_epsilon = kMusketHitTieEpsilon) noexcept -> MusketHit {

    if (!std::isfinite(maximum_distance) ||
        maximum_distance <= 0.0F) {
        return {};
    }
    const auto epsilon =
        std::isfinite(tie_epsilon)
            ? std::max(tie_epsilon, 0.0F)
            : kMusketHitTieEpsilon;

    MusketHit best {};
    for (const auto& candidate : candidates) {
        if (!candidate.hit() ||
            !std::isfinite(candidate.distance) ||
            candidate.distance < 0.0F ||
            candidate.distance > maximum_distance ||
            !std::isfinite(candidate.position.x) ||
            !std::isfinite(candidate.position.y) ||
            !std::isfinite(candidate.position.z)) {
            continue;
        }

        if (!best.hit() ||
            musket_hit_precedes(
                candidate,
                best,
                epsilon)) {
            best = candidate;
        }
    }
    return best;
}

} // namespace valcraft
