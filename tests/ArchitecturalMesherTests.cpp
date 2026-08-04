#include "render/ArchitecturalMesher.h"
#include "render/BackroomsVisibility.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
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

[[nodiscard]] auto perceptual_block_visibility(
    float block_light) noexcept -> float {
    const auto normalized_light = std::clamp(
        block_light / 15.0F,
        0.0F,
        1.0F);
    const auto ramp = std::clamp(
        (normalized_light -
         kBackroomsDarknessBlockLightBlackThreshold) /
            (kBackroomsDarknessBlockLightFullVisibilityThreshold -
             kBackroomsDarknessBlockLightBlackThreshold),
        0.0F,
        1.0F);
    return ramp * ramp * (3.0F - 2.0F * ramp);
}

[[nodiscard]] auto interpolated_quad_block_light(
    const ArchitecturalMesh& mesh,
    const ArchitecturalQuad& quad,
    float normalized_u,
    float normalized_v) -> float {
    REQUIRE(quad.first_vertex + 3U < mesh.vertices.size());
    REQUIRE(quad.first_index + 5U < mesh.indices.size());
    const auto value = [&](std::uint32_t offset) noexcept {
        return static_cast<float>(
            mesh.vertices[quad.first_vertex + offset].block_light);
    };
    const auto flipped_diagonal =
        mesh.indices[quad.first_index + 2U] ==
        quad.first_vertex + 3U;

    if (flipped_diagonal) {
        if (normalized_u + normalized_v <= 1.0F) {
            return
                value(0U) * (1.0F - normalized_u - normalized_v) +
                value(1U) * normalized_u +
                value(3U) * normalized_v;
        }
        return
            value(1U) * (1.0F - normalized_v) +
            value(2U) * (normalized_u + normalized_v - 1.0F) +
            value(3U) * (1.0F - normalized_u);
    }

    if (normalized_v <= normalized_u) {
        return
            value(0U) * (1.0F - normalized_u) +
            value(1U) * (normalized_u - normalized_v) +
            value(2U) * normalized_v;
    }
    return
        value(0U) * (1.0F - normalized_v) +
        value(2U) * normalized_u +
        value(3U) * (normalized_v - normalized_u);
}

template <typename LightChannel>
[[nodiscard]] auto quad_light_channel_is_affine(
    const ArchitecturalMesh& mesh,
    const ArchitecturalQuad& quad,
    LightChannel&& light_channel) -> bool {
    REQUIRE(quad.first_vertex + 3U < mesh.vertices.size());
    const auto value = [&](std::uint32_t offset) noexcept {
        return static_cast<int>(
            light_channel(
                mesh.vertices[
                    quad.first_vertex + offset]));
    };

    // Je vérifie que les deux triangles décrivent exactement le même plan de
    // lumière. Dans ce cas, leur diagonale ne peut créer aucune cassure.
    return value(0U) + value(2U) ==
           value(1U) + value(3U);
}

template <typename ActualLight>
[[nodiscard]] auto maximum_floor_visibility_error(
    const ArchitecturalMesh& mesh,
    const ArchitecturalQuad& quad,
    ActualLight&& actual_light) -> float {
    REQUIRE(quad.face == ArchitecturalFace::PositiveY);
    auto maximum_error = 0.0F;
    for (int offset_v = 0;
         offset_v < static_cast<int>(quad.height);
         ++offset_v) {
        for (int offset_u = 0;
             offset_u < static_cast<int>(quad.width);
             ++offset_u) {
            const auto normalized_u =
                (static_cast<float>(offset_u) + 0.5F) /
                static_cast<float>(quad.width);
            const auto normalized_v =
                (static_cast<float>(offset_v) + 0.5F) /
                static_cast<float>(quad.height);
            const auto predicted = interpolated_quad_block_light(
                mesh,
                quad,
                normalized_u,
                normalized_v);
            const auto actual = static_cast<float>(actual_light(
                quad.owner_cell.x + offset_v,
                quad.owner_cell.z + offset_u));
            maximum_error = std::max(
                maximum_error,
                std::fabs(
                    perceptual_block_visibility(predicted) -
                    perceptual_block_visibility(actual)));
        }
    }
    return maximum_error;
}

} // namespace

