#include "gameplay/combat/ColossalSweep.h"

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace valcraft {
namespace {

struct ClosestPointResult {
    glm::vec3 point {0.0F};
    float distance_squared =
        std::numeric_limits<float>::max();
};

[[nodiscard]] auto finite_vector(
    const glm::vec3& value) noexcept -> bool {
    return std::isfinite(value.x) &&
           std::isfinite(value.y) &&
           std::isfinite(value.z);
}

[[nodiscard]] auto closest_point_on_segment(
    const glm::vec3& point,
    const glm::vec3& start,
    const glm::vec3& end) noexcept
    -> ClosestPointResult {
    const auto segment = end - start;
    const auto length_squared =
        glm::dot(segment, segment);
    auto parameter = 0.0F;
    if (length_squared >
        std::numeric_limits<float>::epsilon()) {
        parameter =
            std::clamp(
                glm::dot(point - start, segment) /
                    length_squared,
                0.0F,
                1.0F);
    }
    const auto closest =
        start + segment * parameter;
    const auto delta = point - closest;
    return {
        closest,
        glm::dot(delta, delta),
    };
}

[[nodiscard]] auto closest_of(
    ClosestPointResult lhs,
    const ClosestPointResult& rhs) noexcept
    -> ClosestPointResult {
    return rhs.distance_squared <
                   lhs.distance_squared
               ? rhs
               : lhs;
}

[[nodiscard]] auto closest_point_on_triangle(
    const glm::vec3& point,
    const glm::vec3& a,
    const glm::vec3& b,
    const glm::vec3& c) noexcept
    -> ClosestPointResult {
    const auto ab = b - a;
    const auto ac = c - a;
    const auto normal =
        glm::cross(ab, ac);
    if (glm::dot(normal, normal) <=
        std::numeric_limits<float>::epsilon()) {
        auto result =
            closest_point_on_segment(
                point,
                a,
                b);
        result =
            closest_of(
                result,
                closest_point_on_segment(
                    point,
                    b,
                    c));
        return closest_of(
            result,
            closest_point_on_segment(
                point,
                c,
                a));
    }

    const auto ap = point - a;
    const auto d1 = glm::dot(ab, ap);
    const auto d2 = glm::dot(ac, ap);
    if (d1 <= 0.0F && d2 <= 0.0F) {
        return {
            a,
            glm::dot(ap, ap),
        };
    }

    const auto bp = point - b;
    const auto d3 = glm::dot(ab, bp);
    const auto d4 = glm::dot(ac, bp);
    if (d3 >= 0.0F && d4 <= d3) {
        return {
            b,
            glm::dot(bp, bp),
        };
    }

    const auto vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0F &&
        d1 >= 0.0F &&
        d3 <= 0.0F) {
        const auto denominator = d1 - d3;
        const auto value =
            denominator >
                    std::numeric_limits<float>::epsilon()
                ? d1 / denominator
                : 0.0F;
        const auto closest = a + ab * value;
        const auto delta = point - closest;
        return {
            closest,
            glm::dot(delta, delta),
        };
    }

    const auto cp = point - c;
    const auto d5 = glm::dot(ab, cp);
    const auto d6 = glm::dot(ac, cp);
    if (d6 >= 0.0F && d5 <= d6) {
        return {
            c,
            glm::dot(cp, cp),
        };
    }

    const auto vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0F &&
        d2 >= 0.0F &&
        d6 <= 0.0F) {
        const auto denominator = d2 - d6;
        const auto value =
            denominator >
                    std::numeric_limits<float>::epsilon()
                ? d2 / denominator
                : 0.0F;
        const auto closest = a + ac * value;
        const auto delta = point - closest;
        return {
            closest,
            glm::dot(delta, delta),
        };
    }

    const auto va =
        d3 * d6 - d5 * d4;
    if (va <= 0.0F &&
        (d4 - d3) >= 0.0F &&
        (d5 - d6) >= 0.0F) {
        const auto edge = c - b;
        const auto denominator =
            (d4 - d3) + (d5 - d6);
        const auto value =
            denominator >
                    std::numeric_limits<float>::epsilon()
                ? (d4 - d3) / denominator
                : 0.0F;
        const auto closest = b + edge * value;
        const auto delta = point - closest;
        return {
            closest,
            glm::dot(delta, delta),
        };
    }

    const auto denominator =
        va + vb + vc;
    if (std::abs(denominator) <=
        std::numeric_limits<float>::epsilon()) {
        return closest_point_on_segment(
            point,
            a,
            b);
    }
    const auto inverse = 1.0F / denominator;
    const auto v = vb * inverse;
    const auto w = vc * inverse;
    const auto closest =
        a + ab * v + ac * w;
    const auto delta = point - closest;
    return {
        closest,
        glm::dot(delta, delta),
    };
}

