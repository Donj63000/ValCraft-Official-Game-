#include "gameplay/weapons/LegendaryWeaponProgression.h"

#include <algorithm>

namespace valcraft {

namespace {

[[nodiscard]] constexpr auto quest_stage_at_least(
    LegendaryWeaponQuestStage current,
    LegendaryWeaponQuestStage expected) noexcept -> bool {
    return static_cast<std::uint8_t>(current) >=
           static_cast<std::uint8_t>(expected);
}

} // namespace

auto is_known_legendary_weapon_quest_stage(
    LegendaryWeaponQuestStage stage) noexcept -> bool {
    switch (stage) {
    case LegendaryWeaponQuestStage::NotStarted:
    case LegendaryWeaponQuestStage::RumorHeard:
    case LegendaryWeaponQuestStage::MapFragmentsComplete:
    case LegendaryWeaponQuestStage::ForgeDiscovered:
    case LegendaryWeaponQuestStage::GuardianDefeated:
    case LegendaryWeaponQuestStage::WeaponClaimed:
    case LegendaryWeaponQuestStage::FirstCombatComplete:
        return true;
    default:
        return false;
    }
}

auto is_known_legendary_weapon_awakening(
    LegendaryWeaponAwakening awakening) noexcept -> bool {
    switch (awakening) {
    case LegendaryWeaponAwakening::Dormant:
    case LegendaryWeaponAwakening::Corrupted:
    case LegendaryWeaponAwakening::Astral:
    case LegendaryWeaponAwakening::Awakened:
        return true;
    default:
        return false;
    }
}

auto is_known_legendary_weapon_cosmetic(
    LegendaryWeaponCosmetic cosmetic) noexcept -> bool {
    switch (cosmetic) {
    case LegendaryWeaponCosmetic::LeviathanBone:
    case LegendaryWeaponCosmetic::CorruptedVeins:
    case LegendaryWeaponCosmetic::AstralRunes:
    case LegendaryWeaponCosmetic::Sovereign:
        return true;
    default:
        return false;
    }
}

auto legendary_weapon_meets_acquisition_requirements(
    std::uint32_t player_level,
    std::uint8_t strength) noexcept -> bool {
    return player_level >= kLegendaryWeaponRequiredPlayerLevel &&
           strength >= kLegendaryWeaponRequiredStrength;
}

auto legendary_weapon_meets_acquisition_requirements(
    std::uint32_t player_level,
    const PlayerAttributeAllocation& attributes) noexcept -> bool {
    return legendary_weapon_meets_acquisition_requirements(
        player_level,
        player_attribute_value(
            attributes,
            PlayerAttribute::Strength));
}

auto derive_legendary_weapon_awakening(
    const LegendaryWeaponProgressionState& state) noexcept
    -> LegendaryWeaponAwakening {
    if (!state.weapon_owned ||
        state.unique_weapon_id == 0ULL ||
        state.corrupted_kills <
            kLegendaryWeaponFirstAwakeningKills) {
        return LegendaryWeaponAwakening::Dormant;
    }
    if (!state.astral_boss_defeated) {
        return LegendaryWeaponAwakening::Corrupted;
    }
    if (state.corrupted_kills <
            kLegendaryWeaponFinalAwakeningKills ||
        !state.major_boss_defeated ||
        !state.forge_ritual_complete) {
        return LegendaryWeaponAwakening::Astral;
    }
    return LegendaryWeaponAwakening::Awakened;
}

auto sanitize_legendary_weapon_progression_state(
    LegendaryWeaponProgressionState state) noexcept
    -> LegendaryWeaponProgressionState {
    if (!is_known_legendary_weapon_quest_stage(
            state.quest_stage)) {
        state.quest_stage =
            LegendaryWeaponQuestStage::NotStarted;
    }
    if (!is_known_legendary_weapon_awakening(
            state.awakening)) {
        state.awakening =
            LegendaryWeaponAwakening::Dormant;
    }
    if (!is_known_legendary_weapon_cosmetic(
            state.cosmetic)) {
        state.cosmetic =
            LegendaryWeaponCosmetic::LeviathanBone;
    }

    state.map_fragments_collected =
        std::min(
            state.map_fragments_collected,
            kLegendaryWeaponRequiredMapFragments);
    state.corrupted_kills =
        std::min(
            state.corrupted_kills,
            kLegendaryWeaponMaximumCorruptedKills);
    state.upgrade_flags &=
        kLegendaryWeaponKnownUpgradeMask;

    if (state.quest_stage ==
        LegendaryWeaponQuestStage::NotStarted) {
        state.map_fragments_collected = 0U;
    } else if (
        state.map_fragments_collected >=
            kLegendaryWeaponRequiredMapFragments &&
        state.quest_stage ==
            LegendaryWeaponQuestStage::RumorHeard) {
        state.quest_stage =
            LegendaryWeaponQuestStage::MapFragmentsComplete;
    }
    if (quest_stage_at_least(
            state.quest_stage,
            LegendaryWeaponQuestStage::MapFragmentsComplete)) {
        state.map_fragments_collected =
            kLegendaryWeaponRequiredMapFragments;
    }

    if (!state.weapon_owned ||
        state.unique_weapon_id == 0ULL) {
        state.weapon_owned = false;
        state.unique_weapon_id = 0ULL;
        if (quest_stage_at_least(
                state.quest_stage,
                LegendaryWeaponQuestStage::WeaponClaimed)) {
            state.quest_stage =
                LegendaryWeaponQuestStage::GuardianDefeated;
        }
        state.awakening =
            LegendaryWeaponAwakening::Dormant;
        state.corrupted_kills = 0U;
        state.upgrade_flags = 0U;
        state.cosmetic =
            LegendaryWeaponCosmetic::LeviathanBone;
        state.astral_boss_defeated = false;
        state.major_boss_defeated = false;
        state.forge_ritual_complete = false;
        return state;
    }

    if (!quest_stage_at_least(
            state.quest_stage,
            LegendaryWeaponQuestStage::WeaponClaimed)) {
        state.quest_stage =
            LegendaryWeaponQuestStage::WeaponClaimed;
        state.map_fragments_collected =
            kLegendaryWeaponRequiredMapFragments;
    }
    state.awakening =
        derive_legendary_weapon_awakening(state);
    return state;
}

auto is_valid_legendary_weapon_progression_state(
    const LegendaryWeaponProgressionState& state) noexcept -> bool {
    return state ==
           sanitize_legendary_weapon_progression_state(
               state);
}

LegendaryWeaponProgression::LegendaryWeaponProgression(
    LegendaryWeaponProgressionState state) noexcept {
    load_state(state);
}

void LegendaryWeaponProgression::reset() noexcept {
    state_ = {};
}

void LegendaryWeaponProgression::load_state(
    LegendaryWeaponProgressionState state) noexcept {
    state_ =
        sanitize_legendary_weapon_progression_state(
            state);
}

auto LegendaryWeaponProgression::state() const noexcept
    -> LegendaryWeaponProgressionState {
    return state_;
}

auto LegendaryWeaponProgression::hear_rumor() noexcept -> bool {
    if (state_.quest_stage !=
        LegendaryWeaponQuestStage::NotStarted) {
        return false;
    }
    state_.quest_stage =
        LegendaryWeaponQuestStage::RumorHeard;
    return true;
}

auto LegendaryWeaponProgression::collect_map_fragment() noexcept
    -> bool {
    if (state_.quest_stage !=
            LegendaryWeaponQuestStage::RumorHeard ||
        state_.map_fragments_collected >=
            kLegendaryWeaponRequiredMapFragments) {
        return false;
    }
    ++state_.map_fragments_collected;
    if (state_.map_fragments_collected ==
        kLegendaryWeaponRequiredMapFragments) {
        state_.quest_stage =
            LegendaryWeaponQuestStage::MapFragmentsComplete;
    }
    return true;
}

auto LegendaryWeaponProgression::discover_forge() noexcept -> bool {
    if (state_.quest_stage !=
        LegendaryWeaponQuestStage::MapFragmentsComplete) {
        return false;
    }
    state_.quest_stage =
        LegendaryWeaponQuestStage::ForgeDiscovered;
    return true;
}

auto LegendaryWeaponProgression::defeat_guardian() noexcept -> bool {
    if (state_.quest_stage !=
        LegendaryWeaponQuestStage::ForgeDiscovered) {
        return false;
    }
    state_.quest_stage =
        LegendaryWeaponQuestStage::GuardianDefeated;
    return true;
}

auto LegendaryWeaponProgression::claim_weapon(
    std::uint64_t unique_weapon_id,
    std::uint32_t player_level,
    std::uint8_t strength) noexcept -> bool {
    if (state_.quest_stage !=
            LegendaryWeaponQuestStage::GuardianDefeated ||
        state_.weapon_owned ||
        unique_weapon_id == 0ULL ||
        !legendary_weapon_meets_acquisition_requirements(
            player_level,
            strength)) {
        return false;
    }
    state_.unique_weapon_id = unique_weapon_id;
    state_.weapon_owned = true;
    state_.quest_stage =
        LegendaryWeaponQuestStage::WeaponClaimed;
    refresh_awakening();
    return true;
}

auto LegendaryWeaponProgression::complete_first_combat() noexcept
    -> bool {
    if (!state_.weapon_owned ||
        state_.quest_stage !=
            LegendaryWeaponQuestStage::WeaponClaimed) {
        return false;
    }
    state_.quest_stage =
        LegendaryWeaponQuestStage::FirstCombatComplete;
    return true;
}

auto LegendaryWeaponProgression::record_corrupted_kills(
    std::uint32_t count) noexcept -> bool {
    if (!state_.weapon_owned || count == 0U) {
        return false;
    }
    const auto previous =
        state_.corrupted_kills;
    const auto remaining =
        kLegendaryWeaponMaximumCorruptedKills -
        state_.corrupted_kills;
    state_.corrupted_kills +=
        std::min(count, remaining);
    refresh_awakening();
    return state_.corrupted_kills != previous;
}

auto LegendaryWeaponProgression::record_astral_boss_defeat()
    noexcept -> bool {
    if (!state_.weapon_owned ||
        state_.astral_boss_defeated) {
        return false;
    }
    state_.astral_boss_defeated = true;
    refresh_awakening();
    return true;
}

auto LegendaryWeaponProgression::record_major_boss_defeat()
    noexcept -> bool {
    if (!state_.weapon_owned ||
        state_.major_boss_defeated) {
        return false;
    }
    state_.major_boss_defeated = true;
    refresh_awakening();
    return true;
}

auto LegendaryWeaponProgression::complete_forge_ritual()
    noexcept -> bool {
    if (!state_.weapon_owned ||
        state_.forge_ritual_complete) {
        return false;
    }
    state_.forge_ritual_complete = true;
    refresh_awakening();
    return true;
}

auto LegendaryWeaponProgression::set_cosmetic(
    LegendaryWeaponCosmetic cosmetic) noexcept -> bool {
    if (!state_.weapon_owned ||
        !is_known_legendary_weapon_cosmetic(cosmetic) ||
        state_.cosmetic == cosmetic) {
        return false;
    }
    state_.cosmetic = cosmetic;
    return true;
}

auto LegendaryWeaponProgression::unlock_upgrades(
    std::uint32_t upgrade_flags) noexcept -> bool {
    if (!state_.weapon_owned) {
        return false;
    }
    const auto previous =
        state_.upgrade_flags;
    state_.upgrade_flags |=
        upgrade_flags &
        kLegendaryWeaponKnownUpgradeMask;
    return state_.upgrade_flags != previous;
}

void LegendaryWeaponProgression::refresh_awakening() noexcept {
    // Je derive toujours l'eveil des preuves persistantes, sans sauvegarder
    // d'etat transitoire de combat ni accepter une regression manuelle.
    state_.awakening =
        derive_legendary_weapon_awakening(
            state_);
}

} // namespace valcraft
