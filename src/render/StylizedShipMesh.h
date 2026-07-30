#pragma once

#include "gameplay/SeaAdventure.h"
#include "world/ChunkMesher.h"

#include <cstddef>
#include <cstdint>

namespace valcraft {

enum class StylizedShipLod : std::uint8_t {
    Far = 0,
    Near,
};

inline constexpr std::size_t kStylizedShipLodCount = 2U;

[[nodiscard]] constexpr auto stylized_ship_lod_index(
    StylizedShipLod lod) noexcept -> std::size_t {
    return static_cast<std::size_t>(lod);
}

[[nodiscard]] constexpr auto stylized_ship_shadow_lod(
    int cascade_index,
    StylizedShipLod active_lod,
    bool far_lod_ready,
    bool preserve_near_details) noexcept
    -> StylizedShipLod {

    // Je conserve le mobilier et les sabords détaillés dans la cascade proche
    // quand je me trouve sous les ponts. À l'extérieur, la silhouette Far
    // suffit pour les deux cascades et évite de reprojeter tout l'intérieur.
    return far_lod_ready &&
                   (!preserve_near_details ||
                    cascade_index > 0)
               ? StylizedShipLod::Far
               : active_lod;
}

struct StylizedShipCacheKey {
    std::uint64_t geometry_revision = 0U;
    StylizedShipLod lod = StylizedShipLod::Near;

    auto operator==(const StylizedShipCacheKey&) const -> bool = default;
};

struct StylizedShipIndexRange {
    std::size_t first_index = 0U;
    std::size_t index_count = 0U;

    [[nodiscard]] auto triangle_count() const noexcept -> std::size_t {
        return index_count / 3U;
    }

    auto operator==(const StylizedShipIndexRange&) const -> bool = default;
};

struct StylizedShipMeshMetrics {
    ShipBounds bounds {};
    StylizedShipIndexRange hull {};
    StylizedShipIndexRange decks {};
    StylizedShipIndexRange structures {};
    StylizedShipIndexRange rigging {};
    StylizedShipIndexRange sails {};
    std::uint64_t content_checksum = 0U;
    float maximum_profile_deviation = 0.0F;
    float maximum_protection_excess = 0.0F;
    float maximum_deck_alignment_error = 0.0F;
    float midship_half_width = 0.0F;
    float stern_half_width = 0.0F;
    float bow_half_width = 0.0F;
    bool interior_axis_open = false;

    auto operator==(const StylizedShipMeshMetrics&) const -> bool = default;
};

struct StylizedShipMeshData {
    ChunkMeshData mesh {};
    StylizedShipCacheKey cache_key {};
    StylizedShipMeshMetrics metrics {};

    [[nodiscard]] auto empty() const noexcept -> bool {
        return mesh.indices.empty();
    }
};

// Je construis exclusivement une representation locale de rendu. Les pieces
// collidables, les supports et le profil de protection restent inchanges.
[[nodiscard]] auto build_stylized_ship_mesh(
    const ShipBlueprint& blueprint,
    StylizedShipLod lod = StylizedShipLod::Near) -> StylizedShipMeshData;

[[nodiscard]] auto build_stylized_ship_mesh(
    const ShipRenderState& render_state,
    StylizedShipLod lod = StylizedShipLod::Near) -> StylizedShipMeshData;

} // namespace valcraft
