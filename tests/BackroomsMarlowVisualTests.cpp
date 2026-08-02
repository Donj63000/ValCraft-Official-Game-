#include "render/BackroomsMarlowVisual.h"
#include "render/VisualEntityPrimitives.h"

#include <doctest/doctest.h>

#include <glm/common.hpp>
#include <glm/vec4.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

namespace valcraft {

namespace {

struct MarlowBounds {
    glm::vec3 minimum {std::numeric_limits<float>::max()};
    glm::vec3 maximum {std::numeric_limits<float>::lowest()};
};

[[nodiscard]] auto part_bounds(
    const CreaturePartInstance& part) noexcept -> MarlowBounds {
    static constexpr std::array<glm::vec3, 8> kCorners {{
        {-0.5F, -0.5F, -0.5F},
        {-0.5F, -0.5F, 0.5F},
        {-0.5F, 0.5F, -0.5F},
        {-0.5F, 0.5F, 0.5F},
        {0.5F, -0.5F, -0.5F},
        {0.5F, -0.5F, 0.5F},
        {0.5F, 0.5F, -0.5F},
        {0.5F, 0.5F, 0.5F},
    }};
    MarlowBounds bounds {};
    for (const auto& corner : kCorners) {
        const auto world = part.transform * glm::vec4 {corner, 1.0F};
        bounds.minimum = glm::min(bounds.minimum, glm::vec3 {world});
        bounds.maximum = glm::max(bounds.maximum, glm::vec3 {world});
    }
    return bounds;
}

[[nodiscard]] auto visual_bounds(
    const std::vector<CreaturePartInstance>& parts) noexcept
    -> MarlowBounds {
    MarlowBounds bounds {};
    for (const auto& part : parts) {
        const auto current = part_bounds(part);
        bounds.minimum = glm::min(bounds.minimum, current.minimum);
        bounds.maximum = glm::max(bounds.maximum, current.maximum);
    }
    return bounds;
}

[[nodiscard]] auto part_tile(
    const CreaturePartInstance& part) noexcept -> CreatureAtlasTile {
    const auto tile_x = static_cast<int>(
        std::lround(
            part.face_uvs.front().u0 * kCreatureAtlasTilesPerAxis));
    const auto tile_y = static_cast<int>(
        std::lround(
            part.face_uvs.front().v0 * kCreatureAtlasTilesPerAxis));
    return static_cast<CreatureAtlasTile>(
        tile_y * static_cast<int>(kCreatureAtlasTilesPerAxis) + tile_x);
}

[[nodiscard]] auto count_tile(
    const std::vector<CreaturePartInstance>& parts,
    CreatureAtlasTile tile) noexcept -> std::size_t {
    return static_cast<std::size_t>(
        std::count_if(
            parts.begin(),
            parts.end(),
            [tile](const CreaturePartInstance& part) {
                return part_tile(part) == tile;
            }));
}

[[nodiscard]] auto matrix_is_finite(const glm::mat4& matrix) noexcept -> bool {
    for (glm::mat4::length_type column = 0;
         column < matrix.length();
         ++column) {
        for (glm::mat4::length_type row = 0;
             row < matrix[column].length();
             ++row) {
            if (!std::isfinite(matrix[column][row])) {
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] auto same_part(
    const CreaturePartInstance& lhs,
    const CreaturePartInstance& rhs) noexcept -> bool {
    for (glm::mat4::length_type column = 0;
         column < lhs.transform.length();
         ++column) {
        for (glm::mat4::length_type row = 0;
             row < lhs.transform[column].length();
             ++row) {
            if (lhs.transform[column][row] != rhs.transform[column][row]) {
                return false;
            }
        }
    }
    for (std::size_t face = 0U; face < lhs.face_uvs.size(); ++face) {
        const auto& a = lhs.face_uvs[face];
        const auto& b = rhs.face_uvs[face];
        if (a.u0 != b.u0 || a.v0 != b.v0 ||
            a.u1 != b.u1 || a.v1 != b.v1) {
            return false;
        }
    }
    return lhs.nightmare_factor == rhs.nightmare_factor &&
           lhs.tension == rhs.tension &&
           lhs.material_class == rhs.material_class &&
           lhs.cavity_mask == rhs.cavity_mask &&
           lhs.emissive_strength == rhs.emissive_strength &&
           lhs.sky_light == rhs.sky_light &&
           lhs.block_light == rhs.block_light &&
           lhs.precipitation_exposure == rhs.precipitation_exposure;
}

[[nodiscard]] auto part_dimensions(
    const CreaturePartInstance& part) noexcept -> glm::vec3 {
    return {
        glm::length(glm::vec3 {part.transform[0]}),
        glm::length(glm::vec3 {part.transform[1]}),
        glm::length(glm::vec3 {part.transform[2]}),
    };
}

[[nodiscard]] auto tile_channel_minimum(
    const std::vector<std::uint8_t>& atlas,
    CreatureAtlasTile tile,
    std::size_t channel) -> std::uint8_t {
    const auto coordinates = creature_atlas_tile_coordinates(tile);
    auto result = std::numeric_limits<std::uint8_t>::max();
    for (int y = 0; y < kCreatureAtlasTileSize; ++y) {
        for (int x = 0; x < kCreatureAtlasTileSize; ++x) {
            const auto atlas_x =
                coordinates[0] * kCreatureAtlasTileSize + x;
            const auto atlas_y =
                coordinates[1] * kCreatureAtlasTileSize + y;
            const auto offset = static_cast<std::size_t>(
                (atlas_y * kCreatureAtlasSize + atlas_x) * 4) + channel;
            result = std::min(result, atlas[offset]);
        }
    }
    return result;
}

[[nodiscard]] auto tile_average_rgb(
    const std::vector<std::uint8_t>& atlas,
    CreatureAtlasTile tile) -> glm::vec3 {
    const auto coordinates = creature_atlas_tile_coordinates(tile);
    std::array<std::uint64_t, 3> sums {};
    for (int y = 0; y < kCreatureAtlasTileSize; ++y) {
        for (int x = 0; x < kCreatureAtlasTileSize; ++x) {
            const auto atlas_x =
                coordinates[0] * kCreatureAtlasTileSize + x;
            const auto atlas_y =
                coordinates[1] * kCreatureAtlasTileSize + y;
            const auto offset = static_cast<std::size_t>(
                (atlas_y * kCreatureAtlasSize + atlas_x) * 4);
            for (std::size_t channel = 0U; channel < sums.size(); ++channel) {
                sums[channel] += atlas[offset + channel];
            }
        }
    }
    constexpr auto sample_count = static_cast<float>(
        kCreatureAtlasTileSize * kCreatureAtlasTileSize);
    return {
        static_cast<float>(sums[0]) / sample_count,
        static_cast<float>(sums[1]) / sample_count,
        static_cast<float>(sums[2]) / sample_count,
    };
}

} // namespace

TEST_CASE("le lacet de Marlow presente son visage vers la cible gameplay") {
    constexpr auto kExpectedPi = 3.14159265358979323846F;
    CHECK(
        backrooms_marlow_visual_body_yaw_radians(0.0F) ==
        doctest::Approx(kExpectedPi * 0.5F));
    CHECK(
        backrooms_marlow_visual_body_yaw_radians(90.0F) ==
        doctest::Approx(0.0F));
    CHECK(
        backrooms_marlow_visual_head_yaw_radians(24.0F) ==
        doctest::Approx(-24.0F * kExpectedPi / 180.0F));
    CHECK(std::isfinite(
        backrooms_marlow_visual_body_yaw_radians(
            std::numeric_limits<float>::quiet_NaN())));
}

TEST_CASE("Marlow est deterministe fini et reste sous son budget visuel") {
    BackroomsMarlowVisualPose pose {};
    pose.position = {7.25F, 39.0F, -13.5F};
    pose.yaw_radians = 0.73F;
    pose.animation_time = 4.12F;
    pose.motion_amount = 0.72F;
    pose.peek_amount = -0.44F;
    pose.head_scan_radians = 0.18F;
    pose.reach_amount = 0.36F;
    pose.sky_light = 0.12F;
    pose.block_light = 0.56F;

    const auto first = build_backrooms_marlow_visual_parts(pose);
    const auto second = build_backrooms_marlow_visual_parts(pose);
    REQUIRE_FALSE(first.empty());
    REQUIRE(first.size() == second.size());
    CHECK(first.size() <= kBackroomsMarlowVisualPartBudget);
    CHECK(first.size() <= 64U);
    for (std::size_t index = 0U; index < first.size(); ++index) {
        CHECK(same_part(first[index], second[index]));
        CHECK(matrix_is_finite(first[index].transform));
        CHECK(first[index].sky_light == doctest::Approx(0.12F));
        CHECK(first[index].precipitation_exposure == doctest::Approx(1.0F));
        CHECK(first[index].emissive_strength == doctest::Approx(0.0F));
    }
}

TEST_CASE("Marlow mesure environ trois metres quatre-vingt-cinq et touche le sol") {
    const auto parts =
        build_backrooms_marlow_visual_parts(BackroomsMarlowVisualPose {});
    REQUIRE_FALSE(parts.empty());
    const auto bounds = visual_bounds(parts);
    const auto height = bounds.maximum.y - bounds.minimum.y;
    CHECK(bounds.minimum.y == doctest::Approx(0.0F).epsilon(0.0001));
    CHECK(height >= 3.80F);
    CHECK(height <= 3.91F);
    CHECK(height == doctest::Approx(kBackroomsMarlowVisualStandingHeight)
                        .epsilon(0.018));
}

TEST_CASE("le visage de Marlow garde deux globes blancs enormes sans masque ni pupille") {
    const auto parts =
        build_backrooms_marlow_visual_parts(BackroomsMarlowVisualPose {});
    REQUIRE_FALSE(parts.empty());
    CHECK(count_tile(parts, CreatureAtlasTile::MarlowEyeWhite) == 2U);
    CHECK(count_tile(parts, CreatureAtlasTile::MarlowSwimCap) == 1U);
    CHECK(count_tile(parts, CreatureAtlasTile::ZombieEye) == 0U);
    CHECK(count_tile(parts, CreatureAtlasTile::VillagerEye) == 0U);
    CHECK(count_tile(parts, CreatureAtlasTile::JackEye) == 0U);
    CHECK(count_tile(parts, CreatureAtlasTile::CrewLeather) == 0U);
    CHECK(count_tile(parts, CreatureAtlasTile::CrewIron) == 0U);
    CHECK(count_tile(parts, CreatureAtlasTile::MarlowBuoyYellow) == 0U);

    auto eye_parts = std::size_t {0U};
    for (const auto& part : parts) {
        const auto tile = part_tile(part);
        const auto allowed =
            tile == CreatureAtlasTile::MarlowSkin ||
            tile == CreatureAtlasTile::MarlowRot ||
            tile == CreatureAtlasTile::MarlowUniform ||
            tile == CreatureAtlasTile::MarlowEyeWhite ||
            tile == CreatureAtlasTile::MarlowSwimCap;
        CHECK(allowed);
        if (tile != CreatureAtlasTile::MarlowEyeWhite) {
            continue;
        }
        const auto dimensions = part_dimensions(part);
        CHECK(dimensions.y >= 0.33F);
        CHECK(dimensions.z >= 0.27F);
        CHECK(dimensions.x >= 0.14F);
        CHECK(part.emissive_strength == doctest::Approx(0.0F));
        ++eye_parts;
    }
    CHECK(eye_parts == 2U);

    const auto atlas = build_creature_atlas_pixels();
    REQUIRE(
        static_cast<std::size_t>(CreatureAtlasTile::Count) <=
        static_cast<std::size_t>(
            kCreatureAtlasTilesPerAxis * kCreatureAtlasTilesPerAxis));
    // Je verifie chaque texel, pas seulement la moyenne : aucune tache sombre
    // centrale ne peut reintroduire une pupille dans les yeux vides.
    CHECK(tile_channel_minimum(
              atlas,
              CreatureAtlasTile::MarlowEyeWhite,
              0U) >= 190U);
    CHECK(tile_channel_minimum(
              atlas,
              CreatureAtlasTile::MarlowEyeWhite,
              1U) >= 200U);
    CHECK(tile_channel_minimum(
              atlas,
              CreatureAtlasTile::MarlowEyeWhite,
              2U) >= 204U);
    CHECK(tile_channel_minimum(
              atlas,
              CreatureAtlasTile::MarlowEyeWhite,
              3U) == 0U);
}

TEST_CASE("les matieres de Marlow restent noyées bleues et la bouee jaune") {
    const auto first = build_creature_atlas_pixels();
    const auto second = build_creature_atlas_pixels();
    REQUIRE(first == second);
    const auto skin =
        tile_average_rgb(first, CreatureAtlasTile::MarlowSkin);
    const auto rot =
        tile_average_rgb(first, CreatureAtlasTile::MarlowRot);
    const auto uniform =
        tile_average_rgb(first, CreatureAtlasTile::MarlowUniform);
    const auto cap =
        tile_average_rgb(first, CreatureAtlasTile::MarlowSwimCap);
    const auto buoy =
        tile_average_rgb(first, CreatureAtlasTile::MarlowBuoyYellow);
    CHECK(skin.b >= skin.r);
    CHECK(skin.g >= skin.r);
    CHECK(std::max({rot.r, rot.g, rot.b}) < 48.0F);
    CHECK(uniform.b > uniform.g + 12.0F);
    CHECK(uniform.g > uniform.r + 12.0F);
    CHECK(cap.b > cap.r + 12.0F);
    CHECK(buoy.r > buoy.g + 35.0F);
    CHECK(buoy.g > buoy.b + 80.0F);
}

TEST_CASE("les primitives modernes lissent le corps sans perdre les lambeaux") {
    const auto parts =
        build_backrooms_marlow_visual_parts(BackroomsMarlowVisualPose {});
    const auto modern = build_visual_entity_primitive_instances(
        std::span<const CreaturePartInstance> {parts},
        VisualEntityContext::Creature);
    REQUIRE(modern.size() == parts.size());
    auto capsules = std::size_t {0U};
    auto ellipsoids = std::size_t {0U};
    auto panels_or_ribbons = std::size_t {0U};
    for (const auto& primitive : modern) {
        CHECK(matrix_is_finite(primitive.render_transform()));
        capsules +=
            primitive.primitive == StylizedPrimitiveType::Capsule ? 1U : 0U;
        ellipsoids +=
            primitive.primitive == StylizedPrimitiveType::Ellipsoid ? 1U : 0U;
        panels_or_ribbons +=
            primitive.primitive == StylizedPrimitiveType::Panel ||
                    primitive.primitive == StylizedPrimitiveType::Ribbon
                ? 1U
                : 0U;
        if (part_tile(primitive.source) ==
            CreatureAtlasTile::MarlowEyeWhite) {
            CHECK(primitive.primitive == StylizedPrimitiveType::Ellipsoid);
        }
    }
    CHECK(capsules >= 18U);
    CHECK(ellipsoids >= 10U);
    CHECK(panels_or_ribbons >= 4U);
}

TEST_CASE("les bras pendent sous les genoux puis jaillissent au screamer") {
    BackroomsMarlowVisualPose idle_pose {};
    const auto idle = build_backrooms_marlow_visual_parts(idle_pose);
    auto scare_pose = idle_pose;
    scare_pose.jumpscare = true;
    const auto scare = build_backrooms_marlow_visual_parts(scare_pose);
    REQUIRE(idle.size() == scare.size());

    auto lowest_hand_detail = std::numeric_limits<float>::max();
    for (const auto& part : idle) {
        if (part_tile(part) != CreatureAtlasTile::MarlowRot) {
            continue;
        }
        const auto center = glm::vec3 {part.transform[3]};
        if (std::abs(center.z) > 0.35F) {
            lowest_hand_detail = std::min(lowest_hand_detail, center.y);
        }
    }
    CHECK(lowest_hand_detail < 0.60F);
    CHECK(visual_bounds(scare).maximum.x >
          visual_bounds(idle).maximum.x + 1.10F);
}

TEST_CASE("l'immersion et le regard d'angle animent Marlow sans changer son rig") {
    BackroomsMarlowVisualPose idle_pose {};
    const auto idle = build_backrooms_marlow_visual_parts(idle_pose);

    auto animated_pose = idle_pose;
    animated_pose.animation_time = 1.37F;
    animated_pose.motion_amount = 1.0F;
    const auto animated = build_backrooms_marlow_visual_parts(animated_pose);
    REQUIRE(animated.size() == idle.size());
    CHECK_FALSE(std::equal(
        idle.begin(), idle.end(), animated.begin(), same_part));

    auto submerged_pose = idle_pose;
    submerged_pose.submersion_ratio = 1.0F;
    const auto submerged =
        build_backrooms_marlow_visual_parts(submerged_pose);
    const auto idle_bounds = visual_bounds(idle);
    const auto submerged_bounds = visual_bounds(submerged);
    CHECK(submerged_bounds.maximum.y ==
          doctest::Approx(
              idle_bounds.maximum.y - kBackroomsMarlowMaximumSubmersion));
    CHECK((submerged_bounds.maximum.y - submerged_bounds.minimum.y) ==
          doctest::Approx(idle_bounds.maximum.y - idle_bounds.minimum.y));

    auto peek_pose = idle_pose;
    peek_pose.peek_amount = 1.0F;
    const auto peek = build_backrooms_marlow_visual_parts(peek_pose);
    CHECK(visual_bounds(peek).maximum.z > idle_bounds.maximum.z + 0.12F);
}

TEST_CASE("la revelation fait emerger puis replonger Marlow progressivement") {
    CHECK(backrooms_marlow_visual_submersion_ratio(0.55F, 0.0F) ==
          doctest::Approx(1.0F));
    CHECK(backrooms_marlow_visual_submersion_ratio(0.55F, 1.0F) ==
          doctest::Approx(0.57475F));
    const auto half_revealed =
        backrooms_marlow_visual_submersion_ratio(0.55F, 0.5F);
    CHECK(half_revealed > 0.57475F);
    CHECK(half_revealed < 1.0F);
    CHECK(backrooms_marlow_visual_submersion_ratio(
              std::numeric_limits<float>::quiet_NaN(),
              std::numeric_limits<float>::infinity()) ==
          doctest::Approx(1.0F));
}

TEST_CASE("la bouee jaune forme un anneau flottant lisse et deterministe") {
    BackroomsMarlowBuoyVisualPose pose {};
    pose.water_surface_position = {4.0F, 40.625F, -2.0F};
    pose.animation_time = 0.72F;
    pose.disturbance = 0.64F;
    pose.block_light = 0.42F;
    const auto first = build_backrooms_marlow_buoy_visual_parts(pose);
    const auto second = build_backrooms_marlow_buoy_visual_parts(pose);
    REQUIRE(first.size() == 16U);
    REQUIRE(first.size() == second.size());
    CHECK(first.size() <= kBackroomsMarlowBuoyVisualPartBudget);
    for (std::size_t index = 0U; index < first.size(); ++index) {
        CHECK(same_part(first[index], second[index]));
        CHECK(part_tile(first[index]) ==
              CreatureAtlasTile::MarlowBuoyYellow);
    }

    const auto modern = build_visual_entity_primitive_instances(
        std::span<const CreaturePartInstance> {first},
        VisualEntityContext::Creature);
    REQUIRE(modern.size() == first.size());
    CHECK(std::all_of(
        modern.begin(),
        modern.end(),
        [](const VisualEntityPrimitiveInstance& part) {
            return part.primitive == StylizedPrimitiveType::Capsule;
        }));
    const auto bounds = visual_bounds(first);
    CHECK(bounds.minimum.y < pose.water_surface_position.y);
    CHECK(bounds.maximum.y > pose.water_surface_position.y);
    CHECK(bounds.maximum.x - bounds.minimum.x >= 0.78F);
    CHECK(bounds.maximum.z - bounds.minimum.z >= 0.78F);
}

TEST_CASE("les entrees non finies de Marlow et de sa bouee sont neutralisees") {
    BackroomsMarlowVisualPose hostile {};
    hostile.position = {
        std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity(),
    };
    hostile.yaw_radians = std::numeric_limits<float>::quiet_NaN();
    hostile.animation_time = std::numeric_limits<float>::infinity();
    hostile.motion_amount = -std::numeric_limits<float>::infinity();
    hostile.submersion_ratio = std::numeric_limits<float>::quiet_NaN();
    hostile.peek_amount = std::numeric_limits<float>::infinity();
    hostile.head_scan_radians = std::numeric_limits<float>::infinity();
    hostile.reach_amount = std::numeric_limits<float>::quiet_NaN();
    hostile.sky_light = std::numeric_limits<float>::quiet_NaN();
    hostile.block_light = std::numeric_limits<float>::infinity();
    const auto parts = build_backrooms_marlow_visual_parts(hostile);
    REQUIRE_FALSE(parts.empty());
    for (const auto& part : parts) {
        CHECK(matrix_is_finite(part.transform));
        CHECK(std::isfinite(part.sky_light));
        CHECK(std::isfinite(part.block_light));
    }

    BackroomsMarlowBuoyVisualPose buoy {};
    buoy.water_surface_position = hostile.position;
    buoy.yaw_radians = hostile.yaw_radians;
    buoy.animation_time = hostile.animation_time;
    buoy.disturbance = hostile.peek_amount;
    const auto buoy_parts =
        build_backrooms_marlow_buoy_visual_parts(buoy);
    REQUIRE_FALSE(buoy_parts.empty());
    for (const auto& part : buoy_parts) {
        CHECK(matrix_is_finite(part.transform));
    }
}

} // namespace valcraft
