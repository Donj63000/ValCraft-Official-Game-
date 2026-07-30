#pragma once

#include "gameplay/progression/ExperienceRewardPolicy.h"

#include <cstdint>
#include <type_traits>
#include <variant>

namespace valcraft {

struct FishingExperienceEvent {
    std::uint32_t catches = 1U;
};

struct CombatExperienceEvent {
    std::uint64_t base_experience = 0ULL;
    bool hostile_target = false;
    bool surface_water_context = false;
    CreaturePhase phase = CreaturePhase::Day;
};

struct HarvestExperienceEvent {
    BlockId block_id =
        to_block_id(
            BlockType::Air);
    bool player_placed = false;
};

struct ConstructionExperienceEvent {
    std::uint32_t completed_constructions = 1U;
};

struct NavigationExperienceEvent {
    std::uint64_t total_distance_meters = 0ULL;
};

struct DepartureExperienceEvent {};
struct OpenSeaReachedExperienceEvent {};

struct FirstDeliveryExperienceEvent {
    std::uint8_t milestone_id = 0U;
};

using ExperienceAwardEvent =
    std::variant<
        FishingExperienceEvent,
        CombatExperienceEvent,
        HarvestExperienceEvent,
        ConstructionExperienceEvent,
        NavigationExperienceEvent,
        DepartureExperienceEvent,
        OpenSeaReachedExperienceEvent,
        FirstDeliveryExperienceEvent>;

struct MaritimeExperienceAwardState {
    std::uint64_t navigation_milestones_awarded = 0ULL;
    std::uint64_t first_delivery_milestones_mask = 0ULL;
    std::uint8_t departure_awarded = 0U;
    std::uint8_t open_sea_awarded = 0U;

    auto operator==(const MaritimeExperienceAwardState&) const -> bool =
        default;
};

static_assert(
    std::is_standard_layout_v<
        MaritimeExperienceAwardState>);
static_assert(
    std::is_trivially_copyable_v<
        MaritimeExperienceAwardState>);

[[nodiscard]] inline constexpr auto
sanitize_maritime_experience_award_state(
    MaritimeExperienceAwardState state) noexcept
    -> MaritimeExperienceAwardState {
    state.departure_awarded =
        static_cast<std::uint8_t>(
            state.departure_awarded != 0U);
    state.open_sea_awarded =
        static_cast<std::uint8_t>(
            state.open_sea_awarded != 0U);
    return state;
}

struct ExperienceAwardResult {
    std::uint64_t base_experience = 0ULL;
    std::uint64_t awarded_experience = 0ULL;
    std::uint64_t units_awarded = 0ULL;
    bool milestone_awarded = false;
    bool duplicate = false;
    bool rejected = false;

    [[nodiscard]] constexpr auto awarded() const noexcept -> bool {
        return awarded_experience != 0ULL;
    }

    auto operator==(const ExperienceAwardResult&) const -> bool =
        default;
};

class ExperienceAwardService final {
public:
    inline static constexpr std::uint8_t
        kFirstDeliveryMilestoneCount =
            64U;

    void reset() noexcept {
        state_ = {};
    }

    void load_state(
        MaritimeExperienceAwardState state) noexcept {
        state_ =
            sanitize_maritime_experience_award_state(
                state);
    }

    [[nodiscard]] auto state() const noexcept
        -> MaritimeExperienceAwardState {
        return state_;
    }

    [[nodiscard]] auto award(
        const FishingExperienceEvent& event) noexcept
        -> ExperienceAwardResult {
        const auto experience =
            ExperienceRewardPolicy::repeated_activity_experience(
                ExperienceActivity::FishingCatch,
                event.catches);
        return {
            .base_experience =
                experience,
            .awarded_experience =
                experience,
            .units_awarded =
                event.catches,
        };
    }

    [[nodiscard]] auto award(
        const CombatExperienceEvent& event) noexcept
        -> ExperienceAwardResult {
        return {
            .base_experience =
                event.base_experience,
            .awarded_experience =
                ExperienceRewardPolicy::combat_experience(
                    event.base_experience,
                    event.hostile_target,
                    event.surface_water_context,
                    event.phase),
            .units_awarded =
                event.base_experience == 0ULL
                    ? 0ULL
                    : 1ULL,
        };
    }

