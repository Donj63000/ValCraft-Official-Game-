#include "world/BlockVisuals.h"
#include "world/ChunkMesher.h"
#include "world/World.h"

#include "TestUtils.h"

#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace valcraft {

namespace {

struct RampExpectation {
    BlockType type;
    BlockCoord rise;
};

constexpr std::array<RampExpectation, 4> kRampExpectations {{
    {BlockType::BackroomsRampPositiveX, {1, 0, 0}},
    {BlockType::BackroomsRampNegativeX, {-1, 0, 0}},
    {BlockType::BackroomsRampPositiveZ, {0, 0, 1}},
    {BlockType::BackroomsRampNegativeZ, {0, 0, -1}},
}};

[[nodiscard]] auto triangle_winding_matches_normal(
    const ChunkVertex& first,
    const ChunkVertex& second,
    const ChunkVertex& third) -> bool {
    const auto edge_a_x = second.x - first.x;
    const auto edge_a_y = second.y - first.y;
    const auto edge_a_z = second.z - first.z;
    const auto edge_b_x = third.x - first.x;
    const auto edge_b_y = third.y - first.y;
    const auto edge_b_z = third.z - first.z;
    const auto cross_x = edge_a_y * edge_b_z - edge_a_z * edge_b_y;
    const auto cross_y = edge_a_z * edge_b_x - edge_a_x * edge_b_z;
    const auto cross_z = edge_a_x * edge_b_y - edge_a_y * edge_b_x;
    return cross_x * first.nx + cross_y * first.ny + cross_z * first.nz >
           0.0001F;
}

} // namespace

TEST_CASE("les blocs de connexion Backrooms restent append-only et exposent leurs propriétés") {
    CHECK(to_block_id(BlockType::BackroomsConnectorStep) == 64U);
    CHECK(to_block_id(BlockType::BackroomsRampPositiveX) == 65U);
    CHECK(to_block_id(BlockType::BackroomsRampNegativeX) == 66U);
    CHECK(to_block_id(BlockType::BackroomsRampPositiveZ) == 67U);
    CHECK(to_block_id(BlockType::BackroomsRampNegativeZ) == 68U);
    CHECK(is_known_block_id(68U));
    CHECK_FALSE(is_known_block_id(69U));

    const auto step_id = to_block_id(BlockType::BackroomsConnectorStep);
    const auto step = block_properties(step_id);
    CHECK(is_backrooms_connector_step(step_id));
    CHECK_FALSE(is_backrooms_ramp(step_id));
    CHECK(step.opaque);
    CHECK(step.collidable);
    CHECK(step.surface_support);
    CHECK_FALSE(step.replaceable);
    CHECK(step.mesh_type == BlockMeshType::FullCube);

    for (const auto& expectation : kRampExpectations) {
        const auto block_id = to_block_id(expectation.type);
        const auto properties = block_properties(block_id);
        CAPTURE(static_cast<int>(block_id));
        CHECK(is_backrooms_ramp(block_id));
        CHECK_FALSE(is_backrooms_connector_step(block_id));
        CHECK(backrooms_ramp_rise_direction(block_id) == expectation.rise);
        CHECK_FALSE(properties.opaque);
        CHECK_FALSE(properties.collidable);
        CHECK(properties.surface_support);
        CHECK_FALSE(properties.replaceable);
        CHECK(properties.mesh_type == BlockMeshType::Ramp);
        CHECK(has_block_mesh(block_id));
    }
}

TEST_CASE("la hauteur locale des rampes suit exactement leur direction de montée") {
    CHECK(backrooms_ramp_surface_height(
              to_block_id(BlockType::BackroomsRampPositiveX),
              0.25F,
              0.75F) == doctest::Approx(0.25F));
    CHECK(backrooms_ramp_surface_height(
              to_block_id(BlockType::BackroomsRampNegativeX),
              0.25F,
              0.75F) == doctest::Approx(0.75F));
    CHECK(backrooms_ramp_surface_height(
              to_block_id(BlockType::BackroomsRampPositiveZ),
              0.25F,
              0.75F) == doctest::Approx(0.75F));
    CHECK(backrooms_ramp_surface_height(
              to_block_id(BlockType::BackroomsRampNegativeZ),
              0.25F,
              0.75F) == doctest::Approx(0.25F));

    // Je vérifie aussi le contrat aux frontières pour éviter une marche
    // numérique lorsque le joueur traverse deux cellules consécutives.
    const auto positive_x = to_block_id(BlockType::BackroomsRampPositiveX);
    CHECK(backrooms_ramp_surface_height(positive_x, -3.0F, 0.5F) ==
          doctest::Approx(0.0F));
    CHECK(backrooms_ramp_surface_height(positive_x, 4.0F, 0.5F) ==
          doctest::Approx(1.0F));
    CHECK(backrooms_ramp_surface_height(
              to_block_id(BlockType::Stone),
              0.5F,
              0.5F) == doctest::Approx(0.0F));
}

