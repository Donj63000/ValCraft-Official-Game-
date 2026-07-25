#include "world/PrecipitationField.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/vec2.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace valcraft {

namespace {

constexpr float kPi = 3.14159265358979323846F;
constexpr float kTwoPi = 2.0F * kPi;
constexpr float kCameraQuantization = 4.0F;
constexpr float kPrecipitationCellSize = 4.0F;
constexpr float kMinimumRadius = 1.0F;
constexpr float kMinimumVerticalSpan = 18.0F;
constexpr float kMaximumSafeCoordinate = 10'000'000.0F;

auto hash_u32(std::uint32_t value) noexcept -> std::uint32_t {
    value ^= value >> 16U;
    value *= 0x7feb352dU;
    value ^= value >> 15U;
    value *= 0x846ca68bU;
    value ^= value >> 16U;
    return value;
}

auto hash_combine(std::uint32_t seed, std::uint32_t value) noexcept -> std::uint32_t {
    return hash_u32(seed ^ (hash_u32(value) + 0x9e3779b9U));
}

auto unit_random(std::uint32_t key, std::uint32_t salt) noexcept -> float {
    const auto mixed = hash_combine(key, salt);
    return static_cast<float>(mixed >> 8U) * (1.0F / 16777216.0F);
}

auto is_finite_position(const glm::vec3& value) noexcept -> bool {
    return std::isfinite(value.x) &&
           std::isfinite(value.y) &&
           std::isfinite(value.z) &&
           std::abs(value.x) <= kMaximumSafeCoordinate &&
           std::abs(value.y) <= kMaximumSafeCoordinate &&
           std::abs(value.z) <= kMaximumSafeCoordinate;
}

auto safe_unit_direction(const glm::vec2& direction) noexcept -> glm::vec2 {
    if (!std::isfinite(direction.x) || !std::isfinite(direction.y)) {
        return {0.0F, 1.0F};
    }
    const auto length_squared = glm::dot(direction, direction);
    if (!std::isfinite(length_squared) || length_squared <= 1.0e-6F) {
        return {0.0F, 1.0F};
    }
    return direction / std::sqrt(length_squared);
}

auto safe_unit_interval(float value) noexcept -> float {
    return std::isfinite(value) ? glm::clamp(value, 0.0F, 1.0F) : 0.0F;
}

auto safe_weather_time(float value) noexcept -> float {
    if (!std::isfinite(value)) {
        return 0.0F;
    }
    return glm::clamp(value, 0.0F, kMaximumWeatherTimeSeconds);
}

auto quantize_coordinate(float value) noexcept -> float {
    return std::floor(value / kCameraQuantization + 0.5F) * kCameraQuantization;
}

auto quantized_camera_anchor(const glm::vec3& camera_position) noexcept -> glm::vec3 {
    return {
        quantize_coordinate(camera_position.x),
        quantize_coordinate(camera_position.y),
        quantize_coordinate(camera_position.z),
    };
}

auto positive_modulo(float value, float period) noexcept -> float {
    auto result = std::fmod(value, period);
    if (result < 0.0F) {
        result += period;
    }
    return result;
}

auto wrap_around_anchor(float value, float anchor, float radius) noexcept -> float {
    const auto width = radius * 2.0F;
    return anchor + positive_modulo(value - anchor + radius, width) - radius;
}

auto active_instance_count(std::size_t maximum, float intensity) noexcept -> std::size_t {
    if (maximum == 0U || intensity <= 0.0F) {
        return 0U;
    }
    const auto scaled = static_cast<std::size_t>(
        std::floor(static_cast<float>(maximum) * intensity));
    return std::max<std::size_t>(scaled, 1U);
}

auto make_instance_key(
    std::uint32_t field_key,
    std::int32_t cell_x,
    std::int32_t cell_z,
    std::size_t layer,
    std::uint32_t salt) noexcept -> std::uint32_t {
    const auto wide_layer =
        static_cast<std::uint64_t>(
            layer);
    const auto folded_layer =
        static_cast<std::uint32_t>(wide_layer) ^
        static_cast<std::uint32_t>(wide_layer >> 32U);
    auto key =
        hash_combine(
            field_key,
            static_cast<std::uint32_t>(
                cell_x));
    key =
        hash_combine(
            key,
            static_cast<std::uint32_t>(
                cell_z));
    key =
        hash_combine(
            key,
            folded_layer);
    return hash_combine(
        key,
        salt);
}

auto make_cycle_key(
    std::uint32_t instance_key,
    std::int64_t cycle_index,
    std::uint32_t salt) noexcept -> std::uint32_t {
    const auto wide_cycle =
        static_cast<std::uint64_t>(
            cycle_index);
    const auto folded_cycle =
        static_cast<std::uint32_t>(wide_cycle) ^
        static_cast<std::uint32_t>(wide_cycle >> 32U);
    return hash_combine(
        hash_combine(
            instance_key,
            folded_cycle),
        salt);
}

struct PrecipitationCell {
    std::int32_t x = 0;
    std::int32_t z = 0;
    std::size_t layer = 0U;
};

auto precipitation_cell_for_instance(
    const glm::vec3& anchor,
    float radius,
    std::size_t index,
    std::size_t sequence_offset) noexcept -> PrecipitationCell {
    const auto half_extent =
        std::max(
            static_cast<int>(
                std::floor(
                    (radius -
                     kPrecipitationCellSize * 0.5F) /
                    kPrecipitationCellSize)),
            0);
    const auto dimension =
        static_cast<std::size_t>(
            half_extent * 2 + 1);
    const auto cell_count =
        dimension *
        dimension;
    const auto layer =
        index /
        cell_count;
    const auto local_index =
        index %
        cell_count;
    const auto permuted_index =
        (local_index * 73U +
         layer * 37U +
         sequence_offset) %
        cell_count;
    const auto offset_x =
        static_cast<int>(
            permuted_index %
            dimension) -
        half_extent;
    const auto offset_z =
        static_cast<int>(
            permuted_index /
            dimension) -
        half_extent;
    const auto anchor_cell_x =
        static_cast<std::int32_t>(
            std::lround(
                anchor.x /
                kPrecipitationCellSize));
    const auto anchor_cell_z =
        static_cast<std::int32_t>(
            std::lround(
                anchor.z /
                kPrecipitationCellSize));

    return {
        static_cast<std::int32_t>(
            anchor_cell_x +
            offset_x),
        static_cast<std::int32_t>(
            anchor_cell_z +
            offset_z),
        layer,
    };
}

auto precipitation_cell_center(std::int32_t coordinate) noexcept -> float {
    return static_cast<float>(
               coordinate) *
           kPrecipitationCellSize;
}

} // namespace

