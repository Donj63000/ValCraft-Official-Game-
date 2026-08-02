#include "gameplay/BackroomsJack.h"
#include "render/BackroomsJackVisual.h"

#include <doctest/doctest.h>

#include <glm/vec4.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace valcraft {

namespace {

struct JackBounds {
    glm::vec3 minimum {std::numeric_limits<float>::max()};
    glm::vec3 maximum {std::numeric_limits<float>::lowest()};
};

[[nodiscard]] auto part_bounds(
    const CreaturePartInstance& part) noexcept -> JackBounds {
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

    JackBounds bounds {};
    for (const auto& corner : kCorners) {
        const auto world =
            part.transform * glm::vec4 {corner, 1.0F};
        bounds.minimum = glm::min(bounds.minimum, glm::vec3 {world});
        bounds.maximum = glm::max(bounds.maximum, glm::vec3 {world});
    }
    return bounds;
}

[[nodiscard]] auto visual_bounds(
    const std::vector<CreaturePartInstance>& parts) noexcept -> JackBounds {
    JackBounds bounds {};
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
            part.face_uvs.front().u0 *
            kCreatureAtlasTilesPerAxis));
    const auto tile_y = static_cast<int>(
        std::lround(
            part.face_uvs.front().v0 *
            kCreatureAtlasTilesPerAxis));
    return static_cast<CreatureAtlasTile>(
        tile_y * static_cast<int>(kCreatureAtlasTilesPerAxis) +
        tile_x);
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

[[nodiscard]] auto tile_average_rgba(
    const std::vector<std::uint8_t>& atlas,
    CreatureAtlasTile tile) -> std::array<float, 4> {
    const auto coordinates = creature_atlas_tile_coordinates(tile);
    std::array<std::uint64_t, 4> sums {};
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
        static_cast<float>(sums[3]) / sample_count,
    };
}

[[nodiscard]] auto tile_alpha_coverage(
    const std::vector<std::uint8_t>& atlas,
    CreatureAtlasTile tile) -> float {
    const auto coordinates = creature_atlas_tile_coordinates(tile);
    auto covered = std::size_t {0U};
    for (int y = 0; y < kCreatureAtlasTileSize; ++y) {
        for (int x = 0; x < kCreatureAtlasTileSize; ++x) {
            const auto atlas_x =
                coordinates[0] * kCreatureAtlasTileSize + x;
            const auto atlas_y =
                coordinates[1] * kCreatureAtlasTileSize + y;
            const auto alpha_offset = static_cast<std::size_t>(
                (atlas_y * kCreatureAtlasSize + atlas_x) * 4 + 3);
            covered += atlas[alpha_offset] > 0U ? 1U : 0U;
        }
    }
    return static_cast<float>(covered) /
           static_cast<float>(
               kCreatureAtlasTileSize * kCreatureAtlasTileSize);
}

[[nodiscard]] auto part_maximum_dimension(
    const CreaturePartInstance& part) noexcept -> float {
    const auto bounds = part_bounds(part);
    const auto dimensions = bounds.maximum - bounds.minimum;
    return std::max({dimensions.x, dimensions.y, dimensions.z});
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
            if (lhs.transform[column][row] !=
                rhs.transform[column][row]) {
                return false;
            }
        }
    }
    for (std::size_t face = 0; face < lhs.face_uvs.size(); ++face) {
        const auto& lhs_uv = lhs.face_uvs[face];
        const auto& rhs_uv = rhs.face_uvs[face];
        if (lhs_uv.u0 != rhs_uv.u0 ||
            lhs_uv.v0 != rhs_uv.v0 ||
            lhs_uv.u1 != rhs_uv.u1 ||
            lhs_uv.v1 != rhs_uv.v1) {
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
           lhs.precipitation_exposure ==
               rhs.precipitation_exposure;
}

} // namespace

TEST_CASE("le lacet gameplay présente réellement le visage de Jack") {
    constexpr auto kPi = 3.14159265358979323846F;
    CHECK(
        backrooms_jack_visual_body_yaw_radians(0.0F) ==
        doctest::Approx(kPi * 0.5F));
    CHECK(
        backrooms_jack_visual_body_yaw_radians(90.0F) ==
        doctest::Approx(0.0F));
    CHECK(
        std::abs(
            backrooms_jack_visual_body_yaw_radians(-90.0F)) ==
        doctest::Approx(kPi));
    CHECK(
        backrooms_jack_visual_head_yaw_radians(18.0F) ==
        doctest::Approx(-18.0F * kPi / 180.0F));
    CHECK(
        std::isfinite(
            backrooms_jack_visual_body_yaw_radians(
                std::numeric_limits<float>::quiet_NaN())));
}

