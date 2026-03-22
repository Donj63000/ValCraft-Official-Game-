#include "world/Chunk.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace valcraft {

namespace {

auto contributes_to_mesh_bounds(BlockId block_id) noexcept -> bool {
    return has_block_mesh(block_id);
}

} // namespace

Chunk::Chunk(ChunkCoord coord)
    : coord_(coord) {
    fill(to_block_id(BlockType::Air));
    clear_lighting();
    dirty_ = true;
    lighting_dirty_ = true;
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

auto Chunk::get_sky_light_local(int x, int y, int z) const -> std::uint8_t {
    if (!in_bounds_local(x, y, z)) {
        throw std::out_of_range("Chunk::get_sky_light_local coordinates out of bounds");
    }
    return sky_light_[index_of(x, y, z)];
}

auto Chunk::get_block_light_local(int x, int y, int z) const -> std::uint8_t {
    if (!in_bounds_local(x, y, z)) {
        throw std::out_of_range("Chunk::get_block_light_local coordinates out of bounds");
    }
    return block_light_[index_of(x, y, z)];
}

void Chunk::set_local(int x, int y, int z, BlockId block_id) {
    if (!in_bounds_local(x, y, z)) {
        throw std::out_of_range("Chunk::set_local coordinates out of bounds");
    }

    auto& block = blocks_[index_of(x, y, z)];
    if (block == block_id) {
        return;
    }

    const auto previous_block = block;
    block = block_id;
    const auto previous_contributes = contributes_to_mesh_bounds(previous_block);
    const auto next_contributes = contributes_to_mesh_bounds(block_id);
    if (!previous_contributes && next_contributes) {
        min_mesh_y_ = std::min(min_mesh_y_, y);
        max_mesh_y_ = std::max(max_mesh_y_, y);
    } else if (previous_contributes && !next_contributes && (y == min_mesh_y_ || y == max_mesh_y_)) {
        rebuild_meshable_bounds();
    }
    dirty_ = true;
    lighting_dirty_ = true;
}

void Chunk::set_sky_light_local(int x, int y, int z, std::uint8_t light_level) {
    if (!in_bounds_local(x, y, z)) {
        throw std::out_of_range("Chunk::set_sky_light_local coordinates out of bounds");
    }
    sky_light_[index_of(x, y, z)] = static_cast<std::uint8_t>(std::min<int>(light_level, 15));
}

void Chunk::set_block_light_local(int x, int y, int z, std::uint8_t light_level) {
    if (!in_bounds_local(x, y, z)) {
        throw std::out_of_range("Chunk::set_block_light_local coordinates out of bounds");
    }
    block_light_[index_of(x, y, z)] = static_cast<std::uint8_t>(std::min<int>(light_level, 15));
}

void Chunk::fill(BlockId block_id) {
    std::fill(blocks_.begin(), blocks_.end(), block_id);
    if (contributes_to_mesh_bounds(block_id)) {
        min_mesh_y_ = kWorldMinY;
        max_mesh_y_ = kWorldMaxY;
    } else {
        min_mesh_y_ = kChunkHeight;
        max_mesh_y_ = kWorldMinY - 1;
    }
    dirty_ = true;
    lighting_dirty_ = true;
}

void Chunk::clear_lighting() noexcept {
    std::fill(sky_light_.begin(), sky_light_.end(), 0);
    std::fill(block_light_.begin(), block_light_.end(), 0);
}

auto Chunk::rebuild_sky_light_column(int x, int z) -> bool {
    if (x < 0 || x >= kChunkSizeX || z < 0 || z >= kChunkSizeZ) {
        throw std::out_of_range("Chunk::rebuild_sky_light_column coordinates out of bounds");
    }

    auto changed = false;
    bool sky_visible = true;
    for (int y = kWorldMaxY; y >= kWorldMinY; --y) {
        const auto block_id = blocks_[index_of(x, y, z)];
        const auto next_light = static_cast<std::uint8_t>(sky_visible && !is_block_opaque(block_id) ? 15 : 0);
        auto& current_light = sky_light_[index_of(x, y, z)];
        if (current_light != next_light) {
            current_light = next_light;
            changed = true;
        }

        if (is_block_opaque(block_id)) {
            sky_visible = false;
        }
    }

    return changed;
}

void Chunk::copy_block_light_from(const std::uint8_t* data, std::size_t count) {
    if (count != block_light_.size()) {
        throw std::out_of_range("Chunk::copy_block_light_from size mismatch");
    }
    std::memcpy(block_light_.data(), data, count * sizeof(std::uint8_t));
}

auto Chunk::blocks() const noexcept -> const std::array<BlockId, kChunkVolume>& {
    return blocks_;
}

auto Chunk::sky_light() const noexcept -> const std::array<std::uint8_t, kChunkVolume>& {
    return sky_light_;
}

auto Chunk::block_light() const noexcept -> const std::array<std::uint8_t, kChunkVolume>& {
    return block_light_;
}

auto Chunk::has_meshable_blocks() const noexcept -> bool {
    return max_mesh_y_ >= min_mesh_y_;
}

auto Chunk::min_mesh_y() const noexcept -> int {
    return has_meshable_blocks() ? min_mesh_y_ : kWorldMinY;
}

auto Chunk::max_mesh_y() const noexcept -> int {
    return has_meshable_blocks() ? max_mesh_y_ : (kWorldMinY - 1);
}

auto Chunk::is_dirty() const noexcept -> bool {
    return dirty_;
}

auto Chunk::is_lighting_dirty() const noexcept -> bool {
    return lighting_dirty_;
}

void Chunk::mark_dirty() noexcept {
    dirty_ = true;
}

void Chunk::mark_lighting_dirty() noexcept {
    lighting_dirty_ = true;
}

void Chunk::clear_dirty() noexcept {
    dirty_ = false;
}

void Chunk::clear_lighting_dirty() noexcept {
    lighting_dirty_ = false;
}

auto Chunk::index_of(int x, int y, int z) noexcept -> std::size_t {
    return static_cast<std::size_t>((y * kChunkSizeZ + z) * kChunkSizeX + x);
}

void Chunk::rebuild_meshable_bounds() noexcept {
    min_mesh_y_ = kChunkHeight;
    max_mesh_y_ = kWorldMinY - 1;

    for (int y = kWorldMinY; y <= kWorldMaxY; ++y) {
        for (int z = 0; z < kChunkSizeZ; ++z) {
            for (int x = 0; x < kChunkSizeX; ++x) {
                if (!contributes_to_mesh_bounds(blocks_[index_of(x, y, z)])) {
                    continue;
                }
                min_mesh_y_ = std::min(min_mesh_y_, y);
                max_mesh_y_ = std::max(max_mesh_y_, y);
            }
        }
    }
}

} // namespace valcraft