TEST_CASE("les blocs architecturaux Backrooms couvrent les enveloppes et la marche continue") {
    for (std::uint16_t numeric_id = 0U; numeric_id <= 68U; ++numeric_id) {
        const auto block_id = static_cast<BlockId>(numeric_id);
        const auto expected =
            (numeric_id >= 42U && numeric_id <= 59U) ||
            numeric_id == 64U;
        CAPTURE(numeric_id);
        CHECK(is_backrooms_architectural_block(block_id) == expected);
    }

    // Je verrouille les meubles et les rampes : seule la marche pleine peut
    // rejoindre les enveloppes greedy afin de supprimer ses cellules internes.
    CHECK_FALSE(is_backrooms_architectural_block(
        to_block_id(BlockType::LeviathanSpine)));
    for (const auto numeric_id :
         std::array<std::uint16_t, 8> {{60U, 61U, 62U, 63U, 65U, 66U, 67U, 68U}}) {
        CAPTURE(numeric_id);
        CHECK_FALSE(is_backrooms_architectural_block(
            static_cast<BlockId>(numeric_id)));
    }
}

TEST_CASE("le maillage Backrooms fusionne les volumes et garde les frontieres physiques") {
    TestCells volume_cells {};
    const auto wallpaper = to_block_id(BlockType::BackroomsWallYellow);
    for (int y = 1; y <= 3; ++y) {
        for (int z = -2; z <= 0; ++z) {
            for (int x = 4; x <= 7; ++x) {
                set_cell(volume_cells, x, y, z, wallpaper, 11, 3);
            }
        }
    }

    const auto volume_mesh = ArchitecturalMesher {}.build_mesh(
        ArchitecturalSection {{4, 1, -2}, {7, 3, 0}, 1},
        make_sampler(volume_cells));

    REQUIRE(volume_mesh.quads.size() == 6U);
    CHECK(volume_mesh.vertices.size() == 24U);
    CHECK(volume_mesh.indices.size() == 36U);
    CHECK(std::all_of(
        volume_mesh.quads.begin(),
        volume_mesh.quads.end(),
        [wallpaper](const ArchitecturalQuad& quad) {
            return quad.material_block == wallpaper;
        }));
    check_mesh_integrity(volume_mesh);

    TestCells boundary_cells {};
    const auto yellow = to_block_id(BlockType::BackroomsWallYellow);
    const auto green = to_block_id(BlockType::BackroomsWallGreen);
    set_cell(boundary_cells, 0, 0, 0, yellow, 15, 1);
    set_cell(boundary_cells, 1, 0, 0, green, 15, 1);
    set_cell(boundary_cells, 2, 0, 0, green, 7, 1);
    set_cell(boundary_cells, 0, 1, 0, to_block_id(BlockType::Air), 15, 1);
    set_cell(boundary_cells, 1, 1, 0, to_block_id(BlockType::Air), 15, 1);
    set_cell(boundary_cells, 2, 1, 0, to_block_id(BlockType::Air), 7, 1);

    const auto boundary_mesh = ArchitecturalMesher {}.build_mesh(
        ArchitecturalSection {{0, 0, 0}, {2, 0, 0}, 1},
        make_sampler(boundary_cells));
    const auto top_quads = static_cast<std::size_t>(std::count_if(
        boundary_mesh.quads.begin(),
        boundary_mesh.quads.end(),
        [](const ArchitecturalQuad& quad) {
            return quad.face == ArchitecturalFace::PositiveY;
        }));

    // Je conserve la frontiere physique entre papiers peints, puis je fusionne
    // les deux paliers lumineux verts : leurs sommets portent le gradient.
    CHECK(top_quads == 2U);
    const auto green_top = std::find_if(
        boundary_mesh.quads.begin(),
        boundary_mesh.quads.end(),
        [green](const ArchitecturalQuad& quad) {
            return quad.face == ArchitecturalFace::PositiveY &&
                   quad.material_block == green;
        });
    REQUIRE(green_top != boundary_mesh.quads.end());
    CHECK(
        static_cast<std::uint32_t>(green_top->width) *
            static_cast<std::uint32_t>(green_top->height) ==
        2U);
    std::array<std::uint8_t, 4> green_sky {};
    for (std::size_t offset = 0U;
         offset < green_sky.size();
         ++offset) {
        green_sky[offset] = boundary_mesh
            .vertices[green_top->first_vertex + offset]
            .sky_light;
    }
    CHECK(std::find(green_sky.begin(), green_sky.end(), 15U) != green_sky.end());
    CHECK(std::find(green_sky.begin(), green_sky.end(), 7U) != green_sky.end());
    CHECK(!has_quad(boundary_mesh, ArchitecturalFace::PositiveX, {0, 0, 0}));
    CHECK(!has_quad(boundary_mesh, ArchitecturalFace::NegativeX, {1, 0, 0}));
    check_mesh_integrity(boundary_mesh);
}