TEST_CASE("Jack le pirate reste deterministe et respecte son budget visuel") {
    BackroomsJackVisualPose pose {};
    pose.position = {12.5F, 41.0F, -8.25F};
    pose.yaw_radians = 0.71F;
    pose.animation_time = 19.25F;
    pose.hunch_ratio = 0.28F;
    pose.head_scan_radians = -0.21F;
    pose.motion_amount = 0.74F;
    pose.chasing = true;
    pose.sky_light = 0.18F;
    pose.block_light = 0.62F;

    const auto first = build_backrooms_jack_visual_parts(pose);
    const auto second = build_backrooms_jack_visual_parts(pose);

    REQUIRE_FALSE(first.empty());
    REQUIRE(first.size() == second.size());
    CHECK(first.size() < kBackroomsJackVisualPartBudget);
    // Je garde une marge reelle dans le budget : elle garantit que la
    // silhouette n'est plus composee de dizaines de micro-doigts inutiles.
    CHECK(first.size() <= 112U);
    for (std::size_t index = 0; index < first.size(); ++index) {
        CHECK(same_part(first[index], second[index]));
        CHECK(matrix_is_finite(first[index].transform));
        CHECK(first[index].sky_light == doctest::Approx(0.18F));
        CHECK(first[index].block_light == doctest::Approx(0.62F));
        CHECK(first[index].precipitation_exposure ==
              doctest::Approx(0.0F));
    }
}

TEST_CASE("Jack debout approche quatre metres cinquante sans quitter le sol") {
    const auto parts =
        build_backrooms_jack_visual_parts(BackroomsJackVisualPose {});
    REQUIRE_FALSE(parts.empty());

    const auto bounds = visual_bounds(parts);
    const auto height = bounds.maximum.y - bounds.minimum.y;
    CHECK(bounds.minimum.y == doctest::Approx(0.0F).epsilon(0.0001));
    CHECK(height >= 4.42F);
    CHECK(height <= 4.58F);
    CHECK(height == doctest::Approx(kBackroomsJackVisualStandingHeight)
                        .epsilon(0.020));
}

TEST_CASE("Jack possede un tricorne une seule botte et une jambe de bois") {
    const auto parts =
        build_backrooms_jack_visual_parts(BackroomsJackVisualPose {});
    REQUIRE_FALSE(parts.empty());

    std::size_t low_leather_parts = 0U;
    std::size_t high_hat_panels = 0U;
    std::size_t positive_side_wood_parts = 0U;
    std::size_t emissive_eye_parts = 0U;
    for (const auto& part : parts) {
        const auto bounds = part_bounds(part);
        const auto center = glm::vec3 {part.transform[3]};
        if (part_tile(part) == CreatureAtlasTile::JackCloth &&
            bounds.maximum.y < 0.40F) {
            ++low_leather_parts;
        }
        if (part_tile(part) == CreatureAtlasTile::JackCloth &&
            bounds.minimum.y > 4.0F) {
            ++high_hat_panels;
        }
        if (part_tile(part) == CreatureAtlasTile::CrewWood &&
            center.z > 0.05F &&
            bounds.maximum.y < 1.20F) {
            ++positive_side_wood_parts;
        }
        if (part.emissive_strength > 0.0F) {
            CHECK(part_tile(part) == CreatureAtlasTile::JackEye);
            CHECK(part.emissive_strength >= 0.76F);
            ++emissive_eye_parts;
        }
    }

    CHECK(low_leather_parts == 1U);
    CHECK(high_hat_panels >= 3U);
    CHECK(positive_side_wood_parts >= 2U);
    CHECK(count_tile(parts, CreatureAtlasTile::CrewWood) >= 2U);
    CHECK(count_tile(parts, CreatureAtlasTile::CrewIron) >= 1U);
    CHECK(count_tile(parts, CreatureAtlasTile::JackEye) == 2U);
    CHECK(count_tile(parts, CreatureAtlasTile::ZombieTeeth) == 14U);
    CHECK(count_tile(parts, CreatureAtlasTile::ZombieClaw) == 12U);
    CHECK(count_tile(parts, CreatureAtlasTile::JackTar) >= 12U);
    CHECK(count_tile(parts, CreatureAtlasTile::JackBone) >= 12U);
    CHECK(emissive_eye_parts == 2U);
}

