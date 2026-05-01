#include "world/World.h"

#include <doctest/doctest.h>

#include <limits>

namespace valcraft {

namespace {

auto unlimited_world_budget(std::size_t mesh_rebuild_budget = std::numeric_limits<std::size_t>::max() / 8U)
    -> WorldWorkBudget {
    WorldWorkBudget budget {};
    budget.chunk_generation_budget = 64U;
    budget.fluid_cell_budget = 8192U;
    budget.mesh_rebuild_budget = mesh_rebuild_budget;
    budget.light_node_budget = std::numeric_limits<std::size_t>::max() / 8U;
    budget.max_generation_ms = std::numeric_limits<double>::infinity();
    budget.max_fluid_ms = std::numeric_limits<double>::infinity();
    budget.max_lighting_ms = std::numeric_limits<double>::infinity();
    budget.max_meshing_ms = std::numeric_limits<double>::infinity();
    return budget;
}

void drain_world_work(World& world, const WorldWorkBudget& budget, int max_iterations) {
    for (int iteration = 0; iteration < max_iterations && world.has_pending_work(); ++iteration) {
        (void)world.process_pending_work(budget);
    }
}

} // namespace

TEST_CASE("frame scheduler retries chunks skipped by overlapping lighting coverage") {
    World world(1337, 1);
    const glm::vec3 focus {0.5F, 70.0F, 0.5F};
    const auto center = world.world_to_chunk(0, 0);

    for (int dz = -1; dz <= 1; ++dz) {
        for (int dx = -1; dx <= 1; ++dx) {
            world.ensure_chunk_loaded({center.x + dx, center.z + dz});
        }
    }
    (void)world.update_streaming(focus);

    const auto budget = unlimited_world_budget(64U);
    drain_world_work(world, budget, 8);

    CHECK(world.are_chunks_ready(focus, 1));
    CHECK_FALSE(world.has_pending_work());

    for (int dz = -1; dz <= 1; ++dz) {
        for (int dx = -1; dx <= 1; ++dx) {
            const ChunkCoord coord {center.x + dx, center.z + dz};
            const auto* chunk = world.find_chunk(coord);
            REQUIRE(chunk != nullptr);
            CHECK(world.mesh_revision(coord) > 0);
            CHECK_FALSE(chunk->is_dirty());
            CHECK_FALSE(chunk->is_lighting_dirty());
        }
    }
}

} // namespace valcraft
