#pragma once

#include "gameplay/weapons/ColossalWeaponDefinition.h"

#include <glm/vec3.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace valcraft {

using ColossalCombatTargetId = std::uint64_t;
using ColossalCombatZoneId = std::uint16_t;

inline constexpr std::size_t kMaximumColossalSweepCandidates = 64U;
inline constexpr std::size_t kMaximumColossalSweepHits = 12U;
inline constexpr std::size_t kMaximumColossalHitLedgerEntries = 32U;

enum class ColossalSweepError : std::uint8_t {
    None = 0,
    InvalidQuery,
    CandidateCapacityExceeded,
    HitLedgerCapacityExceeded,
};

struct ColossalBladePose {
    glm::vec3 hilt {0.0F};
    glm::vec3 tip {0.0F, 0.0F, 1.0F};
};

struct ColossalSweepCandidate {
    ColossalCombatTargetId target_id = 0U;
    ColossalCombatZoneId zone_id = 0U;
    glm::vec3 center {0.0F};
    float radius = 0.5F;
    std::uint8_t hit_priority = 0U;
    bool enabled = true;
    bool friendly = false;
};

struct ColossalSweepOcclusionRequest {
    ColossalCombatTargetId target_id = 0U;
    ColossalCombatZoneId zone_id = 0U;
    glm::vec3 origin {0.0F};
    glm::vec3 target_center {0.0F};
};

using ColossalSweepOcclusionProbe = bool (*)(
    void* user_data,
    const ColossalSweepOcclusionRequest& request) noexcept;

struct ColossalSweepCallbacks {
    void* user_data = nullptr;
    ColossalSweepOcclusionProbe is_occluded = nullptr;
};

struct ColossalSweepQuery {
    std::uint64_t attack_sequence = 0U;
    ColossalBladePose previous_pose {};
    ColossalBladePose current_pose {};
    glm::vec3 attack_origin {0.0F};
    glm::vec3 forward {0.0F, 0.0F, 1.0F};
    float blade_radius = 0.18F;
    float maximum_range = 3.25F;
    float arc_degrees = 150.0F;
    std::uint8_t maximum_targets = 6U;
    bool enforce_horizontal_arc = true;
    bool can_hit_friendlies = false;
};

struct ColossalShockwaveQuery {
    std::uint64_t attack_sequence = 0U;
    glm::vec3 origin {0.0F};
    float radius = 0.0F;
    std::uint8_t maximum_targets = 6U;
    bool can_hit_friendlies = false;
};

struct ColossalSweepHit {
    ColossalCombatTargetId target_id = 0U;
    ColossalCombatZoneId zone_id = 0U;
    glm::vec3 target_center {0.0F};
    glm::vec3 contact_point {0.0F};
    float center_distance_squared = 0.0F;
    float blade_distance_squared = 0.0F;
    std::uint8_t hit_priority = 0U;
};

struct ColossalSweepResult {
    ColossalSweepError error = ColossalSweepError::None;
    std::array<ColossalSweepHit, kMaximumColossalSweepHits> hits {};
    std::size_t hit_count = 0U;
    std::size_t geometric_contact_count = 0U;
    std::size_t occluded_count = 0U;
    std::size_t duplicate_count = 0U;
    std::size_t friendly_ignored_count = 0U;
    bool target_limit_reached = false;

    [[nodiscard]] auto accepted_hits() const noexcept
        -> std::span<const ColossalSweepHit> {
        return {
            hits.data(),
            std::min(hit_count, hits.size()),
        };
    }
};

class ColossalHitLedger {
public:
    void begin_attack(
        std::uint64_t attack_sequence) noexcept;
    [[nodiscard]] auto contains(
        ColossalCombatTargetId target_id) const noexcept -> bool;
    [[nodiscard]] auto try_register(
        ColossalCombatTargetId target_id) noexcept -> bool;
    [[nodiscard]] auto attack_sequence() const noexcept
        -> std::uint64_t;
    [[nodiscard]] auto size() const noexcept -> std::size_t;
    [[nodiscard]] auto full() const noexcept -> bool;
    void clear() noexcept;

private:
    std::uint64_t attack_sequence_ = 0U;
    std::array<
        ColossalCombatTargetId,
        kMaximumColossalHitLedgerEntries>
        entries_ {};
    std::size_t size_ = 0U;
};

struct ColossalTunnelClearance {
    float left_blocks = 0.0F;
    float right_blocks = 0.0F;
    float overhead_blocks = 0.0F;
    float forward_blocks = 0.0F;
};

struct ColossalTunnelAttackChoice {
    ColossalAttackShape shape =
        ColossalAttackShape::HorizontalArc;
    bool replaced_with_vertical = false;
    bool attack_has_clearance = false;
};

[[nodiscard]] auto resolve_colossal_sweep(
    const ColossalSweepQuery& query,
    std::span<const ColossalSweepCandidate> candidates,
    ColossalHitLedger& ledger,
    const ColossalSweepCallbacks& callbacks = {}) noexcept
    -> ColossalSweepResult;

// Je résous l'onde séparément du volume de lame afin qu'une cible proche de
// l'impact puisse être touchée sans devoir croiser visuellement le tranchant.
[[nodiscard]] auto resolve_colossal_shockwave(
    const ColossalShockwaveQuery& query,
    std::span<const ColossalSweepCandidate> candidates,
    ColossalHitLedger& ledger,
    const ColossalSweepCallbacks& callbacks = {}) noexcept
    -> ColossalSweepResult;

[[nodiscard]] auto choose_colossal_tunnel_attack(
    ColossalAttackShape requested_shape,
    const ColossalTunnelClearance& clearance,
    float required_horizontal_blocks = 2.75F,
    float required_vertical_blocks = 2.25F,
    float required_forward_blocks = 1.25F) noexcept
    -> ColossalTunnelAttackChoice;

} // namespace valcraft
