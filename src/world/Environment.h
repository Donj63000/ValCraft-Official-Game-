#pragma once

#include <glm/vec3.hpp>

namespace valcraft {

struct EnvironmentState {
    float time_of_day = 8.0F;
    float daylight_factor = 1.0F;
    glm::vec3 sun_direction {0.25F, 0.9F, 0.35F};
    glm::vec3 sun_color {1.0F, 0.95F, 0.82F};
    glm::vec3 ambient_color {0.42F, 0.48F, 0.56F};
    glm::vec3 fog_color {0.60F, 0.76F, 0.94F};
    glm::vec3 sky_color {0.46F, 0.70F, 0.94F};
};

class EnvironmentClock {
public:
    explicit EnvironmentClock(float initial_time_of_day = 8.0F, bool frozen = false);

    void update(float dt);
    void set_frozen(bool frozen) noexcept;
    void set_time_of_day(float time_of_day) noexcept;

    [[nodiscard]] auto is_frozen() const noexcept -> bool;
    [[nodiscard]] auto time_of_day() const noexcept -> float;
    [[nodiscard]] auto current_state() const -> EnvironmentState;
    [[nodiscard]] static auto normalize_time_of_day(float time_of_day) noexcept -> float;
    [[nodiscard]] static auto compute_state(float time_of_day) -> EnvironmentState;

private:
    float time_of_day_ = 8.0F;
    bool frozen_ = false;
};

} // namespace valcraft
