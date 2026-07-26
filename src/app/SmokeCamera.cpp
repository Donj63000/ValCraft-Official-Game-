#include "app/SmokeCamera.h"

#include <algorithm>
#include <cmath>

namespace valcraft {

auto make_land_smoke_camera_pose(
    float elapsed_seconds,
    int surface_height,
    bool streaming_stress) noexcept -> LandSmokeCameraPose {
    const auto safe_elapsed =
        std::max(std::isfinite(elapsed_seconds) ? elapsed_seconds : 0.0F, 0.0F);
    const auto speed_x = streaming_stress ? 8.0F : 1.25F;
    const auto speed_z = streaming_stress ? 3.0F : 0.45F;
    const auto safe_surface_height = std::clamp(surface_height, 0, 255);

    LandSmokeCameraPose pose {};
    pose.position = {
        0.5F + safe_elapsed * speed_x,
        static_cast<float>(safe_surface_height) + 2.40F,
        0.5F + safe_elapsed * speed_z,
    };
    pose.yaw_degrees =
        std::atan2(speed_z, speed_x) * 180.0F / 3.14159265358979323846F;
    pose.pitch_degrees = -12.5F;
    return pose;
}

} // namespace valcraft