TEST_CASE("les colonnes de marche Backrooms deviennent une enveloppe continue") {
    TestCells cells {};
    const auto step = to_block_id(BlockType::BackroomsConnectorStep);
    for (int y = 0; y < 3; ++y) {
        for (int z = 0; z < 2; ++z) {
            for (int x = 0; x < 2; ++x) {
                set_cell(cells, x, y, z, step, 12U, 4U);
            }
        }
    }

    const auto mesh = ArchitecturalMesher {}.build_mesh(
        ArchitecturalSection {{0, 0, 0}, {1, 2, 1}, 1},
        make_sampler(cells));

    // Je remplace douze rounded-boxes et leurs faces internes par les six
    // nappes du volume visible. Le materiau 64 reste celui du beton PBR.
    REQUIRE(mesh.quads.size() == 6U);
    CHECK(mesh.vertices.size() == 24U);
    CHECK(mesh.indices.size() == 36U);
    CHECK(std::all_of(
        mesh.quads.begin(),
        mesh.quads.end(),
        [step](const ArchitecturalQuad& quad) {
            return quad.material_block == step;
        }));
    check_mesh_integrity(mesh);
}

TEST_CASE("les meubles ajoures ne percent plus le sol architectural") {
    const auto carpet = to_block_id(BlockType::BackroomsCarpet);
    for (const auto furniture : std::array {
             BlockType::BackroomsDesk,
             BlockType::BackroomsChair,
         }) {
        TestCells cells {};
        set_cell(cells, 0, 0, 0, carpet, 0U, 8U);
        set_cell(cells, 0, 1, 0, to_block_id(furniture), 0U, 8U);

        const auto mesh = ArchitecturalMesher {}.build_mesh(
            ArchitecturalSection {{0, 0, 0}, {0, 0, 0}, 1},
            make_sampler(cells));

        CAPTURE(static_cast<int>(furniture));
        CHECK(has_quad(
            mesh,
            ArchitecturalFace::PositiveY,
            {0, 0, 0}));
        check_mesh_integrity(mesh);
    }
}

TEST_CASE("l'ordre GPU conserve les triangles intercalaires entre les quads") {
    ArchitecturalMesh mesh {};
    for (std::uint32_t index = 0U; index < 27U; ++index) {
        mesh.indices.push_back(index);
    }
    const auto opaque_material =
        to_block_id(BlockType::BackroomsWallYellow);
    const auto transparent_material = to_block_id(BlockType::Glass);
    mesh.quads = {
        {0U, 0U, 1U, 1U, {}, ArchitecturalFace::PositiveX,
         opaque_material, 0U},
        {0U, 9U, 1U, 1U, {}, ArchitecturalFace::PositiveX,
         transparent_material, ArchitecturalTransparent},
        {0U, 18U, 1U, 1U, {}, ArchitecturalFace::PositiveX,
         opaque_material, 0U},
    };

    std::vector<std::uint32_t> ordered;
    std::vector<std::uint8_t> coverage;
    const auto opaque_count = order_architectural_indices_for_render(
        mesh,
        ordered,
        coverage);
    const std::vector<std::uint32_t> expected {
        0U, 1U, 2U, 3U, 4U, 5U,
        18U, 19U, 20U, 21U, 22U, 23U,
        6U, 7U, 8U,
        15U, 16U, 17U,
        24U, 25U, 26U,
        9U, 10U, 11U, 12U, 13U, 14U,
    };

    CHECK(opaque_count == 21U);
    CHECK(ordered == expected);
    REQUIRE(coverage.size() == mesh.indices.size());
    for (std::size_t index = 0U; index < coverage.size(); ++index) {
        const auto covered_by_quad =
            index < 6U ||
            (index >= 9U && index < 15U) ||
            (index >= 18U && index < 24U);
        CHECK(coverage[index] == (covered_by_quad ? 1U : 0U));
    }
}