TEST_CASE("les connexions réemploient le béton Backrooms sans nouvelle tuile") {
    const auto concrete = to_block_id(BlockType::BackroomsConcrete);
    constexpr std::array<BlockVisualFace, 6> faces {{
        BlockVisualFace::PositiveX,
        BlockVisualFace::NegativeX,
        BlockVisualFace::PositiveY,
        BlockVisualFace::NegativeY,
        BlockVisualFace::PositiveZ,
        BlockVisualFace::NegativeZ,
    }};

    for (const auto& expectation : kRampExpectations) {
        const auto block_id = to_block_id(expectation.type);
        CAPTURE(static_cast<int>(block_id));
        for (const auto face : faces) {
            CHECK(block_atlas_tile(block_id, face) ==
                  block_atlas_tile(concrete, face));
        }
        CHECK(block_visual_material(block_id) == BlockVisualMaterial::Rock);
    }

    const auto step_id = to_block_id(BlockType::BackroomsConnectorStep);
    CHECK(block_atlas_tile(step_id, BlockVisualFace::PositiveY) ==
          block_atlas_tile(concrete, BlockVisualFace::PositiveY));
    CHECK(block_visual_material(step_id) == BlockVisualMaterial::Rock);
}

TEST_CASE("le ChunkMesher produit un prisme triangulaire valide pour chaque rampe") {
    constexpr BlockCoord block_coord {4, 10, 6};
    for (std::size_t ramp_index = 0;
         ramp_index < kRampExpectations.size();
         ++ramp_index) {
        const auto& expectation = kRampExpectations[ramp_index];
        const auto block_id = to_block_id(expectation.type);
        World world(9120 + static_cast<int>(ramp_index), 1);
        test::make_chunk_empty(world, {0, 0});
        world.set_block(
            block_coord.x,
            block_coord.y,
            block_coord.z,
            block_id);

        const auto mesh = ChunkMesher {}.build_mesh(world, {0, 0});
        CAPTURE(static_cast<int>(block_id));
        CHECK(mesh.face_count == 5U);
        CHECK(mesh.vertices.size() == 18U);
        CHECK(mesh.indices.size() == 24U);
        CHECK(mesh.water_vertices.empty());
        CHECK(mesh.water_indices.empty());

        std::size_t slope_vertex_count = 0U;
        for (const auto& vertex : mesh.vertices) {
            CHECK(vertex.x >= static_cast<float>(block_coord.x));
            CHECK(vertex.x <= static_cast<float>(block_coord.x + 1));
            CHECK(vertex.y >= static_cast<float>(block_coord.y));
            CHECK(vertex.y <= static_cast<float>(block_coord.y + 1));
            CHECK(vertex.z >= static_cast<float>(block_coord.z));
            CHECK(vertex.z <= static_cast<float>(block_coord.z + 1));
            CHECK(vertex.material_class == doctest::Approx(
                      block_visual_material_value(BlockVisualMaterial::Rock)));

            const auto normal_length = std::sqrt(
                vertex.nx * vertex.nx +
                vertex.ny * vertex.ny +
                vertex.nz * vertex.nz);
            CHECK(normal_length == doctest::Approx(1.0F).epsilon(0.0001F));

            if (vertex.ny > 0.7F && vertex.ny < 0.8F) {
                ++slope_vertex_count;
                const auto local_x = vertex.x - static_cast<float>(block_coord.x);
                const auto local_z = vertex.z - static_cast<float>(block_coord.z);
                CHECK(
                    vertex.y - static_cast<float>(block_coord.y) ==
                    doctest::Approx(backrooms_ramp_surface_height(
                        block_id,
                        local_x,
                        local_z)));
                CHECK(vertex.nx == doctest::Approx(
                          -static_cast<float>(expectation.rise.x) /
                          std::sqrt(2.0F)));
                CHECK(vertex.nz == doctest::Approx(
                          -static_cast<float>(expectation.rise.z) /
                          std::sqrt(2.0F)));
            }
        }
        CHECK(slope_vertex_count == 4U);

        for (std::size_t index = 0; index < mesh.indices.size(); index += 3U) {
            const auto first_index = mesh.indices[index];
            const auto second_index = mesh.indices[index + 1U];
            const auto third_index = mesh.indices[index + 2U];
            REQUIRE(static_cast<std::size_t>(first_index) < mesh.vertices.size());
            REQUIRE(static_cast<std::size_t>(second_index) < mesh.vertices.size());
            REQUIRE(static_cast<std::size_t>(third_index) < mesh.vertices.size());
            CHECK(triangle_winding_matches_normal(
                mesh.vertices[first_index],
                mesh.vertices[second_index],
                mesh.vertices[third_index]));
        }
    }
}

