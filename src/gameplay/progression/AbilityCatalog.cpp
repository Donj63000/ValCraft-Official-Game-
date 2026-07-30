#include "gameplay/progression/AbilityCatalog.h"

#include <algorithm>

namespace valcraft {

namespace {

using RankFloats = std::array<float, kAbilityRankCount>;
using RankValues =
    std::array<
        std::array<float, kAbilityValueCount>,
        kAbilityRankCount>;

constexpr std::array<std::uint8_t, 10> kTierLevels {{
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
}};

constexpr std::array<std::uint8_t, 10> kTierPathPoints {{
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
}};

[[nodiscard]] constexpr auto make_rank_levels(
    std::uint8_t tier) noexcept
    -> std::array<std::uint8_t, kAbilityRankCount> {
    if (tier == 10U) {
        return {50U, 70U, 90U};
    }
    const auto base =
        kTierLevels[static_cast<std::size_t>(tier - 1U)];
    return {
        base,
        static_cast<std::uint8_t>(base + 2U),
        static_cast<std::uint8_t>(base + 5U),
    };
}

[[nodiscard]] constexpr auto make_rank_costs(
    std::uint8_t tier) noexcept
    -> std::array<std::uint8_t, kAbilityRankCount> {
    return tier == 10U
               ? std::array<std::uint8_t, kAbilityRankCount> {
                     3U,
                     4U,
                     5U,
                 }
               : std::array<std::uint8_t, kAbilityRankCount> {
                     1U,
                     2U,
                     3U,
                 };
}

[[nodiscard]] constexpr auto repeated(
    float value) noexcept -> RankFloats {
    return {value, value, value};
}

[[nodiscard]] constexpr auto zero_values() noexcept
    -> RankValues {
    return {};
}

[[nodiscard]] constexpr auto make_ability(
    AbilityId id,
    std::string_view stable_name,
    AbilityPath path,
    std::uint8_t tier,
    AbilityCategory category,
    AbilityTargeting targeting,
    float energy_cost,
    RankFloats cooldowns,
    RankFloats ranges = {},
    RankFloats durations = {},
    RankValues values = {},
    AbilityTag tags = AbilityTag::None,
    bool implemented = false) noexcept
    -> AbilityDefinition {
    AbilityDefinition definition {};
    definition.id = id;
    definition.stable_name = stable_name;
    definition.path = path;
    definition.category = category;
    definition.targeting = targeting;
    definition.tier = tier;
    definition.required_level =
        kTierLevels[static_cast<std::size_t>(tier - 1U)];
    definition.required_path_points =
        kTierPathPoints[static_cast<std::size_t>(tier - 1U)];
    definition.prerequisite =
        tier == 1U
            ? AbilityId::None
            : static_cast<AbilityId>(
                  static_cast<std::uint8_t>(id) -
                  1U);
    definition.maximum_charges =
        category == AbilityCategory::Passive
            ? 0U
            : 1U;
    definition.tags = tags;
    definition.implemented = implemented;
    definition.visual_id = stable_name;
    definition.sfx_id = stable_name;

    const auto required_levels =
        make_rank_levels(tier);
    const auto rank_costs =
        make_rank_costs(tier);
    for (std::size_t rank_index = 0U;
         rank_index < definition.ranks.size();
         ++rank_index) {
        auto& rank =
            definition.ranks[rank_index];
        rank.required_level =
            required_levels[rank_index];
        rank.skill_point_cost =
            rank_costs[rank_index];
        rank.energy_cost =
            category == AbilityCategory::Passive
                ? 0.0F
                : energy_cost;
        rank.cooldown_seconds =
            category == AbilityCategory::Passive
                ? 0.0F
                : cooldowns[rank_index];
        rank.range_meters =
            ranges[rank_index];
        rank.duration_seconds =
            durations[rank_index];
        rank.values =
            values[rank_index];
    }
    return definition;
}

constexpr auto kAbilityCatalog =
    std::array<AbilityDefinition, kAbilityCount> {{
        make_ability(
            AbilityId::KnightVanguardStrike,
            "KnightVanguardStrike",
            AbilityPath::Knight,
            1U,
            AbilityCategory::Active,
            AbilityTargeting::MeleeCone,
            12.0F,
            {2.5F, 2.3F, 2.1F},
            repeated(3.0F),
            {},
            {{
                {1.40F, 0.25F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F},
                {1.60F, 0.35F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F},
                {1.80F, 0.35F, 2.0F, 3.0F, 0.0F, 0.0F, 0.0F, 0.0F},
            }},
            AbilityTag::Offensive |
                AbilityTag::Melee,
            true),
        make_ability(
            AbilityId::KnightIronGuard,
            "KnightIronGuard",
            AbilityPath::Knight,
            2U,
            AbilityCategory::Active,
            AbilityTargeting::Self,
            20.0F,
            {14.0F, 13.0F, 12.0F},
            {},
            {3.0F, 3.5F, 4.0F},
            {{
                {0.25F, 0.60F, 0.0F, 4.0F, 2.0F, 5.0F, 0.0F, 0.0F},
                {0.30F, 0.75F, 0.0F, 4.0F, 2.0F, 5.0F, 0.0F, 0.0F},
                {0.35F, 0.90F, 0.15F, 4.0F, 2.0F, 5.0F, 0.0F, 0.0F},
            }},
            AbilityTag::Defensive |
                AbilityTag::Buff,
            true),
        make_ability(
            AbilityId::KnightBulwarkCharge,
            "KnightBulwarkCharge",
            AbilityPath::Knight,
            3U,
            AbilityCategory::Active,
            AbilityTargeting::Direction,
            18.0F,
            {9.0F, 8.5F, 8.0F},
            {5.0F, 6.0F, 7.0F},
            {},
            {{
                {0.80F, 1.0F, 0.0F, 0.0F, 0.40F, 0.10F, 5.0F, 2.0F},
                {1.00F, 1.0F, 1.0F, 1.0F, 0.40F, 0.10F, 5.0F, 2.0F},
                {1.20F, 3.0F, 1.0F, 1.0F, 0.40F, 0.10F, 5.0F, 2.0F},
            }},
            AbilityTag::Offensive |
                AbilityTag::Melee |
                AbilityTag::Mobility),
        make_ability(
            AbilityId::KnightChampionCry,
            "KnightChampionCry",
            AbilityPath::Knight,
            4U,
            AbilityCategory::Active,
            AbilityTargeting::Self,
            25.0F,
            {20.0F, 18.0F, 16.0F},
            {6.0F, 7.0F, 8.0F},
            {7.0F, 8.0F, 9.0F},
            {{
                {0.15F, 0.10F, 0.0F, 0.10F, 1.0F, 0.0F, 0.0F, 0.0F},
                {0.20F, 0.15F, 0.0F, 0.10F, 1.0F, 0.0F, 0.0F, 0.0F},
                {0.25F, 0.20F, 3.0F, 0.10F, 1.0F, 0.0F, 0.0F, 0.0F},
            }},
            AbilityTag::Buff |
                AbilityTag::Area),
        make_ability(
            AbilityId::KnightPerfectRiposte,
            "KnightPerfectRiposte",
            AbilityPath::Knight,
            5U,
            AbilityCategory::Active,
            AbilityTargeting::Self,
            15.0F,
            {12.0F, 10.0F, 8.0F},
            {},
            {0.35F, 0.45F, 0.55F},
            {{
                {1.80F, 0.0F, 0.0F, 0.0F, 1.0F, 0.20F, 1.5F, 0.0F},
                {2.20F, 5.0F, 0.0F, 0.0F, 1.0F, 0.20F, 1.5F, 0.0F},
                {2.60F, 5.0F, 4.0F, 1.0F, 1.0F, 0.20F, 1.5F, 0.0F},
            }},
            AbilityTag::Offensive |
                AbilityTag::Melee |
                AbilityTag::Defensive),
        make_ability(
            AbilityId::KnightShockwave,
            "KnightShockwave",
            AbilityPath::Knight,
            6U,
            AbilityCategory::Active,
            AbilityTargeting::Self,
            35.0F,
            {20.0F, 18.0F, 16.0F}),
        make_ability(
            AbilityId::KnightSteelSkin,
            "KnightSteelSkin",
            AbilityPath::Knight,
            7U,
            AbilityCategory::Passive,
            AbilityTargeting::Self,
            0.0F,
            {}),
        make_ability(
            AbilityId::KnightColossusFury,
            "KnightColossusFury",
            AbilityPath::Knight,
            8U,
            AbilityCategory::Active,
            AbilityTargeting::Self,
            45.0F,
            {32.0F, 29.0F, 26.0F}),
        make_ability(
            AbilityId::KnightLivingFortress,
            "KnightLivingFortress",
            AbilityPath::Knight,
            9U,
            AbilityCategory::Active,
            AbilityTargeting::Self,
            55.0F,
            {48.0F, 44.0F, 40.0F}),
        make_ability(
            AbilityId::KnightTitanJudgment,
            "KnightTitanJudgment",
            AbilityPath::Knight,
            10U,
            AbilityCategory::Ultimate,
            AbilityTargeting::GroundPoint,
            80.0F,
            {100.0F, 90.0F, 80.0F}),

        make_ability(
            AbilityId::NinjaWindAcceleration,
            "NinjaWindAcceleration",
            AbilityPath::Ninja,
            1U,
            AbilityCategory::Utility,
            AbilityTargeting::Self,
            12.0F,
            {10.0F, 9.0F, 8.0F},
            {},
            {4.0F, 5.0F, 6.0F},
            {{
                {0.20F, 0.15F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F},
                {0.25F, 0.20F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F},
                {0.30F, 0.25F, 4.0F, 0.25F, 4.0F, 0.0F, 0.0F, 0.0F},
            }},
            AbilityTag::Mobility |
                AbilityTag::Buff,
            true),
        make_ability(
            AbilityId::NinjaSmokeBomb,
            "NinjaSmokeBomb",
            AbilityPath::Ninja,
            2U,
            AbilityCategory::Active,
            AbilityTargeting::GroundPoint,
            20.0F,
            {15.0F, 13.0F, 11.0F},
            {4.0F, 5.0F, 6.0F},
            {5.0F, 6.0F, 7.0F},
            {{
                {0.10F, 0.0F, 0.0F, 0.0F, 0.10F, 1.0F, 0.0F, 0.0F},
                {0.15F, 0.30F, 0.0F, 0.0F, 0.10F, 1.0F, 0.0F, 0.0F},
                {0.20F, 0.40F, 0.20F, 2.0F, 0.10F, 1.0F, 0.0F, 0.0F},
            }},
            AbilityTag::Buff |
                AbilityTag::Area),
        make_ability(
            AbilityId::NinjaShinobiLeap,
            "NinjaShinobiLeap",
            AbilityPath::Ninja,
            3U,
            AbilityCategory::Active,
            AbilityTargeting::Direction,
            15.0F,
            {8.0F, 7.0F, 6.0F},
            {4.0F, 5.0F, 6.0F},
            {},
            {{
                {0.0F, 0.0F, 0.0F, 2.0F, 5.0F, 2.0F, 0.0F, 0.0F},
                {1.0F, 0.0F, 0.0F, 2.0F, 5.0F, 2.0F, 0.0F, 0.0F},
                {1.0F, 1.0F, 3.0F, 2.0F, 5.0F, 2.0F, 0.0F, 0.0F},
            }},
            AbilityTag::Mobility),
        make_ability(
            AbilityId::NinjaLightningDash,
            "NinjaLightningDash",
            AbilityPath::Ninja,
            4U,
            AbilityCategory::Active,
            AbilityTargeting::Direction,
            22.0F,
            {11.0F, 9.5F, 8.0F},
            {6.0F, 7.0F, 8.0F},
            {},
            {{
                {7.0F, 0.0F, 2.0F, 12.0F, 0.75F, 0.0F, 0.0F, 0.0F},
                {9.0F, 0.40F, 2.0F, 12.0F, 0.75F, 0.0F, 0.0F, 0.0F},
                {11.0F, 0.50F, 2.0F, 12.0F, 0.75F, 0.0F, 0.0F, 0.0F},
            }},
            AbilityTag::Offensive |
                AbilityTag::Mobility |
                AbilityTag::Area),
        make_ability(
            AbilityId::NinjaSpectralKunai,
            "NinjaSpectralKunai",
            AbilityPath::Ninja,
            5U,
            AbilityCategory::Active,
            AbilityTargeting::Enemy,
            20.0F,
            {8.0F, 7.0F, 6.0F},
            {},
            {6.0F, 6.0F, 6.0F},
            {{
                {7.0F, 1.0F, 0.30F, 0.25F, 0.60F, 0.0F, 0.0F, 0.0F},
                {9.0F, 2.0F, 0.40F, 0.20F, 0.60F, 0.0F, 0.0F, 0.0F},
                {11.0F, 3.0F, 0.50F, 0.15F, 0.60F, 0.0F, 0.0F, 0.0F},
            }},
            AbilityTag::Offensive |
                AbilityTag::Ranged),
        make_ability(
            AbilityId::NinjaSubstitution,
            "NinjaSubstitution",
            AbilityPath::Ninja,
            6U,
            AbilityCategory::Active,
            AbilityTargeting::Self,
            25.0F,
            {22.0F, 19.0F, 16.0F}),
        make_ability(
            AbilityId::NinjaGhostStep,
            "NinjaGhostStep",
            AbilityPath::Ninja,
            7U,
            AbilityCategory::Passive,
            AbilityTargeting::Self,
            0.0F,
            {}),
        make_ability(
            AbilityId::NinjaBladeDance,
            "NinjaBladeDance",
            AbilityPath::Ninja,
            8U,
            AbilityCategory::Active,
            AbilityTargeting::Self,
            45.0F,
            {32.0F, 28.0F, 24.0F}),
        make_ability(
            AbilityId::NinjaAssassinMark,
            "NinjaAssassinMark",
            AbilityPath::Ninja,
            9U,
            AbilityCategory::Active,
            AbilityTargeting::Enemy,
            30.0F,
            {36.0F, 32.0F, 28.0F}),
        make_ability(
            AbilityId::NinjaThousandLightningStorm,
            "NinjaThousandLightningStorm",
            AbilityPath::Ninja,
            10U,
            AbilityCategory::Ultimate,
            AbilityTargeting::GroundPoint,
            80.0F,
            {100.0F, 90.0F, 80.0F}),

        make_ability(
            AbilityId::CommanderFootman,
            "CommanderFootman",
            AbilityPath::Commander,
            1U,
            AbilityCategory::Active,
            AbilityTargeting::GroundPoint,
            25.0F,
            {20.0F, 18.0F, 16.0F},
            repeated(8.0F),
            {20.0F, 25.0F, 30.0F},
            {{
                {14.0F, 3.0F, 1.2F, 0.0F, 0.0F, 0.0F, 1.0F, 0.50F},
                {18.0F, 4.0F, 1.2F, 6.0F, 6.0F, 0.0F, 1.0F, 0.50F},
                {22.0F, 5.0F, 1.2F, 6.0F, 6.0F, 6.0F, 1.0F, 0.50F},
            }},
            AbilityTag::Summon,
            true),
        make_ability(
            AbilityId::CommanderAssaultOrder,
            "CommanderAssaultOrder",
            AbilityPath::Commander,
            2U,
            AbilityCategory::Active,
            AbilityTargeting::GroundPoint,
            10.0F,
            {10.0F, 8.0F, 6.0F},
            {},
            {6.0F, 8.0F, 10.0F},
            {{
                {0.20F, 0.15F, 0.0F, 5.0F, 8.0F, 3.0F, 5.0F, 0.0F},
                {0.30F, 0.20F, 0.0F, 5.0F, 8.0F, 3.0F, 5.0F, 0.0F},
                {0.40F, 0.25F, 0.10F, 5.0F, 8.0F, 3.0F, 5.0F, 0.0F},
            }},
            AbilityTag::Buff |
                AbilityTag::Summon),
        make_ability(
            AbilityId::CommanderFleetMarksman,
            "CommanderFleetMarksman",
            AbilityPath::Commander,
            3U,
            AbilityCategory::Active,
            AbilityTargeting::GroundPoint,
            30.0F,
            {24.0F, 22.0F, 20.0F},
            {14.0F, 16.0F, 18.0F},
            {20.0F, 25.0F, 30.0F},
            {{
                {9.0F, 4.0F, 2.4F, 0.0F, 0.0F, 2.0F, 1.0F, 0.0F},
                {12.0F, 5.0F, 2.2F, 0.0F, 0.0F, 2.0F, 1.0F, 0.0F},
                {15.0F, 6.0F, 2.0F, 4.0F, 0.60F, 2.0F, 1.0F, 0.0F},
            }},
            AbilityTag::Offensive |
                AbilityTag::Ranged |
                AbilityTag::Summon),
        make_ability(
            AbilityId::CommanderWarBanner,
            "CommanderWarBanner",
            AbilityPath::Commander,
            4U,
            AbilityCategory::Active,
            AbilityTargeting::GroundPoint,
            30.0F,
            {28.0F, 24.0F, 20.0F},
            {6.0F, 8.0F, 10.0F},
            {12.0F, 15.0F, 18.0F},
            {{
                {12.0F, 0.10F, 0.25F, 3.0F, 0.15F, 0.0F, 0.0F, 0.0F},
                {16.0F, 0.15F, 0.50F, 4.0F, 0.15F, 0.0F, 0.0F, 0.0F},
                {20.0F, 0.20F, 0.75F, 5.0F, 0.15F, 0.0F, 0.0F, 0.0F},
            }},
            AbilityTag::Buff |
                AbilityTag::Area |
                AbilityTag::Healing |
                AbilityTag::Construction),
        make_ability(
            AbilityId::CommanderBulwarkFormation,
            "CommanderBulwarkFormation",
            AbilityPath::Commander,
            5U,
            AbilityCategory::Active,
            AbilityTargeting::GroundPoint,
            25.0F,
            {20.0F, 18.0F, 16.0F},
            {},
            {6.0F, 8.0F, 10.0F},
            {{
                {0.25F, 0.15F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F},
                {0.35F, 0.25F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F},
                {0.45F, 0.30F, 0.60F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F},
            }},
            AbilityTag::Buff |
                AbilityTag::Defensive |
                AbilityTag::Summon),
        make_ability(
            AbilityId::CommanderMedic,
            "CommanderMedic",
            AbilityPath::Commander,
            6U,
            AbilityCategory::Active,
            AbilityTargeting::GroundPoint,
            40.0F,
            {35.0F, 30.0F, 25.0F}),
        make_ability(
            AbilityId::CommanderSapper,
            "CommanderSapper",
            AbilityPath::Commander,
            7U,
            AbilityCategory::Active,
            AbilityTargeting::GroundPoint,
            40.0F,
            {24.0F, 21.0F, 18.0F}),
        make_ability(
            AbilityId::CommanderEliteCaptain,
            "CommanderEliteCaptain",
            AbilityPath::Commander,
            8U,
            AbilityCategory::Active,
            AbilityTargeting::GroundPoint,
            55.0F,
            {50.0F, 45.0F, 40.0F}),
        make_ability(
            AbilityId::CommanderImmediateReinforcements,
            "CommanderImmediateReinforcements",
            AbilityPath::Commander,
            9U,
            AbilityCategory::Active,
            AbilityTargeting::GroundPoint,
            60.0F,
            {65.0F, 58.0F, 50.0F}),
        make_ability(
            AbilityId::CommanderGrandArmy,
            "CommanderGrandArmy",
            AbilityPath::Commander,
            10U,
            AbilityCategory::Ultimate,
            AbilityTargeting::GroundPoint,
            80.0F,
            {120.0F, 105.0F, 90.0F}),

        make_ability(
            AbilityId::BuilderConstructionPlan,
            "BuilderConstructionPlan",
            AbilityPath::Builder,
            1U,
            AbilityCategory::Utility,
            AbilityTargeting::WorldLineOrGrid,
            8.0F,
            {1.5F, 1.2F, 0.9F},
            repeated(8.0F),
            {},
            {{
                {2.0F, 2.0F, 1.0F, 1.0F, 10.0F, 0.0F, 0.0F, 0.0F},
                {3.0F, 3.0F, 1.0F, 1.0F, 10.0F, 0.0F, 0.0F, 0.0F},
                {9.0F, 5.0F, 3.0F, 3.0F, 10.0F, 0.0F, 0.0F, 0.0F},
            }},
            AbilityTag::WorldEdit |
                AbilityTag::Construction,
            true),
        make_ability(
            AbilityId::BuilderExpressRepair,
            "BuilderExpressRepair",
            AbilityPath::Builder,
            2U,
            AbilityCategory::Active,
            AbilityTargeting::GroundPoint,
            18.0F,
            {10.0F, 8.0F, 6.0F},
            repeated(8.0F),
            {},
            {{
                {0.25F, 4.0F, 0.0F, 0.0F, 3.0F, 4.0F, 0.50F, 0.0F},
                {0.35F, 6.0F, 0.0F, 0.0F, 3.0F, 4.0F, 0.50F, 0.0F},
                {0.50F, 8.0F, 0.20F, 5.0F, 3.0F, 4.0F, 0.50F, 0.0F},
            }},
            AbilityTag::Healing |
                AbilityTag::Construction),
        make_ability(
            AbilityId::BuilderDeployableWall,
            "BuilderDeployableWall",
            AbilityPath::Builder,
            3U,
            AbilityCategory::Active,
            AbilityTargeting::WorldLineOrGrid,
            15.0F,
            {8.0F, 7.0F, 6.0F},
            repeated(8.0F),
            {},
            {{
                {3.0F, 2.0F, 6.0F, 0.60F, 3.0F, 15.0F, 0.0F, 0.0F},
                {3.0F, 3.0F, 9.0F, 0.60F, 3.0F, 15.0F, 0.0F, 0.0F},
                {5.0F, 3.0F, 15.0F, 0.60F, 3.0F, 15.0F, 1.0F, 0.0F},
            }},
            AbilityTag::WorldEdit |
                AbilityTag::Construction |
                AbilityTag::Defensive),
        make_ability(
            AbilityId::BuilderModularBridge,
            "BuilderModularBridge",
            AbilityPath::Builder,
            4U,
            AbilityCategory::Active,
            AbilityTargeting::WorldLineOrGrid,
            20.0F,
            {12.0F, 10.0F, 8.0F},
            repeated(9.0F),
            {},
            {{
                {5.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F},
                {7.0F, 1.0F, 1.0F, 2.0F, 0.0F, 0.0F, 0.0F, 0.0F},
                {9.0F, 2.0F, 1.0F, 2.0F, 1.0F, 0.0F, 0.0F, 0.0F},
            }},
            AbilityTag::WorldEdit |
                AbilityTag::Construction |
                AbilityTag::Mobility),
        make_ability(
            AbilityId::BuilderExcavationWave,
            "BuilderExcavationWave",
            AbilityPath::Builder,
            5U,
            AbilityCategory::Active,
            AbilityTargeting::Direction,
            25.0F,
            {14.0F, 12.0F, 10.0F},
            repeated(5.0F),
            {},
            {{
                {3.0F, 1.0F, 1.0F, 3.0F, 0.40F, 0.0F, 0.0F, 0.0F},
                {3.0F, 3.0F, 1.0F, 9.0F, 0.40F, 0.0F, 0.0F, 0.0F},
                {3.0F, 3.0F, 2.0F, 18.0F, 0.40F, 0.0F, 0.0F, 0.0F},
            }},
            AbilityTag::Offensive |
                AbilityTag::Area |
                AbilityTag::WorldEdit),
        make_ability(
            AbilityId::BuilderConstructionGolem,
            "BuilderConstructionGolem",
            AbilityPath::Builder,
            6U,
            AbilityCategory::Active,
            AbilityTargeting::GroundPoint,
            35.0F,
            {30.0F, 26.0F, 22.0F}),
        make_ability(
            AbilityId::BuilderAutomatedTurret,
            "BuilderAutomatedTurret",
            AbilityPath::Builder,
            7U,
            AbilityCategory::Active,
            AbilityTargeting::GroundPoint,
            40.0F,
            {12.0F, 10.0F, 8.0F}),
        make_ability(
            AbilityId::BuilderModularBastion,
            "BuilderModularBastion",
            AbilityPath::Builder,
            8U,
            AbilityCategory::Active,
            AbilityTargeting::WorldLineOrGrid,
            45.0F,
            {40.0F, 35.0F, 30.0F}),
        make_ability(
            AbilityId::BuilderGrandProject,
            "BuilderGrandProject",
            AbilityPath::Builder,
            9U,
            AbilityCategory::Active,
            AbilityTargeting::WorldLineOrGrid,
            50.0F,
            {45.0F, 40.0F, 35.0F}),
        make_ability(
            AbilityId::BuilderAbsoluteArchitect,
            "BuilderAbsoluteArchitect",
            AbilityPath::Builder,
            10U,
            AbilityCategory::Ultimate,
            AbilityTargeting::Self,
            80.0F,
            {120.0F, 105.0F, 90.0F}),
    }};

static_assert(
    kAbilityCatalog.size() ==
    kAbilityCount);

[[nodiscard]] constexpr auto catalog_is_indexed_by_id() noexcept -> bool {
    for (std::size_t index = 0U;
         index < kAbilityCatalog.size();
         ++index) {
        if (static_cast<std::size_t>(
                kAbilityCatalog[index].id) !=
            index) {
            return false;
        }
    }
    return true;
}

static_assert(catalog_is_indexed_by_id());

} // namespace

auto ability_catalog() noexcept
    -> std::span<const AbilityDefinition, kAbilityCount> {
    return kAbilityCatalog;
}

auto ability_definition(
    AbilityId id) noexcept -> const AbilityDefinition* {
    if (!ability_id_is_valid(id)) {
        return nullptr;
    }
    return &kAbilityCatalog[ability_index(id)];
}

auto ability_rank_definition(
    AbilityId id,
    std::uint8_t rank) noexcept -> const AbilityRankDefinition* {
    const auto* definition =
        ability_definition(id);
    if (definition == nullptr ||
        rank == 0U ||
        rank > kAbilityRankCount) {
        return nullptr;
    }
    return &definition->ranks[
        static_cast<std::size_t>(rank - 1U)];
}

auto ability_id_is_valid(
    AbilityId id) noexcept -> bool {
    return static_cast<std::uint8_t>(id) <
           static_cast<std::uint8_t>(
               AbilityId::Count);
}

auto ability_index(
    AbilityId id) noexcept -> std::size_t {
    if (!ability_id_is_valid(id)) {
        return kAbilityCount;
    }
    return static_cast<std::size_t>(
        static_cast<std::uint8_t>(id));
}

auto ability_id_from_index(
    std::size_t index) noexcept -> AbilityId {
    if (index >= kAbilityCount) {
        return AbilityId::None;
    }
    return static_cast<AbilityId>(
        static_cast<std::uint8_t>(index));
}

auto ability_path_index(
    AbilityPath path) noexcept -> std::size_t {
    const auto index =
        static_cast<std::size_t>(
            static_cast<std::uint8_t>(path));
    return std::min(
        index,
        kAbilityPathCount);
}

auto ability_path_first_id(
    AbilityPath path) noexcept -> AbilityId {
    const auto path_index =
        ability_path_index(path);
    if (path_index >= kAbilityPathCount) {
        return AbilityId::None;
    }
    return ability_id_from_index(
        path_index * 10U);
}

auto ability_previous_tier_id(
    AbilityId id) noexcept -> AbilityId {
    const auto* definition =
        ability_definition(id);
    return definition == nullptr
               ? AbilityId::None
               : definition->prerequisite;
}

auto resolved_ability_visual_id(
    AbilityId id) noexcept -> std::string_view {
    const auto* definition =
        ability_definition(id);
    return definition != nullptr &&
                   !definition->visual_id.empty()
               ? definition->visual_id
               : kGenericAbilityVisualId;
}

auto resolved_ability_sfx_id(
    AbilityId id) noexcept -> std::string_view {
    const auto* definition =
        ability_definition(id);
    return definition != nullptr &&
                   !definition->sfx_id.empty()
               ? definition->sfx_id
               : kGenericAbilitySfxId;
}

} // namespace valcraft
