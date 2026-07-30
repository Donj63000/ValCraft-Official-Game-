#include "render/OceanLifeField.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace valcraft {
namespace {

constexpr float kPi = 3.14159265358979323846F;
constexpr float kTwoPi = kPi * 2.0F;
constexpr float kMaximumSafeCoordinate = 1'000'000.0F;
constexpr float kSchoolValidationExtent = 4.0F;
constexpr float kMaximumSchoolOrbitRadius = 1.35F;
constexpr float kBottomClearance = 0.55F;
constexpr float kSurfaceClearance = 0.78F;
constexpr int kMaximumCellRadius = 4;
constexpr std::size_t kMaximumCandidateCount =
    static_cast<std::size_t>(
        (kMaximumCellRadius * 2 + 1) *
        (kMaximumCellRadius * 2 + 1));

struct OceanLifeCandidate {
    int cell_x = 0;
    int cell_z = 0;
    std::uint32_t key = 0U;
    float base_x = 0.0F;
    float base_z = 0.0F;
    float distance_squared = 0.0F;
    bool preferred = false;
    bool surface_checked = false;
    bool surface_valid = false;
    float water_surface_y = 0.0F;
    float bottom_y = 0.0F;
};

[[nodiscard]] constexpr auto mix_hash(
    std::uint32_t value) noexcept -> std::uint32_t {
    value ^= value >> 16U;
    value *= 0x7feb352dU;
    value ^= value >> 15U;
    value *= 0x846ca68bU;
    value ^= value >> 16U;
    return value;
}

[[nodiscard]] constexpr auto hash_cell(
    std::uint32_t seed,
    int cell_x,
    int cell_z,
    std::uint32_t salt) noexcept -> std::uint32_t {
    auto hash = mix_hash(seed ^ salt);
    hash = mix_hash(
        hash ^
        static_cast<std::uint32_t>(cell_x) *
            0x9e3779b9U);
    hash = mix_hash(
        hash ^
        static_cast<std::uint32_t>(cell_z) *
            0x85ebca6bU);
    return hash;
}

[[nodiscard]] constexpr auto hash_member(
    std::uint32_t school_key,
    std::size_t member_index,
    std::uint32_t salt) noexcept -> std::uint32_t {
    return mix_hash(
        school_key ^
        static_cast<std::uint32_t>(member_index) *
            0x9e3779b9U ^
        salt);
}

[[nodiscard]] constexpr auto unit_random(
    std::uint32_t value) noexcept -> float {
    return static_cast<float>(value >> 8U) *
           (1.0F / 16'777'216.0F);
}

[[nodiscard]] constexpr auto signed_random(
    std::uint32_t value) noexcept -> float {
    return unit_random(value) * 2.0F - 1.0F;
}

[[nodiscard]] auto finite_camera_position(
    const glm::vec3& position) noexcept -> bool {
    return std::isfinite(position.x) &&
           std::isfinite(position.y) &&
           std::isfinite(position.z) &&
           std::abs(position.x) <= kMaximumSafeCoordinate &&
           std::abs(position.z) <= kMaximumSafeCoordinate;
}

[[nodiscard]] auto floor_cell_coordinate(
    float coordinate) noexcept -> int {
    return static_cast<int>(
        std::floor(
            coordinate /
            kOceanLifeCellSize));
}

[[nodiscard]] auto normalized_direction(
    const glm::vec2& direction,
    float fallback_angle) noexcept -> glm::vec2 {
    const auto length_squared =
        glm::dot(direction, direction);
    if (!std::isfinite(length_squared) ||
        length_squared <=
            std::numeric_limits<float>::epsilon()) {
        return {
            std::cos(fallback_angle),
            std::sin(fallback_angle),
        };
    }
    return direction /
           std::sqrt(length_squared);
}

[[nodiscard]] auto clamped_budget(
    const OceanLifeBudget& source) noexcept -> OceanLifeBudget {
    return {
        std::min(
            source.max_schools,
            kOceanLifeMaximumSchoolCount),
        std::min(
            source.fish_per_school,
            kOceanLifeMaximumFishPerSchool),
        std::isfinite(source.radius)
            ? glm::clamp(source.radius, 0.0F, 56.0F)
            : 0.0F,
    };
}

[[nodiscard]] auto preferred_school_probability(
    const OceanLifeBudget& budget) noexcept -> float {
    const auto cells_in_radius =
        kPi *
        (budget.radius / kOceanLifeCellSize) *
        (budget.radius / kOceanLifeCellSize);
    return glm::clamp(
        (static_cast<float>(budget.max_schools) + 0.75F) /
            std::max(cells_in_radius, 1.0F),
        0.18F,
        0.72F);
}

void evaluate_candidate_surface(
    OceanLifeCandidate& candidate,
    OceanLifeSurfaceSampler sampler) {
    if (candidate.surface_checked) {
        return;
    }
    candidate.surface_checked = true;

    constexpr std::array<glm::vec2, 5U> offsets {{
        {0.0F, 0.0F},
        {kSchoolValidationExtent, 0.0F},
        {-kSchoolValidationExtent, 0.0F},
        {0.0F, kSchoolValidationExtent},
        {0.0F, -kSchoolValidationExtent},
    }};

    auto minimum_water_surface =
        std::numeric_limits<float>::max();
    auto maximum_bottom =
        std::numeric_limits<float>::lowest();

    for (const auto& offset : offsets) {
        const auto world_x =
            static_cast<int>(
                std::floor(
                    candidate.base_x +
                    offset.x));
        const auto world_z =
            static_cast<int>(
                std::floor(
                    candidate.base_z +
                    offset.y));
        const auto surface =
            sampler(world_x, world_z);
        if (surface.water_level <= surface.surface_height) {
            return;
        }

        minimum_water_surface =
            std::min(
                minimum_water_surface,
                static_cast<float>(
                    surface.water_level) +
                    1.0F);
        maximum_bottom =
            std::max(
                maximum_bottom,
                static_cast<float>(
                    surface.surface_height) +
                    1.0F);
    }

    const auto water_depth =
        minimum_water_surface -
        maximum_bottom;
    if (!std::isfinite(water_depth) ||
        water_depth < kOceanLifeMinimumWaterDepth) {
        return;
    }

    candidate.surface_valid = true;
    candidate.water_surface_y =
        minimum_water_surface;
    candidate.bottom_y =
        maximum_bottom;
}

[[nodiscard]] auto packed_visual(
    std::uint32_t school_key,
    std::size_t member_index) noexcept -> std::uint32_t {
    const auto palette =
        hash_member(
            school_key,
            0U,
            0x46534843U) &
        0x3U;
    const auto school_id =
        mix_hash(
            school_key ^
            0x5343484fU) &
        0x07ffffffU;
    return
        (palette << 30U) |
        (school_id << 3U) |
        (static_cast<std::uint32_t>(member_index) &
         0x7U);
}

} // namespace