TEST_CASE("les faces Backrooms recuperent la lumiere du couloir adjacent") {
    TestCells cells {};
    const auto wallpaper =
        to_block_id(BlockType::BackroomsWallYellow);
    set_cell(cells, 0, 0, 0, wallpaper, 0, 0);
    set_cell(
        cells,
        1,
        0,
        0,
        to_block_id(BlockType::Air),
        2,
        13);

    const auto mesh = ArchitecturalMesher {}.build_mesh(
        ArchitecturalSection {{0, 0, 0}, {0, 0, 0}, 1},
        make_sampler(cells));
    const auto lit_face = std::find_if(
        mesh.quads.begin(),
        mesh.quads.end(),
        [](const ArchitecturalQuad& quad) {
            return quad.face == ArchitecturalFace::PositiveX;
        });

    REQUIRE(lit_face != mesh.quads.end());
    for (std::uint32_t offset = 0U; offset < 4U; ++offset) {
        const auto& vertex =
            mesh.vertices[lit_face->first_vertex + offset];
        CHECK(vertex.sky_light == 2U);
        CHECK(vertex.block_light == 13U);
    }
    check_mesh_integrity(mesh);
}

TEST_CASE("la rampe Backrooms se raffine sans couture perceptuelle") {
    TestCells cells {};
    const auto carpet =
        to_block_id(BlockType::BackroomsCarpet);
    for (int x = 0; x < 8; ++x) {
        set_cell(cells, x, 0, 0, carpet, 0, 0);
        set_cell(
            cells,
            x,
            1,
            0,
            to_block_id(BlockType::Air),
            0,
            static_cast<std::uint8_t>(x * 2));
    }

    const auto mesh = ArchitecturalMesher {}.build_mesh(
        ArchitecturalSection {{0, 0, 0}, {7, 0, 0}, 1},
        make_sampler(cells));
    std::vector<const ArchitecturalQuad*> floors;
    for (const auto& quad : mesh.quads) {
        if (quad.face == ArchitecturalFace::PositiveY) {
            floors.push_back(&quad);
        }
    }
    REQUIRE(floors.size() > 2U);
    CHECK(floors.size() <= 8U);
    auto covered_cells = std::uint32_t {0U};
    std::map<
        std::tuple<float, float, float>,
        std::pair<std::uint8_t, std::uint8_t>> shared_lights;
    auto shared_vertices = std::size_t {0U};
    for (const auto* floor : floors) {
        const auto area =
            static_cast<std::uint32_t>(floor->width) *
            static_cast<std::uint32_t>(floor->height);
        covered_cells += area;
        if (area > 1U) {
            CHECK(
                maximum_floor_visibility_error(
                    mesh,
                    *floor,
                    [](int x, int) {
                        return static_cast<std::uint8_t>(x * 2);
                    }) <= 0.060001F);
        }
        for (std::uint32_t offset = 0U; offset < 4U; ++offset) {
            const auto& vertex =
                mesh.vertices[floor->first_vertex + offset];
            const auto [found, inserted] = shared_lights.emplace(
                std::tuple {vertex.x, vertex.y, vertex.z},
                std::pair {vertex.sky_light, vertex.block_light});
            if (!inserted) {
                ++shared_vertices;
                CHECK(found->second.first == vertex.sky_light);
                CHECK(found->second.second == vertex.block_light);
            }
        }
    }
    CHECK(covered_cells == 8U);
    CHECK(shared_vertices > 0U);
    check_mesh_integrity(mesh);
}

TEST_CASE("un plateau deja visible conserve le patch Backrooms quatre par quatre") {
    TestCells cells {};
    const auto carpet =
        to_block_id(BlockType::BackroomsCarpet);
    for (int z = 0; z < 4; ++z) {
        for (int x = 0; x < 4; ++x) {
            set_cell(cells, x, 0, z, carpet, 0, 0);
            const auto distance =
                std::abs(x - 1) + std::abs(z - 1);
            set_cell(
                cells,
                x,
                1,
                z,
                to_block_id(BlockType::Air),
                0,
                static_cast<std::uint8_t>(14 - distance));
        }
    }

    const auto mesh = ArchitecturalMesher {}.build_mesh(
        ArchitecturalSection {{0, 0, 0}, {3, 0, 3}, 1},
        make_sampler(cells));
    std::vector<const ArchitecturalQuad*> floors;
    for (const auto& quad : mesh.quads) {
        if (quad.face == ArchitecturalFace::PositiveY) {
            floors.push_back(&quad);
        }
    }

    REQUIRE(floors.size() == 1U);
    CHECK(floors.front()->width == 4U);
    CHECK(floors.front()->height == 4U);
    CHECK(
        maximum_floor_visibility_error(
            mesh,
            *floors.front(),
            [](int x, int z) {
                const auto distance =
                    std::abs(x - 1) + std::abs(z - 1);
                return static_cast<std::uint8_t>(14 - distance);
            }) == doctest::Approx(0.0F));
    check_mesh_integrity(mesh);
}

