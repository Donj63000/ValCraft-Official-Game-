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

constexpr std::array<BlockCoord, 6> kPressureNeighborOffsets {{
    {1, 0, 0},
    {-1, 0, 0},
    {0, 0, 1},
    {0, 0, -1},
    {0, 1, 0},
    {0, -1, 0},
}};

constexpr std::uint8_t kWaterVerticalFlowUnitsPerStep = 2U;
constexpr std::uint8_t kWaterHorizontalFlowUnitsPerStep = 1U;
constexpr std::uint8_t kWaterPressureRiseUnitsPerStep = 1U;
constexpr std::size_t kPressureSearchVisitLimit = 16384U;

constexpr auto kUnlimitedBudget = std::numeric_limits<std::size_t>::max() / 4U;
constexpr auto kSkyColumnCount = static_cast<std::size_t>(kChunkSizeX * kChunkSizeZ);
constexpr auto kMeshInvalidationPriorityRadius = 2;
constexpr std::size_t kLightingTimeCheckInterval = 128;
constexpr std::uint8_t kLightingBoundaryNegX = 1U << 0U;
constexpr std::uint8_t kLightingBoundaryPosX = 1U << 1U;
constexpr std::uint8_t kLightingBoundaryNegZ = 1U << 2U;
constexpr std::uint8_t kLightingBoundaryPosZ = 1U << 3U;
constexpr std::uint8_t kLightingBoundaryAll =
    kLightingBoundaryNegX | kLightingBoundaryPosX | kLightingBoundaryNegZ | kLightingBoundaryPosZ;

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

