#pragma once

#include "world/BackroomsGenerator.h"

#include <glm/vec3.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace valcraft {

class World;

inline constexpr int kBackroomsMarlowNavigationChunkRadius = 2;
inline constexpr int kBackroomsMarlowNavigationChunkSide =
    kBackroomsMarlowNavigationChunkRadius * 2 + 1;
inline constexpr int kBackroomsMarlowReadinessChunkRadius = 3;
inline constexpr int kBackroomsMarlowReadinessChunkSide =
    kBackroomsMarlowReadinessChunkRadius * 2 + 1;
inline constexpr int kBackroomsMarlowNavigationSide =
    kChunkSizeX * kBackroomsMarlowNavigationChunkSide;
inline constexpr std::size_t kBackroomsMarlowNavigationCellCount =
    static_cast<std::size_t>(
        kBackroomsMarlowNavigationSide *
        kBackroomsMarlowNavigationSide);
inline constexpr std::size_t kBackroomsMarlowReadinessCellCount =
    static_cast<std::size_t>(
        kBackroomsMarlowReadinessChunkSide *
        kBackroomsMarlowReadinessChunkSide);
inline constexpr std::size_t kBackroomsMarlowMaximumEvents = 12U;

inline constexpr float kBackroomsMarlowMaximumPressure = 1.0F;
inline constexpr float kBackroomsMarlowCaptureDistance = 3.8F;
inline constexpr float kBackroomsMarlowConnectedShoreDistance = 1.5F;
inline constexpr float kBackroomsMarlowDrowningSeconds = 1.8F;
inline constexpr float kBackroomsMarlowScreamerSeconds = 0.85F;
inline constexpr float kBackroomsMarlowInitialGraceSeconds = 12.0F;

enum class BackroomsMarlowPhase : std::uint8_t {
    Dormant = 0,
    Signaling = 1,
    CornerPeek = 2,
    Emerging = 3,
    Blocking = 4,
    Submerging = 5,
    Dragging = 6,
    Drowning = 7,
    Screamer = 8,
    Cooldown = 9,
};

enum class BackroomsMarlowEncounterMode : std::uint8_t {
    CornerPeek = 0,
    Blocking = 1,
    WaterAmbush = 2,
};

enum class BackroomsMarlowEventKind : std::uint8_t {
    WaterSignal = 0,
    BuoyAppeared = 1,
    Surfaced = 2,
    Submerged = 3,
    GrabbedPlayer = 4,
    Screamer = 5,
    Vanished = 6,
};

struct BackroomsMarlowGridPoint {
    int x = 0;
    int z = 0;

    auto operator==(const BackroomsMarlowGridPoint&) const -> bool = default;
};

struct BackroomsMarlowNavigationCell {
    float floor_y = static_cast<float>(kBackroomsFloorY + 1);
    float water_surface_y = static_cast<float>(kBackroomsFloorY + 1);
    float water_depth = 0.0F;
    float clearance = 0.0F;
    bool walkable = false;
    bool has_water = false;
    bool deep_water = false;
    bool guaranteed_route = false;
    bool dark = false;
};

struct BackroomsMarlowNavigationGrid {
    ChunkCoord center_chunk {};
    std::int32_t logical_level = 0;
    int origin_world_x =
        -kBackroomsMarlowNavigationChunkRadius * kChunkSizeX;
    int origin_world_z =
        -kBackroomsMarlowNavigationChunkRadius * kChunkSizeZ;
    // Je garde la grille sur le tas : une grille 5x5 chunks est trop grande
    // pour etre retournee par valeur sans risquer d'epuiser la pile Windows.
    std::vector<BackroomsMarlowNavigationCell> cells =
        std::vector<BackroomsMarlowNavigationCell>(
            kBackroomsMarlowNavigationCellCount);
};

struct BackroomsMarlowPath {
    // Je reserve le chemin sur le tas pour que le runtime reste leger sur la
    // pile, notamment dans les tests et lors des transitions de niveau.
    std::vector<BackroomsMarlowGridPoint> nodes =
        std::vector<BackroomsMarlowGridPoint>(
            kBackroomsMarlowNavigationCellCount);
    std::size_t count = 0U;
    std::size_t cursor = 0U;

    [[nodiscard]] auto empty() const noexcept -> bool {
        return count == 0U || cursor >= count;
    }
};

struct BackroomsMarlowChunkReadiness {
    ChunkCoord center_chunk {};
    std::array<bool, kBackroomsMarlowReadinessCellCount> ready {};
    std::array<std::uint64_t, kBackroomsMarlowReadinessCellCount>
        mesh_revisions {};
};

