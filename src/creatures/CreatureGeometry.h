#pragma once

#include "creatures/CreatureTypes.h"

#include <glm/gtc/quaternion.hpp>
#include <glm/mat4x4.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace valcraft {

constexpr int kCreatureAtlasSize = 128;
constexpr int kCreatureAtlasTileSize = 16;
constexpr float kCreatureAtlasTilesPerAxis = 8.0F;
inline constexpr std::size_t kCrewVisualRenderCapacity = 6U;
inline constexpr std::size_t kCrewVisualPartBudget = 64U;
inline constexpr float kCrewVisualDrawDistance = 96.0F;

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
    CrewNavyCloth = 31,
    CrewIvoryCloth = 32,
    CrewStripedCloth = 33,
    CrewOchreCloth = 34,
    CrewRedCloth = 35,
    CrewBurgundyCloth = 36,
    CrewSkinLight = 37,
    CrewSkinMedium = 38,
    CrewSkinDark = 39,
    CrewHairBrown = 40,
    CrewHairBlack = 41,
    CrewHairGrey = 42,
    CrewLeather = 43,
    CrewGold = 44,
    CrewRope = 45,
    CrewWood = 46,
    CrewIron = 47,
    CrewWater = 48,
    CrewFish = 49,
    CrewCanvas = 50,
    Count = 51,
};

enum class CrewVisualRole : std::uint8_t {
    Captain = 0,
    Fisher = 1,
    Rigger = 2,
    WaterTender = 3,
    Deckhand = 4,
    Quartermaster = 5,
};

enum class CrewVisualActivity : std::uint8_t {
    Idle = 0,
    Walk = 1,
    Steer = 2,
    Inspect = 3,
    FishCast = 4,
    FishWait = 5,
    FishReel = 6,
    TendWater = 7,
    Carry = 8,
    HaulRope = 9,
    Scrub = 10,
    TurnCapstan = 11,
    SortCargo = 12,
    Socialize = 13,
    Rest = 14,
    Hurt = 15,
    KnockedOut = 16,
    Recover = 17,
};

struct CrewRenderInstance {
    glm::vec3 position {0.0F};
    float yaw_radians = 0.0F;
    float animation_time = 0.0F;
    std::uint32_t appearance_seed = 0U;
    CrewVisualRole role = CrewVisualRole::Deckhand;
    CrewVisualActivity activity = CrewVisualActivity::Idle;
    float motion_amount = 0.0F;
    // Je separe la phase de marche de la progression de la tache afin qu'une
    // arrivee a un poste ne transforme jamais brutalement une pose de jambe.
    float locomotion_phase = 0.0F;
    float activity_phase = 0.0F;
    float hurt_amount = 0.0F;
    float knockout_amount = 0.0F;
    float daylight_factor = 1.0F;
    float sky_light = 1.0F;
    float local_light = 0.0F;
    float precipitation_exposure = 1.0F;

    // Orientation de la plateforme sous les pieds. Elle reste separee du lacet
    // propre du marin afin que ses animations soient exprimees dans le navire.
    glm::quat platform_orientation {1.0F, 0.0F, 0.0F, 0.0F};
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
    float sky_light = 1.0F;
    float block_light = 0.0F;
    float precipitation_exposure = 1.0F;
};

[[nodiscard]] auto creature_atlas_tile_coordinates(CreatureAtlasTile tile) noexcept -> std::array<int, 2>;
[[nodiscard]] auto build_creature_atlas_pixels() -> std::vector<std::uint8_t>;
[[nodiscard]] auto build_creature_parts(const CreatureRenderInstance& creature) -> std::vector<CreaturePartInstance>;
void append_crew_parts(std::vector<CreaturePartInstance>& parts, const CrewRenderInstance& crew);
[[nodiscard]] auto build_crew_parts(const CrewRenderInstance& crew) -> std::vector<CreaturePartInstance>;
[[nodiscard]] auto build_creature_mesh(std::span<const CreaturePartInstance> parts) -> CreatureMeshData;
[[nodiscard]] auto build_creature_mesh(const CreatureRenderInstance& creature) -> CreatureMeshData;
[[nodiscard]] auto build_crew_mesh(const CrewRenderInstance& crew) -> CreatureMeshData;

} // namespace valcraft