auto is_finite_vec3(const glm::vec3& value) noexcept -> bool {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

auto sky_column_index(int local_x, int local_z) noexcept -> std::size_t {
    return static_cast<std::size_t>(local_z * kChunkSizeX + local_x);
}

auto chunk_linear_index(int local_x, int local_y, int local_z) noexcept -> std::size_t {
    return static_cast<std::size_t>((local_y * kChunkSizeZ + local_z) * kChunkSizeX + local_x);
}

auto lighting_boundary_mask_for_column(int local_x, int local_z) noexcept -> std::uint8_t {
    std::uint8_t mask = 0;
    if (local_x == 0) {
        mask |= kLightingBoundaryNegX;
    }
    if (local_x == kChunkSizeX - 1) {
        mask |= kLightingBoundaryPosX;
    }
    if (local_z == 0) {
        mask |= kLightingBoundaryNegZ;
    }
    if (local_z == kChunkSizeZ - 1) {
        mask |= kLightingBoundaryPosZ;
    }
    return mask;
}

auto lighting_boundary_mask_touches_neighbor(std::uint8_t boundary_mask, const ChunkCoord& neighbor_offset) noexcept -> bool {
    if (boundary_mask == 0) {
        return false;
    }

    const auto touches_neg_x = (boundary_mask & kLightingBoundaryNegX) != 0;
    const auto touches_pos_x = (boundary_mask & kLightingBoundaryPosX) != 0;
    const auto touches_neg_z = (boundary_mask & kLightingBoundaryNegZ) != 0;
    const auto touches_pos_z = (boundary_mask & kLightingBoundaryPosZ) != 0;

    if (neighbor_offset.x < 0 && neighbor_offset.z == 0) {
        return touches_neg_x;
    }
    if (neighbor_offset.x > 0 && neighbor_offset.z == 0) {
        return touches_pos_x;
    }
    if (neighbor_offset.x == 0 && neighbor_offset.z < 0) {
        return touches_neg_z;
    }
    if (neighbor_offset.x == 0 && neighbor_offset.z > 0) {
        return touches_pos_z;
    }
    if (neighbor_offset.x < 0 && neighbor_offset.z < 0) {
        return touches_neg_x && touches_neg_z;
    }
    if (neighbor_offset.x < 0 && neighbor_offset.z > 0) {
        return touches_neg_x && touches_pos_z;
    }
    if (neighbor_offset.x > 0 && neighbor_offset.z < 0) {
        return touches_pos_x && touches_neg_z;
    }
    if (neighbor_offset.x > 0 && neighbor_offset.z > 0) {
        return touches_pos_x && touches_pos_z;
    }
    return false;
}

auto section_min_y(std::size_t section_index) noexcept -> int {
    return static_cast<int>(section_index * static_cast<std::size_t>(kChunkSectionHeight));
}

auto section_max_y(std::size_t section_index) noexcept -> int {
    return std::min(kWorldMaxY, section_min_y(section_index) + kChunkSectionHeight - 1);
}

auto water_state_after_receiving(WaterState previous_state, std::uint8_t level) noexcept -> WaterState {
    if (level == 0U) {
        return 0;
    }

    const auto infinite = water_state_is_infinite(previous_state);
    const auto source = water_state_is_source(previous_state) || infinite;
    return make_water_state(level, source, infinite);
}

auto expand_section_mask(const std::bitset<kChunkSectionCount>& sections) noexcept -> std::bitset<kChunkSectionCount> {
    auto expanded = sections;
    for (std::size_t section_index = 0; section_index < kChunkSectionCount; ++section_index) {
        if (!sections.test(section_index)) {
            continue;
        }
        if (section_index > 0) {
            expanded.set(section_index - 1);
        }
        if (section_index + 1 < kChunkSectionCount) {
            expanded.set(section_index + 1);
        }
    }
    return expanded;
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
    : stream_radius_(std::clamp(stream_radius, 0, kMaxStreamRadius)),
      generator_(seed) {
    const auto loaded_side = static_cast<std::size_t>(std::max(stream_radius_, 0) * 2 + 3);
    const auto max_loaded_chunks = loaded_side * loaded_side;
    chunks_.reserve(max_loaded_chunks);
    chunk_overrides_.reserve(max_loaded_chunks);
    pending_generation_set_.reserve(max_loaded_chunks);
    pending_fluid_set_.reserve(max_loaded_chunks * static_cast<std::size_t>(kChunkHeight / 4));
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
        const auto override_iterator = chunk_overrides_.find(chunk_coord);
        if (override_iterator == chunk_overrides_.end()) {
            return to_block_id(BlockType::Air);
        }
        return override_iterator->second.blocks[chunk_linear_index(local.x, local.y, local.z)];
    }
    return chunk->get_local(local.x, local.y, local.z);
}

auto World::raw_water_state(int x, int y, int z) const -> WaterState {
    if (!is_world_y_valid(y)) {
        return 0;
    }

    const auto chunk_coord = world_to_chunk(x, z);
    const auto local = world_to_local(x, y, z);
    const auto* chunk = find_chunk(chunk_coord);
    if (chunk == nullptr) {
        const auto override_iterator = chunk_overrides_.find(chunk_coord);
        if (override_iterator == chunk_overrides_.end()) {
            return 0;
        }
        return override_iterator->second.water_state[chunk_linear_index(local.x, local.y, local.z)];
    }
    return chunk->get_water_state_local(local.x, local.y, local.z);
}

auto World::water_level(int x, int y, int z) const -> std::uint8_t {
    return water_level_from_state(raw_water_state(x, y, z));
}

auto World::has_water(int x, int y, int z) const -> bool {
    return water_level(x, y, z) > 0;
}

auto World::water_surface_y(int x, int y, int z) const -> std::optional<float> {
    const auto level = water_level(x, y, z);
    if (level == 0) {
        return std::nullopt;
    }

    const auto top_height = has_water(x, y + 1, z) ? 1.0F : static_cast<float>(level) / static_cast<float>(kMaxWaterLevel);
    return static_cast<float>(y) + top_height;
}

auto World::peek_block_or_generated(int x, int y, int z) const -> BlockId {
    if (!is_world_y_valid(y)) {
        return to_block_id(BlockType::Air);
    }

    const auto chunk_coord = world_to_chunk(x, z);
    const auto local = world_to_local(x, y, z);
    const auto* chunk = find_chunk(chunk_coord);
    if (chunk != nullptr) {
        return chunk->get_local(local.x, local.y, local.z);
    }

    const auto override_iterator = chunk_overrides_.find(chunk_coord);
    if (override_iterator != chunk_overrides_.end()) {
        return override_iterator->second.blocks[chunk_linear_index(local.x, local.y, local.z)];
    }

    return generator_.sample_block(x, y, z);
}

auto World::peek_water_level_or_generated(int x, int y, int z) const -> std::uint8_t {
    if (!is_world_y_valid(y)) {
        return 0;
    }

    const auto chunk_coord = world_to_chunk(x, z);
    const auto local = world_to_local(x, y, z);
    const auto* chunk = find_chunk(chunk_coord);
    if (chunk != nullptr) {
        return chunk->water_level_local(local.x, local.y, local.z);
    }

    const auto override_iterator = chunk_overrides_.find(chunk_coord);
    if (override_iterator != chunk_overrides_.end()) {
        return water_level_from_state(override_iterator->second.water_state[chunk_linear_index(local.x, local.y, local.z)]);
    }

    return water_level_from_state(generator_.sample_water_state(x, y, z));
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
    const auto current_water_state = chunk.get_water_state_local(local.x, local.y, local.z);

    auto next_block = current_block;
    auto next_water_state = current_water_state;
    if (block_id == to_block_id(BlockType::Water)) {
        if (current_block != to_block_id(BlockType::Air) &&
            !is_block_replaceable(current_block) &&
            !is_torch_block(current_block)) {
            return;
        }
        next_block = to_block_id(BlockType::Air);
        next_water_state = make_water_state(kMaxWaterLevel, true);
    } else {
        next_block = block_id;
        next_water_state = 0;
    }

    if (current_block == next_block && current_water_state == next_water_state) {
        return;
    }

    if (current_block != next_block) {
        chunk.set_local(local.x, local.y, local.z, next_block);
    }
    if (current_water_state != next_water_state) {
        chunk.set_water_state_local(local.x, local.y, local.z, next_water_state);
    }

    update_chunk_override_after_cell_change(
        chunk_coord,
        local,
        current_block,
        next_block,
        current_water_state,
        next_water_state);

    if (current_block != next_block) {
        update_chunk_emissive_cache(record, local, current_block, next_block);
        mark_sky_column_dirty(chunk_coord, local.x, local.z);
        mark_chunk_and_neighbors_lighting_dirty(chunk_coord);
        remove_unsupported_torches_around(x, y, z);
    }

    mark_chunk_and_neighbors_dirty(chunk_coord, local);
    enqueue_fluid_cell({x, y, z});
    enqueue_adjacent_fluid_cells({x, y, z});
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
    if (!is_finite_vec3(origin) || !is_finite_vec3(direction) || !std::isfinite(max_distance) || max_distance <= 0.0F) {
        return {};
    }

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
            0.0F,
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
                travelled,
            };
        }

        if (has_water(current.x, current.y, current.z)) {
            return {
                true,
                current,
                previous,
                to_block_id(BlockType::Water),
                travelled,
            };
        }
    }

    return {};
}

auto World::can_place_torch_at(const BlockCoord& world_coord) const -> bool {
    if (!is_world_y_valid(world_coord.y)) {
        return false;
    }

    const auto current_block = get_block(world_coord.x, world_coord.y, world_coord.z);
    if (is_torch_block(current_block)) {
        const auto support_offset = torch_support_offset(current_block);
        const BlockCoord support_coord {
            world_coord.x + support_offset.x,
            world_coord.y + support_offset.y,
            world_coord.z + support_offset.z,
        };
        return can_place_torch_at(world_coord, support_coord);
    }

    constexpr std::array<BlockCoord, 5> support_offsets {{
        {0, -1, 0},
        {1, 0, 0},
        {-1, 0, 0},
        {0, 0, 1},
        {0, 0, -1},
    }};

    for (const auto& support_offset : support_offsets) {
        const BlockCoord support_coord {
            world_coord.x + support_offset.x,
            world_coord.y + support_offset.y,
            world_coord.z + support_offset.z,
        };
        if (can_place_torch_at(world_coord, support_coord)) {
            return true;
        }
    }

    return false;
}

