#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace valcraft {

enum class AbilityPath : std::uint8_t {
    Knight = 0,
    Ninja = 1,
    Commander = 2,
    Builder = 3,
    Count = 4,
};

inline constexpr std::size_t kAbilityPathCount =
    static_cast<std::size_t>(AbilityPath::Count);

enum class AbilityCategory : std::uint8_t {
    Active = 0,
    Utility = 1,
    Passive = 2,
    Ultimate = 3,
};

enum class AbilityTargeting : std::uint8_t {
    Self = 0,
    MeleeCone = 1,
    Direction = 2,
    GroundPoint = 3,
    Enemy = 4,
    WorldLineOrGrid = 5,
};

enum class AbilityId : std::uint8_t {
    KnightVanguardStrike = 0,
    KnightIronGuard = 1,
    KnightBulwarkCharge = 2,
    KnightChampionCry = 3,
    KnightPerfectRiposte = 4,
    KnightShockwave = 5,
    KnightSteelSkin = 6,
    KnightColossusFury = 7,
    KnightLivingFortress = 8,
    KnightTitanJudgment = 9,

    NinjaWindAcceleration = 10,
    NinjaSmokeBomb = 11,
    NinjaShinobiLeap = 12,
    NinjaLightningDash = 13,
    NinjaSpectralKunai = 14,
    NinjaSubstitution = 15,
    NinjaGhostStep = 16,
    NinjaBladeDance = 17,
    NinjaAssassinMark = 18,
    NinjaThousandLightningStorm = 19,

    CommanderFootman = 20,
    CommanderAssaultOrder = 21,
    CommanderFleetMarksman = 22,
    CommanderWarBanner = 23,
    CommanderBulwarkFormation = 24,
    CommanderMedic = 25,
    CommanderSapper = 26,
    CommanderEliteCaptain = 27,
    CommanderImmediateReinforcements = 28,
    CommanderGrandArmy = 29,

    BuilderConstructionPlan = 30,
    BuilderExpressRepair = 31,
    BuilderDeployableWall = 32,
    BuilderModularBridge = 33,
    BuilderExcavationWave = 34,
    BuilderConstructionGolem = 35,
    BuilderAutomatedTurret = 36,
    BuilderModularBastion = 37,
    BuilderGrandProject = 38,
    BuilderAbsoluteArchitect = 39,

    Count = 40,
    None = 0xFFU,
};

inline constexpr std::size_t kAbilityCount =
    static_cast<std::size_t>(AbilityId::Count);
inline constexpr std::size_t kAbilityRankCount = 3U;
inline constexpr std::size_t kAbilityValueCount = 8U;
inline constexpr std::size_t kWindMovementBonusValueIndex = 0U;
inline constexpr std::size_t kWindRecoveryBonusValueIndex = 1U;
inline constexpr std::size_t kWindBladeDamageValueIndex = 2U;
inline constexpr std::size_t kWindMasteryDodgeValueIndex = 3U;
inline constexpr std::size_t kWindBladeRangeValueIndex = 4U;
inline constexpr std::size_t kFootmanHealthValueIndex = 0U;
inline constexpr std::size_t kFootmanDamageValueIndex = 1U;
inline constexpr std::size_t kFootmanAttackIntervalValueIndex = 2U;
inline constexpr std::size_t kFootmanTauntIntervalValueIndex = 3U;
inline constexpr std::size_t kFootmanTauntRadiusValueIndex = 4U;
inline constexpr std::size_t kFootmanProjectileBlockIntervalValueIndex = 5U;
inline constexpr std::size_t kFootmanMasteryHealthValueIndex = 6U;
inline constexpr std::size_t kFootmanMasteryReductionValueIndex = 7U;
inline constexpr std::string_view kGenericAbilityVisualId =
    "AbilityGenericVisual";
inline constexpr std::string_view kGenericAbilitySfxId =
    "AbilityGenericSfx";

static_assert(kAbilityCount == 40U);

enum class AbilityTag : std::uint32_t {
    None = 0U,
    Offensive = 1U << 0U,
    Melee = 1U << 1U,
    Mobility = 1U << 2U,
    Buff = 1U << 3U,
    Summon = 1U << 4U,
    WorldEdit = 1U << 5U,
    Construction = 1U << 6U,
    Defensive = 1U << 7U,
    Ranged = 1U << 8U,
    Area = 1U << 9U,
    Healing = 1U << 10U,
};

[[nodiscard]] constexpr auto operator|(
    AbilityTag lhs,
    AbilityTag rhs) noexcept -> AbilityTag {
    return static_cast<AbilityTag>(
        static_cast<std::uint32_t>(lhs) |
        static_cast<std::uint32_t>(rhs));
}

[[nodiscard]] constexpr auto ability_tags_contain(
    AbilityTag tags,
    AbilityTag expected) noexcept -> bool {
    const auto expected_bits =
        static_cast<std::uint32_t>(expected);
    return (
               static_cast<std::uint32_t>(tags) &
               expected_bits) ==
           expected_bits;
}

struct AbilityRankDefinition {
    std::uint8_t required_level = 1U;
    std::uint8_t skill_point_cost = 1U;
    float energy_cost = 0.0F;
    float cooldown_seconds = 0.0F;
    float range_meters = 0.0F;
    float duration_seconds = 0.0F;
    std::array<float, kAbilityValueCount> values {};

    auto operator==(const AbilityRankDefinition&) const -> bool = default;
};

struct AbilityDefinition {
    AbilityId id = AbilityId::None;
    std::string_view stable_name {};
    AbilityPath path = AbilityPath::Knight;
    AbilityCategory category = AbilityCategory::Active;
    AbilityTargeting targeting = AbilityTargeting::Self;
    std::uint8_t tier = 1U;
    std::uint8_t required_level = 1U;
    std::uint8_t required_path_points = 0U;
    AbilityId prerequisite = AbilityId::None;
    std::array<AbilityRankDefinition, kAbilityRankCount> ranks {};
    std::uint8_t maximum_charges = 1U;
    AbilityTag tags = AbilityTag::None;
    bool implemented = false;
    std::string_view visual_id {};
    std::string_view sfx_id {};

    auto operator==(const AbilityDefinition&) const -> bool = default;
};

[[nodiscard]] auto ability_catalog() noexcept
    -> std::span<const AbilityDefinition, kAbilityCount>;
[[nodiscard]] auto ability_definition(
    AbilityId id) noexcept -> const AbilityDefinition*;
[[nodiscard]] auto ability_rank_definition(
    AbilityId id,
    std::uint8_t rank) noexcept -> const AbilityRankDefinition*;
[[nodiscard]] auto ability_id_is_valid(
    AbilityId id) noexcept -> bool;
[[nodiscard]] auto ability_index(
    AbilityId id) noexcept -> std::size_t;
[[nodiscard]] auto ability_id_from_index(
    std::size_t index) noexcept -> AbilityId;
[[nodiscard]] auto ability_path_index(
    AbilityPath path) noexcept -> std::size_t;
[[nodiscard]] auto ability_path_first_id(
    AbilityPath path) noexcept -> AbilityId;
[[nodiscard]] auto ability_previous_tier_id(
    AbilityId id) noexcept -> AbilityId;
[[nodiscard]] auto resolved_ability_visual_id(
    AbilityId id) noexcept -> std::string_view;
[[nodiscard]] auto resolved_ability_sfx_id(
    AbilityId id) noexcept -> std::string_view;

} // namespace valcraft
