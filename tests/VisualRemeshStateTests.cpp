#include "TestUtils.h"
#include "render/VisualPipeline.h"
#include "world/World.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace valcraft {
namespace {

[[nodiscard]] auto one_slice_budget() -> WorldWorkBudget {
    WorldWorkBudget budget {};
    budget.chunk_generation_budget = 0U;
    budget.fluid_cell_budget = 0U;
    budget.mesh_rebuild_budget = 1U;
    budget.light_node_budget = 0U;
    budget.max_generation_ms = std::numeric_limits<double>::infinity();
    budget.max_fluid_ms = std::numeric_limits<double>::infinity();
    budget.max_lighting_ms = std::numeric_limits<double>::infinity();
    budget.max_meshing_ms = std::numeric_limits<double>::infinity();
    return budget;
}

void prepare_empty_modern_chunk(World& world, const ChunkCoord& coord) {
    test::make_chunk_empty(world, coord);
    world.rebuild_lighting();
    world.rebuild_dirty_meshes();
    (void)world.consume_pending_gpu_uploads(256U);

    auto* chunk = world.find_chunk(coord);
    REQUIRE(chunk != nullptr);
    REQUIRE_FALSE(chunk->is_dirty());
    REQUIRE_FALSE(chunk->is_lighting_dirty());
    REQUIRE(world.mesh_revision(coord) > 0U);
}

void place_test_stone_without_gameplay_side_effects(
    World& world,
    const ChunkCoord& coord,
    int local_x,
    int local_y,
    int local_z) {
    auto* chunk = world.find_chunk(coord);
    REQUIRE(chunk != nullptr);
    chunk->set_local(
        local_x,
        local_y,
        local_z,
        to_block_id(BlockType::Stone));
    // Je neutralise seulement le travail d'éclairage du test : la révision
    // d'entrée et la saleté de la section restent celles du vrai changement.
    chunk->clear_lighting_dirty();
}

[[nodiscard]] auto section_for_y(int y) noexcept -> std::size_t {
    return static_cast<std::size_t>(y / kChunkSectionHeight);
}

} // namespace

TEST_CASE("le remeshing moderne progresse par deux couches et publie une seule révision") {
    CHECK(kModernVisualRemeshSliceHeight == 2);
    CHECK(
        kModernVisualRemeshSlicesPerSection ==
        static_cast<std::size_t>(
            kChunkSectionHeight / 2));
    constexpr ChunkCoord origin {0, 0};
    constexpr int edited_y = 34;
    World world(
        91'337,
        0,
        WorldGenerationProfile::Continental,
        WorldGenerationVersion::Latest,
        VisualPipeline::ModernStylized);
    prepare_empty_modern_chunk(world, origin);

    const auto revision_before = world.mesh_revision(origin);
    const auto section_index = section_for_y(edited_y);
    const auto* published_before = world.organic_section_meshes_for(origin);
    REQUIRE(published_before != nullptr);
    REQUIRE((*published_before)[section_index].empty());

    place_test_stone_without_gameplay_side_effects(
        world,
        origin,
        8,
        edited_y,
        8);

    const auto budget = one_slice_budget();
    for (std::size_t slice = 0U;
         slice < kModernVisualRemeshSlicesPerSection;
         ++slice) {
        const auto stats = world.process_pending_work(budget);
        const auto status = world.visual_remesh_status(origin);
        CAPTURE(slice);
        CHECK(stats.mesh_sections_processed == 1U);

        if (slice + 1U < kModernVisualRemeshSlicesPerSection) {
            CHECK(stats.meshed_chunks == 0U);
            REQUIRE(status.active);
            CHECK(status.completed_slices == slice + 1U);
            CHECK(status.total_slices ==
                  kModernVisualRemeshSlicesPerSection);
            CHECK(status.target_sections.count() == 1U);
            CHECK(status.target_sections.test(section_index));
            CHECK(world.mesh_revision(origin) == revision_before);
            CHECK(world.consume_pending_gpu_uploads(8U).empty());

            const auto* still_published =
                world.organic_section_meshes_for(origin);
            REQUIRE(still_published != nullptr);
            CHECK((*still_published)[section_index].empty());
        } else {
            CHECK(stats.meshed_chunks == 1U);
            CHECK_FALSE(status.active);
            CHECK(world.mesh_revision(origin) == revision_before + 1U);
            const auto uploads =
                world.consume_pending_gpu_uploads(8U);
            REQUIRE(uploads.size() == 1U);
            CHECK(uploads.front() == origin);
        }
    }

    const auto* published_after =
        world.organic_section_meshes_for(origin);
    REQUIRE(published_after != nullptr);
    CHECK_FALSE((*published_after)[section_index].empty());
}

