#pragma once

#include "creatures/CreatureTypes.h"

#include <glm/mat4x4.hpp>

#include <array>
#include <cstdint>
#include <span>
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
    PigBelly = 16,
    PigHoof = 17,
    CowHoof = 18,
    SheepShadow = 19,
    TransformHide = 20,
    TransformSinew = 21,
    TransformGlow = 22,
    ZombieClaw = 23,
    ZombieWool = 24,
    ZombieHorn = 25,
    VillagerCloth = 26,
    VillagerSkin = 27,
    VillagerHair = 28,
    VillagerApron = 29,
    VillagerEye = 30,
    Count = 31,
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

struct BoxUvRect {
    float u0 = 0.0F;
    float v0 = 0.0F;
    float u1 = 0.0F;
    float v1 = 0.0F;
};

struct CreaturePartInstance {
    glm::mat4 transform {1.0F};
    std::array<BoxUvRect, 6> face_uvs {};
    float nightmare_factor = 0.0F;
    float tension = 0.0F;
    float material_class = 0.0F;
    float cavity_mask = 0.0F;
    float emissive_strength = 0.0F;
};

[[nodiscard]] auto creature_atlas_tile_coordinates(CreatureAtlasTile tile) noexcept -> std::array<int, 2>;
[[nodiscard]] auto build_creature_atlas_pixels() -> std::vector<std::uint8_t>;
[[nodiscard]] auto build_creature_parts(const CreatureRenderInstance& creature) -> std::vector<CreaturePartInstance>;
[[nodiscard]] auto build_creature_mesh(std::span<const CreaturePartInstance> parts) -> CreatureMeshData;
[[nodiscard]] auto build_creature_mesh(const CreatureRenderInstance& creature) -> CreatureMeshData;

} // namespace valcraft
