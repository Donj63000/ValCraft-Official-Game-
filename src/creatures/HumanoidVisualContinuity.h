#pragma once

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>

#include <algorithm>
#include <cmath>

namespace valcraft {

struct HumanoidLimbSpan {
    glm::vec3 start {0.0F};
    glm::vec3 end {0.0F};
    bool valid = false;
};

// J'allonge légèrement chaque os visuel de part et d'autre de son articulation.
// Les capsules se recouvrent ainsi dans les épaules, les genoux et les chevilles
// au lieu de ne se toucher que par un unique sommet arrondi.
[[nodiscard]] inline auto make_overlapping_humanoid_limb_span(
    const glm::vec3& start,
    const glm::vec3& end,
    float requested_overlap) noexcept -> HumanoidLimbSpan {
    const auto delta = end - start;
    const auto length_squared = glm::dot(delta, delta);
    if (!std::isfinite(length_squared) ||
        length_squared <= 1.0e-8F ||
        !std::isfinite(requested_overlap)) {
        return {start, end, false};
    }

    const auto length = std::sqrt(length_squared);
    const auto direction = delta / length;
    const auto overlap = std::clamp(
        requested_overlap,
        0.0F,
        length * 0.18F);
    return {
        start - direction * overlap,
        end + direction * overlap,
        true,
    };
}

} // namespace valcraft
