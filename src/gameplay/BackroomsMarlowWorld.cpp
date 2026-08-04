#include "gameplay/BackroomsMarlowWorld.h"

#include "gameplay/PlayerController.h"
#include "world/World.h"

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace valcraft {

namespace {

[[nodiscard]] auto finite_vec3(const glm::vec3& value) noexcept -> bool {
    return std::isfinite(value.x) &&
           std::isfinite(value.y) &&
           std::isfinite(value.z);
}

[[nodiscard]] auto safe_world_position(const glm::vec3& value) noexcept
    -> bool {
    constexpr auto margin = 64.0;
    constexpr auto minimum =
        static_cast<double>(std::numeric_limits<int>::lowest()) + margin;
    constexpr auto maximum =
        static_cast<double>(std::numeric_limits<int>::max()) - margin;
    return finite_vec3(value) &&
           static_cast<double>(value.x) >= minimum &&
           static_cast<double>(value.x) <= maximum &&
           static_cast<double>(value.y) >= minimum &&
           static_cast<double>(value.y) <= maximum &&
           static_cast<double>(value.z) >= minimum &&
           static_cast<double>(value.z) <= maximum;
}

} // namespace

auto sweep_backrooms_marlow_drag(
    const PlayerController& player,
    const World& world,
    const glm::vec3& current_position,
    const glm::vec3& target_position,
    float maximum_distance) noexcept -> BackroomsMarlowDragSweepResult {
    if (!safe_world_position(current_position)) {
        return {glm::vec3 {0.0F}, true};
    }
    if (!safe_world_position(target_position) ||
        !std::isfinite(maximum_distance)) {
        return {current_position, true};
    }

    // Je verifie le point de depart avant les sorties rapides : meme un drag
    // nul doit remonter une penetration deja presente au directeur de Marlow.
    if (player.collides_at(world, current_position)) {
        return {current_position, true};
    }

    const auto safe_maximum_distance = std::clamp(
        maximum_distance,
        0.0F,
        kBackroomsMarlowMaximumDragDistance);
    const auto delta = target_position - current_position;
    const auto distance_squared = glm::dot(delta, delta);
    if (!std::isfinite(distance_squared)) {
        return {current_position, true};
    }
    if (distance_squared <= 1.0e-8F || safe_maximum_distance <= 0.0F) {
        return {current_position, false};
    }

    const auto distance = std::sqrt(distance_squared);
    const auto travel = std::min(distance, safe_maximum_distance);
    const auto direction = delta / distance;
    constexpr auto kSweepStep = 0.08F;
    constexpr auto kMaximumSweepSteps =
        static_cast<int>(
            kBackroomsMarlowMaximumDragDistance / kSweepStep) +
        1;
    const auto step_count = std::clamp(
        static_cast<int>(std::ceil(travel / kSweepStep)),
        1,
        kMaximumSweepSteps);

    auto previous_ratio = 0.0F;
    auto last_safe = current_position;
    for (auto step = 1; step <= step_count; ++step) {
        const auto ratio =
            static_cast<float>(step) /
            static_cast<float>(step_count);
        const auto candidate =
            current_position + direction * (travel * ratio);
        if (!player.collides_at(world, candidate)) {
            previous_ratio = ratio;
            last_safe = candidate;
            continue;
        }

        // Je raffine le premier impact pour conserver la derniere position
        // valide sans dependre de la cadence des frames.
        auto safe_ratio = previous_ratio;
        auto blocked_ratio = ratio;
        for (auto iteration = 0; iteration < 10; ++iteration) {
            const auto middle_ratio =
                (safe_ratio + blocked_ratio) * 0.5F;
            const auto middle =
                current_position + direction * (travel * middle_ratio);
            if (player.collides_at(world, middle)) {
                blocked_ratio = middle_ratio;
            } else {
                safe_ratio = middle_ratio;
                last_safe = middle;
            }
        }
        return {last_safe, true};
    }

    return {current_position + direction * travel, false};
}

auto backrooms_marlow_flashlight_hits_water(
    const World& world,
    const glm::vec3& eye_position,
    const glm::vec3& look_direction,
    bool flashlight_enabled,
    float flashlight_intensity,
    float maximum_distance) noexcept -> bool {
    if (!flashlight_enabled ||
        !std::isfinite(flashlight_intensity) ||
        flashlight_intensity <= 0.0F ||
        !safe_world_position(eye_position) ||
        !finite_vec3(look_direction) ||
        !std::isfinite(maximum_distance)) {
        return false;
    }

    const auto direction_length_squared =
        glm::dot(look_direction, look_direction);
    if (!std::isfinite(direction_length_squared) ||
        direction_length_squared <= 1.0e-6F) {
        return false;
    }

    const auto safe_maximum_distance = std::clamp(
        maximum_distance,
        0.0F,
        kBackroomsMarlowFlashlightRange);
    if (safe_maximum_distance <= 0.0F) {
        return false;
    }
    const auto hit = world.raycast_water_or_opaque(
        eye_position,
        look_direction,
        safe_maximum_distance);
    return hit.hit && hit.block_id == to_block_id(BlockType::Water);
}

auto advance_backrooms_marlow_death_delay(
    float remaining_seconds,
    float dt,
    bool armed_this_update) noexcept -> float {
    const auto remaining =
        std::isfinite(remaining_seconds)
            ? std::max(remaining_seconds, 0.0F)
            : 0.0F;
    if (armed_this_update) {
        return remaining;
    }
    const auto safe_dt =
        std::isfinite(dt)
            ? std::clamp(dt, 0.0F, 0.25F)
            : 0.0F;
    return std::max(remaining - safe_dt, 0.0F);
}

} // namespace valcraft