TEST_CASE("les matieres procedurales de Jack restent noires distinctes et deterministes") {
    const auto first = build_creature_atlas_pixels();
    const auto second = build_creature_atlas_pixels();
    REQUIRE(first == second);
    REQUIRE(
        static_cast<std::size_t>(CreatureAtlasTile::Count) <=
        static_cast<std::size_t>(
            kCreatureAtlasTilesPerAxis * kCreatureAtlasTilesPerAxis));

    const auto tar =
        tile_average_rgba(first, CreatureAtlasTile::JackTar);
    const auto bone =
        tile_average_rgba(first, CreatureAtlasTile::JackBone);
    const auto cloth =
        tile_average_rgba(first, CreatureAtlasTile::JackCloth);
    const auto eye =
        tile_average_rgba(first, CreatureAtlasTile::JackEye);

    CHECK(std::max({tar[0], tar[1], tar[2]}) < 35.0F);
    CHECK(std::max({cloth[0], cloth[1], cloth[2]}) < 35.0F);
    CHECK(bone[0] < 82.0F);
    CHECK(bone[0] > tar[0] + 24.0F);
    CHECK(bone[1] > cloth[1] + 20.0F);
    CHECK(eye[0] > eye[1] + 48.0F);
    CHECK(eye[1] > eye[2] + 36.0F);
    CHECK(eye[3] > 48.0F);
    CHECK(tile_alpha_coverage(first, CreatureAtlasTile::JackEye) > 0.32F);
    CHECK(tile_alpha_coverage(first, CreatureAtlasTile::JackEye) < 0.86F);
    CHECK(tile_alpha_coverage(first, CreatureAtlasTile::JackTar) == 0.0F);
    CHECK(tile_alpha_coverage(first, CreatureAtlasTile::JackBone) == 0.0F);
    CHECK(tile_alpha_coverage(first, CreatureAtlasTile::JackCloth) == 0.0F);
}

TEST_CASE("les bras de Jack forment deux tentacules asymetriques aux griffes lisibles") {
    BackroomsJackVisualPose pose {};
    pose.animation_time = 0.37F;
    const auto parts = build_backrooms_jack_visual_parts(pose);
    REQUIRE_FALSE(parts.empty());

    auto dark_arm_parts = std::size_t {0U};
    auto left_claws = std::size_t {0U};
    auto right_claws = std::size_t {0U};
    auto left_lowest_claw = std::numeric_limits<float>::max();
    auto right_lowest_claw = std::numeric_limits<float>::max();
    for (const auto& part : parts) {
        const auto center = glm::vec3 {part.transform[3]};
        const auto tile = part_tile(part);
        if (tile == CreatureAtlasTile::JackTar &&
            std::abs(center.z) > 0.30F) {
            ++dark_arm_parts;
        }
        if (tile != CreatureAtlasTile::ZombieClaw) {
            continue;
        }
        // Je refuse les anciennes micro-fourches : chaque phalange terminale
        // doit conserver une taille projetee utile a moyenne distance.
        CHECK(part_maximum_dimension(part) >= 0.16F);
        if (center.z < 0.0F) {
            ++left_claws;
            left_lowest_claw = std::min(left_lowest_claw, center.y);
        } else {
            ++right_claws;
            right_lowest_claw = std::min(right_lowest_claw, center.y);
        }
    }

    CHECK(dark_arm_parts >= 12U);
    CHECK(left_claws == 6U);
    CHECK(right_claws == 6U);
    CHECK(std::abs(left_lowest_claw - right_lowest_claw) > 0.040F);
}

TEST_CASE("les tentacules continuent de ramper lorsque Jack observe sans marcher") {
    BackroomsJackVisualPose first_pose {};
    first_pose.motion_amount = 0.0F;
    first_pose.animation_time = 0.0F;
    auto second_pose = first_pose;
    second_pose.animation_time = 1.17F;

    const auto first = build_backrooms_jack_visual_parts(first_pose);
    const auto second = build_backrooms_jack_visual_parts(second_pose);
    REQUIRE(first.size() == second.size());

    auto moved_tentacle_parts = std::size_t {0U};
    for (std::size_t index = 0U; index < first.size(); ++index) {
        const auto tile = part_tile(first[index]);
        const auto center = glm::vec3 {first[index].transform[3]};
        if ((tile == CreatureAtlasTile::JackTar ||
             tile == CreatureAtlasTile::ZombieClaw) &&
            std::abs(center.z) > 0.30F &&
            !same_part(first[index], second[index])) {
            ++moved_tentacle_parts;
        }
    }
    CHECK(moved_tentacle_parts >= 20U);
}

