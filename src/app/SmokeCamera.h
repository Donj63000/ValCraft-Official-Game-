#pragma once

#include <glm/vec3.hpp>

namespace valcraft {

struct LandSmokeCameraPose {
    glm::vec3 position {0.5F, 80.0F, 0.5F};
    float yaw_degrees = -90.0F;
    float pitch_degrees = -12.5F;

    auto operator==(const LandSmokeCameraPose&) const -> bool = default;
};

// Je sépare cette trajectoire du gameplay afin que les captures graphiques
// restent reproductibles et testables sans modifier la physique du joueur.
[[nodiscard]] auto make_land_smoke_camera_pose(
    float elapsed_seconds,
    int surface_height,
    bool streaming_stress) noexcept -> LandSmokeCameraPose;

} // namespace valcraft