auto ocean_life_instance_direction(
    const OceanLifeInstance& instance) noexcept -> glm::vec2 {
    if (!std::isfinite(instance.heading_radians)) {
        return {1.0F, 0.0F};
    }
    return {
        std::cos(instance.heading_radians),
        std::sin(instance.heading_radians),
    };
}

auto ocean_life_instance_color(
    const OceanLifeInstance& instance) noexcept -> glm::vec3 {
    constexpr std::array<glm::vec3, 4U> palette {{
        {0.24F, 0.48F, 0.52F},
        {0.58F, 0.67F, 0.66F},
        {0.58F, 0.40F, 0.20F},
        {0.28F, 0.34F, 0.52F},
    }};
    return palette[
        ocean_life_instance_palette_index(instance)];
}

OceanLifeField::OceanLifeField() {
    frame_.instances.reserve(
        kOceanLifeMaximumInstanceCount);
}

auto OceanLifeField::sample(
    WorldGenerationProfile profile,
    std::uint32_t world_seed,
    const glm::vec3& camera_position,
    float absolute_time_seconds,
    const OceanLifeBudget& requested_budget,
    OceanLifeSurfaceSampler surface_sampler)
    -> const OceanLifeFrame& {
    clear();

    const auto budget =
        clamped_budget(requested_budget);
    if (profile !=
            WorldGenerationProfile::OceanAdventure ||
        !surface_sampler.valid() ||
        !finite_camera_position(camera_position) ||
        !std::isfinite(absolute_time_seconds) ||
        budget.max_schools == 0U ||
        budget.fish_per_school == 0U ||
        budget.radius <= 0.0F) {
        return frame_;
    }

    const auto center_cell_x =
        floor_cell_coordinate(camera_position.x);
    const auto center_cell_z =
        floor_cell_coordinate(camera_position.z);
    const auto cell_radius =
        std::min(
            static_cast<int>(
                std::ceil(
                    (budget.radius +
                     kMaximumSchoolOrbitRadius) /
                    kOceanLifeCellSize)) +
                1,
            kMaximumCellRadius);
    const auto probability =
        preferred_school_probability(budget);
    const auto maximum_candidate_distance =
        budget.radius +
        kMaximumSchoolOrbitRadius;
    const auto maximum_candidate_distance_squared =
        maximum_candidate_distance *
        maximum_candidate_distance;

    std::array<
        OceanLifeCandidate,
        kMaximumCandidateCount>
        candidates {};
    std::size_t candidate_count = 0U;

    for (int offset_z = -cell_radius;
         offset_z <= cell_radius;
         ++offset_z) {
        for (int offset_x = -cell_radius;
             offset_x <= cell_radius;
             ++offset_x) {
            const auto cell_x =
                center_cell_x + offset_x;
            const auto cell_z =
                center_cell_z + offset_z;
            const auto key =
                hash_cell(
                    world_seed,
                    cell_x,
                    cell_z,
                    0x4f434541U);
            const auto base_x =
                static_cast<float>(cell_x) *
                    kOceanLifeCellSize +
                2.0F +
                unit_random(
                    mix_hash(
                        key ^
                        0x13579bdfU)) *
                    12.0F;
            const auto base_z =
                static_cast<float>(cell_z) *
                    kOceanLifeCellSize +
                2.0F +
                unit_random(
                    mix_hash(
                        key ^
                        0x2468ace0U)) *
                    12.0F;
            const auto dx =
                base_x -
                camera_position.x;
            const auto dz =
                base_z -
                camera_position.z;
            const auto distance_squared =
                dx * dx +
                dz * dz;
            if (!std::isfinite(distance_squared) ||
                distance_squared >
                    maximum_candidate_distance_squared ||
                candidate_count >=
                    candidates.size()) {
                continue;
            }

            candidates[candidate_count++] = {
                cell_x,
                cell_z,
                key,
                base_x,
                base_z,
                distance_squared,
                unit_random(
                    mix_hash(
                        key ^
                        0xa511e9b3U)) <
                    probability,
            };
        }
    }

    std::sort(
        candidates.begin(),
        candidates.begin() +
            static_cast<std::ptrdiff_t>(
                candidate_count),
        [](const OceanLifeCandidate& left,
           const OceanLifeCandidate& right) noexcept {
            if (left.distance_squared !=
                right.distance_squared) {
                return left.distance_squared <
                       right.distance_squared;
            }
            if (left.cell_x != right.cell_x) {
                return left.cell_x < right.cell_x;
            }
            return left.cell_z < right.cell_z;
        });

    const auto time =
        static_cast<double>(
            absolute_time_seconds);
    const auto append_school =
        [&](OceanLifeCandidate& candidate) -> bool {
        evaluate_candidate_surface(
            candidate,
            surface_sampler);
        if (!candidate.surface_valid) {
            return false;
        }

        const auto orbit_x =
            0.55F +
            unit_random(
                mix_hash(
                    candidate.key ^
                    0x51ed270bU)) *
                0.80F;
        const auto orbit_z =
            0.55F +
            unit_random(
                mix_hash(
                    candidate.key ^
                    0x68bc21ebU)) *
                0.80F;
        const auto angular_speed =
            (0.09F +
             unit_random(
                 mix_hash(
                     candidate.key ^
                     0x02e5be93U)) *
                 0.10F) *
            ((candidate.key & 1U) == 0U
                 ? 1.0F
                 : -1.0F);
        const auto initial_angle =
            unit_random(
                mix_hash(
                    candidate.key ^
                    0x967a889bU)) *
            kTwoPi;
        const auto angle =
            static_cast<float>(
                static_cast<double>(
                    initial_angle) +
                time *
                    static_cast<double>(
                        angular_speed));
        const glm::vec2 school_center {
            candidate.base_x +
                std::cos(angle) *
                    orbit_x,
            candidate.base_z +
                std::sin(angle) *
                    orbit_z,
        };
        const auto distance =
            glm::length(
                school_center -
                glm::vec2 {
                    camera_position.x,
                    camera_position.z,
                });
        const auto fade =
            ocean_life_distance_fade(
                distance,
                budget.radius);
        if (!(fade > 0.0F) ||
            !std::isfinite(fade)) {
            return false;
        }

        const auto fallback_heading =
            unit_random(
                mix_hash(
                    candidate.key ^
                    0xe0f4c725U)) *
            kTwoPi;
        const auto school_direction =
            normalized_direction(
                {
                    -std::sin(angle) *
                        orbit_x *
                        angular_speed,
                    std::cos(angle) *
                        orbit_z *
                        angular_speed,
                },
                fallback_heading);
        const glm::vec2 school_perpendicular {
            -school_direction.y,
            school_direction.x,
        };
        const auto row_count =
            (budget.fish_per_school + 1U) /
            2U;
        const auto available_depth =
            candidate.water_surface_y -
            candidate.bottom_y;
        const auto vertical_band =
            mix_hash(
                candidate.key ^
                0x7f4a7c15U) %
            3U;
        constexpr std::array<float, 3U> kVerticalBandFractions {
            0.16F,
            0.48F,
            0.78F,
        };
        const auto depth_jitter =
            signed_random(
                mix_hash(
                    candidate.key ^
                    0x94d049bbU)) *
            0.08F;
        const auto usable_depth =
            std::max(
                available_depth -
                    kSurfaceClearance -
                    kBottomClearance,
                0.0F);
        const auto school_depth =
            glm::clamp(
                kSurfaceClearance +
                    usable_depth *
                        glm::clamp(
                            kVerticalBandFractions[vertical_band] +
                                depth_jitter,
                            0.08F,
                            0.92F),
                kSurfaceClearance,
                available_depth -
                    kBottomClearance);

        for (std::size_t member_index = 0U;
             member_index <
             budget.fish_per_school;
             ++member_index) {
            const auto member_key =
                hash_member(
                    candidate.key,
                    member_index,
                    0x46495348U);
            const auto row =
                member_index / 2U;
            const auto side =
                member_index % 2U;
            const auto centered_row =
                static_cast<float>(row) -
                static_cast<float>(
                    row_count - 1U) *
                    0.5F;
            const auto phase =
                unit_random(
                    mix_hash(
                        member_key ^
                        0xb5297a4dU)) *
                kTwoPi;
            const auto along =
                centered_row *
                    0.52F +
                signed_random(
                    mix_hash(
                        member_key ^
                        0x1b56c4e9U)) *
                    0.08F;
            const auto side_sign =
                side == 0U
                    ? -1.0F
                    : 1.0F;
            const auto lateral =
                side_sign *
                    (0.24F +
                     unit_random(
                         mix_hash(
                             member_key ^
                             0xc2b2ae35U)) *
                         0.08F) +
                std::sin(
                    static_cast<float>(
                        time * 1.15) +
                    phase) *
                    0.045F;
            const auto heading_wobble =
                std::sin(
                    static_cast<float>(
                        time * 0.72) +
                    phase) *
                0.075F;
            const auto base_heading =
                std::atan2(
                    school_direction.y,
                    school_direction.x);
            const auto heading =
                base_heading +
                heading_wobble;
            const auto vertical_offset =
                std::sin(
                    static_cast<float>(
                        time * 0.86) +
                    phase) *
                    0.065F +
                signed_random(
                    mix_hash(
                        member_key ^
                        0x27d4eb2fU)) *
                    0.035F;
            const auto position_xz =
                school_center +
                school_direction *
                    along +
                school_perpendicular *
                    lateral;
            const auto scale =
                0.22F +
                unit_random(
                    mix_hash(
                        member_key ^
                        0x165667b1U)) *
                    0.16F;

            frame_.instances.push_back({
                {
                    position_xz.x,
                    candidate.water_surface_y -
                        school_depth +
                        vertical_offset,
                    position_xz.y,
                },
                scale,
                heading,
                phase,
                fade,
                packed_visual(
                    candidate.key,
                    member_index),
            });
        }

        ++frame_.school_count;
        return true;
    };

    for (int preferred_pass = 1;
         preferred_pass >= 0 &&
         frame_.school_count <
             budget.max_schools;
         --preferred_pass) {
        const auto require_preferred =
            preferred_pass != 0;
        for (std::size_t index = 0U;
             index < candidate_count &&
             frame_.school_count <
                 budget.max_schools;
             ++index) {
            auto& candidate =
                candidates[index];
            if (candidate.preferred !=
                require_preferred) {
                continue;
            }
            static_cast<void>(
                append_school(candidate));
        }
    }

    return frame_;
}

auto OceanLifeField::frame() const noexcept
    -> const OceanLifeFrame& {
    return frame_;
}

void OceanLifeField::clear() noexcept {
    // Je conserve la capacite maximale pour ne jamais allouer en regime etabli.
    frame_.instances.clear();
    frame_.school_count = 0U;
}

} // namespace valcraft
