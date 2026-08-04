#include "render/BackroomsPropMesh.h"
#include "world/ChunkMesher.h"
#include "world/World.h"

#include "TestUtils.h"

#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>

namespace valcraft {

namespace {

constexpr std::array<BlockType, 6> kModernHardSurfaceProps {{
    BlockType::BackroomsDesk,
    BlockType::BackroomsChair,
    BlockType::BackroomsRampPositiveX,
    BlockType::BackroomsRampNegativeX,
    BlockType::BackroomsRampPositiveZ,
    BlockType::BackroomsRampNegativeZ,
}};

[[nodiscard]] auto world_architectural_sampler(World& world)
    -> ArchitecturalSampler {
    return [&world](int x, int y, int z) {
        return ArchitecturalCellSample {
            world.get_block(x, y, z),
            world.get_sky_light(x, y, z),
            world.get_block_light(x, y, z),
        };
    };
}

[[nodiscard]] auto section_zero() noexcept -> ArchitecturalSection {
    return {
        {0, 0, 0},
        {kChunkSizeX - 1, 15, kChunkSizeZ - 1},
        1,
    };
}

} // namespace

TEST_CASE("les accessoires solides Backrooms ont un routage moderne explicite") {
    for (const auto type : kModernHardSurfaceProps) {
        CAPTURE(static_cast<int>(type));
        CHECK(is_modern_backrooms_hard_surface_prop(to_block_id(type)));
    }
    CHECK_FALSE(is_modern_backrooms_hard_surface_prop(
        to_block_id(BlockType::BackroomsPlant)));
    CHECK_FALSE(is_modern_backrooms_hard_surface_prop(
        to_block_id(BlockType::PoolroomsFloat)));
    CHECK_FALSE(is_modern_backrooms_hard_surface_prop(
        to_block_id(BlockType::BackroomsWallYellow)));
    CHECK_FALSE(is_modern_backrooms_hard_surface_prop(
        to_block_id(BlockType::BackroomsConnectorStep)));
}

TEST_CASE("World emet chaque accessoire Backrooms exactement une fois hors du mesh voxel") {
    constexpr BlockCoord owner {4, 10, 6};
    for (std::size_t prop_index = 0U;
         prop_index < kModernHardSurfaceProps.size();
         ++prop_index) {
        const auto block_id = to_block_id(kModernHardSurfaceProps[prop_index]);
        World world(
            9280 + static_cast<int>(prop_index),
            0,
            WorldGenerationProfile::Backrooms,
            WorldGenerationVersion::BackroomsV2,
            VisualPipeline::ModernStylized,
            0);
        test::make_chunk_empty(world, {0, 0});
        world.set_block(owner.x, owner.y, owner.z, block_id);
        world.rebuild_dirty_meshes();

        const auto* voxel_sections = world.section_meshes_for({0, 0});
        const auto* architecture_sections =
            world.architectural_section_meshes_for({0, 0});
        REQUIRE(voxel_sections != nullptr);
        REQUIRE(architecture_sections != nullptr);
        CAPTURE(static_cast<int>(block_id));
        for (const auto& voxel_mesh : *voxel_sections) {
            CHECK(voxel_mesh.vertices.empty());
            CHECK(voxel_mesh.indices.empty());
        }

        ArchitecturalMesh expected;
        const auto sampler = world_architectural_sampler(world);
        const auto first_index = append_modern_backrooms_prop_geometry(
            expected,
            section_zero(),
            sampler,
            StylizedPrimitiveLod::Medium);
        REQUIRE(first_index == 0U);
        REQUIRE_FALSE(expected.empty());
        REQUIRE(expected.bounds.valid);
        CHECK(expected.bounds.min_x >= static_cast<float>(owner.x));
        CHECK(expected.bounds.min_y >= static_cast<float>(owner.y));
        CHECK(expected.bounds.min_z >= static_cast<float>(owner.z));
        CHECK(expected.bounds.max_x <= static_cast<float>(owner.x + 1));
        CHECK(expected.bounds.max_y <= static_cast<float>(owner.y + 1));
        CHECK(expected.bounds.max_z <= static_cast<float>(owner.z + 1));

        // Je compare le contenu complet, pas seulement un compteur : une
        // seconde emission, une forme voxel ou un mauvais materiau echoue ici.
        CHECK((*architecture_sections)[0] == expected);
        CHECK(
            architectural_mesh_deterministic_hash(
                (*architecture_sections)[0]) ==
            architectural_mesh_deterministic_hash(expected));
        for (std::size_t section_index = 1U;
             section_index < architecture_sections->size();
             ++section_index) {
            CHECK((*architecture_sections)[section_index].empty());
        }
    }
}

TEST_CASE("les rampes hard-surface restent des prismes inclines valides") {
    constexpr BlockCoord owner {4, 10, 6};
    for (const auto type : std::array {
             BlockType::BackroomsRampPositiveX,
             BlockType::BackroomsRampNegativeX,
             BlockType::BackroomsRampPositiveZ,
             BlockType::BackroomsRampNegativeZ,
         }) {
        const auto block_id = to_block_id(type);
        ArchitecturalMesh mesh;
        const ArchitecturalSampler sampler =
            [block_id, owner](int x, int y, int z) {
                return ArchitecturalCellSample {
                    x == owner.x && y == owner.y && z == owner.z
                        ? block_id
                        : to_block_id(BlockType::Air),
                    15U,
                    0U,
                };
            };
        [[maybe_unused]] const auto first_added_index =
            append_modern_backrooms_prop_geometry(
            mesh,
            section_zero(),
            sampler,
            StylizedPrimitiveLod::Medium);

        CAPTURE(static_cast<int>(block_id));
        CHECK(mesh.vertices.size() == 18U);
        CHECK(mesh.indices.size() == 24U);
        CHECK(mesh.triangle_count() == 8U);
        CHECK(mesh.quads.empty());
        CHECK(mesh.fixtures.empty());
        std::size_t slope_vertex_count = 0U;
        for (const auto& vertex : mesh.vertices) {
            const auto normal_length = std::sqrt(
                vertex.nx * vertex.nx +
                vertex.ny * vertex.ny +
                vertex.nz * vertex.nz);
            CHECK(normal_length == doctest::Approx(1.0F).epsilon(0.0001F));
            CHECK(vertex.material_block == block_id);
            if (vertex.ny > 0.70F && vertex.ny < 0.72F) {
                ++slope_vertex_count;
                const auto local_x = vertex.x - static_cast<float>(owner.x);
                const auto local_z = vertex.z - static_cast<float>(owner.z);
                CHECK(
                    vertex.y - static_cast<float>(owner.y) ==
                    doctest::Approx(backrooms_ramp_surface_height(
                        block_id,
                        local_x,
                        local_z)));
            }
        }
        CHECK(slope_vertex_count == 4U);
    }
}

TEST_CASE("le mesher de props refuse les sections hors budget avant le sampler") {
    auto sample_count = std::size_t {0U};
    const ArchitecturalSampler sampler =
        [&sample_count](int, int, int) {
            ++sample_count;
            return ArchitecturalCellSample {};
        };
    ArchitecturalMesh mesh;

    const ArchitecturalSection oversized {
        {0, 0, 0},
        {16, 15, 15},
        1,
    };
    CHECK_THROWS_AS(
        static_cast<void>(append_modern_backrooms_prop_geometry(
            mesh,
            oversized,
            sampler)),
        std::length_error);
    CHECK(sample_count == 0U);
    CHECK(mesh.empty());

    constexpr auto maximum = std::numeric_limits<int>::max();
    const ArchitecturalSection overflowing_halo {
        {maximum, 0, 0},
        {maximum, 0, 0},
        1,
    };
    CHECK_THROWS_AS(
        static_cast<void>(append_modern_backrooms_prop_geometry(
            mesh,
            overflowing_halo,
            sampler)),
        std::overflow_error);
    CHECK(sample_count == 0U);
    CHECK(mesh.empty());
}

TEST_CASE("le mesher de props borne la densite avant de modifier le mesh") {
    auto sample_count = std::size_t {0U};
    const ArchitecturalSampler dense_sampler =
        [&sample_count](int, int, int) {
            ++sample_count;
            return ArchitecturalCellSample {
                to_block_id(BlockType::BackroomsDesk),
                15U,
                0U,
            };
        };
    const ArchitecturalSection dense_section {
        {0, 0, 0},
        {8, 7, 7},
        1,
    };
    ArchitecturalMesh mesh;
    mesh.vertices.push_back(HardSurfaceVertex {});
    mesh.indices.push_back(0U);
    mesh.bounds.valid = true;
    const auto before = mesh;

    CHECK_THROWS_AS(
        static_cast<void>(append_modern_backrooms_prop_geometry(
            mesh,
            dense_section,
            dense_sampler,
            StylizedPrimitiveLod::High)),
        std::length_error);
    CHECK(sample_count == 513U);
    CHECK(mesh == before);
}

} // namespace valcraft
