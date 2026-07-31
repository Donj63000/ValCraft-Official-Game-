#include "gameplay/combat/WorldProtectionRegistry.h"

#include <algorithm>
#include <cmath>

namespace valcraft {
namespace {

[[nodiscard]] auto valid_bounds(
    const WorldProtectionRegion& region) noexcept -> bool {
    return region.minimum.x <= region.maximum.x &&
           region.minimum.y <= region.maximum.y &&
           region.minimum.z <= region.maximum.z;
}

[[nodiscard]] auto contains_cell(
    const WorldProtectionRegion& region,
    const ColossalWorldCell& cell) noexcept -> bool {
    return cell.x >= region.minimum.x &&
           cell.x <= region.maximum.x &&
           cell.y >= region.minimum.y &&
           cell.y <= region.maximum.y &&
           cell.z >= region.minimum.z &&
           cell.z <= region.maximum.z;
}

[[nodiscard]] auto coordinate_precedes(
    const ColossalWorldCell& lhs,
    const ColossalWorldCell& rhs) noexcept -> bool {
    if (lhs.y != rhs.y) {
        return lhs.y < rhs.y;
    }
    if (lhs.x != rhs.x) {
        return lhs.x < rhs.x;
    }
    return lhs.z < rhs.z;
}

[[nodiscard]] auto candidate_precedes(
    const ColossalFragileCellCandidate& lhs,
    const ColossalFragileCellCandidate& rhs) noexcept -> bool {
    constexpr auto epsilon = 0.000001F;
    if (std::abs(
            lhs.impact_distance_squared -
            rhs.impact_distance_squared) >
        epsilon) {
        return lhs.impact_distance_squared <
               rhs.impact_distance_squared;
    }
    return coordinate_precedes(
        lhs.cell,
        rhs.cell);
}

[[nodiscard]] auto direct_protection_flags(
    const ColossalFragileCellCandidate& candidate) noexcept
    -> WorldProtectionFlag {
    auto flags = WorldProtectionFlag::None;
    if (candidate.player_placed) {
        flags |=
            WorldProtectionFlag::PlayerConstruction;
    }
    if (candidate.ship_surface) {
        flags |= WorldProtectionFlag::Ship;
    }
    if (candidate.important_structure) {
        flags |=
            WorldProtectionFlag::ImportantStructure;
    }
    if (candidate.quest_structure) {
        flags |=
            WorldProtectionFlag::QuestStructure;
    }
    return flags;
}

} // namespace

auto WorldProtectionRegistry::register_region(
    const WorldProtectionRegion& region) noexcept
    -> WorldProtectionRegistrationResult {
    WorldProtectionRegistrationResult result {};
    result.region_count = region_count_;
    if (region.id == 0U) {
        result.error =
            WorldProtectionRegistrationError::InvalidId;
        return result;
    }
    if (!valid_bounds(region)) {
        result.error =
            WorldProtectionRegistrationError::InvalidBounds;
        return result;
    }
    if (region.flags == WorldProtectionFlag::None) {
        result.error =
            WorldProtectionRegistrationError::MissingFlags;
        return result;
    }
    if (this->region(region.id).has_value()) {
        result.error =
            WorldProtectionRegistrationError::DuplicateId;
        return result;
    }
    if (region_count_ >= regions_.size()) {
        result.error =
            WorldProtectionRegistrationError::
                CapacityExceeded;
        return result;
    }
    regions_[region_count_] = region;
    ++region_count_;
    result.registered = true;
    result.region_count = region_count_;
    return result;
}

auto WorldProtectionRegistry::unregister_region(
    std::uint64_t region_id) noexcept -> bool {
    for (std::size_t index = 0U;
         index < region_count_;
         ++index) {
        if (regions_[index].id != region_id) {
            continue;
        }
        for (auto move_index = index + 1U;
             move_index < region_count_;
             ++move_index) {
            regions_[move_index - 1U] =
                regions_[move_index];
        }
        --region_count_;
        regions_[region_count_] = {};
        return true;
    }
    return false;
}

auto WorldProtectionRegistry::protection_at(
    const ColossalWorldCell& cell) const noexcept
    -> WorldProtectionFlag {
    auto flags = WorldProtectionFlag::None;
    for (std::size_t index = 0U;
         index < region_count_;
         ++index) {
        if (contains_cell(
                regions_[index],
                cell)) {
            flags |= regions_[index].flags;
        }
    }
    return flags;
}

auto WorldProtectionRegistry::region(
    std::uint64_t region_id) const noexcept
    -> std::optional<WorldProtectionRegion> {
    for (std::size_t index = 0U;
         index < region_count_;
         ++index) {
        if (regions_[index].id == region_id) {
            return regions_[index];
        }
    }
    return std::nullopt;
}

auto WorldProtectionRegistry::region_count() const noexcept
    -> std::size_t {
    return region_count_;
}

void WorldProtectionRegistry::clear() noexcept {
    regions_ = {};
    region_count_ = 0U;
}

auto build_colossal_fragile_impact_plan(
    const ColossalFragileImpactQuery& query,
    std::span<const ColossalFragileCellCandidate> candidates,
    const WorldProtectionRegistry& protections) noexcept
    -> ColossalFragileImpactPlan {
    ColossalFragileImpactPlan plan {};
    if (query.attack_sequence == 0U ||
        query.maximum_cells == 0U ||
        !query.charged_execution) {
        plan.error =
            ColossalCellRejection::InvalidQuery;
        return plan;
    }
    if (candidates.size() >
        kMaximumColossalCellCandidates) {
        plan.error =
            ColossalCellRejection::
                CandidateCapacityExceeded;
        return plan;
    }

    std::array<
        ColossalFragileCellCandidate,
        kMaximumColossalCellCandidates>
        ordered {};
    auto ordered_count = std::size_t {0U};
    for (const auto& candidate : candidates) {
        if (!std::isfinite(
                candidate.impact_distance_squared) ||
            candidate.impact_distance_squared < 0.0F) {
            ++plan.non_fragile_count;
            continue;
        }
        ordered[ordered_count] = candidate;
        ++ordered_count;
    }
    std::sort(
        ordered.begin(),
        ordered.begin() +
            static_cast<std::ptrdiff_t>(ordered_count),
        candidate_precedes);

    const auto edit_limit =
        std::min<std::size_t>(
            query.maximum_cells,
            plan.edits.size());
    for (std::size_t index = 0U;
         index < ordered_count;
         ++index) {
        const auto& candidate = ordered[index];
        auto duplicate = false;
        for (std::size_t accepted = 0U;
             accepted < plan.edit_count;
             ++accepted) {
            if (plan.edits[accepted].cell ==
                candidate.cell) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) {
            ++plan.duplicate_count;
            continue;
        }
        if (!candidate.loaded) {
            ++plan.unloaded_count;
            continue;
        }
        if (!colossal_cell_is_fragile(
                candidate.material)) {
            ++plan.non_fragile_count;
            continue;
        }

        const auto protection =
            protections.protection_at(
                candidate.cell) |
            direct_protection_flags(candidate);
        if (protection !=
            WorldProtectionFlag::None) {
            ++plan.protected_count;
            continue;
        }
        if (plan.edit_count >= edit_limit) {
            plan.edit_limit_reached = true;
            continue;
        }
        plan.edits[plan.edit_count] = {
            candidate.cell,
            candidate.material,
            candidate.block_token,
        };
        ++plan.edit_count;
    }
    return plan;
}

} // namespace valcraft
