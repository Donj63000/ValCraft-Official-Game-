#pragma once

#include "app/GameMode.h"
#include "gameplay/weapons/LegendaryWeaponProgression.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string_view>

namespace valcraft {

inline constexpr std::string_view kLegendaryWeaponQuestTitle =
    "LE FER QUI N'AURAIT JAMAIS DU ETRE FORGE";
inline constexpr std::size_t kLegendaryQuestMapFragmentCount = 3U;
inline constexpr std::size_t kLegendaryQuestEventCapacity = 32U;
inline constexpr std::uint8_t kLegendaryQuestTutorialSweepBit = 1U << 0U;
inline constexpr std::uint8_t kLegendaryQuestTutorialGuardBit = 1U << 1U;
inline constexpr std::uint8_t kLegendaryQuestTutorialChargeBit = 1U << 2U;
inline constexpr std::uint8_t kLegendaryQuestTutorialCompleteMask =
    kLegendaryQuestTutorialSweepBit |
    kLegendaryQuestTutorialGuardBit |
    kLegendaryQuestTutorialChargeBit;

using LegendaryQuestAnchorId = std::uint64_t;

enum class LegendaryQuestForgeSite : std::uint8_t {
    RemoteMountain = 0,
    VolcanicIsland = 1,
    RuinedIsland = 2,
};

enum class LegendaryQuestRumorSource : std::uint8_t {
    OldBlacksmith = 0,
    Sailor = 1,
    CrewMember = 2,
    RuinDocument = 3,
};

enum class LegendaryQuestAnchorKind : std::uint8_t {
    Rumor = 0,
    MapFragment = 1,
    Forge = 2,
    Guardian = 3,
    Blade = 4,
};

enum class LegendaryQuestMapClueKind : std::uint8_t {
    BlackIronEtching = 0,
    BrokenChainLedger = 1,
    AncientFurnaceRubbing = 2,
};

enum class LegendaryQuestBearing : std::uint8_t {
    North = 0,
    NorthEast = 1,
    East = 2,
    SouthEast = 3,
    South = 4,
    SouthWest = 5,
    West = 6,
    NorthWest = 7,
};

enum class LegendaryQuestDistanceBand : std::uint8_t {
    Near = 0,
    Distant = 1,
    VeryDistant = 2,
};

enum class LegendaryQuestForgeFeature : std::uint8_t {
    GiantTools = 0,
    BrokenChains = 1,
    GiantMoulds = 2,
    BlackMetalBlocks = 3,
    WallClawMarks = 4,
    AncientFurnace = 5,
    SealedRoom = 6,
};

inline constexpr std::array<LegendaryQuestForgeFeature, 7U>
    kLegendaryQuestForgeFeatures {
        LegendaryQuestForgeFeature::GiantTools,
        LegendaryQuestForgeFeature::BrokenChains,
        LegendaryQuestForgeFeature::GiantMoulds,
        LegendaryQuestForgeFeature::BlackMetalBlocks,
        LegendaryQuestForgeFeature::WallClawMarks,
        LegendaryQuestForgeFeature::AncientFurnace,
        LegendaryQuestForgeFeature::SealedRoom,
    };

struct LegendaryQuestWorldPoint {
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t z = 0;

    auto operator==(const LegendaryQuestWorldPoint&) const -> bool = default;
};

struct LegendaryQuestAnchor {
    LegendaryQuestAnchorId id = 0ULL;
    LegendaryQuestAnchorKind kind = LegendaryQuestAnchorKind::Rumor;
    LegendaryQuestWorldPoint position {};
    float discovery_radius = 0.0F;
    std::string_view marker_id {};

    auto operator==(const LegendaryQuestAnchor&) const -> bool = default;
};

struct LegendaryQuestMapClue {
    std::uint8_t fragment_index = 0U;
    LegendaryQuestMapClueKind kind =
        LegendaryQuestMapClueKind::BlackIronEtching;
    LegendaryQuestAnchor source {};
    LegendaryQuestBearing forge_bearing = LegendaryQuestBearing::North;
    LegendaryQuestDistanceBand forge_distance =
        LegendaryQuestDistanceBand::VeryDistant;
    std::string_view hint_id {};

