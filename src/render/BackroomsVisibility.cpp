#include "render/BackroomsVisibility.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace valcraft {

static_assert(kBackroomsStreamingSafetyChunks > 0);
static_assert(kBackroomsStreamingSafetyChunks <= kMaxStreamRadius);
static_assert(kBackroomsCoverageScanRadius >= 0);
static_assert(kBackroomsTerminalFogWidth > 0.0F);
static_assert(kBackroomsCoverageMargin >= 0.0F);
static_assert(kBackroomsTerminalFogEndCap >= 0.0F);
static_assert(kBackroomsFogExpansionSpeed > 0.0F);
static_assert(kBackroomsFogMaximumDeltaSeconds > 0.0F);
static_assert(
    kBackroomsDarknessBlockLightBlackThreshold >=
    0.0F);
static_assert(
    kBackroomsDarknessBlockLightFullVisibilityThreshold >
    kBackroomsDarknessBlockLightBlackThreshold);
static_assert(
    kBackroomsDarknessBlockLightFullVisibilityThreshold <=
    1.0F);
static_assert(
    kBackroomsDarknessFlashlightBlackThreshold >=
    0.0F);
static_assert(
    kBackroomsDarknessFlashlightFullVisibilityThreshold >
    kBackroomsDarknessFlashlightBlackThreshold);
static_assert(
    kBackroomsDarknessFlashlightFullVisibilityThreshold <=
    1.0F);

namespace {

[[nodiscard]] auto sanitized_light(float value) noexcept
    -> float {
    // Je traite toute valeur non finie comme une absence de lumière : une
    // donnée GPU corrompue ne doit jamais révéler une salle plongée dans le noir.
    return std::isfinite(value)
               ? std::clamp(value, 0.0F, 1.0F)
               : 0.0F;
}

[[nodiscard]] auto smooth_visibility_ramp(
    float value,
    float black_threshold,
    float full_visibility_threshold) noexcept
    -> float {
    const auto normalized = std::clamp(
        (value - black_threshold) /
            (full_visibility_threshold -
             black_threshold),
        0.0F,
        1.0F);
    return
        normalized *
        normalized *
        (3.0F - 2.0F * normalized);
}

} // namespace

auto backrooms_stream_radius(
    int configured_radius) noexcept -> int {
    // Je borne avant l'addition pour ne jamais déborder avec INT_MAX et pour
    // conserver l'anneau de sécurité dans la limite globale du monde.
    const auto visible_radius = std::clamp(
        configured_radius,
        0,
        kMaxStreamRadius -
            kBackroomsStreamingSafetyChunks);
    return visible_radius +
           kBackroomsStreamingSafetyChunks;
}

auto backrooms_initial_preload_radius(
    int internal_stream_radius) noexcept -> int {
    // Je termine aussi l'anneau de securite avant de rendre la main au joueur.
    // Le point d'apparition peut etre tout pres d'une frontiere de chunk : cet
    // anneau garantit donc le premier franchissement dans les quatre directions.
    return std::clamp(
        internal_stream_radius,
        0,
        kMaxStreamRadius);
}

auto backrooms_contiguous_chunk_coverage_distance(
    const glm::vec3& camera_position,
    std::span<const ChunkCoord> uploaded_chunks,
    int maximum_radius) noexcept -> float {
    if (!std::isfinite(camera_position.x) ||
        !std::isfinite(camera_position.z) ||
        uploaded_chunks.empty()) {
        return 0.0F;
    }

    const auto radius_limit = std::clamp(
        maximum_radius,
        0,
        kBackroomsCoverageScanRadius);
    const auto chunk_x = std::floor(
        static_cast<double>(camera_position.x) /
        static_cast<double>(kChunkSizeX));
    const auto chunk_z = std::floor(
        static_cast<double>(camera_position.z) /
        static_cast<double>(kChunkSizeZ));
    const auto minimum_chunk = static_cast<double>(
        std::numeric_limits<int>::lowest() +
        radius_limit);
    const auto maximum_chunk = static_cast<double>(
        std::numeric_limits<int>::max() -
        radius_limit);
    if (chunk_x < minimum_chunk ||
        chunk_x > maximum_chunk ||
        chunk_z < minimum_chunk ||
        chunk_z > maximum_chunk) {
        return 0.0F;
    }

    const ChunkCoord center {
        static_cast<int>(chunk_x),
        static_cast<int>(chunk_z),
    };
    const auto chunk_is_uploaded =
        [uploaded_chunks](
            const ChunkCoord& candidate) noexcept {
            return std::find(
                       uploaded_chunks.begin(),
                       uploaded_chunks.end(),
                       candidate) !=
                   uploaded_chunks.end();
        };

    // Je n'accepte que des anneaux complets et successifs : un chunk lointain
    // déjà transféré ne doit jamais masquer un trou plus proche de la caméra.
    auto complete_radius = -1;
    for (auto radius = 0;
         radius <= radius_limit;
         ++radius) {
        auto ring_complete = true;
        for (auto dz = -radius;
             dz <= radius &&
             ring_complete;
             ++dz) {
            for (auto dx = -radius;
                 dx <= radius;
                 ++dx) {
                if (radius > 0 &&
                    std::abs(dx) != radius &&
                    std::abs(dz) != radius) {
                    continue;
                }
                if (!chunk_is_uploaded({
                        center.x + dx,
                        center.z + dz,
                    })) {
                    ring_complete = false;
                    break;
                }
            }
        }
        if (!ring_complete) {
            break;
        }
        complete_radius = radius;
    }
    if (complete_radius < 0) {
        return 0.0F;
    }

    const auto minimum_x =
        (static_cast<double>(center.x) -
         static_cast<double>(complete_radius)) *
        static_cast<double>(kChunkSizeX);
    const auto maximum_x =
        (static_cast<double>(center.x) +
         static_cast<double>(complete_radius) +
         1.0) *
        static_cast<double>(kChunkSizeX);
    const auto minimum_z =
        (static_cast<double>(center.z) -
         static_cast<double>(complete_radius)) *
        static_cast<double>(kChunkSizeZ);
    const auto maximum_z =
        (static_cast<double>(center.z) +
         static_cast<double>(complete_radius) +
         1.0) *
        static_cast<double>(kChunkSizeZ);
    const auto coverage_distance = std::max(
        std::min({
            static_cast<double>(camera_position.x) -
                minimum_x,
            maximum_x -
                static_cast<double>(camera_position.x),
            static_cast<double>(camera_position.z) -
                minimum_z,
            maximum_z -
                static_cast<double>(camera_position.z),
        }),
        0.0);
    return static_cast<float>(coverage_distance);
}

