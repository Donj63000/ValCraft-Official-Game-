#pragma once

#include "creatures/CreatureGeometry.h"
#include "gameplay/PlayerController.h"

#include <array>
#include <cstdint>
#include <vector>

namespace valcraft {

constexpr int kPlayerAtlasSize = 64;
constexpr int kPlayerAtlasTileSize = 16;
constexpr float kPlayerAtlasTilesPerAxis = 4.0F;

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
    Count = 16,
};

[[nodiscard]] auto player_atlas_tile_coordinates(PlayerAtlasTile tile) noexcept -> std::array<int, 2>;
[[nodiscard]] auto build_player_atlas_pixels() -> std::vector<std::uint8_t>;
[[nodiscard]] auto build_player_mesh(const PlayerController& player) -> CreatureMeshData;

} // namespace valcraft
