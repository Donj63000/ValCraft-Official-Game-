#include "render/ArchitecturalMesher.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <tuple>
#include <vector>

namespace valcraft {

namespace {

using TestCells = std::map<std::tuple<int, int, int>, ArchitecturalCellSample>;

void set_cell(
    TestCells& cells,
    int x,
    int y,
    int z,
    BlockId block,
    std::uint8_t sky = 15,
    std::uint8_t light = 0) {
    cells[{x, y, z}] = {block, sky, light};
}

[[nodiscard]] auto make_sampler(const TestCells& cells) -> ArchitecturalSampler {
    return [&cells](int x, int y, int z) {
        const auto found = cells.find({x, y, z});
        return found == cells.end()
            ? ArchitecturalCellSample {}
            : found->second;
    };
}

[[nodiscard]] auto subtract(
    const HardSurfaceVertex& lhs,
    const HardSurfaceVertex& rhs) -> std::array<float, 3> {
    return {
        lhs.x - rhs.x,
        lhs.y - rhs.y,
        lhs.z - rhs.z,
    };
}

[[nodiscard]] auto cross(
    const std::array<float, 3>& lhs,
    const std::array<float, 3>& rhs) -> std::array<float, 3> {
    return {
        lhs[1] * rhs[2] - lhs[2] * rhs[1],
        lhs[2] * rhs[0] - lhs[0] * rhs[2],
        lhs[0] * rhs[1] - lhs[1] * rhs[0],
    };
}

[[nodiscard]] auto dot(
    const std::array<float, 3>& lhs,
    const std::array<float, 3>& rhs) -> float {
    return lhs[0] * rhs[0] + lhs[1] * rhs[1] + lhs[2] * rhs[2];
}

void check_mesh_integrity(const ArchitecturalMesh& mesh) {
    REQUIRE(mesh.indices.size() % 3U == 0U);
    REQUIRE(mesh.vertices.size() == mesh.quads.size() * 4U);
    REQUIRE(mesh.indices.size() == mesh.quads.size() * 6U);
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
        CHECK(normal_length == doctest::Approx(1.0F));
        CHECK(vertex.sky_light <= 15U);
        CHECK(vertex.block_light <= 15U);
        CHECK(
            (vertex.surface_flags &
             static_cast<std::uint8_t>(~(
                 ArchitecturalBevelNegativeU |
                 ArchitecturalBevelPositiveU |
                 ArchitecturalBevelNegativeV |
                 ArchitecturalBevelPositiveV |
                 ArchitecturalTransparent))) == 0U);
    }

    for (std::size_t triangle = 0U;
         triangle < mesh.indices.size();
         triangle += 3U) {
        const auto index_a = mesh.indices[triangle];
        const auto index_b = mesh.indices[triangle + 1U];
        const auto index_c = mesh.indices[triangle + 2U];
        REQUIRE(index_a < mesh.vertices.size());
        REQUIRE(index_b < mesh.vertices.size());
        REQUIRE(index_c < mesh.vertices.size());
        const auto& a = mesh.vertices[index_a];
        const auto& b = mesh.vertices[index_b];
        const auto& c = mesh.vertices[index_c];
        const auto geometric_normal = cross(subtract(b, a), subtract(c, a));
        const std::array<float, 3> stored_normal {a.nx, a.ny, a.nz};
        CHECK(dot(geometric_normal, stored_normal) > 0.0F);
    }

    for (const auto& quad : mesh.quads) {
        REQUIRE(quad.first_vertex + 3U < mesh.vertices.size());
        REQUIRE(quad.first_index + 5U < mesh.indices.size());
        CHECK(quad.width > 0U);
        CHECK(quad.height > 0U);
        for (std::uint32_t offset = 0U; offset < 4U; ++offset) {
            const auto& vertex = mesh.vertices[quad.first_vertex + offset];
            CHECK(vertex.material_block == quad.material_block);
            CHECK(vertex.surface_flags == quad.surface_flags);
        }
    }
    CHECK(mesh.bounds.valid == !mesh.empty());
}

[[nodiscard]] auto has_quad(
    const ArchitecturalMesh& mesh,
    ArchitecturalFace face,
    BlockCoord owner) -> bool {
    return std::any_of(
        mesh.quads.begin(),
        mesh.quads.end(),
        [face, owner](const ArchitecturalQuad& quad) {
            return quad.face == face && quad.owner_cell == owner;
        });
}

} // namespace

