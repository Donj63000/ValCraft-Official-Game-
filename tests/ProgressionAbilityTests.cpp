#include "gameplay/progression/AbilityCatalog.h"
#include "gameplay/progression/AbilitySystem.h"
#include "gameplay/progression/CommanderAbilityEffects.h"
#include "gameplay/progression/KnightAdvancedAbilitySystem.h"
#include "gameplay/progression/NinjaAbilityEffects.h"
#include "gameplay/progression/PlayerBuildState.h"
#include "gameplay/progression/ProgressionCurve.h"
#include "world/Block.h"

#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <ostream>

namespace valcraft {

namespace {

struct CastProbe {
    bool validation_result = true;
    bool commit_result = true;
    std::uint32_t validation_calls = 0U;
    std::uint32_t commit_calls = 0U;
    AbilityCastResolution last_resolution {};
};

[[nodiscard]] auto validate_cast(
    void* user_data,
    const AbilityCastRequest&,
    const AbilityCastResolution& resolution) noexcept -> bool {
    auto& probe =
        *static_cast<CastProbe*>(
            user_data);
    ++probe.validation_calls;
    probe.last_resolution = resolution;
    return probe.validation_result;
}

[[nodiscard]] auto commit_cast(
    void* user_data,
    const AbilityCastRequest&,
    const AbilityCastResolution& resolution) noexcept -> bool {
    auto& probe =
        *static_cast<CastProbe*>(
            user_data);
    ++probe.commit_calls;
    probe.last_resolution = resolution;
    return probe.commit_result;
}

[[nodiscard]] auto callbacks_for(
    CastProbe& probe) noexcept -> AbilityCastCallbacks {
    return {
        &probe,
        &validate_cast,
        &commit_cast,
    };
}

[[nodiscard]] auto make_castable_state(
    AbilityId id,
    std::uint8_t rank,
    bool mastery = false) noexcept -> PlayerBuildState {
    PlayerBuildState state {};
    const auto index =
        ability_index(id);
    state.ability_ranks[index] = rank;
    state.ability_masteries[index] =
        mastery ? 1U : 0U;
    state.equipped_abilities[0] = id;
    state.charges[index] = 1U;
    return state;
}

[[nodiscard]] auto targeted_request(
    AbilityId id,
    float distance) noexcept -> AbilityCastRequest {
    AbilityCastRequest request {};
    request.id = id;
    request.target_valid = true;
    request.ground_target_valid = true;
    request.target_distance_meters = distance;
    return request;
}

} // namespace

TEST_CASE("la courbe de progression conserve ses seuils et ses trois budgets") {
    CHECK(player_experience_for_next_level(1U) == 100ULL);
    CHECK(player_experience_for_next_level(2U) == 117ULL);
    CHECK(player_experience_for_next_level(99U) == 11272ULL);
    CHECK(player_experience_for_next_level(100U) == 0ULL);
    CHECK(player_experience_for_next_level(0U) == 100ULL);
    CHECK(player_experience_for_next_level(101U) == 0ULL);

    auto cumulative = std::uint64_t {0ULL};
    for (std::uint32_t level = 1U;
         level < 100U;
         ++level) {
        CHECK(
            player_cumulative_experience_for_level(level) ==
            cumulative);
        cumulative +=
            player_experience_for_next_level(level);
    }
    CHECK(
        player_cumulative_experience_for_level(100U) ==
        cumulative);

    CHECK(player_skill_points_earned(1U) == 1U);
    CHECK(player_skill_points_earned(100U) == 100U);
    CHECK(player_attribute_points_earned(4U) == 0U);
    CHECK(player_attribute_points_earned(5U) == 1U);
    CHECK(player_attribute_points_earned(100U) == 20U);
    CHECK(player_mastery_points_earned(59U) == 0U);
    CHECK(player_mastery_points_earned(60U) == 1U);
    CHECK(player_mastery_points_earned(100U) == 5U);
}

TEST_CASE("les identifiants des quarante capacites restent stables") {
    CHECK(static_cast<std::uint8_t>(
              AbilityId::KnightVanguardStrike) == 0U);
    CHECK(static_cast<std::uint8_t>(
              AbilityId::KnightTitanJudgment) == 9U);
    CHECK(static_cast<std::uint8_t>(
              AbilityId::NinjaWindAcceleration) == 10U);
    CHECK(static_cast<std::uint8_t>(
              AbilityId::NinjaThousandLightningStorm) == 19U);
    CHECK(static_cast<std::uint8_t>(
              AbilityId::CommanderFootman) == 20U);
    CHECK(static_cast<std::uint8_t>(
              AbilityId::CommanderGrandArmy) == 29U);
    CHECK(static_cast<std::uint8_t>(
              AbilityId::BuilderConstructionPlan) == 30U);
    CHECK(static_cast<std::uint8_t>(
              AbilityId::BuilderAbsoluteArchitect) == 39U);
    CHECK(static_cast<std::uint8_t>(
              AbilityId::Count) == 40U);
    CHECK(static_cast<std::uint8_t>(
              AbilityId::None) == 0xFFU);
}

TEST_CASE("le catalogue des quarante capacités garde un ordre et des coûts stables") {
    constexpr std::array<std::uint8_t, 10U> tier_levels {
        1U,
        5U,
        10U,
        15U,
        20U,
        25U,
        30U,
        35U,
        40U,
        50U,
    };
    constexpr std::array<std::uint8_t, 10U> path_points {
        0U,
        3U,
        6U,
        9U,
        12U,
        15U,
        18U,
        21U,
        24U,
        30U,
    };
    constexpr std::array<
        std::array<float, 10U>,
        kAbilityPathCount> energy_costs {{
        {{12.0F, 20.0F, 18.0F, 25.0F, 15.0F, 35.0F, 0.0F, 45.0F, 55.0F, 80.0F}},
        {{12.0F, 20.0F, 15.0F, 22.0F, 20.0F, 25.0F, 0.0F, 45.0F, 30.0F, 80.0F}},
        {{25.0F, 10.0F, 30.0F, 30.0F, 25.0F, 40.0F, 40.0F, 55.0F, 60.0F, 80.0F}},
        {{8.0F, 18.0F, 15.0F, 20.0F, 25.0F, 35.0F, 40.0F, 45.0F, 50.0F, 80.0F}},
    }};
    constexpr std::array<
        std::array<
            std::array<float, kAbilityRankCount>,
            10U>,
        kAbilityPathCount> cooldowns {{
        {{
            {{2.5F, 2.3F, 2.1F}},
            {{14.0F, 13.0F, 12.0F}},
            {{9.0F, 8.5F, 8.0F}},
            {{20.0F, 18.0F, 16.0F}},
            {{12.0F, 10.0F, 8.0F}},
            {{20.0F, 18.0F, 16.0F}},
            {{0.0F, 0.0F, 0.0F}},
            {{32.0F, 29.0F, 26.0F}},
            {{48.0F, 44.0F, 40.0F}},
            {{100.0F, 90.0F, 80.0F}},
        }},
        {{
            {{10.0F, 9.0F, 8.0F}},
            {{15.0F, 13.0F, 11.0F}},
            {{8.0F, 7.0F, 6.0F}},
            {{11.0F, 9.5F, 8.0F}},
            {{8.0F, 7.0F, 6.0F}},
            {{22.0F, 19.0F, 16.0F}},
            {{0.0F, 0.0F, 0.0F}},
            {{32.0F, 28.0F, 24.0F}},
            {{36.0F, 32.0F, 28.0F}},
            {{100.0F, 90.0F, 80.0F}},
        }},
        {{
            {{20.0F, 18.0F, 16.0F}},
            {{10.0F, 8.0F, 6.0F}},
            {{24.0F, 22.0F, 20.0F}},
            {{28.0F, 24.0F, 20.0F}},
            {{20.0F, 18.0F, 16.0F}},
            {{35.0F, 30.0F, 25.0F}},
            {{24.0F, 21.0F, 18.0F}},
            {{50.0F, 45.0F, 40.0F}},
            {{65.0F, 58.0F, 50.0F}},
            {{120.0F, 105.0F, 90.0F}},
        }},
        {{
            {{1.5F, 1.2F, 0.9F}},
            {{10.0F, 8.0F, 6.0F}},
            {{8.0F, 7.0F, 6.0F}},
            {{12.0F, 10.0F, 8.0F}},
            {{14.0F, 12.0F, 10.0F}},
            {{30.0F, 26.0F, 22.0F}},
            {{12.0F, 10.0F, 8.0F}},
            {{40.0F, 35.0F, 30.0F}},
            {{45.0F, 40.0F, 35.0F}},
            {{120.0F, 105.0F, 90.0F}},
        }},
    }};

    const auto catalog =
        ability_catalog();
    REQUIRE(catalog.size() == 40U);
    auto implemented_count = std::size_t {0U};
    for (std::size_t index = 0U;
         index < catalog.size();
         ++index) {
        const auto& ability =
            catalog[index];
        const auto path_index =
            index / 10U;
        const auto tier_index =
            index % 10U;
        CHECK(ability.id == ability_id_from_index(index));
        CHECK(ability_index(ability.id) == index);
        CHECK(
            ability.path ==
            static_cast<AbilityPath>(
                static_cast<std::uint8_t>(path_index)));
        CHECK(ability.tier == tier_index + 1U);
        CHECK(ability.required_level == tier_levels[tier_index]);
        CHECK(ability.required_path_points == path_points[tier_index]);
        CHECK_FALSE(ability.stable_name.empty());
        CHECK(ability.visual_id == ability.stable_name);
        CHECK(ability.sfx_id == ability.stable_name);
        CHECK(
            ability.prerequisite ==
            (tier_index == 0U
                 ? AbilityId::None
                 : ability_id_from_index(index - 1U)));
        const auto expected_category =
            tier_index == 9U
                ? AbilityCategory::Ultimate
                : (index == 6U || index == 16U
                       ? AbilityCategory::Passive
                       : (index == 10U || index == 30U
                              ? AbilityCategory::Utility
                              : AbilityCategory::Active));
        CHECK(ability.category == expected_category);
        implemented_count +=
            ability.implemented ? 1U : 0U;

        for (std::size_t rank_index = 0U;
             rank_index < kAbilityRankCount;
             ++rank_index) {
            const auto& rank =
                ability.ranks[rank_index];
            const auto expected_level =
                tier_index == 9U
                    ? std::array<std::uint8_t, 3U> {50U, 70U, 90U}[rank_index]
                    : static_cast<std::uint8_t>(
                          tier_levels[tier_index] +
                          std::array<std::uint8_t, 3U> {0U, 2U, 5U}[rank_index]);
            const auto expected_cost =
                static_cast<std::uint8_t>(
                    (tier_index == 9U ? 3U : 1U) +
                    rank_index);
            CHECK(rank.required_level == expected_level);
            CHECK(rank.skill_point_cost == expected_cost);
            CHECK(
                rank.energy_cost ==
                doctest::Approx(
                    energy_costs[path_index][tier_index]));
            CHECK(
                rank.cooldown_seconds ==
                doctest::Approx(
                    cooldowns[path_index][tier_index][rank_index]));
        }
    }
    CHECK(implemented_count == 5U);
    CHECK(catalog[6U].category == AbilityCategory::Passive);
    CHECK(catalog[16U].category == AbilityCategory::Passive);
    CHECK(catalog[10U].category == AbilityCategory::Utility);
    CHECK(catalog[30U].category == AbilityCategory::Utility);
    for (const auto ultimate_index : {9U, 19U, 29U, 39U}) {
        CHECK(catalog[ultimate_index].category == AbilityCategory::Ultimate);
    }
}

TEST_CASE("les cinq capacités jouables exposent exactement leurs données de gameplay") {
    const auto* vanguard =
        ability_definition(
            AbilityId::KnightVanguardStrike);
    const auto* iron_guard =
        ability_definition(
            AbilityId::KnightIronGuard);
    const auto* wind =
        ability_definition(
            AbilityId::NinjaWindAcceleration);
    const auto* footman =
        ability_definition(
            AbilityId::CommanderFootman);
    const auto* plan =
        ability_definition(
            AbilityId::BuilderConstructionPlan);
    REQUIRE(vanguard != nullptr);
    REQUIRE(iron_guard != nullptr);
    REQUIRE(wind != nullptr);
    REQUIRE(footman != nullptr);
    REQUIRE(plan != nullptr);

    CHECK(vanguard->implemented);
    CHECK(vanguard->targeting == AbilityTargeting::MeleeCone);
    CHECK(ability_tags_contain(vanguard->tags, AbilityTag::Offensive));
    CHECK(ability_tags_contain(vanguard->tags, AbilityTag::Melee));
    CHECK(vanguard->ranks[2U].range_meters == doctest::Approx(3.0F));
    CHECK(vanguard->ranks[2U].values[0U] == doctest::Approx(1.8F));
    CHECK(vanguard->ranks[2U].values[1U] == doctest::Approx(0.35F));
    CHECK(vanguard->ranks[2U].values[2U] == doctest::Approx(2.0F));
    CHECK(vanguard->ranks[2U].values[3U] == doctest::Approx(3.0F));

    CHECK(iron_guard->implemented);
    CHECK(iron_guard->targeting == AbilityTargeting::Self);
    CHECK(ability_tags_contain(
        iron_guard->tags,
        AbilityTag::Defensive));
    CHECK(ability_tags_contain(
        iron_guard->tags,
        AbilityTag::Buff));
    CHECK(iron_guard->ranks[0U].duration_seconds == doctest::Approx(3.0F));
    CHECK(iron_guard->ranks[1U].duration_seconds == doctest::Approx(3.5F));
    CHECK(iron_guard->ranks[2U].duration_seconds == doctest::Approx(4.0F));
    CHECK(iron_guard->ranks[0U].values[0U] == doctest::Approx(0.25F));
    CHECK(iron_guard->ranks[1U].values[0U] == doctest::Approx(0.30F));
    CHECK(iron_guard->ranks[2U].values[0U] == doctest::Approx(0.35F));
    CHECK(iron_guard->ranks[2U].values[1U] == doctest::Approx(0.90F));
    CHECK(iron_guard->ranks[2U].values[2U] == doctest::Approx(0.15F));
    CHECK(iron_guard->ranks[2U].values[3U] == doctest::Approx(4.0F));
    CHECK(iron_guard->ranks[2U].values[4U] == doctest::Approx(2.0F));
    CHECK(iron_guard->ranks[2U].values[5U] == doctest::Approx(5.0F));

    CHECK(wind->implemented);
    CHECK(wind->targeting == AbilityTargeting::Self);
    CHECK(wind->ranks[0U].duration_seconds == doctest::Approx(4.0F));
    CHECK(wind->ranks[1U].duration_seconds == doctest::Approx(5.0F));
    CHECK(wind->ranks[2U].duration_seconds == doctest::Approx(6.0F));
    CHECK(wind->ranks[2U].values[0U] == doctest::Approx(0.30F));
    CHECK(wind->ranks[2U].values[1U] == doctest::Approx(0.25F));
    CHECK(wind->ranks[2U].values[2U] == doctest::Approx(4.0F));
    CHECK(wind->ranks[2U].values[3U] == doctest::Approx(0.25F));

    CHECK(footman->implemented);
    CHECK(footman->targeting == AbilityTargeting::GroundPoint);
    CHECK(footman->ranks[2U].range_meters == doctest::Approx(8.0F));
    CHECK(footman->ranks[2U].duration_seconds == doctest::Approx(30.0F));
    CHECK(footman->ranks[2U].values[0U] == doctest::Approx(22.0F));
    CHECK(footman->ranks[2U].values[1U] == doctest::Approx(5.0F));
    CHECK(footman->ranks[2U].values[2U] == doctest::Approx(1.2F));
    CHECK(footman->ranks[2U].values[3U] == doctest::Approx(6.0F));
    CHECK(footman->ranks[2U].values[5U] == doctest::Approx(6.0F));
    CHECK(footman->ranks[2U].values[7U] == doctest::Approx(0.50F));

    CHECK(plan->implemented);
    CHECK(plan->targeting == AbilityTargeting::WorldLineOrGrid);
    CHECK(ability_tags_contain(plan->tags, AbilityTag::WorldEdit));
    CHECK(ability_tags_contain(plan->tags, AbilityTag::Construction));
    CHECK(plan->ranks[2U].range_meters == doctest::Approx(8.0F));
    CHECK(plan->ranks[0U].values[0U] == doctest::Approx(2.0F));
    CHECK(plan->ranks[1U].values[0U] == doctest::Approx(3.0F));
    CHECK(plan->ranks[2U].values[0U] == doctest::Approx(9.0F));
    CHECK(plan->ranks[2U].values[1U] == doctest::Approx(5.0F));
    CHECK(plan->ranks[2U].values[4U] == doctest::Approx(10.0F));
}

TEST_CASE("le catalogue des paliers deux a cinq reste aligne sur les modules avances") {
    constexpr std::array<AbilityId, 11U> prepared_ids {{
        AbilityId::KnightBulwarkCharge,
        AbilityId::KnightChampionCry,
        AbilityId::KnightPerfectRiposte,
        AbilityId::NinjaSmokeBomb,
        AbilityId::NinjaShinobiLeap,
        AbilityId::NinjaLightningDash,
        AbilityId::NinjaSpectralKunai,
        AbilityId::CommanderAssaultOrder,
        AbilityId::CommanderFleetMarksman,
        AbilityId::CommanderWarBanner,
        AbilityId::CommanderBulwarkFormation,
    }};
    for (const auto id : prepared_ids) {
        const auto* definition =
            ability_definition(id);
        REQUIRE(definition != nullptr);
        CHECK_FALSE(definition->implemented);
    }

    const auto* charge =
        ability_definition(
            AbilityId::KnightBulwarkCharge);
    const auto* cry =
        ability_definition(
            AbilityId::KnightChampionCry);
    const auto* riposte =
        ability_definition(
            AbilityId::KnightPerfectRiposte);
    const auto* charge_spec =
        knight_bulwark_charge_definition(
            KnightAbilityRank::RankThree);
    const auto* cry_spec =
        knight_champion_cry_definition(
            KnightAbilityRank::RankThree);
    const auto* riposte_spec =
        knight_perfect_riposte_definition(
            KnightAbilityRank::RankThree);
    REQUIRE(charge != nullptr);
    REQUIRE(cry != nullptr);
    REQUIRE(riposte != nullptr);
    REQUIRE(charge_spec != nullptr);
    REQUIRE(cry_spec != nullptr);
    REQUIRE(riposte_spec != nullptr);
    CHECK(
        charge->ranks[2U].range_meters ==
        doctest::Approx(
            charge_spec->distance_meters));
    CHECK(
        charge->ranks[2U].values[0U] ==
        doctest::Approx(
            charge_spec->weapon_damage_multiplier));
    CHECK(
        charge->ranks[2U].values[1U] ==
        doctest::Approx(
            static_cast<float>(
                charge_spec->maximum_targets)));
    CHECK(
        cry->ranks[2U].range_meters ==
        doctest::Approx(cry_spec->radius_meters));
    CHECK(
        cry->ranks[2U].duration_seconds ==
        doctest::Approx(cry_spec->duration_seconds));
    CHECK(
        cry->ranks[2U].values[0U] ==
        doctest::Approx(
            cry_spec->self_melee_damage_bonus));
    CHECK(
        riposte->ranks[2U].duration_seconds ==
        doctest::Approx(
            riposte_spec->parry_window_seconds));
    CHECK(
        riposte->ranks[2U].values[0U] ==
        doctest::Approx(
            riposte_spec->
                counter_weapon_damage_multiplier));

    const auto smoke_spec =
        ninja_smoke_bomb_tuning(
            NinjaAbilityRank::RankThree);
    const auto leap_spec =
        ninja_shinobi_leap_tuning(
            NinjaAbilityRank::RankThree);
    const auto dash_spec =
        ninja_lightning_dash_tuning(
            NinjaAbilityRank::RankThree);
    const auto kunai_spec =
        ninja_spectral_kunai_tuning(
            NinjaAbilityRank::RankThree);
    const auto* smoke =
        ability_definition(
            AbilityId::NinjaSmokeBomb);
    const auto* leap =
        ability_definition(
            AbilityId::NinjaShinobiLeap);
    const auto* dash =
        ability_definition(
            AbilityId::NinjaLightningDash);
    const auto* kunai =
        ability_definition(
            AbilityId::NinjaSpectralKunai);
    REQUIRE(smoke != nullptr);
    REQUIRE(leap != nullptr);
    REQUIRE(dash != nullptr);
    REQUIRE(kunai != nullptr);
    CHECK(
        smoke->ranks[2U].range_meters ==
        doctest::Approx(smoke_spec.radius));
    CHECK(
        smoke->ranks[2U].duration_seconds ==
        doctest::Approx(
            smoke_spec.duration_seconds));
    CHECK(
        smoke->ranks[2U].values[1U] ==
        doctest::Approx(smoke_spec.attack_bonus));
    CHECK(
        leap->ranks[2U].range_meters ==
        doctest::Approx(leap_spec.distance));
    CHECK(
        leap->ranks[2U].values[2U] ==
        doctest::Approx(
            leap_spec.free_second_impulse_distance));
    CHECK(
        dash->ranks[2U].values[0U] ==
        doctest::Approx(dash_spec.base_damage));
    CHECK(
        dash->ranks[2U].values[1U] ==
        doctest::Approx(
            dash_spec.kill_cooldown_reduction));
    CHECK(
        kunai->ranks[2U].duration_seconds ==
        doctest::Approx(
            kunai_spec.mark_duration_seconds));
    CHECK(
        kunai->ranks[2U].values[1U] ==
        doctest::Approx(
            static_cast<float>(
                kunai_spec.bounce_count)));

    const auto assault_spec =
        assault_order_spec(
            CommanderRank::RankThree);
    const auto marksman_spec =
        fleet_shooter_spec(
            CommanderRank::RankThree);
    const auto banner_spec =
        war_banner_spec(
            CommanderRank::RankThree);
    const auto formation_spec =
        rampart_formation_spec(
            CommanderRank::RankThree);
    const auto* assault =
        ability_definition(
            AbilityId::CommanderAssaultOrder);
    const auto* marksman =
        ability_definition(
            AbilityId::CommanderFleetMarksman);
    const auto* banner =
        ability_definition(
            AbilityId::CommanderWarBanner);
    const auto* formation =
        ability_definition(
            AbilityId::CommanderBulwarkFormation);
    REQUIRE(assault_spec.has_value());
    REQUIRE(marksman_spec.has_value());
    REQUIRE(banner_spec.has_value());
    REQUIRE(formation_spec.has_value());
    REQUIRE(assault != nullptr);
    REQUIRE(marksman != nullptr);
    REQUIRE(banner != nullptr);
    REQUIRE(formation != nullptr);
    CHECK(
        assault->ranks[2U].duration_seconds ==
        doctest::Approx(
            assault_spec->duration_seconds));
    CHECK(
        assault->ranks[2U].values[0U] ==
        doctest::Approx(
            assault_spec->movement_speed_bonus));
    CHECK(
        marksman->ranks[2U].range_meters ==
        doctest::Approx(marksman_spec->range));
    CHECK(
        marksman->ranks[2U].values[0U] ==
        doctest::Approx(
            marksman_spec->base_health));
    CHECK(
        banner->ranks[2U].range_meters ==
        doctest::Approx(banner_spec->radius));
    CHECK(
        banner->ranks[2U].values[1U] ==
        doctest::Approx(
            banner_spec->ally_damage_bonus));
    CHECK(
        formation->ranks[2U].duration_seconds ==
        doctest::Approx(
            formation_spec->duration_seconds));
    CHECK(
        formation->ranks[2U].values[0U] ==
        doctest::Approx(
            formation_spec->
                frontal_unit_damage_reduction));
}

TEST_CASE("les trente-cinq capacités non implémentées restent impossibles à acheter maîtriser ou équiper") {
    auto rejected_count = std::size_t {0U};
    for (const auto& definition : ability_catalog()) {
        if (definition.implemented) {
            continue;
        }
        CAPTURE(ability_index(definition.id));
        ++rejected_count;

        PlayerBuildState state {};
        const auto pristine_state = state;
        CHECK(
            player_ability_rank_purchase_failure(
                state,
                100U,
                definition.id) ==
            AbilityBuildFailure::UnimplementedAbility);
        CHECK(
            purchase_player_ability_rank(
                state,
                100U,
                definition.id)
                .failure ==
            AbilityBuildFailure::UnimplementedAbility);
        CHECK(state == pristine_state);

        const auto index =
            ability_index(definition.id);
        state.ability_ranks[index] =
            static_cast<std::uint8_t>(
                kAbilityRankCount);
        const auto injected_rank_state = state;
        CHECK(
            player_ability_mastery_purchase_failure(
                state,
                100U,
                definition.id) ==
            AbilityBuildFailure::UnimplementedAbility);
        CHECK(
            purchase_player_ability_mastery(
                state,
                100U,
                definition.id)
                .failure ==
            AbilityBuildFailure::UnimplementedAbility);
        CHECK(
            equip_player_ability(
                state,
                0U,
                definition.id)
                .failure ==
            AbilityBuildFailure::UnimplementedAbility);
        CHECK(state == injected_rank_state);
    }
    CHECK(rejected_count == 35U);
}

TEST_CASE("les achats respectent niveau points disponibles et maîtrise") {
    PlayerBuildState state {};

    CHECK(
        player_ability_rank_purchase_failure(
            state,
            1U,
            AbilityId::KnightVanguardStrike) ==
        AbilityBuildFailure::None);
    CHECK(
        purchase_player_ability_rank(
            state,
            1U,
            AbilityId::KnightVanguardStrike)
            .succeeded());
    CHECK(
        state.charges[
            ability_index(AbilityId::KnightVanguardStrike)] ==
        1U);
    CHECK(
        player_ability_rank_purchase_failure(
            state,
            1U,
            AbilityId::KnightVanguardStrike) ==
        AbilityBuildFailure::RequiredLevel);
    CHECK(
        player_ability_rank_purchase_failure(
            PlayerBuildState {},
            100U,
            AbilityId::KnightIronGuard) ==
        AbilityBuildFailure::MissingPrerequisite);
    CHECK(
        player_ability_rank_purchase_failure(
            state,
            100U,
            AbilityId::KnightIronGuard) ==
        AbilityBuildFailure::RequiredPathPoints);

    REQUIRE(
        purchase_player_ability_rank(
            state,
            100U,
            AbilityId::KnightVanguardStrike)
            .succeeded());
    REQUIRE(
        purchase_player_ability_rank(
            state,
            100U,
            AbilityId::KnightVanguardStrike)
            .succeeded());
    const auto budget =
        player_build_point_budget(
            state,
            100U);
    CHECK(budget.spent_skill_points == 6U);
    CHECK(budget.spent_path_points[0U] == 6U);
    CHECK(budget.available_skill_points == 94U);

    CHECK(
        player_ability_mastery_purchase_failure(
            state,
            100U,
            AbilityId::KnightIronGuard) ==
        AbilityBuildFailure::RankThreeRequired);
    CHECK(
        purchase_player_ability_mastery(
            state,
            100U,
            AbilityId::KnightVanguardStrike)
            .succeeded());
    CHECK(
        player_ability_has_mastery(
            state,
            AbilityId::KnightVanguardStrike));
    CHECK(
        player_ability_mastery_purchase_failure(
            state,
            100U,
            AbilityId::KnightVanguardStrike) ==
        AbilityBuildFailure::AlreadyMastered);
}

TEST_CASE("les cinq emplacements appliquent strictement leur catégorie et refusent les doublons") {
    for (std::size_t slot = 0U;
         slot < kEquippedAbilitySlotCount;
         ++slot) {
        CAPTURE(slot);
        CHECK(
            ability_category_fits_equipped_slot(
                AbilityCategory::Active,
                slot) ==
            (slot < 3U));
        CHECK(
            ability_category_fits_equipped_slot(
                AbilityCategory::Utility,
                slot) ==
            (slot == 3U));
        CHECK(
            ability_category_fits_equipped_slot(
                AbilityCategory::Ultimate,
                slot) ==
            (slot == 4U));
        CHECK_FALSE(
            ability_category_fits_equipped_slot(
                AbilityCategory::Passive,
                slot));
    }
    CHECK_FALSE(
        ability_category_fits_equipped_slot(
            AbilityCategory::Active,
            kEquippedAbilitySlotCount));

    auto state =
        make_castable_state(
            AbilityId::KnightVanguardStrike,
            1U);
    CHECK(
        equip_player_ability(
            state,
            4U,
            AbilityId::KnightVanguardStrike)
            .failure ==
        AbilityBuildFailure::IncompatibleSlot);
    CHECK(
        equip_player_ability(
            state,
            3U,
            AbilityId::KnightVanguardStrike)
            .failure ==
        AbilityBuildFailure::IncompatibleSlot);
    CHECK(
        equip_player_ability(
            state,
            1U,
            AbilityId::KnightVanguardStrike)
            .failure ==
        AbilityBuildFailure::DuplicateAbility);
    CHECK(
        equip_player_ability(
            state,
            5U,
            AbilityId::None)
            .failure ==
        AbilityBuildFailure::InvalidSlot);
    CHECK(
        equip_player_ability(
            state,
            1U,
            AbilityId::NinjaWindAcceleration)
            .failure ==
        AbilityBuildFailure::AbilityNotLearned);
    state.ability_ranks[
        ability_index(
            AbilityId::KnightSteelSkin)] = 1U;
    CHECK(
        equip_player_ability(
            state,
            1U,
            AbilityId::KnightSteelSkin)
            .failure ==
        AbilityBuildFailure::UnimplementedAbility);
    CHECK(
        equip_player_ability(
            state,
            0U,
            AbilityId::None)
            .succeeded());
    CHECK(state.equipped_abilities[0U] == AbilityId::None);

    PlayerBuildState utility_state {};
    utility_state.ability_ranks[
        ability_index(
            AbilityId::NinjaWindAcceleration)] = 1U;
    CHECK(
        equip_player_ability(
            utility_state,
            0U,
            AbilityId::NinjaWindAcceleration)
            .failure ==
        AbilityBuildFailure::IncompatibleSlot);
    CHECK(
        equip_player_ability(
            utility_state,
            3U,
            AbilityId::NinjaWindAcceleration)
            .succeeded());
    CHECK(
        utility_state.equipped_abilities[3U] ==
        AbilityId::NinjaWindAcceleration);
}

TEST_CASE("la voie dominante résout les égalités par le dernier choix et la révision ne recule jamais") {
    PlayerBuildState state {};
    state.last_dominant_path =
        AbilityPath::Ninja;
    CHECK(
        player_dominant_path(
            state,
            100U) ==
        AbilityPath::Ninja);

    state.ability_ranks[
        ability_index(
            AbilityId::KnightVanguardStrike)] = 1U;
    state.ability_ranks[
        ability_index(
            AbilityId::NinjaWindAcceleration)] = 1U;
    CHECK(
        player_dominant_path(
            state,
            100U) ==
        AbilityPath::Ninja);

    state.last_dominant_path =
        AbilityPath::Commander;
    CHECK(
        player_dominant_path(
            state,
            100U) ==
        AbilityPath::Knight);

    state.revision = 41ULL;
    select_player_dominant_path(
        state,
        AbilityPath::Ninja);
    CHECK(state.last_dominant_path == AbilityPath::Ninja);
    CHECK(state.revision == 42ULL);
    select_player_dominant_path(
        state,
        AbilityPath::Ninja);
    CHECK(state.revision == 42ULL);
    select_player_dominant_path(
        state,
        AbilityPath::Count);
    CHECK(state.last_dominant_path == AbilityPath::Ninja);
    CHECK(state.revision == 42ULL);

    state.ability_ranks[
        ability_index(
            AbilityId::NinjaWindAcceleration)] = 2U;
    state.last_dominant_path =
        AbilityPath::Knight;
    CHECK(
        player_dominant_path(
            state,
            100U) ==
        AbilityPath::Ninja);
}

TEST_CASE("chaque mutation réussie du build incrémente exactement sa révision") {
    PlayerBuildState state {};
    CHECK(state.revision == 0ULL);
    REQUIRE(
        purchase_player_ability_rank(
            state,
            100U,
            AbilityId::KnightVanguardStrike)
            .succeeded());
    CHECK(state.revision == 1ULL);

    CHECK(
        purchase_player_ability_rank(
            state,
            100U,
            AbilityId::KnightIronGuard)
            .failure ==
        AbilityBuildFailure::RequiredPathPoints);
    CHECK(state.revision == 1ULL);

    REQUIRE(
        equip_player_ability(
            state,
            0U,
            AbilityId::KnightVanguardStrike)
            .succeeded());
    CHECK(state.revision == 2ULL);
    REQUIRE(
        purchase_player_ability_rank(
            state,
            100U,
            AbilityId::KnightVanguardStrike)
            .succeeded());
    REQUIRE(
        purchase_player_ability_rank(
            state,
            100U,
            AbilityId::KnightVanguardStrike)
            .succeeded());
    CHECK(state.revision == 4ULL);
    REQUIRE(
        purchase_player_ability_mastery(
            state,
            100U,
            AbilityId::KnightVanguardStrike)
            .succeeded());
    CHECK(state.revision == 5ULL);
    select_player_dominant_path(
        state,
        AbilityPath::Builder);
    CHECK(state.revision == 6ULL);

    sanitize_player_build_state(
        state,
        100U);
    CHECK(state.revision == 6ULL);
}

TEST_CASE("la sanitization reconstruit un build déterministe et borne son état transitoire") {
    PlayerBuildState state {};
    state.ability_ranks.fill(
        std::numeric_limits<std::uint8_t>::max());
    state.ability_masteries.fill(
        std::numeric_limits<std::uint8_t>::max());
    state.attributes.values.fill(
        std::numeric_limits<std::uint8_t>::max());
    state.equipped_abilities = {
        AbilityId::KnightVanguardStrike,
        AbilityId::KnightVanguardStrike,
        AbilityId::KnightSteelSkin,
        AbilityId::None,
        static_cast<AbilityId>(254U),
    };
    state.val_energy =
        std::numeric_limits<float>::quiet_NaN();
    state.global_cooldown_remaining =
        std::numeric_limits<float>::infinity();
    state.energy_regeneration_delay_remaining = 99.0F;
    state.cooldowns_remaining.fill(
        std::numeric_limits<float>::infinity());
    state.charges.fill(
        std::numeric_limits<std::uint8_t>::max());
    state.last_dominant_path =
        AbilityPath::Count;
    state.revision = 918ULL;
    state.selected_construction_plan = 99U;
    auto& plan =
        state.construction_plans[0U];
    plan.shape =
        static_cast<ConstructionPlanShape>(99U);
    plan.mirrored = true;
    plan.cell_count = 12U;
    plan.cells[0U] = {100, -100, 100, 999U};
    plan.cells[1U] = {100, -100, 100, 9U};

    CHECK(
        player_ability_rank(
            state,
            AbilityId::KnightVanguardStrike) ==
        0U);
    CHECK_FALSE(
        player_ability_has_mastery(
            state,
            AbilityId::KnightVanguardStrike));

    sanitize_player_build_state(
        state,
        6U);

    const auto vanguard_index =
        ability_index(
            AbilityId::KnightVanguardStrike);
    for (std::size_t index = 0U;
         index < kAbilityCount;
         ++index) {
        CHECK(
            state.ability_ranks[index] ==
            (index == vanguard_index ? 3U : 0U));
        CHECK(state.ability_masteries[index] == 0U);
    }
    CHECK(state.attributes.values[0U] == 1U);
    CHECK(state.attributes.values[1U] == 0U);
    CHECK(
        state.equipped_abilities[0U] ==
        AbilityId::KnightVanguardStrike);
    CHECK(state.equipped_abilities[1U] == AbilityId::None);
    CHECK(state.equipped_abilities[2U] == AbilityId::None);
    CHECK(state.equipped_abilities[4U] == AbilityId::None);
    CHECK(state.last_dominant_path == AbilityPath::Knight);
    CHECK(state.revision == 918ULL);
    CHECK(state.val_energy == doctest::Approx(100.0F));
    CHECK(state.global_cooldown_remaining == doctest::Approx(0.0F));
    CHECK(
        state.energy_regeneration_delay_remaining ==
        doctest::Approx(1.5F));
    CHECK(state.selected_construction_plan == 2U);
    CHECK(plan.shape == ConstructionPlanShape::Line);
    CHECK_FALSE(plan.mirrored);
    CHECK(plan.cell_count == 2U);
    CHECK(plan.cells[0U].x == 32);
    CHECK(plan.cells[0U].y == -32);
    CHECK(plan.cells[0U].z == 32);
    CHECK(plan.cells[0U].material_id == 0U);
    CHECK(construction_plan_is_canonical(plan));

    const auto sanitized_once = state;
    sanitize_player_build_state(
        state,
        6U);
    CHECK(state == sanitized_once);
}

TEST_CASE("la sanitization conserve les achats valides et rembourse le reste d'un arbre sur-budget") {
    PlayerBuildState state {};
    state.ability_ranks.fill(3U);
    state.ability_masteries.fill(1U);
    state.attributes.values.fill(10U);
    sanitize_player_build_state(
        state,
        100U);

    const auto budget =
        player_build_point_budget(
            state,
            100U);
    CHECK(budget.spent_skill_points == 30U);
    CHECK(budget.available_skill_points == 70U);
    CHECK(budget.spent_attribute_points == 20U);
    CHECK(budget.available_attribute_points == 0U);
    CHECK(budget.spent_mastery_points == 5U);
    CHECK(budget.available_mastery_points == 0U);
    for (std::size_t index = 0U;
         index < kAbilityCount;
         ++index) {
        const auto* definition =
            ability_definition(
                ability_id_from_index(index));
        REQUIRE(definition != nullptr);
        const auto expected =
            definition->implemented ? 3U : 0U;
        CHECK(state.ability_ranks[index] == expected);
        CHECK(state.ability_masteries[index] == expected / 3U);
    }
    CHECK(state.attributes.values[0U] == 10U);
    CHECK(state.attributes.values[1U] == 10U);
    CHECK(state.attributes.values[2U] == 0U);
    CHECK(state.attributes.values[3U] == 0U);
}

TEST_CASE("garde de fer prépare exactement ses trois rangs défensifs") {
    constexpr std::array expected_durations {
        3.0F,
        3.5F,
        4.0F,
    };
    constexpr std::array expected_reductions {
        0.25F,
        0.30F,
        0.35F,
    };
    constexpr std::array expected_knockback {
        0.60F,
        0.75F,
        0.90F,
    };

    for (auto rank = std::uint8_t {1U};
         rank <= 3U;
         ++rank) {
        const auto state =
            make_castable_state(
                AbilityId::KnightIronGuard,
                rank,
                rank == 3U);
        AbilityCastRequest request {};
        request.id =
            AbilityId::KnightIronGuard;
        const auto prepared =
            prepare_player_ability_cast(
                state,
                request);
        REQUIRE(prepared.succeeded());
        const auto index =
            static_cast<std::size_t>(
                rank - 1U);
        CHECK(
            prepared.resolution.duration_seconds ==
            doctest::Approx(
                expected_durations[index]));
        CHECK(
            prepared.resolution.values[0U] ==
            doctest::Approx(
                expected_reductions[index]));
        CHECK(
            prepared.resolution.values[1U] ==
            doctest::Approx(
                expected_knockback[index]));
        CHECK(
            prepared.resolution.values[2U] ==
            doctest::Approx(
                rank == 3U ? 0.15F : 0.0F));
        CHECK(
            prepared.resolution.mastery_active ==
            (rank == 3U));
    }
}

TEST_CASE("la frappe avant-garde applique sa maîtrise et ne paie qu'après commit") {
    auto state =
        make_castable_state(
            AbilityId::KnightVanguardStrike,
            3U,
            true);
    auto request =
        targeted_request(
            AbilityId::KnightVanguardStrike,
            2.5F);
    request.seconds_since_successful_shield_block = 2.0F;

    const auto prepared =
        prepare_player_ability_cast(
            state,
            request);
    REQUIRE(prepared.succeeded());
    CHECK(prepared.resolution.energy_cost == doctest::Approx(6.0F));
    CHECK(prepared.resolution.values[0U] == doctest::Approx(2.25F));
    CHECK(
        ability_effects_contain(
            prepared.resolution.effects,
            AbilityEffectFlag::VanguardSecondaryImpact));
    CHECK(
        ability_effects_contain(
            prepared.resolution.effects,
            AbilityEffectFlag::VanguardBlockSynergy));

    AbilitySystem system {};
    const auto initial = state;
    CHECK(
        system.try_cast(
                  state,
                  request,
                  {})
            .failure ==
        AbilityCastFailure::MissingCommitter);
    CHECK(state == initial);

    CastProbe probe {};
    probe.validation_result = false;
    CHECK(
        system.try_cast(
                  state,
                  request,
                  callbacks_for(probe))
            .failure ==
        AbilityCastFailure::ExternalValidationRejected);
    CHECK(state == initial);
    CHECK(probe.validation_calls == 1U);
    CHECK(probe.commit_calls == 0U);

    probe.validation_result = true;
    probe.commit_result = false;
    CHECK(
        system.try_cast(
                  state,
                  request,
                  callbacks_for(probe))
            .failure ==
        AbilityCastFailure::ExternalCommitRejected);
    CHECK(state == initial);
    CHECK(probe.commit_calls == 1U);

    probe.commit_result = true;
    REQUIRE(
        system.try_cast(
                  state,
                  request,
                  callbacks_for(probe))
            .succeeded());
    CHECK(state.val_energy == doctest::Approx(94.0F));
    CHECK(state.global_cooldown_remaining == doctest::Approx(0.25F));
    CHECK(
        state.energy_regeneration_delay_remaining ==
        doctest::Approx(1.5F));
    CHECK(
        state.cooldowns_remaining[
            ability_index(request.id)] ==
        doctest::Approx(2.1F));
    CHECK(state.charges[ability_index(request.id)] == 0U);
    CHECK(state.successful_cast_sequence == 1ULL);
}

TEST_CASE("la frappe avant-garde hérite exactement de la portée effective de l'arme") {
    const auto state =
        make_castable_state(
            AbilityId::KnightVanguardStrike,
            1U);
    auto request =
        targeted_request(
            AbilityId::KnightVanguardStrike,
            4.25F);
    request.effective_range_meters = 4.25F;

    const auto at_weapon_limit =
        prepare_player_ability_cast(
            state,
            request);
    REQUIRE(at_weapon_limit.succeeded());
    CHECK(
        at_weapon_limit.resolution.range_meters ==
        doctest::Approx(4.25F));

    request.target_distance_meters = 4.251F;
    CHECK(
        prepare_player_ability_cast(
            state,
            request)
            .failure ==
        AbilityCastFailure::TargetOutOfRange);

    request.effective_range_meters = 1.75F;
    request.target_distance_meters = 1.751F;
    const auto shorter_weapon =
        prepare_player_ability_cast(
            state,
            request);
    CHECK(
        shorter_weapon.resolution.range_meters ==
        doctest::Approx(1.75F));
    CHECK(
        shorter_weapon.failure ==
        AbilityCastFailure::TargetOutOfRange);

    request.effective_range_meters =
        std::numeric_limits<float>::quiet_NaN();
    request.target_distance_meters = 3.0F;
    const auto invalid_override =
        prepare_player_ability_cast(
            state,
            request);
    REQUIRE(invalid_override.succeeded());
    CHECK(
        invalid_override.resolution.range_meters ==
        doctest::Approx(3.0F));
}

TEST_CASE("un cast ne paie et ne recharge qu'après un commit réussi") {
    auto state =
        make_castable_state(
            AbilityId::KnightVanguardStrike,
            1U);
    state.revision = 41ULL;
    const auto initial = state;
    const auto request =
        targeted_request(
            AbilityId::KnightVanguardStrike,
            2.0F);
    AbilitySystem system {};
    CastProbe probe {};

    probe.validation_result = false;
    CHECK(
        system.try_cast(
                  state,
                  request,
                  callbacks_for(probe))
            .failure ==
        AbilityCastFailure::ExternalValidationRejected);
    CHECK(state == initial);
    CHECK(probe.commit_calls == 0U);

    probe.validation_result = true;
    probe.commit_result = false;
    CHECK(
        system.try_cast(
                  state,
                  request,
                  callbacks_for(probe))
            .failure ==
        AbilityCastFailure::ExternalCommitRejected);
    CHECK(state == initial);
    CHECK(probe.commit_calls == 1U);

    probe.commit_result = true;
    REQUIRE(
        system.try_cast(
                  state,
                  request,
                  callbacks_for(probe))
            .succeeded());
    CHECK(state.revision == 42ULL);
    CHECK(state.successful_cast_sequence == 1ULL);
    CHECK(state.val_energy == doctest::Approx(88.0F));
    CHECK(state.global_cooldown_remaining == doctest::Approx(0.25F));
    CHECK(
        state.cooldowns_remaining[
            ability_index(request.id)] ==
        doctest::Approx(2.5F));
    CHECK(state.charges[ability_index(request.id)] == 0U);
}

TEST_CASE("le système de capacités publie les événements logiques du cast") {
    AbilitySystem system {};
    auto unavailable_state = PlayerBuildState {};
    auto request =
        targeted_request(
            AbilityId::KnightVanguardStrike,
            2.0F);
    request.event_payload.source_id = 1U;
    request.event_payload.position = {
        2.0F,
        3.0F,
        4.0F,
    };
    request.event_payload.direction = {
        0.0F,
        0.0F,
        -1.0F,
    };

    CHECK(
        system.try_cast(
                  unavailable_state,
                  request,
                  {})
            .failure ==
        AbilityCastFailure::AbilityNotLearned);
    CHECK(system.logical_events().empty());

    auto state =
        make_castable_state(
            AbilityId::KnightVanguardStrike,
            1U);
    const auto blocked =
        system.try_cast(
            state,
            request,
            {});
    CHECK(
        blocked.failure ==
        AbilityCastFailure::MissingCommitter);
    REQUIRE(blocked.resolution.cast_sequence != 0U);
    REQUIRE(system.logical_events().size() == 2U);
    CHECK(
        system.logical_events()[0U].type ==
        AbilityEventType::CastStarted);
    CHECK(
        system.logical_events()[1U].type ==
        AbilityEventType::Blocked);
    CHECK(
        system.logical_events()[1U]
            .payload.detail_code ==
        static_cast<std::uint32_t>(
            AbilityCastFailure::MissingCommitter));

    system.reset_timing();
    CHECK(system.logical_events().empty());

    CastProbe probe {};
    const auto succeeded =
        system.try_cast(
            state,
            request,
            callbacks_for(probe));
    REQUIRE(succeeded.succeeded());
    REQUIRE(succeeded.resolution.cast_sequence != 0U);
    REQUIRE(system.logical_events().size() == 2U);
    CHECK(
        system.logical_events()[0U]
            .cast_sequence ==
        succeeded.resolution.cast_sequence);
    CHECK(
        system.logical_events()[1U].type ==
        AbilityEventType::CastSucceeded);
    CHECK(
        system.logical_events()[1U]
            .payload.position ==
        request.event_payload.position);

    auto hit_payload =
        request.event_payload;
    hit_payload.ability_id =
        request.id;
    hit_payload.primary_value = 3.5F;
    REQUIRE(
        system.publish_logical_event(
                  AbilityEventType::Hit,
                  succeeded.resolution
                      .cast_sequence,
                  hit_payload)
            .accepted());

    std::array<AbilityLogicalEvent, 3U> drained {};
    CHECK(
        system.drain_logical_events(
            drained) == 3U);
    CHECK(drained[2U].type == AbilityEventType::Hit);
    CHECK(drained[2U].payload.primary_value == doctest::Approx(3.5F));
    CHECK(system.logical_events().empty());
}

TEST_CASE("les validations génériques refusent portée EV GCD et recharge") {
    auto state =
        make_castable_state(
            AbilityId::CommanderFootman,
            1U);
    auto request =
        targeted_request(
            AbilityId::CommanderFootman,
            8.01F);
    CHECK(
        prepare_player_ability_cast(
            state,
            request)
            .failure ==
        AbilityCastFailure::TargetOutOfRange);

    request.target_distance_meters = 8.0F;
    request.ground_target_valid = false;
    CHECK(
        prepare_player_ability_cast(
            state,
            request)
            .failure ==
        AbilityCastFailure::InvalidTarget);
    request.target_distance_meters =
        std::numeric_limits<float>::quiet_NaN();
    request.ground_target_valid = true;
    CHECK(
        prepare_player_ability_cast(
            state,
            request)
            .failure ==
        AbilityCastFailure::InvalidTarget);
    request.target_distance_meters = 8.0F;
    request.ground_target_valid = true;
    state.val_energy = 24.99F;
    CHECK(
        prepare_player_ability_cast(
            state,
            request)
            .failure ==
        AbilityCastFailure::InsufficientEnergy);

    state.val_energy = 100.0F;
    state.global_cooldown_remaining = 0.01F;
    CHECK(
        prepare_player_ability_cast(
            state,
            request)
            .failure ==
        AbilityCastFailure::GlobalCooldown);
    state.global_cooldown_remaining = 0.0F;
    state.charges[ability_index(request.id)] = 0U;
    state.cooldowns_remaining[ability_index(request.id)] = 1.0F;
    CHECK(
        prepare_player_ability_cast(
            state,
            request)
            .failure ==
        AbilityCastFailure::Cooldown);
}

TEST_CASE("le vent et le fantassin résolvent leurs rangs et maîtrises") {
    auto wind_state =
        make_castable_state(
            AbilityId::NinjaWindAcceleration,
            3U,
            true);
    AbilityCastRequest wind_request {};
    wind_request.id =
        AbilityId::NinjaWindAcceleration;
    const auto wind =
        prepare_player_ability_cast(
            wind_state,
            wind_request);
    REQUIRE(wind.succeeded());
    CHECK(wind.resolution.duration_seconds == doctest::Approx(6.0F));
    CHECK(wind.resolution.values[0U] == doctest::Approx(0.30F));
    CHECK(wind.resolution.values[1U] == doctest::Approx(0.25F));
    CHECK(wind.resolution.values[2U] == doctest::Approx(4.0F));
    CHECK(
        ability_effects_contain(
            wind.resolution.effects,
            AbilityEffectFlag::WindBlade));
    CHECK(
        ability_effects_contain(
            wind.resolution.effects,
            AbilityEffectFlag::WindMasteryCleanseSlow |
                AbilityEffectFlag::WindMasteryDodge));

    auto footman_state =
        make_castable_state(
            AbilityId::CommanderFootman,
            3U,
            true);
    const auto footman =
        prepare_player_ability_cast(
            footman_state,
            targeted_request(
                AbilityId::CommanderFootman,
                8.0F));
    REQUIRE(footman.succeeded());
    CHECK(footman.resolution.duration_seconds == doctest::Approx(30.0F));
    CHECK(footman.resolution.values[0U] == doctest::Approx(22.0F));
    CHECK(footman.resolution.values[1U] == doctest::Approx(5.0F));
    CHECK(footman.resolution.values[2U] == doctest::Approx(1.2F));
    CHECK(
        ability_effects_contain(
            footman.resolution.effects,
            AbilityEffectFlag::FootmanLightTaunt |
                AbilityEffectFlag::FootmanProjectileBlock |
                AbilityEffectFlag::FootmanMasterySurvival |
                AbilityEffectFlag::FootmanMasteryDamageReduction));
}

TEST_CASE("le plan de construction est borné par rang et interdit sur navire mobile") {
    auto state =
        make_castable_state(
            AbilityId::BuilderConstructionPlan,
            3U);
    auto& plan =
        state.construction_plans[0U];
    plan.shape = ConstructionPlanShape::Grid;
    plan.cell_count = 9U;
    for (std::size_t index = 0U;
         index < plan.cell_count;
         ++index) {
        plan.cells[index] = {
            static_cast<std::int8_t>(
                index % 3U),
            0,
            static_cast<std::int8_t>(
                index / 3U),
            4U,
        };
    }
    auto request =
        targeted_request(
            AbilityId::BuilderConstructionPlan,
            8.0F);

    auto prepared =
        prepare_player_ability_cast(
            state,
            request);
    REQUIRE(prepared.succeeded());
    CHECK(prepared.resolution.maximum_construction_cells == 9U);
    CHECK(prepared.resolution.construction_plan.cell_count == 9U);

    request.construction_cell_count = 10U;
    CHECK(
        prepare_player_ability_cast(
            state,
            request)
            .failure ==
        AbilityCastFailure::InvalidConstructionPlan);
    request.construction_cell_count = 0U;
    request.on_moving_ship = true;
    CHECK(
        prepare_player_ability_cast(
            state,
            request)
            .failure ==
        AbilityCastFailure::MovingShipConstruction);

    request.on_moving_ship = false;
    state.ability_masteries[
        ability_index(
            AbilityId::BuilderConstructionPlan)] = 1U;
    plan.mirrored = true;
    plan.cell_count = 10U;
    plan.cells[9U] = {3, 0, 0, 4U};
    prepared =
        prepare_player_ability_cast(
            state,
            request);
    REQUIRE(prepared.succeeded());
    CHECK(prepared.resolution.maximum_construction_cells == 10U);
    CHECK(
        ability_effects_contain(
            prepared.resolution.effects,
            AbilityEffectFlag::ConstructionMirror));

    auto empty_state =
        make_castable_state(
            AbilityId::BuilderConstructionPlan,
            3U);
    request.construction_cell_count = 9U;
    CHECK(
        prepare_player_ability_cast(
            empty_state,
            request)
            .failure ==
        AbilityCastFailure::InvalidConstructionPlan);
}

TEST_CASE("un plan vide produit seulement la ligne implicite autorisée par le rang") {
    const auto state =
        make_castable_state(
            AbilityId::BuilderConstructionPlan,
            2U);
    auto request =
        targeted_request(
            AbilityId::BuilderConstructionPlan,
            8.0F);

    const auto implicit_plan =
        prepare_player_ability_cast(
            state,
            request);
    REQUIRE(implicit_plan.succeeded());
    CHECK(
        implicit_plan.resolution
                .construction_plan.shape ==
        ConstructionPlanShape::Line);
    CHECK(
        implicit_plan.resolution
                .construction_plan.cell_count ==
        3U);
    CHECK(
        implicit_plan.resolution
                .maximum_construction_cells ==
        3U);
    for (std::size_t index = 0U;
         index < 3U;
         ++index) {
        const auto& cell =
            implicit_plan.resolution
                .construction_plan.cells[index];
        CHECK(cell.x == static_cast<std::int8_t>(index));
        CHECK(cell.y == 0);
        CHECK(cell.z == 0);
        CHECK(
            cell.material_id ==
            to_block_id(BlockType::Planks));
    }

    request.construction_cell_count = 3U;
    const auto explicit_expansion =
        prepare_player_ability_cast(
            state,
            request);
    CHECK(
        explicit_expansion.failure ==
        AbilityCastFailure::InvalidConstructionPlan);
    CHECK(
        explicit_expansion.resolution
                .maximum_construction_cells ==
        0U);
}

TEST_CASE("le fixed-step respecte délai de régénération GCD et recharge") {
    auto state =
        make_castable_state(
            AbilityId::KnightVanguardStrike,
            3U);
    CastProbe probe {};
    AbilitySystem system {};
    REQUIRE(
        system.try_cast(
                  state,
                  targeted_request(
                      AbilityId::KnightVanguardStrike,
                      2.0F),
                  callbacks_for(probe))
            .succeeded());
    CHECK(state.val_energy == doctest::Approx(88.0F));

    system.update(state, 1.5F);
    CHECK(state.val_energy == doctest::Approx(88.0F).epsilon(0.001));
    CHECK(state.global_cooldown_remaining == doctest::Approx(0.0F));
    CHECK(state.charges[ability_index(AbilityId::KnightVanguardStrike)] == 0U);

    system.update(state, 0.5F);
    CHECK(state.val_energy == doctest::Approx(92.0F).epsilon(0.001));
    CHECK(state.charges[ability_index(AbilityId::KnightVanguardStrike)] == 0U);
    system.update(state, 0.1F);
    CHECK(state.charges[ability_index(AbilityId::KnightVanguardStrike)] == 1U);
    CHECK(
        state.cooldowns_remaining[
            ability_index(AbilityId::KnightVanguardStrike)] ==
        doctest::Approx(0.0F));

    const auto before_invalid_update = state;
    system.update(
        state,
        std::numeric_limits<float>::quiet_NaN());
    system.update(state, -1.0F);
    CHECK(state == before_invalid_update);

    system.update(
        state,
        std::numeric_limits<float>::max());
    CHECK(state.val_energy == doctest::Approx(100.0F));
    CHECK(system.pending_time_seconds() >= 0.0);
    CHECK(
        system.pending_time_seconds() <
        static_cast<double>(
            kAbilityFixedStepSeconds));
}

TEST_CASE("le fixed-step donne le même résultat quelle que soit la découpe des images") {
    auto one_frame =
        make_castable_state(
            AbilityId::NinjaWindAcceleration,
            1U);
    one_frame.val_energy = 20.0F;
    one_frame.energy_regeneration_delay_remaining = 1.5F;
    one_frame.global_cooldown_remaining = 0.25F;
    const auto ability =
        ability_index(
            AbilityId::NinjaWindAcceleration);
    one_frame.charges[ability] = 0U;
    one_frame.cooldowns_remaining[ability] = 2.0F;
    auto many_frames = one_frame;

    AbilitySystem one_frame_system {};
    AbilitySystem many_frames_system {};
    one_frame_system.update(
        one_frame,
        2.0F);
    for (std::size_t step = 0U;
         step < 120U;
         ++step) {
        many_frames_system.update(
            many_frames,
            kAbilityFixedStepSeconds);
    }

    CHECK(one_frame.val_energy == doctest::Approx(many_frames.val_energy));
    CHECK(
        one_frame.energy_regeneration_delay_remaining ==
        doctest::Approx(
            many_frames.energy_regeneration_delay_remaining));
    CHECK(
        one_frame.cooldowns_remaining[ability] ==
        doctest::Approx(
            many_frames.cooldowns_remaining[ability]));
    CHECK(one_frame.charges[ability] == many_frames.charges[ability]);
}

TEST_CASE("les dégâts et les invocations appliquent exactement les bonus additifs prévus") {
    CHECK(
        player_melee_damage_multiplier(
            1U,
            0U) ==
        doctest::Approx(1.0F));
    CHECK(
        player_melee_damage_multiplier(
            100U,
            10U) ==
        doctest::Approx(1.4475F));
    CHECK(
        player_ninja_damage_multiplier(
            100U,
            10U) ==
        doctest::Approx(1.4475F));
    CHECK(
        player_melee_damage_multiplier(
            1000U,
            std::numeric_limits<std::uint8_t>::max()) ==
        doctest::Approx(1.5475F));
    CHECK(
        player_ninja_damage_multiplier(
            0U,
            10U) ==
        doctest::Approx(1.2F));

    CHECK(
        player_summon_health_multiplier(
            100U,
            10U) ==
        doctest::Approx(1.695F));
    CHECK(
        player_summon_damage_multiplier(
            100U,
            10U) ==
        doctest::Approx(1.497F));
    CHECK(
        player_summon_health_multiplier(
            0U,
            std::numeric_limits<std::uint8_t>::max()) ==
        doctest::Approx(1.3F));
    CHECK(
        player_summon_damage_multiplier(
            1000U,
            std::numeric_limits<std::uint8_t>::max()) ==
        doctest::Approx(1.597F));
}

TEST_CASE("la sagesse augmente exactement le plafond et la régénération des EV") {
    PlayerBuildState state {};
    state.attributes.values[
        player_attribute_index(
            PlayerAttribute::Wisdom)] = 10U;
    const auto base_parameters =
        player_ability_energy_parameters(
            state);
    CHECK(base_parameters.maximum_energy == doctest::Approx(140.0F));
    CHECK(
        base_parameters.regeneration_per_second ==
        doctest::Approx(8.8F));

    const auto equipment_parameters =
        player_ability_energy_parameters(
            state,
            5U);
    CHECK(equipment_parameters.maximum_energy == doctest::Approx(160.0F));
    CHECK(
        equipment_parameters.regeneration_per_second ==
        doctest::Approx(9.2F));

    state.val_energy = 100.0F;
    AbilitySystem system {};
    system.update(
        state,
        1.0F);
    CHECK(state.val_energy == doctest::Approx(108.8F).epsilon(0.001));

    state.val_energy = 200.0F;
    sanitize_player_build_state(
        state,
        50U);
    CHECK(state.val_energy == doctest::Approx(140.0F));
}

} // namespace valcraft
