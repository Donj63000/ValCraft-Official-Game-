#pragma once

#include "render/RendererQuality.h"
#include "world/WorldGenerator.h"

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <type_traits>
#include <vector>

namespace valcraft {

inline constexpr float kOceanLifeCellSize = 16.0F;
inline constexpr float kOceanLifeFadeWidth = 8.0F;
inline constexpr float kOceanLifeMinimumWaterDepth = 3.0F;
inline constexpr std::size_t kOceanLifeMaximumSchoolCount = 6U;
inline constexpr std::size_t kOceanLifeMaximumFishPerSchool = 8U;
inline constexpr std::size_t kOceanLifeMaximumInstanceCount =
    kOceanLifeMaximumSchoolCount *
    kOceanLifeMaximumFishPerSchool;

struct OceanLifeBudget {
    std::size_t max_schools = kOceanLifeMaximumSchoolCount;
    std::size_t fish_per_school = kOceanLifeMaximumFishPerSchool;
    float radius = 56.0F;

    auto operator==(const OceanLifeBudget&) const -> bool = default;
};

[[nodiscard]] constexpr auto ocean_life_budget_for_quality(
    RendererQuality quality) noexcept -> OceanLifeBudget {
    switch (quality) {
    case RendererQuality::Medium:
        return {4U, 6U, 40.0F};
    case RendererQuality::Low:
        return {3U, 4U, 28.0F};
    case RendererQuality::High:
    case RendererQuality::Dynamic:
    default:
        return {6U, 8U, 56.0F};
    }
}

[[nodiscard]] constexpr auto ocean_life_distance_fade(
    float distance,
    float radius) noexcept -> float {
    if (!(distance >= 0.0F) ||
        !(radius > 0.0F) ||
        distance >= radius) {
        return 0.0F;
    }
    const auto fade_start =
        radius > kOceanLifeFadeWidth
            ? radius - kOceanLifeFadeWidth
            : 0.0F;
    if (distance <= fade_start) {
        return 1.0F;
    }
    const auto width = radius - fade_start;
    const auto linear =
        width > 0.0F
            ? (radius - distance) / width
            : 0.0F;
    const auto clamped =
        linear < 0.0F
            ? 0.0F
            : (linear > 1.0F ? 1.0F : linear);
    return clamped * clamped * (3.0F - 2.0F * clamped);
}

// Je garde exactement huit scalaires par poisson : le GPU pourra les lire
// directement comme attributs d'instance sans conversion ni allocation.
struct OceanLifeInstance {
    glm::vec3 position {0.0F};
    float scale = 0.0F;
    float heading_radians = 0.0F;
    float animation_phase = 0.0F;
    float fade = 0.0F;
    std::uint32_t packed_visual = 0U;

    auto operator==(const OceanLifeInstance&) const -> bool = default;
};

static_assert(sizeof(OceanLifeInstance) == 32U);
static_assert(alignof(OceanLifeInstance) == alignof(float));
static_assert(std::is_standard_layout_v<OceanLifeInstance>);
static_assert(std::is_trivially_copyable_v<OceanLifeInstance>);

[[nodiscard]] auto ocean_life_instance_direction(
    const OceanLifeInstance& instance) noexcept -> glm::vec2;

[[nodiscard]] auto ocean_life_instance_color(
    const OceanLifeInstance& instance) noexcept -> glm::vec3;

[[nodiscard]] constexpr auto ocean_life_instance_school_id(
    const OceanLifeInstance& instance) noexcept -> std::uint32_t {
    return (instance.packed_visual & 0x3fffffffU) >> 3U;
}

[[nodiscard]] constexpr auto ocean_life_instance_member_index(
    const OceanLifeInstance& instance) noexcept -> std::uint8_t {
    return static_cast<std::uint8_t>(
        instance.packed_visual & 0x7U);
}

[[nodiscard]] constexpr auto ocean_life_instance_palette_index(
    const OceanLifeInstance& instance) noexcept -> std::uint8_t {
    return static_cast<std::uint8_t>(
        instance.packed_visual >> 30U);
}

struct OceanLifeFrame {
    std::vector<OceanLifeInstance> instances {};
    std::size_t school_count = 0U;

    auto operator==(const OceanLifeFrame&) const -> bool = default;
};

// Je transporte une vue non proprietaire vers l'echantillonneur. Le champ ne
// la conserve jamais apres sample(), ce qui permet une lambda sans allocation.
struct OceanLifeSurfaceSampler {
    using Callback = TerrainSurfaceSample (*)(
        const void* context,
        int world_x,
        int world_z);
    using DirectCallback = TerrainSurfaceSample (*)(
        int world_x,
        int world_z);

    const void* context = nullptr;
    Callback callback = nullptr;
    DirectCallback direct_callback = nullptr;

    [[nodiscard]] constexpr auto valid() const noexcept -> bool {
        return callback != nullptr ||
               direct_callback != nullptr;
    }

    [[nodiscard]] auto operator()(
        int world_x,
        int world_z) const -> TerrainSurfaceSample {
        return callback != nullptr
                   ? callback(
                         context,
                         world_x,
                         world_z)
                   : direct_callback(
                         world_x,
                         world_z);
    }
};

template <typename Sampler>
[[nodiscard]] auto make_ocean_life_surface_sampler(
    const Sampler& sampler) noexcept -> OceanLifeSurfaceSampler {
    static_assert(
        std::is_invocable_r_v<
            TerrainSurfaceSample,
            const Sampler&,
            int,
            int>);
    if constexpr (std::is_function_v<Sampler>) {
        return {
            nullptr,
            nullptr,
            &sampler,
        };
    } else if constexpr (
        std::is_pointer_v<Sampler> &&
        std::is_function_v<
            std::remove_pointer_t<Sampler>>) {
        return {
            nullptr,
            nullptr,
            sampler,
        };
    } else {
        return {
            &sampler,
            [](const void* context,
               int world_x,
               int world_z) -> TerrainSurfaceSample {
                return std::invoke(
                    *static_cast<const Sampler*>(context),
                    world_x,
                    world_z);
            },
            nullptr,
        };
    }
}

class OceanLifeField {
public:
    OceanLifeField();

    [[nodiscard]] auto sample(
        WorldGenerationProfile profile,
        std::uint32_t world_seed,
        const glm::vec3& camera_position,
        float absolute_time_seconds,
        const OceanLifeBudget& budget,
        OceanLifeSurfaceSampler surface_sampler)
        -> const OceanLifeFrame&;

    [[nodiscard]] auto frame() const noexcept
        -> const OceanLifeFrame&;

    void clear() noexcept;

private:
    OceanLifeFrame frame_ {};
};

} // namespace valcraft
