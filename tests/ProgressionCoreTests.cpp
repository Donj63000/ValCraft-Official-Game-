#include "gameplay/PlayerProgression.h"
#include "gameplay/progression/ExperienceAwardService.h"
#include "gameplay/progression/ExperienceRewardPolicy.h"
#include "gameplay/progression/PlayerDerivedStats.h"
#include "gameplay/progression/ProgressionCurve.h"

#include <doctest/doctest.h>

#include <cstdint>
#include <limits>

namespace valcraft {

TEST_CASE("la courbe courante et la courbe v13 restent explicitement distinctes") {
    CHECK(player_experience_for_next_level(0U) == 100ULL);
    CHECK(player_experience_for_next_level(1U) == 100ULL);
    CHECK(player_experience_for_next_level(2U) == 117ULL);
    CHECK(player_experience_for_next_level(3U) == 136ULL);
    CHECK(player_experience_for_next_level(30U) == 1'405ULL);
    CHECK(player_experience_for_next_level(99U) == 11'272ULL);
    CHECK(player_experience_for_next_level(100U) == 0ULL);
    CHECK(player_experience_for_next_level(101U) == 0ULL);
    CHECK(
        player_cumulative_experience_for_level(
            100U) ==
        kPlayerProgressionTotalExperience);
    CHECK(kPlayerProgressionTotalExperience == 406'065ULL);

    CHECK(player_experience_for_next_level_v13(1U) == 100ULL);
    CHECK(player_experience_for_next_level_v13(2U) == 122ULL);
    CHECK(player_experience_for_next_level_v13(3U) == 148ULL);
    CHECK(player_experience_for_next_level_v13(99U) == 21'268ULL);
    CHECK(player_experience_for_next_level_v13(100U) == 0ULL);

    auto v13_total =
        std::uint64_t {0ULL};
    for (auto level =
             kPlayerProgressionMinLevel;
         level <
         kPlayerProgressionMaxLevel;
         ++level) {
        v13_total +=
            player_experience_for_next_level_v13(
                level);
    }
    CHECK(
        v13_total ==
        kPlayerProgressionV13TotalExperience);
}

TEST_CASE("la politique centrale expose toutes les recompenses du plan") {
    CHECK(
        activity_experience(
            ExperienceActivity::FishingCatch) ==
        10ULL);
    CHECK(
        activity_experience(
            ExperienceActivity::ConstructionCompleted) ==
        5ULL);
    CHECK(
        activity_experience(
            ExperienceActivity::Departure) ==
        100ULL);
    CHECK(
        activity_experience(
            ExperienceActivity::OpenSeaReached) ==
        250ULL);
    CHECK(
        activity_experience(
            ExperienceActivity::FirstDelivery) ==
        100ULL);

    CHECK(
        navigation_experience_between(
            0ULL,
            249ULL) ==
        0ULL);
    CHECK(
        navigation_experience_between(
            0ULL,
            250ULL) ==
        100ULL);
    CHECK(
        navigation_experience_between(
            249ULL,
            501ULL) ==
        200ULL);
    CHECK(
        navigation_experience_between(
            500ULL,
            250ULL) ==
        0ULL);

    CHECK(
        creature_threat_experience(
            CreatureThreatTier::NeutralVillager) ==
        0ULL);
    CHECK(
        creature_threat_experience(
            CreatureThreatTier::NeutralAnimal) ==
        2ULL);
    CHECK(
        creature_threat_experience(
            CreatureThreatTier::Hostile1) ==
        15ULL);
    CHECK(
        creature_threat_experience(
            CreatureThreatTier::Hostile2) ==
        30ULL);
    CHECK(
        creature_threat_experience(
            CreatureThreatTier::Hostile3) ==
        55ULL);
    CHECK(
        creature_threat_experience(
            CreatureThreatTier::Hostile4) ==
        95ULL);
    CHECK(
        creature_threat_experience(
            CreatureThreatTier::Hostile5) ==
        160ULL);
    CHECK(
        creature_threat_experience(
            CreatureThreatTier::Hostile6) ==
        240ULL);
    CHECK(
        creature_threat_experience(
            CreatureThreatTier::Hostile3,
            true) ==
        137ULL);

    CHECK(boss_experience(0ULL) == 0ULL);
    CHECK(boss_experience(100ULL) == 500ULL);
    CHECK(boss_experience(500ULL) == 500ULL);
    CHECK(boss_experience(5'000ULL) == 3'000ULL);
}

TEST_CASE("le bonus nocturne reste cible borne et deterministe") {
    ExperienceAwardContext context {
        .reason =
            ExperienceReason::Combat,
        .hostile_target = true,
        .at_surface = false,
        .surface_water_context = true,
        .phase =
            CreaturePhase::Night,
    };
    CHECK(
        apply_experience_modifiers(
            55ULL,
            context) ==
        68ULL);

    context.surface_water_context = false;
    context.at_surface = true;
    CHECK(
        apply_experience_modifiers(
            55ULL,
            context) ==
        55ULL);
    context.surface_water_context = true;
    context.hostile_target = false;
    CHECK(
        apply_experience_modifiers(
            55ULL,
            context) ==
        55ULL);
    context.hostile_target = true;
    context.reason =
        ExperienceReason::Fishing;
    CHECK(
        apply_experience_modifiers(
            55ULL,
            context) ==
        55ULL);
    CHECK(
        multiply_experience_ratio(
            std::numeric_limits<std::uint64_t>::max(),
            5U,
            4U) ==
        std::numeric_limits<std::uint64_t>::max());
}

TEST_CASE("le service attribue les evenements types et deduplique les jalons") {
    ExperienceAwardService service {};

    const auto fishing =
        service.award(
            FishingExperienceEvent {
                .catches = 2U,
            });
    CHECK(fishing.base_experience == 20ULL);
    CHECK(fishing.awarded_experience == 20ULL);
    CHECK(fishing.units_awarded == 2ULL);
    CHECK(fishing.awarded());
    CHECK_FALSE(fishing.milestone_awarded);

    const auto dry_night_combat =
        service.award(
            CombatExperienceEvent {
                .base_experience = 55ULL,
                .hostile_target = true,
                .surface_water_context = false,
                .phase =
                    CreaturePhase::Night,
            });
    CHECK(dry_night_combat.base_experience == 55ULL);
    CHECK(dry_night_combat.awarded_experience == 55ULL);

    const auto maritime_night_combat =
        service.award(
            CombatExperienceEvent {
                .base_experience = 55ULL,
                .hostile_target = true,
                .surface_water_context = true,
                .phase =
                    CreaturePhase::Night,
            });
    CHECK(maritime_night_combat.base_experience == 55ULL);
    CHECK(maritime_night_combat.awarded_experience == 68ULL);

    CHECK(
        service.award(
                   HarvestExperienceEvent {
                       .block_id =
                           to_block_id(
                               BlockType::Stone),
                   })
            .awarded_experience ==
        1ULL);
    CHECK(
        service.award(
                   HarvestExperienceEvent {
                       .block_id =
                           to_block_id(
                               BlockType::DiamondOre),
                       .player_placed = true,
                   })
            .awarded_experience ==
        0ULL);
    CHECK(
        service.award(
                   ConstructionExperienceEvent {
                       .completed_constructions = 2U,
                   })
            .awarded_experience ==
        10ULL);

    const auto before_navigation =
        service.award(
            NavigationExperienceEvent {
                .total_distance_meters = 249ULL,
            });
    CHECK_FALSE(before_navigation.awarded());
    CHECK_FALSE(before_navigation.duplicate);

    const auto first_navigation =
        service.award(
            NavigationExperienceEvent {
                .total_distance_meters = 250ULL,
            });
    CHECK(first_navigation.awarded_experience == 100ULL);
    CHECK(first_navigation.units_awarded == 1ULL);
    CHECK(first_navigation.milestone_awarded);

    const auto duplicate_navigation =
        service.award(
            NavigationExperienceEvent {
                .total_distance_meters = 499ULL,
            });
    CHECK_FALSE(duplicate_navigation.awarded());
    CHECK(duplicate_navigation.duplicate);

    const auto navigation_jump =
        service.award(
            NavigationExperienceEvent {
                .total_distance_meters = 751ULL,
            });
    CHECK(navigation_jump.awarded_experience == 200ULL);
    CHECK(navigation_jump.units_awarded == 2ULL);
    CHECK(
        service.state()
            .navigation_milestones_awarded ==
        3ULL);

    const auto departure =
        service.award(
            DepartureExperienceEvent {});
    CHECK(departure.awarded_experience == 100ULL);
    CHECK(departure.milestone_awarded);
    CHECK(
        service.award(
                   DepartureExperienceEvent {})
            .duplicate);

    const auto open_sea =
        service.award(
            OpenSeaReachedExperienceEvent {});
    CHECK(open_sea.awarded_experience == 250ULL);
    CHECK(open_sea.milestone_awarded);
    CHECK(
        service.award(
                   OpenSeaReachedExperienceEvent {})
            .duplicate);

    const auto first_delivery =
        service.award(
            FirstDeliveryExperienceEvent {
                .milestone_id = 2U,
            });
    CHECK(first_delivery.awarded_experience == 100ULL);
    CHECK(first_delivery.milestone_awarded);
    CHECK(
        service.award(
                   FirstDeliveryExperienceEvent {
                       .milestone_id = 2U,
                   })
            .duplicate);
    CHECK(
        service.award(
                   FirstDeliveryExperienceEvent {
                       .milestone_id = 63U,
                   })
            .awarded_experience ==
        100ULL);
    CHECK(
        (service.state()
             .first_delivery_milestones_mask &
         (std::uint64_t {1ULL} << 63U)) !=
        0ULL);
    CHECK(
        service.award(
                   FirstDeliveryExperienceEvent {
                       .milestone_id = 64U,
                   })
            .rejected);

    const auto saved_state =
        service.state();
    ExperienceAwardService restored {};
    restored.load_state(
        saved_state);
    CHECK(restored.state() == saved_state);
    CHECK(
        restored.award(
                    NavigationExperienceEvent {
                        .total_distance_meters = 751ULL,
                    })
            .duplicate);
    CHECK(
        restored.award(
                    FirstDeliveryExperienceEvent {
                        .milestone_id = 2U,
                    })
            .duplicate);

    restored.load_state(
        {
            .navigation_milestones_awarded = 3ULL,
            .first_delivery_milestones_mask =
                saved_state.first_delivery_milestones_mask,
            .departure_awarded = 255U,
            .open_sea_awarded = 2U,
        });
    CHECK(restored.state().departure_awarded == 1U);
    CHECK(restored.state().open_sea_awarded == 1U);

    const ExperienceAwardEvent typed_event =
        FishingExperienceEvent {
            .catches = 3U,
        };
    CHECK(
        restored.award(
                    typed_event)
            .awarded_experience ==
        30ULL);
}

TEST_CASE("les statistiques derivees et leurs deltas partagent une seule source") {
    const auto level_one =
        player_derived_stats(
            1U);
    CHECK(level_one.base_max_health == doctest::Approx(20.0F));
    CHECK(level_one.attack_damage_multiplier == doctest::Approx(1.0F));
    CHECK(level_one.damage_reduction_percent == doctest::Approx(0.0F));
    CHECK(level_one.apnea_duration_multiplier == doctest::Approx(1.0F));
    CHECK(level_one.apnea_resistance_percent == doctest::Approx(0.0F));
    CHECK(level_one.safe_fall_multiplier == doctest::Approx(1.0F));
    CHECK(level_one.movement_speed_multiplier == doctest::Approx(1.0F));
    CHECK(level_one.mining_speed_multiplier == doctest::Approx(1.0F));

    const auto level_hundred =
        player_derived_stats(
            100U);
    CHECK(level_hundred.base_max_health == doctest::Approx(30.0F));
    CHECK(level_hundred.attack_damage_multiplier == doctest::Approx(1.2475F));
    CHECK(level_hundred.damage_reduction_percent == doctest::Approx(9.9F));
    CHECK(level_hundred.apnea_duration_multiplier == doctest::Approx(1.495F));
    CHECK(level_hundred.safe_fall_multiplier == doctest::Approx(1.2475F));
    CHECK(level_hundred.movement_speed_multiplier == doctest::Approx(1.099F));
    CHECK(level_hundred.mining_speed_multiplier == doctest::Approx(1.198F));

    const auto level_ten_delta =
        player_derived_stats_delta(
            9U,
            10U);
    CHECK(level_ten_delta.base_max_health == doctest::Approx(1.0F));
    CHECK(level_ten_delta.attack_damage_multiplier == doctest::Approx(0.0025F));
    CHECK(level_ten_delta.damage_reduction_percent == doctest::Approx(0.1F));
    CHECK(level_ten_delta.apnea_duration_multiplier == doctest::Approx(0.005F));
    CHECK(level_ten_delta.safe_fall_multiplier == doctest::Approx(0.0025F));
    CHECK(level_ten_delta.movement_speed_multiplier == doctest::Approx(0.001F));
    CHECK(level_ten_delta.mining_speed_multiplier == doctest::Approx(0.002F));
    CHECK(level_ten_delta.skill_points == 1U);
    CHECK(level_ten_delta.attribute_points == 1U);
    CHECK(level_ten_delta.mastery_points == 0U);

    const auto super_vision_delta =
        player_derived_stats_delta(
            29U,
            30U);
    CHECK(super_vision_delta.super_vision_unlocked);
    CHECK_FALSE(super_vision_delta.flight_unlocked);

    const auto flight_delta =
        player_derived_stats_delta(
            99U,
            100U);
    CHECK_FALSE(flight_delta.super_vision_unlocked);
    CHECK(flight_delta.flight_unlocked);
    CHECK(flight_delta.mastery_points == 1U);

    CHECK(
        player_derived_stats_delta(
            30U,
            29U) ==
        PlayerDerivedStatsDelta {});
}

TEST_CASE("les transitions xp utilisent la nouvelle courbe sans perdre de gain") {
    PlayerProgression progression {};
    const auto gain =
        progression.add_experience(
            100ULL +
            117ULL +
            10ULL);

    CHECK(gain.awarded_experience == 227ULL);
    CHECK(gain.levels_gained == 2U);
    CHECK_FALSE(gain.reached_max_level);
    CHECK(progression.level() == 3U);
    CHECK(progression.experience() == 10ULL);
    CHECK(
        progression.derived_stats() ==
        player_derived_stats(
            3U));
    CHECK(
        progression.capabilities() ==
        player_progression_capabilities(
            3U));

    const auto migrated =
        migrate_legacy_player_progression_state(
            {2U, 75ULL});
    CHECK(migrated.level == 2U);
    CHECK(migrated.experience == 59ULL);
}

TEST_CASE("les capacites distinguent deblocage et disponibilite maritime") {
    const auto classic_level_hundred =
        player_progression_capabilities(
            100U,
            PlayerProgressionMode::ClassicAdventure);
    CHECK(
        classic_level_hundred
            .super_vision
            .unlocked);
    CHECK(
        classic_level_hundred
            .super_vision
            .available);
    CHECK(
        classic_level_hundred
            .flight
            .unlocked);
    CHECK(
        classic_level_hundred
            .flight
            .available);
    CHECK_FALSE(
        classic_level_hundred
            .flight_action_reserved_for_fishing);

    const auto sea_level_hundred =
        player_progression_capabilities(
            100U,
            PlayerProgressionMode::SeaAdventure);
    CHECK(
        sea_level_hundred
            .super_vision
            .unlocked);
    CHECK(
        sea_level_hundred
            .super_vision
            .available);
    CHECK(
        sea_level_hundred
            .flight
            .unlocked);
    CHECK_FALSE(
        sea_level_hundred
            .flight
            .available);
    CHECK(
        sea_level_hundred
            .flight_action_reserved_for_fishing);

    const auto sea_level_ninety_nine =
        player_progression_capabilities(
            99U,
            PlayerProgressionMode::SeaAdventure);
    CHECK_FALSE(
        sea_level_ninety_nine
            .flight
            .unlocked);
    CHECK_FALSE(
        sea_level_ninety_nine
            .flight
            .available);
    CHECK(
        sea_level_ninety_nine
            .flight_action_reserved_for_fishing);

    PlayerProgression progression {};
    progression.load_state(
        {100U, 0ULL});
    CHECK(
        progression
            .capabilities(
                PlayerProgressionMode::SeaAdventure)
            .flight
            .unlocked);
    CHECK_FALSE(
        progression
            .capabilities(
                PlayerProgressionMode::SeaAdventure)
            .flight
            .available);
    // Je conserve has_flight_power comme contrat de deblocage permanent.
    CHECK(
        progression
            .has_flight_power());
}

TEST_CASE("la boucle variee atteint le niveau cent en soixante a quatre-vingts heures") {
    ExperienceAwardService service {};
    auto fishing_total =
        std::uint64_t {0ULL};
    auto navigation_total =
        std::uint64_t {0ULL};
    auto combat_total =
        std::uint64_t {0ULL};
    auto harvest_total =
        std::uint64_t {0ULL};
    auto construction_total =
        std::uint64_t {0ULL};
    auto total =
        service
            .award(
                DepartureExperienceEvent {})
            .awarded_experience +
        service
            .award(
                OpenSeaReachedExperienceEvent {})
            .awarded_experience +
        service
            .award(
                FirstDeliveryExperienceEvent {
                    .milestone_id = 0U,
                })
            .awarded_experience +
        service
            .award(
                FirstDeliveryExperienceEvent {
                    .milestone_id = 1U,
                })
            .awarded_experience;
    auto elapsed_hours =
        std::uint32_t {0U};
    auto navigation_distance =
        std::uint64_t {0ULL};

    while (total <
               kPlayerProgressionTotalExperience &&
           elapsed_hours < 200U) {
        ++elapsed_hours;
        const auto fishing =
            service.award(
                FishingExperienceEvent {
                    .catches = 60U,
                });
        navigation_distance +=
            4'248ULL;
        const auto navigation =
            service.award(
                NavigationExperienceEvent {
                    .total_distance_meters =
                        navigation_distance,
                });
        const auto combat =
            service.award(
                CombatExperienceEvent {
                    .base_experience =
                        20ULL * 95ULL,
                    .hostile_target = true,
                    .surface_water_context =
                        false,
                    .phase =
                        CreaturePhase::Day,
                });
        auto harvest =
            ExperienceAwardResult {};
        for (std::uint32_t block = 0U;
             block < 120U;
             ++block) {
            harvest.awarded_experience +=
                service
                    .award(
                        HarvestExperienceEvent {
                            .block_id =
                                to_block_id(
                                    BlockType::Wood),
                        })
                    .awarded_experience;
        }
        const auto construction =
            service.award(
                ConstructionExperienceEvent {
                    .completed_constructions =
                        120U,
                });

        fishing_total +=
            fishing.awarded_experience;
        navigation_total +=
            navigation.awarded_experience;
        combat_total +=
            combat.awarded_experience;
        harvest_total +=
            harvest.awarded_experience;
        construction_total +=
            construction.awarded_experience;
        total +=
            fishing.awarded_experience +
            navigation.awarded_experience +
            combat.awarded_experience +
            harvest.awarded_experience +
            construction.awarded_experience;
    }

    CHECK(elapsed_hours >= 60U);
    CHECK(elapsed_hours <= 80U);
    const auto significant_share =
        kPlayerProgressionTotalExperience *
        8ULL /
        100ULL;
    CHECK(fishing_total >= significant_share);
    CHECK(navigation_total >= significant_share);
    CHECK(combat_total >= significant_share);
    CHECK(harvest_total >= significant_share);
    CHECK(construction_total >= significant_share);

    constexpr auto kMinimumSoloHours =
        60ULL;
    CHECK(
        ExperienceRewardPolicy::
                repeated_activity_experience(
                    ExperienceActivity::
                        FishingCatch,
                    static_cast<std::uint32_t>(
                        60ULL *
                        kMinimumSoloHours)) <
        kPlayerProgressionTotalExperience);
    CHECK(
        ExperienceRewardPolicy::
                navigation_experience_between(
                    0ULL,
                    4'248ULL *
                        kMinimumSoloHours) <
        kPlayerProgressionTotalExperience);
    CHECK(
        ExperienceRewardPolicy::
                repeated_activity_experience(
                    ExperienceActivity::
                        ConstructionCompleted,
                    static_cast<std::uint32_t>(
                        120ULL *
                        kMinimumSoloHours)) <
        kPlayerProgressionTotalExperience);
}

} // namespace valcraft
