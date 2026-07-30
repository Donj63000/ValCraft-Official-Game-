#pragma once

#include "creatures/CreatureTypes.h"
#include "world/Block.h"

#include <algorithm>
#include <cstdint>
#include <limits>

namespace valcraft {

enum class ExperienceActivity : std::uint8_t {
    FishingCatch = 0,
    ConstructionCompleted = 1,
    Departure = 2,
    OpenSeaReached = 3,
    FirstDelivery = 4,
};

enum class CreatureThreatTier : std::uint8_t {
    NeutralVillager = 0,
    NeutralAnimal = 1,
    Hostile1 = 2,
    Hostile2 = 3,
    Hostile3 = 4,
    Hostile4 = 5,
    Hostile5 = 6,
    Hostile6 = 7,
};

class ExperienceRewardPolicy final {
public:
    inline static constexpr std::uint64_t kFishingCatchExperience =
        10ULL;
    inline static constexpr std::uint64_t kConstructionCompletedExperience =
        5ULL;
    inline static constexpr std::uint64_t kDepartureExperience =
        100ULL;
    inline static constexpr std::uint64_t kOpenSeaReachedExperience =
        250ULL;
    inline static constexpr std::uint64_t kFirstDeliveryExperience =
        100ULL;
    inline static constexpr std::uint64_t kNavigationExperience =
        100ULL;
    inline static constexpr std::uint64_t
        kNavigationExperienceDistanceMeters =
            250ULL;

    [[nodiscard]] static constexpr auto activity_experience(
        ExperienceActivity activity) noexcept -> std::uint64_t {
        switch (activity) {
        case ExperienceActivity::FishingCatch:
            return kFishingCatchExperience;
        case ExperienceActivity::ConstructionCompleted:
            return kConstructionCompletedExperience;
        case ExperienceActivity::Departure:
            return kDepartureExperience;
        case ExperienceActivity::OpenSeaReached:
            return kOpenSeaReachedExperience;
        case ExperienceActivity::FirstDelivery:
            return kFirstDeliveryExperience;
        default:
            return 0ULL;
        }
    }

    [[nodiscard]] static constexpr auto navigation_milestone(
        std::uint64_t distance_meters) noexcept -> std::uint64_t {
        return distance_meters /
               kNavigationExperienceDistanceMeters;
    }

    [[nodiscard]] static constexpr auto navigation_experience_between(
        std::uint64_t previous_distance_meters,
        std::uint64_t current_distance_meters) noexcept -> std::uint64_t {
        if (current_distance_meters <=
            previous_distance_meters) {
            return 0ULL;
        }

        const auto previous_milestone =
            navigation_milestone(
                previous_distance_meters);
        const auto current_milestone =
            navigation_milestone(
                current_distance_meters);
        return (
                   current_milestone -
                   previous_milestone) *
               kNavigationExperience;
    }

    [[nodiscard]] static constexpr auto multiply_ratio(
        std::uint64_t base_experience,
        std::uint32_t numerator,
        std::uint32_t denominator) noexcept -> std::uint64_t {
        if (base_experience == 0ULL ||
            numerator == 0U ||
            denominator == 0U) {
            return 0ULL;
        }

        const auto denominator64 =
            static_cast<std::uint64_t>(
                denominator);
        const auto numerator64 =
            static_cast<std::uint64_t>(
                numerator);
        const auto quotient =
            base_experience /
            denominator64;
        const auto remainder =
            base_experience %
            denominator64;
        if (quotient >
            std::numeric_limits<std::uint64_t>::max() /
                numerator64) {
            return std::numeric_limits<std::uint64_t>::max();
        }

        const auto whole =
            quotient *
            numerator64;
        if (remainder != 0ULL &&
            numerator64 >
                std::numeric_limits<std::uint64_t>::max() /
                    remainder) {
            return std::numeric_limits<std::uint64_t>::max();
        }

        const auto fraction =
            (remainder *
             numerator64) /
            denominator64;
        if (whole >
            std::numeric_limits<std::uint64_t>::max() -
                fraction) {
            return std::numeric_limits<std::uint64_t>::max();
        }
        return whole + fraction;
    }

    [[nodiscard]] static constexpr auto repeated_activity_experience(
        ExperienceActivity activity,
        std::uint32_t count) noexcept -> std::uint64_t {
        return multiply_ratio(
            activity_experience(
                activity),
            count,
            1U);
    }

    [[nodiscard]] static constexpr auto harvest_experience(
        BlockId block_id,
        bool player_placed = false) noexcept -> std::uint64_t {
        if (player_placed) {
            return 0ULL;
        }

        const auto item_id =
            block_item_id(
                block_id);
        switch (static_cast<BlockType>(item_id)) {
        case BlockType::Wood:
        case BlockType::PineWood:
            return 4ULL;
        case BlockType::CoalOre:
            return 8ULL;
        case BlockType::IronOre:
            return 14ULL;
        case BlockType::GoldOre:
            return 22ULL;
        case BlockType::DiamondOre:
            return 36ULL;
        case BlockType::MetallicAlloyOre:
            return 50ULL;
        case BlockType::Leaves:
        case BlockType::PineLeaves:
        case BlockType::TallGrass:
        case BlockType::RedFlower:
        case BlockType::YellowFlower:
        case BlockType::DeadShrub:
        case BlockType::Cactus:
        case BlockType::Air:
        case BlockType::Pastron:
        case BlockType::RoundShield:
        case BlockType::Sword:
        case BlockType::Spear:
        case BlockType::Shoes:
        case BlockType::Pants:
        case BlockType::Pickaxe:
        case BlockType::Axe:
        case BlockType::Shovel:
            return 0ULL;
        default:
            return is_block_breakable(
                       item_id)
                       ? 1ULL
                       : 0ULL;
        }
    }

