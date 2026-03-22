#pragma once

#include <algorithm>
#include <chrono>

namespace valcraft {

inline auto resolve_simulation_frame_time(
    bool deterministic_smoke,
    std::chrono::duration<double> measured_frame_time,
    std::chrono::duration<double> fixed_step) -> std::chrono::duration<double> {
    if (deterministic_smoke) {
        return fixed_step;
    }

    return std::min(measured_frame_time, std::chrono::duration<double>(0.25));
}

} // namespace valcraft