TEST_CASE("un plateau uniforme et un gradient affine restent fusionnes en quatre par quatre") {
    const auto carpet =
        to_block_id(BlockType::BackroomsCarpet);
    const auto build_floor = [&](bool affine_gradient) {
        TestCells cells {};
        for (int z = -1; z <= 4; ++z) {
            for (int x = -1; x <= 4; ++x) {
                const auto light = static_cast<std::uint8_t>(
                    affine_gradient ? 10 + x : 5);
                set_cell(cells, x, 0, z, carpet, 0, 0);
                set_cell(
                    cells,
                    x,
                    1,
                    z,
                    to_block_id(BlockType::Air),
                    0,
                    light);
            }
        }
        return ArchitecturalMesher {}.build_mesh(
            ArchitecturalSection {{0, 0, 0}, {3, 0, 3}, 1},
            make_sampler(cells));
    };

    for (const auto affine_gradient : {false, true}) {
        const auto mesh = build_floor(affine_gradient);
        std::vector<const ArchitecturalQuad*> floors;
        for (const auto& quad : mesh.quads) {
            if (quad.face == ArchitecturalFace::PositiveY) {
                floors.push_back(&quad);
            }
        }

        CAPTURE(affine_gradient);
        REQUIRE(floors.size() == 1U);
        CHECK(floors.front()->width == 4U);
        CHECK(floors.front()->height == 4U);
        CHECK(quad_light_channel_is_affine(
            mesh,
            *floors.front(),
            [](const HardSurfaceVertex& vertex) noexcept {
                return vertex.sky_light;
            }));
        CHECK(quad_light_channel_is_affine(
            mesh,
            *floors.front(),
            [](const HardSurfaceVertex& vertex) noexcept {
                return vertex.block_light;
            }));
        check_mesh_integrity(mesh);
    }

    const auto first = build_floor(true);
    const auto second = build_floor(true);
    CHECK(first == second);
    CHECK(
        architectural_mesh_deterministic_hash(first) ==
        architectural_mesh_deterministic_hash(second));
}

TEST_CASE("une anomalie intermediaire sous le seuil ne garde pas un long patch") {
    TestCells cells {};
    const auto carpet =
        to_block_id(BlockType::BackroomsCarpet);
    for (int x = 0; x < 4; ++x) {
        set_cell(cells, x, 0, 0, carpet, 0, 0);
        set_cell(
            cells,
            x,
            1,
            0,
            to_block_id(BlockType::Air),
            0,
            static_cast<std::uint8_t>(x == 0 ? 1 : 0));
    }

    const auto build = [&] {
        return ArchitecturalMesher {}.build_mesh(
            ArchitecturalSection {{0, 0, 0}, {3, 0, 0}, 1},
            make_sampler(cells));
    };
    const auto mesh = build();
    std::vector<const ArchitecturalQuad*> floors;
    for (const auto& quad : mesh.quads) {
        if (quad.face == ArchitecturalFace::PositiveY) {
            floors.push_back(&quad);
        }
    }

    // Je verrouille le cas qui passait le controle perceptuel aux centres :
    // ses coins externes sont coplanaires, mais son sommet intermediaire ne
    // suit pas ce plan et produisait une longue couture au raccord suivant.
    REQUIRE(floors.size() > 1U);
    auto covered_cells = std::uint32_t {0U};
    for (const auto* floor : floors) {
        covered_cells +=
            static_cast<std::uint32_t>(floor->width) *
            static_cast<std::uint32_t>(floor->height);
    }
    CHECK(covered_cells == 4U);
    const auto repeated = build();
    CHECK(mesh == repeated);
    CHECK(
        architectural_mesh_deterministic_hash(mesh) ==
        architectural_mesh_deterministic_hash(repeated));
    check_mesh_integrity(mesh);
}