struct BackroomsMarlowPlayerContext {
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
    // Je recois ici uniquement la distance horizontale parcourue depuis le
    // precedent update, jamais une vitesse ni une distance cumulee.
    float travelled_horizontal_distance = 0.0F;
    float water_depth = 0.0F;
    bool sprinting = false;
    bool in_water = false;
    // Je traite ces trois booleens comme des impulsions d'une seule frame.
    bool entered_water = false;
    bool jumped = false;
    bool landed_in_water = false;
    // Je demande un rayon deja valide contre la vraie surface d'eau ; le
    // coeur gameplay ne depend ainsi ni du renderer ni de SDL.
    bool flashlight_on_water = false;
};

struct BackroomsMarlowUpdateContext {
    BackroomsMarlowPlayerContext player {};
    BackroomsMarlowChunkReadiness chunk_readiness {};
    bool allow_manifestation = true;
    bool allow_capture = true;
    bool threat_slot_available = true;
    bool threat_slot_owned = false;
    bool simulation_frozen = false;
    bool player_alive = true;
    // Je superpose facultativement les overrides du monde au generateur afin
    // que les revisions de maillage correspondent a de vrais obstacles.
    const World* spatial_world = nullptr;
    int spatial_world_y_offset = 0;
};

struct BackroomsMarlowPressureResult {
    float pressure = 0.0F;
    float quiet_seconds = 0.0F;
};

struct BackroomsMarlowModeSelection {
    BackroomsMarlowEncounterMode mode =
        BackroomsMarlowEncounterMode::CornerPeek;
    std::uint32_t next_random_state = 1U;
};

struct BackroomsMarlowManifestationSelection {
    glm::vec3 position {0.0F};
    glm::vec3 buoy_position {0.0F};
    float body_yaw_degrees = 0.0F;
    float peek_side = 1.0F;
    BackroomsMarlowEncounterMode mode =
        BackroomsMarlowEncounterMode::CornerPeek;
    std::uint32_t next_random_state = 1U;
    bool found = false;
    bool has_guaranteed_detour = false;
};

struct BackroomsMarlowCaptureEvaluation {
    glm::vec3 water_target {0.0F};
    float distance = 0.0F;
    bool player_reachable = false;
    bool connected_water_found = false;
    bool allowed = false;
};

struct BackroomsMarlowEvent {
    BackroomsMarlowEventKind kind =
        BackroomsMarlowEventKind::WaterSignal;
    glm::vec3 position {0.0F};
    std::uint64_t sequence = 0U;
};

struct BackroomsMarlowRenderView {
    glm::vec3 position {0.0F};
    float body_yaw_degrees = 0.0F;
    float immersion_ratio = 0.0F;
    float reveal_amount = 0.0F;
    float peek_side = 1.0F;
    BackroomsMarlowPhase phase = BackroomsMarlowPhase::Dormant;
    bool visible = false;
};

struct BackroomsMarlowBuoyView {
    glm::vec3 position {0.0F};
    float warning_amount = 0.0F;
    bool visible = false;
};

struct BackroomsMarlowInterferenceView {
    glm::vec3 position {0.0F};
    float radius = 0.0F;
    float intensity = 0.0F;
    bool blackout_pulse = false;
};

struct BackroomsMarlowCaptureView {
    glm::vec3 water_target {0.0F};
    float drag_amount = 0.0F;
    float drowning_amount = 0.0F;
    bool active = false;
    bool lock_player_controls = false;
};

struct BackroomsMarlowUpdateResult {
    std::array<BackroomsMarlowEvent, kBackroomsMarlowMaximumEvents> events {};
    std::size_t event_count = 0U;
    BackroomsMarlowRenderView render {};
    BackroomsMarlowBuoyView buoy {};
    BackroomsMarlowInterferenceView interference {};
    BackroomsMarlowCaptureView capture {};
    bool requests_threat_slot = false;
    bool cancels_threat_request = false;
    bool holds_threat_slot = false;
    bool releases_threat_slot = false;
    bool capture_started = false;
    bool kill_player = false;
};

// Je conserve ici uniquement les donnees que je peux reprendre sans restaurer
// un corps, un chemin ou une cinematique a moitie jouee.
struct BackroomsMarlowState {
    float pressure = 0.0F;
    float cue_seconds = 0.0F;
    float manifestation_seconds = 0.0F;
    float cooldown_seconds = 0.0F;
    std::int32_t logical_level = -2;
    BackroomsMarlowEncounterMode last_mode =
        BackroomsMarlowEncounterMode::CornerPeek;
    std::uint32_t random_state = 1U;
    std::uint64_t next_event_sequence = 1U;
    bool has_last_mode = false;
    bool initialized = false;
};