    [[nodiscard]] auto award(
        const HarvestExperienceEvent& event) noexcept
        -> ExperienceAwardResult {
        const auto experience =
            ExperienceRewardPolicy::harvest_experience(
                event.block_id,
                event.player_placed);
        return {
            .base_experience =
                experience,
            .awarded_experience =
                experience,
            .units_awarded =
                experience == 0ULL
                    ? 0ULL
                    : 1ULL,
        };
    }

    [[nodiscard]] auto award(
        const ConstructionExperienceEvent& event) noexcept
        -> ExperienceAwardResult {
        const auto experience =
            ExperienceRewardPolicy::repeated_activity_experience(
                ExperienceActivity::ConstructionCompleted,
                event.completed_constructions);
        return {
            .base_experience =
                experience,
            .awarded_experience =
                experience,
            .units_awarded =
                event.completed_constructions,
        };
    }

    [[nodiscard]] auto award(
        const NavigationExperienceEvent& event) noexcept
        -> ExperienceAwardResult {
        const auto current_milestone =
            ExperienceRewardPolicy::navigation_milestone(
                event.total_distance_meters);
        if (current_milestone == 0ULL) {
            return {};
        }
        if (current_milestone <=
            state_.navigation_milestones_awarded) {
            return {
                .duplicate = true,
            };
        }

        const auto milestones_awarded =
            current_milestone -
            state_.navigation_milestones_awarded;
        state_.navigation_milestones_awarded =
            current_milestone;
        const auto experience =
            milestones_awarded *
            ExperienceRewardPolicy::kNavigationExperience;
        return {
            .base_experience =
                experience,
            .awarded_experience =
                experience,
            .units_awarded =
                milestones_awarded,
            .milestone_awarded = true,
        };
    }

    [[nodiscard]] auto award(
        const DepartureExperienceEvent&) noexcept
        -> ExperienceAwardResult {
        if (state_.departure_awarded != 0U) {
            return {
                .duplicate = true,
            };
        }

        state_.departure_awarded = 1U;
        const auto experience =
            ExperienceRewardPolicy::activity_experience(
                ExperienceActivity::Departure);
        return {
            .base_experience =
                experience,
            .awarded_experience =
                experience,
            .units_awarded = 1ULL,
            .milestone_awarded = true,
        };
    }

    [[nodiscard]] auto award(
        const OpenSeaReachedExperienceEvent&) noexcept
        -> ExperienceAwardResult {
        if (state_.open_sea_awarded != 0U) {
            return {
                .duplicate = true,
            };
        }

        state_.open_sea_awarded = 1U;
        const auto experience =
            ExperienceRewardPolicy::activity_experience(
                ExperienceActivity::OpenSeaReached);
        return {
            .base_experience =
                experience,
            .awarded_experience =
                experience,
            .units_awarded = 1ULL,
            .milestone_awarded = true,
        };
    }

    [[nodiscard]] auto award(
        const FirstDeliveryExperienceEvent& event) noexcept
        -> ExperienceAwardResult {
        if (event.milestone_id >=
            kFirstDeliveryMilestoneCount) {
            return {
                .rejected = true,
            };
        }

        const auto milestone_bit =
            std::uint64_t {1ULL} <<
            event.milestone_id;
        if ((state_.first_delivery_milestones_mask &
             milestone_bit) != 0ULL) {
            return {
                .duplicate = true,
            };
        }

        state_.first_delivery_milestones_mask |=
            milestone_bit;
        const auto experience =
            ExperienceRewardPolicy::activity_experience(
                ExperienceActivity::FirstDelivery);
        return {
            .base_experience =
                experience,
            .awarded_experience =
                experience,
            .units_awarded = 1ULL,
            .milestone_awarded = true,
        };
    }

    [[nodiscard]] auto award(
        const ExperienceAwardEvent& event) noexcept
        -> ExperienceAwardResult {
        return std::visit(
            [this](const auto& typed_event) noexcept {
                return award(
                    typed_event);
            },
            event);
    }

private:
    MaritimeExperienceAwardState state_ {};
};

} // namespace valcraft