TEST_CASE("une entrée modifiée abandonne le staging sans exposer sa révision") {
    constexpr ChunkCoord origin {0, 0};
    constexpr int edited_y = 34;
    World world(
        91'338,
        0,
        WorldGenerationProfile::Continental,
        WorldGenerationVersion::Latest,
        VisualPipeline::ModernStylized);
    prepare_empty_modern_chunk(world, origin);
    const auto revision_before = world.mesh_revision(origin);
    const auto budget = one_slice_budget();

    place_test_stone_without_gameplay_side_effects(
        world,
        origin,
        7,
        edited_y,
        7);
    const auto first_stats = world.process_pending_work(budget);
    CHECK(first_stats.mesh_sections_processed == 1U);
    const auto first_status = world.visual_remesh_status(origin);
    REQUIRE(first_status.active);
    REQUIRE(first_status.completed_slices == 1U);

    place_test_stone_without_gameplay_side_effects(
        world,
        origin,
        8,
        edited_y,
        7);
    const auto abandoned_stats = world.process_pending_work(budget);
    CHECK(abandoned_stats.mesh_sections_processed == 1U);
    CHECK(abandoned_stats.meshed_chunks == 0U);
    CHECK_FALSE(world.visual_remesh_status(origin).active);
    CHECK(world.mesh_revision(origin) == revision_before);
    CHECK(world.consume_pending_gpu_uploads(8U).empty());

    const auto restarted_stats = world.process_pending_work(budget);
    CHECK(restarted_stats.mesh_sections_processed == 1U);
    const auto restarted_status = world.visual_remesh_status(origin);
    REQUIRE(restarted_status.active);
    CHECK(restarted_status.completed_slices == 1U);
    CHECK(restarted_status.source_revision !=
          first_status.source_revision);
    CHECK(world.mesh_revision(origin) == revision_before);

    for (std::size_t remaining = 1U;
         remaining < kModernVisualRemeshSlicesPerSection;
         ++remaining) {
        (void)world.process_pending_work(budget);
    }
    CHECK_FALSE(world.visual_remesh_status(origin).active);
    CHECK(world.mesh_revision(origin) == revision_before + 1U);

    const auto* meshes = world.organic_section_meshes_for(origin);
    REQUIRE(meshes != nullptr);
    const auto& edited_mesh = (*meshes)[section_for_y(edited_y)];
    CHECK_FALSE(edited_mesh.empty());
    CHECK(edited_mesh.quad_count >= 10U);
}

TEST_CASE("le remeshing canonique conserve arbres cactus et feuillage aux frontieres verticales") {
    constexpr ChunkCoord origin {0, 0};
    World world(
        91'341,
        0,
        WorldGenerationProfile::Continental,
        WorldGenerationVersion::Latest,
        VisualPipeline::ModernStylized);
    prepare_empty_modern_chunk(world, origin);

    auto* chunk = world.find_chunk(origin);
    REQUIRE(chunk != nullptr);
    const auto wood = to_block_id(BlockType::Wood);
    const auto leaves = to_block_id(BlockType::Leaves);
    const auto cactus = to_block_id(BlockType::Cactus);

    // Je fais traverser l'arbre a y=15/16 et le cactus a y=31/32.
    for (int y = 13; y <= 16; ++y) {
        chunk->set_local(6, y, 7, wood);
    }
    for (int y = 16; y <= 17; ++y) {
        for (int offset_z = -1; offset_z <= 1; ++offset_z) {
            for (int offset_x = -1; offset_x <= 1; ++offset_x) {
                chunk->set_local(
                    6 + offset_x,
                    y,
                    7 + offset_z,
                    leaves);
            }
        }
    }
    chunk->set_local(6, 16, 7, wood);
    for (int y = 30; y <= 33; ++y) {
        chunk->set_local(11, y, 9, cactus);
    }

    world.rebuild_lighting();
    world.rebuild_dirty_meshes();
    (void)world.consume_pending_gpu_uploads(256U);

    const auto* meshes =
        world.organic_section_meshes_for(origin);
    REQUIRE(meshes != nullptr);
    CHECK_FALSE((*meshes)[0].empty());
    CHECK_FALSE((*meshes)[1].empty());
    CHECK_FALSE((*meshes)[2].empty());

    std::size_t wood_vertices = 0U;
    std::size_t leaf_vertices = 0U;
    std::size_t cactus_vertices = 0U;
    for (std::size_t section_index = 0U;
         section_index < meshes->size();
         ++section_index) {
        const auto& mesh = (*meshes)[section_index];
        for (const auto& vertex : mesh.vertices) {
            if (vertex.primary_block_id == wood) {
                ++wood_vertices;
            } else if (vertex.primary_block_id == leaves) {
                ++leaf_vertices;
            } else if (vertex.primary_block_id == cactus) {
                ++cactus_vertices;
            }
        }
        for (std::size_t index_offset = 0U;
             index_offset < mesh.indices.size();
             index_offset += 3U) {
            const auto& a =
                mesh.vertices[mesh.indices[index_offset]];
            const auto& b =
                mesh.vertices[mesh.indices[index_offset + 1U]];
            const auto& c =
                mesh.vertices[mesh.indices[index_offset + 2U]];
            const auto centroid_y =
                (a.y + b.y + c.y) / 3.0F;
            const auto expected_section =
                static_cast<std::size_t>(
                    std::clamp(
                        static_cast<int>(
                            std::floor(
                                centroid_y /
                                static_cast<float>(
                                    kChunkSectionHeight))),
                        0,
                        static_cast<int>(
                            kChunkSectionCount - 1U)));
            CHECK(expected_section == section_index);
        }
    }
    CHECK(wood_vertices > 0U);
    CHECK(leaf_vertices > 0U);
    CHECK(cactus_vertices > 0U);

    // Je retire uniquement la peau cubique du tronc classe comme arbre. Les
    // cellules logiques et leur collision restent evidemment intactes.
    const auto* architecture =
        world.architectural_section_meshes_for(origin);
    REQUIRE(architecture != nullptr);
    for (const auto& section : *architecture) {
        CHECK(std::none_of(
            section.vertices.begin(),
            section.vertices.end(),
            [wood](const HardSurfaceVertex& vertex) {
                return vertex.material_block == wood;
            }));
    }
    CHECK(world.get_block(6, 15, 7) == wood);
    CHECK(world.get_block(6, 16, 7) == wood);
    CHECK(world.get_block(11, 31, 9) == cactus);
    CHECK(world.get_block(11, 32, 9) == cactus);
}