[[nodiscard]] auto closest_point_on_swept_blade(
    const glm::vec3& point,
    const ColossalSweepQuery& query) noexcept
    -> ClosestPointResult {
    const auto& previous =
        query.previous_pose;
    const auto& current =
        query.current_pose;
    auto result =
        closest_point_on_triangle(
            point,
            previous.hilt,
            previous.tip,
            current.tip);
    result =
        closest_of(
            result,
            closest_point_on_triangle(
                point,
                previous.hilt,
                current.tip,
                current.hilt));
    result =
        closest_of(
            result,
            closest_point_on_segment(
                point,
                previous.hilt,
                previous.tip));
    result =
        closest_of(
            result,
            closest_point_on_segment(
                point,
                current.hilt,
                current.tip));
    result =
        closest_of(
            result,
            closest_point_on_segment(
                point,
                previous.hilt,
                current.hilt));
    return closest_of(
        result,
        closest_point_on_segment(
            point,
            previous.tip,
            current.tip));
}

[[nodiscard]] auto inside_horizontal_arc(
    const ColossalSweepQuery& query,
    const ColossalSweepCandidate& candidate) noexcept
    -> bool {
    if (!query.enforce_horizontal_arc ||
        query.arc_degrees >= 359.0F) {
        return true;
    }
    auto forward =
        glm::vec3 {
            query.forward.x,
            0.0F,
            query.forward.z,
        };
    auto direction =
        glm::vec3 {
            candidate.center.x -
                query.attack_origin.x,
            0.0F,
            candidate.center.z -
                query.attack_origin.z,
        };
    const auto forward_length_squared =
        glm::dot(forward, forward);
    const auto direction_length_squared =
        glm::dot(direction, direction);
    if (direction_length_squared <=
        std::numeric_limits<float>::epsilon()) {
        return true;
    }
    if (forward_length_squared <=
        std::numeric_limits<float>::epsilon()) {
        return false;
    }
    forward /=
        std::sqrt(forward_length_squared);
    direction /=
        std::sqrt(direction_length_squared);
    const auto half_angle_radians =
        std::clamp(
            query.arc_degrees,
            0.0F,
            360.0F) *
        0.5F *
        0.01745329251994329577F;
    return glm::dot(forward, direction) >=
           std::cos(half_angle_radians);
}

[[nodiscard]] auto hit_precedes(
    const ColossalSweepHit& lhs,
    const ColossalSweepHit& rhs) noexcept -> bool {
    constexpr auto epsilon = 0.000001F;
    if (std::abs(
            lhs.center_distance_squared -
            rhs.center_distance_squared) >
        epsilon) {
        return lhs.center_distance_squared <
               rhs.center_distance_squared;
    }
    if (lhs.hit_priority != rhs.hit_priority) {
        return lhs.hit_priority >
               rhs.hit_priority;
    }
    if (lhs.target_id != rhs.target_id) {
        return lhs.target_id < rhs.target_id;
    }
    return lhs.zone_id < rhs.zone_id;
}

