#pragma once

#include "gameplay/progression/PlayerAttributes.h"

#include <cstdint>

namespace valcraft {

inline constexpr std::uint32_t kLegendaryWeaponRequiredPlayerLevel = 35U;
inline constexpr std::uint8_t kLegendaryWeaponRequiredStrength = 4U;
inline constexpr std::uint8_t kLegendaryWeaponRequiredMapFragments = 3U;
inline constexpr std::uint32_t kLegendaryWeaponFirstAwakeningKills = 25U;
inline constexpr std::uint32_t kLegendaryWeaponFinalAwakeningKills = 100U;
inline constexpr std::uint32_t kLegendaryWeaponMaximumCorruptedKills =
    1'000'000U;
inline constexpr std::uint32_t kLegendaryWeaponUpgradeMomentum = 1U << 0U;
inline constexpr std::uint32_t kLegendaryWeaponUpgradeStability = 1U << 1U;
inline constexpr std::uint32_t kLegendaryWeaponUpgradeExecution = 1U << 2U;
inline constexpr std::uint32_t kLegendaryWeaponKnownUpgradeMask =
    kLegendaryWeaponUpgradeMomentum |
    kLegendaryWeaponUpgradeStability |
    kLegendaryWeaponUpgradeExecution;

enum class LegendaryWeaponQuestStage : std::uint8_t {
    NotStarted = 0,
    RumorHeard = 1,
    MapFragmentsComplete = 2,
    ForgeDiscovered = 3,
    GuardianDefeated = 4,
    WeaponClaimed = 5,
    FirstCombatComplete = 6,
};

enum class LegendaryWeaponAwakening : std::uint8_t {
    Dormant = 0,
    Corrupted = 1,
    Astral = 2,
    Awakened = 3,
};

enum class LegendaryWeaponCosmetic : std::uint8_t {
    LeviathanBone = 0,
    CorruptedVeins = 1,
    AstralRunes = 2,
    Sovereign = 3,
};

struct LegendaryWeaponProgressionState {
    std::uint64_t unique_weapon_id = 0ULL;
    LegendaryWeaponQuestStage quest_stage =
        LegendaryWeaponQuestStage::NotStarted;
    LegendaryWeaponAwakening awakening =
        LegendaryWeaponAwakening::Dormant;
    std::uint8_t map_fragments_collected = 0U;
    std::uint32_t corrupted_kills = 0U;
    std::uint32_t upgrade_flags = 0U;
    LegendaryWeaponCosmetic cosmetic =
        LegendaryWeaponCosmetic::LeviathanBone;
    bool weapon_owned = false;
    bool astral_boss_defeated = false;
    bool major_boss_defeated = false;
    bool forge_ritual_complete = false;

    auto operator==(const LegendaryWeaponProgressionState&) const
        -> bool = default;
};

[[nodiscard]] auto is_known_legendary_weapon_quest_stage(
    LegendaryWeaponQuestStage stage) noexcept -> bool;
[[nodiscard]] auto is_known_legendary_weapon_awakening(
    LegendaryWeaponAwakening awakening) noexcept -> bool;
[[nodiscard]] auto is_known_legendary_weapon_cosmetic(
    LegendaryWeaponCosmetic cosmetic) noexcept -> bool;
[[nodiscard]] auto legendary_weapon_meets_acquisition_requirements(
    std::uint32_t player_level,
    std::uint8_t strength) noexcept -> bool;
[[nodiscard]] auto legendary_weapon_meets_acquisition_requirements(
    std::uint32_t player_level,
    const PlayerAttributeAllocation& attributes) noexcept -> bool;
[[nodiscard]] auto derive_legendary_weapon_awakening(
    const LegendaryWeaponProgressionState& state) noexcept
    -> LegendaryWeaponAwakening;
[[nodiscard]] auto sanitize_legendary_weapon_progression_state(
    LegendaryWeaponProgressionState state) noexcept
    -> LegendaryWeaponProgressionState;
[[nodiscard]] auto is_valid_legendary_weapon_progression_state(
    const LegendaryWeaponProgressionState& state) noexcept -> bool;

class LegendaryWeaponProgression {
public:
    LegendaryWeaponProgression() = default;
    explicit LegendaryWeaponProgression(
        LegendaryWeaponProgressionState state) noexcept;

    void reset() noexcept;
    void load_state(LegendaryWeaponProgressionState state) noexcept;
    [[nodiscard]] auto state() const noexcept
        -> LegendaryWeaponProgressionState;

    [[nodiscard]] auto hear_rumor() noexcept -> bool;
    [[nodiscard]] auto collect_map_fragment() noexcept -> bool;
    [[nodiscard]] auto discover_forge() noexcept -> bool;
    [[nodiscard]] auto defeat_guardian() noexcept -> bool;
    [[nodiscard]] auto claim_weapon(
        std::uint64_t unique_weapon_id,
        std::uint32_t player_level,
        std::uint8_t strength) noexcept -> bool;
    [[nodiscard]] auto complete_first_combat() noexcept -> bool;

    [[nodiscard]] auto record_corrupted_kills(
        std::uint32_t count = 1U) noexcept -> bool;
    [[nodiscard]] auto record_astral_boss_defeat() noexcept -> bool;
    [[nodiscard]] auto record_major_boss_defeat() noexcept -> bool;
    [[nodiscard]] auto complete_forge_ritual() noexcept -> bool;
    [[nodiscard]] auto set_cosmetic(
        LegendaryWeaponCosmetic cosmetic) noexcept -> bool;
    [[nodiscard]] auto unlock_upgrades(
        std::uint32_t upgrade_flags) noexcept -> bool;

private:
    void refresh_awakening() noexcept;

    LegendaryWeaponProgressionState state_ {};
};

} // namespace valcraft
