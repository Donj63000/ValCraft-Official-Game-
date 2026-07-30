#pragma once

#include "render/SeaHorizon.h"

#include <glm/vec3.hpp>

namespace valcraft {

// Je conserve le joueur comme centre du streaming dès qu'il ne repose plus
// réellement sur le navire, même s'il reste assez proche pour la prédiction.
[[nodiscard]] inline auto resolve_sea_adventure_streaming_focus(
    const glm::vec3& player_position,
    bool player_on_ship,
    const glm::vec3& ship_position,
    const glm::vec3& ship_velocity) noexcept -> glm::vec3 {
    if (!player_on_ship) {
        return player_position;
    }

    return sea_horizon_predictive_streaming_focus(
        player_position,
        ship_position,
        ship_velocity);
}

} // namespace valcraft
