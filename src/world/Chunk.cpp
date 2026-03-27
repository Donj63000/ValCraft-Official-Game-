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
    auto& meshable_count = meshable_count_per_y_[static_cast<std::size_t>(y)];
    if (previous_contributes != next_contributes) {
        if (previous_contributes && meshable_count > 0) {
            --meshable_count;
        } else if (next_contributes) {
            ++meshable_count;
        }
    }

    if (!previous_contributes && next_contributes) {
        min_mesh_y_ = std::min(min_mesh_y_, y);
        max_mesh_y_ = std::max(max_mesh_y_, y);
    } else if (previous_contributes && !next_contributes && meshable_count == 0 && (y == min_mesh_y_ || y == max_mesh_y_)) {
        rebuild_meshable_bounds();
    }

    const auto previous_surface_support = is_block_surface_support(previous_block);
    const auto next_surface_support = is_block_surface_support(block_id);
    auto& surface_height = surface_heightmap_[surface_index_of(x, z)];
    if (next_surface_support) {
        surface_height = std::max(surface_height, y);
    } else if (previous_surface_support && surface_height == y) {
        rebuild_surface_height_column(x, z);
    }

    mark_section_dirty_for_y(y);
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
    const auto meshable = contributes_to_mesh_bounds(block_id);
    if (meshable) {
        min_mesh_y_ = kWorldMinY;
        max_mesh_y_ = kWorldMaxY;
    } else {
        min_mesh_y_ = kChunkHeight;
        max_mesh_y_ = kWorldMinY - 1;
    }
    meshable_count_per_y_.fill(static_cast<std::uint16_t>(meshable ? kChunkSizeX * kChunkSizeZ : 0));
    surface_heightmap_.fill(is_block_surface_support(block_id) ? kWorldMaxY : 0);
    dirty_sections_.set();
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

auto Chunk::surface_height_local(int x, int z) const -> int {
    if (x < 0 || x >= kChunkSizeX || z < 0 || z >= kChunkSizeZ) {
        throw std::out_of_range("Chunk::surface_height_local coordinates out of bounds");
    }
    return surface_heightmap_[surface_index_of(x, z)];
}

auto Chunk::is_dirty() const noexcept -> bool {
    return dirty_sections_.any();
}

auto Chunk::is_lighting_dirty() const noexcept -> bool {
    return lighting_dirty_;
}

auto Chunk::is_section_dirty(std::size_t section_index) const noexcept -> bool {
    return section_index < dirty_sections_.size() && dirty_sections_.test(section_index);
}

auto Chunk::dirty_section_count() const noexcept -> std::size_t {
    return dirty_sections_.count();
}

void Chunk::mark_dirty() noexcept {
    dirty_sections_.set();
}

void Chunk::mark_section_dirty(std::size_t section_index) noexcept {
    if (section_index < dirty_sections_.size()) {
        dirty_sections_.set(section_index);
    }
}

void Chunk::mark_section_dirty_for_y(int y) noexcept {
    if (!is_world_y_valid(y)) {
        return;
    }

    const auto section_index = section_index_of_y(y);
    dirty_sections_.set(section_index);
    if (y > kWorldMinY && y % kChunkSectionHeight == 0 && section_index > 0) {
        dirty_sections_.set(section_index - 1);
    }
    if (y < kWorldMaxY && y % kChunkSectionHeight == kChunkSectionHeight - 1 && section_index + 1 < dirty_sections_.size()) {
        dirty_sections_.set(section_index + 1);
    }
}

void Chunk::mark_lighting_dirty() noexcept {
    lighting_dirty_ = true;
}

void Chunk::clear_dirty() noexcept {
    dirty_sections_.reset();
}

void Chunk::clear_section_dirty(std::size_t section_index) noexcept {
    if (section_index < dirty_sections_.size()) {
        dirty_sections_.reset(section_index);
    }
}

void Chunk::clear_lighting_dirty() noexcept {
    lighting_dirty_ = false;
}

auto Chunk::index_of(int x, int y, int z) noexcept -> std::size_t {
    return static_cast<std::size_t>((y * kChunkSizeZ + z) * kChunkSizeX + x);
}

auto Chunk::surface_index_of(int x, int z) noexcept -> std::size_t {
    return static_cast<std::size_t>(z * kChunkSizeX + x);
}

auto Chunk::section_index_of_y(int y) noexcept -> std::size_t {
    return static_cast<std::size_t>(y / kChunkSectionHeight);
}

void Chunk::rebuild_meshable_bounds() noexcept {
    min_mesh_y_ = kChunkHeight;
    max_mesh_y_ = kWorldMinY - 1;

    for (int y = kWorldMinY; y <= kWorldMaxY; ++y) {
        if (meshable_count_per_y_[static_cast<std::size_t>(y)] == 0) {
            continue;
        }
        min_mesh_y_ = std::min(min_mesh_y_, y);
        max_mesh_y_ = std::max(max_mesh_y_, y);
    }
}

void Chunk::rebuild_surface_height_column(int x, int z) noexcept {
    auto& height = surface_heightmap_[surface_index_of(x, z)];
    height = 0;
    for (int y = kWorldMaxY; y >= kWorldMinY; --y) {
        if (!is_block_surface_support(blocks_[index_of(x, y, z)])) {
            continue;
        }
        height = y;
        return;
    }
}

} // namespace valcraft
