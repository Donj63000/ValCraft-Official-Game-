#include "world/World.h"

#include <glm/geometric.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

namespace valcraft {

namespace {

constexpr std::array<ChunkCoord, 4> kNeighborOffsets {{
    {1, 0},
    {-1, 0},
    {0, 1},
    {0, -1},
}};

} // namespace

World::World(int seed, int stream_radius)
    : stream_radius_(stream_radius),
      generator_(seed) {
}

auto World::get_block(int x, int y, int z) const -> BlockId {
    if (!is_world_y_valid(y)) {
        return to_block_id(BlockType::Air);
    }

    const auto chunk_coord = world_to_chunk(x, z);
    const auto local = world_to_local(x, y, z);
    const auto* chunk = find_chunk(chunk_coord);
    if (chunk == nullptr) {
        return to_block_id(BlockType::Air);
    }
    return chunk->get_local(local.x, local.y, local.z);
}

void World::set_block(int x, int y, int z, BlockId block_id) {
    if (!is_world_y_valid(y)) {
        return;
    }

    const auto chunk_coord = world_to_chunk(x, z);
    ensure_chunk_loaded(chunk_coord);

    const auto local = world_to_local(x, y, z);
    auto* chunk = find_chunk(chunk_coord);
    if (chunk == nullptr) {
        throw std::runtime_error("Chunk disappeared during set_block");
    }

    chunk->set_local(local.x, local.y, local.z, block_id);
    mark_chunk_and_neighbors_dirty(chunk_coord, local);
}

auto World::world_to_chunk(int x, int z) const noexcept -> ChunkCoord {
    return {
        floor_div(x, kChunkSizeX),
        floor_div(z, kChunkSizeZ),
    };
}

auto World::world_to_local(int x, int y, int z) const noexcept -> BlockCoord {
    return {
        positive_mod(x, kChunkSizeX),
        y,
        positive_mod(z, kChunkSizeZ),
    };
}

auto World::local_to_world(const ChunkCoord& chunk_coord, const BlockCoord& local) const noexcept -> BlockCoord {
    return {
        chunk_coord.x * kChunkSizeX + local.x,
        local.y,
        chunk_coord.z * kChunkSizeZ + local.z,
    };
}

auto World::raycast(const glm::vec3& origin, const glm::vec3& direction, float max_distance) const -> RaycastHit {
    if (glm::dot(direction, direction) < 1.0e-6F) {
        return {};
    }

    const auto dir = glm::normalize(direction);
    BlockCoord current {
        static_cast<int>(std::floor(origin.x)),
        static_cast<int>(std::floor(origin.y)),
        static_cast<int>(std::floor(origin.z)),
    };
    BlockCoord previous = current;

    const auto starting_block = get_block(current.x, current.y, current.z);
    if (is_block_solid(starting_block)) {
        return {
            true,
            current,
            current,
            starting_block,
        };
    }

    const auto compute_step = [](float component) -> int {
        if (component > 0.0F) {
            return 1;
        }
        if (component < 0.0F) {
            return -1;
        }
        return 0;
    };

    const auto step_x = compute_step(dir.x);
    const auto step_y = compute_step(dir.y);
    const auto step_z = compute_step(dir.z);

    const auto inf = std::numeric_limits<float>::infinity();
    const auto next_boundary = [](float origin_component, int current_cell, int step) -> float {
        if (step > 0) {
            return static_cast<float>(current_cell + 1) - origin_component;
        }
        return origin_component - static_cast<float>(current_cell);
    };

    float t_max_x = step_x == 0 ? inf : next_boundary(origin.x, current.x, step_x) / std::abs(dir.x);
    float t_max_y = step_y == 0 ? inf : next_boundary(origin.y, current.y, step_y) / std::abs(dir.y);
    float t_max_z = step_z == 0 ? inf : next_boundary(origin.z, current.z, step_z) / std::abs(dir.z);

    const auto t_delta_x = step_x == 0 ? inf : 1.0F / std::abs(dir.x);
    const auto t_delta_y = step_y == 0 ? inf : 1.0F / std::abs(dir.y);
    const auto t_delta_z = step_z == 0 ? inf : 1.0F / std::abs(dir.z);

    float travelled = 0.0F;
    while (travelled <= max_distance) {
        previous = current;

        if (t_max_x <= t_max_y && t_max_x <= t_max_z) {
            current.x += step_x;
            travelled = t_max_x;
            t_max_x += t_delta_x;
        } else if (t_max_y <= t_max_z) {
            current.y += step_y;
            travelled = t_max_y;
            t_max_y += t_delta_y;
        } else {
            current.z += step_z;
            travelled = t_max_z;
            t_max_z += t_delta_z;
        }

        if (travelled > max_distance) {
            break;
        }

        const auto block_id = get_block(current.x, current.y, current.z);
        if (is_block_solid(block_id)) {
            return {
                true,
                current,
                previous,
                block_id,
            };
        }
    }

    return {};
}

void World::ensure_chunk_loaded(const ChunkCoord& coord) {
    if (chunks_.contains(coord)) {
        return;
    }

    auto [iterator, inserted] = chunks_.try_emplace(coord, coord);
    if (!inserted) {
        return;
    }

    generator_.generate_chunk(iterator->second.chunk);
    iterator->second.chunk.mark_dirty();
    mark_neighbors_dirty(coord);
}

void World::update_streaming(const glm::vec3& player_position) {
    const auto center = world_to_chunk(
        static_cast<int>(std::floor(player_position.x)),
        static_cast<int>(std::floor(player_position.z)));

    for (int dz = -stream_radius_; dz <= stream_radius_; ++dz) {
        for (int dx = -stream_radius_; dx <= stream_radius_; ++dx) {
            ensure_chunk_loaded({center.x + dx, center.z + dz});
        }
    }

    unload_far_chunks(center);
}

void World::rebuild_dirty_meshes() {
    for (auto& [coord, record] : chunks_) {
        if (record.chunk.is_dirty()) {
            rebuild_chunk_mesh(record);
        }
    }
}

auto World::find_chunk(const ChunkCoord& coord) -> Chunk* {
    const auto iterator = chunks_.find(coord);
    return iterator == chunks_.end() ? nullptr : &iterator->second.chunk;
}

auto World::find_chunk(const ChunkCoord& coord) const -> const Chunk* {
    const auto iterator = chunks_.find(coord);
    return iterator == chunks_.end() ? nullptr : &iterator->second.chunk;
}

auto World::mesh_for(const ChunkCoord& coord) const -> const ChunkMeshData* {
    const auto iterator = chunks_.find(coord);
    return iterator == chunks_.end() ? nullptr : &iterator->second.mesh;
}

auto World::mesh_revision(const ChunkCoord& coord) const -> std::uint64_t {
    const auto iterator = chunks_.find(coord);
    return iterator == chunks_.end() ? 0 : iterator->second.mesh_revision;
}

auto World::chunk_records() const noexcept -> const std::unordered_map<ChunkCoord, ChunkRecord, ChunkCoordHash>& {
    return chunks_;
}

auto World::seed() const noexcept -> int {
    return generator_.seed();
}

auto World::stream_radius() const noexcept -> int {
    return stream_radius_;
}

auto World::surface_height(int world_x, int world_z) -> int {
    const auto coord = world_to_chunk(world_x, world_z);
    ensure_chunk_loaded(coord);
    const auto local = world_to_local(world_x, 0, world_z);
    auto* chunk = find_chunk(coord);
    if (chunk == nullptr) {
        return 0;
    }

    for (int y = kWorldMaxY; y >= kWorldMinY; --y) {
        if (is_block_solid(chunk->get_local(local.x, y, local.z))) {
            return y;
        }
    }

    return 0;
}

auto World::floor_div(int value, int divisor) noexcept -> int {
    auto quotient = value / divisor;
    const auto remainder = value % divisor;
    if (remainder < 0) {
        --quotient;
    }
    return quotient;
}

auto World::positive_mod(int value, int divisor) noexcept -> int {
    const auto remainder = value % divisor;
    return remainder < 0 ? remainder + divisor : remainder;
}

void World::mark_chunk_and_neighbors_dirty(const ChunkCoord& coord, const BlockCoord& local) {
    if (auto* chunk = find_chunk(coord); chunk != nullptr) {
        chunk->mark_dirty();
    }

    if (local.x == 0) {
        if (auto* neighbor = find_chunk({coord.x - 1, coord.z}); neighbor != nullptr) {
            neighbor->mark_dirty();
        }
    }
    if (local.x == kChunkSizeX - 1) {
        if (auto* neighbor = find_chunk({coord.x + 1, coord.z}); neighbor != nullptr) {
            neighbor->mark_dirty();
        }
    }
    if (local.z == 0) {
        if (auto* neighbor = find_chunk({coord.x, coord.z - 1}); neighbor != nullptr) {
            neighbor->mark_dirty();
        }
    }
    if (local.z == kChunkSizeZ - 1) {
        if (auto* neighbor = find_chunk({coord.x, coord.z + 1}); neighbor != nullptr) {
            neighbor->mark_dirty();
        }
    }
}

void World::mark_neighbors_dirty(const ChunkCoord& coord) {
    if (auto* chunk = find_chunk(coord); chunk != nullptr) {
        chunk->mark_dirty();
    }

    for (const auto& offset : kNeighborOffsets) {
        if (auto* neighbor = find_chunk({coord.x + offset.x, coord.z + offset.z}); neighbor != nullptr) {
            neighbor->mark_dirty();
        }
    }
}

void World::unload_far_chunks(const ChunkCoord& center) {
    std::vector<ChunkCoord> to_remove;
    to_remove.reserve(chunks_.size());

    const auto unload_radius = stream_radius_ + 1;
    for (const auto& [coord, record] : chunks_) {
        const auto dx = std::abs(coord.x - center.x);
        const auto dz = std::abs(coord.z - center.z);
        if (dx > unload_radius || dz > unload_radius) {
            to_remove.push_back(coord);
        }
    }

    for (const auto& coord : to_remove) {
        for (const auto& offset : kNeighborOffsets) {
            if (auto* neighbor = find_chunk({coord.x + offset.x, coord.z + offset.z}); neighbor != nullptr) {
                neighbor->mark_dirty();
            }
        }
        chunks_.erase(coord);
    }
}

void World::rebuild_chunk_mesh(ChunkRecord& record) {
    record.mesh = mesher_.build_mesh(*this, record.chunk.coord());
    record.chunk.clear_dirty();
    ++record.mesh_revision;
}

} // namespace valcraft
