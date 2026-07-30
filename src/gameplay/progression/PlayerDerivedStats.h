#pragma once

#include "gameplay/progression/PlayerAttributes.h"
#include "gameplay/progression/ProgressionCurve.h"

#include <cstdint>

namespace valcraft {

enum class PlayerProgressionMode : std::uint8_t {
    ClassicAdventure = 0,
    SeaAdventure = 1,
};

struct PlayerCapabilityAvailability {
    bool unlocked = false;
    bool available = false;

    auto operator==(const PlayerCapabilityAvailability&) const -> bool =
        default;
};

struct PlayerProgressionCapabilities {
    // Je separe le deblocage permanent de la disponibilite imposee par le
    // mode courant afin de ne jamais retirer un acquis au joueur.
    PlayerCapabilityAvailability super_vision {};
    PlayerCapabilityAvailability flight {};
    // Je rends explicite que la commande de vol appartient a la peche en mer,
    // meme lorsque le vol n'est pas encore debloque.
    bool flight_action_reserved_for_fishing = false;

    auto operator==(const PlayerProgressionCapabilities&) const -> bool =
        default;
};

struct PlayerDerivedStats {
    float base_max_health = 20.0F;
    float attack_damage_multiplier = 1.0F;
    float damage_reduction_percent = 0.0F;
    float apnea_duration_multiplier = 1.0F;
    float apnea_resistance_percent = 0.0F;
    float safe_fall_multiplier = 1.0F;
    float movement_speed_multiplier = 1.0F;
    float mining_speed_multiplier = 1.0F;

    auto operator==(const PlayerDerivedStats&) const -> bool =
        default;
};

struct PlayerDerivedStatsDelta {
    float base_max_health = 0.0F;
    float attack_damage_multiplier = 0.0F;
    float damage_reduction_percent = 0.0F;
    float apnea_duration_multiplier = 0.0F;
    float apnea_resistance_percent = 0.0F;
    float safe_fall_multiplier = 0.0F;
    float movement_speed_multiplier = 0.0F;
    float mining_speed_multiplier = 0.0F;
    std::uint32_t skill_points = 0U;
    std::uint32_t attribute_points = 0U;
    std::uint32_t mastery_points = 0U;
    bool super_vision_unlocked = false;
    bool flight_unlocked = false;

    auto operator==(const PlayerDerivedStatsDelta&) const -> bool =
        default;
};

[[nodiscard]] inline constexpr auto player_progression_capabilities(
    std::uint32_t level,
    PlayerProgressionMode mode =
        PlayerProgressionMode::ClassicAdventure) noexcept
    -> PlayerProgressionCapabilities {
    const auto normalized_level =
        normalize_player_progression_level(
            level);
    const auto super_vision_unlocked =
        normalized_level >=
        kPlayerProgressionSuperVisionLevel;
    const auto flight_unlocked =
        normalized_level >=
        kPlayerProgressionFlightLevel;
    const auto sea_adventure =
        mode ==
        PlayerProgressionMode::SeaAdventure;
    return {
        .super_vision = {
            .unlocked =
                super_vision_unlocked,
            .available =
                super_vision_unlocked,
        },
        .flight = {
            .unlocked =
                flight_unlocked,
            .available =
                flight_unlocked &&
                !sea_adventure,
        },
        .flight_action_reserved_for_fishing =
            sea_adventure,
    };
}

[[nodiscard]] inline constexpr auto player_derived_stats(
    std::uint32_t level) noexcept -> PlayerDerivedStats {
    const auto apnea_duration_multiplier =
        player_level_apnea_duration_multiplier(
            level);
    return {
        .base_max_health =
            player_base_max_health(
                level),
        .attack_damage_multiplier =
            player_level_damage_multiplier(
                level),
        .damage_reduction_percent =
            player_level_damage_reduction(
                level) *
            100.0F,
        .apnea_duration_multiplier =
            apnea_duration_multiplier,
        .apnea_resistance_percent =
            (1.0F -
             1.0F /
                 apnea_duration_multiplier) *
            100.0F,
        .safe_fall_multiplier =
            player_level_safe_fall_multiplier(
                level),
        .movement_speed_multiplier =
            player_level_movement_speed_multiplier(
                level),
        .mining_speed_multiplier =
            player_level_mining_speed_multiplier(
                level),
    };
}

[[nodiscard]] inline constexpr auto player_derived_stats_delta(
    std::uint32_t previous_level,
    std::uint32_t current_level) noexcept -> PlayerDerivedStatsDelta {
    const auto normalized_previous_level =
        normalize_player_progression_level(
            previous_level);
    const auto normalized_current_level =
        normalize_player_progression_level(
            current_level);
    if (normalized_current_level <=
        normalized_previous_level) {
        return {};
    }

    // Je calcule tous les deltas depuis les mêmes fonctions pures que le
    // gameplay afin que les annonces ne puissent plus inventer un bonus global.
    const auto previous =
        player_derived_stats(
            normalized_previous_level);
    const auto current =
        player_derived_stats(
            normalized_current_level);
    const auto previous_capabilities =
        player_progression_capabilities(
            normalized_previous_level);
    const auto current_capabilities =
        player_progression_capabilities(
            normalized_current_level);

    return {
        .base_max_health =
            current.base_max_health -
            previous.base_max_health,
        .attack_damage_multiplier =
            current.attack_damage_multiplier -
            previous.attack_damage_multiplier,
        .damage_reduction_percent =
            current.damage_reduction_percent -
            previous.damage_reduction_percent,
        .apnea_duration_multiplier =
            current.apnea_duration_multiplier -
            previous.apnea_duration_multiplier,
        .apnea_resistance_percent =
            current.apnea_resistance_percent -
            previous.apnea_resistance_percent,
        .safe_fall_multiplier =
            current.safe_fall_multiplier -
            previous.safe_fall_multiplier,
        .movement_speed_multiplier =
            current.movement_speed_multiplier -
            previous.movement_speed_multiplier,
        .mining_speed_multiplier =
            current.mining_speed_multiplier -
            previous.mining_speed_multiplier,
        .skill_points =
            player_skill_points_earned(
                normalized_current_level) -
            player_skill_points_earned(
                normalized_previous_level),
        .attribute_points =
            player_attribute_points_earned(
                normalized_current_level) -
            player_attribute_points_earned(
                normalized_previous_level),
        .mastery_points =
            player_mastery_points_earned(
                normalized_current_level) -
            player_mastery_points_earned(
                normalized_previous_level),
        .super_vision_unlocked =
            !previous_capabilities
                 .super_vision
                 .unlocked &&
            current_capabilities
                .super_vision
                .unlocked,
        .flight_unlocked =
            !previous_capabilities
                 .flight
                 .unlocked &&
            current_capabilities
                .flight
                .unlocked,
    };
}

} // namespace valcraft
