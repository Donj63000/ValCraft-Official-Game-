#include "gameplay/quests/LegendaryWeaponQuest.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace valcraft {

namespace {

constexpr std::uint64_t kLayoutSalt = 0xA0761D6478BD642FULL;
constexpr std::uint64_t kWeaponSalt = 0xE7037ED1A0B428DBULL;
constexpr std::uint64_t kSignatureSalt = 0x8EBC6AF09C88C6E3ULL;
constexpr std::int32_t kMaximumQuestCoordinate = 10'000;

[[nodiscard]] constexpr auto mix64(std::uint64_t value) noexcept
    -> std::uint64_t {
    value += 0x9E3779B97F4A7C15ULL;
    value =
        (value ^ (value >> 30U)) *
        0xBF58476D1CE4E5B9ULL;
    value =
        (value ^ (value >> 27U)) *
        0x94D049BB133111EBULL;
    return value ^ (value >> 31U);
}

[[nodiscard]] constexpr auto mode_salt(GameMode game_mode) noexcept
    -> std::uint64_t {
    return game_mode == GameMode::SeaAdventure
        ? 0xD1B54A32D192ED03ULL
        : 0xABC98388FB8FAC03ULL;
}

[[nodiscard]] constexpr auto sample(
    std::uint64_t world_seed,
    GameMode game_mode,
    std::uint64_t salt) noexcept -> std::uint64_t {
    return mix64(
        world_seed ^
        mode_salt(game_mode) ^
        salt);
}

[[nodiscard]] constexpr auto bounded_coordinate(
    std::uint64_t value,
    std::int32_t minimum,
    std::int32_t maximum) noexcept -> std::int32_t {
    const auto span =
        static_cast<std::uint64_t>(
            static_cast<std::int64_t>(maximum) -
            static_cast<std::int64_t>(minimum) +
            1LL);
    return static_cast<std::int32_t>(
        static_cast<std::int64_t>(minimum) +
        static_cast<std::int64_t>(value % span));
}

[[nodiscard]] constexpr auto signed_coordinate(
    std::uint64_t value,
    std::int32_t minimum_magnitude,
    std::int32_t maximum_magnitude) noexcept -> std::int32_t {
    const auto magnitude =
        bounded_coordinate(
            value >> 1U,
            minimum_magnitude,
            maximum_magnitude);
    return (value & 1ULL) != 0ULL
        ? magnitude
        : -magnitude;
}

[[nodiscard]] constexpr auto anchor_id(
    std::uint64_t world_seed,
    GameMode game_mode,
    std::uint8_t slot) noexcept -> LegendaryQuestAnchorId {
    // Je garde huit bits pour le role afin de garantir des ancres distinctes
    // meme si deux tirages deterministes partagent un prefixe.
    return
        (sample(world_seed, game_mode, kLayoutSalt) &
         0xFFFFFFFFFFFFFF00ULL) |
        static_cast<std::uint64_t>(slot);
}

[[nodiscard]] constexpr auto non_zero_hash(
    std::uint64_t value) noexcept -> std::uint64_t {
    const auto hashed = mix64(value);
    return hashed == 0ULL ? 1ULL : hashed;
}

[[nodiscard]] constexpr auto absolute_i64(std::int64_t value) noexcept
    -> std::int64_t {
    return value < 0LL ? -value : value;
}

[[nodiscard]] auto bearing_between(
    const LegendaryQuestWorldPoint& source,
    const LegendaryQuestWorldPoint& destination) noexcept
    -> LegendaryQuestBearing {
    const auto dx =
        static_cast<std::int64_t>(destination.x) -
        static_cast<std::int64_t>(source.x);
    const auto dz =
        static_cast<std::int64_t>(destination.z) -
        static_cast<std::int64_t>(source.z);
    const auto ax = absolute_i64(dx);
    const auto az = absolute_i64(dz);

    if (ax * 2LL <= az) {
        return dz < 0LL
            ? LegendaryQuestBearing::North
            : LegendaryQuestBearing::South;
    }
    if (az * 2LL <= ax) {
        return dx < 0LL
            ? LegendaryQuestBearing::West
            : LegendaryQuestBearing::East;
    }
    if (dx >= 0LL && dz < 0LL) {
        return LegendaryQuestBearing::NorthEast;
    }
    if (dx >= 0LL && dz >= 0LL) {
        return LegendaryQuestBearing::SouthEast;
    }
    if (dx < 0LL && dz >= 0LL) {
        return LegendaryQuestBearing::SouthWest;
    }
    return LegendaryQuestBearing::NorthWest;
}

[[nodiscard]] auto distance_band_between(
    const LegendaryQuestWorldPoint& source,
    const LegendaryQuestWorldPoint& destination) noexcept
    -> LegendaryQuestDistanceBand {
    const auto dx =
        static_cast<std::int64_t>(destination.x) -
        static_cast<std::int64_t>(source.x);
    const auto dz =
        static_cast<std::int64_t>(destination.z) -
        static_cast<std::int64_t>(source.z);
    const auto squared_distance = dx * dx + dz * dz;
    if (squared_distance <= 350LL * 350LL) {
        return LegendaryQuestDistanceBand::Near;
    }
    if (squared_distance <= 900LL * 900LL) {
        return LegendaryQuestDistanceBand::Distant;
    }
    return LegendaryQuestDistanceBand::VeryDistant;
}

[[nodiscard]] constexpr auto is_known_action(
    LegendaryQuestAction action) noexcept -> bool {
    return
        static_cast<std::uint8_t>(action) <
        static_cast<std::uint8_t>(
            LegendaryQuestAction::Count);
}

[[nodiscard]] constexpr auto tutorial_action_for(
    LegendaryQuestAction action) noexcept
    -> LegendaryQuestTutorialAction {
    switch (action) {
    case LegendaryQuestAction::TutorialSweepHit:
        return LegendaryQuestTutorialAction::Sweep;
    case LegendaryQuestAction::TutorialGuardSucceeded:
        return LegendaryQuestTutorialAction::Guard;
    case LegendaryQuestAction::TutorialChargedHit:
        return LegendaryQuestTutorialAction::ChargedAttack;
    default:
        return LegendaryQuestTutorialAction::None;
    }
}

[[nodiscard]] constexpr auto tutorial_bit_for(
    LegendaryQuestTutorialAction action) noexcept -> std::uint8_t {
    switch (action) {
    case LegendaryQuestTutorialAction::Sweep:
        return kLegendaryQuestTutorialSweepBit;
    case LegendaryQuestTutorialAction::Guard:
        return kLegendaryQuestTutorialGuardBit;
    case LegendaryQuestTutorialAction::ChargedAttack:
        return kLegendaryQuestTutorialChargeBit;
    case LegendaryQuestTutorialAction::None:
    default:
        return 0U;
    }
}

[[nodiscard]] constexpr auto event_priority(
    LegendaryQuestEventType type) noexcept
    -> LegendaryQuestEventPriority {
    switch (type) {
    case LegendaryQuestEventType::WeaponAcquired:
    case LegendaryQuestEventType::QuestCompleted:
    case LegendaryQuestEventType::TransactionRollbackFailed:
        return LegendaryQuestEventPriority::Critical;
    case LegendaryQuestEventType::TemporarySessionBlocked:
    case LegendaryQuestEventType::RumorHeard:
    case LegendaryQuestEventType::MapFragmentCollected:
    case LegendaryQuestEventType::MapCompleted:
    case LegendaryQuestEventType::ForgeDiscovered:
    case LegendaryQuestEventType::GuardianDefeated:
    case LegendaryQuestEventType::BladeRefused:
    case LegendaryQuestEventType::TutorialEncounterRequested:
    case LegendaryQuestEventType::TutorialObjectiveCompleted:
    case LegendaryQuestEventType::TransactionRolledBack:
        return LegendaryQuestEventPriority::State;
    case LegendaryQuestEventType::RequestRejected:
    case LegendaryQuestEventType::BladeRequirementsMissing:
    case LegendaryQuestEventType::InventoryFull:
    case LegendaryQuestEventType::Count:
    default:
        return LegendaryQuestEventPriority::Presentation;
    }
}

[[nodiscard]] constexpr auto event_message_id(
    LegendaryQuestEventType type) noexcept -> std::string_view {
    switch (type) {
    case LegendaryQuestEventType::RequestRejected:
        return "quest.leviathan.event.request_rejected";
    case LegendaryQuestEventType::TemporarySessionBlocked:
        return "quest.leviathan.event.issou_no_progress";
    case LegendaryQuestEventType::RumorHeard:
        return "quest.leviathan.event.rumor_heard";
    case LegendaryQuestEventType::MapFragmentCollected:
        return "quest.leviathan.event.map_fragment";
    case LegendaryQuestEventType::MapCompleted:
        return "quest.leviathan.event.map_completed";
    case LegendaryQuestEventType::ForgeDiscovered:
        return "quest.leviathan.event.forge_discovered";
    case LegendaryQuestEventType::GuardianDefeated:
        return "quest.leviathan.event.guardian_defeated";
    case LegendaryQuestEventType::BladeRefused:
        return "quest.leviathan.event.blade_barely_moves";
    case LegendaryQuestEventType::BladeRequirementsMissing:
        return "quest.leviathan.event.requirements_missing";
    case LegendaryQuestEventType::InventoryFull:
        return "quest.leviathan.event.inventory_full";
    case LegendaryQuestEventType::WeaponAcquired:
        return "quest.leviathan.event.weapon_acquired";
    case LegendaryQuestEventType::TutorialEncounterRequested:
        return "quest.leviathan.event.first_combat";
    case LegendaryQuestEventType::TutorialObjectiveCompleted:
        return "quest.leviathan.event.tutorial_objective";
    case LegendaryQuestEventType::QuestCompleted:
        return "quest.leviathan.event.quest_completed";
    case LegendaryQuestEventType::TransactionRolledBack:
        return "quest.leviathan.event.claim_rolled_back";
    case LegendaryQuestEventType::TransactionRollbackFailed:
        return "quest.leviathan.event.claim_rollback_failed";
    case LegendaryQuestEventType::Count:
    default:
        return {};
    }
}

[[nodiscard]] constexpr auto event_audio_id(
    LegendaryQuestEventType type) noexcept -> std::string_view {
    switch (type) {
    case LegendaryQuestEventType::BladeRefused:
        return "sfx.quest.leviathan.blade_refused_low";
    case LegendaryQuestEventType::WeaponAcquired:
        return "sfx.quest.leviathan.blade_claimed";
    case LegendaryQuestEventType::GuardianDefeated:
        return "sfx.quest.leviathan.guardian_fall";
    case LegendaryQuestEventType::MapCompleted:
        return "sfx.quest.map_completed";
    case LegendaryQuestEventType::QuestCompleted:
        return "sfx.quest.completed";
    default:
        return {};
    }
}

enum class CallbackOutcome : std::uint8_t {
    Accepted = 0,
    Rejected = 1,
    Threw = 2,
};

template <typename Callback, typename... Arguments>
[[nodiscard]] auto invoke_callback(
    const Callback& callback,
    Arguments&&... arguments) noexcept -> CallbackOutcome {
    try {
        return callback(std::forward<Arguments>(arguments)...)
            ? CallbackOutcome::Accepted
            : CallbackOutcome::Rejected;
    } catch (...) {
        return CallbackOutcome::Threw;
    }
}

[[nodiscard]] constexpr auto stage_at_least(
    LegendaryWeaponQuestStage current,
    LegendaryWeaponQuestStage expected) noexcept -> bool {
    return
        static_cast<std::uint8_t>(current) >=
        static_cast<std::uint8_t>(expected);
}

[[nodiscard]] constexpr auto stage_completion_units(
    const LegendaryWeaponProgressionState& progression,
    std::uint8_t tutorial_mask) noexcept -> std::uint8_t {
    if (progression.quest_stage ==
        LegendaryWeaponQuestStage::NotStarted) {
        return 0U;
    }

    std::uint8_t units = 1U;
    units = static_cast<std::uint8_t>(
        units +
        std::min<std::uint8_t>(
            progression.map_fragments_collected,
            static_cast<std::uint8_t>(
                kLegendaryQuestMapFragmentCount)));
    if (stage_at_least(
            progression.quest_stage,
            LegendaryWeaponQuestStage::ForgeDiscovered)) {
        ++units;
    }
    if (stage_at_least(
            progression.quest_stage,
            LegendaryWeaponQuestStage::GuardianDefeated)) {
        ++units;
    }
    if (stage_at_least(
            progression.quest_stage,
            LegendaryWeaponQuestStage::WeaponClaimed)) {
        ++units;
    }
    if (progression.quest_stage ==
        LegendaryWeaponQuestStage::FirstCombatComplete) {
        return 10U;
    }
    if ((tutorial_mask & kLegendaryQuestTutorialSweepBit) != 0U) {
        ++units;
    }
    if ((tutorial_mask & kLegendaryQuestTutorialGuardBit) != 0U) {
        ++units;
    }
    if ((tutorial_mask & kLegendaryQuestTutorialChargeBit) != 0U) {
        ++units;
    }
    return std::min<std::uint8_t>(units, 10U);
}

} // namespace

