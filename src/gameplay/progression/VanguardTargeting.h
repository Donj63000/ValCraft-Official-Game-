#pragma once

#include "gameplay/progression/AbilityEvents.h"

#include <glm/vec3.hpp>

#include <array>
#include <cstddef>
#include <span>

namespace valcraft {

inline constexpr std::size_t kVanguardMaximumTargetCount = 32U;

struct VanguardTargetCandidate {
    AbilityEventEntityId id = 0U;
    glm::vec3 aim_position {0.0F};
    bool hostile = false;

    auto operator==(const VanguardTargetCandidate&) const -> bool = default;
};

using VanguardVisibilityQuery = bool (*)(
    void* user_data,
    const glm::vec3& origin,
    const glm::vec3& normalized_direction,
    float target_distance) noexcept;

struct VanguardTargetingQuery {
    glm::vec3 origin {0.0F};
    glm::vec3 forward {0.0F, 0.0F, 1.0F};
    float range_meters = 0.0F;
    float half_angle_degrees = 45.0F;
    std::span<const VanguardTargetCandidate> candidates {};
    void* visibility_user_data = nullptr;
    VanguardVisibilityQuery is_visible = nullptr;
};

struct VanguardTargetSelection {
    std::array<
        VanguardTargetCandidate,
        kVanguardMaximumTargetCount>
        targets {};
    std::size_t target_count = 0U;

    [[nodiscard]] constexpr auto empty() const noexcept -> bool {
        return target_count == 0U;
    }
};

// Je garde la sélection indépendante du moteur afin de tester exactement le
// cône, la portée, la déduplication et l'occlusion sans démarrer une partie.
[[nodiscard]] auto select_vanguard_targets(
    const VanguardTargetingQuery& query) noexcept
    -> VanguardTargetSelection;

} // namespace valcraft
