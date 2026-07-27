#include "render/StylizedShipMesh.h"
#include "world/BlockVisuals.h"

#include <doctest/doctest.h>

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/vec3.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <vector>

namespace valcraft {

namespace {

[[nodiscard]] auto vertex_position(
    const ChunkVertex& vertex) noexcept -> glm::vec3 {

    return {
        vertex.x,
        vertex.y,
        vertex.z,
    };
}

[[nodiscard]] auto vertex_normal(
    const ChunkVertex& vertex) noexcept -> glm::vec3 {

    return {
        vertex.nx,
        vertex.ny,
        vertex.nz,
    };
}

[[nodiscard]] auto finite_vertex(
    const ChunkVertex& vertex) noexcept -> bool {

    const std::array values {
        vertex.x,
        vertex.y,
        vertex.z,
        vertex.u,
        vertex.v,
        vertex.nx,
        vertex.ny,
        vertex.nz,
        vertex.face_shade,
        vertex.ao,
        vertex.sky_light,
        vertex.block_light,
        vertex.material_class,
        vertex.wave_weight,
    };
    return std::all_of(
        values.begin(),
        values.end(),
        [](float value) {
            return std::isfinite(value);
        });
}

[[nodiscard]] auto allowed_profile_half_width(
    const ShipProtectionProfile& profile,
    float local_y,
    float local_z) noexcept -> float {

    const auto hull_half_width =
        profile.half_width_at(
            local_z);
    if (local_y <
        profile.middle_hull_min_y) {
        return std::max(
            profile.lower_minimum_half_width,
            hull_half_width -
                profile.lower_width_inset);
    }
    if (local_y <
        profile.upper_hull_min_y) {
        return std::max(
            profile.middle_minimum_half_width,
            hull_half_width -
                profile.middle_width_inset);
    }
    return hull_half_width;
}

[[nodiscard]] auto range_end(
    const StylizedShipIndexRange& range) noexcept -> std::size_t {

    return range.first_index +
           range.index_count;
}

[[nodiscard]] auto maximum_triangle_edge(
    const StylizedShipMeshData& result,
    const StylizedShipIndexRange& range) noexcept -> float {

    auto maximum_length =
        0.0F;

    const auto end =
        range_end(range);

    for (auto index = range.first_index;
         index + 2U < end;
         index += 3U) {

        const auto first =
            vertex_position(
                result.mesh.vertices[
                    result.mesh.indices[index]]);

        const auto second =
            vertex_position(
                result.mesh.vertices[
                    result.mesh.indices[index + 1U]]);

        const auto third =
            vertex_position(
                result.mesh.vertices[
                    result.mesh.indices[index + 2U]]);

        maximum_length =
            std::max({
                maximum_length,
                glm::length(
                    second -
                    first),
                glm::length(
                    third -
                    second),
                glm::length(
                    first -
                    third),
            });
    }

    return maximum_length;
}

void check_valid_range(
    const StylizedShipMeshData& result,
    const StylizedShipIndexRange& range) {

    CHECK(range.first_index <=
          result.mesh.indices.size());
    CHECK(range_end(range) <=
          result.mesh.indices.size());
    CHECK(range.index_count % 3U ==
          0U);
}

[[nodiscard]] auto segment_intersects_triangle(
    const glm::vec3& origin,
    const glm::vec3& direction,
    float maximum_distance,
    const glm::vec3& first,
    const glm::vec3& second,
    const glm::vec3& third) noexcept -> bool {

    constexpr auto epsilon =
        1.0e-5F;
    const auto first_edge =
        second -
        first;
    const auto second_edge =
        third -
        first;
    const auto cross_direction =
        glm::cross(
            direction,
            second_edge);
    const auto determinant =
        glm::dot(
            first_edge,
            cross_direction);
    if (std::abs(determinant) <=
        epsilon) {
        return false;
    }
    const auto inverse_determinant =
        1.0F /
        determinant;
    const auto translated_origin =
        origin -
        first;
    const auto first_coordinate =
        glm::dot(
            translated_origin,
            cross_direction) *
        inverse_determinant;
    if (first_coordinate < 0.0F ||
        first_coordinate > 1.0F) {
        return false;
    }
    const auto cross_origin =
        glm::cross(
            translated_origin,
            first_edge);
    const auto second_coordinate =
        glm::dot(
            direction,
            cross_origin) *
        inverse_determinant;
    if (second_coordinate < 0.0F ||
        first_coordinate +
                second_coordinate >
            1.0F) {
        return false;
    }
    const auto distance =
        glm::dot(
            second_edge,
            cross_origin) *
        inverse_determinant;
    return distance >= 0.0F &&
           distance <=
               maximum_distance;
}

[[nodiscard]] auto hull_blocks_interior_axis(
    const StylizedShipMeshData& result,
    const ShipProtectionProfile& profile) -> bool {

    const auto origin =
        glm::vec3 {
            0.0F,
            std::lerp(
                profile.upper_hull_min_y,
                profile.main_deck_top_y,
                0.45F),
            profile.stern_z +
                0.5F,
        };
    const auto maximum_distance =
        profile.bow_z -
        profile.stern_z -
        1.0F;
    const auto end =
        range_end(
            result.metrics.hull);
    for (auto index =
             result.metrics.hull.first_index;
         index + 2U < end;
         index += 3U) {
        const auto first_index =
            result.mesh.indices[index];
        const auto second_index =
            result.mesh.indices[index + 1U];
        const auto third_index =
            result.mesh.indices[index + 2U];
        if (segment_intersects_triangle(
                origin,
                {0.0F, 0.0F, 1.0F},
                maximum_distance,
                vertex_position(
                    result.mesh.vertices[first_index]),
                vertex_position(
                    result.mesh.vertices[second_index]),
                vertex_position(
                    result.mesh.vertices[third_index]))) {
            return true;
        }
    }
    return false;
}

} // namespace

TEST_CASE("stylized ship mesh is deterministic and keyed by the immutable blueprint revision") {
    const auto& blueprint =
        amelie_ship_blueprint();
    const auto first =
        build_stylized_ship_mesh(
            blueprint,
            StylizedShipLod::Near);
    const auto second =
        build_stylized_ship_mesh(
            blueprint,
            StylizedShipLod::Near);

    REQUIRE_FALSE(first.empty());
    REQUIRE_FALSE(second.empty());
    CHECK(first.cache_key ==
          second.cache_key);
    CHECK(first.cache_key.geometry_revision ==
          blueprint.geometry_revision);
    CHECK(first.cache_key.lod ==
          StylizedShipLod::Near);
    CHECK(first.metrics.content_checksum ==
          second.metrics.content_checksum);
    CHECK(first.metrics ==
          second.metrics);
    CHECK(first.mesh.vertices.size() ==
          second.mesh.vertices.size());
    CHECK(first.mesh.indices ==
          second.mesh.indices);

    const ShipRenderState render_state {
        true,
        {},
        glm::mat4 {1.0F},
        &blueprint,
        blueprint.parts,
        blueprint.bounds,
        blueprint.bounds,
        blueprint.geometry_revision,
    };
    const auto from_render_state =
        build_stylized_ship_mesh(
            render_state,
            StylizedShipLod::Near);
    CHECK(from_render_state.cache_key ==
          first.cache_key);
    CHECK(from_render_state.metrics.content_checksum ==
          first.metrics.content_checksum);
}

TEST_CASE("stylized ship mesh emits finite unit normals and non degenerate indexed triangles") {
    const auto result =
        build_stylized_ship_mesh(
            amelie_ship_blueprint(),
            StylizedShipLod::Near);
    REQUIRE_FALSE(result.empty());
    REQUIRE(result.mesh.indices.size() % 3U ==
            0U);

    check_valid_range(
        result,
        result.metrics.hull);
    check_valid_range(
        result,
        result.metrics.decks);
    check_valid_range(
        result,
        result.metrics.structures);
    check_valid_range(
        result,
        result.metrics.rigging);
    check_valid_range(
        result,
        result.metrics.sails);

    for (const auto& vertex : result.mesh.vertices) {
        REQUIRE(finite_vertex(vertex));
        CHECK(glm::length(
                  vertex_normal(vertex)) ==
              doctest::Approx(1.0F).epsilon(0.0001));
        CHECK(vertex.u >= 0.0F);
        CHECK(vertex.u <= 1.0F);
        CHECK(vertex.v >= 0.0F);
        CHECK(vertex.v <= 1.0F);
        CHECK(vertex.wave_weight >= 0.0F);
        CHECK(vertex.wave_weight <= 1.0F);
    }

    for (std::size_t index = 0U;
         index < result.mesh.indices.size();
         index += 3U) {
        const auto first_index =
            result.mesh.indices[index];
        const auto second_index =
            result.mesh.indices[index + 1U];
        const auto third_index =
            result.mesh.indices[index + 2U];
        REQUIRE(first_index <
                result.mesh.vertices.size());
        REQUIRE(second_index <
                result.mesh.vertices.size());
        REQUIRE(third_index <
                result.mesh.vertices.size());
        const auto first =
            vertex_position(
                result.mesh.vertices[first_index]);
        const auto second =
            vertex_position(
                result.mesh.vertices[second_index]);
        const auto third =
            vertex_position(
                result.mesh.vertices[third_index]);
        const auto doubled_area =
            glm::length(
                glm::cross(
                    second -
                        first,
                    third -
                        first));
        CHECK(std::isfinite(doubled_area));
        CHECK(doubled_area >
              1.0e-5F);
    }
}

TEST_CASE("stylized ship far lod reduces hull rigging and sail geometry") {
    const auto near_mesh =
        build_stylized_ship_mesh(
            amelie_ship_blueprint(),
            StylizedShipLod::Near);
    const auto far_mesh =
        build_stylized_ship_mesh(
            amelie_ship_blueprint(),
            StylizedShipLod::Far);

    REQUIRE_FALSE(near_mesh.empty());
    REQUIRE_FALSE(far_mesh.empty());
    CHECK(far_mesh.cache_key.geometry_revision ==
          near_mesh.cache_key.geometry_revision);
    CHECK(far_mesh.cache_key.lod ==
          StylizedShipLod::Far);
    CHECK(far_mesh.mesh.indices.size() <
          near_mesh.mesh.indices.size());
    CHECK(far_mesh.metrics.hull.triangle_count() <
          near_mesh.metrics.hull.triangle_count());
    CHECK(far_mesh.metrics.rigging.triangle_count() <
          near_mesh.metrics.rigging.triangle_count());
    CHECK(far_mesh.metrics.sails.triangle_count() <
          near_mesh.metrics.sails.triangle_count());
}

TEST_CASE("stylized ship renders every support volume instead of collision-only surfaces") {
    const auto& blueprint =
        amelie_ship_blueprint();

    const auto result =
        build_stylized_ship_mesh(
            blueprint,
            StylizedShipLod::Near);

    REQUIRE(result.metrics.decks.index_count >
            0U);

    CHECK(result.metrics.maximum_deck_alignment_error ==
          doctest::Approx(0.0F));

    std::vector<ShipBounds> support_bounds;

    for (const auto& part : blueprint.parts) {
        if (part.supports_player &&
            part.material ==
                ShipMaterial::LightDeck &&
            (part.shape ==
                 ShipPartShape::Box ||
             part.shape ==
                 ShipPartShape::Slab ||
             part.shape ==
                 ShipPartShape::Stair)) {

            support_bounds.push_back({
                glm::min(
                    part.local_start,
                    part.local_end),
                glm::max(
                    part.local_start,
                    part.local_end),
            });
        }
    }

    REQUIRE_FALSE(support_bounds.empty());

    constexpr auto tolerance =
        1.0e-4F;

    auto found_main_deck =
        false;
    auto found_visible_underside =
        false;
    auto found_visible_side =
        false;

    const auto deck_end =
        range_end(
            result.metrics.decks);

    for (auto index =
             result.metrics.decks.first_index;
         index < deck_end;
         ++index) {
        const auto& vertex =
            result.mesh.vertices[
                result.mesh.indices[index]];

        const auto point =
            vertex_position(vertex);

        const auto matches_support_surface =
            std::any_of(
                support_bounds.begin(),
                support_bounds.end(),
                [&](const ShipBounds& bounds) {

                    const auto inside =
                        point.x >=
                            bounds.min.x -
                                tolerance &&
                        point.x <=
                            bounds.max.x +
                                tolerance &&
                        point.y >=
                            bounds.min.y -
                                tolerance &&
                        point.y <=
                            bounds.max.y +
                                tolerance &&
                        point.z >=
                            bounds.min.z -
                                tolerance &&
                        point.z <=
                            bounds.max.z +
                                tolerance;

                    const auto on_boundary =
                        std::abs(
                            point.x -
                            bounds.min.x) <=
                            tolerance ||
                        std::abs(
                            point.x -
                            bounds.max.x) <=
                            tolerance ||
                        std::abs(
                            point.y -
                            bounds.min.y) <=
                            tolerance ||
                        std::abs(
                            point.y -
                            bounds.max.y) <=
                            tolerance ||
                        std::abs(
                            point.z -
                            bounds.min.z) <=
                            tolerance ||
                        std::abs(
                            point.z -
                            bounds.max.z) <=
                            tolerance;

                    return inside &&
                           on_boundary;
                });

        CHECK(matches_support_surface);

        found_main_deck =
            found_main_deck ||
            std::abs(
                vertex.y -
                blueprint.protection_profile.main_deck_top_y) <=
                tolerance;

        found_visible_underside =
            found_visible_underside ||
            vertex.ny < -0.90F;

        found_visible_side =
            found_visible_side ||
            std::abs(vertex.ny) <
                0.10F;
    }

    CHECK(found_main_deck);
    CHECK(found_visible_underside);
    CHECK(found_visible_side);

    // Une tuile n'est plus étirée sur toute la largeur du pont.
    CHECK(
        maximum_triangle_edge(
            result,
            result.metrics.decks) <
        3.0F);
}

TEST_CASE("stylized ship keeps visible inward hull walls and sheltered lighting") {
    const auto& blueprint =
        amelie_ship_blueprint();

    const auto result =
        build_stylized_ship_mesh(
            blueprint,
            StylizedShipLod::Near);

    REQUIRE(result.metrics.structures.index_count >
            0U);

    auto inward_wall_vertex_count =
        std::size_t {0U};

    auto minimum_interior_sky =
        1.0F;

    const auto structure_end =
        range_end(
            result.metrics.structures);

    for (auto index =
             result.metrics.structures.first_index;
         index < structure_end;
         ++index) {

        const auto& vertex =
            result.mesh.vertices[
                result.mesh.indices[index]];

        const auto point =
            vertex_position(vertex);

        const auto normal =
            vertex_normal(vertex);

        const auto upper_interior_wall =
            point.y >
                blueprint.protection_profile.upper_hull_min_y &&
            point.y <
                blueprint.protection_profile.main_deck_top_y -
                    0.20F &&
            std::abs(point.x) >
                0.70F &&
            std::abs(normal.x) >
                0.70F &&
            point.x *
                    normal.x <
                -0.30F;

        if (!upper_interior_wall) {
            continue;
        }

        ++inward_wall_vertex_count;

        minimum_interior_sky =
            std::min(
                minimum_interior_sky,
                vertex.sky_light);
    }

    CHECK(inward_wall_vertex_count >
          100U);

    CHECK(minimum_interior_sky <
          0.90F);
}

TEST_CASE("stylized ship keeps interior textiles horizontal and reserves sail deformation for real sails") {
    const auto result =
        build_stylized_ship_mesh(
            amelie_ship_blueprint(),
            StylizedShipLod::Near);

    const auto fabric_material =
        block_visual_material_value(
            BlockVisualMaterial::Fabric);

    auto found_horizontal_interior_textile =
        false;

    const auto structure_end =
        range_end(
            result.metrics.structures);

    for (auto index =
             result.metrics.structures.first_index;
         index < structure_end;
         ++index) {

        const auto& vertex =
            result.mesh.vertices[
                result.mesh.indices[index]];

        found_horizontal_interior_textile =
            found_horizontal_interior_textile ||
            (
                vertex.y < 2.10F &&
                std::abs(vertex.ny) >
                    0.90F &&
                std::abs(
                    vertex.material_class -
                    fabric_material) <
                    0.01F
            );
    }

    CHECK(found_horizontal_interior_textile);

    auto minimum_sail_height =
        (std::numeric_limits<float>::max)();

    const auto sail_end =
        range_end(
            result.metrics.sails);

    for (auto index =
             result.metrics.sails.first_index;
         index < sail_end;
         ++index) {

        const auto& vertex =
            result.mesh.vertices[
                result.mesh.indices[index]];

        minimum_sail_height =
            std::min(
                minimum_sail_height,
                vertex.y);
    }

    // Les matelas et tapis situés vers Y=1 ne doivent plus apparaître dans la
    // plage géométrique des voiles.
    CHECK(minimum_sail_height >
          7.50F);
}

TEST_CASE("stylized ship renders identity glyphs and bounds texture stretching") {
    const auto result =
        build_stylized_ship_mesh(
            amelie_ship_blueprint(),
            StylizedShipLod::Near);

    auto found_main_mast_glyph =
        false;

    const auto structure_end =
        range_end(
            result.metrics.structures);

    for (auto index =
             result.metrics.structures.first_index;
         index < structure_end;
         ++index) {

        const auto& vertex =
            result.mesh.vertices[
                result.mesh.indices[index]];

        found_main_mast_glyph =
            found_main_mast_glyph ||
            (
                std::abs(vertex.x) <
                    0.20F &&
                vertex.y >
                    14.0F &&
                vertex.y <
                    19.1F &&
                vertex.z >
                    -0.25F &&
                vertex.z <
                    -0.18F
            );
    }

    CHECK(found_main_mast_glyph);

    CHECK(
        maximum_triangle_edge(
            result,
            result.metrics.structures) <
        3.0F);

    CHECK(
        maximum_triangle_edge(
            result,
            result.metrics.rigging) <
        2.25F);
}

TEST_CASE("stylized hull follows the protection profile and keeps tapered ends") {
    const auto& blueprint =
        amelie_ship_blueprint();
    const auto& profile =
        blueprint.protection_profile;
    const auto result =
        build_stylized_ship_mesh(
            blueprint,
            StylizedShipLod::Near);
    REQUIRE(result.metrics.hull.index_count >
            0U);
    CHECK(result.metrics.maximum_profile_deviation <=
          1.0e-5F);
    CHECK(result.metrics.maximum_protection_excess <=
          1.0e-5F);
    CHECK(result.metrics.bow_half_width <
          result.metrics.midship_half_width *
              0.30F);
    CHECK(result.metrics.stern_half_width <
          result.metrics.midship_half_width);

    auto has_bow =
        false;
    auto has_stern =
        false;
    const auto hull_end =
        range_end(
            result.metrics.hull);
    for (auto index =
             result.metrics.hull.first_index;
         index < hull_end;
         ++index) {
        const auto& vertex =
            result.mesh.vertices[
                result.mesh.indices[index]];
        const auto clamped_z =
            std::clamp(
                vertex.z,
                profile.stern_z,
                profile.bow_z);
        const auto allowed_half_width =
            allowed_profile_half_width(
                profile,
                vertex.y,
                clamped_z);
        CHECK(std::abs(vertex.x) <=
              allowed_half_width +
                  profile.boundary_margin +
                  1.0e-5F);
        CHECK(vertex.y >=
              profile.lower_hull_min_y -
                  1.0e-5F);
        CHECK(vertex.y <=
              profile.main_deck_top_y +
                  1.0e-5F);
        CHECK(vertex.z >=
              profile.stern_z -
                  profile.boundary_margin -
                  1.0e-5F);
        CHECK(vertex.z <=
              profile.bow_z +
                  profile.boundary_margin +
                  1.0e-5F);
        has_bow =
            has_bow ||
            std::abs(
                vertex.z -
                profile.bow_z) <=
                1.0e-5F;
        has_stern =
            has_stern ||
            std::abs(
                vertex.z -
                profile.stern_z) <=
                1.0e-5F;
    }
    CHECK(has_bow);
    CHECK(has_stern);
}

TEST_CASE("stylized hull normals remain smooth and point outside the protected volume") {
    const auto& blueprint =
        amelie_ship_blueprint();
    const auto& profile =
        blueprint.protection_profile;
    const auto result =
        build_stylized_ship_mesh(
            blueprint,
            StylizedShipLod::Near);
    REQUIRE(result.metrics.hull.index_count >
            0U);

    std::map<std::array<float, 3>, glm::vec3>
        interior_normals;
    std::size_t shared_vertex_count = 0U;
    std::size_t outward_side_count = 0U;
    std::size_t downward_keel_count = 0U;
    const auto hull_end =
        range_end(
            result.metrics.hull);
    for (auto index =
             result.metrics.hull.first_index;
         index < hull_end;
         ++index) {
        const auto& vertex =
            result.mesh.vertices[
                result.mesh.indices[index]];
        const auto position =
            vertex_position(vertex);
        const auto normal =
            vertex_normal(vertex);

        CHECK(normal.x *
                  position.x >=
              -1.0e-5F);
        if (std::abs(position.x) >
                0.25F &&
            std::abs(normal.x) >
                0.10F) {
            CHECK(normal.x *
                      position.x >
                  0.0F);
            ++outward_side_count;
        }
        if (std::abs(position.x) <=
                1.0e-5F &&
            std::abs(
                position.y -
                profile.lower_hull_min_y) <=
                1.0e-5F &&
            position.z >
                profile.stern_z +
                    1.0e-4F &&
            position.z <
                profile.bow_z -
                    1.0e-4F) {
            CHECK(normal.y <
                  -0.50F);
            ++downward_keel_count;
        }

        if (position.z <=
                profile.stern_z +
                    1.0e-4F ||
            position.z >=
                profile.bow_z -
                    1.0e-4F) {
            continue;
        }
        const std::array key {
            position.x,
            position.y,
            position.z,
        };
        const auto [iterator, inserted] =
            interior_normals.emplace(
                key,
                normal);
        if (!inserted) {
            CHECK(glm::dot(
                      iterator->second,
                      normal) >
                  0.9999F);
            ++shared_vertex_count;
        }
    }
    CHECK(outward_side_count >
          100U);
    CHECK(downward_keel_count >
          10U);
    CHECK(shared_vertex_count >
          100U);
}

TEST_CASE("stylized hull leaves the playable interior viewing axis open") {
    const auto& blueprint =
        amelie_ship_blueprint();
    const auto result =
        build_stylized_ship_mesh(
            blueprint,
            StylizedShipLod::Near);

    REQUIRE(result.metrics.hull.index_count >
            0U);
    CHECK(result.metrics.interior_axis_open);
    CHECK_FALSE(
        hull_blocks_interior_axis(
            result,
            blueprint.protection_profile));
}

TEST_CASE("stylized sails are subdivided double sided and carry animation weights") {
    const auto result =
        build_stylized_ship_mesh(
            amelie_ship_blueprint(),
            StylizedShipLod::Near);
    REQUIRE(result.metrics.sails.triangle_count() >
            24U);

    auto minimum_weight =
        (std::numeric_limits<float>::max)();
    auto maximum_weight =
        (std::numeric_limits<float>::lowest)();
    const auto sail_end =
        range_end(
            result.metrics.sails);
    for (auto index =
             result.metrics.sails.first_index;
         index < sail_end;
         ++index) {
        const auto& vertex =
            result.mesh.vertices[
                result.mesh.indices[index]];
        minimum_weight =
            std::min(
                minimum_weight,
                vertex.wave_weight);
        maximum_weight =
            std::max(
                maximum_weight,
                vertex.wave_weight);
    }
    CHECK(minimum_weight ==
          doctest::Approx(0.0F));
    CHECK(maximum_weight ==
          doctest::Approx(1.0F));
}

TEST_CASE("stylized square sails preserve continuous UVs and curved lighting normals") {
    const auto result =
        build_stylized_ship_mesh(
            amelie_ship_blueprint(),
            StylizedShipLod::Near);
    REQUIRE(result.metrics.sails.index_count >
            0U);

    std::set<int> horizontal_coordinates;
    std::set<int> vertical_coordinates;
    auto curved_normal_count =
        std::size_t {0U};
    const auto fabric_material =
        block_visual_material_value(
            BlockVisualMaterial::Fabric);
    const auto sail_end =
        range_end(
            result.metrics.sails);
    for (auto index =
             result.metrics.sails.first_index;
         index < sail_end;
         ++index) {
        const auto& vertex =
            result.mesh.vertices[
                result.mesh.indices[index]];
        if (std::abs(
                vertex.material_class -
                fabric_material) >
                0.01F ||
            std::abs(vertex.nz) <
                0.70F) {
            continue;
        }

        const auto atlas_u =
            vertex.u *
            kBlockAtlasTilesPerAxis;
        const auto atlas_v =
            vertex.v *
            kBlockAtlasTilesPerAxis;
        horizontal_coordinates.insert(
            static_cast<int>(
                std::lround(
                    (
                        atlas_u -
                        std::floor(atlas_u)
                    ) *
                    10'000.0F)));
        vertical_coordinates.insert(
            static_cast<int>(
                std::lround(
                    (
                        atlas_v -
                        std::floor(atlas_v)
                    ) *
                    10'000.0F)));
        if (std::abs(vertex.nx) +
                std::abs(vertex.ny) >
            1.0e-3F) {
            ++curved_normal_count;
        }
    }

    CHECK(horizontal_coordinates.size() >=
          9U);
    CHECK(vertical_coordinates.size() >=
          7U);
    CHECK(curved_normal_count >
          20U);
}

TEST_CASE("stylized ship rejects a render state without a blueprint") {
    ShipRenderState state {};
    state.geometry_revision =
        0xA6E11EULL;
    const auto result =
        build_stylized_ship_mesh(
            state,
            StylizedShipLod::Far);
    CHECK(result.empty());
    CHECK(result.cache_key.geometry_revision ==
          state.geometry_revision);
    CHECK(result.cache_key.lod ==
          StylizedShipLod::Far);
}

} // namespace valcraft
