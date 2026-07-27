#pragma once

#include "creatures/CreatureGeometry.h"
#include "gameplay/PlayerController.h"
#include "gameplay/PlayerMusket.h"
#include "world/Block.h"

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <array>
#include <cstdint>
#include <vector>

namespace valcraft {

constexpr int kPlayerAtlasSize = 128;
constexpr int kPlayerAtlasTileSize = 16;
constexpr float kPlayerAtlasTilesPerAxis = 8.0F;

enum class PlayerAtlasTile : std::uint8_t {
    Skin = 0,
    Hair = 1,
    Shirt = 2,
    Pants = 3,
    Shoes = 4,
    Eye = 5,
    Mouth = 6,
    Hurt = 7,
    SkinShadow = 8,
    HairShadow = 9,
    ShirtShadow = 10,
    PantsShadow = 11,
    Sole = 12,
    Sleeve = 13,
    Belt = 14,
    Face = 15,
    SwordBlade = 16,
    SwordEdge = 17,
    SwordGuard = 18,
    SwordGrip = 19,
    SwordPommel = 20,
    MusketWalnut = 21,
    MusketSteel = 22,
    MusketBrass = 23,
    MusketFlint = 24,
    Count = 25,
};

enum class PlayerMeshView : std::uint8_t {
    FirstPerson = 0,
    WorldAvatar = 1,
};

struct PlayerViewModelPose {
    glm::vec3 root_position {0.0F};
    glm::vec3 shoulder_position {0.0F};
    glm::vec3 elbow_position {0.0F};
    glm::vec3 wrist_position {0.0F};
    glm::vec3 offhand_position {0.0F};
    glm::vec3 muzzle_position {0.0F};
    glm::vec3 muzzle_forward {0.0F, 0.0F, -1.0F};
    glm::mat4 item_socket_transform {1.0F};
    float look_sway_yaw = 0.0F;
    float look_sway_pitch = 0.0F;
    float walk_bob = 0.0F;
    float action_swing = 0.0F;
    float musket_aim_ratio = 0.0F;
    float musket_reload_progress = 0.0F;
    float musket_recoil_ratio = 0.0F;
    std::uint8_t musket_reload_stage = 0U;
    bool musket_active = false;
};

struct PlayerViewModelMesh {
    CreatureMeshData mesh;
    PlayerViewModelPose pose {};

    [[nodiscard]] auto empty() const noexcept -> bool {
        return mesh.empty();
    }
};

struct PlayerViewModelParts {
    std::vector<CreaturePartInstance> parts;
    PlayerViewModelPose pose {};

    [[nodiscard]] auto empty() const noexcept -> bool {
        return parts.empty();
    }
};

[[nodiscard]] auto player_atlas_tile_coordinates(PlayerAtlasTile tile) noexcept -> std::array<int, 2>;
[[nodiscard]] auto build_player_atlas_pixels() -> std::vector<std::uint8_t>;
[[nodiscard]] auto build_player_world_avatar_parts(const PlayerController& player) -> std::vector<CreaturePartInstance>;
[[nodiscard]] auto build_player_viewmodel_parts(const PlayerController& player,
                                                BlockId held_item = to_block_id(BlockType::Air)) -> PlayerViewModelParts;
[[nodiscard]] auto build_player_viewmodel_parts(const PlayerController& player,
                                                BlockId held_item,
                                                const PlayerMusketView& musket) -> PlayerViewModelParts;
[[nodiscard]] auto build_player_world_avatar_mesh(const PlayerController& player) -> CreatureMeshData;
[[nodiscard]] auto build_player_viewmodel_mesh(const PlayerController& player,
                                               BlockId held_item = to_block_id(BlockType::Air)) -> PlayerViewModelMesh;
[[nodiscard]] auto build_player_viewmodel_mesh(const PlayerController& player,
                                               BlockId held_item,
                                               const PlayerMusketView& musket) -> PlayerViewModelMesh;
[[nodiscard]] auto build_player_mesh(const PlayerController& player,
                                     PlayerMeshView view = PlayerMeshView::FirstPerson) -> CreatureMeshData;

} // namespace valcraft
