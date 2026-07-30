#include "render/StylizedShipMesh.h"
#include "render/ShipMesh.h"
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

[[nodiscard]] auto range_intersects_ray(
    const StylizedShipMeshData& result,
    const StylizedShipIndexRange& range,
    const glm::vec3& origin,
    const glm::vec3& direction,
    float maximum_distance) -> bool {

    const auto end =
        range_end(range);
    for (auto index =
             range.first_index;
         index + 2U < end;
         index += 3U) {
        if (segment_intersects_triangle(
                origin,
                direction,
                maximum_distance,
                vertex_position(
                    result.mesh.vertices[
                        result.mesh.indices[index]]),
                vertex_position(
                    result.mesh.vertices[
                        result.mesh.indices[index + 1U]]),
                vertex_position(
                    result.mesh.vertices[
                        result.mesh.indices[index + 2U]]))) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] auto range_intersects_front_ray(
    const StylizedShipMeshData& result,
    const StylizedShipIndexRange& range,
    const glm::vec3& origin,
    const glm::vec3& direction,
    float maximum_distance) -> bool {

    const auto end =
        range_end(range);
    for (auto index =
             range.first_index;
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
        const auto raw_normal =
            glm::cross(
                second - first,
                third - first);
        const auto normal_length =
            glm::length(
                raw_normal);
        if (normal_length <=
                1.0e-6F ||
            glm::dot(
                raw_normal /
                    normal_length,
                direction) >=
                -1.0e-4F) {
            continue;
        }

        if (segment_intersects_triangle(
                origin,
                direction,
                maximum_distance,
                first,
                second,
                third)) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] auto interior_profile_half_width(
    const ShipProtectionProfile& profile,
    float local_y,
    float local_z) noexcept -> float {

    const auto outer_half_width =
        allowed_profile_half_width(
            profile,
            local_y,
            local_z);
    const auto wall_thickness =
        std::min(
            0.44F,
            std::max(
                0.22F,
                outer_half_width *
                    0.36F));
    return std::max(
        0.48F,
        outer_half_width -
            wall_thickness);
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

TEST_CASE("stylized ship bakes exterior lanterns deterministically without changing either lod geometry") {
    const auto& lit_blueprint =
        amelie_ship_blueprint();
    auto unlit_blueprint =
        lit_blueprint;
    unlit_blueprint.exterior_lanterns = {};

    for (const auto lod :
         {StylizedShipLod::Near,
          StylizedShipLod::Far}) {
        CAPTURE(
            static_cast<int>(
                lod));
        const auto lit =
            build_stylized_ship_mesh(
                lit_blueprint,
                lod);
        const auto lit_again =
            build_stylized_ship_mesh(
                lit_blueprint,
                lod);
        const auto unlit =
            build_stylized_ship_mesh(
                unlit_blueprint,
                lod);

        REQUIRE_FALSE(
            lit.empty());
        REQUIRE(
            lit.mesh.vertices.size() ==
            unlit.mesh.vertices.size());
        CHECK(
            lit.mesh.indices ==
            unlit.mesh.indices);
        CHECK(
            lit.mesh.face_count ==
            unlit.mesh.face_count);
        CHECK(
            lit.metrics.content_checksum ==
            lit_again.metrics.content_checksum);

        auto geometry_unchanged =
            true;
        auto illuminated_deck_vertices =
            std::size_t {0U};
        auto excluded_material_unchanged =
            true;
        auto sheltered_vertices_unchanged =
            true;
        auto maximum_deck_light =
            0.0F;
        auto central_passage_vertex_count =
            std::size_t {0U};
        auto minimum_central_passage_light =
            std::numeric_limits<float>::infinity();
        for (auto index = std::size_t {0U};
             index < lit.mesh.vertices.size();
             ++index) {
            const auto& lit_vertex =
                lit.mesh.vertices[index];
            const auto& unlit_vertex =
                unlit.mesh.vertices[index];
            geometry_unchanged =
                geometry_unchanged &&
                lit_vertex.x == unlit_vertex.x &&
                lit_vertex.y == unlit_vertex.y &&
                lit_vertex.z == unlit_vertex.z &&
                lit_vertex.u == unlit_vertex.u &&
                lit_vertex.v == unlit_vertex.v &&
                lit_vertex.nx == unlit_vertex.nx &&
                lit_vertex.ny == unlit_vertex.ny &&
                lit_vertex.nz == unlit_vertex.nz &&
                lit_vertex.material_class ==
                    unlit_vertex.material_class &&
                lit_vertex.wave_weight ==
                    unlit_vertex.wave_weight;

            const auto material =
                static_cast<ShipMaterial>(
                    static_cast<int>(
                        std::lround(
                            lit_vertex.material_class)));
            const auto excluded_material =
                material ==
                    ShipMaterial::Lantern ||
                material ==
                    ShipMaterial::CreamCanvas ||
                material ==
                    ShipMaterial::BlackCanvas;
            if (excluded_material) {
                excluded_material_unchanged =
                    excluded_material_unchanged &&
                    lit_vertex.block_light ==
                        unlit_vertex.block_light;
            }
            const auto sheltered =
                lit_vertex.y <
                    3.65F ||
                lit_vertex.sky_light <
                    0.58F;
            if (sheltered) {
                sheltered_vertices_unchanged =
                    sheltered_vertices_unchanged &&
                    lit_vertex.block_light ==
                        unlit_vertex.block_light;
            }
            const auto central_passage =
                material ==
                    ShipMaterial::LightDeck &&
                std::abs(
                    lit_vertex.x) <=
                    1.25F &&
                std::abs(
                    lit_vertex.y -
                        lit_blueprint
                            .protection_profile
                            .main_deck_top_y) <=
                    0.06F &&
                lit_vertex.z >=
                    -8.0F &&
                lit_vertex.z <=
                    7.0F &&
                lit_vertex.ny >
                    0.35F &&
                lit_vertex.sky_light >=
                    0.58F;
            if (central_passage) {
                ++central_passage_vertex_count;
                minimum_central_passage_light =
                    std::min(
                        minimum_central_passage_light,
                        lit_vertex.block_light);
            }
            if (material !=
                    ShipMaterial::LightDeck ||
                lit_vertex.y <
                    3.65F ||
                lit_vertex.sky_light <
                    0.58F ||
                lit_vertex.block_light <=
                    unlit_vertex.block_light +
                        1.0e-5F) {
                continue;
            }
            ++illuminated_deck_vertices;
            maximum_deck_light =
                std::max(
                    maximum_deck_light,
                    lit_vertex.block_light);
        }

        CHECK(
            geometry_unchanged);
        CHECK(
            excluded_material_unchanged);
        CHECK(
            sheltered_vertices_unchanged);
        CHECK(
            illuminated_deck_vertices >
            0U);
        CHECK(
            maximum_deck_light >
            0.05F);
        CAPTURE(
            central_passage_vertex_count);
        CAPTURE(
            minimum_central_passage_light);
        CHECK(
            central_passage_vertex_count >
            0U);
        CHECK(
            minimum_central_passage_light >
            0.01F);
    }
}

TEST_CASE("les pieces abritees nommees refusent explicitement le bake des fanaux exterieurs") {
    const auto& lit_blueprint =
        amelie_ship_blueprint();
    auto unlit_blueprint =
        lit_blueprint;
    unlit_blueprint.exterior_lanterns = {};

    for (const auto lod :
         {StylizedShipLod::Near,
          StylizedShipLod::Far}) {
        CAPTURE(
            static_cast<int>(
                lod));
        const auto lit =
            build_stylized_ship_mesh(
                lit_blueprint,
                lod);
        const auto unlit =
            build_stylized_ship_mesh(
                unlit_blueprint,
                lod);
        REQUIRE(
            lit.mesh.vertices.size() ==
            unlit.mesh.vertices.size());

        auto fore_hatch_wall_vertex_count =
            std::size_t {0U};
        auto fore_hatch_wall_unchanged =
            true;
        auto battery_ceiling_vertex_count =
            std::size_t {0U};
        auto battery_ceiling_unchanged =
            true;
        auto closed_battery_transom_vertex_count =
            std::size_t {0U};
        auto closed_battery_transom_unchanged =
            true;

        for (auto index =
                 std::size_t {0U};
             index <
             lit.mesh.vertices.size();
             ++index) {
            const auto& lit_vertex =
                lit.mesh.vertices[index];
            const auto& unlit_vertex =
                unlit.mesh.vertices[index];
            const auto material =
                static_cast<ShipMaterial>(
                    static_cast<int>(
                        std::lround(
                            lit_vertex
                                .material_class)));
            const auto unchanged =
                lit_vertex.block_light ==
                unlit_vertex.block_light;

            // Je verrouille la paroi intérieure de la trémie avant, pas son
            // hiloire extérieur : le fanal ne doit pas descendre par l'ouverture.
            const auto fore_hatch_inner_wall =
                material ==
                    ShipMaterial::LightDeck &&
                std::abs(
                    std::abs(
                        lit_vertex.x) -
                    1.22F) <=
                    1.0e-3F &&
                lit_vertex.y >=
                    3.64F &&
                lit_vertex.y <=
                    4.01F &&
                lit_vertex.z >=
                    9.0F &&
                lit_vertex.z <=
                    13.0F &&
                std::abs(
                    lit_vertex.nx) >=
                    0.99F &&
                lit_vertex.x *
                        lit_vertex.nx <
                    -0.95F;
            if (fore_hatch_inner_wall) {
                ++fore_hatch_wall_vertex_count;
                fore_hatch_wall_unchanged =
                    fore_hatch_wall_unchanged &&
                    unchanged;
            }

            // Je cible la vraie sous-face du pont principal au-dessus de la
            // batterie, loin des deux trémies et de leurs débordements.
            const auto battery_ceiling =
                material ==
                    ShipMaterial::LightDeck &&
                std::abs(
                    lit_vertex.y -
                    3.65F) <=
                    1.0e-3F &&
                lit_vertex.ny <=
                    -0.99F &&
                std::abs(
                    lit_vertex.x) <=
                    1.0F &&
                lit_vertex.z >=
                    -7.0F &&
                lit_vertex.z <=
                    7.0F;
            if (battery_ceiling) {
                ++battery_ceiling_vertex_count;
                battery_ceiling_unchanged =
                    battery_ceiling_unchanged &&
                    unchanged;
            }

            // Je contrôle la fermeture centrale du pont-batterie au tableau
            // arrière, qui reste volontairement présente dans les deux LOD.
            const auto closed_battery_transom =
                material ==
                    ShipMaterial::DarkHull &&
                std::abs(
                    lit_vertex.z -
                    (lit_blueprint
                         .protection_profile
                         .stern_z -
                     0.46F)) <=
                    1.0e-3F &&
                lit_vertex.nz <=
                    -0.99F &&
                std::abs(
                    lit_vertex.x) <=
                    1.0F &&
                lit_vertex.y >=
                    -0.90F &&
                lit_vertex.y <=
                    0.90F;
            if (closed_battery_transom) {
                ++closed_battery_transom_vertex_count;
                closed_battery_transom_unchanged =
                    closed_battery_transom_unchanged &&
                    unchanged;
            }
        }

        CAPTURE(
            fore_hatch_wall_vertex_count);
        CAPTURE(
            battery_ceiling_vertex_count);
        CAPTURE(
            closed_battery_transom_vertex_count);
        CHECK(
            fore_hatch_wall_vertex_count >
            0U);
        CHECK(
            fore_hatch_wall_unchanged);
        CHECK(
            battery_ceiling_vertex_count >
            0U);
        CHECK(
            battery_ceiling_unchanged);
        CHECK(
            closed_battery_transom_vertex_count >
            0U);
        CHECK(
            closed_battery_transom_unchanged);
    }
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
        CHECK(vertex.v >= 0.0F);
        // Je garde une borne de sécurité en mètres locaux sans réinterdire la
        // répétition physique des textures sur les grandes surfaces.
        CHECK(vertex.u <= 128.0F);
        CHECK(vertex.v <= 128.0F);
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
    CHECK(far_mesh.metrics.decks.triangle_count() <
          near_mesh.metrics.decks.triangle_count());
    CHECK(far_mesh.metrics.structures.triangle_count() <
          near_mesh.metrics.structures.triangle_count());
    CHECK(far_mesh.metrics.rigging.triangle_count() <
          near_mesh.metrics.rigging.triangle_count());
    // Je borne séparément le gréement : les cordages fins et les petites roues
    // ne doivent plus consommer la majorité du budget du mobilier intérieur.
    CHECK(near_mesh.metrics.rigging.triangle_count() <=
          90'000U);
    CHECK(far_mesh.metrics.rigging.triangle_count() <=
          15'500U);
    CHECK(far_mesh.metrics.sails.triangle_count() <
          near_mesh.metrics.sails.triangle_count());
    CHECK(far_mesh.mesh.vertices.size() * 2U <
          near_mesh.mesh.vertices.size());
}

TEST_CASE("stylized ship shadows prefer the simplified lod with a safe fallback") {
    CHECK(
        stylized_ship_shadow_lod(
            0,
            StylizedShipLod::Near,
            true,
            true) ==
        StylizedShipLod::Near);
    CHECK(
        stylized_ship_shadow_lod(
            1,
            StylizedShipLod::Near,
            true,
            true) ==
        StylizedShipLod::Far);
    CHECK(
        stylized_ship_shadow_lod(
            0,
            StylizedShipLod::Near,
            true,
            false) ==
        StylizedShipLod::Far);
    CHECK(
        stylized_ship_shadow_lod(
            1,
            StylizedShipLod::Near,
            false,
            false) ==
        StylizedShipLod::Near);
    CHECK(
        stylized_ship_shadow_lod(
            0,
            StylizedShipLod::Far,
            true,
            true) ==
        StylizedShipLod::Far);
}

TEST_CASE("stylized ship far lod removes interior segments but keeps exterior rigging") {
    const std::array<ShipPart, 1>
        interior_segment {{
            {
                ShipPartShape::Segment,
                ShipMaterial::Rope,
                {0.0F, -1.60F, -4.0F},
                {0.0F, -0.40F, -2.0F},
                {0.0F, 1.0F, 0.0F},
                0.06F,
            },
        }};
    auto interior_blueprint =
        amelie_ship_blueprint();
    interior_blueprint.parts =
        interior_segment;
    const auto interior_near =
        build_stylized_ship_mesh(
            interior_blueprint,
            StylizedShipLod::Near);
    const auto interior_far =
        build_stylized_ship_mesh(
            interior_blueprint,
            StylizedShipLod::Far);

    // Je verrouille le cas des lignes dégénérées : leur volume rendu n'a pas
    // trois dimensions, mais leur centre suffit pour les exclure du LOD Far.
    CHECK(
        interior_near.metrics.rigging.triangle_count() >
        0U);
    CHECK(
        interior_far.metrics.rigging.triangle_count() ==
        0U);

    const std::array<ShipPart, 1>
        exterior_segment {{
            {
                ShipPartShape::Segment,
                ShipMaterial::Rope,
                {0.0F, 3.80F, 0.0F},
                {0.0F, 8.00F, 0.0F},
                {0.0F, 1.0F, 0.0F},
                0.06F,
            },
        }};
    auto exterior_blueprint =
        amelie_ship_blueprint();
    exterior_blueprint.parts =
        exterior_segment;
    const auto exterior_far =
        build_stylized_ship_mesh(
            exterior_blueprint,
            StylizedShipLod::Far);
    CHECK(
        exterior_far.metrics.rigging.triangle_count() >
        0U);
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

    auto sheltered_hull_vertex_count =
        std::size_t {0U};
    auto maximum_sheltered_hull_block =
        0.0F;
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
        REQUIRE(
            std::isfinite(
                vertex.block_light));
        if (vertex.y >=
            3.60F) {
            continue;
        }
        ++sheltered_hull_vertex_count;
        maximum_sheltered_hull_block =
            std::max(
                maximum_sheltered_hull_block,
                vertex.block_light);
    }
    CHECK(
        sheltered_hull_vertex_count >
        0U);
    CHECK(
        maximum_sheltered_hull_block ==
        doctest::Approx(0.0F));

    auto maximum_lantern_emission =
        0.0F;
    for (const auto& vertex :
         result.mesh.vertices) {
        if (vertex.material_class !=
            static_cast<float>(
                ShipMaterial::Lantern)) {
            continue;
        }
        REQUIRE(
            std::isfinite(
                vertex.block_light));
        maximum_lantern_emission =
            std::max(
                maximum_lantern_emission,
                vertex.block_light);
    }
    CHECK(
        maximum_lantern_emission >=
        11.0F / 15.0F);
}

TEST_CASE("stylized ship cuts every near gunport while the far hull stays sealed") {
    const auto& blueprint =
        amelie_ship_blueprint();
    const auto near_mesh =
        build_stylized_ship_mesh(
            blueprint,
            StylizedShipLod::Near);
    const auto far_mesh =
        build_stylized_ship_mesh(
            blueprint,
            StylizedShipLod::Far);

    constexpr std::array cannon_rows {
        -15.5F,
        -9.5F,
        -3.5F,
        4.5F,
        10.5F,
        16.5F,
    };
    for (const auto local_z :
         cannon_rows) {
        for (const auto side_sign :
             {-1.0F, 1.0F}) {
            const auto direction =
                glm::vec3 {
                    side_sign,
                    0.0F,
                    0.0F,
                };
            const auto distance =
                blueprint.protection_profile
                    .half_width_at(
                        local_z) +
                1.0F;
            CAPTURE(local_z);
            CAPTURE(side_sign);

            CHECK_FALSE(
                range_intersects_ray(
                    near_mesh,
                    near_mesh.metrics.hull,
                    {0.0F, 1.78F, local_z},
                    direction,
                    distance));
            CHECK(
                range_intersects_ray(
                    far_mesh,
                    far_mesh.metrics.hull,
                    {0.0F, 1.78F, local_z},
                    direction,
                    distance));
            CHECK(
                range_intersects_ray(
                    near_mesh,
                    near_mesh.metrics.hull,
                    {0.0F, 1.16F, local_z},
                    direction,
                    distance));
            CHECK(
                range_intersects_ray(
                    near_mesh,
                    near_mesh.metrics.hull,
                    {0.0F, 2.42F, local_z},
                    direction,
                    distance));
            CHECK(
                range_intersects_ray(
                    near_mesh,
                    near_mesh.metrics.hull,
                    {0.0F,
                     1.78F,
                     local_z + 0.66F},
                    direction,
                    distance));
        }
    }

    for (const auto side_sign :
         {-1.0F, 1.0F}) {
        const auto direction =
            glm::vec3 {
                side_sign,
                0.0F,
                0.0F,
            };
        const auto distance =
            blueprint.protection_profile
                .half_width_at(
                    -32.5F) +
            1.0F;
        CHECK_FALSE(
            range_intersects_ray(
                near_mesh,
                near_mesh.metrics.hull,
                {0.0F, 2.05F, -32.5F},
                direction,
                distance));
        CHECK(
            range_intersects_ray(
                far_mesh,
                far_mesh.metrics.hull,
                {0.0F, 2.05F, -32.5F},
                direction,
                distance));
    }
}

TEST_CASE("stylized ship visibly closes its chines and every playable deck end") {
    const auto& blueprint =
        amelie_ship_blueprint();
    const auto& profile =
        blueprint.protection_profile;
    const auto near_mesh =
        build_stylized_ship_mesh(
            blueprint,
            StylizedShipLod::Near);

    REQUIRE(
        near_mesh.metrics.structures.index_count >
        0U);

    constexpr std::array chine_z_samples {
        -28.0F,
        0.0F,
        21.35F,
    };
    const std::array chine_y_samples {
        profile.middle_hull_min_y,
        profile.upper_hull_min_y,
    };
    for (const auto local_z :
         chine_z_samples) {
        for (const auto transition_y :
             chine_y_samples) {
            const auto narrower_inner =
                interior_profile_half_width(
                    profile,
                    transition_y - 0.02F,
                    local_z);
            const auto wider_inner =
                interior_profile_half_width(
                    profile,
                    transition_y + 0.02F,
                    local_z);
            for (const auto side_sign :
                 {-1.0F, 1.0F}) {
                const auto origin =
                    glm::vec3 {
                        side_sign *
                            (narrower_inner -
                             0.62F),
                        transition_y +
                            0.46F,
                        local_z,
                    };
                const auto target =
                    glm::vec3 {
                        side_sign *
                            ((narrower_inner +
                              wider_inner) *
                             0.5F),
                        transition_y,
                        local_z,
                    };
                const auto ray =
                    target - origin;
                const auto distance =
                    glm::length(ray);
                CAPTURE(local_z);
                CAPTURE(transition_y);
                CAPTURE(side_sign);
                CHECK(
                    range_intersects_front_ray(
                        near_mesh,
                        near_mesh.metrics.structures,
                        origin,
                        ray / distance,
                        distance + 0.12F));
            }
        }
    }

    struct TerminalView {
        float floor_y;
        float ceiling_y;
        float wall_z;
        float inside_z;
        float direction_z;
    };
    constexpr std::array terminal_views {
        TerminalView {-5.00F, -2.28F, -33.0F, -31.8F, -1.0F},
        TerminalView {-5.00F, -2.28F, 24.0F, 22.8F, 1.0F},
        TerminalView {-2.00F, 0.72F, -34.0F, -32.8F, -1.0F},
        TerminalView {-2.00F, 0.72F, 31.0F, 29.8F, 1.0F},
        TerminalView {1.00F, 3.65F, 34.0F, 32.8F, 1.0F},
    };
    for (const auto& view :
         terminal_views) {
        const auto local_y =
            (view.floor_y +
             view.ceiling_y) *
            0.5F;
        const auto half_width =
            interior_profile_half_width(
                profile,
                local_y,
                view.wall_z);
        for (const auto x_ratio :
             {-0.88F, 0.0F, 0.88F}) {
            CAPTURE(view.wall_z);
            CAPTURE(x_ratio);
            CHECK(
                range_intersects_front_ray(
                    near_mesh,
                    near_mesh.metrics.structures,
                    {half_width *
                         x_ratio,
                     local_y,
                     view.inside_z},
                    {0.0F,
                     0.0F,
                     view.direction_z},
                    1.65F));
        }
    }
}

TEST_CASE("ship lantern lighting is isolated by decks and transverse bulkheads") {
    const std::array parts {
        ShipPart {
            ShipPartShape::Box,
            ShipMaterial::Lantern,
            {-0.15F, 2.90F, -2.15F},
            {0.15F, 3.26F, -1.85F},
            {0.0F, 0.0F, 1.0F},
            0.0F,
            false,
            false,
            U'\0',
        },
        ShipPart {
            ShipPartShape::Box,
            ShipMaterial::CleanBeam,
            {-3.0F, 1.00F, -0.11F},
            {3.0F, 3.65F, 0.11F},
            {0.0F, 0.0F, 1.0F},
            0.0F,
            true,
            false,
            U'\0',
        },
    };
    const auto lighting =
        ship_mesh_detail::make_lighting_context(
            parts);

    CHECK(
        lighting.block_light(
            {0.0F, 1.78F, -1.0F}) >
        0.55F);
    CHECK(
        lighting.block_light(
            {0.0F, 1.78F, 1.0F}) ==
        doctest::Approx(0.0F));
    CHECK(
        lighting.block_light(
            {0.0F, 0.18F, -2.0F}) ==
        doctest::Approx(0.0F));
}

TEST_CASE("stylized ship keeps interior textiles horizontal and reserves sail deformation for real sails") {
    const auto result =
        build_stylized_ship_mesh(
            amelie_ship_blueprint(),
            StylizedShipLod::Near);

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
                (
                    vertex.material_class ==
                        static_cast<float>(
                            ShipMaterial::CreamCanvas) ||
                    vertex.material_class ==
                        static_cast<float>(
                            ShipMaterial::Linen) ||
                    vertex.material_class ==
                        static_cast<float>(
                            ShipMaterial::BurgundyTextile) ||
                    vertex.material_class ==
                        static_cast<float>(
                            ShipMaterial::NavyTextile)
                )
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
                static_cast<float>(
                    ShipMaterial::BlackCanvas)) >
                0.01F ||
            std::abs(vertex.nz) <
                0.70F) {
            continue;
        }

        horizontal_coordinates.insert(
            static_cast<int>(
                std::lround(
                    vertex.u *
                    10'000.0F)));
        vertical_coordinates.insert(
            static_cast<int>(
                std::lround(
                    vertex.v *
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

TEST_CASE("stylized ship encodes exact append only material identifiers") {
    const auto result =
        build_stylized_ship_mesh(
            amelie_ship_blueprint(),
            StylizedShipLod::Near);
    REQUIRE_FALSE(
        result.empty());

    auto found_oiled_oak =
        false;
    auto found_linen =
        false;
    for (const auto& vertex :
         result.mesh.vertices) {
        const auto rounded =
            std::lround(
                vertex.material_class);
        CHECK(
            vertex.material_class ==
            doctest::Approx(
                static_cast<float>(
                    rounded)));
        CHECK(rounded >=
              static_cast<long>(
                  ShipMaterial::DarkHull));
        CHECK(rounded <=
              static_cast<long>(
                  ShipMaterial::Ceramic));
        found_oiled_oak =
            found_oiled_oak ||
            rounded ==
                static_cast<long>(
                    ShipMaterial::OiledOak);
        found_linen =
            found_linen ||
            rounded ==
                static_cast<long>(
                    ShipMaterial::Linen);
    }
    CHECK(found_oiled_oak);
    CHECK(found_linen);
}

TEST_CASE("ship material fallback stays exhaustive and shared by both mesh pipelines") {
    constexpr std::array materials {
        ShipMaterial::DarkHull,
        ShipMaterial::LightDeck,
        ShipMaterial::CleanBeam,
        ShipMaterial::CreamCanvas,
        ShipMaterial::Rope,
        ShipMaterial::Iron,
        ShipMaterial::Brass,
        ShipMaterial::Lantern,
        ShipMaterial::Glass,
        ShipMaterial::BlackCanvas,
        ShipMaterial::SolidGold,
        ShipMaterial::OiledOak,
        ShipMaterial::Linen,
        ShipMaterial::BurgundyTextile,
        ShipMaterial::NavyTextile,
        ShipMaterial::Leather,
        ShipMaterial::Paper,
        ShipMaterial::Ceramic,
    };
    constexpr std::array fallback_materials {
        ShipAtlasMaterial::DarkHull,
        ShipAtlasMaterial::LightDeck,
        ShipAtlasMaterial::CleanBeam,
        ShipAtlasMaterial::CreamCanvas,
        ShipAtlasMaterial::Rope,
        ShipAtlasMaterial::Iron,
        ShipAtlasMaterial::Brass,
        ShipAtlasMaterial::Lantern,
        ShipAtlasMaterial::DarkHull,
        ShipAtlasMaterial::BlackCanvas,
        ShipAtlasMaterial::SolidGold,
        ShipAtlasMaterial::CleanBeam,
        ShipAtlasMaterial::CreamCanvas,
        ShipAtlasMaterial::BlackCanvas,
        ShipAtlasMaterial::BlackCanvas,
        ShipAtlasMaterial::DarkHull,
        ShipAtlasMaterial::CreamCanvas,
        ShipAtlasMaterial::CreamCanvas,
    };
    static_assert(
        materials.size() ==
        static_cast<std::size_t>(
            ShipMaterial::Ceramic) +
            1U);
    static_assert(
        materials.size() ==
        fallback_materials.size());

    // Je parcours explicitement toutes les valeurs append-only et les six faces
    // afin que le Legacy et les conversions modernes gardent un repli unique.
    for (std::size_t material_index = 0U;
         material_index < materials.size();
         ++material_index) {
        for (const auto& face :
             ship_mesh_detail::kFaces) {
            const auto visual =
                ship_mesh_detail::material_visual(
                    materials[material_index],
                    face.visual_face);
            if (materials[material_index] ==
                ShipMaterial::Glass) {
                const auto glass =
                    to_block_id(
                        BlockType::Glass);
                CHECK(
                    visual.tile ==
                    block_atlas_tile(
                        glass,
                        face.visual_face));
                CHECK(
                    visual.material_class ==
                    doctest::Approx(
                        block_visual_material_value(
                            glass)));
            } else {
                const auto fallback =
                    fallback_materials[
                        material_index];
                CHECK(
                    visual.tile ==
                    ship_atlas_tile(
                        fallback));
                CHECK(
                    visual.material_class ==
                    doctest::Approx(
                        block_visual_material_value(
                            ship_visual_material(
                                fallback))));
            }
            CHECK(
                visual.emissive_light ==
                doctest::Approx(
                    materials[material_index] ==
                            ShipMaterial::Lantern
                        ? 11.0F / 15.0F
                        : 0.0F));
        }
    }
}

TEST_CASE("stylized furniture rounds boxes and omits draped cloth from far lod") {
    std::vector<ShipPart> parts {
        ShipPart {
            ShipPartShape::ChamferedBox,
            ShipMaterial::Ceramic,
            {-1.00F, 5.00F, -1.20F},
            {1.00F, 5.70F, 1.20F},
            {0.0F, 1.0F, 0.0F},
            0.0F,
            false,
            false,
            U'\0',
        },
        ShipPart {
            ShipPartShape::DrapedPanel,
            ShipMaterial::Paper,
            {-1.20F, 6.05F, -1.00F},
            {1.20F, 6.05F, 1.00F},
            {0.0F, 1.0F, 0.0F},
            0.06F,
            false,
            false,
            U'\0',
        },
    };
    auto blueprint =
        amelie_ship_blueprint();
    blueprint.parts =
        parts;
    blueprint.geometry_revision ^=
        0xC4A6EULL;

    const auto near_mesh =
        build_stylized_ship_mesh(
            blueprint,
            StylizedShipLod::Near);
    const auto far_mesh =
        build_stylized_ship_mesh(
            blueprint,
            StylizedShipLod::Far);
    REQUIRE_FALSE(
        near_mesh.empty());
    REQUIRE_FALSE(
        far_mesh.empty());

    const auto count_material_indices =
        [](const StylizedShipMeshData& mesh,
           ShipMaterial material) {
            auto count =
                std::size_t {0U};
            const auto end =
                range_end(
                    mesh.metrics.structures);
            for (auto index =
                     mesh.metrics.structures.first_index;
                 index < end;
                 ++index) {
                const auto& vertex =
                    mesh.mesh.vertices[
                        mesh.mesh.indices[index]];
                if (vertex.material_class ==
                    static_cast<float>(
                        material)) {
                    ++count;
                }
            }
            return count;
        };
    const auto near_ceramic =
        count_material_indices(
            near_mesh,
            ShipMaterial::Ceramic);
    const auto far_ceramic =
        count_material_indices(
            far_mesh,
            ShipMaterial::Ceramic);
    CHECK(near_ceramic >
          far_ceramic);
    CHECK(far_ceramic >
          0U);
    CHECK(
        count_material_indices(
            near_mesh,
            ShipMaterial::Paper) >
        0U);
    CHECK(
        count_material_indices(
            far_mesh,
            ShipMaterial::Paper) ==
        0U);
}

TEST_CASE("stylized furniture uses local physical UVs and keeps horizontal cloth in bounds") {
    std::vector<ShipPart> parts {
        ShipPart {
            ShipPartShape::ChamferedBox,
            ShipMaterial::Ceramic,
            {-4.50F, 5.00F, -1.50F},
            {-0.50F, 5.80F, 1.50F},
            {0.0F, 1.0F, 0.0F},
            0.0F,
            false,
            false,
            U'\0',
        },
        ShipPart {
            ShipPartShape::ChamferedBox,
            ShipMaterial::Leather,
            {0.50F, 5.00F, -1.50F},
            {4.50F, 5.80F, 1.50F},
            {0.0F, 1.0F, 0.0F},
            0.0F,
            false,
            false,
            U'\0',
        },
        ShipPart {
            ShipPartShape::ChamferedBox,
            ShipMaterial::OiledOak,
            {-0.50F, 5.00F, 4.60F},
            {0.50F, 5.40F, 5.40F},
            {0.0F, 1.0F, 0.0F},
            0.0F,
            false,
            false,
            U'\0',
        },
        ShipPart {
            ShipPartShape::DrapedPanel,
            ShipMaterial::Paper,
            {-2.00F, 6.00F, -8.00F},
            {2.00F, 6.00F, -5.00F},
            {0.0F, 1.0F, 0.0F},
            0.12F,
            false,
            false,
            U'\0',
        },
    };
    auto blueprint =
        amelie_ship_blueprint();
    blueprint.parts =
        parts;
    blueprint.geometry_revision ^=
        0x5A7E71A1ULL;

    const auto result =
        build_stylized_ship_mesh(
            blueprint,
            StylizedShipLod::Near);
    REQUIRE_FALSE(
        result.empty());

    const auto material_uvs =
        [&](ShipMaterial material) {
            std::vector<std::array<float, 2>>
                uvs;
            for (const auto& vertex :
                 result.mesh.vertices) {
                if (vertex.material_class ==
                    static_cast<float>(
                        material)) {
                    uvs.push_back(
                        {vertex.u, vertex.v});
                }
            }
            return uvs;
        };
    const auto maximum_uv =
        [](const std::vector<std::array<float, 2>>&
               uvs) {
            auto maximum =
                0.0F;
            for (const auto& uv :
                 uvs) {
                maximum =
                    std::max(
                        maximum,
                        std::max(
                            std::abs(uv[0]),
                            std::abs(uv[1])));
            }
            return maximum;
        };

    const auto large_uvs =
        material_uvs(
            ShipMaterial::Ceramic);
    const auto translated_uvs =
        material_uvs(
            ShipMaterial::Leather);
    const auto small_uvs =
        material_uvs(
            ShipMaterial::OiledOak);
    const auto cloth_uvs =
        material_uvs(
            ShipMaterial::Paper);
    REQUIRE_FALSE(
        large_uvs.empty());
    REQUIRE(
        translated_uvs.size() ==
        large_uvs.size());
    REQUIRE_FALSE(
        small_uvs.empty());
    REQUIRE_FALSE(
        cloth_uvs.empty());

    CHECK(
        maximum_uv(large_uvs) >
        2.50F);
    CHECK(
        maximum_uv(small_uvs) <
        1.10F);
    CHECK(
        maximum_uv(large_uvs) >
        maximum_uv(small_uvs) *
            2.0F);
    CHECK(
        maximum_uv(cloth_uvs) >
        2.50F);

    // Je vérifie que déplacer un meuble ne fait pas glisser sa texture locale.
    for (std::size_t index = 0U;
         index < large_uvs.size();
         ++index) {
        CHECK(
            translated_uvs[index][0] ==
            doctest::Approx(
                large_uvs[index][0]));
        CHECK(
            translated_uvs[index][1] ==
            doctest::Approx(
                large_uvs[index][1]));
    }

    auto boundary_top =
        -std::numeric_limits<float>::infinity();
    auto interior_top =
        -std::numeric_limits<float>::infinity();
    for (const auto& vertex :
         result.mesh.vertices) {
        if (vertex.material_class !=
            static_cast<float>(
                ShipMaterial::Paper)) {
            continue;
        }
        CHECK(vertex.y >=
              5.94F -
                  1.0e-5F);
        CHECK(vertex.y <=
              6.06F +
                  1.0e-5F);
        if (vertex.ny <= 0.50F) {
            continue;
        }
        CHECK(vertex.y >=
              6.00F -
                  1.0e-5F);
        const auto on_boundary =
            std::abs(
                std::abs(vertex.x) -
                2.00F) <=
                1.0e-4F ||
            std::abs(
                vertex.z -
                (-8.00F)) <=
                1.0e-4F ||
            std::abs(
                vertex.z -
                (-5.00F)) <=
                1.0e-4F;
        if (on_boundary) {
            boundary_top =
                std::max(
                    boundary_top,
                    vertex.y);
        } else {
            interior_top =
                std::max(
                    interior_top,
                    vertex.y);
        }
    }
    CHECK(
        boundary_top ==
        doctest::Approx(6.00F));
    CHECK(
        interior_top >
        boundary_top +
            0.02F);
}

TEST_CASE("near interior detail vertices stay inside their declared bounds and hull lining") {
    const auto& source_blueprint =
        amelie_ship_blueprint();
    const auto& profile =
        source_blueprint.protection_profile;
    std::vector<std::size_t>
        detail_indices;
    for (std::size_t index = 0U;
         index < source_blueprint.parts.size();
         ++index) {
        const auto& part =
            source_blueprint.parts[index];
        if (part.shape !=
                ShipPartShape::ChamferedBox &&
            part.shape !=
                ShipPartShape::DrapedPanel) {
            continue;
        }
        const auto bounds =
            ship_mesh_detail::render_bounds(
                part);
        const auto center =
            (bounds.min +
             bounds.max) *
            0.5F;
        const auto inside_closed_decks =
            bounds.min.y >=
                profile.sheltered_floor_y -
                    1.0e-4F &&
            bounds.max.y <=
                profile.main_deck_top_y -
                    0.05F &&
            bounds.min.z >=
                profile.stern_z &&
            bounds.max.z <=
                profile.bow_z &&
            std::abs(center.x) <=
                interior_profile_half_width(
                    profile,
                    center.y,
                    center.z) +
                    1.0e-4F;
        if (inside_closed_decks) {
            detail_indices.push_back(
                index);
        }
    }
    REQUIRE_FALSE(
        detail_indices.empty());

    // Je réserve DarkHull à la doublure générée et j'encode jusqu'à dix-sept
    // détails par lot. Je peux ainsi rattacher chaque sommet à sa pièce exacte
    // sans ajouter de métadonnée au format de sommet du monde.
    constexpr auto details_per_batch =
        static_cast<std::size_t>(
            ShipMaterial::Ceramic);
    for (std::size_t batch_start = 0U;
         batch_start <
         detail_indices.size();
         batch_start +=
             details_per_batch) {
        const auto batch_count =
            std::min(
                details_per_batch,
                detail_indices.size() -
                    batch_start);
        std::vector<ShipPart>
            isolated_parts;
        isolated_parts.reserve(
            source_blueprint.parts.size());
        for (const auto& source_part :
             source_blueprint.parts) {
            const auto supporting_deck =
                source_part.supports_player &&
                source_part.material ==
                    ShipMaterial::LightDeck &&
                ship_mesh_detail::is_volume_shape(
                    source_part.shape);
            if (supporting_deck ||
                source_part.shape ==
                    ShipPartShape::Opening) {
                isolated_parts.push_back(
                    source_part);
                continue;
            }
            if (source_part.shape ==
                    ShipPartShape::Panel &&
                source_part.material ==
                    ShipMaterial::Glass) {
                auto opening =
                    source_part;
                opening.shape =
                    ShipPartShape::Opening;
                opening.material =
                    ShipMaterial::DarkHull;
                opening.collidable =
                    false;
                opening.supports_player =
                    false;
                isolated_parts.push_back(
                    opening);
            }
        }
        for (std::size_t slot = 0U;
             slot < batch_count;
             ++slot) {
            auto detail =
                source_blueprint.parts[
                    detail_indices[
                        batch_start +
                        slot]];
            detail.material =
                static_cast<ShipMaterial>(
                    slot + 1U);
            isolated_parts.push_back(
                detail);
        }

        auto isolated_blueprint =
            source_blueprint;
        isolated_blueprint.parts =
            isolated_parts;
        isolated_blueprint.geometry_revision ^=
            0xB0A0D500ULL +
            batch_start;
        const auto result =
            build_stylized_ship_mesh(
                isolated_blueprint,
                StylizedShipLod::Near);
        REQUIRE_FALSE(
            result.empty());

        const auto structure_end =
            range_end(
                result.metrics.structures);
        for (std::size_t slot = 0U;
             slot < batch_count;
             ++slot) {
            const auto source_index =
                detail_indices[
                    batch_start +
                    slot];
            const auto& source_part =
                source_blueprint.parts[
                    source_index];
            const auto bounds =
                ship_mesh_detail::render_bounds(
                    source_part);
            const auto encoded_material =
                static_cast<float>(
                    slot + 1U);
            auto vertex_count =
                std::size_t {0U};
            for (auto index =
                     result.metrics.structures
                         .first_index;
                 index < structure_end;
                 ++index) {
                const auto& vertex =
                    result.mesh.vertices[
                        result.mesh.indices[
                            index]];
                if (vertex.material_class !=
                    encoded_material) {
                    continue;
                }
                ++vertex_count;
                CAPTURE(source_index);
                CAPTURE(source_part.shape);
                CAPTURE(vertex.x);
                CAPTURE(vertex.y);
                CAPTURE(vertex.z);
                CHECK(vertex.x >=
                      bounds.min.x -
                          1.0e-4F);
                CHECK(vertex.x <=
                      bounds.max.x +
                          1.0e-4F);
                CHECK(vertex.y >=
                      bounds.min.y -
                          1.0e-4F);
                CHECK(vertex.y <=
                      bounds.max.y +
                          1.0e-4F);
                CHECK(vertex.z >=
                      bounds.min.z -
                          1.0e-4F);
                CHECK(vertex.z <=
                      bounds.max.z +
                          1.0e-4F);
                CHECK(
                    std::abs(vertex.x) <=
                    interior_profile_half_width(
                        profile,
                        vertex.y,
                        vertex.z) +
                        1.0e-4F);
            }
            CHECK(vertex_count >
                  0U);
        }
    }
}

TEST_CASE("stylized ship refit stays inside the near and far geometry budgets") {
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

    // Je verrouille les plafonds du plan afin qu'un futur accessoire ne
    // réintroduise pas silencieusement une régression CPU/GPU.
    CHECK(near_mesh.mesh.vertices.size() <=
          510'000U);
    CHECK(near_mesh.mesh.indices.size() <=
          545'000U);
    CHECK(near_mesh.mesh.indices.size() /
              3U <=
          182'000U);
    CHECK(far_mesh.mesh.vertices.size() <=
          130'000U);
    CHECK(far_mesh.mesh.indices.size() <=
          145'000U);
    CHECK(far_mesh.mesh.indices.size() /
              3U <=
          49'000U);
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