struct BackroomsMarlowRuntime {
    BackroomsMarlowNavigationGrid navigation {};
    BackroomsMarlowChunkReadiness navigation_readiness {};
    BackroomsMarlowPath path {};
    BackroomsMarlowManifestationSelection pending_manifestation {};
    glm::vec3 position {0.0F};
    glm::vec3 buoy_position {0.0F};
    glm::vec3 capture_target {0.0F};
    float body_yaw_degrees = 0.0F;
    float peek_side = 1.0F;
    float phase_seconds = 0.0F;
    float phase_duration_seconds = 0.0F;
    float quiet_seconds = 0.0F;
    float grace_seconds = kBackroomsMarlowInitialGraceSeconds;
    float retry_seconds = 0.0F;
    BackroomsMarlowPhase phase = BackroomsMarlowPhase::Dormant;
    bool navigation_valid = false;
    bool navigation_readiness_valid = false;
    bool waiting_for_threat_slot = false;
    bool buoy_warning_active = false;
    bool previous_flashlight_on_water = false;
    bool capture_event_emitted = false;
    bool kill_event_emitted = false;
};

[[nodiscard]] auto initialize_backrooms_marlow(
    std::uint32_t seed,
    std::int32_t logical_level = -2) noexcept -> BackroomsMarlowState;

[[nodiscard]] auto sanitize_backrooms_marlow_state(
    const BackroomsMarlowState& state) noexcept -> BackroomsMarlowState;

[[nodiscard]] auto prepare_backrooms_marlow_for_persistence(
    const BackroomsMarlowState& state) noexcept -> BackroomsMarlowState;

[[nodiscard]] auto evaluate_backrooms_marlow_pressure(
    float pressure,
    float quiet_seconds,
    const BackroomsMarlowPlayerContext& player,
    bool flashlight_water_started,
    float dt) noexcept -> BackroomsMarlowPressureResult;

[[nodiscard]] auto select_backrooms_marlow_mode(
    std::uint32_t random_state,
    float pressure,
    bool has_previous_mode,
    BackroomsMarlowEncounterMode previous_mode) noexcept
    -> BackroomsMarlowModeSelection;

[[nodiscard]] auto backrooms_marlow_chunk_at(
    const glm::vec3& position) noexcept -> ChunkCoord;

[[nodiscard]] auto backrooms_marlow_navigation_cell(
    const BackroomsMarlowNavigationGrid& grid,
    int world_x,
    int world_z) noexcept -> const BackroomsMarlowNavigationCell*;

[[nodiscard]] auto build_backrooms_marlow_navigation_grid(
    const BackroomsGenerator& generator,
    const ChunkCoord& center_chunk,
    const World* spatial_world = nullptr,
    int spatial_world_y_offset = 0) noexcept
    -> BackroomsMarlowNavigationGrid;

[[nodiscard]] auto find_backrooms_marlow_path(
    const BackroomsMarlowNavigationGrid& grid,
    BackroomsMarlowGridPoint start,
    BackroomsMarlowGridPoint goal) -> BackroomsMarlowPath;

[[nodiscard]] auto backrooms_marlow_has_detour(
    const BackroomsMarlowNavigationGrid& grid,
    BackroomsMarlowGridPoint start,
    BackroomsMarlowGridPoint goal,
    BackroomsMarlowGridPoint blocked) noexcept -> bool;

[[nodiscard]] auto backrooms_marlow_supercover_clear(
    const BackroomsMarlowNavigationGrid& grid,
    const BackroomsMarlowChunkReadiness& readiness,
    BackroomsMarlowGridPoint from,
    BackroomsMarlowGridPoint to) noexcept -> bool;

[[nodiscard]] auto select_backrooms_marlow_manifestation(
    const BackroomsMarlowNavigationGrid& grid,
    const BackroomsMarlowChunkReadiness& readiness,
    const BackroomsMarlowPlayerContext& player,
    BackroomsMarlowEncounterMode mode,
    std::uint32_t random_state) noexcept
    -> BackroomsMarlowManifestationSelection;

[[nodiscard]] auto evaluate_backrooms_marlow_capture(
    const BackroomsMarlowNavigationGrid& grid,
    const BackroomsMarlowChunkReadiness& readiness,
    const BackroomsMarlowPlayerContext& player,
    const glm::vec3& marlow_position,
    bool buoy_warning_active) noexcept
    -> BackroomsMarlowCaptureEvaluation;

[[nodiscard]] auto update_backrooms_marlow(
    BackroomsMarlowState& state,
    BackroomsMarlowRuntime& runtime,
    const BackroomsGenerator& generator,
    const BackroomsMarlowUpdateContext& context,
    float dt) -> BackroomsMarlowUpdateResult;

} // namespace valcraft
