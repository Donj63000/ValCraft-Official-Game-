#pragma once

#include "world/BackroomsGenerator.h"

#include <glm/vec3.hpp>

#include <array>
#include <cstddef>
#include <cstdint>

namespace valcraft {

class World;

inline constexpr int kBackroomsJackNavigationChunkRadius = 2;
inline constexpr int kBackroomsJackNavigationChunkSide =
    kBackroomsJackNavigationChunkRadius * 2 + 1;
// Je garde l'A* compact mais j'observe un anneau supplémentaire pour placer
// les apparitions lointaines uniquement dans des chunks réellement publiés.
inline constexpr int kBackroomsJackReadinessChunkRadius = 3;
inline constexpr int kBackroomsJackReadinessChunkSide =
    kBackroomsJackReadinessChunkRadius * 2 + 1;
inline constexpr int kBackroomsJackNavigationSide =
    kChunkSizeX * kBackroomsJackNavigationChunkSide;
inline constexpr std::size_t kBackroomsJackNavigationCellCount =
    static_cast<std::size_t>(
        kBackroomsJackNavigationSide *
        kBackroomsJackNavigationSide);
inline constexpr std::size_t kBackroomsJackReadinessCellCount =
    static_cast<std::size_t>(
        kBackroomsJackReadinessChunkSide *
        kBackroomsJackReadinessChunkSide);
inline constexpr std::size_t kBackroomsJackMaximumEvents = 16U;
inline constexpr float kBackroomsJackMinimumSpawnDistance = 16.0F;
inline constexpr float kBackroomsJackMaximumSpawnDistance = 52.0F;
inline constexpr float kBackroomsJackDefaultMaximumVisibleDistance = 64.0F;
inline constexpr float kBackroomsJackFirstCueMinimumSeconds = 20.0F;
inline constexpr float kBackroomsJackFirstCueMaximumSeconds = 35.0F;
inline constexpr float kBackroomsJackFollowingCueMinimumSeconds = 25.0F;
inline constexpr float kBackroomsJackFollowingCueMaximumSeconds = 45.0F;
inline constexpr float kBackroomsJackCueDeadlineSeconds = 50.0F;
inline constexpr float kBackroomsJackInitialSpawnDelayMinimumSeconds = 40.0F;
inline constexpr float kBackroomsJackInitialSpawnDelayMaximumSeconds = 60.0F;
inline constexpr float kBackroomsJackFollowingSpawnDelayMinimumSeconds = 45.0F;
inline constexpr float kBackroomsJackFollowingSpawnDelayMaximumSeconds = 65.0F;
inline constexpr float kBackroomsJackVisualDeadlineSeconds = 80.0F;
inline constexpr float kBackroomsJackPostChaseVisualDeadlineSeconds = 120.0F;
inline constexpr float kBackroomsJackSpawnRetryMinimumSeconds = 1.0F;
inline constexpr float kBackroomsJackSpawnRetryMaximumSeconds = 1.0F;
inline constexpr float kBackroomsJackHiddenHuntMinimumSeconds = 25.0F;
inline constexpr float kBackroomsJackHiddenHuntMaximumSeconds = 40.0F;
inline constexpr float kBackroomsJackRevealMinimumSeconds = 0.25F;
inline constexpr float kBackroomsJackRevealMaximumSeconds = 0.40F;
inline constexpr float kBackroomsJackInterferenceTailMinimumSeconds = 0.80F;
inline constexpr float kBackroomsJackInterferenceTailMaximumSeconds = 1.80F;
inline constexpr float kBackroomsJackDistantCueMinimumDistance = 14.0F;
inline constexpr float kBackroomsJackDistantCueMaximumDistance = 32.0F;
inline constexpr float kBackroomsJackStandingHeight = 4.52F;
inline constexpr float kBackroomsJackBentHeight = 3.90F;
inline constexpr float kBackroomsJackStandingClearance = 5.05F;
inline constexpr float kBackroomsJackLowCeilingEntryHunchRatio = 0.84F;
inline constexpr float kBackroomsJackMinimumCooldownSeconds = 70.0F;
inline constexpr float kBackroomsJackMaximumCooldownSeconds = 90.0F;
inline constexpr float kBackroomsJackPsychologicalCooldownMinimumSeconds =
    kBackroomsJackFollowingSpawnDelayMinimumSeconds;
inline constexpr float kBackroomsJackPsychologicalCooldownMaximumSeconds =
    kBackroomsJackFollowingSpawnDelayMaximumSeconds;
inline constexpr float kBackroomsJackMaximumPersistedSpawnDelaySeconds = 60.0F;
inline constexpr float kBackroomsJackMaximumPersistedCooldownSeconds = 90.0F;
inline constexpr float kBackroomsJackPoolroomsSpeedMultiplier = 0.98F;
inline constexpr std::int32_t kBackroomsMinimumLogicalLevel = -1'000'000;
inline constexpr std::int32_t kBackroomsMaximumLogicalLevel = 1'000'000;

[[nodiscard]] inline constexpr auto is_valid_backrooms_logical_level(
    std::int32_t logical_level) noexcept -> bool {
    return logical_level >= kBackroomsMinimumLogicalLevel &&
           logical_level <= kBackroomsMaximumLogicalLevel;
}

enum class BackroomsJackPhase : std::uint8_t {
    Dormant = 0,
    Wandering = 1,
    Watching = 2,
    Chasing = 3,
    Searching = 4,
    Jumpscare = 5,
    Cooldown = 6,
};

enum class BackroomsJackMotion : std::uint8_t {
    Idle = 0,
    Walking = 1,
    Running = 2,
};

enum class BackroomsJackEncounterMode : std::uint8_t {
    HiddenHunt = 0,
    CorridorStare = 1,
    RearStare = 2,
};

struct BackroomsJackEncounterModeOrder {
    std::array<BackroomsJackEncounterMode, 3> modes {{
        BackroomsJackEncounterMode::HiddenHunt,
        BackroomsJackEncounterMode::CorridorStare,
        BackroomsJackEncounterMode::RearStare,
    }};
    std::uint32_t next_random_state = 1U;
};

enum class BackroomsJackEventKind : std::uint8_t {
    Notice = 0,
    Chase = 1,
    BootStep = 2,
    WoodenLegStep = 3,
    Screamer = 4,
    Vanished = 5,
    DistantBootStep = 6,
    DistantWoodenLegStep = 7,
};

enum class BackroomsJackLightInterferenceMode : std::uint8_t {
    Flicker = 0,
    BlackoutPulse = 1,
};

enum class BackroomsJackSmokePose : std::uint8_t {
    Standing = 0,
    Bent = 1,
    Watching = 2,
    Chasing = 3,
    Jumpscare = 4,
};

struct BackroomsJackGridPoint {
    int x = 0;
    int z = 0;

