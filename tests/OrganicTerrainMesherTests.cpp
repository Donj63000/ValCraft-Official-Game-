#include "world/OrganicTerrainMesher.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <tuple>
#include <vector>

namespace valcraft {

namespace {

[[nodiscard]] auto make_sample(BlockId block_id,
                               std::uint8_t sky_light = 15,
                               std::uint8_t block_light = 0) -> OrganicTerrainCellSample {
    return {block_id, sky_light, block_light};
}

[[nodiscard]] auto position_of(const TerrainVertex& vertex) -> std::array<float, 3> {
    return {vertex.x, vertex.y, vertex.z};
}

[[nodiscard]] auto subtract(const std::array<float, 3>& lhs,
                            const std::array<float, 3>& rhs) -> std::array<float, 3> {
    return {
        lhs[0] - rhs[0],
        lhs[1] - rhs[1],
        lhs[2] - rhs[2],
    };
}

[[nodiscard]] auto cross(const std::array<float, 3>& lhs,
                         const std::array<float, 3>& rhs) -> std::array<float, 3> {
    return {
        lhs[1] * rhs[2] - lhs[2] * rhs[1],
        lhs[2] * rhs[0] - lhs[0] * rhs[2],
        lhs[0] * rhs[1] - lhs[1] * rhs[0],
    };
}

[[nodiscard]] auto length_squared(const std::array<float, 3>& value) -> float {
    return value[0] * value[0] + value[1] * value[1] + value[2] * value[2];
}

[[nodiscard]] auto dot(const std::array<float, 3>& lhs,
                       const std::array<float, 3>& rhs) -> float {
    return lhs[0] * rhs[0] + lhs[1] * rhs[1] + lhs[2] * rhs[2];
}

void check_mesh_integrity(const OrganicTerrainMesh& mesh) {
    REQUIRE(mesh.indices.size() % 3U == 0U);
    REQUIRE(mesh.indices.size() >= mesh.quad_count * 6U);

    for (const auto& vertex : mesh.vertices) {
        CHECK(std::isfinite(vertex.x));
        CHECK(std::isfinite(vertex.y));
        CHECK(std::isfinite(vertex.z));
        CHECK(std::isfinite(vertex.nx));
        CHECK(std::isfinite(vertex.ny));
        CHECK(std::isfinite(vertex.nz));
        const auto normal_length = std::sqrt(
            vertex.nx * vertex.nx +
            vertex.ny * vertex.ny +
            vertex.nz * vertex.nz);
        CHECK(normal_length == doctest::Approx(1.0F).epsilon(0.00001));
        CHECK(vertex.sky_light <= 15);
        CHECK(vertex.block_light <= 15);
    }

    for (std::size_t triangle = 0; triangle < mesh.indices.size(); triangle += 3U) {
        const auto index_a = mesh.indices[triangle];
        const auto index_b = mesh.indices[triangle + 1U];
        const auto index_c = mesh.indices[triangle + 2U];
        REQUIRE(index_a < mesh.vertices.size());
        REQUIRE(index_b < mesh.vertices.size());
        REQUIRE(index_c < mesh.vertices.size());
        const auto edge_ab = subtract(
            position_of(mesh.vertices[index_b]),
            position_of(mesh.vertices[index_a]));
        const auto edge_ac = subtract(
            position_of(mesh.vertices[index_c]),
            position_of(mesh.vertices[index_a]));
        CHECK(length_squared(cross(edge_ab, edge_ac)) > 1.0e-12F);
    }
}

[[nodiscard]] auto seam_vertices_at_x(const OrganicTerrainMesh& mesh, float seam_x)
    -> std::vector<TerrainVertex> {
    std::vector<TerrainVertex> result {};
    for (const auto& vertex : mesh.vertices) {
        if (std::abs(vertex.x - seam_x) <= 0.00001F) {
            result.push_back(vertex);
        }
    }
    std::sort(result.begin(), result.end(), [](const TerrainVertex& lhs, const TerrainVertex& rhs) {
        return std::tie(lhs.y, lhs.z, lhs.primary_block_id, lhs.secondary_block_id) <
               std::tie(rhs.y, rhs.z, rhs.primary_block_id, rhs.secondary_block_id);
    });
    return result;
}

[[nodiscard]] auto seam_vertices_near_x(const OrganicTerrainMesh& mesh, float seam_center)
    -> std::vector<TerrainVertex> {
    std::vector<TerrainVertex> result {};
    for (const auto& vertex : mesh.vertices) {
        // Le déplacement maximal vaut 0,35 : cette bande isole donc exactement
        // les sommets de la cellule duale partagée par les deux sections.
        if (std::abs(vertex.x - seam_center) <= 0.351F) {
            result.push_back(vertex);
        }
    }
    std::sort(result.begin(), result.end(), [](const TerrainVertex& lhs, const TerrainVertex& rhs) {
        return std::tie(lhs.x, lhs.y, lhs.z, lhs.primary_block_id, lhs.secondary_block_id) <
               std::tie(rhs.x, rhs.y, rhs.z, rhs.primary_block_id, rhs.secondary_block_id);
    });
    return result;
}

[[nodiscard]] auto normal_of(const TerrainVertex& vertex) -> std::array<float, 3> {
    return {vertex.nx, vertex.ny, vertex.nz};
}

} // namespace

TEST_CASE("organic terrain surface nets build a shared flat surface") {
    const OrganicTerrainSection section {{0, 0, 0}, {3, 0, 3}};
    const auto grass = to_block_id(BlockType::Grass);
    const OrganicTerrainSampler sampler = [grass](int, int y, int) {
        return make_sample(y <= 0 ? grass : to_block_id(BlockType::Air));
    };

    const OrganicTerrainMesher mesher {
        OrganicTerrainMesherSettings {
            .maximum_vertex_displacement = 0.42F,
            .exposed_surface_relaxation = 0.26F,
            .adaptive_lip_refinement = false,
        },
    };
    const auto mesh = mesher.build_mesh(section, sampler);

    REQUIRE(mesh.quad_count == 16U);
    REQUIRE(mesh.vertices.size() == 25U);
    REQUIRE(mesh.indices.size() == 96U);
    for (const auto& vertex : mesh.vertices) {
        CHECK(vertex.y == doctest::Approx(1.0F));
        CHECK(vertex.nx == doctest::Approx(0.0F));
        CHECK(vertex.ny == doctest::Approx(1.0F));
        CHECK(vertex.nz == doctest::Approx(0.0F));
        CHECK(vertex.primary_block_id == grass);
        CHECK(vertex.secondary_block_id == to_block_id(BlockType::Stone));
        CHECK(vertex.material_blend <= 34U);
        CHECK(
            (vertex.surface_flags &
             kTerrainSurfaceFlagGeologicalBlend) != 0U);
    }
    check_mesh_integrity(mesh);
}

TEST_CASE("organic terrain quads keep one flat material across both triangles") {
    const auto grass = to_block_id(BlockType::Grass);
    const auto dirt = to_block_id(BlockType::Dirt);
    const OrganicTerrainSection section {{0, 0, 0}, {4, 2, 4}};
    const OrganicTerrainSampler sampler = [grass, dirt](int x, int y, int z) {
        const auto surface = (x * 7 + z * 11) % 3;
        if (y > surface) {
            return make_sample(to_block_id(BlockType::Air));
        }
        return make_sample(y == surface ? grass : dirt);
    };

    const OrganicTerrainMesher mesher {
        OrganicTerrainMesherSettings {
            .maximum_vertex_displacement = 0.42F,
            .exposed_surface_relaxation = 0.26F,
            .adaptive_lip_refinement = false,
        },
    };
    const auto mesh = mesher.build_mesh(section, sampler);

    REQUIRE_FALSE(mesh.empty());
    REQUIRE(mesh.indices.size() == mesh.quad_count * 6U);
    for (std::size_t quad = 0U; quad < mesh.quad_count; ++quad) {
        const auto first_triangle_provoking =
            mesh.indices[quad * 6U + 2U];
        const auto second_triangle_provoking =
            mesh.indices[quad * 6U + 5U];
        CAPTURE(quad);
        REQUIRE(first_triangle_provoking < mesh.vertices.size());
        REQUIRE(second_triangle_provoking < mesh.vertices.size());

        // OpenGL emploie par défaut le dernier sommet comme sommet provocateur.
        // Je garantis donc que les attributs de matériau `flat` restent les
        // mêmes sur les deux moitiés d'un quad.
        CHECK(first_triangle_provoking == second_triangle_provoking);
        CHECK(mesh.vertices[first_triangle_provoking].primary_block_id ==
              mesh.vertices[second_triangle_provoking].primary_block_id);
        CHECK(mesh.vertices[first_triangle_provoking].secondary_block_id ==
              mesh.vertices[second_triangle_provoking].secondary_block_id);

        const auto triangle_normal = [&mesh, quad](std::size_t triangle_offset) {
            const auto base = quad * 6U + triangle_offset;
            const auto point_a = position_of(
                mesh.vertices[mesh.indices[base]]);
            const auto point_b = position_of(
                mesh.vertices[mesh.indices[base + 1U]]);
            const auto point_c = position_of(
                mesh.vertices[mesh.indices[base + 2U]]);
            return cross(
                subtract(point_b, point_a),
                subtract(point_c, point_a));
        };
        const auto first_normal = triangle_normal(0U);
        const auto second_normal = triangle_normal(3U);

        // La permutation du second triangle est cyclique : je vérifie qu'elle
        // préserve bien son orientation et ne retourne aucune moitié de quad.
        CHECK(dot(first_normal, second_normal) > 0.0F);
    }
    check_mesh_integrity(mesh);
}

TEST_CASE("organic terrain surface nets round a removed cell and a tunnel") {
    const auto stone = to_block_id(BlockType::Stone);
    const auto air = to_block_id(BlockType::Air);
    const OrganicTerrainSection cavity_section {{-1, -1, -1}, {1, 1, 1}};
    const OrganicTerrainSampler cavity_sampler = [stone, air](int x, int y, int z) {
        return make_sample(x == 0 && y == 0 && z == 0 ? air : stone, 0, 3);
    };

    const auto cavity = OrganicTerrainMesher {}.build_mesh(cavity_section, cavity_sampler);
    REQUIRE(cavity.quad_count == 6U);
    REQUIRE(cavity.vertices.size() == 8U);
    check_mesh_integrity(cavity);
    for (const auto& vertex : cavity.vertices) {
        CHECK(vertex.x > 0.0F);
        CHECK(vertex.x < 1.0F);
        CHECK(vertex.y > 0.0F);
        CHECK(vertex.y < 1.0F);
        CHECK(vertex.z > 0.0F);
        CHECK(vertex.z < 1.0F);
        CHECK(vertex.primary_block_id == to_block_id(BlockType::Grass));
        CHECK(vertex.secondary_block_id == stone);
        CHECK(
            (vertex.surface_flags &
             kTerrainSurfaceFlagGeologicalBlend) != 0U);
        CHECK(vertex.block_light == 3);
    }

    const OrganicTerrainSection tunnel_section {{-2, -1, -1}, {2, 1, 1}};
    const OrganicTerrainSampler tunnel_sampler = [stone, air](int, int y, int z) {
        return make_sample(y == 0 && z == 0 ? air : stone);
    };
    const auto tunnel = OrganicTerrainMesher {}.build_mesh(tunnel_section, tunnel_sampler);
    REQUIRE(tunnel.quad_count == 20U);
    REQUIRE(tunnel.vertices.size() == 24U);
    check_mesh_integrity(tunnel);
}

TEST_CASE("organic terrain surface nets remain exactly deterministic") {
    const OrganicTerrainSection section {{-3, -2, 4}, {2, 2, 9}};
    const OrganicTerrainSampler sampler = [](int x, int y, int z) {
        const auto is_air = y > ((x * 13 + z * 7) % 3) - 1 ||
                            (x == -1 && z == 6 && y == -1);
        const auto block = is_air
                               ? to_block_id(BlockType::Air)
                               : ((x + z) % 2 == 0
                                      ? to_block_id(BlockType::Dirt)
                                      : to_block_id(BlockType::Stone));
        return make_sample(
            block,
            static_cast<std::uint8_t>((x - y + z) & 0x0F),
            static_cast<std::uint8_t>((x + y - z) & 0x07));
    };
    const OrganicTerrainMesher mesher {};

    const auto first = mesher.build_mesh(section, sampler);
    const auto second = mesher.build_mesh(section, sampler);

    REQUIRE_FALSE(first.empty());
    CHECK(first == second);
    check_mesh_integrity(first);
}

TEST_CASE("organic terrain surface nets constrain isolated geometry to its logical block") {
    const auto stone = to_block_id(BlockType::Stone);
    const OrganicTerrainSection section {{0, 0, 0}, {0, 0, 0}};
    const OrganicTerrainSampler sampler = [stone](int x, int y, int z) {
        return make_sample(
            x == 0 && y == 0 && z == 0 ? stone : to_block_id(BlockType::Air),
            15,
            31);
    };

    const auto mesh = OrganicTerrainMesher {}.build_mesh(section, sampler);

    REQUIRE(mesh.quad_count == 6U);
    REQUIRE(mesh.vertices.size() == 8U);
    check_mesh_integrity(mesh);
    for (const auto& vertex : mesh.vertices) {
        CHECK(vertex.x >= 0.0F);
        CHECK(vertex.x <= 1.0F);
        CHECK(vertex.y >= 0.0F);
        CHECK(vertex.y <= 1.0F);
        CHECK(vertex.z >= 0.0F);
        CHECK(vertex.z <= 1.0F);
        CHECK(vertex.primary_block_id == to_block_id(BlockType::Grass));
        CHECK(vertex.secondary_block_id == stone);
        CHECK(
            (vertex.surface_flags &
             kTerrainSurfaceFlagGeologicalBlend) != 0U);
        CHECK(vertex.block_light == 15);
    }
}

TEST_CASE("organic terrain adjacent negative sections agree exactly on their seam") {
    const auto dirt = to_block_id(BlockType::Dirt);
    const OrganicTerrainSampler sampler = [dirt](int x, int y, int z) {
        const auto sky = static_cast<std::uint8_t>((x - y + z) & 0x0F);
        return make_sample(y <= -2 ? dirt : to_block_id(BlockType::Air), sky, 0);
    };
    const OrganicTerrainMesher mesher {};
    const auto left = mesher.build_mesh({{-4, -2, -3}, {-1, -2, 0}}, sampler);
    const auto right = mesher.build_mesh({{0, -2, -3}, {3, -2, 0}}, sampler);

    check_mesh_integrity(left);
    check_mesh_integrity(right);
    const auto left_seam = seam_vertices_at_x(left, 0.0F);
    const auto right_seam = seam_vertices_at_x(right, 0.0F);
    REQUIRE(left_seam.size() == 5U);
    CHECK(left_seam == right_seam);
}

TEST_CASE("organic terrain cubic normals remain continuous over a stepped slope") {
    const auto stone = to_block_id(BlockType::Stone);
    const OrganicTerrainSampler sampler = [stone](int x, int y, int z) {
        return make_sample(
            3 * y <= x + z ? stone : to_block_id(BlockType::Air));
    };
    const auto mesh = OrganicTerrainMesher {}.build_mesh(
        {{-8, -8, -8}, {8, 8, 8}},
        sampler);

    REQUIRE_FALSE(mesh.empty());
    check_mesh_integrity(mesh);
    for (std::size_t triangle = 0U;
         triangle < mesh.indices.size();
         triangle += 3U) {
        const auto& first = mesh.vertices[mesh.indices[triangle]];
        const auto& second = mesh.vertices[mesh.indices[triangle + 1U]];
        const auto& third = mesh.vertices[mesh.indices[triangle + 2U]];
        CAPTURE(triangle / 3U);
        // La dérivée B-spline est continue : même sur la marche logique, deux
        // sommets voisins ne doivent plus produire une rupture lumineuse.
        CHECK(dot(normal_of(first), normal_of(second)) > 0.82F);
        CHECK(dot(normal_of(second), normal_of(third)) > 0.82F);
        CHECK(dot(normal_of(third), normal_of(first)) > 0.82F);
    }
}

TEST_CASE("organic terrain curved normals match exactly across a negative section seam") {
    const auto dirt = to_block_id(BlockType::Dirt);
    const OrganicTerrainSampler sampler = [dirt](int x, int y, int z) {
        const auto sky = static_cast<std::uint8_t>((x - 2 * y + z) & 0x0F);
        return make_sample(
            3 * y <= x + z ? dirt : to_block_id(BlockType::Air),
            sky);
    };
    const OrganicTerrainMesher mesher {};
    const auto left = mesher.build_mesh(
        {{-12, -8, -6}, {-9, 2, 6}},
        sampler);
    const auto right = mesher.build_mesh(
        {{-8, -8, -6}, {-5, 2, 6}},
        sampler);

    check_mesh_integrity(left);
    check_mesh_integrity(right);
    const auto left_seam = seam_vertices_near_x(left, -8.0F);
    const auto right_seam = seam_vertices_near_x(right, -8.0F);
    auto matched_vertices = std::size_t {0U};
    for (const auto& left_vertex : left_seam) {
        const auto matching_right = std::find_if(
            right_seam.begin(),
            right_seam.end(),
            [&left_vertex](const TerrainVertex& right_vertex) {
                return left_vertex.x == right_vertex.x &&
                       left_vertex.y == right_vertex.y &&
                       left_vertex.z == right_vertex.z;
            });
        if (matching_right == right_seam.end()) {
            continue;
        }
        ++matched_vertices;
        CHECK(left_vertex == *matching_right);
    }
    // Les faces verticales de la marche appartiennent volontairement à une
    // seule section. Je compare donc tous les sommets réellement dupliqués,
    // ceux qui doivent être rigoureusement identiques côté GPU.
    CHECK(matched_vertices >= 10U);
}

TEST_CASE("organic terrain visual halo cannot change topology or isolated cell positions") {
    const auto stone = to_block_id(BlockType::Stone);
    const auto base_sampler = [stone](int x, int y, int z) {
        return make_sample(
            x == 0 && y == 0 && z == 0
                ? stone
                : to_block_id(BlockType::Air));
    };
    const auto altered_normal_halo_sampler = [stone](int x, int y, int z) {
        const auto belongs_to_normal_halo =
            std::abs(x) == 2 ||
            std::abs(y) == 2 ||
            std::abs(z) == 2;
        return make_sample(
            (x == 0 && y == 0 && z == 0) ||
                    belongs_to_normal_halo
                ? stone
                : to_block_id(BlockType::Air));
    };
    const OrganicTerrainSection section {{0, 0, 0}, {0, 0, 0}};
    const OrganicTerrainMesher mesher {};
    const auto base = mesher.build_mesh(section, base_sampler);
    const auto altered = mesher.build_mesh(
        section,
        altered_normal_halo_sampler);

    REQUIRE(base.vertices.size() == altered.vertices.size());
    CHECK(base.indices == altered.indices);
    CHECK(base.quad_count == altered.quad_count);
    for (std::size_t index = 0U; index < base.vertices.size(); ++index) {
        const auto& lhs = base.vertices[index];
        const auto& rhs = altered.vertices[index];
        CAPTURE(index);
        CHECK(lhs.x == rhs.x);
        CHECK(lhs.y == rhs.y);
        CHECK(lhs.z == rhs.z);
        CHECK(lhs.primary_block_id == rhs.primary_block_id);
        CHECK(lhs.secondary_block_id == rhs.secondary_block_id);
        CHECK(lhs.material_blend == rhs.material_blend);
        CHECK(lhs.surface_flags == rhs.surface_flags);
    }
}

TEST_CASE("organic terrain relaxation widens an exposed height transition without changing indices") {
    const auto grass = to_block_id(BlockType::Grass);
    const OrganicTerrainSampler sampler = [grass](int x, int y, int) {
        const auto surface_height = x < 0 ? 0 : 1;
        return make_sample(
            y <= surface_height ? grass : to_block_id(BlockType::Air));
    };
    const OrganicTerrainSection section {{-4, -2, -1}, {3, 2, 1}};
    const OrganicTerrainMesher unrelaxed_mesher {
        OrganicTerrainMesherSettings {
            .maximum_vertex_displacement = 0.42F,
            .exposed_surface_relaxation = 0.0F,
            .adaptive_lip_refinement = false,
        },
    };
    const OrganicTerrainMesher relaxed_mesher {
        OrganicTerrainMesherSettings {
            .maximum_vertex_displacement = 0.42F,
            .exposed_surface_relaxation = 0.26F,
            .adaptive_lip_refinement = false,
        },
    };
    const auto unrelaxed = unrelaxed_mesher.build_mesh(section, sampler);
    const auto relaxed = relaxed_mesher.build_mesh(section, sampler);

    REQUIRE_FALSE(unrelaxed.empty());
    REQUIRE(unrelaxed.vertices.size() == relaxed.vertices.size());
    CHECK(unrelaxed.indices == relaxed.indices);
    CHECK(unrelaxed.quad_count == relaxed.quad_count);

    auto moved_vertex_count = std::size_t {0U};
    auto moved_neighbor_count = std::size_t {0U};
    auto updated_normal_count = std::size_t {0U};
    auto lower_transition = unrelaxed.vertices.size();
    auto upper_transition = unrelaxed.vertices.size();
    for (std::size_t index = 0U; index < unrelaxed.vertices.size(); ++index) {
        const auto& before = unrelaxed.vertices[index];
        const auto& after = relaxed.vertices[index];
        const std::array<float, 3> correction {
            after.x - before.x,
            after.y - before.y,
            after.z - before.z,
        };
        const auto correction_length = std::sqrt(length_squared(correction));
        CAPTURE(index);
        CHECK(correction_length <= 0.26001F);
        CHECK(std::abs(after.x - std::round(before.x)) <= 0.42001F);
        CHECK(std::abs(after.y - std::round(before.y)) <= 0.42001F);
        CHECK(std::abs(after.z - std::round(before.z)) <= 0.42001F);
        if (correction_length > 0.00001F) {
            ++moved_vertex_count;
            if (std::abs(before.x) > 0.5F) {
                ++moved_neighbor_count;
            }

            // Une position relaxée doit posséder la normale du champ au point
            // final, pas celle calculée avant son déplacement.
            if (dot(
                    normal_of(before),
                    normal_of(after)) < 0.999999F) {
                ++updated_normal_count;
            }
        }

        if (std::abs(before.z - 1.0F) > 0.00001F) {
            continue;
        }
        if (before.x < 0.0F &&
            before.y > 1.05F &&
            before.y < 1.45F) {
            lower_transition = index;
        } else if (before.x > 0.0F &&
                   before.y > 1.55F &&
                   before.y < 1.95F) {
            upper_transition = index;
        }
    }

    REQUIRE(lower_transition < unrelaxed.vertices.size());
    REQUIRE(upper_transition < unrelaxed.vertices.size());
    const auto baseline_horizontal_span =
        unrelaxed.vertices[upper_transition].x -
        unrelaxed.vertices[lower_transition].x;
    const auto baseline_height_step =
        unrelaxed.vertices[upper_transition].y -
        unrelaxed.vertices[lower_transition].y;
    const auto relaxed_horizontal_span =
        relaxed.vertices[upper_transition].x -
        relaxed.vertices[lower_transition].x;
    const auto relaxed_height_step =
        relaxed.vertices[upper_transition].y -
        relaxed.vertices[lower_transition].y;
    const auto baseline_angle =
        std::atan2(baseline_height_step, baseline_horizontal_span);
    const auto relaxed_angle =
        std::atan2(relaxed_height_step, relaxed_horizontal_span);

    CHECK(moved_vertex_count >= 8U);
    CHECK(moved_neighbor_count >= 4U);
    CHECK(updated_normal_count > 0U);
    CHECK(relaxed_horizontal_span > baseline_horizontal_span * 1.24F);
    CHECK(relaxed_height_step < baseline_height_step * 0.80F);
    CHECK(relaxed_angle < baseline_angle - 0.27F);
    check_mesh_integrity(relaxed);
}

TEST_CASE("organic terrain relaxation preserves tunnels ceilings and vertical walls") {
    const auto stone = to_block_id(BlockType::Stone);
    const auto air = to_block_id(BlockType::Air);
    const OrganicTerrainMesher unrelaxed_mesher {
        OrganicTerrainMesherSettings {
            .maximum_vertex_displacement = 0.42F,
            .exposed_surface_relaxation = 0.0F,
            .adaptive_lip_refinement = false,
        },
    };
    const OrganicTerrainMesher relaxed_mesher {
        OrganicTerrainMesherSettings {
            .maximum_vertex_displacement = 0.42F,
            .exposed_surface_relaxation = 0.26F,
            .adaptive_lip_refinement = false,
        },
    };

    const OrganicTerrainSampler targeted_cell_sampler =
        [stone, air](int x, int y, int z) {
            return make_sample(
                x == 0 && y == 0 && z == 0 ? air : stone);
        };
    const OrganicTerrainSection targeted_cell_section {
        {-1, -1, -1},
        {1, 1, 1},
    };
    const auto targeted_cell_before = unrelaxed_mesher.build_mesh(
        targeted_cell_section,
        targeted_cell_sampler);
    const auto targeted_cell_after = relaxed_mesher.build_mesh(
        targeted_cell_section,
        targeted_cell_sampler);
    REQUIRE_FALSE(targeted_cell_before.empty());
    CHECK(targeted_cell_before == targeted_cell_after);

    const OrganicTerrainSampler tunnel_sampler = [stone, air](int, int y, int z) {
        return make_sample(y == 0 && z == 0 ? air : stone);
    };
    const OrganicTerrainSection tunnel_section {{-2, -1, -1}, {2, 1, 1}};
    const auto tunnel_before =
        unrelaxed_mesher.build_mesh(tunnel_section, tunnel_sampler);
    const auto tunnel_after =
        relaxed_mesher.build_mesh(tunnel_section, tunnel_sampler);
    REQUIRE_FALSE(tunnel_before.empty());
    CHECK(tunnel_before == tunnel_after);

    const OrganicTerrainSampler ceiling_sampler = [stone, air](int, int y, int) {
        return make_sample(y >= 1 ? stone : air);
    };
    const OrganicTerrainSection ceiling_section {{-2, 1, -2}, {2, 1, 2}};
    const auto ceiling_before =
        unrelaxed_mesher.build_mesh(ceiling_section, ceiling_sampler);
    const auto ceiling_after =
        relaxed_mesher.build_mesh(ceiling_section, ceiling_sampler);
    REQUIRE_FALSE(ceiling_before.empty());
    CHECK(ceiling_before == ceiling_after);

    const OrganicTerrainSampler wall_sampler = [stone, air](int x, int, int) {
        return make_sample(x <= 0 ? stone : air);
    };
    const OrganicTerrainSection wall_section {{0, -2, -2}, {0, 2, 2}};
    const auto wall_before =
        unrelaxed_mesher.build_mesh(wall_section, wall_sampler);
    const auto wall_after =
        relaxed_mesher.build_mesh(wall_section, wall_sampler);
    REQUIRE_FALSE(wall_before.empty());
    CHECK(wall_before == wall_after);
}

TEST_CASE("organic terrain relaxation preserves every local binary topology") {
    const auto stone = to_block_id(BlockType::Stone);
    const auto air = to_block_id(BlockType::Air);
    const OrganicTerrainSection section {{0, 0, 0}, {1, 1, 1}};
    const OrganicTerrainMesher unrelaxed_mesher {
        OrganicTerrainMesherSettings {
            .maximum_vertex_displacement = 0.42F,
            .exposed_surface_relaxation = 0.0F,
            .adaptive_lip_refinement = false,
        },
    };
    const OrganicTerrainMesher relaxed_mesher {
        OrganicTerrainMesherSettings {
            .maximum_vertex_displacement = 0.42F,
            .exposed_surface_relaxation = 0.26F,
            .adaptive_lip_refinement = false,
        },
    };

    for (std::uint32_t mask = 1U; mask < 0xFFU; ++mask) {
        CAPTURE(mask);
        const OrganicTerrainSampler sampler =
            [stone, air, mask](int x, int y, int z) {
                if (x < 0 || x > 1 ||
                    y < 0 || y > 1 ||
                    z < 0 || z > 1) {
                    return make_sample(air);
                }
                const auto bit = static_cast<unsigned int>(
                    x + 2 * (y + 2 * z));
                return make_sample(
                    (mask & (1U << bit)) != 0U ? stone : air);
            };
        const auto unrelaxed =
            unrelaxed_mesher.build_mesh(section, sampler);
        const auto relaxed =
            relaxed_mesher.build_mesh(section, sampler);

        REQUIRE(unrelaxed.vertices.size() == relaxed.vertices.size());
        CHECK(unrelaxed.indices == relaxed.indices);
        CHECK(unrelaxed.quad_count == relaxed.quad_count);
        check_mesh_integrity(relaxed);
        for (std::size_t index = 0U; index < relaxed.vertices.size(); ++index) {
            const auto correction = subtract(
                position_of(relaxed.vertices[index]),
                position_of(unrelaxed.vertices[index]));
            CHECK(length_squared(correction) <= 0.26001F * 0.26001F);
        }
        for (std::size_t triangle = 0U;
             triangle < unrelaxed.indices.size();
             triangle += 3U) {
            const auto triangle_normal = [](const OrganicTerrainMesh& mesh,
                                            std::size_t offset) {
                const auto point_a =
                    position_of(mesh.vertices[mesh.indices[offset]]);
                const auto point_b =
                    position_of(mesh.vertices[mesh.indices[offset + 1U]]);
                const auto point_c =
                    position_of(mesh.vertices[mesh.indices[offset + 2U]]);
                return cross(
                    subtract(point_b, point_a),
                    subtract(point_c, point_a));
            };
            const auto before_normal =
                triangle_normal(unrelaxed, triangle);
            const auto after_normal =
                triangle_normal(relaxed, triangle);
            CHECK(dot(before_normal, after_normal) > 0.0F);
        }
    }
}

TEST_CASE("organic terrain relaxation softens a crenellated exposed cliff lip") {
    const auto stone = to_block_id(BlockType::Stone);
    const OrganicTerrainSampler sampler = [stone](int x, int y, int z) {
        const auto lip_phase = z & 3;
        const auto cliff_limit_x = lip_phase < 2 ? 0 : 1;
        return make_sample(
            x <= cliff_limit_x && y <= 4
                ? stone
                : to_block_id(BlockType::Air));
    };
    const OrganicTerrainSection section {{-2, 0, -6}, {1, 4, 6}};
    const OrganicTerrainMesher unrelaxed_mesher {
        OrganicTerrainMesherSettings {
            .maximum_vertex_displacement = 0.42F,
            .exposed_surface_relaxation = 0.0F,
            .adaptive_lip_refinement = false,
        },
    };
    const OrganicTerrainMesher relaxed_mesher {
        OrganicTerrainMesherSettings {
            .maximum_vertex_displacement = 0.42F,
            .exposed_surface_relaxation = 0.26F,
            .adaptive_lip_refinement = false,
        },
    };
    const auto unrelaxed = unrelaxed_mesher.build_mesh(section, sampler);
    const auto relaxed = relaxed_mesher.build_mesh(section, sampler);

    REQUIRE_FALSE(unrelaxed.empty());
    REQUIRE(unrelaxed.vertices.size() == relaxed.vertices.size());
    CHECK(unrelaxed.indices == relaxed.indices);
    CHECK(unrelaxed.quad_count == relaxed.quad_count);

    constexpr auto kMissing = std::numeric_limits<std::size_t>::max();
    std::array<std::size_t, 20> lip_vertex_by_z {};
    lip_vertex_by_z.fill(kMissing);
    for (std::size_t index = 0U; index < unrelaxed.vertices.size(); ++index) {
        const auto& vertex = unrelaxed.vertices[index];
        if (vertex.x <= 0.0F ||
            std::abs(vertex.y - 5.0F) > 0.421F ||
            vertex.z <= -5.0F ||
            vertex.z >= 5.0F ||
            vertex.ny <= 0.05F) {
            continue;
        }
        const auto bin = static_cast<int>(std::lround(vertex.z)) + 9;
        if (bin < 0 ||
            bin >= static_cast<int>(lip_vertex_by_z.size())) {
            continue;
        }
        auto& selected =
            lip_vertex_by_z[static_cast<std::size_t>(bin)];
        if (selected == kMissing ||
            vertex.x > unrelaxed.vertices[selected].x) {
            selected = index;
        }
    }

    auto sample_count = std::size_t {0U};
    auto baseline_variation = 0.0F;
    auto relaxed_variation = 0.0F;
    auto previous = kMissing;
    for (const auto index : lip_vertex_by_z) {
        if (index == kMissing) {
            continue;
        }
        ++sample_count;
        if (previous != kMissing) {
            baseline_variation += std::abs(
                unrelaxed.vertices[index].x -
                unrelaxed.vertices[previous].x);
            relaxed_variation += std::abs(
                relaxed.vertices[index].x -
                relaxed.vertices[previous].x);
        }
        previous = index;
    }

    REQUIRE(sample_count >= 8U);
    REQUIRE(baseline_variation > 1.0F);
    CHECK(relaxed_variation < baseline_variation * 0.80F);
    check_mesh_integrity(relaxed);
}

TEST_CASE("organic terrain adaptive lip refinement stays local and within budget") {
    const auto grass = to_block_id(BlockType::Grass);
    const auto dirt = to_block_id(BlockType::Dirt);
    const auto air = to_block_id(BlockType::Air);
    const OrganicTerrainSection section {{0, 0, 0}, {4, 2, 4}};
    const OrganicTerrainSampler sampler =
        [grass, dirt, air](int x, int y, int z) {
            const auto surface = (x * 7 + z * 11) % 3;
            if (y > surface) {
                return make_sample(air);
            }
            return make_sample(y == surface ? grass : dirt);
        };
    const OrganicTerrainMesher unrefined_mesher {
        OrganicTerrainMesherSettings {
            .maximum_vertex_displacement = 0.42F,
            .exposed_surface_relaxation = 0.26F,
            .adaptive_lip_refinement = false,
        },
    };
    const OrganicTerrainMesher refined_mesher {
        OrganicTerrainMesherSettings {
            .maximum_vertex_displacement = 0.42F,
            .exposed_surface_relaxation = 0.26F,
            .adaptive_lip_refinement = true,
        },
    };
    const auto unrefined =
        unrefined_mesher.build_mesh(section, sampler);
    const auto refined =
        refined_mesher.build_mesh(section, sampler);

    REQUIRE_FALSE(unrefined.empty());
    REQUIRE(refined.vertices.size() > unrefined.vertices.size());
    REQUIRE(refined.indices.size() > unrefined.indices.size());
    CHECK(refined.quad_count == unrefined.quad_count);
    // Le profil canonique reste sous le budget global de +25 %.
    CHECK(refined.indices.size() * 4U <=
          unrefined.indices.size() * 5U);
    REQUIRE(refined.vertices.size() >= unrefined.vertices.size());
    for (std::size_t index = 0U;
         index < unrefined.vertices.size();
         ++index) {
        CHECK(refined.vertices[index] == unrefined.vertices[index]);
    }

    auto original_neighbors =
        std::vector<std::vector<std::uint32_t>>(
            refined.vertices.size() - unrefined.vertices.size());
    for (std::size_t triangle = 0U;
         triangle < refined.indices.size();
         triangle += 3U) {
        for (std::size_t corner = 0U; corner < 3U; ++corner) {
            const auto candidate =
                refined.indices[triangle + corner];
            if (candidate < unrefined.vertices.size()) {
                continue;
            }
            auto& neighbors = original_neighbors[
                candidate - unrefined.vertices.size()];
            for (std::size_t other = 0U; other < 3U; ++other) {
                const auto neighbor =
                    refined.indices[triangle + other];
                if (neighbor >= unrefined.vertices.size() ||
                    std::find(
                        neighbors.begin(),
                        neighbors.end(),
                        neighbor) != neighbors.end()) {
                    continue;
                }
                neighbors.push_back(neighbor);
            }
        }
    }

    auto curved_midpoint_count = std::size_t {0U};
    for (std::size_t local = 0U;
         local < original_neighbors.size();
         ++local) {
        const auto& neighbors = original_neighbors[local];
        if (neighbors.size() != 2U) {
            continue;
        }
        const auto& vertex =
            refined.vertices[unrefined.vertices.size() + local];
        const auto& first = refined.vertices[neighbors[0]];
        const auto& second = refined.vertices[neighbors[1]];
        const std::array<float, 3> chord_midpoint {
            (first.x + second.x) * 0.5F,
            (first.y + second.y) * 0.5F,
            (first.z + second.z) * 0.5F,
        };
        const auto projection = subtract(
            position_of(vertex),
            chord_midpoint);
        CHECK(length_squared(projection) <= 0.16001F * 0.16001F);
        if (length_squared(projection) > 1.0e-8F) {
            ++curved_midpoint_count;
        }
    }
    CHECK(curved_midpoint_count >= 1U);
    check_mesh_integrity(refined);
}

TEST_CASE("organic terrain adaptive refinement excludes a targeted tunnel") {
    const auto stone = to_block_id(BlockType::Stone);
    const auto air = to_block_id(BlockType::Air);
    const OrganicTerrainSampler sampler = [stone, air](int, int y, int z) {
        return make_sample(y == 0 && z == 0 ? air : stone, 0U, 3U);
    };
    const OrganicTerrainSection section {{-3, -1, -1}, {3, 1, 1}};
    const OrganicTerrainMesher unrefined_mesher {
        OrganicTerrainMesherSettings {
            .maximum_vertex_displacement = 0.42F,
            .exposed_surface_relaxation = 0.26F,
            .adaptive_lip_refinement = false,
        },
    };
    const auto unrefined =
        unrefined_mesher.build_mesh(section, sampler);
    const OrganicTerrainMesher refined_mesher {
        OrganicTerrainMesherSettings {
            .maximum_vertex_displacement = 0.42F,
            .exposed_surface_relaxation = 0.26F,
            .adaptive_lip_refinement = true,
        },
    };
    const auto refined =
        refined_mesher.build_mesh(section, sampler);

    REQUIRE_FALSE(unrefined.empty());
    CHECK(refined == unrefined);
}

TEST_CASE("organic terrain bounds the memory overhead of its normal-only halo") {
    std::size_t sampler_calls = 0U;
    const OrganicTerrainSampler sampler = [&sampler_calls](int, int, int) {
        ++sampler_calls;
        return make_sample(to_block_id(BlockType::Air));
    };

    // Une tranche réelle fait 16 x 4 x 16 cellules. Le halo topologique
    // historique aurait 18 x 6 x 18 échantillons ; le halo de normales reste
    // borné à 20 x 8 x 20, soit moins de 5/3 du stockage initial.
    constexpr std::size_t kTopologyHaloSampleCount = 18U * 6U * 18U;
    constexpr std::size_t kNormalHaloSampleCount = 20U * 8U * 20U;
    const auto mesh = OrganicTerrainMesher {}.build_mesh(
        {{0, 0, 0}, {15, 3, 15}},
        sampler);

    CHECK(mesh.empty());
    CHECK(sampler_calls == kNormalHaloSampleCount);
    CHECK(kNormalHaloSampleCount * 3U <=
          kTopologyHaloSampleCount * 5U);
    CHECK(sizeof(OrganicTerrainCellSample) <= 4U);
    CHECK(kOrganicTerrainVisualFieldSampleCount == 64U);
    CHECK(kOrganicTerrainVisualFieldBlockClassificationCount == 0U);
}

TEST_CASE("organic terrain classification keeps architecture out of the density field") {
    CHECK(is_organic_terrain_block(to_block_id(BlockType::Grass)));
    CHECK(is_organic_terrain_block(to_block_id(BlockType::Stone)));
    CHECK(is_organic_terrain_block(to_block_id(BlockType::DiamondOre)));
    CHECK_FALSE(is_organic_terrain_block(to_block_id(BlockType::Air)));
    CHECK_FALSE(is_organic_terrain_block(to_block_id(BlockType::Planks)));
    CHECK_FALSE(is_organic_terrain_block(to_block_id(BlockType::Cobblestone)));
    CHECK_FALSE(is_organic_terrain_block(to_block_id(BlockType::Water)));
}

TEST_CASE("organic terrain geological blend remains smooth across negative macro cells") {
    const auto grass = to_block_id(BlockType::Grass);
    const auto stone = to_block_id(BlockType::Stone);
    const OrganicTerrainSection section {{-25, 0, -1}, {25, 0, 1}};
    const OrganicTerrainSampler sampler = [grass](int, int y, int) {
        return make_sample(y <= 0 ? grass : to_block_id(BlockType::Air));
    };

    const auto mesh = OrganicTerrainMesher {}.build_mesh(section, sampler);
    REQUIRE_FALSE(mesh.empty());

    auto top_vertices = std::vector<TerrainVertex> {};
    for (const auto& vertex : mesh.vertices) {
        if (vertex.ny > 0.999F &&
            std::abs(vertex.z - 1.0F) < 0.00001F) {
            top_vertices.push_back(vertex);
        }
    }
    std::sort(
        top_vertices.begin(),
        top_vertices.end(),
        [](const TerrainVertex& lhs, const TerrainVertex& rhs) {
            return lhs.x < rhs.x;
        });
    REQUIRE(top_vertices.size() >= 40U);

    for (std::size_t index = 0U;
         index < top_vertices.size();
         ++index) {
        const auto& vertex = top_vertices[index];
        CHECK(vertex.primary_block_id == grass);
        CHECK(vertex.secondary_block_id == stone);
        CHECK(
            (vertex.surface_flags &
             kTerrainSurfaceFlagGeologicalBlend) != 0U);
        if (index > 0U) {
            const auto difference = std::abs(
                static_cast<int>(vertex.material_blend) -
                static_cast<int>(
                    top_vertices[index - 1U].material_blend));
            CHECK(difference <= 8);
        }
    }
}

TEST_CASE("organic terrain geological families preserve biome and mineral identity") {
    const auto stone = to_block_id(BlockType::Stone);
    for (const auto source : {
             BlockType::Grass,
             BlockType::Dirt,
             BlockType::Stone,
             BlockType::Sand,
             BlockType::Gravel,
             BlockType::MossyStone,
             BlockType::Snow,
             BlockType::CoalOre,
             BlockType::IronOre,
             BlockType::GoldOre,
             BlockType::DiamondOre,
             BlockType::MetallicAlloyOre,
         }) {
        const auto source_id = to_block_id(source);
        const OrganicTerrainSampler sampler = [source_id](
                                                  int x,
                                                  int y,
                                                  int z) {
            return make_sample(
                x == 0 && y == 0 && z == 0
                    ? source_id
                    : to_block_id(BlockType::Air));
        };
        const auto mesh = OrganicTerrainMesher {}.build_mesh(
            {{0, 0, 0}, {0, 0, 0}},
            sampler);
        REQUIRE_FALSE(mesh.empty());

        const auto expected_surface =
            source == BlockType::Grass ||
                    source == BlockType::Dirt ||
                    source == BlockType::Stone
                ? to_block_id(BlockType::Grass)
                : source_id;
        for (const auto& vertex : mesh.vertices) {
            CAPTURE(static_cast<unsigned int>(source_id));
            CHECK(vertex.primary_block_id == expected_surface);
            CHECK(vertex.secondary_block_id == stone);
            CHECK(
                (vertex.surface_flags &
                 kTerrainSurfaceFlagGeologicalBlend) != 0U);
            if (source >= BlockType::CoalOre &&
                source <= BlockType::MetallicAlloyOre) {
                CHECK(vertex.material_blend <= 107U);
            }
        }
    }
}

TEST_CASE("organic terrain sheltered vertices retain more rock than exposed vertices") {
    const auto grass = to_block_id(BlockType::Grass);
    const auto build = [grass](std::uint8_t sky_light) {
        return OrganicTerrainMesher {}.build_mesh(
            {{-2, 0, -2}, {2, 0, 2}},
            [grass, sky_light](int, int y, int) {
                return make_sample(
                    y <= 0 ? grass : to_block_id(BlockType::Air),
                    sky_light);
            });
    };
    const auto exposed = build(15U);
    const auto sheltered = build(0U);
    REQUIRE(exposed.vertices.size() == sheltered.vertices.size());
    REQUIRE_FALSE(exposed.vertices.empty());

    for (std::size_t index = 0U;
         index < exposed.vertices.size();
         ++index) {
        CHECK(
            sheltered.vertices[index].material_blend >=
            exposed.vertices[index].material_blend);
    }
}

TEST_CASE("organic terrain keeps an exposed minority ore visible among stone samples") {
    const auto stone = to_block_id(BlockType::Stone);
    const auto iron = to_block_id(BlockType::IronOre);
    const OrganicTerrainSampler sampler = [stone, iron](
                                              int x,
                                              int y,
                                              int z) {
        if (y > 0) {
            return make_sample(to_block_id(BlockType::Air));
        }
        return make_sample(
            x == 0 && y == 0 && z == 0 ? iron : stone);
    };
    const auto mesh = OrganicTerrainMesher {}.build_mesh(
        {{-2, 0, -2}, {2, 0, 2}},
        sampler);
    REQUIRE_FALSE(mesh.empty());

    auto ore_vertices = std::size_t {0U};
    auto background_vertices = std::size_t {0U};
    for (const auto& vertex : mesh.vertices) {
        if (vertex.primary_block_id == iron) {
            ++ore_vertices;
            CHECK(vertex.secondary_block_id == stone);
            CHECK(vertex.material_blend <= 107U);
        } else {
            ++background_vertices;
            CHECK(vertex.primary_block_id == to_block_id(BlockType::Grass));
        }
    }

    // Chaque sommet du filon reçoit un seul vote minerai contre plusieurs
    // votes pierre : je vérifie qu'il reste malgré tout visible et localisé.
    CHECK(ore_vertices == 4U);
    CHECK(background_vertices > ore_vertices);
}

} // namespace valcraft
