#include "render/VisualEntityPrimitives.h"

#include "creatures/HumanoidVisualContinuity.h"
#include "player/PlayerGeometry.h"

#include <doctest/doctest.h>

#include <glm/common.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <utility>

namespace valcraft {

namespace {

template <typename Tile>
[[nodiscard]] auto uniform_tile(Tile tile) -> std::array<BoxUvRect, 6> {
    const auto index = static_cast<int>(tile);
    constexpr auto step = 1.0F / 8.0F;
    const auto x = index % 8;
    const auto y = index / 8;
    const auto rect = BoxUvRect {
        static_cast<float>(x) * step,
        static_cast<float>(y) * step,
        static_cast<float>(x + 1) * step,
        static_cast<float>(y + 1) * step,
    };
    std::array<BoxUvRect, 6> result {};
    result.fill(rect);
    return result;
}

template <typename Tile>
[[nodiscard]] auto make_part(
    const glm::vec3& dimensions,
    Tile tile) -> CreaturePartInstance {
    CreaturePartInstance part {};
    part.transform = glm::translate(
        glm::mat4 {1.0F},
        glm::vec3 {7.0F, 11.0F, -3.0F});
    part.transform =
        glm::scale(part.transform, dimensions);
    part.face_uvs = uniform_tile(tile);
    return part;
}

[[nodiscard]] auto finite_matrix(const glm::mat4& matrix) -> bool {
    for (glm::mat4::length_type column = 0; column < 4; ++column) {
        for (glm::mat4::length_type row = 0; row < 4; ++row) {
            if (!std::isfinite(matrix[column][row])) {
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] auto exact_part_equal(
    const CreaturePartInstance& lhs,
    const CreaturePartInstance& rhs) -> bool {
    return std::memcmp(&lhs.transform, &rhs.transform, sizeof(glm::mat4)) == 0 &&
           std::memcmp(
               lhs.face_uvs.data(),
               rhs.face_uvs.data(),
               sizeof(BoxUvRect) * lhs.face_uvs.size()) == 0 &&
           lhs.nightmare_factor == rhs.nightmare_factor &&
           lhs.tension == rhs.tension &&
           lhs.material_class == rhs.material_class &&
           lhs.cavity_mask == rhs.cavity_mask &&
           lhs.emissive_strength == rhs.emissive_strength &&
           lhs.sky_light == rhs.sky_light &&
           lhs.block_light == rhs.block_light &&
           lhs.precipitation_exposure == rhs.precipitation_exposure;
}

[[nodiscard]] auto transformed_mesh_extent(
    const StylizedPrimitiveMesh& mesh,
    const glm::mat4& transform) -> glm::vec3 {
    auto minimum = glm::vec3 {std::numeric_limits<float>::max()};
    auto maximum = glm::vec3 {std::numeric_limits<float>::lowest()};
    for (const auto& vertex : mesh.vertices) {
        const auto transformed = transform * glm::vec4 {
            vertex.x,
            vertex.y,
            vertex.z,
            1.0F,
        };
        minimum = glm::min(minimum, glm::vec3 {transformed});
        maximum = glm::max(maximum, glm::vec3 {transformed});
    }
    return maximum - minimum;
}

} // namespace

TEST_CASE("les os visuels humains se recouvrent sans déplacer leur centre") {
    const glm::vec3 start {1.0F, 2.0F, 3.0F};
    const glm::vec3 end {1.0F, 3.0F, 3.0F};
    const auto first = make_overlapping_humanoid_limb_span(
        start,
        end,
        0.07F);
    const auto second = make_overlapping_humanoid_limb_span(
        start,
        end,
        0.07F);

    REQUIRE(first.valid);
    CHECK(first.start.x == doctest::Approx(1.0F));
    CHECK(first.start.y == doctest::Approx(1.93F));
    CHECK(first.end.y == doctest::Approx(3.07F));
    CHECK(first.end.z == doctest::Approx(3.0F));
    CHECK((first.start + first.end) * 0.5F ==
          (start + end) * 0.5F);
    CHECK(first.start == second.start);
    CHECK(first.end == second.end);

    const auto clamped = make_overlapping_humanoid_limb_span(
        start,
        end,
        10.0F);
    REQUIRE(clamped.valid);
    CHECK(clamped.start.y == doctest::Approx(1.82F));
    CHECK(clamped.end.y == doctest::Approx(3.18F));
}

TEST_CASE("les os visuels humains refusent les entrées non finies ou dégénérées") {
    const auto degenerate = make_overlapping_humanoid_limb_span(
        glm::vec3 {1.0F},
        glm::vec3 {1.0F},
        0.04F);
    const auto nan_overlap = make_overlapping_humanoid_limb_span(
        glm::vec3 {0.0F},
        glm::vec3 {0.0F, 1.0F, 0.0F},
        std::numeric_limits<float>::quiet_NaN());
    const auto infinite_endpoint = make_overlapping_humanoid_limb_span(
        glm::vec3 {0.0F},
        glm::vec3 {
            std::numeric_limits<float>::infinity(),
            1.0F,
            0.0F,
        },
        0.04F);

    CHECK_FALSE(degenerate.valid);
    CHECK_FALSE(nan_overlap.valid);
    CHECK_FALSE(infinite_endpoint.valid);
}

TEST_CASE("le cache des primitives d'entite contient les trois LOD immuables") {
    constexpr std::array<StylizedPrimitiveType, 6> primitives {{
        StylizedPrimitiveType::RoundedBox,
        StylizedPrimitiveType::Capsule,
        StylizedPrimitiveType::Ellipsoid,
        StylizedPrimitiveType::TaperedCylinder,
        StylizedPrimitiveType::Panel,
        StylizedPrimitiveType::Ribbon,
    }};
    constexpr std::array<StylizedPrimitiveLod, 3> lods {{
        StylizedPrimitiveLod::Low,
        StylizedPrimitiveLod::Medium,
        StylizedPrimitiveLod::High,
    }};

    const auto& cache = visual_entity_primitive_cache();
    const VisualEntityPrimitiveCache independently_built_cache {};
    CHECK(cache.fingerprint() != 0U);
    CHECK(cache.fingerprint() == independently_built_cache.fingerprint());
    CHECK(&cache == &visual_entity_primitive_cache());
    for (const auto primitive : primitives) {
        std::size_t previous_triangles = 0U;
        for (const auto lod : lods) {
            const auto& mesh = cache.mesh(primitive, lod);
            const auto& independent_mesh =
                independently_built_cache.mesh(primitive, lod);
            REQUIRE_FALSE(mesh.empty());
            REQUIRE(mesh.vertices.size() == independent_mesh.vertices.size());
            CHECK(mesh.indices == independent_mesh.indices);
            const auto vertices_are_identical =
                mesh.vertices.empty() ||
                std::memcmp(
                    mesh.vertices.data(),
                    independent_mesh.vertices.data(),
                    mesh.vertices.size() *
                        sizeof(StylizedPrimitiveVertex)) == 0;
            CHECK(vertices_are_identical);
            CHECK(mesh.triangle_count() >= previous_triangles);
            previous_triangles = mesh.triangle_count();
            for (const auto index : mesh.indices) {
                CHECK(index < mesh.vertices.size());
            }
        }
    }
}

TEST_CASE("le LOD des entités borne les micro-détails et ses deux distances") {
    CHECK(
        select_visual_entity_primitive_lod(
            18.0F * 18.0F,
            0.40F,
            3,
            false,
            false) ==
        StylizedPrimitiveLod::High);
    CHECK(
        select_visual_entity_primitive_lod(
            18.01F * 18.01F,
            0.40F,
            3,
            false,
            false) ==
        StylizedPrimitiveLod::Medium);
    CHECK(
        select_visual_entity_primitive_lod(
            56.0F * 56.0F,
            0.40F,
            3,
            false,
            false) ==
        StylizedPrimitiveLod::Medium);
    CHECK(
        select_visual_entity_primitive_lod(
            56.01F * 56.01F,
            0.40F,
            3,
            false,
            false) ==
        StylizedPrimitiveLod::Low);
    CHECK(
        select_visual_entity_primitive_lod(
            1.0F,
            0.10F,
            3,
            false,
            false) ==
        StylizedPrimitiveLod::Medium);
    CHECK(
        select_visual_entity_primitive_lod(
            1.0F,
            0.05F,
            3,
            false,
            false) ==
        StylizedPrimitiveLod::Low);
    CHECK(
        select_visual_entity_primitive_lod(
            1.0F,
            0.40F,
            3,
            true,
            false) ==
        StylizedPrimitiveLod::Low);
    CHECK(
        select_visual_entity_primitive_lod(
            std::numeric_limits<float>::quiet_NaN(),
            0.40F,
            3,
            false,
            false) ==
        StylizedPrimitiveLod::Low);
    CHECK_FALSE(
        visual_entity_part_casts_simplified_shadow(
            0.084F));
    CHECK(
        visual_entity_part_casts_simplified_shadow(
            0.085F));
}

TEST_CASE("la classification remplace les masses organiques et les membres sans toucher au rig") {
    const auto head = make_part(
        glm::vec3 {0.42F, 0.46F, 0.40F},
        CreatureAtlasTile::PigHide);
    const auto limb = make_part(
        glm::vec3 {0.16F, 0.82F, 0.15F},
        CreatureAtlasTile::PigHide);

    const auto head_result =
        classify_visual_entity_part(head, VisualEntityContext::Creature);
    const auto limb_result =
        classify_visual_entity_part(limb, VisualEntityContext::Creature);
    CHECK(head_result.primitive == StylizedPrimitiveType::Ellipsoid);
    CHECK(head_result.role == VisualEntitySemanticRole::HeadOrJoint);
    CHECK(limb_result.primitive == StylizedPrimitiveType::Capsule);
    CHECK(limb_result.role == VisualEntitySemanticRole::Limb);
    CHECK(limb_result.major_axis == VisualEntityLocalAxis::Y);
    CHECK(finite_matrix(limb_result.primitive_to_part_local));
}

TEST_CASE("les capsules modernes renforcent les membres dans une marge bornee") {
    constexpr auto tolerance = 2.0e-5F;
    constexpr auto expected_width_scale = 1.10F;
    const std::array<std::pair<glm::vec3, VisualEntityLocalAxis>, 3> cases {{
        {glm::vec3 {0.82F, 0.16F, 0.15F}, VisualEntityLocalAxis::X},
        {glm::vec3 {0.16F, 0.82F, 0.15F}, VisualEntityLocalAxis::Y},
        {glm::vec3 {0.16F, 0.15F, 0.82F}, VisualEntityLocalAxis::Z},
    }};
    const auto& capsule = visual_entity_primitive_cache().mesh(
        StylizedPrimitiveType::Capsule,
        StylizedPrimitiveLod::High);

    for (const auto& [dimensions, expected_axis] : cases) {
        const auto limb = make_part(dimensions, CreatureAtlasTile::SheepFace);
        const auto classification =
            classify_visual_entity_part(limb, VisualEntityContext::Creature);
        REQUIRE(classification.primitive == StylizedPrimitiveType::Capsule);
        CHECK(classification.major_axis == expected_axis);

        const auto rendered_extent = transformed_mesh_extent(
            capsule,
            limb.transform * classification.primitive_to_part_local);
        auto expected_extent = dimensions * expected_width_scale;
        switch (expected_axis) {
        case VisualEntityLocalAxis::X:
            expected_extent.x = dimensions.x;
            break;
        case VisualEntityLocalAxis::Y:
            expected_extent.y = dimensions.y;
            break;
        case VisualEntityLocalAxis::Z:
            expected_extent.z = dimensions.z;
            break;
        }
        CHECK(rendered_extent.x == doctest::Approx(expected_extent.x).epsilon(tolerance));
        CHECK(rendered_extent.y == doctest::Approx(expected_extent.y).epsilon(tolerance));
        CHECK(rendered_extent.z == doctest::Approx(expected_extent.z).epsilon(tolerance));
        CHECK(rendered_extent.x <= dimensions.x * expected_width_scale + tolerance);
        CHECK(rendered_extent.y <= dimensions.y * expected_width_scale + tolerance);
        CHECK(rendered_extent.z <= dimensions.z * expected_width_scale + tolerance);
    }
}

TEST_CASE("le vocabulaire de formes distingue corps tete appendice et chaussure") {
    const auto body = make_part(
        glm::vec3 {0.68F, 0.36F, 0.40F},
        CreatureAtlasTile::PigHide);
    const auto head = make_part(
        glm::vec3 {0.42F, 0.40F, 0.39F},
        CreatureAtlasTile::PigHide);
    const auto ear = make_part(
        glm::vec3 {0.08F, 0.18F, 0.04F},
        CreatureAtlasTile::PigEar);
    const auto shoe = make_part(
        glm::vec3 {0.24F, 0.12F, 0.18F},
        PlayerAtlasTile::Shoes);
    const auto small_accent = make_part(
        glm::vec3 {0.04F, 0.05F, 0.03F},
        CreatureAtlasTile::PigHoof);

    const auto body_result =
        classify_visual_entity_part(body, VisualEntityContext::Creature);
    const auto head_result =
        classify_visual_entity_part(head, VisualEntityContext::Creature);
    const auto ear_result =
        classify_visual_entity_part(ear, VisualEntityContext::Creature);
    const auto shoe_result =
        classify_visual_entity_part(shoe, VisualEntityContext::PlayerWorld);
    const auto accent_result =
        classify_visual_entity_part(small_accent, VisualEntityContext::Creature);

    CHECK(body_result.primitive == StylizedPrimitiveType::Capsule);
    CHECK(body_result.role == VisualEntitySemanticRole::OrganicMass);
    CHECK(body_result.major_axis == VisualEntityLocalAxis::X);
    CHECK(head_result.primitive == StylizedPrimitiveType::Ellipsoid);
    CHECK(head_result.role == VisualEntitySemanticRole::HeadOrJoint);
    CHECK(ear_result.primitive == StylizedPrimitiveType::Panel);
    CHECK(ear_result.role == VisualEntitySemanticRole::FlexibleDetail);
    CHECK(shoe_result.primitive == StylizedPrimitiveType::Ellipsoid);
    CHECK(shoe_result.role == VisualEntitySemanticRole::HardProp);
    CHECK(accent_result.primitive == StylizedPrimitiveType::Ellipsoid);
    CHECK(accent_result.role == VisualEntitySemanticRole::HeadOrJoint);
}

TEST_CASE("le renfort de silhouette reste contextuel et ne translate aucune piece") {
    const auto creature_limb = make_part(
        glm::vec3 {0.16F, 0.82F, 0.15F},
        CreatureAtlasTile::PigHide);
    const auto player_limb = make_part(
        glm::vec3 {0.16F, 0.82F, 0.15F},
        PlayerAtlasTile::Sleeve);
    const auto crew_limb = make_part(
        glm::vec3 {0.16F, 0.82F, 0.15F},
        CreatureAtlasTile::CrewNavyCloth);
    auto generic_limb = creature_limb;
    generic_limb.material_class = 0.20F;

    const auto creature_result = classify_visual_entity_part(
        creature_limb,
        VisualEntityContext::Creature);
    const auto player_result = classify_visual_entity_part(
        player_limb,
        VisualEntityContext::PlayerWorld);
    const auto crew_result = classify_visual_entity_part(
        crew_limb,
        VisualEntityContext::Crew);
    const auto generic_result = classify_visual_entity_part(
        generic_limb,
        VisualEntityContext::Generic);
    const auto& capsule = visual_entity_primitive_cache().mesh(
        StylizedPrimitiveType::Capsule,
        StylizedPrimitiveLod::High);

    const auto creature_extent = transformed_mesh_extent(
        capsule,
        creature_limb.transform * creature_result.primitive_to_part_local);
    const auto player_extent = transformed_mesh_extent(
        capsule,
        player_limb.transform * player_result.primitive_to_part_local);
    const auto crew_extent = transformed_mesh_extent(
        capsule,
        crew_limb.transform * crew_result.primitive_to_part_local);
    const auto generic_extent = transformed_mesh_extent(
        capsule,
        generic_limb.transform * generic_result.primitive_to_part_local);

    CHECK(creature_extent.x == doctest::Approx(0.16F * 1.10F));
    CHECK(player_extent.x == doctest::Approx(0.16F * 1.07F));
    CHECK(crew_extent.x == doctest::Approx(0.16F * 1.07F));
    CHECK(generic_extent.x == doctest::Approx(0.16F));
    CHECK(creature_extent.y == doctest::Approx(0.82F));
    CHECK(player_extent.y == doctest::Approx(0.82F));
    CHECK(crew_extent.y == doctest::Approx(0.82F));
    CHECK(generic_extent.y == doctest::Approx(0.82F));
    CHECK(glm::all(glm::equal(
        glm::vec3 {creature_result.primitive_to_part_local[3]},
        glm::vec3 {0.0F})));
    CHECK(glm::all(glm::equal(
        glm::vec3 {player_result.primitive_to_part_local[3]},
        glm::vec3 {0.0F})));
    CHECK(glm::all(glm::equal(
        glm::vec3 {generic_result.primitive_to_part_local[3]},
        glm::vec3 {0.0F})));
}

TEST_CASE("les recettes animales reelles restent deterministes lisses et dans leur budget") {
    constexpr std::array<CreatureSpecies, 3> animal_species {{
        CreatureSpecies::Pig,
        CreatureSpecies::Cow,
        CreatureSpecies::Sheep,
    }};
    constexpr auto maximum_part_budget = 96U;
    constexpr auto maximum_high_lod_triangles_per_part = 800U;

    for (const auto species : animal_species) {
        CreatureRenderInstance creature {};
        creature.species = species;
        creature.position = {2.0F, 3.0F, -4.0F};
        creature.animation_time = 0.73F;
        creature.daylight_factor = 1.0F;
        creature.appearance_seed =
            0xA53C91U + static_cast<std::uint32_t>(species);
        creature.behavior_state = CreatureBehaviorState::Wander;
        creature.phase = CreaturePhase::Day;
        creature.motion_amount = 0.64F;
        creature.gaze_weight = 0.38F;

        const auto parts = build_creature_parts(creature);
        const auto first = build_visual_entity_primitive_instances(
            std::span<const CreaturePartInstance> {parts},
            VisualEntityContext::Creature);
        const auto second = build_visual_entity_primitive_instances(
            std::span<const CreaturePartInstance> {parts},
            VisualEntityContext::Creature);
        REQUIRE_FALSE(parts.empty());
        REQUIRE(parts.size() <= maximum_part_budget);
        REQUIRE(first.size() == parts.size());
        REQUIRE(second.size() == parts.size());

        std::size_t capsule_count = 0U;
        std::size_t ellipsoid_count = 0U;
        std::size_t rounded_box_count = 0U;
        std::size_t high_lod_triangle_count = 0U;
        for (std::size_t index = 0U; index < first.size(); ++index) {
            CHECK(first[index].primitive == second[index].primitive);
            CHECK(first[index].role == second[index].role);
            CHECK(first[index].source_index == index);
            CHECK(exact_part_equal(first[index].source, parts[index]));
            CHECK(finite_matrix(first[index].render_transform()));
            CHECK(std::memcmp(
                      &first[index].primitive_to_part_local,
                      &second[index].primitive_to_part_local,
                      sizeof(glm::mat4)) == 0);
            capsule_count +=
                first[index].primitive == StylizedPrimitiveType::Capsule
                    ? 1U
                    : 0U;
            ellipsoid_count +=
                first[index].primitive == StylizedPrimitiveType::Ellipsoid
                    ? 1U
                    : 0U;
            rounded_box_count +=
                first[index].primitive == StylizedPrimitiveType::RoundedBox
                    ? 1U
                    : 0U;
            high_lod_triangle_count += visual_entity_primitive_cache()
                                           .mesh(
                                               first[index].primitive,
                                               StylizedPrimitiveLod::High)
                                           .triangle_count();
        }

        CHECK(capsule_count >= 8U);
        CHECK(ellipsoid_count >= 2U);
        CHECK(rounded_box_count * 5U <= parts.size());
        CHECK(high_lod_triangle_count <=
              parts.size() * maximum_high_lod_triangles_per_part);
    }
}

TEST_CASE("cornes cordages et lames recoivent une silhouette semantique") {
    const auto horn = make_part(
        glm::vec3 {0.08F, 0.52F, 0.09F},
        CreatureAtlasTile::CowHorn);
    const auto rope = make_part(
        glm::vec3 {0.90F, 0.05F, 0.05F},
        CreatureAtlasTile::CrewRope);
    const auto blade = make_part(
        glm::vec3 {0.05F, 0.86F, 0.02F},
        PlayerAtlasTile::SwordBlade);

    const auto horn_result =
        classify_visual_entity_part(horn, VisualEntityContext::Creature);
    const auto rope_result =
        classify_visual_entity_part(rope, VisualEntityContext::Crew);
    const auto blade_result =
        classify_visual_entity_part(blade, VisualEntityContext::PlayerViewModel);
    CHECK(horn_result.primitive == StylizedPrimitiveType::TaperedCylinder);
    CHECK(horn_result.role == VisualEntitySemanticRole::HornOrClaw);
    CHECK(rope_result.primitive == StylizedPrimitiveType::TaperedCylinder);
    CHECK(rope_result.role == VisualEntitySemanticRole::ToolShaft);
    CHECK(rope_result.major_axis == VisualEntityLocalAxis::X);
    CHECK(blade_result.primitive == StylizedPrimitiveType::Panel);
    CHECK(blade_result.role == VisualEntitySemanticRole::Blade);

    const auto aligned_axis =
        glm::vec3 {rope_result.primitive_to_part_local[1]};
    CHECK(glm::all(glm::lessThan(
        glm::abs(aligned_axis - glm::vec3 {1.0F, 0.0F, 0.0F}),
        glm::vec3 {1.0e-6F})));
}

TEST_CASE("la conversion preserve exactement chaque instance et son ordre") {
    auto first = make_part(
        glm::vec3 {0.17F, 0.71F, 0.15F},
        PlayerAtlasTile::Sleeve);
    first.nightmare_factor = 0.21F;
    first.tension = 0.32F;
    first.material_class = 0.24F;
    first.cavity_mask = 0.13F;
    first.emissive_strength = 0.17F;
    first.sky_light = 0.72F;
    first.block_light = 0.44F;
    first.precipitation_exposure = 0.91F;
    const auto second = make_part(
        glm::vec3 {0.38F, 0.42F, 0.36F},
        PlayerAtlasTile::Face);
    const std::array<CreaturePartInstance, 2> source {{first, second}};

    const auto converted = build_visual_entity_primitive_instances(
        std::span<const CreaturePartInstance> {source},
        VisualEntityContext::PlayerWorld);
    REQUIRE(converted.size() == source.size());
    for (std::size_t index = 0U; index < source.size(); ++index) {
        CHECK(converted[index].source_index == index);
        CHECK(exact_part_equal(converted[index].source, source[index]));
        CHECK(finite_matrix(converted[index].render_transform()));
    }
}

TEST_CASE("les UV multi-faces et les entrees hostiles ont un repli deterministe") {
    auto mixed = make_part(
        glm::vec3 {0.06F, 0.90F, 0.025F},
        PlayerAtlasTile::SwordBlade);
    mixed.face_uvs[2] = uniform_tile(PlayerAtlasTile::SwordEdge)[0];
    const auto mask = visual_entity_atlas_tile_mask(mixed);
    CHECK((mask & (std::uint64_t {1U} << static_cast<std::uint8_t>(
                       PlayerAtlasTile::SwordBlade))) != 0U);
    CHECK((mask & (std::uint64_t {1U} << static_cast<std::uint8_t>(
                       PlayerAtlasTile::SwordEdge))) != 0U);

    auto hostile = mixed;
    hostile.transform[0][0] = std::numeric_limits<float>::quiet_NaN();
    hostile.face_uvs[0].u0 = std::numeric_limits<float>::infinity();
    const auto first =
        classify_visual_entity_part(hostile, VisualEntityContext::PlayerViewModel);
    const auto second =
        classify_visual_entity_part(hostile, VisualEntityContext::PlayerViewModel);
    CHECK_FALSE(first.valid_transform);
    CHECK(first.primitive == StylizedPrimitiveType::RoundedBox);
    CHECK(first.role == VisualEntitySemanticRole::HardProp);
    CHECK(std::memcmp(
              &first.primitive_to_part_local,
              &second.primitive_to_part_local,
              sizeof(glm::mat4)) == 0);

    const auto& invalid_mesh = visual_entity_primitive_cache().mesh(
        static_cast<StylizedPrimitiveType>(255U),
        static_cast<StylizedPrimitiveLod>(255U));
    const auto& fallback_mesh = visual_entity_primitive_cache().mesh(
        StylizedPrimitiveType::RoundedBox,
        StylizedPrimitiveLod::Medium);
    CHECK(&invalid_mesh == &fallback_mesh);
}

} // namespace valcraft