[[nodiscard]] auto valid_query(
    const ColossalSweepQuery& query) noexcept -> bool {
    return query.attack_sequence != 0U &&
           finite_vector(query.previous_pose.hilt) &&
           finite_vector(query.previous_pose.tip) &&
           finite_vector(query.current_pose.hilt) &&
           finite_vector(query.current_pose.tip) &&
           finite_vector(query.attack_origin) &&
           finite_vector(query.forward) &&
           std::isfinite(query.blade_radius) &&
           query.blade_radius >= 0.0F &&
           std::isfinite(query.maximum_range) &&
           query.maximum_range > 0.0F &&
           std::isfinite(query.arc_degrees) &&
           query.arc_degrees >= 0.0F &&
           query.maximum_targets > 0U;
}

} // namespace

void ColossalHitLedger::begin_attack(
    std::uint64_t attack_sequence) noexcept {
    if (attack_sequence_ == attack_sequence) {
        return;
    }
    attack_sequence_ = attack_sequence;
    size_ = 0U;
}

auto ColossalHitLedger::contains(
    ColossalCombatTargetId target_id) const noexcept -> bool {
    return std::find(
               entries_.begin(),
               entries_.begin() +
                   static_cast<std::ptrdiff_t>(size_),
               target_id) !=
           entries_.begin() +
               static_cast<std::ptrdiff_t>(size_);
}

auto ColossalHitLedger::try_register(
    ColossalCombatTargetId target_id) noexcept -> bool {
    if (target_id == 0U ||
        contains(target_id) ||
        full()) {
        return false;
    }
    entries_[size_] = target_id;
    ++size_;
    return true;
}

auto ColossalHitLedger::attack_sequence() const noexcept
    -> std::uint64_t {
    return attack_sequence_;
}

auto ColossalHitLedger::size() const noexcept
    -> std::size_t {
    return size_;
}

auto ColossalHitLedger::full() const noexcept -> bool {
    return size_ >= entries_.size();
}

void ColossalHitLedger::clear() noexcept {
    attack_sequence_ = 0U;
    size_ = 0U;
}