auto generate_legendary_weapon_quest_layout(
    std::uint64_t world_seed,
    GameMode game_mode) noexcept
    -> std::optional<LegendaryWeaponQuestLayout> {
    if (!is_known_game_mode(game_mode)) {
        return std::nullopt;
    }

    LegendaryWeaponQuestLayout result {};
    result.world_seed = world_seed;
    result.game_mode = game_mode;

    const auto site_roll =
        sample(world_seed, game_mode, 0x101ULL);
    const auto rumor_roll =
        sample(world_seed, game_mode, 0x102ULL);

    if (game_mode == GameMode::ClassicAdventure) {
        result.forge_site =
            LegendaryQuestForgeSite::RemoteMountain;
        result.rumor_source =
            (rumor_roll & 3ULL) == 0ULL
            ? LegendaryQuestRumorSource::RuinDocument
            : LegendaryQuestRumorSource::OldBlacksmith;
    } else {
        result.forge_site =
            (site_roll & 1ULL) == 0ULL
            ? LegendaryQuestForgeSite::VolcanicIsland
            : LegendaryQuestForgeSite::RuinedIsland;
        switch (rumor_roll % 3ULL) {
        case 0ULL:
            result.rumor_source =
                LegendaryQuestRumorSource::Sailor;
            break;
        case 1ULL:
            result.rumor_source =
                LegendaryQuestRumorSource::CrewMember;
            break;
        default:
            result.rumor_source =
                LegendaryQuestRumorSource::RuinDocument;
            break;
        }
    }

    const auto forge_x_roll =
        sample(world_seed, game_mode, 0x201ULL);
    const auto forge_z_roll =
        sample(world_seed, game_mode, 0x202ULL);
    const auto forge_y_roll =
        sample(world_seed, game_mode, 0x203ULL);

    LegendaryQuestWorldPoint forge_position {};
    if (game_mode == GameMode::ClassicAdventure) {
        forge_position = {
            signed_coordinate(forge_x_roll, 950, 1'450),
            bounded_coordinate(forge_y_roll, 104, 144),
            signed_coordinate(forge_z_roll, 650, 1'100),
        };
    } else {
        forge_position = {
            bounded_coordinate(forge_x_roll, 2'200, 3'600),
            bounded_coordinate(forge_y_roll, 70, 82),
            signed_coordinate(forge_z_roll, 480, 1'050),
        };
    }

    result.rumor = {
        anchor_id(world_seed, game_mode, 1U),
        LegendaryQuestAnchorKind::Rumor,
        {
            signed_coordinate(rumor_roll, 18, 64),
            game_mode == GameMode::SeaAdventure ? 68 : 72,
            signed_coordinate(rumor_roll >> 17U, 18, 64),
        },
        3.5F,
        "quest.leviathan.marker.rumor",
    };

    constexpr std::array<std::string_view, 3U> kClueHints {
        "quest.leviathan.hint.black_iron",
        "quest.leviathan.hint.broken_chains",
        "quest.leviathan.hint.ancient_furnace",
    };
    constexpr std::array<LegendaryQuestMapClueKind, 3U> kClueKinds {
        LegendaryQuestMapClueKind::BlackIronEtching,
        LegendaryQuestMapClueKind::BrokenChainLedger,
        LegendaryQuestMapClueKind::AncientFurnaceRubbing,
    };

    for (std::size_t index = 0U;
         index < kLegendaryQuestMapFragmentCount;
         ++index) {
        const auto numerator =
            static_cast<std::int64_t>(index + 1U);
        constexpr std::int64_t kDenominator = 4LL;
        const auto lateral_roll =
            sample(
                world_seed,
                game_mode,
                0x300ULL +
                static_cast<std::uint64_t>(index));
        const auto lateral_x =
            static_cast<std::int64_t>(
                signed_coordinate(lateral_roll, 45, 125));
        const auto lateral_z =
            static_cast<std::int64_t>(
                signed_coordinate(lateral_roll >> 11U, 40, 115));
        const auto base_x =
            static_cast<std::int64_t>(forge_position.x) *
            numerator /
            kDenominator;
        const auto base_z =
            static_cast<std::int64_t>(forge_position.z) *
            numerator /
            kDenominator;
        const LegendaryQuestWorldPoint fragment_position {
            static_cast<std::int32_t>(base_x + lateral_x),
            game_mode == GameMode::SeaAdventure
                ? bounded_coordinate(lateral_roll >> 24U, 68, 78)
                : bounded_coordinate(lateral_roll >> 24U, 74, 102),
            static_cast<std::int32_t>(base_z + lateral_z),
        };

        const LegendaryQuestAnchor source {
            anchor_id(
                world_seed,
                game_mode,
                static_cast<std::uint8_t>(2U + index)),
            LegendaryQuestAnchorKind::MapFragment,
            fragment_position,
            2.75F,
            "quest.leviathan.marker.map_fragment",
        };
        result.map_clues[index] = {
            static_cast<std::uint8_t>(index),
            kClueKinds[index],
            source,
            bearing_between(fragment_position, forge_position),
            distance_band_between(fragment_position, forge_position),
            kClueHints[index],
        };
    }

    result.forge = {
        anchor_id(world_seed, game_mode, 5U),
        LegendaryQuestAnchorKind::Forge,
        forge_position,
        18.0F,
        result.forge_site ==
                LegendaryQuestForgeSite::RemoteMountain
            ? "quest.leviathan.marker.mountain_forge"
            : result.forge_site ==
                    LegendaryQuestForgeSite::VolcanicIsland
                ? "quest.leviathan.marker.volcanic_forge"
                : "quest.leviathan.marker.ruined_island_forge",
    };
    result.guardian = {
        anchor_id(world_seed, game_mode, 6U),
        LegendaryQuestAnchorKind::Guardian,
        {
            forge_position.x - 7,
            forge_position.y,
            forge_position.z + 2,
        },
        5.0F,
        "quest.leviathan.marker.guardian",
    };
    result.blade = {
        anchor_id(world_seed, game_mode, 7U),
        LegendaryQuestAnchorKind::Blade,
        {
            forge_position.x + 5,
            forge_position.y + 1,
            forge_position.z - 4,
        },
        2.5F,
        "quest.leviathan.marker.blade",
    };
    result.unique_weapon_id =
        non_zero_hash(
            world_seed ^
            mode_salt(game_mode) ^
            kWeaponSalt);
    result.signature =
        non_zero_hash(
            world_seed ^
            mode_salt(game_mode) ^
            kSignatureSalt);
    return result;
}

auto is_valid_legendary_weapon_quest_layout(
    const LegendaryWeaponQuestLayout& layout) noexcept -> bool {
    const auto expected =
        generate_legendary_weapon_quest_layout(
            layout.world_seed,
            layout.game_mode);
    if (!expected.has_value() ||
        *expected != layout ||
        layout.unique_weapon_id == 0ULL ||
        layout.signature == 0ULL) {
        return false;
    }

    const auto point_is_bounded =
        [](const LegendaryQuestWorldPoint& point) noexcept {
            return
                point.x >= -kMaximumQuestCoordinate &&
                point.x <= kMaximumQuestCoordinate &&
                point.y >= 0 &&
                point.y <= kMaximumQuestCoordinate &&
                point.z >= -kMaximumQuestCoordinate &&
                point.z <= kMaximumQuestCoordinate;
        };
    if (!point_is_bounded(layout.rumor.position) ||
        !point_is_bounded(layout.forge.position) ||
        !point_is_bounded(layout.guardian.position) ||
        !point_is_bounded(layout.blade.position)) {
        return false;
    }
    for (const auto& clue : layout.map_clues) {
        if (!point_is_bounded(clue.source.position)) {
            return false;
        }
    }
    return true;
}

LegendaryWeaponQuest::LegendaryWeaponQuest(
    std::uint64_t world_seed,
    GameMode game_mode) noexcept {
    static_cast<void>(
        configure(world_seed, game_mode));
}

auto LegendaryWeaponQuest::configure(
    std::uint64_t world_seed,
    GameMode game_mode) noexcept -> bool {
    const auto generated =
        generate_legendary_weapon_quest_layout(
            world_seed,
            game_mode);
    if (!generated.has_value()) {
        return false;
    }

    layout_ = *generated;
    configured_ = true;
    blade_refusal_witnessed_ = false;
    tutorial_completion_mask_ = 0U;
    event_count_ = 0U;
    next_event_id_ = 1ULL;
    event_overflow_stats_ = {};
    return true;
}

void LegendaryWeaponQuest::reset_transient_progress() noexcept {
    blade_refusal_witnessed_ = false;
    tutorial_completion_mask_ = 0U;
    clear_events();
}

auto LegendaryWeaponQuest::restore_runtime_state(
    const LegendaryQuestRuntimeState& state,
    const LegendaryWeaponProgressionState& progression) noexcept
    -> bool {
    if (!configured_ ||
        state.layout_signature != layout_.signature ||
        !is_valid_legendary_weapon_progression_state(
            progression) ||
        (state.tutorial_completion_mask &
         static_cast<std::uint8_t>(
             ~kLegendaryQuestTutorialCompleteMask)) != 0U) {
        return false;
    }

    const auto stage = progression.quest_stage;
    if (state.blade_refusal_witnessed &&
        !stage_at_least(
            stage,
            LegendaryWeaponQuestStage::GuardianDefeated)) {
        return false;
    }
    const auto tutorial_stage =
        stage == LegendaryWeaponQuestStage::WeaponClaimed ||
        stage == LegendaryWeaponQuestStage::FirstCombatComplete;
    if (!tutorial_stage &&
        state.tutorial_completion_mask != 0U) {
        return false;
    }

    blade_refusal_witnessed_ =
        state.blade_refusal_witnessed;
    tutorial_completion_mask_ =
        stage ==
            LegendaryWeaponQuestStage::FirstCombatComplete
        ? kLegendaryQuestTutorialCompleteMask
        : state.tutorial_completion_mask;
    return true;
}

auto LegendaryWeaponQuest::configured() const noexcept -> bool {
    return configured_;
}

auto LegendaryWeaponQuest::layout() const noexcept
    -> const LegendaryWeaponQuestLayout& {
    return layout_;
}

auto LegendaryWeaponQuest::runtime_state() const noexcept
    -> LegendaryQuestRuntimeState {
    return {
        configured_ ? layout_.signature : 0ULL,
        tutorial_completion_mask_,
        blade_refusal_witnessed_,
    };
}

auto LegendaryWeaponQuest::process(
    const LegendaryQuestRequest& request,
    const LegendaryQuestContext& context,
    const LegendaryQuestCallbacks& callbacks) noexcept
    -> LegendaryQuestProcessResult {
    const auto stage = context.progression.quest_stage;
    if (!configured_) {
        return reject(
            LegendaryQuestFailure::NotConfigured,
            request,
            stage);
    }
    if (!is_known_action(request.action)) {
        return reject(
            LegendaryQuestFailure::InvalidAction,
            request,
            stage);
    }
    if (context.temporary_issou_session) {
        return reject(
            LegendaryQuestFailure::TemporaryIssouSession,
            request,
            stage);
    }
    if (!is_valid_legendary_weapon_progression_state(
            context.progression)) {
        return reject(
            LegendaryQuestFailure::InvalidProgressionState,
            request,
            stage);
    }
    if (stage ==
        LegendaryWeaponQuestStage::FirstCombatComplete) {
        return reject(
            LegendaryQuestFailure::QuestAlreadyComplete,
            request,
            stage);
    }

    LegendaryQuestProcessResult result {};
    result.stage_before = stage;
    result.expected_stage_after = stage;
    result.tutorial_completion_mask =
        tutorial_completion_mask_;

    const auto emit =
        [this, &result, &request, stage](
            LegendaryQuestEventType type,
            LegendaryWeaponQuestStage stage_after,
            LegendaryQuestFailure failure =
                LegendaryQuestFailure::None,
            LegendaryQuestTutorialAction tutorial_action =
                LegendaryQuestTutorialAction::None) noexcept {
            LegendaryQuestEvent event {};
            event.type = type;
            event.priority = event_priority(type);
            event.stage_before = stage;
            event.stage_after = stage_after;
            event.failure = failure;
            event.anchor_id = request.anchor_id;
            event.fragment_index = request.fragment_index;
            event.tutorial_action = tutorial_action;
            event.tutorial_completion_mask =
                tutorial_completion_mask_;
            const auto event_id = publish(event);
            record_publication(result, event_id);
        };

    const auto reject_here =
        [this, &request, stage](
            LegendaryQuestFailure failure) noexcept {
            return reject(failure, request, stage);
        };

    switch (request.action) {
    case LegendaryQuestAction::HearRumor: {
        if (stage !=
            LegendaryWeaponQuestStage::NotStarted) {
            return reject_here(
                LegendaryQuestFailure::WrongQuestStage);
        }
        if (request.anchor_id != layout_.rumor.id) {
            return reject_here(
                LegendaryQuestFailure::WrongAnchor);
        }
        if (!callbacks.commit_hear_rumor) {
            return reject_here(
                LegendaryQuestFailure::MissingCallback);
        }
        const auto outcome =
            invoke_callback(callbacks.commit_hear_rumor);
        if (outcome != CallbackOutcome::Accepted) {
            return reject_here(
                outcome == CallbackOutcome::Threw
                ? LegendaryQuestFailure::CallbackThrew
                : LegendaryQuestFailure::
                    ProgressionCommitRejected);
        }
        result.expected_stage_after =
            LegendaryWeaponQuestStage::RumorHeard;
        result.stage_advanced = true;
        emit(
            LegendaryQuestEventType::RumorHeard,
            result.expected_stage_after);
        return result;
    }
    case LegendaryQuestAction::CollectMapFragment: {
        if (stage !=
            LegendaryWeaponQuestStage::RumorHeard) {
            return reject_here(
                LegendaryQuestFailure::WrongQuestStage);
        }
        const auto expected_index =
            context.progression.map_fragments_collected;
        if (expected_index >=
                kLegendaryQuestMapFragmentCount ||
            request.fragment_index != expected_index) {
            return reject_here(
                LegendaryQuestFailure::WrongMapFragment);
        }
        const auto index =
            static_cast<std::size_t>(expected_index);
        if (request.anchor_id !=
            layout_.map_clues[index].source.id) {
            return reject_here(
                LegendaryQuestFailure::WrongAnchor);
        }
        if (!callbacks.commit_map_fragment) {
            return reject_here(
                LegendaryQuestFailure::MissingCallback);
        }
        const auto outcome =
            invoke_callback(
                callbacks.commit_map_fragment);
        if (outcome != CallbackOutcome::Accepted) {
            return reject_here(
                outcome == CallbackOutcome::Threw
                ? LegendaryQuestFailure::CallbackThrew
                : LegendaryQuestFailure::
                    ProgressionCommitRejected);
        }
        const auto map_complete =
            expected_index + 1U ==
            static_cast<std::uint8_t>(
                kLegendaryQuestMapFragmentCount);
        result.expected_stage_after =
            map_complete
            ? LegendaryWeaponQuestStage::
                MapFragmentsComplete
            : LegendaryWeaponQuestStage::RumorHeard;
        result.stage_advanced =
            result.expected_stage_after != stage;
        emit(
            LegendaryQuestEventType::MapFragmentCollected,
            result.expected_stage_after);
        if (map_complete) {
            emit(
                LegendaryQuestEventType::MapCompleted,
                result.expected_stage_after);
        }
        return result;
    }
    case LegendaryQuestAction::DiscoverForge: {
        if (stage !=
            LegendaryWeaponQuestStage::
                MapFragmentsComplete) {
            return reject_here(
                LegendaryQuestFailure::WrongQuestStage);
        }
        if (request.anchor_id != layout_.forge.id) {
            return reject_here(
                LegendaryQuestFailure::WrongAnchor);
        }
        if (!callbacks.commit_forge_discovery) {
            return reject_here(
                LegendaryQuestFailure::MissingCallback);
        }
        const auto outcome =
            invoke_callback(
                callbacks.commit_forge_discovery);
        if (outcome != CallbackOutcome::Accepted) {
            return reject_here(
                outcome == CallbackOutcome::Threw
                ? LegendaryQuestFailure::CallbackThrew
                : LegendaryQuestFailure::
                    ProgressionCommitRejected);
        }
        result.expected_stage_after =
            LegendaryWeaponQuestStage::ForgeDiscovered;
        result.stage_advanced = true;
        emit(
            LegendaryQuestEventType::ForgeDiscovered,
            result.expected_stage_after);
        return result;
    }
    case LegendaryQuestAction::DefeatGuardian: {
        if (stage !=
            LegendaryWeaponQuestStage::ForgeDiscovered) {
            return reject_here(
                LegendaryQuestFailure::WrongQuestStage);
        }
        if (request.anchor_id != layout_.guardian.id) {
            return reject_here(
                LegendaryQuestFailure::WrongAnchor);
        }
        if (!callbacks.commit_guardian_defeat) {
            return reject_here(
                LegendaryQuestFailure::MissingCallback);
        }
        const auto outcome =
            invoke_callback(
                callbacks.commit_guardian_defeat);
        if (outcome != CallbackOutcome::Accepted) {
            return reject_here(
                outcome == CallbackOutcome::Threw
                ? LegendaryQuestFailure::CallbackThrew
                : LegendaryQuestFailure::
                    ProgressionCommitRejected);
        }
        result.expected_stage_after =
            LegendaryWeaponQuestStage::GuardianDefeated;
        result.stage_advanced = true;
        emit(
            LegendaryQuestEventType::GuardianDefeated,
            result.expected_stage_after);
        return result;
    }
    case LegendaryQuestAction::InteractWithBlade: {
        if (stage !=
            LegendaryWeaponQuestStage::GuardianDefeated) {
            return reject_here(
                LegendaryQuestFailure::WrongQuestStage);
        }
        if (request.anchor_id != layout_.blade.id) {
            return reject_here(
                LegendaryQuestFailure::WrongAnchor);
        }
        if (!blade_refusal_witnessed_) {
            // Je rends le premier echec obligatoire : il installe le poids de
            // l'arme sans risquer de dupliquer l'objet dans l'inventaire.
            blade_refusal_witnessed_ = true;
            result.failure =
                LegendaryQuestFailure::
                    FirstInteractionMustFail;
            result.tutorial_completion_mask =
                tutorial_completion_mask_;
            emit(
                LegendaryQuestEventType::BladeRefused,
                stage,
                result.failure);
            return result;
        }
        if (!legendary_weapon_meets_acquisition_requirements(
                context.player_level,
                context.player_strength)) {
            result.failure =
                LegendaryQuestFailure::RequirementsNotMet;
            emit(
                LegendaryQuestEventType::
                    BladeRequirementsMissing,
                stage,
                result.failure);
            return result;
        }
        if (!callbacks.try_commit_weapon_to_inventory ||
            !callbacks.commit_weapon_claim ||
            !callbacks.rollback_weapon_from_inventory) {
            return reject_here(
                LegendaryQuestFailure::MissingCallback);
        }

        const auto inventory_outcome =
            invoke_callback(
                callbacks.try_commit_weapon_to_inventory,
                layout_.unique_weapon_id);
        if (inventory_outcome ==
            CallbackOutcome::Rejected) {
            result.failure =
                LegendaryQuestFailure::InventoryRejected;
            emit(
                LegendaryQuestEventType::InventoryFull,
                stage,
                result.failure);
            return result;
        }
        if (inventory_outcome ==
            CallbackOutcome::Threw) {
            const auto rollback_outcome =
                invoke_callback(
                    callbacks.rollback_weapon_from_inventory,
                    layout_.unique_weapon_id);
            result.inventory_rolled_back =
                rollback_outcome ==
                CallbackOutcome::Accepted;
            result.failure =
                result.inventory_rolled_back
                ? LegendaryQuestFailure::CallbackThrew
                : LegendaryQuestFailure::
                    InventoryRollbackFailed;
            emit(
                result.inventory_rolled_back
                    ? LegendaryQuestEventType::
                        TransactionRolledBack
                    : LegendaryQuestEventType::
                        TransactionRollbackFailed,
                stage,
                result.failure);
            return result;
        }

        result.inventory_committed = true;
        const auto claim_outcome =
            invoke_callback(
                callbacks.commit_weapon_claim,
                layout_.unique_weapon_id,
                context.player_level,
                context.player_strength);
        if (claim_outcome != CallbackOutcome::Accepted) {
            const auto rollback_outcome =
                invoke_callback(
                    callbacks.rollback_weapon_from_inventory,
                    layout_.unique_weapon_id);
            result.inventory_rolled_back =
                rollback_outcome ==
                CallbackOutcome::Accepted;
            if (!result.inventory_rolled_back) {
                result.failure =
                    LegendaryQuestFailure::
                        InventoryRollbackFailed;
                emit(
                    LegendaryQuestEventType::
                        TransactionRollbackFailed,
                    stage,
                    result.failure);
                return result;
            }
            result.failure =
                claim_outcome == CallbackOutcome::Threw
                ? LegendaryQuestFailure::CallbackThrew
                : LegendaryQuestFailure::
                    ProgressionCommitRejected;
            emit(
                LegendaryQuestEventType::
                    TransactionRolledBack,
                stage,
                result.failure);
            return result;
        }

        result.expected_stage_after =
            LegendaryWeaponQuestStage::WeaponClaimed;
        result.stage_advanced = true;
        emit(
            LegendaryQuestEventType::WeaponAcquired,
            result.expected_stage_after);
        emit(
            LegendaryQuestEventType::
                TutorialEncounterRequested,
            result.expected_stage_after);
        return result;
    }
    case LegendaryQuestAction::TutorialSweepHit:
    case LegendaryQuestAction::TutorialGuardSucceeded:
    case LegendaryQuestAction::TutorialChargedHit: {
        if (stage !=
            LegendaryWeaponQuestStage::WeaponClaimed) {
            return reject_here(
                LegendaryQuestFailure::WrongQuestStage);
        }
        if (request.combat_target_id == 0ULL ||
            !std::isfinite(request.combat_value) ||
            request.combat_value <= 0.0F) {
            return reject_here(
                LegendaryQuestFailure::
                    InvalidCombatEvidence);
        }
        const auto tutorial_action =
            tutorial_action_for(request.action);
        const auto tutorial_bit =
            tutorial_bit_for(tutorial_action);
        if ((tutorial_completion_mask_ &
             tutorial_bit) != 0U) {
            return reject_here(
                LegendaryQuestFailure::
                    TutorialObjectiveAlreadyComplete);
        }

        const auto next_mask =
            static_cast<std::uint8_t>(
                tutorial_completion_mask_ |
                tutorial_bit);
        const auto completes_tutorial =
            next_mask ==
            kLegendaryQuestTutorialCompleteMask;
        if (completes_tutorial) {
            if (!callbacks.commit_first_combat) {
                return reject_here(
                    LegendaryQuestFailure::
                        MissingCallback);
            }
            const auto outcome =
                invoke_callback(
                    callbacks.commit_first_combat);
            if (outcome != CallbackOutcome::Accepted) {
                return reject_here(
                    outcome == CallbackOutcome::Threw
                    ? LegendaryQuestFailure::CallbackThrew
                    : LegendaryQuestFailure::
                        ProgressionCommitRejected);
            }
        }

        tutorial_completion_mask_ = next_mask;
        result.tutorial_completion_mask =
            tutorial_completion_mask_;
        result.expected_stage_after =
            completes_tutorial
            ? LegendaryWeaponQuestStage::
                FirstCombatComplete
            : stage;
        result.stage_advanced = completes_tutorial;
        emit(
            LegendaryQuestEventType::
                TutorialObjectiveCompleted,
            result.expected_stage_after,
            LegendaryQuestFailure::None,
            tutorial_action);
        if (completes_tutorial) {
            emit(
                LegendaryQuestEventType::QuestCompleted,
                result.expected_stage_after,
                LegendaryQuestFailure::None,
                tutorial_action);
        }
        return result;
    }
    case LegendaryQuestAction::Count:
    default:
        return reject_here(
            LegendaryQuestFailure::InvalidAction);
    }
}

auto LegendaryWeaponQuest::presentation_state(
    const LegendaryWeaponProgressionState& progression,
    std::uint32_t player_level,
    std::uint8_t player_strength) const noexcept
    -> LegendaryQuestPresentationState {
    LegendaryQuestPresentationState result {};
    result.tutorial_objectives = {{
        {
            LegendaryQuestTutorialAction::Sweep,
            false,
            "quest.leviathan.tutorial.sweep",
        },
        {
            LegendaryQuestTutorialAction::Guard,
            false,
            "quest.leviathan.tutorial.guard",
        },
        {
            LegendaryQuestTutorialAction::ChargedAttack,
            false,
            "quest.leviathan.tutorial.charge",
        },
    }};

    if (!configured_ ||
        !is_valid_legendary_weapon_progression_state(
            progression)) {
        return result;
    }

    result.valid = true;
    result.stage = progression.quest_stage;
    result.forge_site = layout_.forge_site;
    result.fragments_collected =
        progression.map_fragments_collected;
    result.level_requirement_met =
        player_level >=
        kLegendaryWeaponRequiredPlayerLevel;
    result.strength_requirement_met =
        player_strength >=
        kLegendaryWeaponRequiredStrength;
    result.blade_refusal_witnessed =
        blade_refusal_witnessed_;
    result.completed =
        progression.quest_stage ==
        LegendaryWeaponQuestStage::FirstCombatComplete;
    result.journal_visible =
        progression.quest_stage !=
        LegendaryWeaponQuestStage::NotStarted;

    const auto completed_tutorial_mask =
        result.completed
        ? kLegendaryQuestTutorialCompleteMask
        : tutorial_completion_mask_;
    result.tutorial_objectives[0].complete =
        (completed_tutorial_mask &
         kLegendaryQuestTutorialSweepBit) != 0U;
    result.tutorial_objectives[1].complete =
        (completed_tutorial_mask &
         kLegendaryQuestTutorialGuardBit) != 0U;
    result.tutorial_objectives[2].complete =
        (completed_tutorial_mask &
         kLegendaryQuestTutorialChargeBit) != 0U;
    result.completion_ratio =
        static_cast<float>(
            stage_completion_units(
                progression,
                completed_tutorial_mask)) /
        10.0F;

    switch (progression.quest_stage) {
    case LegendaryWeaponQuestStage::NotStarted:
        result.target = layout_.rumor;
        result.world_marker_visible = false;
        result.objective_id =
            "quest.leviathan.objective.discover_rumor";
        break;
    case LegendaryWeaponQuestStage::RumorHeard: {
        const auto fragment_index =
            std::min<std::size_t>(
                progression.map_fragments_collected,
                kLegendaryQuestMapFragmentCount - 1U);
        result.target =
            layout_.map_clues[fragment_index].source;
        result.world_marker_visible = true;
        result.objective_id =
            layout_.map_clues[fragment_index].hint_id;
        break;
    }
    case LegendaryWeaponQuestStage::MapFragmentsComplete:
        result.target = layout_.forge;
        result.world_marker_visible = true;
        result.objective_id =
            layout_.forge_site ==
                    LegendaryQuestForgeSite::RemoteMountain
                ? "quest.leviathan.objective.find_mountain_forge"
                : layout_.forge_site ==
                        LegendaryQuestForgeSite::VolcanicIsland
                    ? "quest.leviathan.objective.find_volcanic_island"
                    : "quest.leviathan.objective.find_ruined_island";
        break;
    case LegendaryWeaponQuestStage::ForgeDiscovered:
        result.target = layout_.guardian;
        result.world_marker_visible = true;
        result.objective_id =
            "quest.leviathan.objective.defeat_heavy_guardian";
        break;
    case LegendaryWeaponQuestStage::GuardianDefeated:
        result.target = layout_.blade;
        result.world_marker_visible = true;
        result.requirements_visible =
            blade_refusal_witnessed_;
        if (!blade_refusal_witnessed_) {
            result.objective_id =
                "quest.leviathan.objective.try_blade";
        } else if (
            !result.level_requirement_met ||
            !result.strength_requirement_met) {
            result.objective_id =
                "quest.leviathan.objective.gain_required_power";
        } else {
            result.objective_id =
                "quest.leviathan.objective.claim_blade";
        }
        break;
    case LegendaryWeaponQuestStage::WeaponClaimed:
        result.target = layout_.guardian;
        result.world_marker_visible = true;
        if (!result.tutorial_objectives[0].complete) {
            result.objective_id =
                result.tutorial_objectives[0].label_id;
        } else if (!result.tutorial_objectives[1].complete) {
            result.objective_id =
                result.tutorial_objectives[1].label_id;
        } else {
            result.objective_id =
                result.tutorial_objectives[2].label_id;
        }
        break;
    case LegendaryWeaponQuestStage::FirstCombatComplete:
        result.world_marker_visible = false;
        result.objective_id =
            "quest.leviathan.objective.complete";
        break;
    default:
        break;
    }
    return result;
}

auto LegendaryWeaponQuest::events() const noexcept
    -> std::span<const LegendaryQuestEvent> {
    return {
        events_.data(),
        event_count_,
    };
}

auto LegendaryWeaponQuest::drain_events(
    std::span<LegendaryQuestEvent> destination) noexcept
    -> std::size_t {
    const auto drained =
        std::min(destination.size(), event_count_);
    std::copy_n(
        events_.begin(),
        drained,
        destination.begin());
    std::move(
        events_.begin() +
            static_cast<std::ptrdiff_t>(drained),
        events_.begin() +
            static_cast<std::ptrdiff_t>(event_count_),
        events_.begin());
    event_count_ -= drained;
    return drained;
}

void LegendaryWeaponQuest::clear_events() noexcept {
    event_count_ = 0U;
}

auto LegendaryWeaponQuest::event_overflow_stats() const noexcept
    -> const LegendaryQuestEventOverflowStats& {
    return event_overflow_stats_;
}

auto LegendaryWeaponQuest::reject(
    LegendaryQuestFailure failure,
    const LegendaryQuestRequest& request,
    LegendaryWeaponQuestStage stage) noexcept
    -> LegendaryQuestProcessResult {
    LegendaryQuestProcessResult result {};
    result.failure = failure;
    result.stage_before = stage;
    result.expected_stage_after = stage;
    result.tutorial_completion_mask =
        tutorial_completion_mask_;

    LegendaryQuestEvent event {};
    event.type =
        failure ==
            LegendaryQuestFailure::TemporaryIssouSession
        ? LegendaryQuestEventType::TemporarySessionBlocked
        : LegendaryQuestEventType::RequestRejected;
    event.priority = event_priority(event.type);
    event.stage_before = stage;
    event.stage_after = stage;
    event.failure = failure;
    event.anchor_id = request.anchor_id;
    event.fragment_index = request.fragment_index;
    event.tutorial_action =
        tutorial_action_for(request.action);
    event.tutorial_completion_mask =
        tutorial_completion_mask_;
    record_publication(result, publish(event));
    return result;
}

auto LegendaryWeaponQuest::publish(
    LegendaryQuestEvent event) noexcept -> std::uint64_t {
    if (event.type == LegendaryQuestEventType::Count) {
        ++event_overflow_stats_.rejected;
        return 0ULL;
    }

    if (event_count_ == events_.size()) {
        const auto incoming_priority =
            static_cast<std::uint8_t>(event.priority);
        auto eviction_index = events_.size();
        for (std::size_t index = 0U;
             index < event_count_;
             ++index) {
            if (static_cast<std::uint8_t>(
                    events_[index].priority) <=
                incoming_priority) {
                eviction_index = index;
                break;
            }
        }
        if (eviction_index == events_.size()) {
            ++event_overflow_stats_.rejected;
            return 0ULL;
        }
        std::move(
            events_.begin() +
                static_cast<std::ptrdiff_t>(
                    eviction_index + 1U),
            events_.begin() +
                static_cast<std::ptrdiff_t>(event_count_),
            events_.begin() +
                static_cast<std::ptrdiff_t>(
                    eviction_index));
        --event_count_;
        ++event_overflow_stats_.evicted;
    }

    event.event_id = next_event_id_;
    if (next_event_id_ ==
        std::numeric_limits<std::uint64_t>::max()) {
        next_event_id_ = 1ULL;
    } else {
        ++next_event_id_;
    }
    event.priority = event_priority(event.type);
    if (event.message_id.empty()) {
        event.message_id =
            event_message_id(event.type);
    }
    if (event.audio_id.empty()) {
        event.audio_id =
            event_audio_id(event.type);
    }
    events_[event_count_] = event;
    ++event_count_;
    return event.event_id;
}

void LegendaryWeaponQuest::record_publication(
    LegendaryQuestProcessResult& result,
    std::uint64_t event_id) const noexcept {
    if (event_id == 0ULL) {
        return;
    }
    if (result.first_event_id == 0ULL) {
        result.first_event_id = event_id;
    }
    result.last_event_id = event_id;
    if (result.published_event_count <
        std::numeric_limits<std::uint8_t>::max()) {
        ++result.published_event_count;
    }
}

} // namespace valcraft
