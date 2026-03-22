#pragma once

#include "world/World.h"

#include <limits>

namespace valcraft::test {

inline void make_chunk_empty(World& world, const ChunkCoord& coord) {
    world.ensure_chunk_loaded(coord);
    auto* chunk = world.find_chunk(coord);
    if (chunk != nullptr) {
        chunk->fill(to_block_id(BlockType::Air));
    }
}

inline void make_flat_floor(World& world, int min_x, int max_x, int y, int min_z, int max_z, BlockId block_id = to_block_id(BlockType::Stone)) {
    for (int z = min_z; z <= max_z; ++z) {
        for (int x = min_x; x <= max_x; ++x) {
            world.set_block(x, y, z, block_id);
        }
    }
}

inline void flush_pending_work(
    World& world,
    WorldWorkBudget budget = {
        64,
        64,
        std::numeric_limits<std::size_t>::max() / 8U,
    }) {
    while (world.has_pending_work()) {
        const auto stats = world.process_pending_work(budget);
        (void)stats;
    }
    world.rebuild_dirty_meshes();
}

} // namespace valcraft::test
