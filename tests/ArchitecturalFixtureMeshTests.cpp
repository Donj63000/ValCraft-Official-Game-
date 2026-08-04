#include "render/ArchitecturalFixtureMesh.h"
#include "render/VisualMaterials.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

namespace valcraft {

namespace {

using FixtureTestCells =
    std::map<
        std::tuple<int, int, int>,
        ArchitecturalCellSample>;

void set_fixture_test_cell(
    FixtureTestCells& cells,
    BlockCoord coordinate,
    BlockId block,
    std::uint8_t sky_light,
    std::uint8_t block_light) {

    cells[{
        coordinate.x,
        coordinate.y,
        coordinate.z,
    }] = {
        block,
        sky_light,
        block_light,
    };
}

[[nodiscard]] auto fixture_test_sampler(
    const FixtureTestCells& cells) -> ArchitecturalSampler {

    return [&cells](int x, int y, int z) {
        const auto found =
            cells.find({x, y, z});
        return found == cells.end()
            ? ArchitecturalCellSample {}
            : found->second;
    };
}

struct TestVector {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
};

[[nodiscard]] constexpr auto subtract(
    const TestVector& lhs,
    const TestVector& rhs) noexcept -> TestVector {

    return {
        lhs.x - rhs.x,
        lhs.y - rhs.y,
        lhs.z - rhs.z,
    };
}

[[nodiscard]] constexpr auto cross(
    const TestVector& lhs,
    const TestVector& rhs) noexcept -> TestVector {

    return {
        lhs.y * rhs.z - lhs.z * rhs.y,
        lhs.z * rhs.x - lhs.x * rhs.z,
        lhs.x * rhs.y - lhs.y * rhs.x,
    };
}

[[nodiscard]] constexpr auto dot(
    const TestVector& lhs,
    const TestVector& rhs) noexcept -> float {

    return
        lhs.x * rhs.x +
        lhs.y * rhs.y +
        lhs.z * rhs.z;
}

[[nodiscard]] auto length(
    const TestVector& value) -> float {

    return std::sqrt(dot(value, value));
}

[[nodiscard]] auto vertex_position(
    const HardSurfaceVertex& vertex) noexcept -> TestVector {

    return {
        vertex.x,
        vertex.y,
        vertex.z,
    };
}

[[nodiscard]] auto centroid(
    const std::vector<HardSurfaceVertex>& vertices,
    std::size_t first,
    std::size_t count) -> TestVector {

    REQUIRE(count > 0U);
    REQUIRE(first + count <= vertices.size());
    TestVector result {};
    for (auto index = first;
         index < first + count;
         ++index) {
        result.x += vertices[index].x;
        result.y += vertices[index].y;
        result.z += vertices[index].z;
    }
    const auto inverse_count =
        1.0F / static_cast<float>(count);
    result.x *= inverse_count;
    result.y *= inverse_count;
    result.z *= inverse_count;
    return result;
}

void check_fixture_triangles(
    const ArchitecturalMesh& mesh,
    std::size_t first_index) {

    REQUIRE(first_index <= mesh.indices.size());
    REQUIRE(
        (mesh.indices.size() - first_index) %
            3U ==
        0U);
    for (auto index_offset = first_index;
         index_offset < mesh.indices.size();
         index_offset += 3U) {
        const auto index_a =
            mesh.indices[index_offset];
        const auto index_b =
            mesh.indices[index_offset + 1U];
        const auto index_c =
            mesh.indices[index_offset + 2U];
        REQUIRE(index_a < mesh.vertices.size());
        REQUIRE(index_b < mesh.vertices.size());
        REQUIRE(index_c < mesh.vertices.size());
        const auto& a = mesh.vertices[index_a];
        const auto& b = mesh.vertices[index_b];
        const auto& c = mesh.vertices[index_c];
        const auto geometric_normal = cross(
            subtract(vertex_position(b), vertex_position(a)),
            subtract(vertex_position(c), vertex_position(a)));
        CHECK(dot(geometric_normal, geometric_normal) > 1.0e-14F);
        const TestVector stored_normal {
            a.nx + b.nx + c.nx,
            a.ny + b.ny + c.ny,
            a.nz + b.nz + c.nz,
        };
        CHECK(dot(geometric_normal, stored_normal) > 0.0F);
    }
}

[[nodiscard]] auto make_oriented_fixture_mesh()
    -> ArchitecturalMesh {

    constexpr std::array<BlockId, 5> torch_blocks {{
        to_block_id(BlockType::Torch),
        to_block_id(BlockType::TorchWallPositiveX),
        to_block_id(BlockType::TorchWallNegativeX),
        to_block_id(BlockType::TorchWallPositiveZ),
        to_block_id(BlockType::TorchWallNegativeZ),
    }};
    FixtureTestCells cells {};
    for (std::size_t index = 0U;
         index < torch_blocks.size();
         ++index) {
        set_fixture_test_cell(
            cells,
            {
                static_cast<int>(index) - 3,
                5,
                -4,
            },
            torch_blocks[index],
            static_cast<std::uint8_t>(6U + index),
            index % 2U == 0U ? 15U : 2U);
    }

    return ArchitecturalMesher {}.build_mesh(
        {{-3, 5, -4}, {1, 5, -4}, 1},
        fixture_test_sampler(cells));
}

} // namespace

TEST_CASE("modern architectural torches preserve every orientation and light") {
    auto mesh = make_oriented_fixture_mesh();
    REQUIRE(mesh.vertices.empty());
    REQUIRE(mesh.indices.empty());
    REQUIRE(mesh.fixtures.size() == 5U);
    const auto fixtures_before = mesh.fixtures;

    const auto shaft =
        build_stylized_tapered_cylinder(
            StylizedPrimitiveLod::Low);
    const auto flame =
        build_stylized_ellipsoid(
            StylizedPrimitiveLod::Low);
    const auto vertices_per_fixture =
        shaft.vertices.size() +
        flame.vertices.size();
    const auto indices_per_fixture =
        shaft.indices.size() +
        flame.indices.size();

    const auto first_index =
        append_architectural_fixture_geometry(
            mesh,
            StylizedPrimitiveLod::Low);

    CHECK(first_index == 0U);
    CHECK(mesh.fixtures == fixtures_before);
    REQUIRE(
        mesh.vertices.size() ==
        mesh.fixtures.size() *
            vertices_per_fixture);
    REQUIRE(
        mesh.indices.size() ==
        mesh.fixtures.size() *
            indices_per_fixture);

    for (std::size_t fixture_index = 0U;
         fixture_index < mesh.fixtures.size();
         ++fixture_index) {
        const auto& fixture =
            mesh.fixtures[fixture_index];
        const auto first_vertex =
            fixture_index *
            vertices_per_fixture;
        const auto first_flame_vertex =
            first_vertex +
            shaft.vertices.size();
        const auto end_vertex =
            first_vertex +
            vertices_per_fixture;
        const auto minimum_x =
            static_cast<float>(
                fixture.owner_cell.x);
        const auto minimum_y =
            static_cast<float>(
                fixture.owner_cell.y);
        const auto minimum_z =
            static_cast<float>(
                fixture.owner_cell.z);
        for (auto vertex_index = first_vertex;
             vertex_index < end_vertex;
             ++vertex_index) {
            const auto& vertex =
                mesh.vertices[vertex_index];
            CHECK(std::isfinite(vertex.x));
            CHECK(std::isfinite(vertex.y));
            CHECK(std::isfinite(vertex.z));
            CHECK(vertex.x >= minimum_x);
            CHECK(vertex.x <= minimum_x + 1.0F);
            CHECK(vertex.y >= minimum_y);
            CHECK(vertex.y <= minimum_y + 1.0F);
            CHECK(vertex.z >= minimum_z);
            CHECK(vertex.z <= minimum_z + 1.0F);
            CHECK(
                std::sqrt(
                    vertex.nx * vertex.nx +
                    vertex.ny * vertex.ny +
                    vertex.nz * vertex.nz) ==
                doctest::Approx(1.0F));
            CHECK(vertex.u_fixed <= 256U);
            CHECK(vertex.v_fixed <= 256U);
            CHECK(
                vertex.sky_light ==
                fixture.sky_light);
            CHECK(
                vertex.block_light ==
                fixture.block_light);
            CHECK(vertex.surface_flags == 0U);

            const auto flame_vertex =
                vertex_index >=
                first_flame_vertex;
            const auto expected_material =
                flame_vertex
                    ? fixture.source_block
                    : to_block_id(BlockType::Wood);
            CHECK(
                vertex.material_block ==
                expected_material);
            CHECK(
                visual_material_definition(
                    visual_material_for_block(
                        vertex.material_block))
                    .emissive ==
                flame_vertex);
        }

        const auto shaft_center = centroid(
            mesh.vertices,
            first_vertex,
            shaft.vertices.size());
        const auto flame_center = centroid(
            mesh.vertices,
            first_flame_vertex,
            flame.vertices.size());
        const auto center_delta = subtract(
            flame_center,
            shaft_center);
        const TestVector fixture_direction {
            fixture.direction_x,
            fixture.direction_y,
            fixture.direction_z,
        };
        CHECK(
            dot(center_delta, fixture_direction) >
            0.35F);
        CHECK(
            length(
                cross(
                    center_delta,
                    fixture_direction)) <
            1.0e-4F);
    }
    check_fixture_triangles(mesh, first_index);
}

TEST_CASE("fixture geometry appends after opaque architecture and has two stable LODs") {
    FixtureTestCells cells {};
    set_fixture_test_cell(
        cells,
        {-1, 2, -2},
        to_block_id(BlockType::Planks),
        12U,
        3U);
    set_fixture_test_cell(
        cells,
        {0, 2, -2},
        to_block_id(BlockType::Torch),
        9U,
        4U);
    const auto source =
        ArchitecturalMesher {}.build_mesh(
            {{-1, 2, -2}, {0, 2, -2}, 1},
            fixture_test_sampler(cells));
    REQUIRE_FALSE(source.indices.empty());
    REQUIRE(source.fixtures.size() == 1U);
    const auto opaque_vertices =
        source.vertices;
    const auto opaque_indices =
        source.indices;
    const auto opaque_quads =
        source.quads;

    auto low_first = source;
    auto low_second = source;
    auto medium = source;
    auto high = source;
    const auto low_first_index =
        append_architectural_fixture_geometry(
            low_first,
            StylizedPrimitiveLod::Low);
    const auto low_second_index =
        append_architectural_fixture_geometry(
            low_second,
            StylizedPrimitiveLod::Low);
    const auto medium_first_index =
        append_architectural_fixture_geometry(
            medium,
            StylizedPrimitiveLod::Medium);
    const auto high_first_index =
        append_architectural_fixture_geometry(
            high,
            StylizedPrimitiveLod::High);

    CHECK(low_first_index == opaque_indices.size());
    CHECK(low_second_index == opaque_indices.size());
    CHECK(medium_first_index == opaque_indices.size());
    CHECK(high_first_index == opaque_indices.size());
    CHECK(
        std::equal(
            opaque_vertices.begin(),
            opaque_vertices.end(),
            low_first.vertices.begin()));
    CHECK(
        std::equal(
            opaque_indices.begin(),
            opaque_indices.end(),
            low_first.indices.begin()));
    CHECK(low_first.quads == opaque_quads);
    CHECK(low_first == low_second);
    CHECK(
        architectural_mesh_deterministic_hash(low_first) ==
        architectural_mesh_deterministic_hash(low_second));
    CHECK(
        medium.triangle_count() >
        low_first.triangle_count());
    CHECK(high == medium);

    check_fixture_triangles(
        low_first,
        low_first_index);
    check_fixture_triangles(
        medium,
        medium_first_index);
}

TEST_CASE("fixture geometry rejects malformed descriptions atomically") {
    ArchitecturalMesh empty {};
    const auto empty_before = empty;
    CHECK(
        append_architectural_fixture_geometry(
            empty,
            StylizedPrimitiveLod::Low) ==
        0U);
    CHECK(empty == empty_before);

    ArchitecturalFixtureInstance fixture {};
    fixture.position_x = 0.5F;
    fixture.position_y = 0.44F;
    fixture.position_z = 0.5F;
    fixture.owner_cell = {0, 0, 0};
    fixture.bounds = {
        0.0F,
        0.0F,
        0.0F,
        1.0F,
        1.0F,
        1.0F,
        true,
    };
    fixture.source_block =
        to_block_id(BlockType::Torch);
    fixture.direction_x = 0.0F;
    fixture.direction_y = 0.0F;
    fixture.direction_z = 0.0F;

    ArchitecturalMesh invalid_direction {};
    invalid_direction.fixtures.push_back(fixture);
    const auto invalid_direction_before =
        invalid_direction;
    CHECK_THROWS_AS(
        static_cast<void>(
            append_architectural_fixture_geometry(
                invalid_direction,
                StylizedPrimitiveLod::Medium)),
        std::invalid_argument);
    CHECK(
        invalid_direction ==
        invalid_direction_before);

    fixture.direction_y = 1.0F;
    fixture.source_block =
        to_block_id(BlockType::Wood);
    ArchitecturalMesh invalid_material {};
    invalid_material.fixtures.push_back(fixture);
    const auto invalid_material_before =
        invalid_material;
    CHECK_THROWS_AS(
        static_cast<void>(
            append_architectural_fixture_geometry(
                invalid_material,
                StylizedPrimitiveLod::Low)),
        std::invalid_argument);
    CHECK(
        invalid_material ==
        invalid_material_before);

    fixture.source_block =
        to_block_id(BlockType::Torch);
    fixture.position_x = 3.0F;
    ArchitecturalMesh outside_cell {};
    outside_cell.fixtures.push_back(fixture);
    const auto outside_cell_before =
        outside_cell;
    CHECK_THROWS_AS(
        static_cast<void>(
            append_architectural_fixture_geometry(
                outside_cell,
                StylizedPrimitiveLod::Low)),
        std::invalid_argument);
    CHECK(outside_cell == outside_cell_before);
}

TEST_CASE("fixture geometry rejects its public count limit atomically") {
    ArchitecturalMesh mesh {};
    mesh.vertices.push_back(HardSurfaceVertex {});
    mesh.indices.push_back(0U);
    mesh.bounds.valid = true;
    mesh.fixtures.resize(kMaximumArchitecturalFixtures + 1U);
    const auto before = mesh;

    // Je depasse le plafond avec seulement 513 descriptions legeres : le test
    // prouve le rejet avant la construction des primitives et de leur copie.
    CHECK_THROWS_AS(
        static_cast<void>(append_architectural_fixture_geometry(
            mesh,
            StylizedPrimitiveLod::Medium)),
        std::length_error);
    CHECK(mesh == before);
}

} // namespace valcraft
