#pragma once

#include "world/Environment.h"

#include <glm/vec3.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace valcraft {

inline constexpr std::size_t kMaximumPrecipitationDropBudget = 12'000U;
inline constexpr std::size_t kMaximumPrecipitationImpactBudget = 256U;
inline constexpr float kMaximumPrecipitationRadius = 96.0F;

struct PrecipitationBudget {
    std::size_t max_drops = 0U;
    std::size_t max_impacts = 0U;
    float radius = 0.0F;

    auto operator==(const PrecipitationBudget&) const -> bool = default;
};

struct RainDropInstance {
    glm::vec3 position {};
    glm::vec3 velocity {};
    float length = 0.0F;
    float width = 0.0F;
    float opacity = 0.0F;
    std::uint32_t id = 0U;

    auto operator==(const RainDropInstance&) const -> bool = default;
};

struct RainImpactInstance {
    glm::vec3 position {};
    float age_seconds = 0.0F;
    float lifetime_seconds = 0.0F;
    float radius = 0.0F;
    float opacity = 0.0F;
    std::uint32_t id = 0U;

    auto operator==(const RainImpactInstance&) const -> bool = default;
};

struct PrecipitationFrame {
    std::vector<RainDropInstance> drops;
    std::vector<RainImpactInstance> impacts;

    auto operator==(const PrecipitationFrame&) const -> bool = default;
};

class PrecipitationField {
public:
    explicit PrecipitationField(std::uint32_t seed = 0x5EEDBEEFU) noexcept;

    [[nodiscard]] auto sample(
        const EnvironmentState& environment,
        const glm::vec3& camera_position,
        float surface_height,
        const PrecipitationBudget& budget) -> const PrecipitationFrame&;

    [[nodiscard]] auto frame() const noexcept -> const PrecipitationFrame&;
    [[nodiscard]] auto seed() const noexcept -> std::uint32_t;
    void clear() noexcept;

private:
    std::uint32_t seed_ = 0x5EEDBEEFU;
    PrecipitationFrame frame_ {};
};

} // namespace valcraft