TEST_CASE("un damier Backrooms deux par deux atteint les cellules terminales") {
    TestCells cells {};
    const auto carpet =
        to_block_id(BlockType::BackroomsCarpet);
    const std::array<std::uint8_t, 4> lights {{15U, 0U, 0U, 15U}};
    auto index = std::size_t {0U};
    for (int z = 0; z < 2; ++z) {
        for (int x = 0; x < 2; ++x) {
            set_cell(cells, x, 0, z, carpet, 0, 0);
            set_cell(
                cells,
                x,
                1,
                z,
                to_block_id(BlockType::Air),
                0,
                lights[index++]);
        }
    }

    const auto mesh = ArchitecturalMesher {}.build_mesh(
        ArchitecturalSection {{0, 0, 0}, {1, 0, 1}, 1},
        make_sampler(cells));
    std::vector<const ArchitecturalQuad*> floors;
    for (const auto& quad : mesh.quads) {
        if (quad.face == ArchitecturalFace::PositiveY) {
            floors.push_back(&quad);
        }
    }
    REQUIRE(floors.size() == 4U);
    CHECK(std::all_of(
        floors.begin(),
        floors.end(),
        [](const ArchitecturalQuad* floor) {
            return floor->width == 1U && floor->height == 1U;
        }));
    check_mesh_integrity(mesh);
}

TEST_CASE("une anomalie de coin quatre par quatre ne devient plus une grande diagonale") {
    TestCells cells {};
    const auto carpet =
        to_block_id(BlockType::BackroomsCarpet);
    for (int z = 0; z < 4; ++z) {
        for (int x = 0; x < 4; ++x) {
            set_cell(cells, x, 0, z, carpet, 0, 0);
            set_cell(
                cells,
                x,
                1,
                z,
                to_block_id(BlockType::Air),
                0,
                static_cast<std::uint8_t>(
                    x == 0 && z == 0 ? 15 : 0));
        }
    }

    const auto mesh = ArchitecturalMesher {}.build_mesh(
        ArchitecturalSection {{0, 0, 0}, {3, 0, 3}, 1},
        make_sampler(cells));
    std::vector<const ArchitecturalQuad*> floors;
    for (const auto& quad : mesh.quads) {
        if (quad.face == ArchitecturalFace::PositiveY) {
            floors.push_back(&quad);
        }
    }

    REQUIRE(floors.size() > 1U);
    CHECK(floors.size() <= 16U);
    const auto corner = std::find_if(
        floors.begin(),
        floors.end(),
        [](const ArchitecturalQuad* floor) {
            return floor->owner_cell.x == 0 &&
                   floor->owner_cell.z == 0;
        });
    REQUIRE(corner != floors.end());
    CHECK((*corner)->width == 1U);
    CHECK((*corner)->height == 1U);
    auto covered_cells = std::uint32_t {0U};
    for (const auto* floor : floors) {
        const auto area =
            static_cast<std::uint32_t>(floor->width) *
            static_cast<std::uint32_t>(floor->height);
        covered_cells += area;
        if (area > 1U) {
            CHECK(
                maximum_floor_visibility_error(
                    mesh,
                    *floor,
                    [](int x, int z) {
                        return static_cast<std::uint8_t>(
                            x == 0 && z == 0 ? 15 : 0);
                    }) <= 0.060001F);
        }
    }
    CHECK(covered_cells == 16U);
    check_mesh_integrity(mesh);
}