    auto operator==(const LegendaryQuestMapClue&) const -> bool = default;
};

struct LegendaryQuestGuardianDefinition {
    float maximum_health = 300.0F;
    float armor = 42.0F;
    float stagger_capacity = 120.0F;
    float movement_speed = 1.65F;
    float vulnerable_zone_multiplier = 1.5F;
    bool teaches_armor = true;
    bool teaches_stagger = true;
    bool teaches_vulnerable_zones = true;

    auto operator==(const LegendaryQuestGuardianDefinition&) const
        -> bool = default;
};

struct LegendaryWeaponQuestLayout {
    std::uint64_t world_seed = 0ULL;
    GameMode game_mode = GameMode::ClassicAdventure;
    LegendaryQuestForgeSite forge_site =
        LegendaryQuestForgeSite::RemoteMountain;
    LegendaryQuestRumorSource rumor_source =
        LegendaryQuestRumorSource::OldBlacksmith;
    LegendaryQuestAnchor rumor {};
    std::array<
        LegendaryQuestMapClue,
        kLegendaryQuestMapFragmentCount> map_clues {};
    LegendaryQuestAnchor forge {};
    LegendaryQuestAnchor guardian {};
    LegendaryQuestAnchor blade {};
    LegendaryQuestGuardianDefinition guardian_definition {};
    std::uint64_t unique_weapon_id = 0ULL;
    std::uint64_t signature = 0ULL;

    auto operator==(const LegendaryWeaponQuestLayout&) const -> bool = default;
};

[[nodiscard]] auto generate_legendary_weapon_quest_layout(
    std::uint64_t world_seed,
    GameMode game_mode) noexcept
    -> std::optional<LegendaryWeaponQuestLayout>;
[[nodiscard]] auto is_valid_legendary_weapon_quest_layout(
    const LegendaryWeaponQuestLayout& layout) noexcept -> bool;

enum class LegendaryQuestAction : std::uint8_t {
    HearRumor = 0,
    CollectMapFragment = 1,
    DiscoverForge = 2,
    DefeatGuardian = 3,
    InteractWithBlade = 4,
    TutorialSweepHit = 5,
    TutorialGuardSucceeded = 6,
    TutorialChargedHit = 7,
    Count = 8,
};

enum class LegendaryQuestTutorialAction : std::uint8_t {
    Sweep = 0,
    Guard = 1,
    ChargedAttack = 2,
    None = 3,
};

enum class LegendaryQuestFailure : std::uint8_t {
    None = 0,
    NotConfigured,
    InvalidAction,
    InvalidProgressionState,
    TemporaryIssouSession,
    QuestAlreadyComplete,
    WrongQuestStage,
    WrongAnchor,
    WrongMapFragment,
    InvalidCombatEvidence,
    MissingCallback,
    ProgressionCommitRejected,
    CallbackThrew,
    FirstInteractionMustFail,
    RequirementsNotMet,
    InventoryRejected,
    InventoryRollbackFailed,
    TutorialObjectiveAlreadyComplete,
};

enum class LegendaryQuestEventType : std::uint8_t {
    RequestRejected = 0,
    TemporarySessionBlocked = 1,
    RumorHeard = 2,
    MapFragmentCollected = 3,
    MapCompleted = 4,
    ForgeDiscovered = 5,
    GuardianDefeated = 6,
    BladeRefused = 7,
    BladeRequirementsMissing = 8,
    InventoryFull = 9,
    WeaponAcquired = 10,
    TutorialEncounterRequested = 11,
    TutorialObjectiveCompleted = 12,
    QuestCompleted = 13,
    TransactionRolledBack = 14,
    TransactionRollbackFailed = 15,
    Count = 16,
};

enum class LegendaryQuestEventPriority : std::uint8_t {
    Presentation = 0,
    State = 1,
    Critical = 2,
};

struct LegendaryQuestRequest {
    LegendaryQuestAction action = LegendaryQuestAction::HearRumor;
    LegendaryQuestAnchorId anchor_id = 0ULL;
    std::uint8_t fragment_index = 0U;
    std::uint64_t combat_target_id = 0ULL;
    float combat_value = 0.0F;
};

struct LegendaryQuestContext {
    LegendaryWeaponProgressionState progression {};
    std::uint32_t player_level = 1U;
    std::uint8_t player_strength = 0U;
    bool temporary_issou_session = false;
};

struct LegendaryQuestCallbacks {
    std::function<bool()> commit_hear_rumor {};
    std::function<bool()> commit_map_fragment {};
    std::function<bool()> commit_forge_discovery {};
    std::function<bool()> commit_guardian_defeat {};