auto World::can_place_torch_at(const BlockCoord& world_coord, const BlockCoord& support_coord) const -> bool {
    if (!is_world_y_valid(world_coord.y)) {
        return false;
    }

    const auto current_block = get_block(world_coord.x, world_coord.y, world_coord.z);
    if (has_water(world_coord.x, world_coord.y, world_coord.z)) {
        return false;
    }
    if (!is_torch_block(current_block) &&
        current_block != to_block_id(BlockType::Air) &&
        (!is_block_replaceable(current_block) || is_block_liquid(current_block))) {
        return false;
    }

    const BlockCoord support_offset {
        support_coord.x - world_coord.x,
        support_coord.y - world_coord.y,
        support_coord.z - world_coord.z,
    };
    const auto expected_torch_block = torch_block_from_support_offset(support_offset);
    if (!is_torch_block(expected_torch_block)) {
        return false;
    }
    if (is_torch_block(current_block) && current_block != expected_torch_block) {
        return false;
    }

    const auto support_block = get_block(support_coord.x, support_coord.y, support_coord.z);
    return is_block_opaque(support_block);
}

auto World::torch_block_to_place(const BlockCoord& world_coord, const BlockCoord& support_coord) const
    -> std::optional<BlockId> {
    if (!is_world_y_valid(world_coord.y)) {
        return std::nullopt;
    }

    const auto current_block = get_block(world_coord.x, world_coord.y, world_coord.z);
    if (has_water(world_coord.x, world_coord.y, world_coord.z)) {
        return std::nullopt;
    }
    if (current_block != to_block_id(BlockType::Air) &&
        (!is_block_replaceable(current_block) || is_block_liquid(current_block))) {
        return std::nullopt;
    }

    if (!can_place_torch_at(world_coord, support_coord)) {
        return std::nullopt;
    }

    const BlockCoord support_offset {
        support_coord.x - world_coord.x,
        support_coord.y - world_coord.y,
        support_coord.z - world_coord.z,
    };
    return torch_block_from_support_offset(support_offset);
}

void World::ensure_chunk_loaded(const ChunkCoord& coord) {
    load_chunk_immediate(coord);
}

