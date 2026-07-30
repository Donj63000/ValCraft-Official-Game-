#include "gameplay/SeaAdventure.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>

#include <algorithm>
#include <array>
#include <cmath>

namespace valcraft {

namespace {

constexpr std::array<ShipInteriorLight, 19>
    kAmelieInteriorLights {{
        {{1.60F, 3.08F, -29.0F}, {1.00F, 0.72F, 0.44F}, 5.2F, 0.92F, 0.11F, {-7.5F, 0.86F, -35.50F}, {7.5F, 3.64F, -25.75F}, 0.45F, {0.0F, 0.0F}, {0.30F, 1.05F}},
        {{-1.60F, 3.08F, -22.0F}, {1.00F, 0.70F, 0.41F}, 4.8F, 0.88F, 0.23F, {-7.5F, 0.86F, -25.75F}, {7.5F, 3.64F, -19.50F}, 0.45F, {0.30F, 1.05F}, {0.0F, 1.65F}},
        {{1.80F, 3.08F, -15.0F}, {1.00F, 0.64F, 0.34F}, 6.4F, 0.80F, 0.37F, {-8.0F, 0.86F, -19.50F}, {8.0F, 3.64F, 20.25F}, 0.40F, {0.0F, 1.65F}, {0.0F, 1.10F}},
        {{-1.80F, 3.08F, -7.0F}, {1.00F, 0.64F, 0.34F}, 6.4F, 0.80F, 0.49F, {-8.0F, 0.86F, -19.50F}, {8.0F, 3.64F, 20.25F}, 0.40F, {0.0F, 1.65F}, {0.0F, 1.10F}},
        {{1.80F, 3.08F, 1.0F}, {1.00F, 0.64F, 0.34F}, 6.4F, 0.80F, 0.61F, {-8.0F, 0.86F, -19.50F}, {8.0F, 3.64F, 20.25F}, 0.40F, {0.0F, 1.65F}, {0.0F, 1.10F}},
        {{-1.80F, 3.08F, 9.0F}, {1.00F, 0.64F, 0.34F}, 6.4F, 0.80F, 0.73F, {-8.0F, 0.86F, -19.50F}, {8.0F, 3.64F, 20.25F}, 0.40F, {0.0F, 1.65F}, {0.0F, 1.10F}},
        {{1.60F, 3.08F, 17.0F}, {1.00F, 0.62F, 0.32F}, 6.0F, 0.74F, 0.89F, {-8.0F, 0.86F, -19.50F}, {8.0F, 3.64F, 20.25F}, 0.40F, {0.0F, 1.65F}, {0.0F, 1.10F}},

        {{-1.60F, 0.18F, -25.0F}, {1.00F, 0.72F, 0.46F}, 5.4F, 0.86F, 1.03F, {-7.6F, -2.14F, -35.50F}, {7.6F, 0.82F, -19.0F}, 0.45F, {0.0F, 0.0F}, {0.0F, 1.10F}},
        {{1.80F, 0.18F, -14.0F}, {1.00F, 0.67F, 0.38F}, 5.0F, 0.82F, 1.17F, {-7.7F, -2.14F, -19.0F}, {7.7F, 0.82F, -10.0F}, 0.45F, {0.0F, 1.10F}, {-0.78F, 1.15F}},
        {{-1.60F, 0.18F, -8.0F}, {1.00F, 0.64F, 0.34F}, 5.2F, 0.78F, 1.31F, {-7.8F, -2.14F, -10.0F}, {7.8F, 0.82F, 2.30F}, 0.45F, {-0.78F, 1.15F}, {1.20F, 1.05F}},
        {{1.80F, 0.18F, -2.0F}, {1.00F, 0.64F, 0.34F}, 5.2F, 0.78F, 1.43F, {-7.8F, -2.14F, -10.0F}, {7.8F, 0.82F, 2.30F}, 0.45F, {-0.78F, 1.15F}, {1.20F, 1.05F}},
        {{-1.80F, 0.18F, 6.0F}, {1.00F, 0.62F, 0.32F}, 6.0F, 0.76F, 1.59F, {-7.8F, -2.14F, 2.30F}, {7.8F, 0.82F, 15.0F}, 0.45F, {1.20F, 1.05F}, {0.0F, 1.10F}},
        {{1.60F, 0.18F, 20.0F}, {1.00F, 0.66F, 0.37F}, 6.6F, 0.74F, 1.71F, {-7.5F, -2.14F, 15.0F}, {7.5F, 0.82F, 31.0F}, 0.45F, {0.0F, 1.10F}, {0.0F, 0.0F}},

        {{-1.50F, -2.72F, -27.0F}, {1.00F, 0.65F, 0.35F}, 5.0F, 0.72F, 1.87F, {-7.2F, -5.02F, -35.50F}, {7.2F, -2.18F, -23.0F}, 0.45F, {0.0F, 0.0F}, {0.0F, 1.05F}},
        {{1.60F, -2.72F, -18.0F}, {1.00F, 0.54F, 0.24F}, 6.0F, 0.90F, 2.03F, {-7.6F, -5.02F, -23.0F}, {7.6F, -2.18F, -10.0F}, 0.45F, {0.0F, 1.05F}, {2.80F, 1.05F}},
        {{-1.60F, -2.72F, -6.0F}, {1.00F, 0.66F, 0.36F}, 5.5F, 0.78F, 2.17F, {-7.8F, -5.02F, -10.0F}, {7.8F, -2.18F, 2.50F}, 0.45F, {2.80F, 1.05F}, {3.05F, 1.05F}},
        {{1.60F, -2.72F, 0.0F}, {1.00F, 0.66F, 0.36F}, 5.5F, 0.78F, 2.29F, {-7.8F, -5.02F, -10.0F}, {7.8F, -2.18F, 2.50F}, 0.45F, {2.80F, 1.05F}, {3.05F, 1.05F}},
        {{-1.60F, -2.72F, 7.0F}, {1.00F, 0.61F, 0.31F}, 6.0F, 0.70F, 2.41F, {-7.8F, -5.02F, 2.50F}, {7.8F, -2.18F, 15.20F}, 0.45F, {3.05F, 1.05F}, {0.0F, 1.15F}},
        {{1.50F, -2.72F, 19.0F}, {1.00F, 0.63F, 0.33F}, 6.8F, 0.70F, 2.57F, {-7.4F, -5.02F, 15.20F}, {7.4F, -2.18F, 31.0F}, 0.45F, {0.0F, 1.15F}, {0.0F, 0.0F}},
    }};

static_assert(
    kAmelieInteriorLights.size() <=
    kMaximumShipInteriorLights);

} // namespace

auto amelie_interior_lights() noexcept
    -> std::span<const ShipInteriorLight> {

    return kAmelieInteriorLights;
}

auto ship_interior_light_attenuation(
    const ShipInteriorLight& light,
    const glm::vec3& local_position,
    float time_seconds) noexcept -> float {

    const auto finite_vec3 =
        [](const glm::vec3& value) noexcept {
            return std::isfinite(value.x) &&
                   std::isfinite(value.y) &&
                   std::isfinite(value.z);
        };
    const auto finite_vec2 =
        [](const glm::vec2& value) noexcept {
            return std::isfinite(value.x) &&
                   std::isfinite(value.y);
        };
    if (!finite_vec3(light.local_position) ||
        !finite_vec3(local_position) ||
        !finite_vec3(light.zone_min) ||
        !finite_vec3(light.zone_max) ||
        !finite_vec2(light.minimum_z_door) ||
        !finite_vec2(light.maximum_z_door) ||
        !std::isfinite(light.radius) ||
        !std::isfinite(light.intensity) ||
        light.radius <= 0.0F ||
        light.intensity <= 0.0F) {
        return 0.0F;
    }

    const auto minimum =
        glm::min(
            light.zone_min,
            light.zone_max);
    const auto maximum =
        glm::max(
            light.zone_min,
            light.zone_max);
    if (local_position.x < minimum.x ||
        local_position.x > maximum.x ||
        local_position.y < minimum.y ||
        local_position.y > maximum.y) {
        return 0.0F;
    }

    auto outside_distance = 0.0F;
    auto doorway = glm::vec2 {0.0F};
    if (local_position.z < minimum.z) {
        outside_distance =
            minimum.z -
            local_position.z;
        doorway =
            light.minimum_z_door;
    } else if (local_position.z > maximum.z) {
        outside_distance =
            local_position.z -
            maximum.z;
        doorway =
            light.maximum_z_door;
    }
    const auto spill =
        std::isfinite(light.zone_spill)
            ? std::max(light.zone_spill, 0.0F)
            : 0.0F;
    if (outside_distance >
            spill + 1.0e-4F ||
        (outside_distance > 0.0F &&
         (doorway.y <= 0.0F ||
          std::abs(
              local_position.x -
              doorway.x) >
              doorway.y))) {
        return 0.0F;
    }

    auto zone_factor = 1.0F;
    if (outside_distance > 0.0F) {
        if (spill <= 1.0e-4F) {
            return 0.0F;
        }
        const auto edge =
            std::clamp(
                1.0F -
                    outside_distance /
                        spill,
                0.0F,
                1.0F);
        zone_factor =
            edge *
            edge *
            (3.0F -
             2.0F * edge);
    }

    const auto distance =
        glm::length(
            local_position -
            light.local_position);
    if (!std::isfinite(distance) ||
        distance >= light.radius) {
        return 0.0F;
    }
    const auto remaining =
        std::clamp(
            1.0F -
                distance /
                    light.radius,
            0.0F,
            1.0F);
    const auto smooth_falloff =
        remaining *
        remaining *
        (3.0F -
         2.0F * remaining);

    const auto safe_time =
        std::isfinite(time_seconds)
            ? time_seconds
            : 0.0F;
    const auto safe_seed =
        std::isfinite(light.flicker_seed)
            ? light.flicker_seed
            : 0.0F;
    // Je combine deux oscillations faibles : la flamme reste vivante sans
    // provoquer de clignotement perceptible ni rendre les tests aléatoires.
    const auto flicker =
        1.0F +
        std::sin(
            safe_time * 6.7F +
            safe_seed * 17.0F) *
            0.028F +
        std::sin(
            safe_time * 13.1F +
            safe_seed * 31.0F) *
            0.012F;

    return std::clamp(
        smooth_falloff *
            zone_factor *
            std::clamp(
                light.intensity,
                0.0F,
                4.0F) *
            flicker,
        0.0F,
        4.0F);
}

auto ship_exterior_light_attenuation(
    const ShipExteriorLight& light,
    const glm::vec3& local_position) noexcept -> float {

    const auto finite_vec3 =
        [](const glm::vec3& value) noexcept {
            return std::isfinite(value.x) &&
                   std::isfinite(value.y) &&
                   std::isfinite(value.z);
        };
    if (!finite_vec3(light.local_position) ||
        !finite_vec3(local_position) ||
        !std::isfinite(light.radius) ||
        !std::isfinite(light.intensity) ||
        !std::isfinite(light.minimum_y) ||
        !std::isfinite(light.maximum_y) ||
        light.radius <= 0.0F ||
        light.intensity <= 0.0F) {
        return 0.0F;
    }

    const auto minimum_y =
        std::min(
            light.minimum_y,
            light.maximum_y);
    const auto maximum_y =
        std::max(
            light.minimum_y,
            light.maximum_y);
    if (local_position.y < minimum_y ||
        local_position.y > maximum_y) {
        return 0.0F;
    }

    const auto distance =
        glm::length(
            local_position -
            light.local_position);
    if (!std::isfinite(distance) ||
        distance >= light.radius) {
        return 0.0F;
    }
    const auto remaining =
        std::clamp(
            1.0F -
                distance /
                    light.radius,
            0.0F,
            1.0F);
    const auto smooth_falloff =
        remaining *
        remaining *
        (3.0F -
         2.0F * remaining);

    return std::clamp(
        smooth_falloff *
            std::clamp(
                light.intensity,
                0.0F,
                4.0F),
        0.0F,
        4.0F);
}

auto ship_exterior_light_level(
    std::span<const ShipExteriorLight> lights,
    const glm::vec3& local_position) noexcept -> float {

    auto level = 0.0F;
    for (const auto& light :
         lights) {
        level =
            std::max(
                level,
                ship_exterior_light_attenuation(
                    light,
                    local_position));
    }
    // Je borne la valeur partagée pour que tous les consommateurs puissent
    // l'utiliser directement sans exposer leurs couleurs ni dépasser l'énergie
    // attendue au chevauchement de deux fanaux.
    return std::clamp(
        level,
        0.0F,
        1.0F);
}

auto ship_exterior_light_activation(
    float daylight_factor,
    float storm_intensity,
    float cloud_intensity,
    float overcast_intensity) noexcept -> float {

    const auto finite_or =
        [](float value, float fallback) noexcept {
            return std::isfinite(value)
                       ? value
                       : fallback;
        };
    const auto daylight =
        std::clamp(
            finite_or(
                daylight_factor,
                1.0F),
            0.0F,
            1.0F);
    const auto storm =
        std::clamp(
            finite_or(
                storm_intensity,
                0.0F),
            0.0F,
            1.0F);
    // Je réunis ici les deux représentations du ciel couvert pour que le
    // maillage, les marins et les gardes activent toujours les fanaux pareil.
    const auto cloud_cover =
        std::clamp(
            std::max(
                finite_or(
                    cloud_intensity,
                    0.0F),
                finite_or(
                    overcast_intensity,
                    0.0F)),
            0.0F,
            1.0F);
    const auto darkness =
        std::max(
            std::clamp(
                1.0F -
                    daylight,
                0.0F,
                1.0F),
            0.45F *
                storm *
                cloud_cover);
    return std::clamp(
        0.06F +
            0.94F *
                darkness,
        0.0F,
        1.0F);
}

} // namespace valcraft
