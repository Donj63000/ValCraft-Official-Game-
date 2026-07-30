#include "gameplay/progression/VanguardTargeting.h"

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <utility>

namespace valcraft {

namespace {

[[nodiscard]] auto finite_vec3(
    const glm::vec3& value) noexcept -> bool {
    return std::isfinite(value.x) &&
           std::isfinite(value.y) &&
           std::isfinite(value.z);
}

[[nodiscard]] auto horizontal_forward(
    const glm::vec3& value) noexcept -> glm::vec3 {
    if (!finite_vec3(value)) {
        return {0.0F, 0.0F, 1.0F};
    }
    const glm::vec3 horizontal {
        value.x,
        0.0F,
        value.z,
    };
    const auto squared_length =
        glm::dot(horizontal, horizontal);
    if (squared_length <= 0.000001F) {
        return {0.0F, 0.0F, 1.0F};
    }
    return horizontal /
           std::sqrt(squared_length);
}

[[nodiscard]] auto target_distance_squared(
    const VanguardTargetCandidate& target,
    const glm::vec3& origin) noexcept -> float {
    const auto delta =
        target.aim_position - origin;
    return glm::dot(delta, delta);
}

} // namespace

auto select_vanguard_targets(
    const VanguardTargetingQuery& query) noexcept
    -> VanguardTargetSelection {
    VanguardTargetSelection result {};
    if (!finite_vec3(query.origin) ||
        !std::isfinite(query.range_meters) ||
        query.range_meters <= 0.0F ||
        !std::isfinite(query.half_angle_degrees)) {
        return result;
    }

    const auto forward =
        horizontal_forward(query.forward);
    const auto half_angle =
        std::clamp(
            query.half_angle_degrees,
            0.0F,
            180.0F);
    constexpr auto kDegreesToRadians =
        0.01745329251994329577F;
    const auto minimum_dot =
        std::cos(
            half_angle *
            kDegreesToRadians);
    const auto maximum_distance_squared =
        query.range_meters *
        query.range_meters;

    for (const auto& candidate :
         query.candidates) {
        if (!candidate.hostile ||
            candidate.id == 0U ||
            !finite_vec3(
                candidate.aim_position)) {
            continue;
        }

        const auto delta =
            candidate.aim_position -
            query.origin;
        const auto distance_squared =
            glm::dot(delta, delta);
        if (!std::isfinite(distance_squared) ||
            distance_squared <= 0.000001F ||
            distance_squared >
                maximum_distance_squared) {
            continue;
        }
        const auto horizontal_delta =
            glm::vec3 {
                delta.x,
                0.0F,
                delta.z,
            };
        const auto horizontal_squared =
            glm::dot(
                horizontal_delta,
                horizontal_delta);
        if (horizontal_squared > 0.000001F) {
            const auto direction =
                horizontal_delta /
                std::sqrt(
                    horizontal_squared);
            if (glm::dot(
                    forward,
                    direction) +
                    0.000001F <
                minimum_dot) {
                continue;
            }
        }

        const auto distance =
            std::sqrt(
                distance_squared);
        const auto normalized_direction =
            delta / distance;
        if (query.is_visible != nullptr &&
            !query.is_visible(
                query.visibility_user_data,
                query.origin,
                normalized_direction,
                distance)) {
            continue;
        }

        auto duplicate = false;
        for (std::size_t index = 0U;
             index < result.target_count;
             ++index) {
            if (result.targets[index].id ==
                candidate.id) {
                duplicate = true;
                break;
            }
        }
        if (duplicate ||
            result.target_count >=
                result.targets.size()) {
            continue;
        }
        result.targets[
            result.target_count++] =
            candidate;
    }

    // Je stabilise l'ordre par distance puis par identifiant : deux clients
    // observent la même cible principale même si leur stockage source diffère.
    std::sort(
        result.targets.begin(),
        result.targets.begin() +
            static_cast<std::ptrdiff_t>(
                result.target_count),
        [&](const auto& lhs,
            const auto& rhs) noexcept {
            const auto lhs_distance =
                target_distance_squared(
                    lhs,
                    query.origin);
            const auto rhs_distance =
                target_distance_squared(
                    rhs,
                    query.origin);
            if (std::abs(
                    lhs_distance -
                    rhs_distance) >
                0.000001F) {
                return lhs_distance <
                       rhs_distance;
            }
            return lhs.id < rhs.id;
        });
    return result;
}

} // namespace valcraft