TEST_CASE("architectural greedy meshing fuses a complete rectangular volume") {
    TestCells cells {};
    const auto planks = to_block_id(BlockType::Planks);
    for (int y = 2; y <= 4; ++y) {
        for (int z = -3; z <= -2; ++z) {
            for (int x = -5; x <= -2; ++x) {
                set_cell(cells, x, y, z, planks);
            }
        }
    }
    const ArchitecturalSection section {{-5, 2, -3}, {-2, 4, -2}, 1};

    const auto mesh = ArchitecturalMesher {}.build_mesh(
        section,
        make_sampler(cells));

    REQUIRE(mesh.quads.size() == 6U);
    CHECK(mesh.vertices.size() == 24U);
    CHECK(mesh.indices.size() == 36U);
    CHECK(mesh.triangle_count() == 12U);
    for (const auto& quad : mesh.quads) {
        CHECK((quad.surface_flags & 0x0FU) == 0x0FU);
    }
    check_mesh_integrity(mesh);
}

TEST_CASE("architectural greedy meshing preserves material and lighting boundaries") {
    TestCells cells {};
    set_cell(cells, 0, 0, 0, to_block_id(BlockType::Planks), 15, 0);
    set_cell(cells, 1, 0, 0, to_block_id(BlockType::Cobblestone), 15, 0);
    set_cell(cells, 2, 0, 0, to_block_id(BlockType::Cobblestone), 8, 0);
    const ArchitecturalSection section {{0, 0, 0}, {2, 0, 0}, 1};

    const auto mesh = ArchitecturalMesher {}.build_mesh(
        section,
        make_sampler(cells));

    const auto top_count = static_cast<std::size_t>(std::count_if(
        mesh.quads.begin(),
        mesh.quads.end(),
        [](const ArchitecturalQuad& quad) {
            return quad.face == ArchitecturalFace::PositiveY;
        }));
    CHECK(top_count == 3U);
    CHECK(!has_quad(mesh, ArchitecturalFace::PositiveX, {0, 0, 0}));
    CHECK(!has_quad(mesh, ArchitecturalFace::NegativeX, {1, 0, 0}));

    const auto first_top = std::find_if(
        mesh.quads.begin(),
        mesh.quads.end(),
        [](const ArchitecturalQuad& quad) {
            return quad.face == ArchitecturalFace::PositiveY &&
                   quad.owner_cell.x == 0;
        });
    const auto second_top = std::find_if(
        mesh.quads.begin(),
        mesh.quads.end(),
        [](const ArchitecturalQuad& quad) {
            return quad.face == ArchitecturalFace::PositiveY &&
                   quad.owner_cell.x == 1;
        });
    REQUIRE(first_top != mesh.quads.end());
    REQUIRE(second_top != mesh.quads.end());
    // Je ne marque jamais de biseau sur la jonction entre deux cellules
    // solides, meme lorsque leur materiau force deux rectangles distincts.
    CHECK(
        (first_top->surface_flags & ArchitecturalBevelPositiveV) == 0U);
    CHECK(
        (second_top->surface_flags & ArchitecturalBevelNegativeV) == 0U);
    check_mesh_integrity(mesh);
}

TEST_CASE("architectural glass removes internal panes and remains explicitly transparent") {
    TestCells cells {};
    const auto glass = to_block_id(BlockType::Glass);
    set_cell(cells, 2, 3, 4, glass);
    set_cell(cells, 3, 3, 4, glass);
    const ArchitecturalSection section {{2, 3, 4}, {3, 3, 4}, 1};

    const auto mesh = ArchitecturalMesher {}.build_mesh(
        section,
        make_sampler(cells));

    REQUIRE(mesh.quads.size() == 6U);
    CHECK(!has_quad(mesh, ArchitecturalFace::PositiveX, {2, 3, 4}));
    CHECK(!has_quad(mesh, ArchitecturalFace::NegativeX, {3, 3, 4}));
    for (const auto& vertex : mesh.vertices) {
        CHECK(vertex.material_block == glass);
        CHECK(
            (vertex.surface_flags & ArchitecturalTransparent) != 0U);
    }
    check_mesh_integrity(mesh);
}

