#include "render/VisualPipeline.h"
#include "world/World.h"

#include <doctest/doctest.h>

#include <glm/vec3.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace valcraft {

namespace {

auto save_plan_digest(const WorldSavePlan& plan) -> std::uint64_t {
    auto digest = std::uint64_t {14695981039346656037ULL};
    const auto mix = [&digest](std::uint64_t value) {
        for (int byte_index = 0; byte_index < 8; ++byte_index) {
            digest ^= (value >> static_cast<unsigned int>(byte_index * 8)) & 0xFFULL;
            digest *= 1099511628211ULL;
        }
    };

    mix(static_cast<std::uint32_t>(plan.seed));
    mix(static_cast<std::uint8_t>(plan.generation_profile));
    mix(static_cast<std::uint8_t>(plan.generation_version));
    mix(plan.chunks.size());
    for (const auto& chunk : plan.chunks) {
        mix(static_cast<std::uint32_t>(chunk.coord.x));
        mix(static_cast<std::uint32_t>(chunk.coord.z));
        mix(chunk.sparse_cells.size());
        for (const auto& cell : chunk.sparse_cells) {
            mix(cell.index);
            mix(cell.block);
            mix(cell.water_state);
        }
        mix(chunk.dense_blocks.size());
        for (const auto block : chunk.dense_blocks) {
            mix(block);
        }
        mix(chunk.dense_water_state.size());
        for (const auto water : chunk.dense_water_state) {
            mix(water);
        }
    }
    return digest;
}

void check_same_hit(const RaycastHit& legacy, const RaycastHit& modern) {
    CHECK(legacy.hit == modern.hit);
    CHECK(legacy.block == modern.block);
    CHECK(legacy.adjacent == modern.adjacent);
    CHECK(legacy.block_id == modern.block_id);
    CHECK(legacy.distance == doctest::Approx(modern.distance));
}

} // namespace

TEST_CASE("modern visual world preserves the complete logical edit and raycast trace") {
    constexpr int seed = 712'903;
    World legacy(
        seed,
        0,
        WorldGenerationProfile::Continental,
        WorldGenerationVersion::Latest,
        VisualPipeline::LegacyVoxel);
    World modern(
        seed,
        0,
        WorldGenerationProfile::Continental,
        WorldGenerationVersion::Latest,
        VisualPipeline::ModernStylized);

    legacy.ensure_chunk_loaded({0, 0});
    modern.ensure_chunk_loaded({0, 0});
    const auto surface_y = legacy.surface_height(7, 7);
    REQUIRE(surface_y == modern.surface_height(7, 7));

    const glm::vec3 origin {7.5F, static_cast<float>(surface_y) + 6.0F, 7.5F};
    const glm::vec3 down {0.0F, -1.0F, 0.0F};
    const auto initial_legacy_hit = legacy.raycast(origin, down, 16.0F);
    const auto initial_modern_hit = modern.raycast(origin, down, 16.0F);
    REQUIRE(initial_legacy_hit.hit);
    check_same_hit(initial_legacy_hit, initial_modern_hit);

    const auto removed = initial_legacy_hit.block;
    legacy.set_block(removed.x, removed.y, removed.z, to_block_id(BlockType::Air));
    modern.set_block(removed.x, removed.y, removed.z, to_block_id(BlockType::Air));
    check_same_hit(
        legacy.raycast(origin, down, 16.0F),
        modern.raycast(origin, down, 16.0F));

    legacy.set_block(removed.x, removed.y, removed.z, to_block_id(BlockType::Grass));
    modern.set_block(removed.x, removed.y, removed.z, to_block_id(BlockType::Grass));
    legacy.rebuild_dirty_meshes();
    modern.rebuild_dirty_meshes();

    CHECK(legacy.get_block(removed.x, removed.y, removed.z) ==
          modern.get_block(removed.x, removed.y, removed.z));
    CHECK(legacy.water_level(removed.x, removed.y, removed.z) ==
          modern.water_level(removed.x, removed.y, removed.z));
    CHECK(save_plan_digest(legacy.capture_save_plan()) ==
          save_plan_digest(modern.capture_save_plan()));
    CHECK(legacy.mesh_revision({0, 0}) > 0U);
    CHECK(modern.mesh_revision({0, 0}) > 0U);

    const auto* legacy_organic = legacy.organic_section_meshes_for({0, 0});
    const auto* modern_organic = modern.organic_section_meshes_for({0, 0});
    REQUIRE(legacy_organic != nullptr);
    REQUIRE(modern_organic != nullptr);

    auto legacy_vertex_count = std::size_t {0};
    auto modern_vertex_count = std::size_t {0};
    for (std::size_t section = 0; section < kChunkSectionCount; ++section) {
        legacy_vertex_count += (*legacy_organic)[section].vertices.size();
        modern_vertex_count += (*modern_organic)[section].vertices.size();
    }
    CHECK(legacy_vertex_count == 0U);
    CHECK(modern_vertex_count > 0U);
}

TEST_CASE("switching visual pipeline rebuilds render caches without touching save state") {
    World world(
        81'771,
        0,
        WorldGenerationProfile::Continental,
        WorldGenerationVersion::Latest,
        VisualPipeline::ModernStylized);
    world.ensure_chunk_loaded({-1, -1});
    world.rebuild_dirty_meshes();
    const auto before = save_plan_digest(world.capture_save_plan());

    world.set_visual_pipeline(VisualPipeline::LegacyVoxel);
    CHECK(world.visual_pipeline() == VisualPipeline::LegacyVoxel);
    world.rebuild_dirty_meshes();
    CHECK(save_plan_digest(world.capture_save_plan()) == before);

    const auto* meshes = world.organic_section_meshes_for({-1, -1});
    REQUIRE(meshes != nullptr);
    for (const auto& mesh : *meshes) {
        CHECK(mesh.empty());
        CHECK(mesh.vertices.capacity() == 0U);
        CHECK(mesh.indices.capacity() == 0U);
    }
}

} // namespace valcraft
