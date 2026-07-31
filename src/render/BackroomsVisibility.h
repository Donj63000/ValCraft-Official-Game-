#pragma once

#include "world/Block.h"

#include <glm/vec3.hpp>

#include <span>

namespace valcraft {

// Je réserve toujours un anneau complet hors champ pour absorber le prochain
// franchissement de chunk sans révéler une salle encore absente.
inline constexpr int kBackroomsStreamingSafetyChunks = 1;
inline constexpr int kBackroomsCoverageScanRadius = 6;
inline constexpr float kBackroomsTerminalFogWidth = 24.0F;
inline constexpr float kBackroomsCoverageMargin = 8.0F;
inline constexpr float kBackroomsTerminalFogEndCap = 64.0F;
inline constexpr float kBackroomsFogExpansionSpeed = 6.0F;
inline constexpr float kBackroomsFogMaximumDeltaSeconds = 0.1F;

// Je garde ces seuils nommés et normalisés pour pouvoir recopier exactement
// la même courbe dans le GLSL sans laisser réapparaître un plancher lumineux.
inline constexpr float
    kBackroomsDarknessBlockLightBlackThreshold = 0.0F;
inline constexpr float
    kBackroomsDarknessBlockLightFullVisibilityThreshold = 0.62F;
inline constexpr float
    kBackroomsDarknessFlashlightBlackThreshold = 0.0F;
inline constexpr float
    kBackroomsDarknessFlashlightFullVisibilityThreshold = 0.18F;

struct BackroomsTerminalFogRange {
    float start_distance = -1.0F;
    float end_distance = -1.0F;

    [[nodiscard]] auto enabled() const noexcept -> bool {
        return start_distance >= 0.0F &&
               end_distance >= start_distance;
    }

    auto operator==(const BackroomsTerminalFogRange&) const -> bool = default;
};

[[nodiscard]] auto backrooms_stream_radius(
    int configured_radius) noexcept -> int;

[[nodiscard]] auto backrooms_initial_preload_radius(
    int internal_stream_radius) noexcept -> int;

[[nodiscard]] auto backrooms_contiguous_chunk_coverage_distance(
    const glm::vec3& camera_position,
    std::span<const ChunkCoord> uploaded_chunks,
    int maximum_radius) noexcept -> float;

[[nodiscard]] auto backrooms_terminal_fog_range(
    float draw_distance,
    float contiguous_chunk_coverage_distance) noexcept
    -> BackroomsTerminalFogRange;

[[nodiscard]] auto backrooms_advance_terminal_fog_range(
    const BackroomsTerminalFogRange& committed_range,
    const BackroomsTerminalFogRange& safe_target_range,
    float delta_seconds) noexcept
    -> BackroomsTerminalFogRange;

[[nodiscard]] auto backrooms_darkness_visibility(
    float block_light,
    float flashlight_energy,
    bool enclosed_interior) noexcept -> float;

} // namespace valcraft