TEST_CASE("architectural openings stay empty and expose only their contour bevels") {
    TestCells cells {};
    const auto planks = to_block_id(BlockType::Planks);
    for (int y = 0; y < 5; ++y) {
        for (int x = 0; x < 5; ++x) {
            if (x != 2 || y != 2) {
                set_cell(cells, x, y, 0, planks);
            }
        }
    }
    const ArchitecturalSection section {{0, 0, 0}, {4, 4, 0}, 1};

    const auto mesh = ArchitecturalMesher {}.build_mesh(
        section,
        make_sampler(cells));

    const auto front_covers_opening = std::any_of(
        mesh.quads.begin(),
        mesh.quads.end(),
        [](const ArchitecturalQuad& quad) {
            if (quad.face != ArchitecturalFace::PositiveZ) {
                return false;
            }
            return quad.owner_cell.x <= 2 &&
                   2 < quad.owner_cell.x + static_cast<int>(quad.width) &&
                   quad.owner_cell.y <= 2 &&
                   2 < quad.owner_cell.y + static_cast<int>(quad.height);
        });
    CHECK(!front_covers_opening);

    const auto left_reveal = std::find_if(
        mesh.quads.begin(),
        mesh.quads.end(),
        [](const ArchitecturalQuad& quad) {
            return quad.face == ArchitecturalFace::PositiveX &&
                   quad.owner_cell == BlockCoord {1, 2, 0};
        });
    const auto right_reveal = std::find_if(
        mesh.quads.begin(),
        mesh.quads.end(),
        [](const ArchitecturalQuad& quad) {
            return quad.face == ArchitecturalFace::NegativeX &&
                   quad.owner_cell == BlockCoord {3, 2, 0};
        });
    const auto lower_reveal = std::find_if(
        mesh.quads.begin(),
        mesh.quads.end(),
        [](const ArchitecturalQuad& quad) {
            return quad.face == ArchitecturalFace::PositiveY &&
                   quad.owner_cell == BlockCoord {2, 1, 0};
        });
    const auto upper_reveal = std::find_if(
        mesh.quads.begin(),
        mesh.quads.end(),
        [](const ArchitecturalQuad& quad) {
            return quad.face == ArchitecturalFace::NegativeY &&
                   quad.owner_cell == BlockCoord {2, 3, 0};
        });
    REQUIRE(left_reveal != mesh.quads.end());
    REQUIRE(right_reveal != mesh.quads.end());
    REQUIRE(lower_reveal != mesh.quads.end());
    REQUIRE(upper_reveal != mesh.quads.end());
    // Je marque les levres avant et arriere de l'ouverture, mais jamais les
    // jonctions encore bordees par une cellule pleine du mur.
    CHECK(
        (left_reveal->surface_flags &
         (ArchitecturalBevelNegativeV | ArchitecturalBevelPositiveV)) ==
        (ArchitecturalBevelNegativeV | ArchitecturalBevelPositiveV));
    CHECK(
        (right_reveal->surface_flags &
         (ArchitecturalBevelNegativeU | ArchitecturalBevelPositiveU)) ==
        (ArchitecturalBevelNegativeU | ArchitecturalBevelPositiveU));
    CHECK(
        (lower_reveal->surface_flags &
         (ArchitecturalBevelNegativeU | ArchitecturalBevelPositiveU)) ==
        (ArchitecturalBevelNegativeU | ArchitecturalBevelPositiveU));
    CHECK(
        (upper_reveal->surface_flags &
         (ArchitecturalBevelNegativeV | ArchitecturalBevelPositiveV)) ==
        (ArchitecturalBevelNegativeV | ArchitecturalBevelPositiveV));
    check_mesh_integrity(mesh);
}

TEST_CASE("architectural ownership removes duplicate faces on negative section boundaries") {
    TestCells cells {};
    const auto cobble = to_block_id(BlockType::Cobblestone);
    for (int x = -2; x <= 1; ++x) {
        set_cell(cells, x, 2, -3, cobble);
    }
    const ArchitecturalSection left {{-2, 2, -3}, {-1, 2, -3}, 1};
    const ArchitecturalSection right {{0, 2, -3}, {1, 2, -3}, 1};

    const auto left_mesh = ArchitecturalMesher {}.build_mesh(
        left,
        make_sampler(cells));
    const auto right_mesh = ArchitecturalMesher {}.build_mesh(
        right,
        make_sampler(cells));

    CHECK(!has_quad(
        left_mesh,
        ArchitecturalFace::PositiveX,
        {-1, 2, -3}));
    CHECK(!has_quad(
        right_mesh,
        ArchitecturalFace::NegativeX,
        {0, 2, -3}));
    CHECK(left_mesh.quads.size() == 5U);
    CHECK(right_mesh.quads.size() == 5U);

    const auto seam_vertex = [](const HardSurfaceVertex& vertex) {
        return vertex.x == 0.0F &&
               vertex.y == 3.0F &&
               (vertex.z == -3.0F || vertex.z == -2.0F);
    };
    CHECK(std::count_if(
        left_mesh.vertices.begin(),
        left_mesh.vertices.end(),
        seam_vertex) > 0);
    CHECK(std::count_if(
        right_mesh.vertices.begin(),
        right_mesh.vertices.end(),
        seam_vertex) > 0);
    check_mesh_integrity(left_mesh);
    check_mesh_integrity(right_mesh);
}