auto World::update_streaming(const glm::vec3& player_position) -> WorldStreamingStats {
    WorldStreamingStats stats {};
    if (!is_finite_vec3(player_position)) {
        return stats;
    }

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

    // Je rescane les chunks sales a chaque tick de travail : certains ne
    // peuvent pas etre mis en file tant que leur region d'eclairage chevauche
    // un job deja planifie.
    enqueue_dirty_chunks();

    const auto generation_start = clock::now();
    process_generation_queue(budget.chunk_generation_budget, budget.max_generation_ms, stats);
    stats.generation_ms =
        std::chrono::duration<double, std::milli>(clock::now() - generation_start).count();

    const auto fluid_start = clock::now();
    process_fluid_queue(budget.fluid_cell_budget, budget.max_fluid_ms, stats);
    stats.fluid_ms =
        std::chrono::duration<double, std::milli>(clock::now() - fluid_start).count();

    // La generation et les fluides peuvent salir d'autres chunks. Je mets en
    // file ceux qui sont eligibles avant de lancer l'eclairage.
    enqueue_dirty_chunks();

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
    stats.pending_fluid = pending_fluid_count();
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

void World::enqueue_loaded_mesh_uploads() {
    for (const auto& [coord, record] : chunks_) {
        if (record.mesh_revision == 0) {
            continue;
        }

        enqueue_gpu_upload(coord);
    }
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

auto World::pending_fluid_count() const noexcept -> std::size_t {
    return pending_fluid_queue_.size();
}

auto World::pending_mesh_count() const noexcept -> std::size_t {
    return pending_mesh_set_.size();
}

auto World::pending_lighting_count() const noexcept -> std::size_t {
    return pending_lighting_queue_.size() + (active_lighting_job_.has_value() ? 1U : 0U);
}

auto World::has_pending_work() const noexcept -> bool {
    if (!pending_generation_queue_.empty() ||
        !pending_fluid_queue_.empty() ||
        !pending_mesh_set_.empty() ||
        !pending_lighting_queue_.empty() ||
        active_lighting_job_.has_value()) {
        return true;
    }

    return std::any_of(chunks_.begin(), chunks_.end(), [](const auto& entry) {
        return entry.second.chunk.is_lighting_dirty();
    });
}

auto World::are_chunks_ready(const glm::vec3& player_position, int radius) const -> bool {
    if (!is_finite_vec3(player_position) || radius < 0) {
        return false;
    }

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

auto World::modified_chunk_snapshots() const -> std::vector<WorldChunkSnapshot> {
    std::vector<WorldChunkSnapshot> snapshots;
    snapshots.reserve(chunk_overrides_.size());

    for (const auto& [coord, override_entry] : chunk_overrides_) {
        if (override_entry.generator_mismatch_count == 0) {
            continue;
        }

        if (const auto loaded_iterator = chunks_.find(coord); loaded_iterator != chunks_.end()) {
            snapshots.push_back({coord, loaded_iterator->second.chunk.blocks(), loaded_iterator->second.chunk.water_state()});
            continue;
        }

        snapshots.push_back({coord, override_entry.blocks, override_entry.water_state});
    }

    return snapshots;
}

void World::replace_chunk_snapshots(const std::vector<WorldChunkSnapshot>& snapshots) {
    chunk_overrides_.clear();
    for (const auto& snapshot : snapshots) {
        const auto mismatch_count = count_generator_mismatches(snapshot.coord, snapshot.blocks, snapshot.water_state);
        if (mismatch_count == 0) {
            continue;
        }
        chunk_overrides_[snapshot.coord] = {snapshot.blocks, snapshot.water_state, mismatch_count};
    }

    for (auto& [coord, record] : chunks_) {
        const auto iterator = chunk_overrides_.find(coord);
        if (iterator == chunk_overrides_.end()) {
            continue;
        }
        apply_chunk_snapshot_to_record(record, iterator->second.blocks, iterator->second.water_state);
        enqueue_lighting_update(coord);
        invalidate_loaded_mesh_neighbors_for_chunk_load(coord);
        apply_chunk_load_fluid_revalidation(coord);
    }
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
    iterator->second.chunk.clear_lighting();
    if (const auto override_iterator = chunk_overrides_.find(coord); override_iterator != chunk_overrides_.end()) {
        apply_chunk_snapshot_to_record(iterator->second, override_iterator->second.blocks, override_iterator->second.water_state);
    } else {
        refresh_chunk_emissive_cache(iterator->second);
    }
    iterator->second.sky_columns_dirty.set();
    iterator->second.chunk.mark_dirty();
    iterator->second.chunk.mark_lighting_dirty();
    enqueue_lighting_update(coord);
    invalidate_loaded_mesh_neighbors_for_chunk_load(coord);
    apply_chunk_load_fluid_revalidation(coord);
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
    const std::bitset<kChunkSectionCount>& dirty_sections,
    std::uint8_t boundary_mask) {
    if (dirty_sections.none() || boundary_mask == 0) {
        return;
    }

    for (const auto& offset : kMeshNeighborOffsets) {
        if (!lighting_boundary_mask_touches_neighbor(boundary_mask, offset)) {
            continue;
        }

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

void World::enqueue_fluid_cell(const BlockCoord& world_coord) {
    if (!is_world_y_valid(world_coord.y) || !is_chunk_loaded_for_world(world_coord.x, world_coord.z)) {
        return;
    }
    if (!pending_fluid_set_.insert(world_coord).second) {
        return;
    }
    pending_fluid_queue_.push_back(world_coord);
}

void World::enqueue_adjacent_fluid_cells(const BlockCoord& world_coord) {
    constexpr std::array<BlockCoord, 7> neighbor_offsets {{
        {0, 0, 0},
        {1, 0, 0},
        {-1, 0, 0},
        {0, 0, 1},
        {0, 0, -1},
        {0, 1, 0},
        {0, -1, 0},
    }};

    for (const auto& offset : neighbor_offsets) {
        enqueue_fluid_cell({
            world_coord.x + offset.x,
            world_coord.y + offset.y,
            world_coord.z + offset.z,
        });
    }
}

void World::enqueue_chunk_fluid_updates(const ChunkCoord& coord) {
    const auto* chunk = find_chunk(coord);
    if (chunk == nullptr) {
        return;
    }

    for (int y = kWorldMinY; y <= kWorldMaxY; ++y) {
        for (int z = 0; z < kChunkSizeZ; ++z) {
            for (int x = 0; x < kChunkSizeX; ++x) {
                if (!chunk->has_water_local(x, y, z)) {
                    continue;
                }

                const BlockCoord world_coord {
                    coord.x * kChunkSizeX + x,
                    y,
                    coord.z * kChunkSizeZ + z,
                };
                if (can_water_flow_into_loaded(world_coord.x, world_coord.y - 1, world_coord.z) ||
                    can_water_flow_into_loaded(world_coord.x + 1, world_coord.y, world_coord.z) ||
                    can_water_flow_into_loaded(world_coord.x - 1, world_coord.y, world_coord.z) ||
                    can_water_flow_into_loaded(world_coord.x, world_coord.y, world_coord.z + 1) ||
                    can_water_flow_into_loaded(world_coord.x, world_coord.y, world_coord.z - 1)) {
                    enqueue_fluid_cell(world_coord);
                }
            }
        }
    }
}

void World::apply_chunk_load_fluid_revalidation(const ChunkCoord& coord) {
    enqueue_chunk_fluid_updates(coord);
    for (const auto& offset : kNeighborOffsets) {
        enqueue_chunk_fluid_updates({coord.x + offset.x, coord.z + offset.z});
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
        if (pending_mesh_set_.contains(coord)) {
            pending_mesh_queue_.erase(
                std::remove(pending_mesh_queue_.begin(), pending_mesh_queue_.end(), coord),
                pending_mesh_queue_.end());
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

void World::process_fluid_queue(std::size_t budget, double max_ms, WorldWorkStats& stats) {
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

    // Je garde le calcul de pression local au batch: une grande nappe d'eau
    // connectee a la mer ne doit pas refaire la meme recherche pour chaque
    // cellule traitee pendant la frame.
    std::unordered_map<BlockCoord, WaterPressureHead, BlockCoordHash> pressure_head_cache {};
    std::unordered_set<BlockCoord, BlockCoordHash> pressure_head_missing_cache {};
    pressure_head_cache.reserve(512U);
    pressure_head_missing_cache.reserve(128U);

    while (remaining > 0 && !pending_fluid_queue_.empty()) {
        if (time_limited && clock::now() >= deadline) {
            break;
        }

        const auto world_coord = pending_fluid_queue_.front();
        pending_fluid_queue_.pop_front();
        pending_fluid_set_.erase(world_coord);
        --remaining;
        ++stats.processed_fluid_cells;

        if (!is_world_y_valid(world_coord.y) || !is_chunk_loaded_for_world(world_coord.x, world_coord.z)) {
            continue;
        }

        auto current_state = raw_water_state(world_coord.x, world_coord.y, world_coord.z);
        auto current_level = water_level_from_state(current_state);
        if (current_level == 0) {
            continue;
        }

        auto current_is_infinite = is_infinite_water_source(world_coord, current_state);
        if (current_is_infinite && current_level != kMaxWaterLevel) {
            current_state = make_water_state(kMaxWaterLevel, true, true);
            (void)set_water_state(world_coord.x, world_coord.y, world_coord.z, current_state);
            current_level = kMaxWaterLevel;
        }

        const BlockCoord below {
            world_coord.x,
            world_coord.y - 1,
            world_coord.z,
        };

        std::array<BlockCoord, 8> changed_cells {};
        std::size_t changed_cell_count = 0;
        const auto remember_change = [&](const BlockCoord& changed_coord) {
            if (changed_cell_count >= changed_cells.size()) {
                return;
            }
            if (std::find(changed_cells.begin(), changed_cells.begin() + static_cast<std::ptrdiff_t>(changed_cell_count), changed_coord) !=
                changed_cells.begin() + static_cast<std::ptrdiff_t>(changed_cell_count)) {
                return;
            }
            changed_cells[changed_cell_count++] = changed_coord;
        };

        const auto write_current_level = [&](std::uint8_t level) {
            current_level = level;
            current_state = level > 0U ? make_water_state(level) : 0;
            if (set_water_state(world_coord.x, world_coord.y, world_coord.z, current_state)) {
                remember_change(world_coord);
            }
        };

        const auto receive_water = [&](const BlockCoord& target,
                                       WaterState target_state,
                                       std::uint8_t transfer) -> bool {
            if (transfer == 0U) {
                return false;
            }
            const auto target_level = water_level_from_state(target_state);
            const auto next_level = static_cast<std::uint8_t>(
                std::min<int>(kMaxWaterLevel, static_cast<int>(target_level) + static_cast<int>(transfer)));
            if (next_level == target_level) {
                return false;
            }

            const auto next_state = water_state_after_receiving(target_state, next_level);
            if (set_water_state(target.x, target.y, target.z, next_state)) {
                remember_change(target);
                return true;
            }
            return false;
        };

        const auto drain_current = [&](std::uint8_t transfer) {
            if (transfer == 0U || current_is_infinite) {
                return;
            }

            const auto next_level = static_cast<std::uint8_t>(current_level > transfer ? current_level - transfer : 0U);
            write_current_level(next_level);
        };

        const auto enqueue_changes = [&]() {
            if (changed_cell_count == 0U) {
                return;
            }
            enqueue_adjacent_fluid_cells(world_coord);
            for (std::size_t index = 0; index < changed_cell_count; ++index) {
                enqueue_adjacent_fluid_cells(changed_cells[index]);
            }
        };

        auto vertical_transfer_happened = false;
        if (can_water_flow_into_loaded(below.x, below.y, below.z) && try_prepare_cell_for_water(below.x, below.y, below.z)) {
            const auto below_state = raw_water_state(below.x, below.y, below.z);
            const auto below_level = water_level_from_state(below_state);
            if (below_level < kMaxWaterLevel) {
                const auto available_level = current_is_infinite ? kMaxWaterLevel : current_level;
                const auto transfer = std::min<std::uint8_t>(
                    std::min<std::uint8_t>(kWaterVerticalFlowUnitsPerStep, available_level),
                    static_cast<std::uint8_t>(kMaxWaterLevel - below_level));
                if (receive_water(below, below_state, transfer)) {
                    drain_current(transfer);
                    vertical_transfer_happened = true;
                }
            }
        }

        if (vertical_transfer_happened) {
            if (current_is_infinite) {
                current_state = make_water_state(kMaxWaterLevel, true, true);
                (void)set_water_state(world_coord.x, world_coord.y, world_coord.z, current_state);
                current_level = kMaxWaterLevel;
            }
            enqueue_changes();
            continue;
        }

        auto horizontal_pressure_head = std::optional<WaterPressureHead> {};
        if (current_level == kMaxWaterLevel) {
            horizontal_pressure_head = pressure_head_y_for(
                world_coord,
                current_state,
                pressure_head_cache,
                pressure_head_missing_cache);
        }

        for (const auto& offset : kNeighborOffsets) {
            if (current_level == 0U && !current_is_infinite) {
                break;
            }

            const BlockCoord neighbor {
                world_coord.x + offset.x,
                world_coord.y,
                world_coord.z + offset.z,
            };
            if (!can_water_flow_into_loaded(neighbor.x, neighbor.y, neighbor.z)) {
                continue;
            }
            if (!try_prepare_cell_for_water(neighbor.x, neighbor.y, neighbor.z)) {
                continue;
            }

            const auto neighbor_state = raw_water_state(neighbor.x, neighbor.y, neighbor.z);
            const auto neighbor_level = water_level_from_state(neighbor_state);
            const auto available_level = current_is_infinite ? kMaxWaterLevel : current_level;
            const auto has_infinite_pressure_push = current_level == kMaxWaterLevel &&
                                                    horizontal_pressure_head.has_value() &&
                                                    horizontal_pressure_head->infinite &&
                                                    horizontal_pressure_head->y >= world_coord.y;
            const auto has_finite_pressure_push = current_level == kMaxWaterLevel &&
                                                  horizontal_pressure_head.has_value() &&
                                                  !horizontal_pressure_head->infinite &&
                                                  horizontal_pressure_head->y > world_coord.y;
            const auto has_pressure_push = has_infinite_pressure_push || has_finite_pressure_push;
            if (current_is_infinite || has_pressure_push) {
                if (neighbor_level >= kMaxWaterLevel) {
                    continue;
                }
            } else if (available_level <= neighbor_level + 1U) {
                continue;
            }

            const auto transfer = std::min<std::uint8_t>(
                std::min<std::uint8_t>(kWaterHorizontalFlowUnitsPerStep, available_level),
                static_cast<std::uint8_t>(kMaxWaterLevel - neighbor_level));
            if (receive_water(neighbor, neighbor_state, transfer)) {
                if (!current_is_infinite && !has_infinite_pressure_push) {
                    drain_current(transfer);
                }
            }
        }

        current_state = raw_water_state(world_coord.x, world_coord.y, world_coord.z);
        current_level = water_level_from_state(current_state);
        current_is_infinite = is_infinite_water_source(world_coord, current_state);
        if (current_level == kMaxWaterLevel) {
            const auto pressure_head = pressure_head_y_for(
                world_coord,
                current_state,
                pressure_head_cache,
                pressure_head_missing_cache);
            if (pressure_head.has_value() && pressure_head->y > world_coord.y && world_coord.y < kSeaLevel) {
                const BlockCoord above {
                    world_coord.x,
                    world_coord.y + 1,
                    world_coord.z,
                };
                if (can_water_flow_into_loaded(above.x, above.y, above.z) &&
                    try_prepare_cell_for_water(above.x, above.y, above.z)) {
                    const auto above_state = raw_water_state(above.x, above.y, above.z);
                    const auto above_level = water_level_from_state(above_state);
                    if (above_level < kMaxWaterLevel) {
                        const auto transfer = std::min<std::uint8_t>(
                            kWaterPressureRiseUnitsPerStep,
                            static_cast<std::uint8_t>(kMaxWaterLevel - above_level));
                        if (receive_water(above, above_state, transfer)) {
                            if (!pressure_head->infinite) {
                                drain_current(transfer);
                            }
                        }
                    }
                }
            }
        }

        if (current_is_infinite) {
            const auto full_infinite_state = make_water_state(kMaxWaterLevel, true, true);
            if (raw_water_state(world_coord.x, world_coord.y, world_coord.z) != full_infinite_state) {
                (void)set_water_state(world_coord.x, world_coord.y, world_coord.z, full_infinite_state);
                remember_change(world_coord);
            }
        }

        enqueue_changes();
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
    job.block_light_difference_counts.fill(0U);
    job.changed_sky_boundary_masks.fill(0U);
    for (auto& changed_sections : job.changed_sky_sections) {
        changed_sections.reset();
    }
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
            const auto changed_sections = chunk.rebuild_sky_light_column(local_x, local_z);
            if (changed_sections.any()) {
                job.changed_sky_sections[slot_index] |= changed_sections;
                job.changed_sky_boundary_masks[slot_index] |= lighting_boundary_mask_for_column(local_x, local_z);
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
        auto changed_sections = job.changed_sky_sections[slot_index];
        const auto sky_boundary_mask = job.changed_sky_boundary_masks[slot_index];
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
                if (section_changed) {
                    changed_sections.set(section_index);
                }
            }
            record.chunk.copy_block_light_from(job.block_light_buffers[slot_index].data(), job.block_light_buffers[slot_index].size());
        }

        const auto lighting_changed = changed_sections.any();
        if (lighting_changed) {
            const auto expanded_sections = expand_section_mask(changed_sections);
            for (std::size_t section_index = 0; section_index < kChunkSectionCount; ++section_index) {
                if (expanded_sections.test(section_index)) {
                    record.chunk.mark_section_dirty(section_index);
                }
            }
            // Je ne remeshe les voisins que si la lumiere a vraiment touche
            // une bordure de chunk, sauf pour la block light ou je garde la
            // voie conservative pour eviter toute regression d'eclairage.
            const auto neighbor_boundary_mask = block_light_changed ? kLightingBoundaryAll : sky_boundary_mask;
            invalidate_loaded_mesh_neighbors_for_sections(coord, changed_sections, neighbor_boundary_mask);
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
        const auto record_iterator = chunks_.find(coord);
        if (record_iterator != chunks_.end()) {
            sync_chunk_override_snapshot(coord, record_iterator->second.chunk);
        }
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

    std::deque<BlockCoord> kept_fluid_updates;
    while (!pending_fluid_queue_.empty()) {
        const auto world_coord = pending_fluid_queue_.front();
        pending_fluid_queue_.pop_front();
        if (!is_chunk_loaded_for_world(world_coord.x, world_coord.z)) {
            continue;
        }
        kept_fluid_updates.push_back(world_coord);
    }
    pending_fluid_queue_ = std::move(kept_fluid_updates);
    pending_fluid_set_.clear();
    for (const auto& world_coord : pending_fluid_queue_) {
        pending_fluid_set_.insert(world_coord);
    }

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

    std::size_t merged_vertex_count = 0;
    std::size_t merged_index_count = 0;
    std::size_t merged_water_vertex_count = 0;
    std::size_t merged_water_index_count = 0;
    for (const auto& section_mesh : record.section_meshes) {
        merged_vertex_count += section_mesh.vertices.size();
        merged_index_count += section_mesh.indices.size();
        merged_water_vertex_count += section_mesh.water_vertices.size();
        merged_water_index_count += section_mesh.water_indices.size();
    }

    ChunkMeshData merged_mesh {};
    // Je reserve la taille deja connue des sections pour eviter les reallocations pendant les pics de streaming.
    merged_mesh.vertices.reserve(std::max(record.mesh_vertex_capacity_hint, std::max<std::size_t>(merged_vertex_count, 256U)));
    merged_mesh.indices.reserve(std::max(record.mesh_index_capacity_hint, std::max<std::size_t>(merged_index_count, 384U)));
    merged_mesh.water_vertices.reserve(std::max<std::size_t>(merged_water_vertex_count, 128U));
    merged_mesh.water_indices.reserve(std::max<std::size_t>(merged_water_index_count, 192U));
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

void World::remove_unsupported_torches_around(int x, int y, int z) {
    constexpr std::array<BlockCoord, 6> neighbor_offsets {{
        {0, 1, 0},
        {1, 0, 0},
        {-1, 0, 0},
        {0, 0, 1},
        {0, 0, -1},
        {0, -1, 0},
    }};

    for (const auto& offset : neighbor_offsets) {
        const BlockCoord torch_coord {x + offset.x, y + offset.y, z + offset.z};
        if (!is_world_y_valid(torch_coord.y)) {
            continue;
        }

        const auto torch_block = get_block(torch_coord.x, torch_coord.y, torch_coord.z);
        if (!is_torch_block(torch_block) || can_place_torch_at(torch_coord)) {
            continue;
        }

        set_block(torch_coord.x, torch_coord.y, torch_coord.z, to_block_id(BlockType::Air));
    }
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

void World::sync_chunk_override_snapshot(const ChunkCoord& coord, const Chunk& chunk) {
    auto iterator = chunk_overrides_.find(coord);
    if (iterator == chunk_overrides_.end()) {
        return;
    }

    iterator->second.blocks = chunk.blocks();
    iterator->second.water_state = chunk.water_state();
    iterator->second.generator_mismatch_count =
        count_generator_mismatches(coord, iterator->second.blocks, iterator->second.water_state);
    if (iterator->second.generator_mismatch_count == 0) {
        chunk_overrides_.erase(iterator);
    }
}

void World::apply_chunk_snapshot_to_record(ChunkRecord& record,
                                           const std::array<BlockId, kChunkVolume>& blocks,
                                           const std::array<WaterState, kChunkVolume>& water_state) {
    auto normalized_water_state = water_state;
    const auto coord = record.chunk.coord();
    for (int y = kWorldMinY; y <= kWorldMaxY; ++y) {
        for (int z = 0; z < kChunkSizeZ; ++z) {
            for (int x = 0; x < kChunkSizeX; ++x) {
                const auto block_index = chunk_linear_index(x, y, z);
                const BlockCoord world_coord {
                    coord.x * kChunkSizeX + x,
                    y,
                    coord.z * kChunkSizeZ + z,
                };
                normalized_water_state[block_index] =
                    normalize_water_state_for_generated(world_coord, normalized_water_state[block_index]);
            }
        }
    }

    record.chunk.copy_blocks_from(blocks.data(), blocks.size());
    record.chunk.copy_water_from(normalized_water_state.data(), normalized_water_state.size());
    record.chunk.clear_lighting();
    record.sky_columns_dirty.set();
    refresh_chunk_emissive_cache(record);
}

auto World::count_generator_mismatches(const ChunkCoord& coord,
                                       const std::array<BlockId, kChunkVolume>& blocks,
                                       const std::array<WaterState, kChunkVolume>& water_state) const -> std::size_t {
    std::size_t mismatch_count = 0;
    for (int y = kWorldMinY; y <= kWorldMaxY; ++y) {
        for (int z = 0; z < kChunkSizeZ; ++z) {
            for (int x = 0; x < kChunkSizeX; ++x) {
                const auto block_index = chunk_linear_index(x, y, z);
                const auto world_x = coord.x * kChunkSizeX + x;
                const auto world_z = coord.z * kChunkSizeZ + z;
                if (blocks[block_index] != generator_.sample_block(world_x, y, world_z)) {
                    ++mismatch_count;
                }
                const BlockCoord world_coord {world_x, y, world_z};
                if (normalize_water_state_for_generated(world_coord, water_state[block_index]) !=
                    generator_.sample_water_state(world_x, y, world_z)) {
                    ++mismatch_count;
                }
            }
        }
    }
    return mismatch_count;
}

auto World::normalize_water_state_for_generated(const BlockCoord& world_coord, WaterState water_state) const -> WaterState {
    const auto generated_state = generator_.sample_water_state(world_coord.x, world_coord.y, world_coord.z);
    if (water_level_from_state(water_state) == kMaxWaterLevel &&
        water_state_is_source(water_state) &&
        water_state_is_infinite(generated_state)) {
        return make_water_state(kMaxWaterLevel, true, true);
    }
    return water_state;
}

auto World::is_chunk_loaded_for_world(int x, int z) const noexcept -> bool {
    return find_chunk(world_to_chunk(x, z)) != nullptr;
}

auto World::can_water_flow_into_loaded(int x, int y, int z) const -> bool {
    if (!is_world_y_valid(y) || !is_chunk_loaded_for_world(x, z)) {
        return false;
    }

    const auto block = get_block(x, y, z);
    return block == to_block_id(BlockType::Air) ||
           is_torch_block(block) ||
           is_block_replaceable(block) ||
           has_water(x, y, z);
}

auto World::is_infinite_water_source(const BlockCoord& world_coord, WaterState water_state) const -> bool {
    if (!is_world_y_valid(world_coord.y) || water_level_from_state(water_state) < kMaxWaterLevel) {
        return false;
    }

    if (water_state_is_infinite(water_state)) {
        return true;
    }

    const auto generated_state = generator_.sample_water_state(world_coord.x, world_coord.y, world_coord.z);
    return water_state_is_source(generated_state) && water_level_from_state(generated_state) == kMaxWaterLevel;
}

auto World::try_prepare_cell_for_water(int x, int y, int z) -> bool {
    if (!is_world_y_valid(y) || !is_chunk_loaded_for_world(x, z)) {
        return false;
    }

    const auto block = get_block(x, y, z);
    if (block == to_block_id(BlockType::Air) || has_water(x, y, z)) {
        return true;
    }
    if (!is_torch_block(block) && !is_block_replaceable(block)) {
        return false;
    }

    set_block(x, y, z, to_block_id(BlockType::Air));
    return get_block(x, y, z) == to_block_id(BlockType::Air);
}

auto World::pressure_head_y_for(
    const BlockCoord& world_coord,
    WaterState water_state,
    std::unordered_map<BlockCoord, WaterPressureHead, BlockCoordHash>& pressure_head_cache,
    std::unordered_set<BlockCoord, BlockCoordHash>& pressure_head_missing_cache) const -> std::optional<WaterPressureHead> {
    if (!is_world_y_valid(world_coord.y) ||
        !is_chunk_loaded_for_world(world_coord.x, world_coord.z) ||
        water_level_from_state(water_state) < kMaxWaterLevel) {
        pressure_head_missing_cache.insert(world_coord);
        return std::nullopt;
    }

    if (const auto cache_iterator = pressure_head_cache.find(world_coord); cache_iterator != pressure_head_cache.end()) {
        return cache_iterator->second;
    }
    if (pressure_head_missing_cache.contains(world_coord)) {
        return std::nullopt;
    }
    if (is_infinite_water_source(world_coord, water_state)) {
        const auto head_y = std::max(world_coord.y, kSeaLevel);
        const WaterPressureHead pressure_head {head_y, true};
        pressure_head_cache[world_coord] = pressure_head;
        return pressure_head;
    }

    std::vector<BlockCoord> frontier {};
    std::vector<BlockCoord> visited {};
    std::unordered_set<BlockCoord, BlockCoordHash> seen {};
    frontier.reserve(128U);
    visited.reserve(128U);
    seen.reserve(256U);

    frontier.push_back(world_coord);
    seen.insert(world_coord);

    auto head_y = world_coord.y;
    auto infinite = false;
    auto reached_search_limit = false;
    for (std::size_t frontier_index = 0; frontier_index < frontier.size(); ++frontier_index) {
        const auto current = frontier[frontier_index];
        visited.push_back(current);

        const auto current_state = raw_water_state(current.x, current.y, current.z);
        if (water_level_from_state(current_state) < kMaxWaterLevel) {
            continue;
        }

        head_y = std::max(head_y, current.y);
        if (is_infinite_water_source(current, current_state)) {
            head_y = std::max(head_y, kSeaLevel);
            infinite = true;
        }

        if (visited.size() >= kPressureSearchVisitLimit) {
            reached_search_limit = true;
            break;
        }

        for (const auto& offset : kPressureNeighborOffsets) {
            const BlockCoord neighbor {
                current.x + offset.x,
                current.y + offset.y,
                current.z + offset.z,
            };
            if (!is_world_y_valid(neighbor.y) || !is_chunk_loaded_for_world(neighbor.x, neighbor.z)) {
                continue;
            }
            if (!seen.insert(neighbor).second) {
                continue;
            }

            if (const auto cache_iterator = pressure_head_cache.find(neighbor); cache_iterator != pressure_head_cache.end()) {
                head_y = std::max(head_y, cache_iterator->second.y);
                infinite = infinite || cache_iterator->second.infinite;
                continue;
            }
            if (pressure_head_missing_cache.contains(neighbor)) {
                continue;
            }

            const auto neighbor_state = raw_water_state(neighbor.x, neighbor.y, neighbor.z);
            if (water_level_from_state(neighbor_state) < kMaxWaterLevel) {
                continue;
            }

            frontier.push_back(neighbor);
        }
    }

    if (!reached_search_limit) {
        const WaterPressureHead pressure_head {head_y, infinite};
        for (const auto& visited_coord : visited) {
            pressure_head_cache[visited_coord] = pressure_head;
        }
    }

    return WaterPressureHead {head_y, infinite};
}

auto World::set_water_state(int x, int y, int z, WaterState water_state) -> bool {
    if (!is_world_y_valid(y)) {
        return false;
    }

    const auto chunk_coord = world_to_chunk(x, z);
    if (!chunks_.contains(chunk_coord)) {
        return false;
    }

    const auto local = world_to_local(x, y, z);
    auto iterator = chunks_.find(chunk_coord);
    if (iterator == chunks_.end()) {
        return false;
    }

    auto& chunk = iterator->second.chunk;
    const auto previous_water_state = chunk.get_water_state_local(local.x, local.y, local.z);
    if (previous_water_state == water_state) {
        return false;
    }

    const auto current_block = chunk.get_local(local.x, local.y, local.z);
    chunk.set_water_state_local(local.x, local.y, local.z, water_state);
    update_chunk_override_after_cell_change(chunk_coord, local, current_block, current_block, previous_water_state, water_state);
    mark_chunk_and_neighbors_dirty(chunk_coord, local);
    return true;
}

void World::update_chunk_override_after_cell_change(const ChunkCoord& coord,
                                                    const BlockCoord& local_coord,
                                                    BlockId previous_block,
                                                    BlockId next_block,
                                                    WaterState previous_water_state,
                                                    WaterState next_water_state) {
    const auto block_index = chunk_linear_index(local_coord.x, local_coord.y, local_coord.z);
    const auto world_coord = local_to_world(coord, local_coord);
    const auto generated_block = generator_.sample_block(world_coord.x, world_coord.y, world_coord.z);
    const auto generated_water_state = generator_.sample_water_state(world_coord.x, world_coord.y, world_coord.z);

    if (auto override_iterator = chunk_overrides_.find(coord); override_iterator == chunk_overrides_.end()) {
        const auto next_mismatch = next_block != generated_block || next_water_state != generated_water_state;
        if (!next_mismatch) {
            return;
        }

        auto* chunk = find_chunk(coord);
        if (chunk == nullptr) {
            return;
        }

        ChunkOverrideEntry entry {};
        entry.blocks = chunk->blocks();
        entry.water_state = chunk->water_state();
        entry.generator_mismatch_count = count_generator_mismatches(coord, entry.blocks, entry.water_state);
        if (entry.generator_mismatch_count > 0) {
            chunk_overrides_.emplace(coord, std::move(entry));
        }
    } else {
        auto& entry = override_iterator->second;
        const auto previous_mismatch =
            previous_block != generated_block || previous_water_state != generated_water_state;
        const auto next_mismatch =
            next_block != generated_block || next_water_state != generated_water_state;
        entry.blocks[block_index] = next_block;
        entry.water_state[block_index] = next_water_state;
        if (previous_mismatch && !next_mismatch) {
            --entry.generator_mismatch_count;
        } else if (!previous_mismatch && next_mismatch) {
            ++entry.generator_mismatch_count;
        }
        if (entry.generator_mismatch_count == 0) {
            chunk_overrides_.erase(override_iterator);
        }
    }
}

auto World::lighting_region_contains(const LightingJob& job, const ChunkCoord& coord) const noexcept -> bool {
    const auto slot = lighting_region_slot_for(job.anchor, coord);
    if (slot == LightingRegionSlot::Invalid) {
        return false;
    }
    return job.region_present[lighting_region_slot_index(slot)];
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