    // Je reserve l'objet dans l'inventaire avant de valider la progression.
    // Si la seconde etape echoue, le rollback doit retirer exactement cet id.
    std::function<bool(std::uint64_t)> try_commit_weapon_to_inventory {};
    std::function<bool(
        std::uint64_t,
        std::uint32_t,
        std::uint8_t)> commit_weapon_claim {};
    std::function<bool(std::uint64_t)> rollback_weapon_from_inventory {};

    std::function<bool()> commit_first_combat {};
};

struct LegendaryQuestEvent {
    std::uint64_t event_id = 0ULL;
    LegendaryQuestEventType type = LegendaryQuestEventType::RequestRejected;
    LegendaryQuestEventPriority priority =
        LegendaryQuestEventPriority::Presentation;
    LegendaryWeaponQuestStage stage_before =
        LegendaryWeaponQuestStage::NotStarted;
    LegendaryWeaponQuestStage stage_after =
        LegendaryWeaponQuestStage::NotStarted;
    LegendaryQuestFailure failure = LegendaryQuestFailure::None;
    LegendaryQuestAnchorId anchor_id = 0ULL;
    std::uint8_t fragment_index = 0U;
    LegendaryQuestTutorialAction tutorial_action =
        LegendaryQuestTutorialAction::None;
    std::uint8_t tutorial_completion_mask = 0U;
    std::string_view message_id {};
    std::string_view audio_id {};

    auto operator==(const LegendaryQuestEvent&) const -> bool = default;
};

struct LegendaryQuestEventOverflowStats {
    std::uint64_t rejected = 0ULL;
    std::uint64_t evicted = 0ULL;

    auto operator==(const LegendaryQuestEventOverflowStats&) const
        -> bool = default;
};

struct LegendaryQuestProcessResult {
    LegendaryQuestFailure failure = LegendaryQuestFailure::None;
    LegendaryWeaponQuestStage stage_before =
        LegendaryWeaponQuestStage::NotStarted;
    LegendaryWeaponQuestStage expected_stage_after =
        LegendaryWeaponQuestStage::NotStarted;
    std::uint8_t tutorial_completion_mask = 0U;
    std::uint64_t first_event_id = 0ULL;
    std::uint64_t last_event_id = 0ULL;
    std::uint8_t published_event_count = 0U;
    bool stage_advanced = false;
    bool inventory_committed = false;
    bool inventory_rolled_back = false;

    [[nodiscard]] constexpr auto accepted() const noexcept -> bool {
        return failure == LegendaryQuestFailure::None;
    }
};

struct LegendaryQuestRuntimeState {
    std::uint64_t layout_signature = 0ULL;
    std::uint8_t tutorial_completion_mask = 0U;
    bool blade_refusal_witnessed = false;

    auto operator==(const LegendaryQuestRuntimeState&) const -> bool = default;
};

struct LegendaryQuestTutorialObjective {
    LegendaryQuestTutorialAction action =
        LegendaryQuestTutorialAction::Sweep;
    bool complete = false;
    std::string_view label_id {};