auto resolve_colossal_sweep(
    const ColossalSweepQuery& query,
    std::span<const ColossalSweepCandidate> candidates,
    ColossalHitLedger& ledger,
    const ColossalSweepCallbacks& callbacks) noexcept
    -> ColossalSweepResult {
    ColossalSweepResult result {};
    if (!valid_query(query)) {
        result.error =
            ColossalSweepError::InvalidQuery;
        return result;
    }
    if (candidates.size() >
        kMaximumColossalSweepCandidates) {
        result.error =
            ColossalSweepError::
                CandidateCapacityExceeded;
        return result;
    }
    ledger.begin_attack(query.attack_sequence);

    std::array<
        ColossalSweepHit,
        kMaximumColossalSweepCandidates>
        contacts {};
    auto contact_count = std::size_t {0U};
    const auto maximum_range_squared =
        query.maximum_range *
        query.maximum_range;

    for (const auto& candidate : candidates) {
        if (!candidate.enabled ||
            candidate.target_id == 0U ||
            !finite_vector(candidate.center) ||
            !std::isfinite(candidate.radius) ||
            candidate.radius < 0.0F) {
            continue;
        }
        if (candidate.friendly &&
            !query.can_hit_friendlies) {
            ++result.friendly_ignored_count;
            continue;
        }
        const auto from_origin =
            candidate.center -
            query.attack_origin;
        const auto center_distance_squared =
            glm::dot(from_origin, from_origin);
        const auto extended_range =
            query.maximum_range +
            candidate.radius +
            query.blade_radius;
        if (center_distance_squared >
                maximum_range_squared &&
            center_distance_squared >
                extended_range * extended_range) {
            continue;
        }
        if (!inside_horizontal_arc(
                query,
                candidate)) {
            continue;
        }

        const auto closest =
            closest_point_on_swept_blade(
                candidate.center,
                query);
        const auto contact_radius =
            query.blade_radius +
            candidate.radius;
        if (closest.distance_squared >
            contact_radius * contact_radius) {
            continue;
        }

        ++result.geometric_contact_count;
        auto duplicate_index = contact_count;
        for (std::size_t index = 0U;
             index < contact_count;
             ++index) {
            if (contacts[index].target_id ==
                candidate.target_id) {
                duplicate_index = index;
                break;
            }
        }
        ColossalSweepHit hit {};
        hit.target_id = candidate.target_id;
        hit.zone_id = candidate.zone_id;
        hit.target_center = candidate.center;
        hit.contact_point = closest.point;
        hit.center_distance_squared =
            center_distance_squared;
        hit.blade_distance_squared =
            closest.distance_squared;
        hit.hit_priority =
            candidate.hit_priority;
        if (duplicate_index < contact_count) {
            ++result.duplicate_count;
            const auto& previous =
                contacts[duplicate_index];
            if (hit.hit_priority >
                    previous.hit_priority ||
                (hit.hit_priority ==
                     previous.hit_priority &&
                 hit.blade_distance_squared <
                     previous.blade_distance_squared)) {
                contacts[duplicate_index] = hit;
            }
            continue;
        }
        contacts[contact_count] = hit;
        ++contact_count;
    }

    std::sort(
        contacts.begin(),
        contacts.begin() +
            static_cast<std::ptrdiff_t>(contact_count),
        hit_precedes);
    const auto target_limit =
        std::min<std::size_t>(
            query.maximum_targets,
            result.hits.size());
    for (std::size_t index = 0U;
         index < contact_count;
         ++index) {
        const auto& hit = contacts[index];
        if (ledger.contains(hit.target_id)) {
            ++result.duplicate_count;
            continue;
        }
        if (callbacks.is_occluded != nullptr &&
            callbacks.is_occluded(
                callbacks.user_data,
                ColossalSweepOcclusionRequest {
                    hit.target_id,
                    hit.zone_id,
                    query.attack_origin,
                    hit.target_center,
                })) {
            ++result.occluded_count;
            continue;
        }
        if (result.hit_count >= target_limit) {
            result.target_limit_reached = true;
            break;
        }
        if (!ledger.try_register(hit.target_id)) {
            result.error =
                ColossalSweepError::
                    HitLedgerCapacityExceeded;
            return result;
        }
        result.hits[result.hit_count] = hit;
        ++result.hit_count;
    }
    return result;
}