TEST_CASE("la révision du halo invalide une section en cours aux frontières de chunks") {
    constexpr ChunkCoord origin {0, 0};
    constexpr ChunkCoord east {1, 0};
    constexpr int edited_y = 34;
    World world(
        91'339,
        1,
        WorldGenerationProfile::Continental,
        WorldGenerationVersion::Latest,
        VisualPipeline::ModernStylized);
    prepare_empty_modern_chunk(world, origin);
    prepare_empty_modern_chunk(world, east);

    const auto origin_revision_before =
        world.mesh_revision(origin);
    const auto east_revision_before =
        world.mesh_revision(east);
    const auto budget = one_slice_budget();

    place_test_stone_without_gameplay_side_effects(
        world,
        origin,
        kChunkSizeX - 1,
        edited_y,
        8);
    (void)world.process_pending_work(budget);
    const auto first_status =
        world.visual_remesh_status(origin);
    REQUIRE(first_status.active);

    place_test_stone_without_gameplay_side_effects(
        world,
        east,
        0,
        edited_y,
        8);
    const auto abandoned_stats =
        world.process_pending_work(budget);
    CHECK(abandoned_stats.mesh_sections_processed == 1U);
    CHECK_FALSE(world.visual_remesh_status(origin).active);
    CHECK(world.mesh_revision(origin) ==
          origin_revision_before);
    CHECK(world.mesh_revision(east) ==
          east_revision_before);

    // Je termine ensuite les deux chunks et vérifie qu'aucun staging
    // intermédiaire n'a créé d'upload partiel.
    world.rebuild_dirty_meshes();
    CHECK(world.mesh_revision(origin) ==
          origin_revision_before + 1U);
    CHECK(world.mesh_revision(east) ==
          east_revision_before + 1U);
    const auto uploads =
        world.consume_pending_gpu_uploads(8U);
    CHECK(uploads.size() == 2U);
}

TEST_CASE("deux mondes identiques produisent la même progression de remeshing") {
    constexpr ChunkCoord origin {-1, 2};
    constexpr int edited_y = 50;
    World first(
        91'340,
        0,
        WorldGenerationProfile::Continental,
        WorldGenerationVersion::Latest,
        VisualPipeline::ModernStylized);
    World second(
        91'340,
        0,
        WorldGenerationProfile::Continental,
        WorldGenerationVersion::Latest,
        VisualPipeline::ModernStylized);
    prepare_empty_modern_chunk(first, origin);
    prepare_empty_modern_chunk(second, origin);
    place_test_stone_without_gameplay_side_effects(
        first,
        origin,
        5,
        edited_y,
        10);
    place_test_stone_without_gameplay_side_effects(
        second,
        origin,
        5,
        edited_y,
        10);

    const auto budget = one_slice_budget();
    for (std::size_t slice = 0U;
         slice < kModernVisualRemeshSlicesPerSection;
         ++slice) {
        const auto first_stats =
            first.process_pending_work(budget);
        const auto second_stats =
            second.process_pending_work(budget);
        const auto first_status =
            first.visual_remesh_status(origin);
        const auto second_status =
            second.visual_remesh_status(origin);

        CAPTURE(slice);
        CHECK(first_stats.mesh_sections_processed ==
              second_stats.mesh_sections_processed);
        CHECK(first_stats.meshed_chunks ==
              second_stats.meshed_chunks);
        CHECK(first_status.active == second_status.active);
        CHECK(first_status.source_revision ==
              second_status.source_revision);
        CHECK(first_status.target_sections ==
              second_status.target_sections);
        CHECK(first_status.next_slice ==
              second_status.next_slice);
        CHECK(first_status.completed_slices ==
              second_status.completed_slices);
        CHECK(first_status.total_slices ==
              second_status.total_slices);
        CHECK(first.mesh_revision(origin) ==
              second.mesh_revision(origin));
    }

    const auto* first_meshes =
        first.organic_section_meshes_for(origin);
    const auto* second_meshes =
        second.organic_section_meshes_for(origin);
    REQUIRE(first_meshes != nullptr);
    REQUIRE(second_meshes != nullptr);
    CHECK((*first_meshes)[section_for_y(edited_y)] ==
          (*second_meshes)[section_for_y(edited_y)]);
}

