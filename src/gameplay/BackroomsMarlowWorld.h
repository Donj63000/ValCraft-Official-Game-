#pragma once

#include <glm/vec3.hpp>

namespace valcraft {

class PlayerController;
class World;

inline constexpr float kBackroomsMarlowMaximumDragDistance = 4.0F;
inline constexpr float kBackroomsMarlowFlashlightRange = 18.0F;

struct BackroomsMarlowDragSweepResult {
    glm::vec3 position {0.0F};
    bool blocked = false;
};

// Je borne et balaie le transport force de Marlow sans jamais traverser une
// collision du joueur. Une entree non finie est traitee comme un blocage.
[[nodiscard]] auto sweep_backrooms_marlow_drag(
    const PlayerController& player,
    const World& world,
    const glm::vec3& current_position,
    const glm::vec3& target_position,
    float maximum_distance) noexcept -> BackroomsMarlowDragSweepResult;

// Je ne signale l'eau que si elle est la premiere cible physique du faisceau.
// Le rayon reste borne a 18 metres, quelle que soit la portee demandee.
[[nodiscard]] auto backrooms_marlow_flashlight_hits_water(
    const World& world,
    const glm::vec3& eye_position,
    const glm::vec3& look_direction,
    bool flashlight_enabled,
    float flashlight_intensity,
    float maximum_distance = kBackroomsMarlowFlashlightRange) noexcept -> bool;

// Je demarre le compte a rebours a la frame suivante afin de garantir au
// screamer sa duree complete, meme avec le pas maximal de 250 ms.
[[nodiscard]] auto advance_backrooms_marlow_death_delay(
    float remaining_seconds,
    float dt,
    bool armed_this_update) noexcept -> float;

} // namespace valcraft
