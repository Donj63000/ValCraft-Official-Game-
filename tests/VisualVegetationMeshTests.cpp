#include "render/VisualVegetationMesh.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <numeric>
#include <tuple>

namespace valcraft {

namespace {

using TestBlocks = std::map<std::tuple<int, int, int>, BlockId>;

[[nodiscard]] auto test_sampler(const TestBlocks& blocks)
    -> VisualVegetationSampler {
    return [&blocks](int x, int y, int z) {
        const auto found = blocks.find({x, y, z});
        return found == blocks.end()
                   ? to_block_id(BlockType::Air)
                   : found->second;
    };
}

[[nodiscard]] auto exact_mesh_equal(
    const OrganicTerrainMesh& lhs,
    const OrganicTerrainMesh& rhs) -> bool {
    return lhs.vertices.size() == rhs.vertices.size() &&
           lhs.indices == rhs.indices &&
           (lhs.vertices.empty() ||
            std::memcmp(
                lhs.vertices.data(),
                rhs.vertices.data(),
                lhs.vertices.size() * sizeof(TerrainVertex)) == 0);
}

void place_oak_crossing_section(
    TestBlocks& blocks,
    int x,
    int base_y,
    int z) {
    const auto wood = to_block_id(BlockType::Wood);
    const auto leaves = to_block_id(BlockType::Leaves);
    for (int y = base_y; y <= base_y + 3; ++y) {
        blocks[{x, y, z}] = wood;
    }
    for (int y = base_y + 3; y <= base_y + 4; ++y) {
        for (int offset_z = -1; offset_z <= 1; ++offset_z) {
            for (int offset_x = -1; offset_x <= 1; ++offset_x) {
                blocks[{x + offset_x, y, z + offset_z}] = leaves;
            }
        }
    }
    blocks[{x, base_y + 3, z}] = wood;
}

void place_pine_crossing_section(
    TestBlocks& blocks,
    int x,
    int base_y,
    int z) {
    const auto wood = to_block_id(BlockType::PineWood);
    const auto leaves = to_block_id(BlockType::PineLeaves);
    for (int y = base_y; y <= base_y + 3; ++y) {
        blocks[{x, y, z}] = wood;
    }
    for (int y = base_y + 3; y <= base_y + 4; ++y) {
        for (int offset_z = -1; offset_z <= 1; ++offset_z) {
            for (int offset_x = -1; offset_x <= 1; ++offset_x) {
                blocks[{x + offset_x, y, z + offset_z}] = leaves;
            }
        }
    }
    blocks[{x, base_y + 3, z}] = wood;
}

[[nodiscard]] auto mesh_is_inside_bounds(
    const OrganicTerrainMesh& mesh,
    const VisualVegetationBounds& bounds) -> bool {
    if (!bounds.valid) {
        return false;
    }
    return std::all_of(
        mesh.vertices.begin(),
        mesh.vertices.end(),
        [&bounds](const TerrainVertex& vertex) {
            constexpr float epsilon = 1.0e-4F;
            return vertex.x >= bounds.min_x - epsilon &&
                   vertex.x <= bounds.max_x + epsilon &&
                   vertex.y >= bounds.min_y - epsilon &&
                   vertex.y <= bounds.max_y + epsilon &&
                   vertex.z >= bounds.min_z - epsilon &&
                   vertex.z <= bounds.max_z + epsilon;
        });
}

} // namespace

TEST_CASE("la végétation convertit ses trois LOD en maillages déterministes valides") {
    TestBlocks blocks {};
    const auto cactus = to_block_id(BlockType::Cactus);
    blocks[{2, 4, 3}] = cactus;
    blocks[{2, 5, 3}] = cactus;
    blocks[{7, 4, 5}] = to_block_id(BlockType::TallGrass);
    const auto build = build_visual_vegetation(
        {{0, 0, 0}, {15, 15, 15}, 1},
        test_sampler(blocks),
        77U);
    const auto lighting = [](int, int, int) {
        return VisualVegetationLighting {13U, 2U};
    };

    std::size_t previous_triangles = std::numeric_limits<std::size_t>::max();
    for (const auto lod : {
             VisualVegetationLod::Near,
             VisualVegetationLod::Medium,
             VisualVegetationLod::Far}) {
        const auto first = build_visual_vegetation_mesh(
            build,
            lod,
            StylizedPrimitiveLod::Low,
            lighting);
        const auto second = build_visual_vegetation_mesh(
            build,
            lod,
            StylizedPrimitiveLod::Low,
            lighting);
        REQUIRE_FALSE(first.empty());
        CHECK(exact_mesh_equal(first, second));
        CHECK(first.triangle_count() <= previous_triangles);
        previous_triangles = first.triangle_count();
        for (const auto index : first.indices) {
            CHECK(index < first.vertices.size());
        }
        for (const auto& vertex : first.vertices) {
            CHECK(std::isfinite(vertex.x));
            CHECK(std::isfinite(vertex.y));
            CHECK(std::isfinite(vertex.z));
            CHECK(std::isfinite(vertex.nx));
            CHECK(std::isfinite(vertex.ny));
            CHECK(std::isfinite(vertex.nz));
            CHECK(vertex.sky_light == 13U);
            CHECK(vertex.block_light == 2U);
            CHECK((vertex.surface_flags & ~1U) == 0U);
        }
    }
}

TEST_CASE("les cartes alpha conservent leurs UV sans agrandir TerrainVertex") {
    TestBlocks blocks {};
    blocks[{4, 4, 4}] = to_block_id(BlockType::TallGrass);
    blocks[{8, 4, 4}] = to_block_id(BlockType::Cactus);
    blocks[{8, 5, 4}] = to_block_id(BlockType::Cactus);

    const auto build = build_visual_vegetation(
        {{0, 0, 0}, {15, 15, 15}, 1},
        test_sampler(blocks),
        0x6D5A4C31U);
    const auto mesh = build_visual_vegetation_mesh(
        build,
        VisualVegetationLod::Near,
        StylizedPrimitiveLod::Low,
        [](int, int, int) {
            return VisualVegetationLighting {15U, 0U};
        });

    REQUIRE_FALSE(mesh.empty());
    CHECK(sizeof(TerrainVertex) == 32U);

    auto cutout_vertex_count = std::size_t {0U};
    auto opaque_vertex_count = std::size_t {0U};
    auto saw_u_zero = false;
    auto saw_u_one = false;
    auto saw_v_zero = false;
    auto saw_v_one = false;
    for (const auto& vertex : mesh.vertices) {
        const auto cutout =
            (vertex.surface_flags & 1U) != 0U;
        if (cutout) {
            ++cutout_vertex_count;
            saw_u_zero =
                saw_u_zero ||
                vertex.secondary_block_id == 0U;
            saw_u_one =
                saw_u_one ||
                vertex.secondary_block_id == 255U;
            saw_v_zero =
                saw_v_zero ||
                vertex.material_blend == 0U;
            saw_v_one =
                saw_v_one ||
                vertex.material_blend == 255U;
            continue;
        }

        ++opaque_vertex_count;
        CHECK(
            vertex.secondary_block_id ==
            to_block_id(BlockType::Air));
        CHECK(vertex.material_blend == 0U);
    }

    CHECK(cutout_vertex_count > 0U);
    CHECK(opaque_vertex_count > 0U);
    CHECK(saw_u_zero);
    CHECK(saw_u_one);
    CHECK(saw_v_zero);
    CHECK(saw_v_one);
}

TEST_CASE("les arbres proches arrondissent leur silhouette dans un budget borne") {
    TestBlocks blocks {};
    place_oak_crossing_section(blocks, 6, 4, 7);
    const auto build = build_visual_vegetation(
        {{0, 0, 0}, {15, 15, 15}, 1},
        test_sampler(blocks),
        0x719E5A2BU);
    const auto lighting = [](int, int, int) {
        return VisualVegetationLighting {15U, 0U};
    };

    const auto near_mesh = build_visual_vegetation_mesh(
        build,
        VisualVegetationLod::Near,
        StylizedPrimitiveLod::Low,
        lighting);
    const auto medium_mesh = build_visual_vegetation_mesh(
        build,
        VisualVegetationLod::Medium,
        StylizedPrimitiveLod::Low,
        lighting);
    const auto medium_again = build_visual_vegetation_mesh(
        build,
        VisualVegetationLod::Medium,
        StylizedPrimitiveLod::Low,
        lighting);
    const auto far_mesh = build_visual_vegetation_mesh(
        build,
        VisualVegetationLod::Far,
        StylizedPrimitiveLod::Low,
        lighting);

    REQUIRE_FALSE(near_mesh.empty());
    REQUIRE_FALSE(medium_mesh.empty());
    REQUIRE_FALSE(far_mesh.empty());
    CHECK(exact_mesh_equal(medium_mesh, medium_again));
    CHECK(near_mesh.triangle_count() > medium_mesh.triangle_count());
    CHECK(medium_mesh.triangle_count() > far_mesh.triangle_count());
    CHECK(near_mesh.triangle_count() <= 1024U);
    CHECK(medium_mesh.triangle_count() == 336U);
    CHECK(far_mesh.triangle_count() <= 16U);

    const auto& near_batch =
        build.lods[visual_vegetation_lod_index(
            VisualVegetationLod::Near)];
    const auto& medium_batch =
        build.lods[visual_vegetation_lod_index(
            VisualVegetationLod::Medium)];
    const auto& far_batch =
        build.lods[visual_vegetation_lod_index(
            VisualVegetationLod::Far)];
    CHECK(mesh_is_inside_bounds(near_mesh, near_batch.bounds));
    CHECK(mesh_is_inside_bounds(medium_mesh, medium_batch.bounds));
    CHECK(mesh_is_inside_bounds(far_mesh, far_batch.bounds));

    std::size_t bounded_lobes = 0U;
    for (const auto& instance : medium_batch.instances) {
        if (instance.primitive !=
            VisualVegetationPrimitive::EllipsoidCanopy) {
            continue;
        }
        VisualVegetationBuild single_lobe_build {};
        auto& single_lobe_batch =
            single_lobe_build.lods[visual_vegetation_lod_index(
                VisualVegetationLod::Medium)];
        single_lobe_batch.lod = VisualVegetationLod::Medium;
        single_lobe_batch.instances.push_back(instance);
        single_lobe_batch.bounds = instance.bounds;
        const auto lobe_mesh = build_visual_vegetation_mesh(
            single_lobe_build,
            VisualVegetationLod::Medium,
            StylizedPrimitiveLod::Low,
            lighting);
        REQUIRE_FALSE(lobe_mesh.empty());
        CHECK(lobe_mesh.triangle_count() == 80U);
        CHECK(mesh_is_inside_bounds(lobe_mesh, instance.bounds));
        ++bounded_lobes;
    }
    CHECK(bounded_lobes == 3U);

    const auto leaf_vertices = static_cast<std::size_t>(std::count_if(
        medium_mesh.vertices.begin(),
        medium_mesh.vertices.end(),
        [](const TerrainVertex& vertex) {
            return vertex.primary_block_id ==
                   to_block_id(BlockType::Leaves);
        }));
    const auto trunk_vertices = static_cast<std::size_t>(std::count_if(
        medium_mesh.vertices.begin(),
        medium_mesh.vertices.end(),
        [](const TerrainVertex& vertex) {
            return vertex.primary_block_id ==
                   to_block_id(BlockType::Wood);
        }));
    CHECK(leaf_vertices == 816U);
    CHECK(trunk_vertices == 192U);
    const auto cutout_vertices = static_cast<std::size_t>(std::count_if(
        medium_mesh.vertices.begin(),
        medium_mesh.vertices.end(),
        [](const TerrainVertex& vertex) {
            return (vertex.surface_flags & 1U) != 0U;
        }));
    CHECK(cutout_vertices == 96U);

    TestBlocks pine_blocks {};
    place_pine_crossing_section(pine_blocks, 6, 4, 7);
    const auto pine_build = build_visual_vegetation(
        {{0, 0, 0}, {15, 15, 15}, 1},
        test_sampler(pine_blocks),
        0x914B2E67U);
    const auto pine_medium = build_visual_vegetation_mesh(
        pine_build,
        VisualVegetationLod::Medium,
        StylizedPrimitiveLod::Low,
        lighting);
    const auto pine_near = build_visual_vegetation_mesh(
        pine_build,
        VisualVegetationLod::Near,
        StylizedPrimitiveLod::Low,
        lighting);
    CHECK(pine_medium.triangle_count() == 336U);
    CHECK(pine_near.triangle_count() == 960U);
    CHECK(mesh_is_inside_bounds(
        pine_medium,
        pine_build.lods[visual_vegetation_lod_index(
            VisualVegetationLod::Medium)].bounds));
    CHECK(mesh_is_inside_bounds(
        pine_near,
        pine_build.lods[visual_vegetation_lod_index(
            VisualVegetationLod::Near)].bounds));
}

TEST_CASE("une construction en bois ne produit aucune géométrie de végétation") {
    TestBlocks blocks {};
    for (int x = 1; x <= 8; ++x) {
        blocks[{x, 3, 2}] = to_block_id(BlockType::Wood);
    }
    const auto build = build_visual_vegetation(
        {{0, 0, 0}, {15, 15, 15}, 1},
        test_sampler(blocks),
        42U);
    const auto mesh = build_visual_vegetation_mesh(
        build,
        VisualVegetationLod::Near,
        StylizedPrimitiveLod::High,
        [](int, int, int) {
            return VisualVegetationLighting {};
        });
    CHECK(mesh.empty());
}

TEST_CASE("la vegetation canonique traverse y15 y16 et y31 y32 sans perdre de triangle") {
    TestBlocks blocks {};
    place_oak_crossing_section(blocks, 6, 13, 7);
    const auto cactus = to_block_id(BlockType::Cactus);
    for (int y = 30; y <= 33; ++y) {
        blocks[{11, y, 9}] = cactus;
    }

    const auto build = build_visual_vegetation(
        {{0, 0, 0}, {15, kWorldMaxY, 15}, 8},
        test_sampler(blocks),
        0x51C710U);
    REQUIRE(build.sources.size() == 2U);
    const auto tree = std::find_if(
        build.sources.begin(),
        build.sources.end(),
        [](const VisualVegetationSource& source) {
            return source.kind ==
                   VisualVegetationSourceKind::BroadleafTree;
        });
    const auto cactus_source = std::find_if(
        build.sources.begin(),
        build.sources.end(),
        [](const VisualVegetationSource& source) {
            return source.kind ==
                   VisualVegetationSourceKind::Cactus;
        });
    REQUIRE(tree != build.sources.end());
    REQUIRE(cactus_source != build.sources.end());
    CHECK(tree->source_cell_count == 4U);
    CHECK(tree->foliage_cell_count > 0U);
    CHECK(cactus_source->source_cell_count == 4U);

    const auto mesh = build_visual_vegetation_mesh(
        build,
        VisualVegetationLod::Medium,
        StylizedPrimitiveLod::Low,
        [](int, int, int) {
            return VisualVegetationLighting {15U, 0U};
        });
    REQUIRE_FALSE(mesh.empty());
    const auto sections = partition_visual_vegetation_mesh(
        mesh,
        kWorldMinY,
        16,
        8U);
    REQUIRE(sections.size() == 8U);
    CHECK_FALSE(sections[0].empty());
    CHECK_FALSE(sections[1].empty());
    CHECK_FALSE(sections[2].empty());

    std::size_t partitioned_triangles = 0U;
    std::size_t leaf_vertices = 0U;
    std::size_t cactus_vertices = 0U;
    for (std::size_t section_index = 0U;
         section_index < sections.size();
         ++section_index) {
        const auto& section = sections[section_index];
        partitioned_triangles += section.triangle_count();
        for (std::size_t index_offset = 0U;
             index_offset < section.indices.size();
             index_offset += 3U) {
            const auto& a =
                section.vertices[section.indices[index_offset]];
            const auto& b =
                section.vertices[section.indices[index_offset + 1U]];
            const auto& c =
                section.vertices[section.indices[index_offset + 2U]];
            const auto centroid_y =
                (a.y + b.y + c.y) / 3.0F;
            const auto expected_section =
                static_cast<std::size_t>(
                    std::clamp(
                        static_cast<int>(
                            std::floor(centroid_y / 16.0F)),
                        0,
                        7));
            CHECK(expected_section == section_index);
        }
        leaf_vertices += static_cast<std::size_t>(
            std::count_if(
                section.vertices.begin(),
                section.vertices.end(),
                [](const TerrainVertex& vertex) {
                    return vertex.primary_block_id ==
                           to_block_id(BlockType::Leaves);
                }));
        cactus_vertices += static_cast<std::size_t>(
            std::count_if(
                section.vertices.begin(),
                section.vertices.end(),
                [](const TerrainVertex& vertex) {
                    return vertex.primary_block_id ==
                           to_block_id(BlockType::Cactus);
                }));
    }
    CHECK(partitioned_triangles == mesh.triangle_count());
    CHECK(leaf_vertices > 0U);
    CHECK(cactus_vertices > 0U);
}

TEST_CASE("les volumes fermes restent opaques et leurs accents restent ajoures") {
    TestBlocks blocks {};
    place_oak_crossing_section(blocks, 6, 4, 7);
    blocks[{12, 4, 11}] = to_block_id(BlockType::TallGrass);

    const auto build = build_visual_vegetation(
        {{0, 0, 0}, {15, 15, 15}, 1},
        test_sampler(blocks),
        0x0A11CEU);
    const auto mesh = build_visual_vegetation_mesh(
        build,
        VisualVegetationLod::Medium,
        StylizedPrimitiveLod::Low,
        [](int, int, int) {
            return VisualVegetationLighting {15U, 0U};
        });
    REQUIRE_FALSE(mesh.empty());

    std::size_t trunk_vertices = 0U;
    std::size_t canopy_vertices = 0U;
    std::size_t canopy_spray_vertices = 0U;
    std::size_t grass_vertices = 0U;
    for (const auto& vertex : mesh.vertices) {
        if (vertex.primary_block_id == to_block_id(BlockType::Wood)) {
            ++trunk_vertices;
            CHECK((vertex.surface_flags & 1U) == 0U);
        } else if (
            vertex.primary_block_id == to_block_id(BlockType::Leaves)) {
            if ((vertex.surface_flags & 1U) != 0U) {
                ++canopy_spray_vertices;
            } else {
                ++canopy_vertices;
            }
        } else if (
            vertex.primary_block_id ==
            to_block_id(BlockType::TallGrass)) {
            ++grass_vertices;
            CHECK((vertex.surface_flags & 1U) != 0U);
        }
    }
    CHECK(trunk_vertices > 0U);
    CHECK(canopy_vertices > 0U);
    CHECK(canopy_spray_vertices > 0U);
    CHECK(grass_vertices > 0U);
}

TEST_CASE("la vegetation retient deterministement la lumiere exposee autour du tronc") {
    VisualVegetationBuild build {};
    auto& batch =
        build.lods[visual_vegetation_lod_index(
            VisualVegetationLod::Medium)];
    batch.lod = VisualVegetationLod::Medium;

    VisualVegetationInstance trunk {};
    trunk.position_x = 0.5F;
    trunk.position_y = 4.0F;
    trunk.position_z = 0.5F;
    trunk.scale_x = 1.0F;
    trunk.scale_y = 4.0F;
    trunk.scale_z = 1.0F;
    trunk.primitive = VisualVegetationPrimitive::TaperedTrunk;
    trunk.source_kind = VisualVegetationSourceKind::BroadleafTree;
    trunk.material_block = to_block_id(BlockType::Wood);
    batch.instances.push_back(trunk);

    const auto lighting = [](int x, int, int z) {
        if (x == 0 && z == 0) {
            return VisualVegetationLighting {0U, 1U};
        }
        return VisualVegetationLighting {14U, 9U};
    };
    const auto first = build_visual_vegetation_mesh(
        build,
        VisualVegetationLod::Medium,
        StylizedPrimitiveLod::Low,
        lighting);
    const auto second = build_visual_vegetation_mesh(
        build,
        VisualVegetationLod::Medium,
        StylizedPrimitiveLod::Low,
        lighting);
    REQUIRE_FALSE(first.empty());
    CHECK(exact_mesh_equal(first, second));

    std::size_t side_vertices = 0U;
    for (const auto& vertex : first.vertices) {
        if (std::abs(vertex.ny) >= 0.5F) {
            continue;
        }
        ++side_vertices;
        CHECK(vertex.sky_light == 14U);
        CHECK(vertex.block_light == 9U);
        CHECK((vertex.surface_flags & 1U) == 0U);
    }
    CHECK(side_vertices > 0U);
}

} // namespace valcraft