TEST_CASE("la premiere publication moderne termine un chunk avant le suivant") {
    constexpr ChunkCoord first {0, 0};
    constexpr ChunkCoord second {2, 0};
    constexpr auto section_index = kChunkSectionCount - 1U;
    World world(
        91'342,
        0,
        WorldGenerationProfile::Continental,
        WorldGenerationVersion::Latest,
        VisualPipeline::ModernStylized);

    // Je prepare les deux premieres publications dans un ordre volontairement
    // stable, avec un halo disjoint et une seule section vide a mailler.
    test::make_chunk_empty(world, first);
    world.rebuild_lighting();
    auto* first_chunk = world.find_chunk(first);
    REQUIRE(first_chunk != nullptr);
    first_chunk->clear_dirty();
    first_chunk->mark_section_dirty(section_index);

    test::make_chunk_empty(world, second);
    world.rebuild_lighting();
    auto* second_chunk = world.find_chunk(second);
    REQUIRE(second_chunk != nullptr);
    second_chunk->clear_dirty();
    second_chunk->mark_section_dirty(section_index);

    REQUIRE(world.mesh_revision(first) == 0U);
    REQUIRE(world.mesh_revision(second) == 0U);
    REQUIRE(world.pending_mesh_count() == 2U);

    const auto budget = one_slice_budget();
    for (std::size_t slice = 0U;
         slice < kModernVisualRemeshSlicesPerSection;
         ++slice) {
        const auto stats = world.process_pending_work(budget);
        const auto first_status =
            world.visual_remesh_status(first);
        const auto second_status =
            world.visual_remesh_status(second);

        CAPTURE(slice);
        CHECK(stats.mesh_sections_processed == 1U);
        CHECK(world.mesh_revision(second) == 0U);
        CHECK_FALSE(second_status.active);
        if (slice + 1U < kModernVisualRemeshSlicesPerSection) {
            CHECK(stats.meshed_chunks == 0U);
            REQUIRE(first_status.active);
            CHECK(first_status.completed_slices == slice + 1U);
            CHECK(first_status.total_slices ==
                  kModernVisualRemeshSlicesPerSection);
            CHECK(world.mesh_revision(first) == 0U);
            CHECK(world.consume_pending_gpu_uploads(8U).empty());
        } else {
            CHECK(stats.meshed_chunks == 1U);
            CHECK_FALSE(first_status.active);
            CHECK(world.mesh_revision(first) == 1U);
            const auto uploads =
                world.consume_pending_gpu_uploads(8U);
            REQUIRE(uploads.size() == 1U);
            CHECK(uploads.front() == first);
        }
    }

    for (std::size_t slice = 0U;
         slice < kModernVisualRemeshSlicesPerSection;
         ++slice) {
        const auto stats = world.process_pending_work(budget);
        const auto second_status =
            world.visual_remesh_status(second);

        CAPTURE(slice);
        CHECK(stats.mesh_sections_processed == 1U);
        CHECK(world.mesh_revision(first) == 1U);
        if (slice + 1U < kModernVisualRemeshSlicesPerSection) {
            CHECK(stats.meshed_chunks == 0U);
            REQUIRE(second_status.active);
            CHECK(second_status.completed_slices == slice + 1U);
            CHECK(world.mesh_revision(second) == 0U);
        } else {
            CHECK(stats.meshed_chunks == 1U);
            CHECK_FALSE(second_status.active);
            CHECK(world.mesh_revision(second) == 1U);
            const auto uploads =
                world.consume_pending_gpu_uploads(8U);
            REQUIRE(uploads.size() == 1U);
            CHECK(uploads.front() == second);
        }
    }

    CHECK(world.pending_mesh_count() == 0U);
}

} // namespace valcraft
