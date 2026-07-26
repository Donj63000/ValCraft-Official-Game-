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
