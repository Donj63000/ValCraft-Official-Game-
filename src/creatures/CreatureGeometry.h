#pragma once

#include "creatures/CreatureTypes.h"

#include <array>
#include <cstdint>
#include <vector>

namespace valcraft {

constexpr int kCreatureAtlasSize = 128;
constexpr int kCreatureAtlasTileSize = 16;
constexpr float kCreatureAtlasTilesPerAxis = 8.0F;

enum class CreatureAtlasTile : std::uint8_t {
    RabbitCoat = 0,
    RabbitBelly = 1,
    RabbitEarInner = 2,
    RabbitNose = 3,
    FennecCoat = 4,
    FennecBack = 5,
    FennecEarInner = 6,
    FennecTail = 7,
    LambWoolLight = 8,
    LambWoolShadow = 9,
    LambFace = 10,
    HoofHorn = 11,
    NightmareFlesh = 12,
    NightmareBone = 13,
    NightmareScar = 14,
    NightmareEye = 15,
};

struct CreatureVertex {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    float u = 0.0F;
    float v = 0.0F;
    float nx = 0.0F;
    float ny = 1.0F;
    float nz = 0.0F;
    float nightmare_factor = 0.0F;
    float tension = 0.0F;
    float material_class = 0.0F;
    float cavity_mask = 0.0F;
    float emissive_strength = 0.0F;
};

struct CreatureMeshData {
    std::vector<CreatureVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::size_t part_count = 0;

    [[nodiscard]] auto empty() const noexcept -> bool {
        return indices.empty();
    }
};

[[nodiscard]] auto creature_atlas_tile_coordinates(CreatureAtlasTile tile) noexcept -> std::array<int, 2>;
[[nodiscard]] auto build_creature_atlas_pixels() -> std::vector<std::uint8_t>;
[[nodiscard]] auto build_creature_mesh(const CreatureRenderInstance& creature) -> CreatureMeshData;

} // namespace valcraft