auto backrooms_terminal_fog_range(
    float draw_distance,
    float contiguous_chunk_coverage_distance) noexcept
    -> BackroomsTerminalFogRange {
    if (!std::isfinite(draw_distance) ||
        draw_distance <= 0.0F) {
        return {};
    }

    const auto safe_draw_distance = draw_distance;
    const auto safe_coverage =
        std::isfinite(
            contiguous_chunk_coverage_distance)
            ? std::max(
                  contiguous_chunk_coverage_distance -
                      kBackroomsCoverageMargin,
                  0.0F)
            : 0.0F;

    // Je rends le brouillard totalement opaque avant le premier bord GPU non
    // garanti, tout en gardant sa portée courte pour l'identité des Backrooms.
    // La plage {0, 0} reste volontairement active : sans couverture garantie,
    // je masque immédiatement la scène au lieu de désactiver cette sécurité.
    const auto end_distance = std::min({
        safe_draw_distance,
        safe_coverage,
        kBackroomsTerminalFogEndCap,
    });
    return {
        std::max(
            end_distance -
                kBackroomsTerminalFogWidth,
            0.0F),
        end_distance,
    };
}

auto backrooms_advance_terminal_fog_range(
    const BackroomsTerminalFogRange& committed_range,
    const BackroomsTerminalFogRange& safe_target_range,
    float delta_seconds) noexcept
    -> BackroomsTerminalFogRange {
    if (!safe_target_range.enabled()) {
        return {};
    }
    if (!committed_range.enabled()) {
        return safe_target_range;
    }

    // Je ferme sans delai si la couverture se contracte. Dans l'autre sens,
    // je ne revele jamais un anneau entier en une frame : le brouillard recule
    // progressivement sur une geometrie deja terminee et publiee sur le GPU.
    if (safe_target_range.end_distance <=
        committed_range.end_distance) {
        return safe_target_range;
    }

    const auto safe_delta_seconds =
        std::isfinite(delta_seconds)
            ? std::clamp(
                  delta_seconds,
                  0.0F,
                  kBackroomsFogMaximumDeltaSeconds)
            : 0.0F;
    const auto end_distance =
        std::min(
            safe_target_range.end_distance,
            committed_range.end_distance +
                kBackroomsFogExpansionSpeed *
                    safe_delta_seconds);
    return {
        std::max(
            end_distance -
                kBackroomsTerminalFogWidth,
            0.0F),
        end_distance,
    };
}

auto backrooms_darkness_visibility(
    float block_light,
    float flashlight_energy,
    bool enclosed_interior) noexcept -> float {
    if (!enclosed_interior) {
        // Je préserve strictement les mondes ouverts et les autres pipelines :
        // cette fonction ne doit jamais y modifier leur éclairage historique.
        return 1.0F;
    }

    const auto local_visibility =
        smooth_visibility_ramp(
            sanitized_light(block_light),
            kBackroomsDarknessBlockLightBlackThreshold,
            kBackroomsDarknessBlockLightFullVisibilityThreshold);
    const auto flashlight_visibility =
        smooth_visibility_ramp(
            sanitized_light(flashlight_energy),
            kBackroomsDarknessFlashlightBlackThreshold,
            kBackroomsDarknessFlashlightFullVisibilityThreshold);

    // Je réunis les deux sources sans dépasser l'unité. Chacune peut révéler
    // seule la scène et deux sources partielles restent monotones.
    return std::clamp(
        1.0F -
            (1.0F - local_visibility) *
                (1.0F - flashlight_visibility),
        0.0F,
        1.0F);
}

} // namespace valcraft