TEST_CASE("la posture basse penche Jack sans deplacer ses pieds") {
    BackroomsJackVisualPose standing_pose {};
    standing_pose.animation_time = 0.0F;
    const auto standing =
        build_backrooms_jack_visual_parts(standing_pose);

    auto hunched_pose = standing_pose;
    hunched_pose.hunch_ratio = 1.0F;
    const auto hunched =
        build_backrooms_jack_visual_parts(hunched_pose);

    const auto standing_bounds = visual_bounds(standing);
    const auto hunched_bounds = visual_bounds(hunched);
    CHECK(hunched_bounds.minimum.y ==
          doctest::Approx(standing_bounds.minimum.y).epsilon(0.0001));
    CHECK(hunched_bounds.maximum.y <
          standing_bounds.maximum.y - 0.30F);
    CHECK(hunched_bounds.maximum.y <= kBackroomsJackBentHeight);
    CHECK(hunched_bounds.maximum.x >
          standing_bounds.maximum.x + 0.25F);

    auto maximum_animated_height = hunched_bounds.maximum.y;
    auto animated_pose = hunched_pose;
    animated_pose.motion_amount = 1.0F;
    animated_pose.chasing = true;
    for (auto sample = 0; sample <= 240; ++sample) {
        animated_pose.animation_time =
            static_cast<float>(sample) / 12.0F;
        maximum_animated_height = std::max(
            maximum_animated_height,
            visual_bounds(
                build_backrooms_jack_visual_parts(animated_pose))
                .maximum.y);
    }
    CHECK(maximum_animated_height <= kBackroomsJackBentHeight);

    auto entry_pose = hunched_pose;
    entry_pose.hunch_ratio =
        kBackroomsJackLowCeilingEntryHunchRatio;
    entry_pose.motion_amount = 1.0F;
    entry_pose.chasing = true;
    auto maximum_entry_height = 0.0F;
    for (auto sample = 0; sample <= 240; ++sample) {
        entry_pose.animation_time =
            static_cast<float>(sample) / 12.0F;
        maximum_entry_height = std::max(
            maximum_entry_height,
            visual_bounds(
                build_backrooms_jack_visual_parts(entry_pose))
                .maximum.y);
    }
    // Je valide toute l'animation au seuil exact ou l'IA recommence a avancer.
    CHECK(maximum_entry_height <= 4.0F);
}

TEST_CASE("la marche la poursuite le regard et le screamer produisent des poses distinctes") {
    BackroomsJackVisualPose initial_pose {};
    initial_pose.motion_amount = 1.0F;
    initial_pose.animation_time = 0.0F;
    const auto initial =
        build_backrooms_jack_visual_parts(initial_pose);

    auto walking_pose = initial_pose;
    walking_pose.animation_time = 0.43F;
    const auto walking =
        build_backrooms_jack_visual_parts(walking_pose);
    REQUIRE(initial.size() == walking.size());

    std::size_t moved_parts = 0U;
    for (std::size_t index = 0; index < initial.size(); ++index) {
        moved_parts += same_part(initial[index], walking[index]) ? 0U : 1U;
    }
    CHECK(moved_parts >= 20U);

    auto chase_pose = walking_pose;
    chase_pose.chasing = true;
    const auto chasing =
        build_backrooms_jack_visual_parts(chase_pose);
    REQUIRE(chasing.size() == walking.size());
    CHECK_FALSE(std::equal(
        walking.begin(),
        walking.end(),
        chasing.begin(),
        same_part));

    auto left_gaze_pose = initial_pose;
    left_gaze_pose.motion_amount = 0.0F;
    left_gaze_pose.head_scan_radians = -0.42F;
    auto right_gaze_pose = left_gaze_pose;
    right_gaze_pose.head_scan_radians = 0.42F;
    const auto left_gaze =
        build_backrooms_jack_visual_parts(left_gaze_pose);
    const auto right_gaze =
        build_backrooms_jack_visual_parts(right_gaze_pose);
    CHECK_FALSE(std::equal(
        left_gaze.begin(),
        left_gaze.end(),
        right_gaze.begin(),
        same_part));

    auto scare_pose = initial_pose;
    scare_pose.motion_amount = 0.0F;
    scare_pose.jumpscare = true;
    const auto scare =
        build_backrooms_jack_visual_parts(scare_pose);
    CHECK(visual_bounds(scare).maximum.x >
          visual_bounds(initial).maximum.x + 0.55F);
}

TEST_CASE("les entrees visuelles non finies sont neutralisees") {
    BackroomsJackVisualPose pose {};
    pose.position = {
        std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity(),
    };
    pose.yaw_radians = std::numeric_limits<float>::quiet_NaN();
    pose.animation_time = std::numeric_limits<float>::infinity();
    pose.hunch_ratio = std::numeric_limits<float>::quiet_NaN();
    pose.head_scan_radians = std::numeric_limits<float>::infinity();
    pose.motion_amount = -std::numeric_limits<float>::infinity();
    pose.sky_light = std::numeric_limits<float>::quiet_NaN();
    pose.block_light = std::numeric_limits<float>::infinity();

    const auto parts = build_backrooms_jack_visual_parts(pose);
    REQUIRE_FALSE(parts.empty());
    for (const auto& part : parts) {
        CHECK(matrix_is_finite(part.transform));
        CHECK(std::isfinite(part.sky_light));
        CHECK(std::isfinite(part.block_light));
    }
}

} // namespace valcraft