PrecipitationField::PrecipitationField(std::uint32_t seed) noexcept
    : seed_(seed) {
}

auto PrecipitationField::sample(
    const EnvironmentState& environment,
    const glm::vec3& camera_position,
    float surface_height,
    const PrecipitationBudget& budget) -> const PrecipitationFrame& {
    clear();

    if (!std::isfinite(environment.precipitation_intensity) ||
        environment.precipitation_intensity <= 0.0F ||
        !is_finite_position(camera_position) ||
        !std::isfinite(surface_height) ||
        std::abs(surface_height) > kMaximumSafeCoordinate ||
        !std::isfinite(budget.radius) ||
        budget.radius <= 0.0F) {
        return frame_;
    }

    const auto intensity =
        glm::clamp(
            environment.precipitation_intensity,
            0.0F,
            1.0F);
    const auto storm =
        safe_unit_interval(
            environment.storm_intensity);
    const auto violent_storm =
        safe_unit_interval(
            environment.violent_storm_intensity);
    const auto wind_strength =
        safe_unit_interval(
            environment.wind_strength);
    const auto wind_direction =
        safe_unit_direction(
            environment.wind_direction_xz);
    const auto density =
        glm::clamp(
            intensity *
                (1.0F +
                 storm * 0.18F +
                 violent_storm * 0.12F),
            0.0F,
            1.0F);
    const auto time_seconds =
        safe_weather_time(
            environment.weather_time_seconds);
    const auto radius =
        glm::clamp(
            budget.radius,
            kMinimumRadius,
            kMaximumPrecipitationRadius);
    const auto drop_budget =
        std::min(
            budget.max_drops,
            kMaximumPrecipitationDropBudget);
    const auto impact_budget =
        std::min(
            budget.max_impacts,
            kMaximumPrecipitationImpactBudget);
    const auto drop_count =
        active_instance_count(
            drop_budget,
            density);
    const auto impact_count =
        active_instance_count(
            impact_budget,
            density);

    if (drop_count == 0U && impact_count == 0U) {
        return frame_;
    }

    const auto anchor =
        quantized_camera_anchor(
            camera_position);
    const auto field_key =
        hash_u32(
            seed_);
    const auto lower_y = surface_height + 0.035F;
    const auto minimum_top_y =
        lower_y +
        std::max(
            kMinimumVerticalSpan,
            radius * 1.45F);
    const auto camera_top_y =
        anchor.y +
        std::max(
            8.0F,
            radius * 0.62F);
    const auto top_y =
        std::max(
            minimum_top_y,
            camera_top_y);
    const auto vertical_span =
        std::max(
            top_y - lower_y,
            kMinimumVerticalSpan);
    const auto base_fall_speed =
        17.0F +
        intensity * 7.0F +
        storm * 5.0F +
        violent_storm * 7.0F;
    const auto base_horizontal_speed =
        (1.4F +
         wind_strength * 5.2F +
         storm * 3.8F +
         violent_storm * 3.0F) *
        (0.45F + intensity * 0.55F);

    frame_.drops.reserve(drop_count);
    for (std::size_t index = 0U; index < drop_count; ++index) {
        const auto cell =
            precipitation_cell_for_instance(
                anchor,
                radius,
                index,
                0U);
        const auto instance_key =
            make_instance_key(
                field_key,
                cell.x,
                cell.z,
                cell.layer,
                0xD20F4A11U);
        const auto fall_speed =
            base_fall_speed *
            glm::mix(
                0.86F,
                1.16F,
                unit_random(instance_key, 1U));
        const auto cycle_seconds =
            vertical_span /
            std::max(
                fall_speed,
                1.0F);
        const auto cycle_time =
            time_seconds +
            unit_random(instance_key, 2U) *
                cycle_seconds;
        const auto cycle_index =
            static_cast<std::int64_t>(
                std::floor(
                    cycle_time /
                    cycle_seconds));
        const auto elapsed =
            positive_modulo(
                cycle_time,
                cycle_seconds);
        const auto key =
            make_cycle_key(
                instance_key,
                cycle_index,
                0xB7E15163U);
        const auto lateral_jitter_angle =
            unit_random(key, 3U) *
            kTwoPi;
        const auto jitter_direction =
            glm::vec2 {
                std::cos(lateral_jitter_angle),
                std::sin(lateral_jitter_angle),
            };
        const auto horizontal_velocity =
            wind_direction *
                base_horizontal_speed *
                glm::mix(
                    0.82F,
                    1.18F,
                    unit_random(key, 4U)) +
            jitter_direction *
                glm::mix(
                    0.05F,
                    0.55F,
                    storm) *
                unit_random(key, 5U);
        const auto base_x =
            precipitation_cell_center(
                cell.x) +
            glm::mix(
                -kPrecipitationCellSize * 0.5F,
                kPrecipitationCellSize * 0.5F,
                unit_random(key, 6U));
        const auto base_z =
            precipitation_cell_center(
                cell.z) +
            glm::mix(
                -kPrecipitationCellSize * 0.5F,
                kPrecipitationCellSize * 0.5F,
                unit_random(key, 7U));
        const auto x =
            wrap_around_anchor(
                base_x +
                    horizontal_velocity.x *
                        elapsed,
                precipitation_cell_center(
                    cell.x),
                kPrecipitationCellSize * 0.5F);
        const auto z =
            wrap_around_anchor(
                base_z +
                    horizontal_velocity.y *
                        elapsed,
                precipitation_cell_center(
                    cell.z),
                kPrecipitationCellSize * 0.5F);
        const auto length =
            (0.42F +
             fall_speed * 0.035F +
             intensity * 0.38F +
             storm * 0.30F +
             violent_storm * 0.42F) *
            glm::mix(
                0.84F,
                1.18F,
                unit_random(key, 8U));
        const auto width =
            (0.012F +
             intensity * 0.018F +
             violent_storm * 0.010F) *
            glm::mix(
                0.82F,
                1.18F,
                unit_random(key, 9U));
        const auto opacity =
            glm::clamp(
                (0.26F +
                 intensity * 0.44F +
                 storm * 0.14F +
                 violent_storm * 0.10F) *
                    glm::mix(
                        0.78F,
                        1.0F,
                        unit_random(key, 10U)),
                0.0F,
                1.0F);

        frame_.drops.push_back({
            {x, top_y - elapsed * fall_speed, z},
            {horizontal_velocity.x, -fall_speed, horizontal_velocity.y},
            length,
            width,
            opacity,
            key,
        });
    }

    frame_.impacts.reserve(impact_count);
    for (std::size_t index = 0U; index < impact_count; ++index) {
        const auto cell =
            precipitation_cell_for_instance(
                anchor,
                radius,
                index,
                19U);
        const auto instance_key =
            make_instance_key(
                field_key,
                cell.x,
                cell.z,
                cell.layer,
                0x1A6C7E33U);
        const auto lifetime =
            glm::mix(
                0.26F,
                0.46F,
                unit_random(instance_key, 23U)) +
            storm * 0.10F +
            violent_storm * 0.08F;
        const auto cycle_time =
            time_seconds +
            unit_random(instance_key, 24U) *
                lifetime;
        const auto cycle_index =
            static_cast<std::int64_t>(
                std::floor(
                    cycle_time /
                    lifetime));
        const auto age =
            positive_modulo(
                cycle_time,
                lifetime);
        const auto key =
            make_cycle_key(
                instance_key,
                cycle_index,
                0x243F6A88U);
        const auto raw_impact_position =
            glm::vec2 {
            precipitation_cell_center(
                cell.x) +
                glm::mix(
                    -kPrecipitationCellSize * 0.5F,
                    kPrecipitationCellSize * 0.5F,
                    unit_random(key, 21U)),
            precipitation_cell_center(
                cell.z) +
                glm::mix(
                    -kPrecipitationCellSize * 0.5F,
                    kPrecipitationCellSize * 0.5F,
                    unit_random(key, 22U)),
        };
        const auto impact_offset =
            raw_impact_position -
            glm::vec2 {
                anchor.x,
                anchor.z,
            };
        const auto impact_distance =
            glm::length(
                impact_offset);
        const auto maximum_impact_distance =
            radius *
            0.98F;
        const auto impact_position =
            impact_distance > maximum_impact_distance
                ? glm::vec2 {
                      anchor.x,
                      anchor.z,
                  } +
                      impact_offset *
                          (maximum_impact_distance /
                           impact_distance)
                : raw_impact_position;
        const auto progress =
            glm::clamp(
                age /
                    std::max(
                        lifetime,
                        0.001F),
                0.0F,
                1.0F);
        const auto ripple_radius =
            (0.045F +
             intensity * 0.18F +
             storm * 0.16F +
             violent_storm * 0.16F) *
            std::sqrt(progress);
        const auto opacity =
            intensity *
            (1.0F - progress) *
            (1.0F - progress) *
            glm::mix(
                0.62F,
                0.92F,
                unit_random(key, 25U));

        frame_.impacts.push_back({
            {
                impact_position.x,
                lower_y,
                impact_position.y,
            },
            age,
            lifetime,
            ripple_radius,
            glm::clamp(opacity, 0.0F, 1.0F),
            key,
        });
    }

    return frame_;
}

auto PrecipitationField::frame() const noexcept -> const PrecipitationFrame& {
    return frame_;
}

auto PrecipitationField::seed() const noexcept -> std::uint32_t {
    return seed_;
}

void PrecipitationField::clear() noexcept {
    // Je conserve les capacités pour éviter les allocations à chaque image.
    frame_.drops.clear();
    frame_.impacts.clear();
}

} // namespace valcraft
