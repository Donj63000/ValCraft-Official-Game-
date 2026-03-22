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
    glm::vec3 sky_zenith_color {0.38F, 0.66F, 0.98F};
    glm::vec3 sky_horizon_color {0.94F, 0.78F, 0.54F};
    glm::vec3 horizon_glow_color {1.00F, 0.66F, 0.34F};
    glm::vec3 distant_fog_color {0.70F, 0.80F, 0.94F};
    glm::vec3 night_tint_color {0.10F, 0.16F, 0.28F};
    glm::vec3 sun_disk_color {1.00F, 0.86F, 0.56F};
    glm::vec3 moon_disk_color {0.80F, 0.90F, 1.00F};
    float star_intensity = 0.0F;
    float cloud_intensity = 0.22F;
    float exposure = 1.0F;
    float saturation_boost = 1.0F;
    float contrast = 1.0F;
    float vignette_strength = 0.14F;
    float glow_threshold = 0.72F;
    float glow_strength = 0.22F;
};

enum class CreaturePhase : unsigned char {
    Day = 0,
    DuskMorph = 1,
    Night = 2,
    DawnRecover = 3,
};

struct CreatureCycleState {
    CreaturePhase phase = CreaturePhase::Day;
    float morph_factor = 0.0F;
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
    [[nodiscard]] auto current_creature_cycle() const noexcept -> CreatureCycleState;
    [[nodiscard]] static auto normalize_time_of_day(float time_of_day) noexcept -> float;
    [[nodiscard]] static auto compute_state(float time_of_day) -> EnvironmentState;
    [[nodiscard]] static auto classify_creature_cycle(float time_of_day) noexcept -> CreatureCycleState;

private:
    float time_of_day_ = 8.0F;
    bool frozen_ = false;
};

} // namespace valcraft