TEST_CASE("le ChunkMesher retire les faces internes de deux rampes parallèles") {
    World world(9130U, 1);
    test::make_chunk_empty(world, {0, 0});
    const auto ramp = to_block_id(BlockType::BackroomsRampPositiveX);
    world.set_block(4, 10, 6, ramp);
    world.set_block(4, 10, 7, ramp);

    const auto mesh = ChunkMesher {}.build_mesh(world, {0, 0});

    // Je retire les deux triangles latéraux superposés, un de chaque rampe.
    CHECK(mesh.face_count == 8U);
    CHECK(mesh.vertices.size() == 30U);
    CHECK(mesh.indices.size() == 42U);
}

TEST_CASE("le pipeline moderne conserve la geometrie inclinee des rampes") {
    World world(
        9135U,
        1,
        WorldGenerationProfile::Backrooms,
        WorldGenerationVersion::BackroomsV2,
        VisualPipeline::ModernStylized,
        0);
    test::make_chunk_empty(world, {0, 0});
    world.set_block(
        4,
        10,
        6,
        to_block_id(BlockType::BackroomsRampPositiveX));

    // Je reproduis exactement le filtre voxel résiduel du pipeline moderne :
    // la rampe doit rester un prisme incliné et ne jamais devenir un cube ou
    // disparaître au profit des meshers organique et architectural.
    const auto mesh = ChunkMesher {}.build_mesh_range(
        world,
        {0, 0},
        0,
        15,
        0U,
        0U,
        ChunkMeshContent::ModernNonOrganic);

    CHECK(mesh.face_count == 5U);
    CHECK(mesh.vertices.size() == 18U);
    CHECK(mesh.indices.size() == 24U);
}

TEST_CASE("le pipeline moderne traite le bois place dans les Backrooms comme une architecture") {
    World world(
        9136U,
        0,
        WorldGenerationProfile::Backrooms,
        WorldGenerationVersion::BackroomsV2,
        VisualPipeline::ModernStylized,
        0);
    test::make_chunk_empty(world, {0, 0});
    world.set_block(4, 10, 6, to_block_id(BlockType::Wood));
    world.rebuild_dirty_meshes();

    const auto* vegetation =
        world.organic_section_meshes_for({0, 0});
    const auto* architecture =
        world.architectural_section_meshes_for({0, 0});
    REQUIRE(vegetation != nullptr);
    REQUIRE(architecture != nullptr);
    for (const auto& section : *vegetation) {
        CHECK(section.empty());
    }
    CHECK_FALSE((*architecture)[0].empty());
}

TEST_CASE("la marche de connecteur conserve le maillage cubique plein") {
    World world(9140U, 1);
    test::make_chunk_empty(world, {0, 0});
    world.set_block(
        4,
        10,
        6,
        to_block_id(BlockType::BackroomsConnectorStep));

    const auto mesh = ChunkMesher {}.build_mesh(world, {0, 0});

    CHECK(mesh.face_count == 6U);
    CHECK(mesh.vertices.size() == 24U);
    CHECK(mesh.indices.size() == 36U);
}

} // namespace valcraft