TEST_CASE("une faible anomalie Backrooms non affine ne reste pas dans un grand quad") {
    const auto carpet =
        to_block_id(BlockType::BackroomsCarpet);
    for (const auto anomaly_in_sky : {false, true}) {
        TestCells cells {};
        for (int z = 0; z < 4; ++z) {
            for (int x = 0; x < 4; ++x) {
                const auto anomaly = static_cast<std::uint8_t>(
                    x == 0 && z == 0 ? 1 : 0);
                set_cell(cells, x, 0, z, carpet, 0, 0);
                set_cell(
                    cells,
                    x,
                    1,
                    z,
                    to_block_id(BlockType::Air),
                    anomaly_in_sky ? anomaly : 0U,
                    anomaly_in_sky ? 0U : anomaly);
            }
        }

        const auto mesh = ArchitecturalMesher {}.build_mesh(
            ArchitecturalSection {{0, 0, 0}, {3, 0, 3}, 1},
            make_sampler(cells));
        std::vector<const ArchitecturalQuad*> floors;
        for (const auto& quad : mesh.quads) {
            if (quad.face == ArchitecturalFace::PositiveY) {
                floors.push_back(&quad);
            }
        }

        CAPTURE(anomaly_in_sky);
        REQUIRE(floors.size() > 1U);
        CHECK(floors.size() <= 16U);
        auto covered_cells = std::uint32_t {0U};
        for (const auto* floor : floors) {
            const auto area =
                static_cast<std::uint32_t>(floor->width) *
                static_cast<std::uint32_t>(floor->height);
            covered_cells += area;
            if (area <= 1U) {
                continue;
            }
            CHECK(quad_light_channel_is_affine(
                mesh,
                *floor,
                [](const HardSurfaceVertex& vertex) noexcept {
                    return vertex.sky_light;
                }));
            CHECK(quad_light_channel_is_affine(
                mesh,
                *floor,
                [](const HardSurfaceVertex& vertex) noexcept {
                    return vertex.block_light;
                }));
        }
        CHECK(covered_cells == 16U);
        check_mesh_integrity(mesh);
    }
}

TEST_CASE("les nappes Backrooms conservent leur diagonale adaptative") {
    TestCells cells {};
    const auto carpet =
        to_block_id(BlockType::BackroomsCarpet);
    set_cell(cells, 0, 0, 0, carpet, 0, 0);
    set_cell(cells, 0, 1, 0, to_block_id(BlockType::Air), 0, 0);
    for (const auto coordinate : std::array<BlockCoord, 2> {{
             {-1, 0, -1},
             {1, 0, 1},
         }}) {
        set_cell(
            cells,
            coordinate.x,
            coordinate.y,
            coordinate.z,
            carpet,
            0,
            0);
        set_cell(
            cells,
            coordinate.x,
            coordinate.y + 1,
            coordinate.z,
            to_block_id(BlockType::Air),
            0,
            15);
    }

    const auto mesh = ArchitecturalMesher {}.build_mesh(
        ArchitecturalSection {{0, 0, 0}, {0, 0, 0}, 1},
        make_sampler(cells));
    const auto floor = std::find_if(
        mesh.quads.begin(),
        mesh.quads.end(),
        [](const ArchitecturalQuad& quad) {
            return quad.face == ArchitecturalFace::PositiveY;
        });

    REQUIRE(floor != mesh.quads.end());
    const auto first_vertex = floor->first_vertex;
    const auto first_index = floor->first_index;
    const std::array<std::uint32_t, 6> expected {{
        first_vertex,
        first_vertex + 1U,
        first_vertex + 3U,
        first_vertex + 1U,
        first_vertex + 2U,
        first_vertex + 3U,
    }};
    REQUIRE(first_index + expected.size() <= mesh.indices.size());
    CHECK(std::equal(
        expected.begin(),
        expected.end(),
        mesh.indices.begin() + first_index));
    check_mesh_integrity(mesh);
}

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

TEST_CASE("architectural mesher validates numeric boundaries before sampling") {
    constexpr auto maximum = std::numeric_limits<int>::max();
    constexpr auto minimum = std::numeric_limits<int>::lowest();
    auto sample_count = std::size_t {0U};
    const ArchitecturalSampler sampler =
        [&sample_count](int, int, int) {
            ++sample_count;
            return ArchitecturalCellSample {};
        };

    // Je conserve une section valide dont le halo atteint exactement INT_MAX :
    // les compteurs 64 bits terminent la boucle sans increment signe invalide.
    const ArchitecturalSection upper_boundary {
        {maximum - 1, 0, 0},
        {maximum - 1, 0, 0},
        1,
    };
    const auto upper_mesh = ArchitecturalMesher {}.build_mesh(
        upper_boundary,
        sampler);
    CHECK(upper_mesh.empty());
    CHECK(sample_count == 27U);

    sample_count = 0U;
    const ArchitecturalSection overflowing_upper {
        {maximum, 0, 0},
        {maximum, 0, 0},
        1,
    };
    CHECK_THROWS_AS(
        static_cast<void>(ArchitecturalMesher {}.build_mesh(
            overflowing_upper,
            sampler)),
        std::overflow_error);
    CHECK(sample_count == 0U);

    const ArchitecturalSection overflowing_lower {
        {minimum, 0, 0},
        {minimum, 0, 0},
        1,
    };
    CHECK_THROWS_AS(
        static_cast<void>(ArchitecturalMesher {}.build_mesh(
            overflowing_lower,
            sampler)),
        std::overflow_error);
    CHECK(sample_count == 0U);
}