auto resolve_colossal_shockwave(
    const ColossalShockwaveQuery& query,
    std::span<const ColossalSweepCandidate> candidates,
    ColossalHitLedger& ledger,
    const ColossalSweepCallbacks& callbacks) noexcept
    -> ColossalSweepResult {
    ColossalSweepResult result {};
    if (query.attack_sequence == 0U ||
        !finite_vector(query.origin) ||
        !std::isfinite(query.radius) ||
        query.radius <= 0.0F ||
        query.maximum_targets == 0U) {
        result.error = ColossalSweepError::InvalidQuery;
        return result;
    }
    if (candidates.size() > kMaximumColossalSweepCandidates) {
        result.error =
            ColossalSweepError::CandidateCapacityExceeded;
        return result;
    }
    ledger.begin_attack(query.attack_sequence);

    std::array<
        ColossalSweepHit,
        kMaximumColossalSweepCandidates>
        contacts {};
    auto contact_count = std::size_t {0U};
    for (const auto& candidate : candidates) {
        if (!candidate.enabled ||
            candidate.target_id == 0U ||
            !finite_vector(candidate.center) ||
            !std::isfinite(candidate.radius) ||
            candidate.radius < 0.0F) {
            continue;
        }
        if (candidate.friendly &&
            !query.can_hit_friendlies) {
            ++result.friendly_ignored_count;
            continue;
        }
        const auto delta = candidate.center - query.origin;
        const auto distance_squared = glm::dot(delta, delta);
        const auto contact_radius =
            query.radius + candidate.radius;
        if (!std::isfinite(distance_squared) ||
            distance_squared >
                contact_radius * contact_radius) {
            continue;
        }

        ++result.geometric_contact_count;
        ColossalSweepHit hit {};
        hit.target_id = candidate.target_id;
        hit.zone_id = candidate.zone_id;
        hit.target_center = candidate.center;
        hit.contact_point = candidate.center;
        hit.center_distance_squared = distance_squared;
        hit.blade_distance_squared = distance_squared;
        hit.hit_priority = candidate.hit_priority;

        auto duplicate = contact_count;
        for (std::size_t index = 0U;
             index < contact_count;
             ++index) {
            if (contacts[index].target_id ==
                candidate.target_id) {
                duplicate = index;
                break;
            }
        }
        if (duplicate < contact_count) {
            ++result.duplicate_count;
            const auto& previous = contacts[duplicate];
            if (hit.hit_priority > previous.hit_priority ||
                (hit.hit_priority == previous.hit_priority &&
                 hit.center_distance_squared <
                     previous.center_distance_squared)) {
                contacts[duplicate] = hit;
            }
            continue;
        }
        contacts[contact_count++] = hit;
    }

    std::sort(
        contacts.begin(),
        contacts.begin() +
            static_cast<std::ptrdiff_t>(contact_count),
        hit_precedes);
    const auto target_limit =
        std::min<std::size_t>(
            query.maximum_targets,
            result.hits.size());
    for (std::size_t index = 0U;
         index < contact_count;
         ++index) {
        const auto& hit = contacts[index];
        if (ledger.contains(hit.target_id)) {
            ++result.duplicate_count;
            continue;
        }
        if (callbacks.is_occluded != nullptr &&
            callbacks.is_occluded(
                callbacks.user_data,
                {
                    hit.target_id,
                    hit.zone_id,
                    query.origin,
                    hit.target_center,
                })) {
            ++result.occluded_count;
            continue;
        }
        if (result.hit_count >= target_limit) {
            result.target_limit_reached = true;
            break;
        }
        if (!ledger.try_register(hit.target_id)) {
            result.error =
                ColossalSweepError::HitLedgerCapacityExceeded;
            return result;
        }
        result.hits[result.hit_count++] = hit;
    }
    return result;
}

auto choose_colossal_tunnel_attack(
    ColossalAttackShape requested_shape,
    const ColossalTunnelClearance& clearance,
    float required_horizontal_blocks,
    float required_vertical_blocks,
    float required_forward_blocks) noexcept
    -> ColossalTunnelAttackChoice {
    ColossalTunnelAttackChoice result {};
    result.shape = requested_shape;
    const auto inputs_valid =
        std::isfinite(clearance.left_blocks) &&
        std::isfinite(clearance.right_blocks) &&
        std::isfinite(clearance.overhead_blocks) &&
        std::isfinite(clearance.forward_blocks) &&
        std::isfinite(required_horizontal_blocks) &&
        std::isfinite(required_vertical_blocks) &&
        std::isfinite(required_forward_blocks) &&
        required_horizontal_blocks >= 0.0F &&
        required_vertical_blocks >= 0.0F &&
        required_forward_blocks >= 0.0F;
    if (!inputs_valid) {
        return result;
    }

    const auto forward_clear =
        clearance.forward_blocks >=
        required_forward_blocks;
    const auto is_horizontal =
        requested_shape ==
            ColossalAttackShape::HorizontalArc ||
        requested_shape ==
            ColossalAttackShape::ReverseHorizontalArc;
    if (!is_horizontal) {
        result.attack_has_clearance =
            forward_clear &&
            clearance.overhead_blocks >=
                required_vertical_blocks;
        return result;
    }

    const auto horizontal_clear =
        clearance.left_blocks +
            clearance.right_blocks >=
        required_horizontal_blocks;
    if (horizontal_clear) {
        result.attack_has_clearance =
            forward_clear;
        return result;
    }

    result.shape =
        ColossalAttackShape::VerticalArc;
    result.replaced_with_vertical = true;
    result.attack_has_clearance =
        forward_clear &&
        clearance.overhead_blocks >=
            required_vertical_blocks;
    return result;
}

} // namespace valcraft
