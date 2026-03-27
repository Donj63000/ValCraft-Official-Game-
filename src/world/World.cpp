#include "world/World.h"

#include <glm/geometric.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
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

constexpr std::array<ChunkCoord, 8> kMeshNeighborOffsets {{
    {-1, -1},
    {0, -1},
    {1, -1},
    {-1, 0},
    {1, 0},
    {-1, 1},
    {0, 1},
    {1, 1},
}};

constexpr std::array<BlockCoord, 6> kLightNeighborOffsets {{
    {1, 0, 0},
    {-1, 0, 0},
    {0, 1, 0},
    {0, -1, 0},
    {0, 0, 1},
    {0, 0, -1},
}};

constexpr auto kUnlimitedBudget = std::numeric_limits<std::size_t>::max() / 4U;
constexpr auto kSkyColumnCount = static_cast<std::size_t>(kChunkSizeX * kChunkSizeZ);
constexpr auto kMeshInvalidationPriorityRadius = 2;
constexpr std::size_t kLightingTimeCheckInterval = 128;

enum class LightingRegionSlot : std::uint8_t {
    Center = 0,
    PosX = 1,
    NegX = 2,
    PosZ = 3,
    NegZ = 4,
    Invalid = 255,
};

constexpr std::array<LightingRegionSlot, 5> kLightingRegionSlotOrder {{
    LightingRegionSlot::Center,
    LightingRegionSlot::PosX,
    LightingRegionSlot::NegX,
    LightingRegionSlot::PosZ,
    LightingRegionSlot::NegZ,
}};

auto lighting_region_slot_index(LightingRegionSlot slot) noexcept -> std::size_t {
    return static_cast<std::size_t>(slot);
}

auto lighting_region_slot_for(const ChunkCoord& anchor, const ChunkCoord& coord) noexcept -> LightingRegionSlot {
    const auto dx = coord.x - anchor.x;
    const auto dz = coord.z - anchor.z;
    if (dx == 0 && dz == 0) {
        return LightingRegionSlot::Center;
    }
    if (dx == 1 && dz == 0) {
        return LightingRegionSlot::PosX;
    }
    if (dx == -1 && dz == 0) {
        return LightingRegionSlot::NegX;
    }
    if (dx == 0 && dz == 1) {
        return LightingRegionSlot::PosZ;
    }
    if (dx == 0 && dz == -1) {
        return LightingRegionSlot::NegZ;
    }
    return LightingRegionSlot::Invalid;
}

auto lighting_region_coord_for(const ChunkCoord& anchor, LightingRegionSlot slot) noexcept -> ChunkCoord {
    switch (slot) {
    case LightingRegionSlot::Center:
        return anchor;
    case LightingRegionSlot::PosX:
        return {anchor.x + 1, anchor.z};
    case LightingRegionSlot::NegX:
        return {anchor.x - 1, anchor.z};
    case LightingRegionSlot::PosZ:
        return {anchor.x, anchor.z + 1};
    case LightingRegionSlot::NegZ:
        return {anchor.x, anchor.z - 1};
    case LightingRegionSlot::Invalid:
        break;
    }
    return anchor;
}

auto has_time_budget(double max_ms) noexcept -> bool {
    return std::isfinite(max_ms);
}

auto sky_column_index(int local_x, int local_z) noexcept -> std::size_t {
    return static_cast<std::size_t>(local_z * kChunkSizeX + local_x);
}

auto chunk_linear_index(int local_x, int local_y, int local_z) noexcept -> std::size_t {
    return static_cast<std::size_t>((local_y * kChunkSizeZ + local_z) * kChunkSizeX + local_x);
}

auto section_min_y(std::size_t section_index) noexcept -> int {
    return static_cast<int>(section_index * static_cast<std::size_t>(kChunkSectionHeight));
}

auto section_max_y(std::size_t section_index) noexcept -> int {
    return std::min(kWorldMaxY, section_min_y(section_index) + kChunkSectionHeight - 1);
}

void append_chunk_mesh_section(ChunkMeshData& destination, const ChunkMeshData& section_mesh) {
    if (!section_mesh.vertices.empty()) {
        const auto base_index = static_cast<std::uint32_t>(destination.vertices.size());
        destination.vertices.insert(destination.vertices.end(), section_mesh.vertices.begin(), section_mesh.vertices.end());
        destination.indices.reserve(destination.indices.size() + section_mesh.indices.size());
        for (const auto index : section_mesh.indices) {
            destination.indices.push_back(base_index + index);
        }
    }

    if (!section_mesh.water_vertices.empty()) {
        const auto base_water_index = static_cast<std::uint32_t>(destination.water_vertices.size());
        destination.water_vertices.insert(destination.water_vertices.end(), section_mesh.water_vertices.begin(), section_mesh.water_vertices.end());
        destination.water_indices.reserve(destination.water_indices.size() + section_mesh.water_indices.size());
        for (const auto index : section_mesh.water_indices) {
            destination.water_indices.push_back(base_water_index + index);
        }
    }

    destination.face_count += section_mesh.face_count;
    destination.water_face_count += section_mesh.water_face_count;
}

} // namespace