TEST_CASE("architectural fixtures represent every real torch orientation without cube faces") {
    TestCells cells {};
    const std::array<BlockId, 5> torches {{
        to_block_id(BlockType::Torch),
        to_block_id(BlockType::TorchWallPositiveX),
        to_block_id(BlockType::TorchWallNegativeX),
        to_block_id(BlockType::TorchWallPositiveZ),
        to_block_id(BlockType::TorchWallNegativeZ),
    }};
    for (std::size_t index = 0U; index < torches.size(); ++index) {
        set_cell(
            cells,
            static_cast<int>(index),
            5,
            -2,
            torches[index],
            7,
            2);
    }
    const ArchitecturalSection section {{0, 5, -2}, {4, 5, -2}, 1};

    const auto mesh = ArchitecturalMesher {}.build_mesh(
        section,
        make_sampler(cells));

    CHECK(mesh.vertices.empty());
    CHECK(mesh.indices.empty());
    CHECK(mesh.quads.empty());
    REQUIRE(mesh.fixtures.size() == torches.size());
    for (std::size_t index = 0U; index < mesh.fixtures.size(); ++index) {
        const auto& fixture = mesh.fixtures[index];
        CHECK(fixture.source_block == torches[index]);
        CHECK(fixture.owner_cell == BlockCoord {
            static_cast<int>(index),
            5,
            -2,
        });
        CHECK(std::isfinite(fixture.position_x));
        CHECK(std::isfinite(fixture.position_y));
        CHECK(std::isfinite(fixture.position_z));
        CHECK(std::isfinite(fixture.direction_x));
        CHECK(std::isfinite(fixture.direction_y));
        CHECK(std::isfinite(fixture.direction_z));
        const auto direction_length = std::sqrt(
            fixture.direction_x * fixture.direction_x +
            fixture.direction_y * fixture.direction_y +
            fixture.direction_z * fixture.direction_z);
        CHECK(direction_length == doctest::Approx(1.0F));
        CHECK(fixture.block_light == 14U);
        CHECK(fixture.bounds.valid);
    }
    CHECK(mesh.bounds.valid);
    check_mesh_integrity(mesh);
}

TEST_CASE("architectural mesh and halo sampling are bit deterministic") {
    TestCells cells {};
    set_cell(cells, -1, 1, -1, to_block_id(BlockType::Wood), 12, 3);
    set_cell(cells, 0, 1, -1, to_block_id(BlockType::Planks), 12, 3);
    set_cell(cells, 1, 1, -1, to_block_id(BlockType::Glass), 9, 4);
    const ArchitecturalSection section {{-1, 0, -1}, {1, 3, 1}, 1};

    const auto first = ArchitecturalMesher {}.build_mesh(
        section,
        make_sampler(cells));
    const auto second = ArchitecturalMesher {}.build_mesh(
        section,
        make_sampler(cells));
    CHECK(first == second);
    CHECK(
        architectural_mesh_deterministic_hash(first) ==
        architectural_mesh_deterministic_hash(second));

    // Je change une cellule au-dela du halo : l'entree contractuelle de la
    // section reste identique et ne doit produire aucune reclassification.
    set_cell(cells, 3, 1, -1, to_block_id(BlockType::Cobblestone));
    const auto outside_changed = ArchitecturalMesher {}.build_mesh(
        section,
        make_sampler(cells));
    CHECK(first == outside_changed);
    CHECK(
        architectural_mesh_deterministic_hash(first) ==
        architectural_mesh_deterministic_hash(outside_changed));
    check_mesh_integrity(first);
}

TEST_CASE("architectural mesher rejects invalid ranges and missing halo data") {
    const ArchitecturalSection inverted {{1, 0, 0}, {0, 1, 1}, 1};
    CHECK_THROWS_AS(
        static_cast<void>(ArchitecturalMesher {}.build_mesh(
            inverted,
            [](int, int, int) {
                return ArchitecturalCellSample {};
            })),
        std::invalid_argument);

    const ArchitecturalSection no_halo {{0, 0, 0}, {1, 1, 1}, 0};
    CHECK_THROWS_AS(
        static_cast<void>(ArchitecturalMesher {}.build_mesh(
            no_halo,
            [](int, int, int) {
                return ArchitecturalCellSample {};
            })),
        std::invalid_argument);

    const ArchitecturalSection valid {{0, 0, 0}, {1, 1, 1}, 1};
    CHECK_THROWS_AS(
        static_cast<void>(ArchitecturalMesher {}.build_mesh(
            valid,
            ArchitecturalSampler {})),
        std::invalid_argument);
}

} // namespace valcraft