    [[nodiscard]] static constexpr auto creature_profile_experience(
        ThreatRank threat_rank,
        EntityWeight weight,
        Faction faction,
        bool ordinary_neutral_wildlife = false,
        std::uint32_t explicit_boss_reward = 0U) noexcept -> std::uint64_t {
        return static_cast<std::uint64_t>(
            deterministic_experience_reward(
                threat_rank,
                weight,
                faction,
                ordinary_neutral_wildlife,
                explicit_boss_reward)
                .experience_points);
    }

    [[nodiscard]] static constexpr auto creature_threat_experience(
        CreatureThreatTier threat,
        bool elite = false) noexcept -> std::uint64_t {
        const auto base =
            [threat]() constexpr noexcept -> std::uint64_t {
            switch (threat) {
            case CreatureThreatTier::NeutralVillager:
                return creature_profile_experience(
                    ThreatRank::Zero,
                    EntityWeight::Normal,
                    Faction::Neutral);
            case CreatureThreatTier::NeutralAnimal:
                return creature_profile_experience(
                    ThreatRank::Zero,
                    EntityWeight::Light,
                    Faction::Neutral,
                    true);
            case CreatureThreatTier::Hostile1:
                return creature_profile_experience(
                    ThreatRank::One,
                    EntityWeight::Light,
                    Faction::Hostile);
            case CreatureThreatTier::Hostile2:
                return creature_profile_experience(
                    ThreatRank::Two,
                    EntityWeight::Normal,
                    Faction::Hostile);
            case CreatureThreatTier::Hostile3:
                return creature_profile_experience(
                    ThreatRank::Three,
                    EntityWeight::Normal,
                    Faction::Hostile);
            case CreatureThreatTier::Hostile4:
                return creature_profile_experience(
                    ThreatRank::Four,
                    EntityWeight::Heavy,
                    Faction::Hostile);
            case CreatureThreatTier::Hostile5:
                return creature_profile_experience(
                    ThreatRank::Five,
                    EntityWeight::Heavy,
                    Faction::Hostile);
            case CreatureThreatTier::Hostile6:
                return creature_profile_experience(
                    ThreatRank::Six,
                    EntityWeight::Heavy,
                    Faction::Hostile);
            default:
                return 0ULL;
            }
        }();
        return elite
                   ? multiply_ratio(
                         base,
                         5U,
                         2U)
                   : base;
    }

    [[nodiscard]] static constexpr auto boss_experience(
        std::uint64_t explicit_reward) noexcept -> std::uint64_t {
        if (explicit_reward == 0ULL) {
            return 0ULL;
        }

        // Je borne la valeur avant la conversion afin que -Wconversion ne
        // puisse masquer ni troncature ni différence entre compilateurs.
        const auto bounded_reward =
            std::clamp<std::uint64_t>(
                explicit_reward,
                500ULL,
                3'000ULL);
        return creature_profile_experience(
            ThreatRank::Zero,
            EntityWeight::Boss,
            Faction::Hostile,
            false,
            static_cast<std::uint32_t>(
                bounded_reward));
    }

    [[nodiscard]] static constexpr auto combat_experience(
        std::uint64_t base_experience,
        bool hostile_target,
        bool at_surface,
        CreaturePhase phase) noexcept -> std::uint64_t {
        const auto night_hostile_surface_combat =
            hostile_target &&
            at_surface &&
            phase == CreaturePhase::Night;
        return night_hostile_surface_combat
                   ? multiply_ratio(
                         base_experience,
                         5U,
                         4U)
                   : base_experience;
    }
};

// Je conserve ces façades pour les appels historiques tout en faisant de la
// classe ci-dessus l'unique propriétaire des valeurs et des règles.
[[nodiscard]] inline constexpr auto activity_experience(
    ExperienceActivity activity) noexcept -> std::uint64_t {
    return ExperienceRewardPolicy::activity_experience(
        activity);
}

[[nodiscard]] inline constexpr auto navigation_experience_milestone(
    std::uint64_t distance_meters) noexcept -> std::uint64_t {
    return ExperienceRewardPolicy::navigation_milestone(
        distance_meters);
}

[[nodiscard]] inline constexpr auto navigation_experience_between(
    std::uint64_t previous_distance_meters,
    std::uint64_t current_distance_meters) noexcept -> std::uint64_t {
    return ExperienceRewardPolicy::navigation_experience_between(
        previous_distance_meters,
        current_distance_meters);
}

[[nodiscard]] inline constexpr auto multiply_experience_ratio(
    std::uint64_t base_experience,
    std::uint32_t numerator,
    std::uint32_t denominator) noexcept -> std::uint64_t {
    return ExperienceRewardPolicy::multiply_ratio(
        base_experience,
        numerator,
        denominator);
}

[[nodiscard]] inline constexpr auto creature_profile_experience(
    ThreatRank threat_rank,
    EntityWeight weight,
    Faction faction,
    bool ordinary_neutral_wildlife = false,
    std::uint32_t explicit_boss_reward = 0U) noexcept -> std::uint64_t {
    return ExperienceRewardPolicy::creature_profile_experience(
        threat_rank,
        weight,
        faction,
        ordinary_neutral_wildlife,
        explicit_boss_reward);
}

[[nodiscard]] inline constexpr auto creature_threat_experience(
    CreatureThreatTier threat,
    bool elite = false) noexcept -> std::uint64_t {
    return ExperienceRewardPolicy::creature_threat_experience(
        threat,
        elite);
}

[[nodiscard]] inline constexpr auto boss_experience(
    std::uint64_t explicit_reward) noexcept -> std::uint64_t {
    return ExperienceRewardPolicy::boss_experience(
        explicit_reward);
}

} // namespace valcraft
