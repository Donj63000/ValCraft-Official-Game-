#include "render/TerrainVisualSample.h"
#include "render/VisualPipeline.h"
#include "world/World.h"

#include <doctest/doctest.h>

#include <cmath>
#include <cstdint>

namespace valcraft {
namespace {

[[nodiscard]] auto flat_test_mesh() -> OrganicTerrainMesh {
    OrganicTerrainMesh mesh {};
    const auto grass = to_block_id(BlockType::Grass);
    const auto dirt = to_block_id(BlockType::Dirt);
    mesh.vertices = {
        {0.0F, 2.0F, 0.0F, 0.0F, 1.0F, 0.0F,
         grass, dirt, 64U, 224U, 15U, 2U, 0U},
        {1.0F, 2.0F, 0.0F, 0.0F, 1.0F, 0.0F,
         grass, dirt, 64U, 224U, 15U, 2U, 0U},
        {0.0F, 2.0F, 1.0F, 0.0F, 1.0F, 0.0F,
         grass, dirt, 64U, 224U, 15U, 2U, 0U},
    };
    mesh.indices = {0U, 1U, 2U};
    return mesh;
}

} // namespace

TEST_CASE("terrain visual sampling interpolates a render-only organic surface") {
    const auto mesh = flat_test_mesh();
    const auto sample = sample_terrain_visual_mesh(
        mesh,
        {{0.25F, 2.6F, 0.25F}, 1.0F, 0.1F},
        42U);

    REQUIRE(sample.has_value());
    CHECK(sample->position.x == doctest::Approx(0.25F));
    CHECK(sample->position.y == doctest::Approx(2.0F));
    CHECK(sample->position.z == doctest::Approx(0.25F));
    CHECK(sample->normal.x == doctest::Approx(0.0F));
    CHECK(sample->normal.y == doctest::Approx(1.0F));
    CHECK(sample->normal.z == doctest::Approx(0.0F));
    CHECK(sample->primary_material == VisualMaterialId::MeadowGrass);
    CHECK(sample->secondary_material == VisualMaterialId::Loam);
    CHECK(sample->material_blend == doctest::Approx(64.0F / 255.0F));
    CHECK(sample->ambient_occlusion == doctest::Approx(224.0F / 255.0F));
    CHECK(sample->sky_light == doctest::Approx(1.0F));
    CHECK(sample->block_light == doctest::Approx(2.0F / 15.0F));
    CHECK(sample->distance_squared == doctest::Approx(0.36F));
    CHECK(sample->mesh_revision == 42U);
}

TEST_CASE("terrain visual sampling ignores packed UVs on cutout vertices") {
    auto mesh = flat_test_mesh();
    for (auto& vertex : mesh.vertices) {
        vertex.primary_block_id =
            to_block_id(BlockType::TallGrass);
        // Sur une carte alpha, ces deux octets contiennent U et V en UNORM8.
        vertex.secondary_block_id = 255U;
        vertex.material_blend = 255U;
        vertex.surface_flags = 1U;
    }

    const auto sample = sample_terrain_visual_mesh(
        mesh,
        {{0.25F, 2.25F, 0.25F}, 1.0F, -1.0F});

    REQUIRE(sample.has_value());
    CHECK(
        sample->primary_material ==
        VisualMaterialId::TallGrass);
    CHECK(
        sample->secondary_material ==
        VisualMaterialId::None);
    CHECK(sample->material_blend == 0.0F);
}

TEST_CASE("terrain visual sampling rejects invalid distant and back-facing queries") {
    const auto mesh = flat_test_mesh();

    CHECK_FALSE(sample_terrain_visual_mesh(
        mesh,
        {{0.25F, 8.0F, 0.25F}, 1.0F, -1.0F}));
    CHECK_FALSE(sample_terrain_visual_mesh(
        mesh,
        {{0.25F, 2.2F, 0.25F}, 1.0F, 1.01F}));
    CHECK_FALSE(sample_terrain_visual_mesh(
        mesh,
        {{0.25F, 2.2F, 0.25F}, -1.0F, -1.0F}));
}

TEST_CASE("terrain visual sampling is deterministic and ignores malformed triangles") {
    auto mesh = flat_test_mesh();
    mesh.indices.insert(mesh.indices.end(), {0U, 1U, 99U});
    const TerrainVisualQuery query {{0.4F, 2.2F, 0.2F}, 1.0F, -1.0F};

    const auto first = sample_terrain_visual_mesh(mesh, query, 7U);
    const auto second = sample_terrain_visual_mesh(mesh, query, 7U);

    REQUIRE(first.has_value());
    CHECK(first == second);
    CHECK(std::isfinite(first->distance_squared));
}

TEST_CASE("world exposes visual terrain sampling only for the modern render pipeline") {
    World modern(
        24'681,
        0,
        WorldGenerationProfile::Continental,
        WorldGenerationVersion::Latest,
        VisualPipeline::ModernStylized);
    modern.ensure_chunk_loaded({0, 0});
    modern.rebuild_dirty_meshes();

    const auto* meshes = modern.organic_section_meshes_for({0, 0});
    REQUIRE(meshes != nullptr);
    const TerrainVertex* reference_vertex = nullptr;
    for (const auto& mesh : *meshes) {
        if (!mesh.vertices.empty()) {
            reference_vertex = &mesh.vertices.front();
            break;
        }
    }
    REQUIRE(reference_vertex != nullptr);

    const TerrainVisualQuery query {
        {
            reference_vertex->x,
            reference_vertex->y,
            reference_vertex->z,
        },
        0.25F,
        -1.0F,
    };
    const auto first = modern.sample_visual_terrain(query);
    const auto second = modern.sample_visual_terrain(query);
    REQUIRE(first.has_value());
    CHECK(first == second);
    CHECK(first->distance_squared == doctest::Approx(0.0F));
    CHECK(first->mesh_revision == modern.mesh_revision({0, 0}));

    World legacy(
        24'681,
        0,
        WorldGenerationProfile::Continental,
        WorldGenerationVersion::Latest,
        VisualPipeline::LegacyVoxel);
    legacy.ensure_chunk_loaded({0, 0});
    legacy.rebuild_dirty_meshes();
    CHECK_FALSE(legacy.sample_visual_terrain(query));
}

} // namespace valcraft