    auto operator==(const BackroomsJackGridPoint&) const -> bool = default;
};

struct BackroomsJackNavigationCell {
    float floor_y = static_cast<float>(kBackroomsFloorY + 1);
    float clearance = 0.0F;
    float movement_speed_multiplier = 1.0F;
    bool walkable = false;
    bool standing_allowed = false;
    bool in_water = false;
};

struct BackroomsJackNavigationGrid {
    ChunkCoord center_chunk {};
    std::int32_t logical_level = 0;
    int origin_world_x =
        -kBackroomsJackNavigationChunkRadius * kChunkSizeX;
    int origin_world_z =
        -kBackroomsJackNavigationChunkRadius * kChunkSizeZ;
    std::array<
        BackroomsJackNavigationCell,
        kBackroomsJackNavigationCellCount> cells {};
};

struct BackroomsJackPath {
    std::array<
        BackroomsJackGridPoint,
        kBackroomsJackNavigationCellCount> nodes {};
    std::size_t count = 0U;
    std::size_t cursor = 0U;

    [[nodiscard]] auto empty() const noexcept -> bool {
        return count == 0U || cursor >= count;
    }
};

struct BackroomsJackPlayerContext {
    glm::vec3 feet_position {
        0.5F,
        static_cast<float>(kBackroomsFloorY + 1),
        0.5F,
    };
    glm::vec3 eye_position {
        0.5F,
        static_cast<float>(kBackroomsFloorY) + 2.62F,
        0.5F,
    };
    glm::vec3 look_direction {0.0F, 0.0F, -1.0F};
    float maximum_sprint_speed = 7.2F;
};

struct BackroomsJackChunkReadiness {
    ChunkCoord center_chunk {};
    std::array<bool, kBackroomsJackReadinessCellCount> ready {};
    std::array<std::uint64_t, kBackroomsJackReadinessCellCount>
        mesh_revisions {};
};

struct BackroomsJackUpdateContext {
    BackroomsJackPlayerContext player {};
    BackroomsJackChunkReadiness chunk_readiness {};
    bool allow_spawn = true;
    bool simulation_frozen = false;
    bool player_alive = true;
    // Je laisse les tests et les outils purs utiliser le generateur seul, mais
    // la partie reelle peut superposer les connecteurs et overrides du World.
    const World* spatial_world = nullptr;
    int spatial_world_y_offset = 0;
    float maximum_visible_distance =
        kBackroomsJackDefaultMaximumVisibleDistance;
};

struct BackroomsJackPerception {
    float distance = 0.0F;
    float jack_front_dot = -1.0F;
    float player_front_dot = -1.0F;
    bool line_of_sight = false;
    bool jack_sees_player = false;
    bool player_sees_jack = false;
    bool player_faces_jack = false;
};

struct BackroomsJackSpawnSelection {
    glm::vec3 position {
        0.5F,
        static_cast<float>(kBackroomsFloorY + 1),
        0.5F,
    };
    float body_yaw_degrees = 0.0F;
    float initial_hunch = 0.0F;
    BackroomsJackEncounterMode encounter_mode =
        BackroomsJackEncounterMode::HiddenHunt;
    std::uint32_t next_random_state = 1U;
    bool found = false;
    bool route_guaranteed = false;
};

struct BackroomsJackEvent {
    BackroomsJackEventKind kind = BackroomsJackEventKind::Notice;
    glm::vec3 position {0.0F};
    std::uint64_t sequence = 0U;
};

struct BackroomsJackRenderView {
    glm::vec3 position {0.0F};
    float body_yaw_degrees = 0.0F;
    float head_yaw_degrees = 0.0F;
    float hunch_ratio = 0.0F;
    float motion_amount = 0.0F;
    // Je transporte la vraie lumiere voxel avec la vue afin que Jack puisse
    // disparaitre dans une panne totale sans inventer un eclairage local.
    float sky_light = 0.0F;
    float block_light = 0.0F;
    BackroomsJackMotion motion = BackroomsJackMotion::Idle;
    bool visible = false;
    bool chasing = false;
    bool jumpscare = false;
    float animation_time_seconds = 0.0F;
};

struct BackroomsJackLightInterferenceView {
    glm::vec3 position {0.0F};
    float radius = 0.0F;
    float intensity = 0.0F;
    bool active = false;
    BackroomsJackLightInterferenceMode mode =
        BackroomsJackLightInterferenceMode::Flicker;
};

struct BackroomsJackUpdateResult {
    std::array<BackroomsJackEvent, kBackroomsJackMaximumEvents> events {};
    std::size_t event_count = 0U;
    BackroomsJackRenderView render {};
    BackroomsJackLightInterferenceView light_interference {};
    bool caught_player = false;
};

struct BackroomsJackState {
    BackroomsJackPhase phase = BackroomsJackPhase::Dormant;
    glm::vec3 position {
        0.5F,
        static_cast<float>(kBackroomsFloorY + 1),
        0.5F,
    };
    glm::vec3 last_seen_player_position {
        0.5F,
        static_cast<float>(kBackroomsFloorY + 1),
        0.5F,
    };
    glm::vec3 previous_player_position {
        0.5F,
        static_cast<float>(kBackroomsFloorY + 1),
        0.5F,
    };
    float body_yaw_degrees = 0.0F;
    float head_yaw_degrees = 0.0F;
    float hunch_ratio = 0.0F;
    float motion_amount = 0.0F;
    float phase_seconds = 0.0F;
    float suspicion = 0.0F;
    float lost_sight_seconds = 0.0F;
    float unseen_travel_distance = 0.0F;
    float spawn_check_seconds = 180.0F;
    float cooldown_seconds = 0.0F;
    float footstep_distance = 0.0F;
    std::array<ChunkCoord, 4> evaded_chunks {};
    std::size_t evaded_chunk_count = 0U;
    ChunkCoord last_evade_chunk {};
    std::uint32_t random_state = 1U;
    std::uint64_t next_event_sequence = 1U;
    bool active = false;
    bool has_previous_player_position = false;
    bool has_last_evade_chunk = false;
    bool next_step_is_wooden = false;
    bool notice_event_emitted = false;
    bool chase_event_emitted = false;
    bool screamer_event_emitted = false;
    // Je rattache le corps persistant de Jack à un seul étage logique afin
    // qu'il ne traverse jamais visuellement un changement de niveau.
    std::int32_t logical_level = 0;
};

struct BackroomsJackRuntime {
    BackroomsJackNavigationGrid navigation {};
    BackroomsJackChunkReadiness navigation_readiness {};
    BackroomsJackPath path {};
    BackroomsJackGridPoint path_target {};
    float fixed_step_accumulator = 0.0F;
    float repath_seconds = 0.0F;
    float stuck_seconds = 0.0F;
    float encounter_limit_seconds = 0.0F;
    float encounter_reaction_seconds = 0.0F;
    glm::vec3 last_simulated_position {0.0F};
    BackroomsJackSpawnSelection pending_spawn {};
    glm::vec3 interference_tail_position {0.0F};
    BackroomsJackEncounterMode encounter_mode =
        BackroomsJackEncounterMode::HiddenHunt;
    BackroomsJackEncounterMode previous_encounter_mode =
        BackroomsJackEncounterMode::HiddenHunt;
    float pending_reveal_seconds = 0.0F;
    float pending_vanish_seconds = 0.0F;
    float distant_cue_seconds = 0.0F;
    float visual_deadline_seconds = 0.0F;
    float hidden_hunt_timeout_seconds = 0.0F;
    float encounter_deadline_seconds = 0.0F;
    float interference_tail_seconds = 0.0F;
    float interference_tail_duration = 0.0F;
    BackroomsJackLightInterferenceMode interference_tail_mode =
        BackroomsJackLightInterferenceMode::Flicker;
    std::uint8_t hidden_hunt_reposition_count = 0U;
    bool navigation_valid = false;
    bool navigation_readiness_valid = false;
    bool has_path_target = false;
    bool movement_blocked = false;
    bool encounter_chases = false;
    bool encounter_outcome_directed = false;
    bool encounter_was_seen = false;
    bool has_last_simulated_position = false;
    bool has_previous_encounter_mode = false;
    bool previous_encounter_chased = false;
    bool pending_reveal = false;
    bool pending_vanish = false;
    bool pending_vanish_uses_short_cooldown = false;
    bool spawn_retry_pending = false;
    bool director_initialized = false;
    bool distant_cue_next_wooden = false;
};

[[nodiscard]] auto prepare_backrooms_jack_for_persistence(
    const BackroomsJackState& state,
    const BackroomsJackRuntime& runtime) noexcept -> BackroomsJackState;

struct BackroomsJackSmokePreview {
    BackroomsJackState state {};
    BackroomsJackRenderView render {};
    BackroomsJackLightInterferenceView light_interference {};
};

[[nodiscard]] auto initialize_backrooms_jack(
    std::uint32_t seed,
    std::int32_t logical_level = 0) noexcept -> BackroomsJackState;

[[nodiscard]] auto backrooms_jack_encounter_mode_order(
    std::uint32_t random_state,
    bool has_previous_mode,
    BackroomsJackEncounterMode previous_mode,
    bool force_visible) noexcept -> BackroomsJackEncounterModeOrder;

[[nodiscard]] auto sanitize_backrooms_jack_state(
    const BackroomsJackState& state) noexcept -> BackroomsJackState;

[[nodiscard]] auto backrooms_jack_chunk_at(
    const glm::vec3& position) noexcept -> ChunkCoord;

[[nodiscard]] auto backrooms_jack_navigation_cell(
    const BackroomsJackNavigationGrid& grid,
    int world_x,
    int world_z) noexcept -> const BackroomsJackNavigationCell*;

[[nodiscard]] auto build_backrooms_jack_navigation_grid(
    const BackroomsGenerator& generator,
    const ChunkCoord& center_chunk) noexcept
    -> BackroomsJackNavigationGrid;

[[nodiscard]] auto find_backrooms_jack_path(
    const BackroomsJackNavigationGrid& grid,
    BackroomsJackGridPoint start,
    BackroomsJackGridPoint goal) -> BackroomsJackPath;

[[nodiscard]] auto backrooms_jack_has_line_of_sight(
    const BackroomsGenerator& generator,
    const glm::vec3& from,
    const glm::vec3& to) noexcept -> bool;

[[nodiscard]] auto evaluate_backrooms_jack_perception(
    const BackroomsGenerator& generator,
    const BackroomsJackPlayerContext& player,
    const glm::vec3& jack_position,
    float jack_body_yaw_degrees,
    float jack_hunch_ratio) noexcept -> BackroomsJackPerception;

[[nodiscard]] auto select_backrooms_jack_spawn(
    const BackroomsGenerator& generator,
    const BackroomsJackNavigationGrid& grid,
    const BackroomsJackPlayerContext& player,
    const BackroomsJackChunkReadiness& readiness,
    std::uint32_t random_state) noexcept
    -> BackroomsJackSpawnSelection;

[[nodiscard]] auto make_backrooms_jack_render_view(
    const BackroomsJackState& state) noexcept -> BackroomsJackRenderView;

[[nodiscard]] auto make_backrooms_jack_render_view(
    const BackroomsJackState& state,
    std::int32_t logical_level) noexcept -> BackroomsJackRenderView;

[[nodiscard]] auto make_backrooms_jack_light_interference_view(
    const BackroomsJackState& state) noexcept
    -> BackroomsJackLightInterferenceView;

[[nodiscard]] auto make_backrooms_jack_light_interference_view(
    const BackroomsJackState& state,
    std::int32_t logical_level) noexcept
    -> BackroomsJackLightInterferenceView;

[[nodiscard]] auto update_backrooms_jack(
    BackroomsJackState& state,
    BackroomsJackRuntime& runtime,
    const BackroomsGenerator& generator,
    const BackroomsJackUpdateContext& context,
    float dt) -> BackroomsJackUpdateResult;

[[nodiscard]] auto make_backrooms_jack_smoke_preview(
    BackroomsJackSmokePose pose,
    std::uint32_t seed = 0x4A41434BU) noexcept
    -> BackroomsJackSmokePreview;

}