    auto operator==(const LegendaryQuestTutorialObjective&) const
        -> bool = default;
};

struct LegendaryQuestPresentationState {
    bool valid = false;
    bool journal_visible = false;
    bool completed = false;
    bool world_marker_visible = false;
    bool requirements_visible = false;
    bool level_requirement_met = false;
    bool strength_requirement_met = false;
    bool blade_refusal_witnessed = false;
    LegendaryWeaponQuestStage stage =
        LegendaryWeaponQuestStage::NotStarted;
    LegendaryQuestForgeSite forge_site =
        LegendaryQuestForgeSite::RemoteMountain;
    LegendaryQuestAnchor target {};
    std::uint8_t fragments_collected = 0U;
    std::uint8_t fragments_required =
        static_cast<std::uint8_t>(kLegendaryQuestMapFragmentCount);
    std::uint32_t required_level = kLegendaryWeaponRequiredPlayerLevel;
    std::uint8_t required_strength =
        kLegendaryWeaponRequiredStrength;
    float completion_ratio = 0.0F;
    std::string_view title = kLegendaryWeaponQuestTitle;
    std::string_view objective_id {};
    std::array<LegendaryQuestTutorialObjective, 3U>
        tutorial_objectives {};

    auto operator==(const LegendaryQuestPresentationState&) const
        -> bool = default;
};

class LegendaryWeaponQuest {
public:
    LegendaryWeaponQuest() = default;
    LegendaryWeaponQuest(
        std::uint64_t world_seed,
        GameMode game_mode) noexcept;

    // Je refuse un mode inconnu sans toucher a la quete deja configuree.
    [[nodiscard]] auto configure(
        std::uint64_t world_seed,
        GameMode game_mode) noexcept -> bool;
    void reset_transient_progress() noexcept;
    [[nodiscard]] auto restore_runtime_state(
        const LegendaryQuestRuntimeState& state,
        const LegendaryWeaponProgressionState& progression) noexcept
        -> bool;

    [[nodiscard]] auto configured() const noexcept -> bool;
    [[nodiscard]] auto layout() const noexcept
        -> const LegendaryWeaponQuestLayout&;
    [[nodiscard]] auto runtime_state() const noexcept
        -> LegendaryQuestRuntimeState;

    [[nodiscard]] auto process(
        const LegendaryQuestRequest& request,
        const LegendaryQuestContext& context,
        const LegendaryQuestCallbacks& callbacks = {}) noexcept
        -> LegendaryQuestProcessResult;

    [[nodiscard]] auto presentation_state(
        const LegendaryWeaponProgressionState& progression,
        std::uint32_t player_level,
        std::uint8_t player_strength) const noexcept
        -> LegendaryQuestPresentationState;

    [[nodiscard]] auto events() const noexcept
        -> std::span<const LegendaryQuestEvent>;
    [[nodiscard]] auto drain_events(
        std::span<LegendaryQuestEvent> destination) noexcept
        -> std::size_t;
    void clear_events() noexcept;
    [[nodiscard]] auto event_overflow_stats() const noexcept
        -> const LegendaryQuestEventOverflowStats&;

private:
    [[nodiscard]] auto reject(
        LegendaryQuestFailure failure,
        const LegendaryQuestRequest& request,
        LegendaryWeaponQuestStage stage) noexcept
        -> LegendaryQuestProcessResult;
    [[nodiscard]] auto publish(
        LegendaryQuestEvent event) noexcept -> std::uint64_t;
    void record_publication(
        LegendaryQuestProcessResult& result,
        std::uint64_t event_id) const noexcept;

    LegendaryWeaponQuestLayout layout_ {};
    bool configured_ = false;
    bool blade_refusal_witnessed_ = false;
    std::uint8_t tutorial_completion_mask_ = 0U;
    std::array<LegendaryQuestEvent, kLegendaryQuestEventCapacity> events_ {};
    std::size_t event_count_ = 0U;
    std::uint64_t next_event_id_ = 1ULL;
    LegendaryQuestEventOverflowStats event_overflow_stats_ {};
};

} // namespace valcraft
