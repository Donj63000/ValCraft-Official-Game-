#pragma once

#include "world/Block.h"

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <vector>

namespace valcraft {

inline constexpr std::uint16_t kTerrainSurfaceFlagCutout = 1U << 0U;
inline constexpr std::uint16_t kTerrainSurfaceFlagGeologicalBlend = 1U << 4U;
inline constexpr std::uint16_t kTerrainSurfaceFlagDirectMaterial = 1U << 5U;
inline constexpr std::uint16_t kTerrainSurfaceFlagUnderwaterSway = 1U << 6U;
inline constexpr std::uint16_t kTerrainSurfaceFlagMarineFish = 1U << 7U;

struct TerrainVertex {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    float nx = 0.0F;
    float ny = 1.0F;
    float nz = 0.0F;
    BlockId primary_block_id = to_block_id(BlockType::Air);

    // Contrat compact du pipeline moderne :
    // - terrain opaque : couche secondaire + poids de mélange ;
    // - végétation alpha-testée (surface_flags bit 0) : UV U/V en UNORM8.
    // Le vertex shader et les lecteurs CPU doivent donc tester ce bit avant
    // d'interpréter ces octets comme un matériau secondaire.
    BlockId secondary_block_id = to_block_id(BlockType::Air);
    std::uint8_t material_blend = 0;
    std::uint8_t ambient_occlusion = 255;
    std::uint8_t sky_light = 15;
    std::uint8_t block_light = 0;
    std::uint16_t surface_flags = 0;

    auto operator==(const TerrainVertex&) const -> bool = default;
};

static_assert(sizeof(TerrainVertex) == 32, "Je garde le sommet de terrain dans le budget GPU de 32 octets");
static_assert(std::is_standard_layout_v<TerrainVertex>, "Je conserve un format directement descriptible à OpenGL");
static_assert(std::is_trivially_copyable_v<TerrainVertex>, "Je dois pouvoir transférer les sommets sans conversion");

struct OrganicTerrainMesh {
    std::vector<TerrainVertex> vertices {};
    std::vector<std::uint32_t> indices {};
    std::size_t quad_count = 0;

    [[nodiscard]] auto empty() const noexcept -> bool {
        return indices.empty();
    }

    [[nodiscard]] auto triangle_count() const noexcept -> std::size_t {
        return indices.size() / 3U;
    }

    auto operator==(const OrganicTerrainMesh&) const -> bool = default;
};

} // namespace valcraft
