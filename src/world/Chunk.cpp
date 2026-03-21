#include "world/Chunk.h"

#include <algorithm>
#include <stdexcept>

namespace valcraft {

Chunk::Chunk(ChunkCoord coord)
    : coord_(coord) {
    fill(to_block_id(BlockType::Air));
    dirty_ = true;
}

auto Chunk::coord() const noexcept -> ChunkCoord {
    return coord_;
}

auto Chunk::in_bounds_local(int x, int y, int z) const noexcept -> bool {
    return x >= 0 && x < kChunkSizeX &&
           z >= 0 && z < kChunkSizeZ &&
           is_world_y_valid(y);
}

auto Chunk::get_local(int x, int y, int z) const -> BlockId {
    if (!in_bounds_local(x, y, z)) {
        throw std::out_of_range("Chunk::get_local coordinates out of bounds");
    }
    return blocks_[index_of(x, y, z)];
}

void Chunk::set_local(int x, int y, int z, BlockId block_id) {
    if (!in_bounds_local(x, y, z)) {
        throw std::out_of_range("Chunk::set_local coordinates out of bounds");
    }

    auto& block = blocks_[index_of(x, y, z)];
    if (block == block_id) {
        return;
    }
    block = block_id;
    dirty_ = true;
}

void Chunk::fill(BlockId block_id) {
    std::fill(blocks_.begin(), blocks_.end(), block_id);
    dirty_ = true;
}

auto Chunk::is_dirty() const noexcept -> bool {
    return dirty_;
}

void Chunk::mark_dirty() noexcept {
    dirty_ = true;
}

void Chunk::clear_dirty() noexcept {
    dirty_ = false;
}

auto Chunk::index_of(int x, int y, int z) noexcept -> std::size_t {
    return static_cast<std::size_t>((y * kChunkSizeZ + z) * kChunkSizeX + x);
}

} // namespace valcraft