World::World(int seed, int stream_radius)
    : stream_radius_(stream_radius),
      generator_(seed) {
    const auto loaded_side = static_cast<std::size_t>(std::max(stream_radius_, 0) * 2 + 3);
    const auto max_loaded_chunks = loaded_side * loaded_side;
    chunks_.reserve(max_loaded_chunks);
    pending_generation_set_.reserve(max_loaded_chunks);
    pending_mesh_set_.reserve(max_loaded_chunks);
    pending_priority_mesh_set_.reserve(max_loaded_chunks);
    deferred_mesh_invalidation_set_.reserve(max_loaded_chunks);
    pending_lighting_set_.reserve(max_loaded_chunks);
    pending_lighting_coverage_.reserve(max_loaded_chunks);
    active_lighting_coverage_.reserve(kLightingRegionSlotOrder.size());
    pending_gpu_upload_set_.reserve(max_loaded_chunks);
    pending_gpu_unload_set_.reserve(max_loaded_chunks);
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

auto World::get_sky_light(int x, int y, int z) const -> std::uint8_t {
    if (!is_world_y_valid(y)) {
        return 0;
    }

    const auto chunk_coord = world_to_chunk(x, z);
    const auto local = world_to_local(x, y, z);
    const auto* chunk = find_chunk(chunk_coord);
    if (chunk == nullptr) {
        return 15;
    }
    return chunk->get_sky_light_local(local.x, local.y, local.z);
}

auto World::get_block_light(int x, int y, int z) const -> std::uint8_t {
    if (!is_world_y_valid(y)) {
        return 0;
    }

    const auto chunk_coord = world_to_chunk(x, z);
    const auto local = world_to_local(x, y, z);
    const auto* chunk = find_chunk(chunk_coord);
    if (chunk == nullptr) {
        return 0;
    }
    return chunk->get_block_light_local(local.x, local.y, local.z);
}

void World::set_block(int x, int y, int z, BlockId block_id) {
    if (!is_world_y_valid(y)) {
        return;
    }

    const auto chunk_coord = world_to_chunk(x, z);
    ensure_chunk_loaded(chunk_coord);

    const auto local = world_to_local(x, y, z);
    auto iterator = chunks_.find(chunk_coord);
    if (iterator == chunks_.end()) {
        throw std::runtime_error("Chunk disappeared during set_block");
    }
    auto& record = iterator->second;
    auto& chunk = record.chunk;

    const auto current_block = chunk.get_local(local.x, local.y, local.z);
    if (current_block == block_id) {
        return;
    }

    chunk.set_local(local.x, local.y, local.z, block_id);
    update_chunk_emissive_cache(record, local, current_block, block_id);
    mark_sky_column_dirty(chunk_coord, local.x, local.z);
    mark_chunk_and_neighbors_dirty(chunk_coord, local);
    mark_chunk_and_neighbors_lighting_dirty(chunk_coord);
    remove_unsupported_torch_above(x, y, z);
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
    if (is_block_targetable(starting_block)) {
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
        if (is_block_targetable(block_id)) {
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

auto World::can_place_torch_at(const BlockCoord& world_coord) const -> bool {
    if (!is_world_y_valid(world_coord.y) || world_coord.y == kWorldMinY) {
        return false;
    }
    const auto current_block = get_block(world_coord.x, world_coord.y, world_coord.z);
    if (current_block != to_block_id(BlockType::Air) &&
        (!is_block_replaceable(current_block) || is_block_liquid(current_block))) {
        return false;
    }

    const auto support_block = get_block(world_coord.x, world_coord.y - 1, world_coord.z);
    return is_block_opaque(support_block);
}

void World::ensure_chunk_loaded(const ChunkCoord& coord) {
    load_chunk_immediate(coord);
}

auto World::update_streaming(const glm::vec3& player_position) -> WorldStreamingStats {
    WorldStreamingStats stats {};
    const auto center = world_to_chunk(
        static_cast<int>(std::floor(player_position.x)),
        static_cast<int>(std::floor(player_position.z)));

    if (has_stream_center_ && center == stream_center_) {
        return stats;
    }

    stats.chunk_changed = true;
    stats.unloaded_chunks = unload_far_chunks(center);

    const auto previous_center = stream_center_;
    const auto had_previous_center = has_stream_center_;
    has_stream_center_ = true;
    stream_center_ = center;

    prune_generation_queue(stats);
    if (!had_previous_center) {
        enqueue_generation_area(center, stats);
    } else {
        enqueue_generation_ring_transition(previous_center, center, stats);
    }
    return stats;
}

auto World::process_pending_work(const WorldWorkBudget& budget) -> WorldWorkStats {
    using clock = std::chrono::steady_clock;
    WorldWorkStats stats {};

    const auto generation_start = clock::now();
    process_generation_queue(budget.chunk_generation_budget, budget.max_generation_ms, stats);
    stats.generation_ms =
        std::chrono::duration<double, std::milli>(clock::now() - generation_start).count();

    const auto lighting_start = clock::now();
    process_lighting_queue(budget.light_node_budget, budget.max_lighting_ms, stats);
    stats.lighting_ms =
        std::chrono::duration<double, std::milli>(clock::now() - lighting_start).count();
    flush_deferred_mesh_invalidations();

    const auto meshing_start = clock::now();
    process_mesh_queue(budget.mesh_rebuild_budget, budget.max_meshing_ms, stats);
    stats.meshing_ms =
        std::chrono::duration<double, std::milli>(clock::now() - meshing_start).count();

    stats.pending_generation = pending_generation_count();
    stats.pending_mesh = pending_mesh_count();
    stats.pending_lighting = pending_lighting_count();
    return stats;
}

void World::rebuild_lighting() {
    WorldWorkStats stats {};
    while (true) {
        enqueue_dirty_chunks();
        if (!active_lighting_job_.has_value() && pending_lighting_queue_.empty()) {
            break;
        }
        process_lighting_queue(kUnlimitedBudget, std::numeric_limits<double>::infinity(), stats);
    }
    flush_deferred_mesh_invalidations();
}

void World::rebuild_dirty_meshes() {
    rebuild_lighting();

    WorldWorkStats stats {};
    while (true) {
        enqueue_dirty_chunks();
        if (pending_priority_mesh_queue_.empty() && pending_mesh_queue_.empty()) {
            break;
        }
        process_mesh_queue(kUnlimitedBudget, std::numeric_limits<double>::infinity(), stats);
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

auto World::consume_pending_gpu_uploads(std::size_t max_count) -> std::vector<ChunkCoord> {
    std::vector<ChunkCoord> uploads;
    uploads.reserve(std::min(max_count, pending_gpu_uploads_.size()));
    while (!pending_gpu_uploads_.empty() && uploads.size() < max_count) {
        const auto coord = pending_gpu_uploads_.front();
        pending_gpu_uploads_.pop_front();
        if (!pending_gpu_upload_set_.erase(coord)) {
            continue;
        }
        if (!chunks_.contains(coord)) {
            continue;
        }
        uploads.push_back(coord);
    }
    return uploads;
}

auto World::consume_pending_gpu_unloads(std::size_t max_count) -> std::vector<ChunkCoord> {
    std::vector<ChunkCoord> unloads;
    unloads.reserve(std::min(max_count, pending_gpu_unloads_.size()));
    while (!pending_gpu_unloads_.empty() && unloads.size() < max_count) {
        const auto coord = pending_gpu_unloads_.front();
        pending_gpu_unloads_.pop_front();
        if (!pending_gpu_unload_set_.erase(coord)) {
            continue;
        }
        unloads.push_back(coord);
    }
    return unloads;
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

    return std::max(0, chunk->surface_height_local(local.x, local.z));
}

auto World::loaded_surface_height(int world_x, int world_z) const -> std::optional<int> {
    const auto coord = world_to_chunk(world_x, world_z);
    const auto* chunk = find_chunk(coord);
    if (chunk == nullptr) {
        return std::nullopt;
    }

    const auto local = world_to_local(world_x, 0, world_z);
    return std::max(0, chunk->surface_height_local(local.x, local.z));
}

auto World::pending_generation_count() const noexcept -> std::size_t {
    return pending_generation_queue_.size();
}

auto World::pending_mesh_count() const noexcept -> std::size_t {
    return pending_priority_mesh_queue_.size() + pending_mesh_queue_.size();
}

auto World::pending_lighting_count() const noexcept -> std::size_t {
    return pending_lighting_queue_.size() + (active_lighting_job_.has_value() ? 1U : 0U);
}

auto World::has_pending_work() const noexcept -> bool {
    return !pending_generation_queue_.empty() ||
           !pending_priority_mesh_queue_.empty() ||
           !pending_mesh_queue_.empty() ||
           !pending_lighting_queue_.empty() ||
           active_lighting_job_.has_value();
}

auto World::are_chunks_ready(const glm::vec3& player_position, int radius) const -> bool {
    const auto center = world_to_chunk(
        static_cast<int>(std::floor(player_position.x)),
        static_cast<int>(std::floor(player_position.z)));

    for (int dz = -radius; dz <= radius; ++dz) {
        for (int dx = -radius; dx <= radius; ++dx) {
            const ChunkCoord coord {center.x + dx, center.z + dz};
            if (pending_mesh_set_.contains(coord)) {
                return false;
            }

            const auto iterator = chunks_.find(coord);
            if (iterator == chunks_.end()) {
                return false;
            }

            const auto& record = iterator->second;
            if (record.mesh_revision == 0 || record.chunk.is_dirty() || record.chunk.is_lighting_dirty()) {
                return false;
            }
        }
    }

    return true;
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
        chunk->mark_section_dirty_for_y(local.y);
    }

    const auto touches_neg_x = local.x == 0;
    const auto touches_pos_x = local.x == kChunkSizeX - 1;
    const auto touches_neg_z = local.z == 0;
    const auto touches_pos_z = local.z == kChunkSizeZ - 1;

    if (touches_neg_x) {
        if (auto* neighbor = find_chunk({coord.x - 1, coord.z}); neighbor != nullptr) {
            neighbor->mark_section_dirty_for_y(local.y);
        }
    }
    if (touches_pos_x) {
        if (auto* neighbor = find_chunk({coord.x + 1, coord.z}); neighbor != nullptr) {
            neighbor->mark_section_dirty_for_y(local.y);
        }
    }
    if (touches_neg_z) {
        if (auto* neighbor = find_chunk({coord.x, coord.z - 1}); neighbor != nullptr) {
            neighbor->mark_section_dirty_for_y(local.y);
        }
    }
    if (touches_pos_z) {
        if (auto* neighbor = find_chunk({coord.x, coord.z + 1}); neighbor != nullptr) {
            neighbor->mark_section_dirty_for_y(local.y);
        }
    }

    if (touches_neg_x && touches_neg_z) {
        if (auto* neighbor = find_chunk({coord.x - 1, coord.z - 1}); neighbor != nullptr) {
            neighbor->mark_section_dirty_for_y(local.y);
        }
    }
    if (touches_neg_x && touches_pos_z) {
        if (auto* neighbor = find_chunk({coord.x - 1, coord.z + 1}); neighbor != nullptr) {
            neighbor->mark_section_dirty_for_y(local.y);
        }
    }
    if (touches_pos_x && touches_neg_z) {
        if (auto* neighbor = find_chunk({coord.x + 1, coord.z - 1}); neighbor != nullptr) {
            neighbor->mark_section_dirty_for_y(local.y);
        }
    }
    if (touches_pos_x && touches_pos_z) {
        if (auto* neighbor = find_chunk({coord.x + 1, coord.z + 1}); neighbor != nullptr) {
            neighbor->mark_section_dirty_for_y(local.y);
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

void World::mark_sky_column_dirty(const ChunkCoord& coord, int local_x, int local_z) {
    const auto iterator = chunks_.find(coord);
    if (iterator == chunks_.end()) {
        return;
    }

    iterator->second.sky_columns_dirty.set(sky_column_index(local_x, local_z));
}

void World::mark_chunk_and_neighbors_lighting_dirty(const ChunkCoord& coord) {
    if (auto* chunk = find_chunk(coord); chunk != nullptr) {
        chunk->mark_lighting_dirty();
    }
    enqueue_lighting_update(coord);
}

void World::load_chunk_immediate(const ChunkCoord& coord) {
    if (chunks_.contains(coord)) {
        return;
    }

    auto [iterator, inserted] = chunks_.try_emplace(coord, coord);
    if (!inserted) {
        return;
    }

    generator_.generate_chunk(iterator->second.chunk);
    iterator->second.emissive_blocks.clear();
    iterator->second.sky_columns_dirty.set();
    iterator->second.chunk.mark_dirty();
    iterator->second.chunk.mark_lighting_dirty();
    enqueue_lighting_update(coord);
    invalidate_loaded_mesh_neighbors_for_chunk_load(coord);
}

void World::enqueue_generation_candidate(const ChunkCoord& coord, WorldStreamingStats* stats) {
    if (chunks_.contains(coord) || pending_generation_set_.contains(coord) || !is_inside_active_stream(coord)) {
        return;
    }

    pending_generation_queue_.push_back(coord);
    pending_generation_set_.insert(coord);
    if (stats != nullptr) {
        ++stats->generation_enqueued;
    }
}

void World::enqueue_generation_area(const ChunkCoord& center, WorldStreamingStats& stats) {
    enqueue_generation_candidate(center, &stats);
    for (int radius = 1; radius <= stream_radius_; ++radius) {
        const auto min_x = center.x - radius;
        const auto max_x = center.x + radius;
        const auto min_z = center.z - radius;
        const auto max_z = center.z + radius;

        for (int x = min_x; x <= max_x; ++x) {
            enqueue_generation_candidate({x, min_z}, &stats);
            enqueue_generation_candidate({x, max_z}, &stats);
        }
        for (int z = min_z + 1; z < max_z; ++z) {
            enqueue_generation_candidate({min_x, z}, &stats);
            enqueue_generation_candidate({max_x, z}, &stats);
        }
    }
}

void World::enqueue_generation_ring_transition(const ChunkCoord& previous_center,
                                               const ChunkCoord& next_center,
                                               WorldStreamingStats& stats) {
    const auto dx = next_center.x - previous_center.x;
    const auto dz = next_center.z - previous_center.z;
    if (std::abs(dx) > 1 || std::abs(dz) > 1) {
        enqueue_generation_area(next_center, stats);
        return;
    }

    if (dx != 0) {
        const auto x = next_center.x + (dx > 0 ? stream_radius_ : -stream_radius_);
        for (int z = next_center.z - stream_radius_; z <= next_center.z + stream_radius_; ++z) {
            enqueue_generation_candidate({x, z}, &stats);
        }
    }

    if (dz != 0) {
        const auto z = next_center.z + (dz > 0 ? stream_radius_ : -stream_radius_);
        for (int x = next_center.x - stream_radius_; x <= next_center.x + stream_radius_; ++x) {
            enqueue_generation_candidate({x, z}, &stats);
        }
    }
}

void World::prune_generation_queue(WorldStreamingStats& stats) {
    std::deque<ChunkCoord> kept_coords;
    while (!pending_generation_queue_.empty()) {
        const auto coord = pending_generation_queue_.front();
        pending_generation_queue_.pop_front();

        if (chunks_.contains(coord) || !is_inside_active_stream(coord)) {
            pending_generation_set_.erase(coord);
            ++stats.generation_pruned;
            continue;
        }

        kept_coords.push_back(coord);
    }

    pending_generation_queue_ = std::move(kept_coords);
}

void World::invalidate_loaded_mesh_neighbors(const ChunkCoord& coord, bool defer_if_lighting_pending) {
    // Chunk meshes bake AO and vertex light from a 3x3 chunk neighborhood, so any
    // block/light availability change must invalidate already-built surrounding meshes.
    for (const auto& offset : kMeshNeighborOffsets) {
        const ChunkCoord neighbor_coord {coord.x + offset.x, coord.z + offset.z};
        const auto iterator = chunks_.find(neighbor_coord);
        if (iterator == chunks_.end() || iterator->second.mesh_revision == 0) {
            continue;
        }

        if (defer_if_lighting_pending &&
            (lighting_anchor_affects(neighbor_coord, coord) || chunk_has_pending_lighting(neighbor_coord))) {
            deferred_mesh_invalidation_set_.insert(neighbor_coord);
            continue;
        }

        iterator->second.chunk.mark_dirty();
        enqueue_mesh_rebuild(neighbor_coord, should_prioritize_mesh_invalidation(neighbor_coord));
    }
}

void World::invalidate_loaded_mesh_neighbors_for_sections(
    const ChunkCoord& coord,
    const std::array<bool, kChunkSectionCount>& dirty_sections) {
    for (const auto& offset : kMeshNeighborOffsets) {
        const ChunkCoord neighbor_coord {coord.x + offset.x, coord.z + offset.z};
        auto* neighbor = find_chunk(neighbor_coord);
        if (neighbor == nullptr) {
            continue;
        }

        const auto mesh_iterator = chunks_.find(neighbor_coord);
        if (mesh_iterator == chunks_.end() || mesh_iterator->second.mesh_revision == 0) {
            continue;
        }

        bool neighbor_dirty = false;
        for (std::size_t section_index = 0; section_index < dirty_sections.size(); ++section_index) {
            if (!dirty_sections[section_index]) {
                continue;
            }
            neighbor->mark_section_dirty(section_index);
            if (section_index > 0) {
                neighbor->mark_section_dirty(section_index - 1);
            }
            if (section_index + 1 < dirty_sections.size()) {
                neighbor->mark_section_dirty(section_index + 1);
            }
            neighbor_dirty = true;
        }

        if (!neighbor_dirty) {
            continue;
        }

        enqueue_mesh_rebuild(neighbor_coord, should_prioritize_mesh_invalidation(neighbor_coord));
    }
}

void World::invalidate_loaded_mesh_neighbors_for_chunk_load(const ChunkCoord& coord) {
    invalidate_loaded_mesh_neighbors(coord, true);
}

void World::flush_deferred_mesh_invalidations() {
    if (active_lighting_job_.has_value() || !pending_lighting_queue_.empty()) {
        return;
    }

    for (auto iterator = deferred_mesh_invalidation_set_.begin(); iterator != deferred_mesh_invalidation_set_.end();) {
        const auto coord = *iterator;
        const auto chunk_iterator = chunks_.find(coord);
        if (chunk_iterator != chunks_.end() && chunk_iterator->second.mesh_revision > 0) {
            chunk_iterator->second.chunk.mark_dirty();
            enqueue_mesh_rebuild(coord, should_prioritize_mesh_invalidation(coord));
        }
        iterator = deferred_mesh_invalidation_set_.erase(iterator);
    }
}

void World::enqueue_lighting_update(const ChunkCoord& coord) {
    if (!chunks_.contains(coord) || pending_lighting_set_.contains(coord)) {
        return;
    }

    const auto coverage = collect_lighting_region(coord);
    if (coverage.empty()) {
        return;
    }

    const auto overlaps_pending = std::any_of(coverage.begin(), coverage.end(), [&](const ChunkCoord& covered_coord) {
        return pending_lighting_coverage_.contains(covered_coord);
    });
    if (overlaps_pending) {
        return;
    }

    pending_lighting_queue_.push_back({coord, coverage});
    pending_lighting_set_.insert(coord);
    for (const auto& covered_coord : coverage) {
        pending_lighting_coverage_.insert(covered_coord);
    }
}

void World::rebuild_pending_lighting_metadata() {
    pending_lighting_set_.clear();
    pending_lighting_coverage_.clear();
    for (const auto& update : pending_lighting_queue_) {
        pending_lighting_set_.insert(update.anchor);
        for (const auto& covered_coord : update.coverage) {
            pending_lighting_coverage_.insert(covered_coord);
        }
    }
}

void World::enqueue_mesh_rebuild(const ChunkCoord& coord, bool prioritize) {
    if (!chunks_.contains(coord) || chunk_has_pending_lighting(coord)) {
        return;
    }

    if (prioritize) {
        if (pending_priority_mesh_set_.contains(coord)) {
            return;
        }
        pending_priority_mesh_queue_.push_back(coord);
        pending_priority_mesh_set_.insert(coord);
        pending_mesh_set_.insert(coord);
        return;
    }

    if (pending_mesh_set_.contains(coord)) {
        return;
    }

    pending_mesh_queue_.push_back(coord);
    pending_mesh_set_.insert(coord);
}

void World::enqueue_dirty_chunks() {
    for (auto& [coord, record] : chunks_) {
        if (record.chunk.is_lighting_dirty()) {
            if (record.sky_columns_dirty.none()) {
                record.sky_columns_dirty.set();
            }
            enqueue_lighting_update(coord);
        } else if (record.chunk.is_dirty() && !chunk_has_pending_lighting(coord)) {
            enqueue_mesh_rebuild(coord);
        }
    }
}

void World::process_generation_queue(std::size_t budget, double max_ms, WorldWorkStats& stats) {
    using clock = std::chrono::steady_clock;

    if (budget == 0) {
        return;
    }

    const auto time_limited = has_time_budget(max_ms);
    if (time_limited && max_ms <= 0.0) {
        return;
    }

    const auto deadline = clock::now() + std::chrono::duration<double, std::milli>(std::max(0.0, max_ms));
    auto remaining = budget;
    while (remaining > 0 && !pending_generation_queue_.empty()) {
        if (time_limited && clock::now() >= deadline) {
            break;
        }

        const auto coord = pending_generation_queue_.front();
        pending_generation_queue_.pop_front();
        pending_generation_set_.erase(coord);

        if (chunks_.contains(coord) || !is_inside_active_stream(coord)) {
            continue;
        }

        load_chunk_immediate(coord);
        ++stats.generated_chunks;
        --remaining;
    }
}

void World::process_lighting_queue(std::size_t budget, double max_ms, WorldWorkStats& stats) {
    using clock = std::chrono::steady_clock;

    auto remaining = budget;
    const auto time_limited = has_time_budget(max_ms);
    if (time_limited && max_ms <= 0.0) {
        return;
    }

    const auto deadline = clock::now() + std::chrono::duration<double, std::milli>(std::max(0.0, max_ms));
    std::size_t processed_since_deadline_check = 0;

    while (true) {
        if (!active_lighting_job_.has_value()) {
            while (!pending_lighting_queue_.empty()) {
                const auto pending_update = std::move(pending_lighting_queue_.front());
                pending_lighting_queue_.pop_front();
                pending_lighting_set_.erase(pending_update.anchor);
                for (const auto& covered_coord : pending_update.coverage) {
                    pending_lighting_coverage_.erase(covered_coord);
                }

                LightingJob job {};
                job.anchor = pending_update.anchor;
                if (!initialize_lighting_job(job)) {
                    continue;
                }

                active_lighting_coverage_.clear();
                for (std::size_t slot_index = 0; slot_index < kLightingRegionSlotOrder.size(); ++slot_index) {
                    if (!job.region_present[slot_index]) {
                        continue;
                    }
                    active_lighting_coverage_.insert(job.region_coords[slot_index]);
                }
                active_lighting_job_ = std::move(job);
                break;
            }

            if (!active_lighting_job_.has_value()) {
                break;
            }
        }

        auto& job = *active_lighting_job_;
        if (job.queue.empty()) {
            active_lighting_coverage_.clear();
            finalize_lighting_job(job);
            active_lighting_job_.reset();
            ++stats.lighting_jobs_completed;
            continue;
        }

        if (remaining == 0) {
            break;
        }
        if (time_limited && processed_since_deadline_check == 0 && clock::now() >= deadline) {
            break;
        }

        const auto node = job.queue.front();
        job.queue.pop_front();
        ++stats.light_nodes_processed;
        --remaining;
        ++processed_since_deadline_check;
        if (processed_since_deadline_check == kLightingTimeCheckInterval) {
            processed_since_deadline_check = 0;
        }

        if (node.light_level <= 1) {
            continue;
        }

        for (const auto& offset : kLightNeighborOffsets) {
            const auto neighbor = BlockCoord {
                node.world_coord.x + offset.x,
                node.world_coord.y + offset.y,
                node.world_coord.z + offset.z,
            };

            if (!is_world_y_valid(neighbor.y)) {
                continue;
            }

            const auto chunk_coord = world_to_chunk(neighbor.x, neighbor.z);
            if (!lighting_region_contains(job, chunk_coord)) {
                continue;
            }

            if (is_block_opaque(get_block(neighbor.x, neighbor.y, neighbor.z))) {
                continue;
            }

            const auto propagated = static_cast<std::uint8_t>(node.light_level - 1);
            if (propagated <= get_job_block_light(job, neighbor)) {
                continue;
            }

            if (set_job_block_light(job, neighbor, propagated)) {
                job.queue.push_back({neighbor, propagated});
            }
        }
    }
}

void World::process_mesh_queue(std::size_t budget, double max_ms, WorldWorkStats& stats) {
    using clock = std::chrono::steady_clock;

    const auto time_limited = has_time_budget(max_ms);
    if (time_limited && max_ms <= 0.0) {
        return;
    }

    const auto deadline = clock::now() + std::chrono::duration<double, std::milli>(std::max(0.0, max_ms));
    auto remaining_normal = budget;
    while (!pending_priority_mesh_queue_.empty() ||
           (remaining_normal > 0 && !pending_mesh_queue_.empty())) {
        const auto prioritize = !pending_priority_mesh_queue_.empty();
        if (!prioritize && time_limited && clock::now() >= deadline) {
            break;
        }

        const auto coord = prioritize ? pending_priority_mesh_queue_.front() : pending_mesh_queue_.front();
        if (prioritize) {
            pending_priority_mesh_queue_.pop_front();
            pending_priority_mesh_set_.erase(coord);
            if (!pending_mesh_set_.contains(coord)) {
                continue;
            }
        } else {
            pending_mesh_queue_.pop_front();
            if (!pending_mesh_set_.contains(coord) || pending_priority_mesh_set_.contains(coord)) {
                continue;
            }
        }
        pending_mesh_set_.erase(coord);

        if (chunk_has_pending_lighting(coord)) {
            continue;
        }

        const auto iterator = chunks_.find(coord);
        if (iterator == chunks_.end() || !iterator->second.chunk.is_dirty()) {
            continue;
        }

        rebuild_chunk_mesh(iterator->second);
        ++stats.meshed_chunks;
        if (!prioritize) {
            --remaining_normal;
        } else {
            ++stats.prioritized_meshed_chunks;
        }
    }
}

auto World::collect_lighting_region(const ChunkCoord& anchor) const -> std::vector<ChunkCoord> {
    std::vector<ChunkCoord> region;
    region.reserve(kLightingRegionSlotOrder.size());

    for (const auto slot : kLightingRegionSlotOrder) {
        const auto coord = lighting_region_coord_for(anchor, slot);
        if (find_chunk(coord) != nullptr) {
            region.push_back(coord);
        }
    }

    return region;
}

auto World::initialize_lighting_job(LightingJob& job) -> bool {
    job.queue.clear();
    job.region_present.fill(false);
    job.changed_chunks.fill(false);
    job.block_light_difference_counts.fill(0U);
    for (auto& column_bits : job.processed_sky_columns) {
        column_bits.reset();
    }
    for (auto& block_light_buffer : job.block_light_buffers) {
        block_light_buffer.assign(kChunkVolume, 0);
    }

    bool has_loaded_chunk = false;
    for (std::size_t slot_index = 0; slot_index < kLightingRegionSlotOrder.size(); ++slot_index) {
        const auto coord = lighting_region_coord_for(job.anchor, kLightingRegionSlotOrder[slot_index]);
        job.region_coords[slot_index] = coord;
        const auto iterator = chunks_.find(coord);
        if (iterator == chunks_.end()) {
            continue;
        }

        has_loaded_chunk = true;
        job.region_present[slot_index] = true;
        job.block_light_difference_counts[slot_index] = static_cast<std::size_t>(std::count_if(
            iterator->second.chunk.block_light().begin(),
            iterator->second.chunk.block_light().end(),
            [](std::uint8_t light_level) {
                return light_level != 0;
            }));
        job.processed_sky_columns[slot_index] = iterator->second.sky_columns_dirty;
        iterator->second.sky_columns_dirty.reset();
    }

    if (!has_loaded_chunk) {
        return false;
    }

    rebuild_local_sky_light(job);
    seed_local_block_lighting(job);
    return true;
}

void World::rebuild_local_sky_light(LightingJob& job) {
    for (std::size_t slot_index = 0; slot_index < kLightingRegionSlotOrder.size(); ++slot_index) {
        if (!job.region_present[slot_index]) {
            continue;
        }

        const auto coord = job.region_coords[slot_index];
        auto iterator = chunks_.find(coord);
        if (iterator == chunks_.end()) {
            continue;
        }

        auto& chunk = iterator->second.chunk;
        auto column_bits = job.processed_sky_columns[slot_index];
        for (std::size_t bit_index = 0; bit_index < kSkyColumnCount; ++bit_index) {
            if (!column_bits.test(bit_index)) {
                continue;
            }

            const auto local_x = static_cast<int>(bit_index % static_cast<std::size_t>(kChunkSizeX));
            const auto local_z = static_cast<int>(bit_index / static_cast<std::size_t>(kChunkSizeX));
            if (chunk.rebuild_sky_light_column(local_x, local_z)) {
                mark_lighting_chunk_changed(job, coord);
            }
        }
    }
}

void World::seed_local_block_lighting(LightingJob& job) {
    for (std::size_t slot_index = 0; slot_index < kLightingRegionSlotOrder.size(); ++slot_index) {
        if (!job.region_present[slot_index]) {
            continue;
        }

        const auto coord = job.region_coords[slot_index];
        const auto iterator = chunks_.find(coord);
        if (iterator == chunks_.end()) {
            continue;
        }

        for (const auto& local_emitter : iterator->second.emissive_blocks) {
            const auto block_id = iterator->second.chunk.get_local(local_emitter.x, local_emitter.y, local_emitter.z);
            const auto emissive = block_emissive_level(block_id);
            if (emissive == 0) {
                continue;
            }

            const auto world_coord = local_to_world(coord, local_emitter);
            if (set_job_block_light(job, world_coord, emissive)) {
                job.queue.push_back({world_coord, emissive});
            }
        }
    }

    const auto seed_boundary_from_neighbor = [&](const ChunkCoord& coord,
                                                 int local_x,
                                                 int y,
                                                 int local_z,
                                                 const ChunkCoord& neighbor_coord,
                                                  int neighbor_x,
                                                  int neighbor_z) {
        if (lighting_region_contains(job, neighbor_coord)) {
            return;
        }

        auto* chunk = find_chunk(coord);
        const auto* neighbor_chunk = find_chunk(neighbor_coord);
        if (chunk == nullptr || neighbor_chunk == nullptr) {
            return;
        }

        if (is_block_opaque(chunk->get_local(local_x, y, local_z)) ||
            is_block_opaque(neighbor_chunk->get_local(neighbor_x, y, neighbor_z))) {
            return;
        }

        const auto neighbor_light = neighbor_chunk->get_block_light_local(neighbor_x, y, neighbor_z);
        if (neighbor_light <= 1) {
            return;
        }

        const auto propagated = static_cast<std::uint8_t>(neighbor_light - 1);
        const BlockCoord target_local {local_x, y, local_z};
        const auto target_world = local_to_world(coord, target_local);
        if (propagated <= get_job_block_light(job, target_world)) {
            return;
        }

        if (set_job_block_light(job, target_world, propagated)) {
            job.queue.push_back({target_world, propagated});
        }
    };

    for (std::size_t slot_index = 0; slot_index < kLightingRegionSlotOrder.size(); ++slot_index) {
        if (!job.region_present[slot_index]) {
            continue;
        }

        const auto coord = job.region_coords[slot_index];
        for (int y = kWorldMinY; y <= kWorldMaxY; ++y) {
            for (int z = 0; z < kChunkSizeZ; ++z) {
                seed_boundary_from_neighbor(coord, 0, y, z, {coord.x - 1, coord.z}, kChunkSizeX - 1, z);
                seed_boundary_from_neighbor(coord, kChunkSizeX - 1, y, z, {coord.x + 1, coord.z}, 0, z);
            }
            for (int x = 0; x < kChunkSizeX; ++x) {
                seed_boundary_from_neighbor(coord, x, y, 0, {coord.x, coord.z - 1}, x, kChunkSizeZ - 1);
                seed_boundary_from_neighbor(coord, x, y, kChunkSizeZ - 1, {coord.x, coord.z + 1}, x, 0);
            }
        }
    }
}

void World::finalize_lighting_job(const LightingJob& job) {
    for (std::size_t slot_index = 0; slot_index < kLightingRegionSlotOrder.size(); ++slot_index) {
        if (!job.region_present[slot_index]) {
            continue;
        }

        const auto coord = job.region_coords[slot_index];
        auto iterator = chunks_.find(coord);
        if (iterator == chunks_.end()) {
            continue;
        }

        auto& record = iterator->second;
        const auto block_light_changed = job.block_light_difference_counts[slot_index] > 0;
        std::array<bool, kChunkSectionCount> changed_sections {};
        if (block_light_changed) {
            const auto& current_block_light = record.chunk.block_light();
            for (std::size_t section_index = 0; section_index < kChunkSectionCount; ++section_index) {
                const auto min_y = section_min_y(section_index);
                const auto max_y = section_max_y(section_index);
                bool section_changed = false;
                for (int y = min_y; y <= max_y && !section_changed; ++y) {
                    for (int z = 0; z < kChunkSizeZ && !section_changed; ++z) {
                        for (int x = 0; x < kChunkSizeX; ++x) {
                            const auto buffer_index = chunk_linear_index(x, y, z);
                            if (current_block_light[buffer_index] != job.block_light_buffers[slot_index][buffer_index]) {
                                section_changed = true;
                                break;
                            }
                        }
                    }
                }
                changed_sections[section_index] = section_changed;
            }
            record.chunk.copy_block_light_from(job.block_light_buffers[slot_index].data(), job.block_light_buffers[slot_index].size());
        }

        const auto lighting_changed = block_light_changed || job.changed_chunks[slot_index];
        if (lighting_changed) {
            if (job.changed_chunks[slot_index]) {
                record.chunk.mark_dirty();
                invalidate_loaded_mesh_neighbors(coord, false);
            } else {
                for (std::size_t section_index = 0; section_index < changed_sections.size(); ++section_index) {
                    if (changed_sections[section_index]) {
                        record.chunk.mark_section_dirty(section_index);
                    }
                }
                invalidate_loaded_mesh_neighbors_for_sections(coord, changed_sections);
            }
        }

        record.chunk.clear_lighting_dirty();
        if (record.chunk.is_dirty()) {
            enqueue_mesh_rebuild(coord);
        }
    }
}

auto World::unload_far_chunks(const ChunkCoord& center) -> std::size_t {
    std::vector<ChunkCoord> to_remove;
    to_remove.reserve(chunks_.size());

    const auto unload_radius = stream_radius_ + 1;
    for (const auto& [coord, record] : chunks_) {
        (void)record;
        const auto dx = std::abs(coord.x - center.x);
        const auto dz = std::abs(coord.z - center.z);
        if (dx > unload_radius || dz > unload_radius) {
            to_remove.push_back(coord);
        }
    }

    if (to_remove.empty()) {
        return 0;
    }

    const auto should_remove_coord = [&](const ChunkCoord& coord) {
        return std::find(to_remove.begin(), to_remove.end(), coord) != to_remove.end();
    };

    if (active_lighting_job_.has_value()) {
        bool overlaps_removed = false;
        for (std::size_t slot_index = 0; slot_index < kLightingRegionSlotOrder.size(); ++slot_index) {
            if (!active_lighting_job_->region_present[slot_index]) {
                continue;
            }
            if (should_remove_coord(active_lighting_job_->region_coords[slot_index])) {
                overlaps_removed = true;
                break;
            }
        }
        if (overlaps_removed) {
            active_lighting_job_.reset();
            active_lighting_coverage_.clear();
        }
    }

    for (const auto& coord : to_remove) {
        for (const auto& offset : kNeighborOffsets) {
            const ChunkCoord neighbor_coord {coord.x + offset.x, coord.z + offset.z};
            if (auto* neighbor = find_chunk(neighbor_coord); neighbor != nullptr) {
                neighbor->mark_dirty();
                neighbor->mark_lighting_dirty();
                enqueue_lighting_update(neighbor_coord);
            }
        }
        pending_generation_set_.erase(coord);
        pending_mesh_set_.erase(coord);
        pending_priority_mesh_set_.erase(coord);
        deferred_mesh_invalidation_set_.erase(coord);
        pending_lighting_set_.erase(coord);
        pending_lighting_coverage_.erase(coord);
        active_lighting_coverage_.erase(coord);
        enqueue_gpu_unload(coord);
        chunks_.erase(coord);
        invalidate_loaded_mesh_neighbors(coord, true);
    }

    std::deque<ChunkCoord> kept_priority_meshes;
    while (!pending_priority_mesh_queue_.empty()) {
        const auto coord = pending_priority_mesh_queue_.front();
        pending_priority_mesh_queue_.pop_front();
        if (chunks_.contains(coord)) {
            kept_priority_meshes.push_back(coord);
        }
    }
    pending_priority_mesh_queue_ = std::move(kept_priority_meshes);

    std::deque<ChunkCoord> kept_meshes;
    while (!pending_mesh_queue_.empty()) {
        const auto coord = pending_mesh_queue_.front();
        pending_mesh_queue_.pop_front();
        if (chunks_.contains(coord)) {
            kept_meshes.push_back(coord);
        }
    }
    pending_mesh_queue_ = std::move(kept_meshes);
    pending_mesh_set_.clear();
    pending_priority_mesh_set_.clear();
    for (const auto& coord : pending_priority_mesh_queue_) {
        pending_priority_mesh_set_.insert(coord);
        pending_mesh_set_.insert(coord);
    }
    for (const auto& coord : pending_mesh_queue_) {
        pending_mesh_set_.insert(coord);
    }

    std::deque<PendingLightingUpdate> kept_lighting_updates;
    while (!pending_lighting_queue_.empty()) {
        auto update = std::move(pending_lighting_queue_.front());
        pending_lighting_queue_.pop_front();
        if (!chunks_.contains(update.anchor)) {
            continue;
        }

        update.coverage.erase(
            std::remove_if(update.coverage.begin(), update.coverage.end(), [&](const ChunkCoord& covered_coord) {
                return !chunks_.contains(covered_coord);
            }),
            update.coverage.end());
        if (update.coverage.empty()) {
            continue;
        }

        kept_lighting_updates.push_back(std::move(update));
    }
    pending_lighting_queue_ = std::move(kept_lighting_updates);
    rebuild_pending_lighting_metadata();

    return to_remove.size();
}

void World::rebuild_chunk_mesh(ChunkRecord& record) {
    auto rebuilt_any_section = false;
    for (std::size_t section_index = 0; section_index < kChunkSectionCount; ++section_index) {
        if (!record.chunk.is_section_dirty(section_index)) {
            continue;
        }

        record.section_meshes[section_index] = mesher_.build_mesh_range(
            *this,
            record.chunk.coord(),
            section_min_y(section_index),
            section_max_y(section_index),
            record.section_mesh_vertex_capacity_hints[section_index],
            record.section_mesh_index_capacity_hints[section_index]);
        record.section_mesh_vertex_capacity_hints[section_index] =
            std::max(record.section_meshes[section_index].total_vertex_count(), static_cast<std::size_t>(128));
        record.section_mesh_index_capacity_hints[section_index] =
            std::max(record.section_meshes[section_index].total_index_count(), static_cast<std::size_t>(192));
        record.chunk.clear_section_dirty(section_index);
        rebuilt_any_section = true;
    }

    if (!rebuilt_any_section) {
        return;
    }

    ChunkMeshData merged_mesh {};
    merged_mesh.vertices.reserve(record.mesh_vertex_capacity_hint > 0 ? record.mesh_vertex_capacity_hint : 256U);
    merged_mesh.indices.reserve(record.mesh_index_capacity_hint > 0 ? record.mesh_index_capacity_hint : 384U);
    merged_mesh.water_vertices.reserve(128U);
    merged_mesh.water_indices.reserve(192U);
    for (const auto& section_mesh : record.section_meshes) {
        append_chunk_mesh_section(merged_mesh, section_mesh);
    }

    record.mesh = std::move(merged_mesh);
    record.mesh_vertex_capacity_hint = std::max(record.mesh.total_vertex_count(), static_cast<std::size_t>(256));
    record.mesh_index_capacity_hint = std::max(record.mesh.total_index_count(), static_cast<std::size_t>(384));
    ++record.mesh_revision;
    enqueue_gpu_upload(record.chunk.coord());
}

void World::enqueue_gpu_upload(const ChunkCoord& coord) {
    pending_gpu_unload_set_.erase(coord);
    if (chunks_.contains(coord) && pending_gpu_upload_set_.insert(coord).second) {
        pending_gpu_uploads_.push_back(coord);
    }
}

void World::enqueue_gpu_unload(const ChunkCoord& coord) {
    pending_gpu_upload_set_.erase(coord);
    if (pending_gpu_unload_set_.insert(coord).second) {
        pending_gpu_unloads_.push_back(coord);
    }
}

void World::remove_unsupported_torch_above(int x, int y, int z) {
    const auto torch_y = y + 1;
    if (!is_world_y_valid(torch_y)) {
        return;
    }

    const BlockCoord torch_coord {x, torch_y, z};
    if (get_block(torch_coord.x, torch_coord.y, torch_coord.z) != to_block_id(BlockType::Torch)) {
        return;
    }
    if (can_place_torch_at(torch_coord)) {
        return;
    }

    set_block(torch_coord.x, torch_coord.y, torch_coord.z, to_block_id(BlockType::Air));
}

void World::refresh_chunk_emissive_cache(ChunkRecord& record) {
    record.emissive_blocks.clear();
    const auto& blocks = record.chunk.blocks();
    for (int y = kWorldMinY; y <= kWorldMaxY; ++y) {
        for (int z = 0; z < kChunkSizeZ; ++z) {
            for (int x = 0; x < kChunkSizeX; ++x) {
                const auto block_id = blocks[chunk_linear_index(x, y, z)];
                if (block_emissive_level(block_id) == 0) {
                    continue;
                }

                record.emissive_blocks.push_back({x, y, z});
            }
        }
    }
}

void World::update_chunk_emissive_cache(ChunkRecord& record,
                                        const BlockCoord& local_coord,
                                        BlockId previous_block,
                                        BlockId next_block) {
    const auto previous_emissive = block_emissive_level(previous_block);
    const auto next_emissive = block_emissive_level(next_block);
    if (previous_emissive == 0 && next_emissive == 0) {
        return;
    }

    record.emissive_blocks.erase(
        std::remove(record.emissive_blocks.begin(), record.emissive_blocks.end(), local_coord),
        record.emissive_blocks.end());
    if (next_emissive > 0) {
        record.emissive_blocks.push_back(local_coord);
    }
}

auto World::lighting_region_contains(const LightingJob& job, const ChunkCoord& coord) const noexcept -> bool {
    const auto slot = lighting_region_slot_for(job.anchor, coord);
    if (slot == LightingRegionSlot::Invalid) {
        return false;
    }
    return job.region_present[lighting_region_slot_index(slot)];
}

void World::mark_lighting_chunk_changed(LightingJob& job, const ChunkCoord& coord) {
    const auto slot = lighting_region_slot_for(job.anchor, coord);
    if (slot == LightingRegionSlot::Invalid) {
        return;
    }

    const auto slot_index = lighting_region_slot_index(slot);
    if (!job.region_present[slot_index]) {
        return;
    }
    job.changed_chunks[slot_index] = true;
}

auto World::lighting_buffer_index(const BlockCoord& local_coord) const noexcept -> std::size_t {
    return static_cast<std::size_t>((local_coord.y * kChunkSizeZ + local_coord.z) * kChunkSizeX + local_coord.x);
}

auto World::get_job_block_light(const LightingJob& job, const BlockCoord& world_coord) const -> std::uint8_t {
    const auto chunk_coord = world_to_chunk(world_coord.x, world_coord.z);
    const auto slot = lighting_region_slot_for(job.anchor, chunk_coord);
    if (slot == LightingRegionSlot::Invalid) {
        return get_block_light(world_coord.x, world_coord.y, world_coord.z);
    }

    const auto slot_index = lighting_region_slot_index(slot);
    if (!job.region_present[slot_index]) {
        return get_block_light(world_coord.x, world_coord.y, world_coord.z);
    }

    const auto local_coord = world_to_local(world_coord.x, world_coord.y, world_coord.z);
    return job.block_light_buffers[slot_index][lighting_buffer_index(local_coord)];
}

auto World::set_job_block_light(LightingJob& job, const BlockCoord& world_coord, std::uint8_t light_level) -> bool {
    const auto chunk_coord = world_to_chunk(world_coord.x, world_coord.z);
    const auto slot = lighting_region_slot_for(job.anchor, chunk_coord);
    if (slot == LightingRegionSlot::Invalid) {
        return false;
    }

    const auto slot_index = lighting_region_slot_index(slot);
    if (!job.region_present[slot_index]) {
        return false;
    }

    const auto local_coord = world_to_local(world_coord.x, world_coord.y, world_coord.z);
    const auto buffer_index = lighting_buffer_index(local_coord);
    auto& current_light = job.block_light_buffers[slot_index][buffer_index];
    const auto clamped_light = static_cast<std::uint8_t>(std::min<int>(light_level, 15));
    if (current_light == clamped_light) {
        return false;
    }

    const auto iterator = chunks_.find(chunk_coord);
    if (iterator == chunks_.end()) {
        return false;
    }

    const auto original_light = iterator->second.chunk.block_light()[buffer_index];
    const auto was_different = current_light != original_light;
    const auto will_be_different = clamped_light != original_light;
    current_light = clamped_light;
    if (!was_different && will_be_different) {
        ++job.block_light_difference_counts[slot_index];
    } else if (was_different && !will_be_different && job.block_light_difference_counts[slot_index] > 0) {
        --job.block_light_difference_counts[slot_index];
    }
    return true;
}

auto World::is_inside_active_stream(const ChunkCoord& coord) const noexcept -> bool {
    if (!has_stream_center_) {
        return true;
    }

    const auto dx = std::abs(coord.x - stream_center_.x);
    const auto dz = std::abs(coord.z - stream_center_.z);
    return dx <= stream_radius_ && dz <= stream_radius_;
}

auto World::should_prioritize_mesh_invalidation(const ChunkCoord& coord) const noexcept -> bool {
    if (!has_stream_center_) {
        return true;
    }

    const auto dx = std::abs(coord.x - stream_center_.x);
    const auto dz = std::abs(coord.z - stream_center_.z);
    return dx <= kMeshInvalidationPriorityRadius && dz <= kMeshInvalidationPriorityRadius;
}

auto World::chunk_has_pending_lighting(const ChunkCoord& coord) const noexcept -> bool {
    return active_lighting_coverage_.contains(coord) || pending_lighting_coverage_.contains(coord);
}

auto World::lighting_anchor_affects(const ChunkCoord& target, const ChunkCoord& anchor) const noexcept -> bool {
    const auto dx = std::abs(target.x - anchor.x);
    const auto dz = std::abs(target.z - anchor.z);
    return (dx == 0 && dz == 0) || (dx == 1 && dz == 0) || (dx == 0 && dz == 1);
}

} // namespace valcraft