TEST_CASE("architectural mesher enforces axis and sampled-volume budgets") {
    auto sample_count = std::size_t {0U};
    const ArchitecturalSampler sampler =
        [&sample_count](int, int, int) {
            ++sample_count;
            return ArchitecturalCellSample {};
        };

    const ArchitecturalSection oversized_axis {
        {0, 0, 0},
        {255, 0, 0},
        1,
    };
    CHECK_THROWS_AS(
        static_cast<void>(ArchitecturalMesher {}.build_mesh(
            oversized_axis,
            sampler)),
        std::length_error);
    CHECK(sample_count == 0U);

    // Je reste sous quatre millions de cellules utiles, mais le halo ferait
    // depasser le budget d'echantillonnage : la reservation doit etre refusee.
    const ArchitecturalSection oversized_halo_volume {
        {0, 0, 0},
        {254, 254, 63},
        1,
    };
    CHECK_THROWS_AS(
        static_cast<void>(ArchitecturalMesher {}.build_mesh(
            oversized_halo_volume,
            sampler)),
        std::length_error);
    CHECK(sample_count == 0U);

    // Je refuse aussi un coeur dont le pire cas de sortie depasserait le
    // budget, meme lorsque son volume echantillonne resterait raisonnable.
    const ArchitecturalSection oversized_output {
        {0, 0, 0},
        {40, 40, 40},
        1,
    };
    CHECK_THROWS_AS(
        static_cast<void>(ArchitecturalMesher {}.build_mesh(
            oversized_output,
            sampler)),
        std::length_error);
    CHECK(sample_count == 0U);

    // Je couvre aussi la frontiere positive exacte avec un sampler vide :
    // 64 x 32 x 32 vaut exactement 65 536 cellules de coeur.
    const ArchitecturalSection exact_output_budget {
        {0, 0, 0},
        {63, 31, 31},
        1,
    };
    const auto exact_mesh = ArchitecturalMesher {}.build_mesh(
        exact_output_budget,
        sampler);
    CHECK(exact_mesh.empty());
    CHECK(sample_count == 66U * 34U * 34U);
}

TEST_CASE("architectural mesh growth rejects excessive prefixes without allocation") {
    const auto exact_limit = checked_architectural_mesh_growth(
        kMaximumArchitecturalMeshVertices,
        kMaximumArchitecturalMeshIndices,
        0U,
        0U);
    CHECK(
        exact_limit.vertex_count ==
        kMaximumArchitecturalMeshVertices);
    CHECK(
        exact_limit.index_count ==
        kMaximumArchitecturalMeshIndices);

    CHECK_THROWS_AS(
        static_cast<void>(checked_architectural_mesh_growth(
            kMaximumArchitecturalMeshVertices + 1U,
            0U,
            0U,
            0U)),
        std::length_error);
    CHECK_THROWS_AS(
        static_cast<void>(checked_architectural_mesh_growth(
            0U,
            kMaximumArchitecturalMeshIndices + 1U,
            0U,
            0U)),
        std::length_error);
    CHECK_THROWS_AS(
        static_cast<void>(checked_architectural_mesh_growth(
            kMaximumArchitecturalMeshVertices,
            kMaximumArchitecturalMeshIndices,
            1U,
            1U)),
        std::length_error);
}

TEST_CASE("architectural mesher clamps untrusted reserve hints") {
    auto sample_count = std::size_t {0U};
    const ArchitecturalSection one_cell {{0, 0, 0}, {0, 0, 0}, 1};

    // Je passe volontairement SIZE_MAX : le mesher doit reserver seulement
    // le pire cas des six faces de cette cellule, sans tentative geante.
    const auto mesh = ArchitecturalMesher {}.build_mesh(
        one_cell,
        [&sample_count](int, int, int) {
            ++sample_count;
            return ArchitecturalCellSample {};
        },
        std::numeric_limits<std::size_t>::max(),
        std::numeric_limits<std::size_t>::max());

    CHECK(mesh.empty());
    CHECK(sample_count == 27U);
}

} // namespace valcraft
