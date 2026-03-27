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
    PigHide = 0,
    PigSnout = 1,
    PigEar = 2,
    CowHide = 3,
    CowMuzzle = 4,
    CowHorn = 5,
    SheepWool = 6,
    SheepFace = 7,
    SheepHoof = 8,
    ZombieFlesh = 9,
    ZombieBone = 10,
    ZombieMouth = 11,
    ZombieTeeth = 12,
    ZombieEye = 13,
    ZombieVein = 14,
    ZombieScar = 15,
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
